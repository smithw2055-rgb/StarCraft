#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/ECS/EntityCommandBuffer.h"
#include "Core/ECS/SmallBuffer.h"
#include "Core/ECS/EventBus.h"
#include "Core/ECS/WorldContext.h"

namespace Core::ECS
{

using EntityID = int;
constexpr EntityID InvalidEntity = 0;
constexpr size_t MaxComponents = 64;
constexpr size_t MaxResources = 64;

using ComponentMask = std::bitset<MaxComponents>;

class EntityManager;

// Generational EntityHandle (Unity DOTS / EnTT style) to prevent ABA dangling references
// when an EntityID slot is recycled across frames.
struct EntityHandle
{
    EntityID Id = InvalidEntity;
    uint32_t Generation = 0;

    constexpr bool IsValid() const { return Id > InvalidEntity && Generation > 0; }
    constexpr bool operator==(const EntityHandle& Other) const
    {
        return Id == Other.Id && Generation == Other.Generation;
    }
    constexpr bool operator!=(const EntityHandle& Other) const
    {
        return !(*this == Other);
    }
};

struct ECSQueryTelemetry
{
    uint64_t TotalQueries = 0;
    uint64_t EarlyOutEmptyPoolCount = 0;
    uint64_t PivotAcceleratedQueries = 0;
    uint64_t ScratchBufferReuses = 0;
    uint64_t CachedQueryHits = 0;
    uint64_t CachedQueryMisses = 0;
};

namespace Detail
{
template <typename PodT>
inline void AppendPodBytes(std::vector<uint8_t>& Buf, const PodT& Val)
{
    static_assert(std::is_trivially_copyable_v<PodT>, "AppendPodBytes requires trivially copyable type");
    const auto* Ptr = reinterpret_cast<const uint8_t*>(&Val);
    Buf.insert(Buf.end(), Ptr, Ptr + sizeof(PodT));
}

template <typename ElemT>
inline void AppendVectorPodBytes(std::vector<uint8_t>& Buf, const std::vector<ElemT>& Vec)
{
    static_assert(std::is_trivially_copyable_v<ElemT>, "AppendVectorPodBytes requires trivially copyable element");
    const uint32_t Count = static_cast<uint32_t>(Vec.size());
    AppendPodBytes(Buf, Count);
    if (Count > 0)
    {
        const auto* Ptr = reinterpret_cast<const uint8_t*>(Vec.data());
        Buf.insert(Buf.end(), Ptr, Ptr + static_cast<size_t>(Count) * sizeof(ElemT));
    }
}

template <typename PodT>
inline bool ReadPodBytes(const std::vector<uint8_t>& Buf, size_t& Cursor, PodT& Out)
{
    static_assert(std::is_trivially_copyable_v<PodT>, "ReadPodBytes requires trivially copyable type");
    if (Cursor + sizeof(PodT) > Buf.size())
        return false;
    std::memcpy(&Out, Buf.data() + Cursor, sizeof(PodT));
    Cursor += sizeof(PodT);
    return true;
}

template <typename ElemT>
inline bool ReadVectorPodBytes(const std::vector<uint8_t>& Buf, size_t& Cursor, std::vector<ElemT>& Out)
{
    static_assert(std::is_trivially_copyable_v<ElemT>, "ReadVectorPodBytes requires trivially copyable element");
    uint32_t Count = 0;
    if (!ReadPodBytes(Buf, Cursor, Count))
        return false;
    const size_t ByteCount = static_cast<size_t>(Count) * sizeof(ElemT);
    if (Cursor + ByteCount > Buf.size())
        return false;
    Out.resize(Count);
    if (Count > 0)
        std::memcpy(Out.data(), Buf.data() + Cursor, ByteCount);
    Cursor += ByteCount;
    return true;
}
} // namespace Detail

// Specialization point for components containing dynamic containers (e.g. std::vector)
template <typename T>
struct CustomComponentBinarySerializer
{
    static constexpr bool kSupported = false;
};

class IComponentPool
{
public:
    virtual ~IComponentPool() = default;
    virtual void RemoveComponent(EntityID Id) = 0;
    virtual void NotifyEntityDestroyed(EntityManager& Owner, EntityID Id) = 0;
    virtual bool HasComponent(EntityID Id) const = 0;
    virtual size_t Size() const = 0;
    virtual const std::vector<EntityID>& GetDenseEntities() const = 0;
    virtual void Clear() = 0;

    // General-Purpose Engine Foundation: Type-Erased Snapshot & Cloning API
    virtual size_t GetComponentByteSize() const = 0;
    virtual bool IsTriviallyCopyable() const = 0;
    virtual bool HasBinarySerializer() const = 0;
    virtual std::unique_ptr<IComponentPool> CreateEmptyPool() const = 0;
    virtual void CapturePoolBinary(std::vector<EntityID>& OutEntities,
                                   std::vector<uint8_t>& OutBytes) const = 0;
    virtual void RestorePoolBinary(EntityManager& Owner,
                                   uint8_t ComponentTypeSlot,
                                   const std::vector<EntityID>& Entities,
                                   const std::vector<uint8_t>& Bytes) = 0;
    virtual std::unique_ptr<IComponentPool> ClonePoolState() const = 0;
    virtual void RestoreFromClonedPool(EntityManager& Owner,
                                       uint8_t ComponentTypeSlot,
                                       const IComponentPool& ClonedPool) = 0;
};

// Paged Sparse-Set ComponentPool:
// - Stores component instances in contiguous fixed-size pages (kPageSize = 256)
//   eliminating per-entity heap allocations while preserving 100% pointer/reference
//   stability across AddComponent/RemoveComponent calls.
// - Maintains a compact Sparse-Set (mSparseToDense + mDenseEntities) for O(1)
//   membership checks, O(1) removal (swap-and-pop), and cache-friendly dense iteration.
// - Supports EnTT / Godot style reactive lifecycle signals (OnConstruct / OnRemove).
// - Supports game-agnostic type-erased binary snapshotting & cloning.
template <typename T>
class ComponentPool final : public IComponentPool
{
public:
    static constexpr size_t kPageSize = 256;
    static constexpr size_t kInvalidDenseIndex = std::numeric_limits<size_t>::max();

    using ConstructCallback = std::function<void(EntityManager&, EntityID, T&)>;
    using RemoveCallback = std::function<void(EntityManager&, EntityID)>;

    T& AddComponent(EntityID Id)
    {
        const size_t SparseIdx = static_cast<size_t>(Id);
        EnsureSparseCapacity(SparseIdx);

        const size_t PageIdx = SparseIdx / kPageSize;
        const size_t PageOffset = SparseIdx % kPageSize;
        EnsurePage(PageIdx);

        T& Slot = (*mPages[PageIdx])[PageOffset];
        Slot = T{};

        if (mSparseToDense[SparseIdx] == kInvalidDenseIndex)
        {
            mSparseToDense[SparseIdx] = mDenseEntities.size();
            mDenseEntities.push_back(Id);
        }
        return Slot;
    }

    void NotifyConstructed(EntityManager& Owner, EntityID Id, T& ComponentRef)
    {
        for (const auto& Cb : mOnConstructCallbacks)
        {
            if (Cb)
                Cb(Owner, Id, ComponentRef);
        }
    }

    void NotifyRemoved(EntityManager& Owner, EntityID Id)
    {
        for (const auto& Cb : mOnRemoveCallbacks)
        {
            if (Cb)
                Cb(Owner, Id);
        }
    }

    void NotifyEntityDestroyed(EntityManager& Owner, EntityID Id) override
    {
        NotifyRemoved(Owner, Id);
    }

    void AddOnConstructCallback(ConstructCallback Cb)
    {
        if (Cb)
            mOnConstructCallbacks.push_back(std::move(Cb));
    }

    void AddOnRemoveCallback(RemoveCallback Cb)
    {
        if (Cb)
            mOnRemoveCallbacks.push_back(std::move(Cb));
    }

    T* GetComponent(EntityID Id)
    {
        if (!HasComponent(Id))
            return nullptr;
        const size_t SparseIdx = static_cast<size_t>(Id);
        return &(*mPages[SparseIdx / kPageSize])[SparseIdx % kPageSize];
    }

    const T* GetComponent(EntityID Id) const
    {
        if (!HasComponent(Id))
            return nullptr;
        const size_t SparseIdx = static_cast<size_t>(Id);
        return &(*mPages[SparseIdx / kPageSize])[SparseIdx % kPageSize];
    }

    void RemoveComponent(EntityID Id) override
    {
        if (!HasComponent(Id))
            return;

        const size_t SparseIdx = static_cast<size_t>(Id);
        const size_t DenseIdx = mSparseToDense[SparseIdx];
        const EntityID LastEntity = mDenseEntities.back();

        mDenseEntities[DenseIdx] = LastEntity;
        mSparseToDense[static_cast<size_t>(LastEntity)] = DenseIdx;
        mDenseEntities.pop_back();
        mSparseToDense[SparseIdx] = kInvalidDenseIndex;

        (*mPages[SparseIdx / kPageSize])[SparseIdx % kPageSize] = T{};
    }

    bool HasComponent(EntityID Id) const override
    {
        if (Id <= InvalidEntity)
            return false;
        const size_t SparseIdx = static_cast<size_t>(Id);
        return SparseIdx < mSparseToDense.size() &&
               mSparseToDense[SparseIdx] != kInvalidDenseIndex;
    }

    size_t Size() const override
    {
        return mDenseEntities.size();
    }

    const std::vector<EntityID>& GetDenseEntities() const override
    {
        return mDenseEntities;
    }

    void Clear() override
    {
        mDenseEntities.clear();
        std::fill(mSparseToDense.begin(), mSparseToDense.end(), kInvalidDenseIndex);
        mPages.clear();
    }

    size_t GetComponentByteSize() const override
    {
        return sizeof(T);
    }

    bool IsTriviallyCopyable() const override
    {
        return std::is_trivially_copyable_v<T>;
    }

    bool HasBinarySerializer() const override
    {
        return std::is_trivially_copyable_v<T> || CustomComponentBinarySerializer<T>::kSupported;
    }

    std::unique_ptr<IComponentPool> CreateEmptyPool() const override
    {
        return std::make_unique<ComponentPool<T>>();
    }

    void CapturePoolBinary(std::vector<EntityID>& OutEntities,
                           std::vector<uint8_t>& OutBytes) const override
    {
        OutEntities.clear();
        OutBytes.clear();
        if constexpr (std::is_trivially_copyable_v<T>)
        {
            OutEntities = mDenseEntities;
            std::sort(OutEntities.begin(), OutEntities.end());
            OutBytes.resize(OutEntities.size() * sizeof(T));
            for (size_t i = 0; i < OutEntities.size(); ++i)
            {
                const T* Comp = GetComponent(OutEntities[i]);
                if (Comp)
                    std::memcpy(OutBytes.data() + i * sizeof(T), Comp, sizeof(T));
            }
        }
        else if constexpr (CustomComponentBinarySerializer<T>::kSupported)
        {
            OutEntities = mDenseEntities;
            std::sort(OutEntities.begin(), OutEntities.end());
            for (size_t i = 0; i < OutEntities.size(); ++i)
            {
                if (const T* Comp = GetComponent(OutEntities[i]))
                    CustomComponentBinarySerializer<T>::Serialize(*Comp, OutBytes);
            }
        }
    }

    void RestorePoolBinary(EntityManager& Owner,
                           uint8_t ComponentTypeSlot,
                           const std::vector<EntityID>& Entities,
                           const std::vector<uint8_t>& Bytes) override;

    std::unique_ptr<IComponentPool> ClonePoolState() const override
    {
        auto Clone = std::make_unique<ComponentPool<T>>();
        if constexpr (std::is_copy_assignable_v<T>)
        {
            std::vector<EntityID> Sorted = mDenseEntities;
            std::sort(Sorted.begin(), Sorted.end());
            for (EntityID Id : Sorted)
            {
                if (const T* Comp = GetComponent(Id))
                {
                    Clone->AddComponent(Id) = *Comp;
                }
            }
        }
        return Clone;
    }

    void RestoreFromClonedPool(EntityManager& Owner,
                               uint8_t ComponentTypeSlot,
                               const IComponentPool& ClonedPool) override;

private:
    using PageBlock = std::array<T, kPageSize>;

    void EnsureSparseCapacity(size_t SparseIdx)
    {
        if (SparseIdx >= mSparseToDense.size())
        {
            const size_t NewSize = std::max(SparseIdx + 1, mSparseToDense.size() * 2 + kPageSize);
            mSparseToDense.resize(NewSize, kInvalidDenseIndex);
        }
    }

    void EnsurePage(size_t PageIdx)
    {
        if (PageIdx >= mPages.size())
            mPages.resize(PageIdx + 1);
        if (!mPages[PageIdx])
            mPages[PageIdx] = std::make_unique<PageBlock>();
    }

    std::vector<std::unique_ptr<PageBlock>> mPages;
    std::vector<size_t> mSparseToDense;
    std::vector<EntityID> mDenseEntities;
    std::vector<ConstructCallback> mOnConstructCallbacks;
    std::vector<RemoveCallback> mOnRemoveCallbacks;
};

inline uint8_t NextComponentTypeID = 0;

template <typename T>
uint8_t GetComponentTypeID()
{
    static uint8_t ID = NextComponentTypeID++;
    return ID;
}

class EntityManager
{
public:
    static constexpr size_t kMaxScratchDepth = 8;

    EntityManager();
    ~EntityManager();

    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;
    EntityManager(EntityManager&&) noexcept = default;
    EntityManager& operator=(EntityManager&&) noexcept = default;

    // Active World context binding (allows legacy static accessors to resolve
    // against the currently executing EntityManager/World).
    void ActivateWorld() const { sActiveWorld = const_cast<EntityManager*>(this); }
    static EntityManager* GetActiveWorld() { return sActiveWorld; }
    static void SetActiveWorld(EntityManager* World) { sActiveWorld = World; }

    EntityID CreateEntity();
    EntityHandle CreateEntityHandle();
    EntityID CreateEntityWithID(EntityID Id, uint32_t Generation = 0);
    void DestroyEntity(EntityID Id);
    bool DestroyEntity(EntityHandle Handle);
    void ClearAllEntitiesAndPools();

    bool IsAlive(EntityID Id) const;
    bool IsAlive(EntityHandle Handle) const;
    uint32_t GetGeneration(EntityID Id) const;
    EntityHandle GetEntityHandle(EntityID Id) const;

    ComponentMask GetMask(EntityID Id) const;
    uint64_t GetMaskMutationVersion() const { return mMaskMutationVersion; }
    size_t GetEntityCount() const { return mLivingEntityCount; }
    size_t GetEntityCapacity() const { return mAlive.size(); }
    const std::vector<EntityID>& GetActiveEntities() const { return mActiveEntities; }
    const std::vector<EntityID>& GetAliveEntities() const { return mActiveEntities; }

    // Type-erased ComponentPool slot introspection for GenericWorldSnapshot
    const IComponentPool* GetPoolBySlot(uint8_t Slot) const
    {
        return Slot < MaxComponents ? mPools[Slot].get() : nullptr;
    }

    IComponentPool* GetMutablePoolBySlot(uint8_t Slot)
    {
        return Slot < MaxComponents ? mPools[Slot].get() : nullptr;
    }

    IComponentPool* GetOrCreatePoolBySlot(uint8_t Slot)
    {
        if (Slot >= MaxComponents)
            return nullptr;
        if (!mPools[Slot] && sPoolFactories[Slot])
            mPools[Slot] = sPoolFactories[Slot]();
        return mPools[Slot].get();
    }

    IComponentPool* InstallPoolBySlot(uint8_t Slot, std::unique_ptr<IComponentPool> Pool)
    {
        if (Slot >= MaxComponents)
            return nullptr;
        mPools[Slot] = std::move(Pool);
        return mPools[Slot].get();
    }

    void SetEntityComponentBitInternal(EntityID Id, uint8_t Slot)
    {
        if (Id > 0 && (size_t)Id < mMasks.size() && Slot < MaxComponents)
        {
            mMasks[(size_t)Id].set(Slot);
            ++mMaskMutationVersion;
        }
    }

    EntityID GetLastEntityId() const { return mLastEntityId; }
    const std::vector<uint32_t>& GetGenerationTable() const { return mGenerations; }
    const std::deque<EntityID>& GetFreeIds() const { return mFreeIds; }
    void RestoreEntityAllocatorState(EntityID LastEntityId,
                                     const std::vector<uint32_t>& Generations,
                                     const std::deque<EntityID>& FreeIds);

    template <typename T>
    T& AddComponent(EntityID Id)
    {
        auto* Pool = GetOrCreatePool<T>();
        if ((size_t)Id >= mMasks.size())
        {
            mMasks.resize((size_t)Id + 1);
            mAlive.resize((size_t)Id + 1, false);
            mGenerations.resize((size_t)Id + 1, 0);
        }
        mMasks[(size_t)Id].set(GetComponentTypeID<T>());
        ++mMaskMutationVersion;
        T& CompRef = Pool->AddComponent(Id);
        Pool->NotifyConstructed(*this, Id, CompRef);
        return CompRef;
    }

    template <typename T>
    void RemoveComponent(EntityID Id)
    {
        const uint8_t ID = GetComponentTypeID<T>();
        const bool HadBit = (Id > 0 && (size_t)Id < mMasks.size() && mMasks[(size_t)Id].test(ID));
        if (Id > 0 && (size_t)Id < mMasks.size())
            mMasks[(size_t)Id].reset(ID);
        if (HadBit)
            ++mMaskMutationVersion;
        if (mPools[ID])
        {
            if (HadBit)
                static_cast<ComponentPool<T>*>(mPools[ID].get())->NotifyRemoved(*this, Id);
            mPools[ID]->RemoveComponent(Id);
        }
    }

    // EnTT / Godot style reactive component lifecycle hooks
    template <typename T>
    void OnComponentConstructed(typename ComponentPool<T>::ConstructCallback Callback)
    {
        GetOrCreatePool<T>()->AddOnConstructCallback(std::move(Callback));
    }

    template <typename T>
    void OnComponentRemoved(typename ComponentPool<T>::RemoveCallback Callback)
    {
        GetOrCreatePool<T>()->AddOnRemoveCallback(std::move(Callback));
    }

    template <typename T>
    bool HasComponent(EntityID Id) const
    {
        if (Id <= 0 || (size_t)Id >= mMasks.size())
            return false;
        const uint8_t ID = GetComponentTypeID<T>();
        return mMasks[(size_t)Id].test(ID);
    }

    template <typename T>
    T* GetComponent(EntityID Id)
    {
        if (Id <= 0)
            return nullptr;
        const uint8_t ID = GetComponentTypeID<T>();
        if (!mPools[ID])
            return nullptr;
        return static_cast<ComponentPool<T>*>(mPools[ID].get())->GetComponent(Id);
    }

    template <typename T>
    const T* GetComponent(EntityID Id) const
    {
        if (Id <= 0)
            return nullptr;
        const uint8_t ID = GetComponentTypeID<T>();
        if (!mPools[ID])
            return nullptr;
        return static_cast<const ComponentPool<T>*>(mPools[ID].get())->GetComponent(Id);
    }

    // Generational EntityHandle safe component accessors (Unity DOTS / Godot RID style):
    // Automatically verifies generation to prevent ABA slot-recycling bugs.
    EntityID ResolveAliveId(EntityHandle Handle) const
    {
        return IsAlive(Handle) ? Handle.Id : InvalidEntity;
    }

    template <typename T>
    bool HasComponent(EntityHandle Handle) const
    {
        return IsAlive(Handle) && HasComponent<T>(Handle.Id);
    }

    template <typename T>
    T* GetComponent(EntityHandle Handle)
    {
        return IsAlive(Handle) ? GetComponent<T>(Handle.Id) : nullptr;
    }

    template <typename T>
    const T* GetComponent(EntityHandle Handle) const
    {
        return IsAlive(Handle) ? GetComponent<T>(Handle.Id) : nullptr;
    }

    template <typename T>
    T* TryGetComponent(EntityHandle Handle)
    {
        return GetComponent<T>(Handle);
    }

    template <typename T>
    const T* TryGetComponent(EntityHandle Handle) const
    {
        return GetComponent<T>(Handle);
    }

    template <typename T>
    T* AddComponentIfAlive(EntityHandle Handle)
    {
        if (!IsAlive(Handle))
            return nullptr;
        return &AddComponent<T>(Handle.Id);
    }

    template <typename T>
    bool RemoveComponentIfAlive(EntityHandle Handle)
    {
        if (!IsAlive(Handle) || !HasComponent<T>(Handle.Id))
            return false;
        RemoveComponent<T>(Handle.Id);
        return true;
    }

    template <typename T>
    size_t GetComponentCount() const
    {
        const uint8_t ID = GetComponentTypeID<T>();
        if (!mPools[ID])
            return 0;
        return mPools[ID]->Size();
    }

    template <typename T>
    bool HasAnyComponent() const
    {
        return GetComponentCount<T>() > 0;
    }

    // Pivot-accelerated multi-component query (EnTT / Unity DOTS style):
    // - Early-outs in O(1) if any required component pool is null or empty
    // - Uses the smallest component pool as pivot when sparse (<= 1/2 of active entities)
    template <typename FirstComponent, typename... RestComponents>
    std::vector<EntityID> GetEntitiesWith() const
    {
        ++mQueryTelemetry.TotalQueries;
        std::vector<EntityID> Result;

        const IComponentPool* PivotPool = SelectSmallestPool<FirstComponent, RestComponents...>();
        if (!PivotPool || PivotPool->Size() == 0)
        {
            ++mQueryTelemetry.EarlyOutEmptyPoolCount;
            return Result;
        }

        const ComponentMask RequiredMask = CreateMask<FirstComponent, RestComponents...>();
        const size_t PivotSize = PivotPool->Size();
        Result.reserve(std::min(PivotSize, mActiveEntities.size()));

        if (PivotSize * 2 <= mActiveEntities.size())
        {
            ++mQueryTelemetry.PivotAcceleratedQueries;
            for (EntityID Id : PivotPool->GetDenseEntities())
            {
                if (IsAlive(Id) && (mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                    Result.push_back(Id);
            }
            return Result;
        }

        for (EntityID Id : mActiveEntities)
        {
            if ((mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                Result.push_back(Id);
        }
        return Result;
    }

    // Unity DOTS EntityQuery style version-cached query:
    // Returns a const reference to a cached entity list in O(1) time with zero
    // allocations when no structural component mask mutations occurred since the last call.
    template <typename FirstComponent, typename... RestComponents>
    const std::vector<EntityID>& GetEntitiesWithCached() const
    {
        const ComponentMask RequiredMask = CreateMask<FirstComponent, RestComponents...>();
        const uint64_t MaskKey = RequiredMask.to_ullong();
        auto& CacheEntry = mQueryCache[MaskKey];

        if (CacheEntry.BuiltAtVersion == mMaskMutationVersion)
        {
            ++mQueryTelemetry.TotalQueries;
            ++mQueryTelemetry.CachedQueryHits;
            return CacheEntry.Entities;
        }

        ++mQueryTelemetry.CachedQueryMisses;
        CacheEntry.Entities = GetEntitiesWith<FirstComponent, RestComponents...>();
        CacheEntry.BuiltAtVersion = mMaskMutationVersion;
        return CacheEntry.Entities;
    }

    template <typename FirstComponent, typename... RestComponents, typename Fn>
    void ForEachEntityWith(Fn&& Func)
    {
        ++mQueryTelemetry.TotalQueries;
        const IComponentPool* PivotPool = SelectSmallestPool<FirstComponent, RestComponents...>();
        if (!PivotPool || PivotPool->Size() == 0)
        {
            ++mQueryTelemetry.EarlyOutEmptyPoolCount;
            return;
        }

        const ComponentMask RequiredMask = CreateMask<FirstComponent, RestComponents...>();
        if (PivotPool->Size() * 2 <= mActiveEntities.size())
        {
            ++mQueryTelemetry.PivotAcceleratedQueries;
            ScopedScratchBuffer Scratch(*this, PivotPool->GetDenseEntities());
            for (EntityID Id : Scratch.View())
            {
                if (IsAlive(Id) && (mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                    Func(Id);
            }
            return;
        }

        for (size_t i = 0; i < mActiveEntities.size();)
        {
            const EntityID Id = mActiveEntities[i];
            if ((mMasks[(size_t)Id] & RequiredMask) != RequiredMask)
            {
                ++i;
                continue;
            }
            Func(Id);
            if (i < mActiveEntities.size() && mActiveEntities[i] == Id)
                ++i;
        }
    }

    template <typename FirstComponent, typename... RestComponents, typename Fn>
    void ForEachEntityWith(Fn&& Func) const
    {
        ++mQueryTelemetry.TotalQueries;
        const IComponentPool* PivotPool = SelectSmallestPool<FirstComponent, RestComponents...>();
        if (!PivotPool || PivotPool->Size() == 0)
        {
            ++mQueryTelemetry.EarlyOutEmptyPoolCount;
            return;
        }

        const ComponentMask RequiredMask = CreateMask<FirstComponent, RestComponents...>();
        if (PivotPool->Size() * 2 <= mActiveEntities.size())
        {
            ++mQueryTelemetry.PivotAcceleratedQueries;
            for (EntityID Id : PivotPool->GetDenseEntities())
            {
                if (IsAlive(Id) && (mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                    Func(Id);
            }
            return;
        }

        for (size_t i = 0; i < mActiveEntities.size(); ++i)
        {
            const EntityID Id = mActiveEntities[i];
            if ((mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                Func(Id);
        }
    }

    // EnTT / Unity DOTS style View iteration: selects the smallest component pool
    // among Components... as the iteration pivot and invokes Func(EntityID, Components&...).
    // Uses a reusable re-entrant scratch buffer to guarantee zero heap allocations
    // after warmup while remaining 100% safe against structural mutations mid-loop.
    template <typename FirstComponent, typename... RestComponents, typename Fn>
    void ForEachComponent(Fn&& Func)
    {
        ++mQueryTelemetry.TotalQueries;
        const IComponentPool* PivotPool = SelectSmallestPool<FirstComponent, RestComponents...>();
        if (!PivotPool || PivotPool->Size() == 0)
        {
            ++mQueryTelemetry.EarlyOutEmptyPoolCount;
            return;
        }

        ++mQueryTelemetry.PivotAcceleratedQueries;
        const ComponentMask RequiredMask = CreateMask<FirstComponent, RestComponents...>();
        ScopedScratchBuffer Scratch(*this, PivotPool->GetDenseEntities());
        for (EntityID Id : Scratch.View())
        {
            if (!IsAlive(Id) || (mMasks[(size_t)Id] & RequiredMask) != RequiredMask)
                continue;
            Func(Id, *GetComponent<FirstComponent>(Id), *GetComponent<RestComponents>(Id)...);
        }
    }

    template <typename FirstComponent, typename... RestComponents, typename Fn>
    void ForEachComponent(Fn&& Func) const
    {
        ++mQueryTelemetry.TotalQueries;
        const IComponentPool* PivotPool = SelectSmallestPool<FirstComponent, RestComponents...>();
        if (!PivotPool || PivotPool->Size() == 0)
        {
            ++mQueryTelemetry.EarlyOutEmptyPoolCount;
            return;
        }

        ++mQueryTelemetry.PivotAcceleratedQueries;
        const ComponentMask RequiredMask = CreateMask<FirstComponent, RestComponents...>();
        for (EntityID Id : PivotPool->GetDenseEntities())
        {
            if (!IsAlive(Id) || (mMasks[(size_t)Id] & RequiredMask) != RequiredMask)
                continue;
            Func(Id, *GetComponent<FirstComponent>(Id), *GetComponent<RestComponents>(Id)...);
        }
    }

    template <typename FirstComponent, typename... RestComponents, typename Predicate>
    bool AnyEntityWith(Predicate&& Pred) const
    {
        ++mQueryTelemetry.TotalQueries;
        const IComponentPool* PivotPool = SelectSmallestPool<FirstComponent, RestComponents...>();
        if (!PivotPool || PivotPool->Size() == 0)
        {
            ++mQueryTelemetry.EarlyOutEmptyPoolCount;
            return false;
        }

        const ComponentMask RequiredMask = CreateMask<FirstComponent, RestComponents...>();
        if (PivotPool->Size() * 2 <= mActiveEntities.size())
        {
            ++mQueryTelemetry.PivotAcceleratedQueries;
            for (EntityID Id : PivotPool->GetDenseEntities())
            {
                if (IsAlive(Id) && (mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                {
                    if (Pred(Id))
                        return true;
                }
            }
            return false;
        }

        const size_t Count = mActiveEntities.size();
        for (size_t i = 0; i < Count && i < mActiveEntities.size(); ++i)
        {
            const EntityID Id = mActiveEntities[i];
            if ((mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
            {
                if (Pred(Id))
                    return true;
            }
        }
        return false;
    }

    // =========================================================================
    // Unified World Singleton Resources (Unity DOTS Singleton / Bevy Res<T>)
    // Delegates directly to mContext (WorldContext) so EM.GetResource<T>() and
    // EM.Context().GetResource<T>() share a single O(1) flat-indexed store.
    // =========================================================================
    template <typename T, typename... Args>
    T& SetResource(Args&&... InArgs)
    {
        return mContext.EmplaceResource<T>(std::forward<Args>(InArgs)...);
    }

    template <typename T>
    T& GetOrCreateResource()
    {
        return mContext.GetOrCreateResource<T>();
    }

    template <typename T>
    const T& GetOrCreateResource() const
    {
        return const_cast<EntityManager*>(this)->mContext.GetOrCreateResource<T>();
    }

    template <typename T>
    T* GetResource()
    {
        return mContext.GetResource<T>();
    }

    template <typename T>
    const T* GetResource() const
    {
        return mContext.GetResource<T>();
    }

    template <typename T>
    bool HasResource() const
    {
        return mContext.HasResource<T>();
    }

    template <typename T>
    void RemoveResource()
    {
        mContext.RemoveResource<T>();
    }

    // =========================================================================
    // World EventBus & Deferred CommandBuffer
    // =========================================================================
    template <typename T>
    ComponentPool<T>* GetPool()
    {
        const uint8_t ID = GetComponentTypeID<T>();
        if (!mPools[ID])
            return nullptr;
        return static_cast<ComponentPool<T>*>(mPools[ID].get());
    }

    template <typename T>
    const ComponentPool<T>* GetPool() const
    {
        const uint8_t ID = GetComponentTypeID<T>();
        if (!mPools[ID])
            return nullptr;
        return static_cast<const ComponentPool<T>*>(mPools[ID].get());
    }

    WorldContext& Context() { return mContext; }
    const WorldContext& Context() const { return mContext; }

    EventBus& Events() { return mEventBus; }
    const EventBus& Events() const { return mEventBus; }

    EntityCommandBuffer& Commands() { return mCommandBuffer; }
    const EntityCommandBuffer& Commands() const { return mCommandBuffer; }

    void FlushCommands()
    {
        mCommandBuffer.Playback(*this);
    }

    const ECSQueryTelemetry& GetQueryTelemetry() const { return mQueryTelemetry; }
    void ResetQueryTelemetry() { mQueryTelemetry = {}; }

private:
    struct CachedQueryEntry
    {
        uint64_t BuiltAtVersion = 0;
        std::vector<EntityID> Entities;
    };

    class ScopedScratchBuffer
    {
    public:
        ScopedScratchBuffer(const EntityManager& Owner, const std::vector<EntityID>& Source)
            : mOwner(Owner), mDepth(Owner.mQueryScratchDepth++)
        {
            if (mDepth < kMaxScratchDepth)
            {
                auto& Buf = mOwner.mQueryScratchBuffers[mDepth];
                if (Buf.capacity() >= Source.size() && !Source.empty())
                {
                    ++mOwner.mQueryTelemetry.ScratchBufferReuses;
                }
                Buf.assign(Source.begin(), Source.end());
                mView = &Buf;
            }
            else
            {
                mFallback = Source;
                mView = &mFallback;
            }
        }

        ~ScopedScratchBuffer()
        {
            if (mOwner.mQueryScratchDepth > 0)
                --mOwner.mQueryScratchDepth;
        }

        const std::vector<EntityID>& View() const { return *mView; }

    private:
        const EntityManager& mOwner;
        size_t mDepth = 0;
        const std::vector<EntityID>* mView = nullptr;
        std::vector<EntityID> mFallback;
    };

    template <typename T>
    ComponentPool<T>* GetOrCreatePool()
    {
        const uint8_t ID = GetComponentTypeID<T>();
        if (!sPoolFactories[ID])
        {
            sPoolFactories[ID] = []() -> std::unique_ptr<IComponentPool> {
                return std::make_unique<ComponentPool<T>>();
            };
        }
        if (!mPools[ID])
            mPools[ID] = std::make_unique<ComponentPool<T>>();
        return static_cast<ComponentPool<T>*>(mPools[ID].get());
    }

    template <typename T>
    const IComponentPool* FindPool() const
    {
        const uint8_t ID = GetComponentTypeID<T>();
        return mPools[ID].get();
    }

    template <typename First, typename... Rest>
    const IComponentPool* SelectSmallestPool() const
    {
        const IComponentPool* Best = FindPool<First>();
        if (!Best)
            return nullptr;
        if constexpr (sizeof...(Rest) > 0)
        {
            const IComponentPool* Others[] = {FindPool<Rest>()...};
            for (const IComponentPool* Pool : Others)
            {
                if (!Pool)
                    return nullptr;
                if (Pool->Size() < Best->Size())
                    Best = Pool;
            }
        }
        return Best;
    }

    static void AddToMask(ComponentMask&)
    {
    }

    template <typename T>
    static void AddToMask(ComponentMask& Mask)
    {
        Mask.set(GetComponentTypeID<T>());
    }

    template <typename First, typename Second, typename... Rest>
    static void AddToMask(ComponentMask& Mask)
    {
        Mask.set(GetComponentTypeID<First>());
        AddToMask<Second, Rest...>(Mask);
    }

    template <typename... Components>
    static ComponentMask CreateMask()
    {
        ComponentMask Mask;
        AddToMask<Components...>(Mask);
        return Mask;
    }

    using PoolFactoryFn = std::unique_ptr<IComponentPool>(*)();
    inline static std::array<PoolFactoryFn, MaxComponents> sPoolFactories{};
    inline static thread_local EntityManager* sActiveWorld = nullptr;

    EntityID mLastEntityId = 0;
    size_t mLivingEntityCount = 0;
    uint64_t mMaskMutationVersion = 1;
    std::vector<bool> mAlive;
    std::vector<uint32_t> mGenerations;
    std::vector<ComponentMask> mMasks;
    std::deque<EntityID> mFreeIds;
    std::vector<EntityID> mActiveEntities;
    std::vector<size_t> mActiveEntityIndices;
    std::array<std::unique_ptr<IComponentPool>, MaxComponents> mPools;
    WorldContext mContext;
    EventBus mEventBus;
    EntityCommandBuffer mCommandBuffer;

    mutable std::unordered_map<uint64_t, CachedQueryEntry> mQueryCache;
    mutable std::array<std::vector<EntityID>, kMaxScratchDepth> mQueryScratchBuffers;
    mutable size_t mQueryScratchDepth = 0;
    mutable ECSQueryTelemetry mQueryTelemetry{};
};

template <typename T>
void ComponentPool<T>::RestorePoolBinary(EntityManager& Owner,
                                         uint8_t ComponentTypeSlot,
                                         const std::vector<EntityID>& Entities,
                                         const std::vector<uint8_t>& Bytes)
{
    Clear();
    if constexpr (std::is_trivially_copyable_v<T>)
    {
        if (Entities.empty() || Bytes.size() < Entities.size() * sizeof(T))
            return;
        for (size_t i = 0; i < Entities.size(); ++i)
        {
            const EntityID Id = Entities[i];
            if (!Owner.IsAlive(Id))
                continue;
            T& Slot = AddComponent(Id);
            std::memcpy(&Slot, Bytes.data() + i * sizeof(T), sizeof(T));
            Owner.SetEntityComponentBitInternal(Id, ComponentTypeSlot);
            NotifyConstructed(Owner, Id, Slot);
        }
    }
    else if constexpr (CustomComponentBinarySerializer<T>::kSupported)
    {
        size_t Cursor = 0;
        for (size_t i = 0; i < Entities.size(); ++i)
        {
            const EntityID Id = Entities[i];
            if (!Owner.IsAlive(Id))
                continue;
            T& Slot = AddComponent(Id);
            if (!CustomComponentBinarySerializer<T>::Deserialize(Bytes, Cursor, Slot))
                break;
            Owner.SetEntityComponentBitInternal(Id, ComponentTypeSlot);
            NotifyConstructed(Owner, Id, Slot);
        }
    }
}

template <typename T>
void ComponentPool<T>::RestoreFromClonedPool(EntityManager& Owner,
                                             uint8_t ComponentTypeSlot,
                                             const IComponentPool& ClonedPool)
{
    Clear();
    if constexpr (std::is_copy_assignable_v<T>)
    {
        const auto& TypedClone = static_cast<const ComponentPool<T>&>(ClonedPool);
        std::vector<EntityID> Sorted = TypedClone.GetDenseEntities();
        std::sort(Sorted.begin(), Sorted.end());
        for (EntityID Id : Sorted)
        {
            if (!Owner.IsAlive(Id))
                continue;
            if (const T* SrcComp = TypedClone.GetComponent(Id))
            {
                T& Slot = AddComponent(Id);
                Slot = *SrcComp;
                Owner.SetEntityComponentBitInternal(Id, ComponentTypeSlot);
                NotifyConstructed(Owner, Id, Slot);
            }
        }
    }
}

inline void EntityCommandBuffer::DestroyEntity(EntityID Id)
{
    mCommands.push_back([Id](EntityManager& EM) {
        EM.DestroyEntity(Id);
    });
}

template <typename T>
void EntityCommandBuffer::AddComponent(EntityID Id, const T& InitialValue)
{
    mCommands.push_back([Id, InitialValue](EntityManager& EM) {
        if (EM.IsAlive(Id))
            EM.AddComponent<T>(Id) = InitialValue;
    });
}

template <typename T>
void EntityCommandBuffer::RemoveComponent(EntityID Id)
{
    mCommands.push_back([Id](EntityManager& EM) {
        if (EM.IsAlive(Id))
            EM.RemoveComponent<T>(Id);
    });
}

} // namespace Core::ECS
