#pragma once

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "Core/ECS/EntityCommandBuffer.h"
#include "Core/ECS/EventBus.h"
#include "Core/ECS/WorldContext.h"

namespace Core::ECS
{

using EntityID = int;
constexpr EntityID InvalidEntity = 0;
constexpr size_t MaxComponents = 64;
constexpr size_t MaxResources = 64;

using ComponentMask = std::bitset<MaxComponents>;

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

class IComponentPool
{
public:
    virtual ~IComponentPool() = default;
    virtual void RemoveComponent(EntityID Id) = 0;
    virtual bool HasComponent(EntityID Id) const = 0;
    virtual size_t Size() const = 0;
    virtual const std::vector<EntityID>& GetDenseEntities() const = 0;
    virtual void Clear() = 0;
};

// Paged Sparse-Set ComponentPool:
// - Stores component instances in contiguous fixed-size pages (kPageSize = 256)
//   eliminating per-entity heap allocations while preserving 100% pointer/reference
//   stability across AddComponent/RemoveComponent calls.
// - Maintains a compact Sparse-Set (mSparseToDense + mDenseEntities) for O(1)
//   membership checks, O(1) removal (swap-and-pop), and cache-friendly dense iteration.
template <typename T>
class ComponentPool final : public IComponentPool
{
public:
    static constexpr size_t kPageSize = 256;
    static constexpr size_t kInvalidDenseIndex = std::numeric_limits<size_t>::max();

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
};

inline uint8_t NextComponentTypeID = 0;

template <typename T>
uint8_t GetComponentTypeID()
{
    static uint8_t ID = NextComponentTypeID++;
    return ID;
}

class IWorldResource
{
public:
    virtual ~IWorldResource() = default;
};

template <typename T>
class WorldResourceHolder final : public IWorldResource
{
public:
    template <typename... Args>
    explicit WorldResourceHolder(Args&&... InArgs)
        : Value(std::forward<Args>(InArgs)...)
    {
    }

    T Value;
};

inline uint8_t NextResourceTypeID = 0;

template <typename T>
uint8_t GetResourceTypeID()
{
    static uint8_t ID = NextResourceTypeID++;
    return ID;
}

class EntityManager
{
public:
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

    bool IsAlive(EntityID Id) const;
    bool IsAlive(EntityHandle Handle) const;
    uint32_t GetGeneration(EntityID Id) const;
    EntityHandle GetEntityHandle(EntityID Id) const;

    ComponentMask GetMask(EntityID Id) const;
    size_t GetEntityCount() const { return mLivingEntityCount; }
    size_t GetEntityCapacity() const { return mAlive.size(); }
    const std::vector<EntityID>& GetActiveEntities() const { return mActiveEntities; }
    const std::vector<EntityID>& GetAliveEntities() const { return mActiveEntities; }

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
        return Pool->AddComponent(Id);
    }

    template <typename T>
    void RemoveComponent(EntityID Id)
    {
        const uint8_t ID = GetComponentTypeID<T>();
        if (Id > 0 && (size_t)Id < mMasks.size())
            mMasks[(size_t)Id].reset(ID);
        if (mPools[ID])
            mPools[ID]->RemoveComponent(Id);
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

    template <typename... Components>
    std::vector<EntityID> GetEntitiesWith() const
    {
        std::vector<EntityID> Result;
        const ComponentMask RequiredMask = CreateMask<Components...>();
        for (EntityID Id : mActiveEntities)
        {
            if ((mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                Result.push_back(Id);
        }
        return Result;
    }

    template <typename... Components, typename Fn>
    void ForEachEntityWith(Fn&& Func)
    {
        const ComponentMask RequiredMask = CreateMask<Components...>();
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

    template <typename... Components, typename Fn>
    void ForEachEntityWith(Fn&& Func) const
    {
        const ComponentMask RequiredMask = CreateMask<Components...>();
        for (size_t i = 0; i < mActiveEntities.size(); ++i)
        {
            const EntityID Id = mActiveEntities[i];
            if ((mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                Func(Id);
        }
    }

    // EnTT / Unity DOTS style View iteration: selects the smallest component pool
    // among Components... as the iteration pivot and invokes Func(EntityID, Components&...).
    template <typename FirstComponent, typename... RestComponents, typename Fn>
    void ForEachComponent(Fn&& Func)
    {
        const IComponentPool* PivotPool = SelectSmallestPool<FirstComponent, RestComponents...>();
        if (!PivotPool || PivotPool->Size() == 0)
            return;

        const ComponentMask RequiredMask = CreateMask<FirstComponent, RestComponents...>();
        // Copy dense entity list if small or iterate snapshot safely
        const std::vector<EntityID> Candidates = PivotPool->GetDenseEntities();
        for (EntityID Id : Candidates)
        {
            if (!IsAlive(Id) || (mMasks[(size_t)Id] & RequiredMask) != RequiredMask)
                continue;
            Func(Id, *GetComponent<FirstComponent>(Id), *GetComponent<RestComponents>(Id)...);
        }
    }

    template <typename... Components, typename Predicate>
    bool AnyEntityWith(Predicate&& Pred) const
    {
        const ComponentMask RequiredMask = CreateMask<Components...>();
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
    // World Singleton Resources (Unity DOTS Singleton / Bevy Resource style)
    // =========================================================================
    template <typename T, typename... Args>
    T& SetResource(Args&&... InArgs)
    {
        const uint8_t ID = GetResourceTypeID<T>();
        auto Holder = std::make_unique<WorldResourceHolder<T>>(std::forward<Args>(InArgs)...);
        T& Ref = Holder->Value;
        mResources[ID] = std::move(Holder);
        return Ref;
    }

    template <typename T>
    T& GetOrCreateResource()
    {
        const uint8_t ID = GetResourceTypeID<T>();
        if (!mResources[ID])
            mResources[ID] = std::make_unique<WorldResourceHolder<T>>();
        return static_cast<WorldResourceHolder<T>*>(mResources[ID].get())->Value;
    }

    template <typename T>
    const T& GetOrCreateResource() const
    {
        return const_cast<EntityManager*>(this)->GetOrCreateResource<T>();
    }

    template <typename T>
    T* GetResource()
    {
        const uint8_t ID = GetResourceTypeID<T>();
        if (!mResources[ID])
            return nullptr;
        return &static_cast<WorldResourceHolder<T>*>(mResources[ID].get())->Value;
    }

    template <typename T>
    const T* GetResource() const
    {
        const uint8_t ID = GetResourceTypeID<T>();
        if (!mResources[ID])
            return nullptr;
        return &static_cast<const WorldResourceHolder<T>*>(mResources[ID].get())->Value;
    }

    template <typename T>
    bool HasResource() const
    {
        const uint8_t ID = GetResourceTypeID<T>();
        return mResources[ID] != nullptr;
    }

    template <typename T>
    void RemoveResource()
    {
        const uint8_t ID = GetResourceTypeID<T>();
        mResources[ID].reset();
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

private:
    template <typename T>
    ComponentPool<T>* GetOrCreatePool()
    {
        const uint8_t ID = GetComponentTypeID<T>();
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

    inline static thread_local EntityManager* sActiveWorld = nullptr;

    EntityID mLastEntityId = 0;
    size_t mLivingEntityCount = 0;
    std::vector<bool> mAlive;
    std::vector<uint32_t> mGenerations;
    std::vector<ComponentMask> mMasks;
    std::deque<EntityID> mFreeIds;
    std::vector<EntityID> mActiveEntities;
    std::vector<size_t> mActiveEntityIndices;
    std::array<std::unique_ptr<IComponentPool>, MaxComponents> mPools;
    std::array<std::unique_ptr<IWorldResource>, MaxResources> mResources;
    WorldContext mContext;
    EventBus mEventBus;
    EntityCommandBuffer mCommandBuffer;
};

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
