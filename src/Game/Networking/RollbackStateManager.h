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
// RollbackStateManager: Ring-buffered Deterministic Rollback & Resimulation
// Referenced from Unity Netcode Prediction/Rollback & GGPO:
// - Stores a sliding ring buffer (kMaxRollbackWindow = 64 frames) of
//   WorldDeterministicSnapshots and per-frame SyncAction logs.
// - Detects state hash divergence or late remote inputs at a past frame F_past,
//   restores the ECS World to F_past, applies corrected inputs, and re-simulates
//   forward to F_current while refreshing intermediate snapshot hashes.
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

    // Captures and records the authoritative ECS world state at the start of FrameNumber
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
        if (!Slot.Valid || Slot.FrameNumber != FrameNumber)
            return nullptr;
        return &Slot;
    }

    // Verifies whether a remote peer's reported FNV-1a state hash for FrameNumber matches local history
    bool VerifyRemoteFrameHash(uint32_t FrameNumber, uint32_t RemoteHash)
    {
        const auto* Entry = FindFrame(FrameNumber);
        if (!Entry)
            return true;
        if (Entry->Snapshot.StateHashFNV1a != RemoteHash)
        {
            ++mTelemetry.DesyncDetections;
            return false;
        }
        return true;
    }

    // Injects a late or corrected remote action at TargetFrame and, if TargetFrame < CurrentFrame,
    // rolls back the ECS World to TargetFrame and re-simulates deterministically up to CurrentFrame.
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
        // Sort actions deterministically by PlayerID
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

        // 1. Restore ECS World to the exact state captured at RollbackFrame
        Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(EM, StartEntry->Snapshot);
        ++mTelemetry.TotalRollbacksExecuted;

        // 2. Re-simulate ticks [RollbackFrame .. CurrentFrame - 1]
        for (uint32_t F = RollbackFrame; F < CurrentFrame; ++F)
        {
            auto* Slot = FindFrameMutable(F);
            if (Slot)
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

        // 3. Refresh snapshot at CurrentFrame if present
        if (auto* CurSlot = FindFrameMutable(CurrentFrame))
        {
            CurSlot->Snapshot = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(EM, CurrentFrame);
        }

        return true;
    }

    const RollbackTelemetry& GetTelemetry() const { return mTelemetry; }

private:
    RollbackFrameEntry* FindFrameMutable(uint32_t FrameNumber)
    {
        auto& Slot = mRing[FrameNumber % kMaxRollbackWindow];
        if (!Slot.Valid || Slot.FrameNumber != FrameNumber)
            return nullptr;
        return &Slot;
    }

    std::array<RollbackFrameEntry, kMaxRollbackWindow> mRing{};
    RollbackTelemetry mTelemetry{};
};

} // namespace Game::Networking
