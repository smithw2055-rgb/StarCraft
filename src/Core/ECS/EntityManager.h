#pragma once

#include <cstdint>
#include <vector>
#include <bitset>
#include <memory>
#include <array>
#include <deque>
#include <utility>
#include <algorithm>

namespace Core::ECS
{

using EntityID = int;
constexpr EntityID InvalidEntity = 0;
constexpr size_t MaxComponents = 64;

using ComponentMask = std::bitset<MaxComponents>;

class IComponentPool
{
public:
    virtual ~IComponentPool() = default;
    virtual void RemoveComponent(EntityID Id) = 0;
};

template<typename T>
class ComponentPool : public IComponentPool
{
public:
    T& AddComponent(EntityID Id)
    {
        if (Id >= (EntityID)mComponentData.size())
        {
            mComponentData.resize((size_t)Id + 1);
            mComponentMask.resize((size_t)Id + 1, false);
        }
        if (!mComponentMask[(size_t)Id])
            ++mActiveCount;
        mComponentData[(size_t)Id] = std::make_unique<T>();
        mComponentMask[(size_t)Id] = true;
        return *mComponentData[(size_t)Id];
    }

    T* GetComponent(EntityID Id)
    {
        if (Id >= 0 && Id < (EntityID)mComponentMask.size() && mComponentMask[(size_t)Id])
            return mComponentData[(size_t)Id].get();
        return nullptr;
    }

    void RemoveComponent(EntityID Id) override
    {
        if (Id >= 0 && Id < (EntityID)mComponentMask.size() && mComponentMask[(size_t)Id])
        {
            mComponentData[(size_t)Id].reset();
            mComponentMask[(size_t)Id] = false;
            if (mActiveCount > 0)
                --mActiveCount;
        }
    }

    bool HasComponent(EntityID Id) const
    {
        return Id >= 0 && Id < (EntityID)mComponentMask.size() && mComponentMask[(size_t)Id];
    }

    size_t Size() const { return mActiveCount; }

private:
    size_t mActiveCount = 0;
    std::vector<std::unique_ptr<T>> mComponentData;
    std::vector<bool> mComponentMask;
};

inline uint8_t NextComponentTypeID = 0;

template<typename T>
uint8_t GetComponentTypeID()
{
    static uint8_t ID = NextComponentTypeID++;
    return ID;
}

class EntityManager
{
public:
    EntityID CreateEntity();
    void DestroyEntity(EntityID Id);
    bool IsAlive(EntityID Id) const;
    ComponentMask GetMask(EntityID Id) const;
    size_t GetEntityCount() const { return mLivingEntityCount; }

    template<typename T>
    T& AddComponent(EntityID Id)
    {
        auto* Pool = GetOrCreatePool<T>();
        if ((size_t)Id >= mMasks.size())
        {
            mMasks.resize((size_t)Id + 1);
            mAlive.resize((size_t)Id + 1, false);
        }
        mMasks[(size_t)Id].set(GetComponentTypeID<T>());
        return Pool->AddComponent(Id);
    }

    template<typename T>
    void RemoveComponent(EntityID Id)
    {
        uint8_t ID = GetComponentTypeID<T>();
        if (Id > 0 && (size_t)Id < mMasks.size())
            mMasks[(size_t)Id].reset(ID);
        if (mPools[ID])
            mPools[ID]->RemoveComponent(Id);
    }

    template<typename T>
    T* GetComponent(EntityID Id)
    {
        if (Id <= 0)
            return nullptr;
        uint8_t ID = GetComponentTypeID<T>();
        if (!mPools[ID])
            return nullptr;
        return static_cast<ComponentPool<T>*>(mPools[ID].get())->GetComponent(Id);
    }

    template<typename T>
    size_t GetComponentCount() const
    {
        uint8_t ID = GetComponentTypeID<T>();
        if (!mPools[ID]) return 0;
        return static_cast<const ComponentPool<T>*>(mPools[ID].get())->Size();
    }

    template<typename T>
    bool HasAnyComponent() const
    {
        return GetComponentCount<T>() > 0;
    }

    template<typename... Components>
    std::vector<EntityID> GetEntitiesWith()
    {
        std::vector<EntityID> Result;
        ComponentMask RequiredMask = CreateMask<Components...>();
        for (EntityID Id : mActiveEntities)
        {
            if ((mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                Result.push_back(Id);
        }
        return Result;
    }

    template<typename... Components, typename Fn>
    void ForEachEntityWith(Fn&& Func)
    {
        ComponentMask RequiredMask = CreateMask<Components...>();
        const size_t Count = mActiveEntities.size();
        for (size_t i = 0; i < Count && i < mActiveEntities.size(); ++i)
        {
            EntityID Id = mActiveEntities[i];
            if ((mMasks[(size_t)Id] & RequiredMask) == RequiredMask)
                Func(Id);
        }
    }

private:
    template<typename T>
    ComponentPool<T>* GetOrCreatePool()
    {
        uint8_t ID = GetComponentTypeID<T>();
        if (!mPools[ID])
        {
            mPools[ID] = std::make_unique<ComponentPool<T>>();
        }
        return static_cast<ComponentPool<T>*>(mPools[ID].get());
    }

    void AddToMask(ComponentMask&)
    {
        // Zero-arg base case: nothing to set
    }

    template<typename T>
    void AddToMask(ComponentMask& Mask)
    {
        Mask.set(GetComponentTypeID<T>());
    }

    template<typename First, typename Second, typename... Rest>
    void AddToMask(ComponentMask& Mask)
    {
        Mask.set(GetComponentTypeID<First>());
        AddToMask<Second, Rest...>(Mask);
    }

    template<typename... Components>
    ComponentMask CreateMask()
    {
        ComponentMask Mask;
        AddToMask<Components...>(Mask);
        return Mask;
    }

    EntityID mLastEntityId = 0;
    size_t mLivingEntityCount = 0;
    std::vector<bool> mAlive;
    std::vector<ComponentMask> mMasks;
    std::deque<EntityID> mFreeIds;
    std::vector<EntityID> mActiveEntities;
    std::vector<size_t> mActiveEntityIndices;
    std::array<std::unique_ptr<IComponentPool>, MaxComponents> mPools;
};

} // namespace Core::ECS
