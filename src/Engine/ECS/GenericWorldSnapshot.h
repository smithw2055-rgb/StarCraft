#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "Core/ECS/EntityManager.h"

namespace Engine::ECS
{

// ============================================================================
// GenericWorldSnapshot: Game-Agnostic Type-Erased ECS World Snapshot
// - Zero dependency on Game/Components.h or any game-specific header.
// - Automatically iterates all active ComponentPools in EntityManager:
//   * Pools supporting binary serialization (trivially copyable or specialized
//     CustomComponentBinarySerializer<T>) are captured as contiguous raw byte
//     buffers (CapturePoolBinary / RestorePoolBinary).
//   * Remaining copyable pools are captured via virtual type-erased cloning
//     (ClonePoolState / RestoreFromClonedPool).
// - Supports optional type-erased WorldContext resource snapshot hooks.
// ============================================================================
class GenericWorldSnapshot
{
public:
    struct EntityIdentityEntry
    {
        Core::ECS::EntityID Id = Core::ECS::InvalidEntity;
        uint32_t Generation = 0;
    };

    struct PoolSnapshotEntry
    {
        uint8_t Slot = 0;
        bool IsBinary = false;
        std::vector<Core::ECS::EntityID> Entities;
        std::vector<uint8_t> RawBytes;
        std::unique_ptr<Core::ECS::IComponentPool> ClonedPool;

        PoolSnapshotEntry() = default;
        PoolSnapshotEntry(const PoolSnapshotEntry& Other)
            : Slot(Other.Slot)
            , IsBinary(Other.IsBinary)
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
                Entities = Other.Entities;
                RawBytes = Other.RawBytes;
                ClonedPool = Other.ClonedPool ? Other.ClonedPool->ClonePoolState() : nullptr;
            }
            return *this;
        }
        PoolSnapshotEntry(PoolSnapshotEntry&&) noexcept = default;
        PoolSnapshotEntry& operator=(PoolSnapshotEntry&&) noexcept = default;
    };

    using ResourceRestoreFn = std::function<void(Core::ECS::EntityManager&)>;
    using ResourceCaptureHook = std::function<ResourceRestoreFn(const Core::ECS::EntityManager&)>;

    void Clear()
    {
        mTick = 0;
        mLastEntityId = 0;
        mGenerations.clear();
        mFreeIds.clear();
        mEntities.clear();
        mPoolSnapshots.clear();
        mResourceRestorers.clear();
    }

    void Capture(const Core::ECS::EntityManager& EM,
                 uint32_t Tick = 0,
                 const std::vector<ResourceCaptureHook>* ResourceHooks = nullptr)
    {
        Clear();
        mTick = Tick;
        mLastEntityId = EM.GetLastEntityId();
        mGenerations = EM.GetGenerationTable();
        mFreeIds = EM.GetFreeIds();

        std::vector<Core::ECS::EntityID> Active = EM.GetActiveEntities();
        std::sort(Active.begin(), Active.end());
        mEntities.reserve(Active.size());
        for (Core::ECS::EntityID Id : Active)
        {
            mEntities.push_back({Id, EM.GetGeneration(Id)});
        }

        for (size_t Slot = 0; Slot < Core::ECS::MaxComponents; ++Slot)
        {
            const Core::ECS::IComponentPool* Pool = EM.GetPoolBySlot(static_cast<uint8_t>(Slot));
            if (!Pool || Pool->Size() == 0)
                continue;

            PoolSnapshotEntry Entry;
            Entry.Slot = static_cast<uint8_t>(Slot);
            if (Pool->HasBinarySerializer())
            {
                Entry.IsBinary = true;
                Pool->CapturePoolBinary(Entry.Entities, Entry.RawBytes);
            }
            else
            {
                Entry.IsBinary = false;
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
            Core::ECS::IComponentPool* Pool = EM.GetOrCreatePoolBySlot(Entry.Slot);
            if (!Pool && Entry.ClonedPool)
            {
                Pool = EM.InstallPoolBySlot(Entry.Slot, Entry.ClonedPool->CreateEmptyPool());
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

    uint32_t ComputeBinaryHashFNV1a() const
    {
        uint32_t Hash = 2166136261u;
        auto MixBytes = [&Hash](const void* Data, size_t Len) {
            const auto* Bytes = static_cast<const uint8_t*>(Data);
            for (size_t i = 0; i < Len; ++i)
            {
                Hash ^= Bytes[i];
                Hash *= 16777619u;
            }
        };

        MixBytes(&mTick, sizeof(mTick));
        for (const auto& Ent : mEntities)
        {
            MixBytes(&Ent.Id, sizeof(Ent.Id));
            MixBytes(&Ent.Generation, sizeof(Ent.Generation));
        }
        for (const auto& Pool : mPoolSnapshots)
        {
            MixBytes(&Pool.Slot, sizeof(Pool.Slot));
            if (!Pool.Entities.empty())
                MixBytes(Pool.Entities.data(), Pool.Entities.size() * sizeof(Core::ECS::EntityID));
            if (!Pool.RawBytes.empty())
                MixBytes(Pool.RawBytes.data(), Pool.RawBytes.size());
        }
        return Hash;
    }

    uint32_t GetTick() const { return mTick; }
    void SetTick(uint32_t Tick) { mTick = Tick; }

    size_t GetEntityCount() const { return mEntities.size(); }
    size_t GetCapturedPoolCount() const { return mPoolSnapshots.size(); }

    const std::vector<EntityIdentityEntry>& GetEntities() const { return mEntities; }
    std::vector<EntityIdentityEntry>& GetMutableEntities() { return mEntities; }

    const std::vector<PoolSnapshotEntry>& GetPoolSnapshots() const { return mPoolSnapshots; }
    std::vector<PoolSnapshotEntry>& GetMutablePoolSnapshots() { return mPoolSnapshots; }

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
// GenericRollbackBuffer: Ring-buffered deterministic rollback state manager
// usable by any 2D/RTS game built on the Engine foundation.
// ============================================================================
template <size_t kRingCapacity = 128>
class GenericRollbackBuffer
{
public:
    template <typename ResourceT>
    void RegisterWorldResource()
    {
        static_assert(std::is_copy_constructible_v<ResourceT>,
                      "Registered WorldContext resource must be copy-constructible");
        mResourceHooks.push_back([](const Core::ECS::EntityManager& EM) -> GenericWorldSnapshot::ResourceRestoreFn {
            if (const auto* Res = EM.Context().GetResource<ResourceT>())
            {
                ResourceT Copy = *Res;
                return [Copy](Core::ECS::EntityManager& TargetEM) {
                    TargetEM.Context().SetResource<ResourceT>(Copy);
                };
            }
            return [](Core::ECS::EntityManager& TargetEM) {
                TargetEM.Context().RemoveResource<ResourceT>();
            };
        });
    }

    void RecordTick(const Core::ECS::EntityManager& EM, uint32_t Tick)
    {
        const size_t Idx = Tick % kRingCapacity;
        mSlots[Idx].Capture(EM, Tick, &mResourceHooks);
        mSlotValid[Idx] = true;
        ++mTotalRecorded;
    }

    bool RollbackToTick(Core::ECS::EntityManager& EM, uint32_t TargetTick)
    {
        const size_t Idx = TargetTick % kRingCapacity;
        if (!mSlotValid[Idx] || mSlots[Idx].GetTick() != TargetTick)
            return false;

        mSlots[Idx].Restore(EM);
        ++mTotalRollbacks;
        return true;
    }

    const GenericWorldSnapshot* FindSnapshot(uint32_t Tick) const
    {
        const size_t Idx = Tick % kRingCapacity;
        if (!mSlotValid[Idx] || mSlots[Idx].GetTick() != Tick)
            return nullptr;
        return &mSlots[Idx];
    }

    void Clear()
    {
        for (size_t i = 0; i < kRingCapacity; ++i)
        {
            mSlots[i].Clear();
            mSlotValid[i] = false;
        }
    }

    void RecordFrame(const Core::ECS::EntityManager& EM, uint32_t Tick) { RecordTick(EM, Tick); }
    uint64_t GetTotalRecorded() const { return mTotalRecorded; }
    uint64_t GetRecordedCount() const { return mTotalRecorded; }
    uint64_t GetTotalRollbacks() const { return mTotalRollbacks; }
    uint64_t GetRollbackCount() const { return mTotalRollbacks; }

private:
    std::array<GenericWorldSnapshot, kRingCapacity> mSlots{};
    std::array<bool, kRingCapacity> mSlotValid{};
    std::vector<GenericWorldSnapshot::ResourceCaptureHook> mResourceHooks;
    uint64_t mTotalRecorded = 0;
    uint64_t mTotalRollbacks = 0;
};

} // namespace Engine::ECS
