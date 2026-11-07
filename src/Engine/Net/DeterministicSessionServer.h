#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Engine/ECS/GenericWorldSnapshot.h"
#include "Engine/ECS/WorldSerializer.h"

namespace Engine::Net
{

struct SessionNetTelemetry
{
    uint64_t TotalTicksSimulated = 0;
    uint64_t TotalCommandsApplied = 0;
    uint64_t LateCommandsInjected = 0;
    uint64_t RollbacksExecuted = 0;
    uint64_t ResimulatedTicks = 0;
    uint64_t HashVerificationsPassed = 0;
    uint64_t DesyncsDetected = 0;
};

// ============================================================================
// DeterministicSessionServer (Phase 42):
// - Game-agnostic deterministic lockstep + rollback multiplayer & replay server
//   built on Engine::ECS::GenericRollbackBuffer and WorldSerializer.
// - Supports:
//   1. Tick-stamped command scheduling & deterministic ordering.
//   2. Automatic snapshot capture and late remote command rollback + resimulation.
//   3. Frame-by-frame FNV-1a state hash verification & desync detection.
//   4. Binary Replay recording, export, and deterministic playback.
// ============================================================================
template <typename CommandT, size_t kRollbackHistory = 128>
class DeterministicSessionServer
{
public:
    static_assert(std::is_trivially_copyable_v<CommandT>,
                  "DeterministicSessionServer requires a trivially copyable CommandT");

    struct FrameCommandEntry
    {
        uint32_t Tick = 0;
        uint8_t PlayerID = 0;
        CommandT Command{};
    };

    using StepSimulationFn = std::function<void(Core::ECS::EntityManager&,
                                                uint32_t Tick,
                                                const std::vector<FrameCommandEntry>& Commands)>;

    void BindSimulationStepCallback(StepSimulationFn Callback)
    {
        mStepCallback = std::move(Callback);
    }

    template <typename ResourceT>
    void RegisterRollbackResource()
    {
        mRollbackBuffer.template RegisterWorldResource<ResourceT>();
    }

    void ScheduleCommand(uint32_t Tick, uint8_t PlayerID, const CommandT& Cmd)
    {
        auto& Vec = mFrameCommands[Tick];
        Vec.push_back({Tick, PlayerID, Cmd});
        std::stable_sort(Vec.begin(), Vec.end(),
                         [](const FrameCommandEntry& A, const FrameCommandEntry& B) {
                             return A.PlayerID < B.PlayerID;
                         });
    }

    // Advances authoritative simulation by 1 tick, recording snapshot and state hash
    uint32_t AdvanceTick(Core::ECS::EntityManager& EM)
    {
        const uint32_t Tick = mCurrentTick;
        mRollbackBuffer.RecordTick(EM, Tick);

        const auto Commands = GetCommandsForTick(Tick);
        if (mStepCallback)
            mStepCallback(EM, Tick, Commands);

        mTelemetry.TotalCommandsApplied += Commands.size();
        ++mTelemetry.TotalTicksSimulated;

        // Compute deterministic post-tick state hash
        Engine::ECS::GenericWorldSnapshot PostSnap;
        PostSnap.Capture(EM, Tick + 1);
        const uint32_t Hash = PostSnap.ComputeBinaryHashFNV1a();
        mTickHashes[Tick] = Hash;

        ++mCurrentTick;
        return Hash;
    }

    // Injects a late remote command for a past tick (PastTick < mCurrentTick),
    // rolls back the ECS World to PastTick, and resimulates forward to mCurrentTick.
    bool InjectLateCommandAndResimulate(Core::ECS::EntityManager& EM,
                                        uint32_t PastTick,
                                        uint8_t PlayerID,
                                        const CommandT& Cmd)
    {
        if (PastTick >= mCurrentTick)
        {
            ScheduleCommand(PastTick, PlayerID, Cmd);
            return true;
        }

        if (!mRollbackBuffer.RollbackToTick(EM, PastTick))
            return false;

        ScheduleCommand(PastTick, PlayerID, Cmd);
        ++mTelemetry.LateCommandsInjected;
        ++mTelemetry.RollbacksExecuted;

        const uint32_t TargetTick = mCurrentTick;
        for (uint32_t T = PastTick; T < TargetTick; ++T)
        {
            mRollbackBuffer.RecordTick(EM, T);
            const auto Commands = GetCommandsForTick(T);
            if (mStepCallback)
                mStepCallback(EM, T, Commands);

            Engine::ECS::GenericWorldSnapshot PostSnap;
            PostSnap.Capture(EM, T + 1);
            mTickHashes[T] = PostSnap.ComputeBinaryHashFNV1a();
            ++mTelemetry.ResimulatedTicks;
        }
        return true;
    }

    bool VerifyRemoteTickHash(uint32_t Tick, uint32_t RemoteHash)
    {
        auto It = mTickHashes.find(Tick);
        if (It == mTickHashes.end())
            return false;
        if (It->second == RemoteHash)
        {
            ++mTelemetry.HashVerificationsPassed;
            return true;
        }
        ++mTelemetry.DesyncsDetected;
        return false;
    }

    uint32_t GetRecordedTickHash(uint32_t Tick) const
    {
        auto It = mTickHashes.find(Tick);
        return It != mTickHashes.end() ? It->second : 0u;
    }

    // Exports deterministic replay stream (header + all recorded FrameCommandEntries)
    bool ExportReplayBinary(std::vector<uint8_t>& OutBytes) const
    {
        OutBytes.clear();
        const uint32_t Magic = 0x52504C59u; // 'R','P','L','Y'
        const uint32_t TotalTicks = mCurrentTick;

        std::vector<FrameCommandEntry> Flat;
        for (const auto& [Tick, Cmds] : mFrameCommands)
        {
            for (const auto& Entry : Cmds)
                Flat.push_back(Entry);
        }

        Core::ECS::Detail::AppendPodBytes(OutBytes, Magic);
        Core::ECS::Detail::AppendPodBytes(OutBytes, TotalTicks);
        Core::ECS::Detail::AppendVectorPodBytes(OutBytes, Flat);
        return true;
    }

    bool ImportReplayBinary(const std::vector<uint8_t>& Bytes, uint32_t* OutTotalTicks = nullptr)
    {
        size_t Cursor = 0;
        uint32_t Magic = 0;
        uint32_t TotalTicks = 0;
        std::vector<FrameCommandEntry> Flat;

        if (!Core::ECS::Detail::ReadPodBytes(Bytes, Cursor, Magic) ||
            Magic != 0x52504C59u ||
            !Core::ECS::Detail::ReadPodBytes(Bytes, Cursor, TotalTicks) ||
            !Core::ECS::Detail::ReadVectorPodBytes(Bytes, Cursor, Flat))
        {
            return false;
        }

        mFrameCommands.clear();
        for (const auto& Entry : Flat)
            ScheduleCommand(Entry.Tick, Entry.PlayerID, Entry.Command);

        if (OutTotalTicks)
            *OutTotalTicks = TotalTicks;
        return true;
    }

    std::vector<FrameCommandEntry> GetCommandsForTick(uint32_t Tick) const
    {
        auto It = mFrameCommands.find(Tick);
        return It != mFrameCommands.end() ? It->second : std::vector<FrameCommandEntry>{};
    }

    uint32_t GetCurrentTick() const noexcept { return mCurrentTick; }
    void ResetSession()
    {
        mCurrentTick = 0;
        mFrameCommands.clear();
        mTickHashes.clear();
        mRollbackBuffer.Clear();
    }

    const SessionNetTelemetry& GetTelemetry() const noexcept { return mTelemetry; }

private:
    uint32_t mCurrentTick = 0;
    std::map<uint32_t, std::vector<FrameCommandEntry>> mFrameCommands;
    std::map<uint32_t, uint32_t> mTickHashes;
    Engine::ECS::GenericRollbackBuffer<kRollbackHistory> mRollbackBuffer;
    StepSimulationFn mStepCallback;
    SessionNetTelemetry mTelemetry{};
};

} // namespace Engine::Net
