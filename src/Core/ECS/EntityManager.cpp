#include "Core/ECS/EntityManager.h"

namespace Core::ECS
{

EntityManager::EntityManager() = default;

EntityManager::~EntityManager()
{
    if (sActiveWorld == this)
        sActiveWorld = nullptr;
}

EntityID EntityManager::CreateEntity()
{
    constexpr size_t FreeIdReuseThreshold = 1024;
    EntityID Id = 0;
    if (mFreeIds.size() > FreeIdReuseThreshold)
    {
        while (!mFreeIds.empty())
        {
            EntityID Candidate = mFreeIds.front();
            mFreeIds.pop_front();
            if (Candidate > 0 && (size_t)Candidate < mAlive.size() && !mAlive[(size_t)Candidate])
            {
                Id = Candidate;
                for (size_t CompID = 0; CompID < MaxComponents; ++CompID)
                {
                    if (mPools[CompID])
                        mPools[CompID]->RemoveComponent(Id);
                }
                break;
            }
        }
    }

    if (Id == 0)
    {
        Id = ++mLastEntityId;
    }

    if (Id > mLastEntityId)
        mLastEntityId = Id;

    if (mAlive.size() <= (size_t)Id)
    {
        mAlive.resize((size_t)Id + 1, false);
        mGenerations.resize((size_t)Id + 1, 0);
        mMasks.resize((size_t)Id + 1);
    }
    mAlive[(size_t)Id] = true;
    if (mGenerations[(size_t)Id] == 0)
        mGenerations[(size_t)Id] = 1;
    mMasks[(size_t)Id].reset();
    ++mLivingEntityCount;

    if (mActiveEntityIndices.size() <= (size_t)Id)
        mActiveEntityIndices.resize((size_t)Id + 1, SIZE_MAX);
    mActiveEntityIndices[(size_t)Id] = mActiveEntities.size();
    mActiveEntities.push_back(Id);

    return Id;
}

EntityHandle EntityManager::CreateEntityHandle()
{
    const EntityID Id = CreateEntity();
    return EntityHandle{Id, GetGeneration(Id)};
}

EntityID EntityManager::CreateEntityWithID(EntityID Id, uint32_t Generation)
{
    if (Id <= 0) return CreateEntity();
    if (IsAlive(Id)) DestroyEntity(Id);

    mFreeIds.erase(std::remove(mFreeIds.begin(), mFreeIds.end(), Id), mFreeIds.end());
    if (Id > mLastEntityId) mLastEntityId = Id;

    if ((size_t)Id >= mMasks.size()) {
        mMasks.resize((size_t)Id + 1);
        mAlive.resize((size_t)Id + 1, false);
        mGenerations.resize((size_t)Id + 1, 0);
        mActiveEntityIndices.resize((size_t)Id + 1, SIZE_MAX);
    }
    for (size_t i = 0; i < MaxComponents; ++i) {
        if (mPools[i]) mPools[i]->RemoveComponent(Id);
    }

    mMasks[(size_t)Id].reset();
    mAlive[(size_t)Id] = true;
    mGenerations[(size_t)Id] = Generation == 0 ? 1 : Generation;
    mActiveEntityIndices[(size_t)Id] = mActiveEntities.size();
    mActiveEntities.push_back(Id);
    mLivingEntityCount++;
    return Id;
}

void EntityManager::DestroyEntity(EntityID Id)
{
    if (Id > 0 && (size_t)Id < mAlive.size() && mAlive[(size_t)Id])
    {
        mAlive[(size_t)Id] = false;
        ++mGenerations[(size_t)Id];
        if (mGenerations[(size_t)Id] == 0)
            mGenerations[(size_t)Id] = 1;

        if (mLivingEntityCount > 0)
            --mLivingEntityCount;

        if ((size_t)Id < mActiveEntityIndices.size() && mActiveEntityIndices[(size_t)Id] != SIZE_MAX)
        {
            size_t Idx = mActiveEntityIndices[(size_t)Id];
            EntityID LastEntity = mActiveEntities.back();
            mActiveEntities[Idx] = LastEntity;
            mActiveEntityIndices[(size_t)LastEntity] = Idx;
            mActiveEntities.pop_back();
            mActiveEntityIndices[(size_t)Id] = SIZE_MAX;
        }

        mMasks[(size_t)Id].reset();
        mFreeIds.push_back(Id);
    }
}

bool EntityManager::DestroyEntity(EntityHandle Handle)
{
    if (!IsAlive(Handle))
        return false;
    DestroyEntity(Handle.Id);
    return true;
}

bool EntityManager::IsAlive(EntityID Id) const
{
    return Id > 0 && (size_t)Id < mAlive.size() && mAlive[(size_t)Id];
}

bool EntityManager::IsAlive(EntityHandle Handle) const
{
    return Handle.IsValid() &&
           IsAlive(Handle.Id) &&
           (size_t)Handle.Id < mGenerations.size() &&
           mGenerations[(size_t)Handle.Id] == Handle.Generation;
}

uint32_t EntityManager::GetGeneration(EntityID Id) const
{
    if (Id > 0 && (size_t)Id < mGenerations.size())
        return mGenerations[(size_t)Id];
    return 0;
}

EntityHandle EntityManager::GetEntityHandle(EntityID Id) const
{
    if (!IsAlive(Id))
        return {};
    return EntityHandle{Id, GetGeneration(Id)};
}

ComponentMask EntityManager::GetMask(EntityID Id) const
{
    if (Id > 0 && (size_t)Id < mMasks.size())
        return mMasks[(size_t)Id];
    return {};
}

} // namespace Core::ECS
