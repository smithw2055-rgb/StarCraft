#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/ECS/EntityManager.h"

namespace Engine::ECS
{

// ============================================================================
// GenericWorldSnapshot: Game-Agnostic Type-Erased ECS World Snapshot
// - Zero dependency on any game-specific components (no Game/Components.h).
// - Iterates over all active Core::ECS::IComponentPool slots [0 .. MaxComponents-1].
// - Uses fast contiguous binary buffers (CapturePoolBinary / RestorePoolBinary)
//   for trivially copyable components, and type-erased pool cloning
//   (ClonePoolState / RestoreFromClonedPool) for non-trivial components.
// - Supports pluggable WorldContext resource snapshot hooks for singleton state.
// ============================================================================
class GenericWorldSnapshot
{
public:
    struct PoolSnapshotEntry
    {
        uint8_t Slot = 0;
        bool IsBinary = true;
        size_t ComponentByteSize = 0;
        std::vector<Core::ECS::EntityID> Entities;
        std::vector<uint8_t> RawBytes;
        std::unique_ptr<Core::ECS::IComponentPool> ClonedPool;

        PoolSnapshotEntry() = default;
        PoolSnapshotEntry(PoolSnapshotEntry&&) noexcept = default;
        PoolSnapshotEntry& operator=(PoolSnapshotEntry&&) noexcept = default;

        PoolSnapshotEntry(const PoolSnapshotEntry& Other)
            : Slot(Other.Slot)
            , IsBinary(Other.IsBinary)
            , ComponentByteSize(Other.ComponentByteSize)
            , Entities(Other.Entities)
            , RawBytes(Other.RawBytes)
            , ClonedPool(Other.ClonedPool ? Other.ClonedPool->ClonePoolState() : nullptr)
        {
        }

        PoolSnapshotEntry& operator=(const PoolSnapshotEntry& Other)
        {
            if (this != &Other)
            {
                Slot = Other.Slot;
                IsBinary = Other.IsBinary;
                ComponentByteSize = Other.ComponentByteSize;
                Entities = Other.Entities;
                RawBytes = Other.RawBytes;
                ClonedPool = Other.ClonedPool ? Other.ClonedPool->ClonePoolState() : nullptr;
            }
            return *this;
        }
    };

    struct EntityIdentityEntry
    {
        Core::ECS::EntityID Id = Core::ECS::InvalidEntity;
        uint32_t Generation = 1;
    };

    using ResourceRestoreFn = std::function<void(Core::ECS::EntityManager&)>;
    using ResourceCaptureHook = std::function<ResourceRestoreFn(const Core::ECS::EntityManager&)>;

    void Capture(const Core::ECS::EntityManager& EM,
                 uint32_t Tick = 0,
                 const std::vector<ResourceCaptureHook>* ResourceHooks = nullptr)
    {
        mTick = Tick;
        mLastEntityId = EM.GetLastEntityId();
        mGenerations = EM.GetGenerationTable();
        mFreeIds = EM.GetFreeIdQueue();
        mEntities.clear();
        mPoolSnapshots.clear();
        mResourceRestorers.clear();

        std::vector<Core::ECS::EntityID> Alive = EM.GetActiveEntities();
        std::sort(Alive.begin(), Alive.end());
        mEntities.reserve(Alive.size());
        for (Core::ECS::EntityID Id : Alive)
        {
            mEntities.push_back({Id, EM.GetGeneration(Id)});
        }

        for (uint8_t Slot = 0; Slot < static_cast<uint8_t>(Core::ECS::MaxComponents); ++Slot)
        {
            const Core::ECS::IComponentPool* Pool = EM.GetPoolBySlot(Slot);
            if (!Pool || Pool->Size() == 0)
                continue;

            PoolSnapshotEntry Entry;
            Entry.Slot = Slot;
            Entry.ComponentByteSize = Pool->GetComponentByteSize();
            Entry.IsBinary = Pool->IsTriviallyCopyable();

            if (Entry.IsBinary)
            {
                Pool->CapturePoolBinary(Entry.Entities, Entry.RawBytes);
                Entry.ClonedPool = Pool->CreateEmptyPool();
            }
            else
            {
                Entry.ClonedPool = Pool->ClonePoolState();
            }
            mPoolSnapshots.push_back(std::move(Entry));
        }

        if (ResourceHooks)
        {
            mResourceRestorers.reserve(ResourceHooks->size());
            for (const auto& Hook : *ResourceHooks)
            {
                if (Hook)
                    mResourceRestorers.push_back(Hook(EM));
            }
        }
    }

    void Restore(Core::ECS::EntityManager& EM) const
    {
        EM.ClearAllEntitiesAndPools();

        for (const auto& Ent : mEntities)
        {
            EM.CreateEntityWithID(Ent.Id, Ent.Generation);
        }

        EM.RestoreEntityAllocatorState(mLastEntityId, mGenerations, mFreeIds);

        for (const auto& Entry : mPoolSnapshots)
        {
            Core::ECS::IComponentPool* Pool = EM.GetMutablePoolBySlot(Entry.Slot);
            if (!Pool && Entry.ClonedPool)
            {
                Pool = EM.EnsurePoolSlot(Entry.Slot, *Entry.ClonedPool);
            }
            if (!Pool)
                continue;

            if (Entry.IsBinary)
            {
                Pool->RestorePoolBinary(EM, Entry.Slot, Entry.Entities, Entry.RawBytes);
            }
            else if (Entry.ClonedPool)
            {
                Pool->RestoreFromClonedPool(EM, Entry.Slot, *Entry.ClonedPool);
            }
        }

        for (const auto& Restorer : mResourceRestorers)
        {
            if (Restorer)
                Restorer(EM);
        }
    }

    uint32_t GetTick() const { return mTick; }
    size_t GetEntityCount() const { return mEntities.size(); }
    size_t GetCapturedPoolCount() const { return mPoolSnapshots.size(); }

    size_t ComputeTotalBinaryBytes() const
    {
        size_t Total = mEntities.size() * sizeof(EntityIdentityEntry);
        for (const auto& Entry : mPoolSnapshots)
        {
            Total += Entry.Entities.size() * sizeof(Core::ECS::EntityID);
            Total += Entry.RawBytes.size();
        }
        return Total;
    }

private:
    uint32_t mTick = 0;
    Core::ECS::EntityID mLastEntityId = 0;
    std::vector<uint32_t> mGenerations;
    std::deque<Core::ECS::EntityID> mFreeIds;
    std::vector<EntityIdentityEntry> mEntities;
    std::vector<PoolSnapshotEntry> mPoolSnapshots;
    std::vector<ResourceRestoreFn> mResourceRestorers;
};

// ============================================================================
// GenericRollbackBuffer: Ring-Buffered Deterministic World State Manager
// Supports registering arbitrary WorldContext singleton types <ResT> to be
// automatically snapshotted and restored alongside all ECS component pools.
// ============================================================================
template <size_t kRingCapacity = 64>
class GenericRollbackBuffer
{
public:
    template <typename ResT>
    void RegisterWorldResource()
    {
        mResourceHooks.push_back([](const Core::ECS::EntityManager& EM) -> GenericWorldSnapshot::ResourceRestoreFn
        {
            if (!EM.HasResource<ResT>())
                return nullptr;
            const ResT Copy = *EM.GetResource<ResT>();
            return [Copy](Core::ECS::EntityManager& TargetEM)
            {
                TargetEM.SetResource<ResT>(Copy);
            };
        });
    }

    void RecordFrame(const Core::ECS::EntityManager& EM, uint32_t Tick)
    {
        const size_t Slot = static_cast<size_t>(Tick) % kRingCapacity;
        mSlots[Slot].Capture(EM, Tick, &mResourceHooks);
        mSlotValid[Slot] = true;
        mLatestTick = Tick;
        ++mRecordedCount;
    }

    bool RollbackToTick(Core::ECS::EntityManager& EM, uint32_t TargetTick)
    {
        const size_t Slot = static_cast<size_t>(TargetTick) % kRingCapacity;
        if (!mSlotValid[Slot] || mSlots[Slot].GetTick() != TargetTick)
            return false;

        mSlots[Slot].Restore(EM);
        mLatestTick = TargetTick;
        ++mRollbackCount;
        return true;
    }

    const GenericWorldSnapshot* FindSnapshot(uint32_t Tick) const
    {
        const size_t Slot = static_cast<size_t>(Tick) % kRingCapacity;
        if (!mSlotValid[Slot] || mSlots[Slot].GetTick() != Tick)
            return nullptr;
        return &mSlots[Slot];
    }

    void Clear()
    {
        mSlotValid.fill(false);
        mLatestTick = 0;
        mRecordedCount = 0;
    }

    uint32_t GetLatestTick() const { return mLatestTick; }
    uint64_t GetRecordedCount() const { return mRecordedCount; }
    uint64_t GetRollbackCount() const { return mRollbackCount; }

private:
    std::array<GenericWorldSnapshot, kRingCapacity> mSlots{};
    std::array<bool, kRingCapacity> mSlotValid{};
    std::vector<GenericWorldSnapshot::ResourceCaptureHook> mResourceHooks;
    uint32_t mLatestTick = 0;
    uint64_t mRecordedCount = 0;
    uint64_t mRollbackCount = 0;
};

} // namespace Engine::ECS
