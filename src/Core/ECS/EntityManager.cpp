#include "Core/ECS/EntityManager.h"

namespace Core::ECS
{

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
        mMasks.resize((size_t)Id + 1);
    }
    mAlive[(size_t)Id] = true;
    mMasks[(size_t)Id].reset();
    ++mLivingEntityCount;

    if (mActiveEntityIndices.size() <= (size_t)Id)
        mActiveEntityIndices.resize((size_t)Id + 1, SIZE_MAX);
    mActiveEntityIndices[(size_t)Id] = mActiveEntities.size();
    mActiveEntities.push_back(Id);

    return Id;
}

void EntityManager::DestroyEntity(EntityID Id)
{
    if (Id > 0 && (size_t)Id < mAlive.size() && mAlive[(size_t)Id])
    {
        mAlive[(size_t)Id] = false;
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

bool EntityManager::IsAlive(EntityID Id) const
{
    return Id > 0 && (size_t)Id < mAlive.size() && mAlive[(size_t)Id];
}

ComponentMask EntityManager::GetMask(EntityID Id) const
{
    if (Id > 0 && (size_t)Id < mMasks.size())
        return mMasks[(size_t)Id];
    return {};
}

} // namespace Core::ECS
