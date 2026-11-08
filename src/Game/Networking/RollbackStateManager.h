#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Engine/ECS/GenericWorldSnapshot.h"
#include "Game/Networking/LockstepSync.h"

namespace Game::Networking
{

struct RollbackFrameEntry
{
    bool Valid = false;
    uint32_t FrameNumber = 0;
    Core::ECS::WorldDeterministicSnapshot Snapshot;
    std::vector<SyncAction> FrameActions;
};

struct RollbackTelemetry
{
    uint64_t TotalSnapshotsRecorded = 0;
    uint64_t TotalRollbacksExecuted = 0;
    uint64_t TotalResimulatedTicks = 0;
    uint64_t DesyncDetections = 0;
};

// ============================================================================
// RollbackStateManager (Phase 45 Streamlined Adapter):
// - Delegates snapshot capture/restore directly to the type-erased
//   Engine::ECS::GenericWorldSnapshot kernel via WorldSnapshotSerializer.
// ============================================================================
class RollbackStateManager
{
public:
    static constexpr size_t kMaxRollbackWindow = 64;

    using ActionApplyFn = std::function<void(Core::ECS::EntityManager&, const SyncAction&)>;
    using SimulationTickFn = std::function<void(Core::ECS::EntityManager&, uint32_t FrameNumber)>;

    void Clear()
    {
        for (auto& Slot : mRing)
            Slot = RollbackFrameEntry{};
        mTelemetry = {};
    }

    uint32_t RecordFrame(const Core::ECS::EntityManager& EM,
                         uint32_t FrameNumber,
                         std::vector<SyncAction> Actions = {})
    {
        auto& Slot = mRing[FrameNumber % kMaxRollbackWindow];
        Slot.Valid = true;
        Slot.FrameNumber = FrameNumber;
        Slot.Snapshot = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(EM, FrameNumber);
        Slot.FrameActions = std::move(Actions);
        ++mTelemetry.TotalSnapshotsRecorded;
        return Slot.Snapshot.StateHashFNV1a;
    }

    const RollbackFrameEntry* FindFrame(uint32_t FrameNumber) const
    {
        const auto& Slot = mRing[FrameNumber % kMaxRollbackWindow];
        return (Slot.Valid && Slot.FrameNumber == FrameNumber) ? &Slot : nullptr;
    }

    bool VerifyRemoteFrameHash(uint32_t FrameNumber, uint32_t RemoteHash)
    {
        const auto* Entry = FindFrame(FrameNumber);
        if (Entry && Entry->Snapshot.StateHashFNV1a != RemoteHash)
        {
            ++mTelemetry.DesyncDetections;
            return false;
        }
        return true;
    }

    bool InjectActionAndResimulate(Core::ECS::EntityManager& EM,
                                   uint32_t TargetFrame,
                                   uint32_t CurrentFrame,
                                   const SyncAction& InjectedAction,
                                   const ActionApplyFn& ApplyAction,
                                   const SimulationTickFn& SimulateTick)
    {
        auto* TargetSlot = FindFrameMutable(TargetFrame);
        if (!TargetSlot || TargetFrame > CurrentFrame)
            return false;

        TargetSlot->FrameActions.push_back(InjectedAction);
        std::stable_sort(TargetSlot->FrameActions.begin(),
                         TargetSlot->FrameActions.end(),
                         [](const SyncAction& A, const SyncAction& B) {
                             return A.PlayerID < B.PlayerID;
                         });
        return RollbackAndResimulate(EM, TargetFrame, CurrentFrame, ApplyAction, SimulateTick);
    }

    bool RollbackAndResimulate(Core::ECS::EntityManager& EM,
                               uint32_t RollbackFrame,
                               uint32_t CurrentFrame,
                               const ActionApplyFn& ApplyAction,
                               const SimulationTickFn& SimulateTick)
    {
        const auto* StartEntry = FindFrame(RollbackFrame);
        if (!StartEntry || RollbackFrame > CurrentFrame)
            return false;

        Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(EM, StartEntry->Snapshot);
        ++mTelemetry.TotalRollbacksExecuted;

        for (uint32_t F = RollbackFrame; F < CurrentFrame; ++F)
        {
            if (auto* Slot = FindFrameMutable(F))
            {
                Slot->Snapshot = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(EM, F);
                if (ApplyAction)
                {
                    for (const auto& Act : Slot->FrameActions)
                        ApplyAction(EM, Act);
                }
            }
            if (SimulateTick)
                SimulateTick(EM, F);
            ++mTelemetry.TotalResimulatedTicks;
        }

        if (auto* CurSlot = FindFrameMutable(CurrentFrame))
            CurSlot->Snapshot = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(EM, CurrentFrame);

        return true;
    }

    const RollbackTelemetry& GetTelemetry() const { return mTelemetry; }

private:
    RollbackFrameEntry* FindFrameMutable(uint32_t FrameNumber)
    {
        auto& Slot = mRing[FrameNumber % kMaxRollbackWindow];
        return (Slot.Valid && Slot.FrameNumber == FrameNumber) ? &Slot : nullptr;
    }

    std::array<RollbackFrameEntry, kMaxRollbackWindow> mRing{};
    RollbackTelemetry mTelemetry{};
};

} // namespace Game::Networking
