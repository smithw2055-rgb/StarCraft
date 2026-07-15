#pragma once

#include <cstdint>
#include <vector>
#include <bitset>
#include <memory>
#include <array>
#include <utility>
#include <unordered_map>
#include <algorithm>

namespace Core::ECS
{

using EntityID = int;
constexpr EntityID InvalidEntity = 0;
// Long-running melee matches create many short-lived projectile/effect entities.
// Entity IDs are currently monotonic, so the component pools must cover the
// whole session rather than only the peak number of simultaneously alive units.
constexpr size_t MaxEntities = 100000;
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
        mComponentData[Id] = std::make_unique<T>();
        mComponentMask[Id] = true;
        return *mComponentData[Id];
    }

    T* GetComponent(EntityID Id)
    {
        if (Id < MaxEntities && mComponentMask[Id])
            return mComponentData[Id].get();
        return nullptr;
    }

    void RemoveComponent(EntityID Id) override
    {
        if (Id < MaxEntities)
        {
            mComponentData[Id].reset();
            mComponentMask[Id] = false;
        }
    }

    bool HasComponent(EntityID Id) const
    {
        return Id < MaxEntities && mComponentMask[Id];
    }

private:
    std::array<std::unique_ptr<T>, MaxEntities> mComponentData;
    std::array<bool, MaxEntities> mComponentMask = {};
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
    size_t GetEntityCount() const { return mAlive.size() - std::count(mAlive.begin(), mAlive.end(), false); }

    template<typename T>
    T& AddComponent(EntityID Id)
    {
        auto* Pool = GetOrCreatePool<T>();
        mMasks[Id].set(GetComponentTypeID<T>());
        return Pool->AddComponent(Id);
    }

    template<typename T>
    T* GetComponent(EntityID Id)
    {
        auto* Pool = GetOrCreatePool<T>();
        return Pool->GetComponent(Id);
    }

    template<typename... Components>
    std::vector<EntityID> GetEntitiesWith()
    {
        std::vector<EntityID> Result;
        ComponentMask RequiredMask = CreateMask<Components...>();
        for (EntityID Id = 1; Id <= mLastEntityId; ++Id)
        {
            if (IsAlive(Id) && (mMasks[Id] & RequiredMask) == RequiredMask)
                Result.push_back(Id);
        }
        return Result;
    }

    template<typename... Components, typename Fn>
    void ForEachEntityWith(Fn&& Func)
    {
        ComponentMask RequiredMask = CreateMask<Components...>();
        for (EntityID Id = 1; Id <= mLastEntityId; ++Id)
        {
            if (IsAlive(Id) && (mMasks[Id] & RequiredMask) == RequiredMask)
                Func(Id);
        }
    }

private:
    template<typename T>
    ComponentPool<T>* GetOrCreatePool()
    {
        uint8_t ID = GetComponentTypeID<T>();
        auto It = mPools.find(ID);
        if (It == mPools.end())
        {
            auto Pool = std::make_unique<ComponentPool<T>>();
            auto* Ptr = Pool.get();
            mPools[ID] = std::move(Pool);
            return Ptr;
        }
        return static_cast<ComponentPool<T>*>(It->second.get());
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
    std::vector<bool> mAlive;
    std::vector<ComponentMask> mMasks;
    std::unordered_map<uint8_t, std::unique_ptr<IComponentPool>> mPools;
};

} // namespace Core::ECS
