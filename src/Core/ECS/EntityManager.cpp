#include "Core/ECS/EntityManager.h"

namespace Core::ECS
{

EntityID EntityManager::CreateEntity()
{
    ++mLastEntityId;
    if (mAlive.size() <= mLastEntityId)
    {
        mAlive.resize(mLastEntityId + 1, false);
        mMasks.resize(mLastEntityId + 1);
    }
    mAlive[mLastEntityId] = true;
    mMasks[mLastEntityId].reset();
    return mLastEntityId;
}

void EntityManager::DestroyEntity(EntityID Id)
{
    if (Id < mAlive.size())
    {
        mAlive[Id] = false;
        mMasks[Id].reset();
    }
}

bool EntityManager::IsAlive(EntityID Id) const
{
    return Id < mAlive.size() && mAlive[Id];
}

ComponentMask EntityManager::GetMask(EntityID Id) const
{
    if (Id < mMasks.size())
        return mMasks[Id];
    return {};
}

} // namespace Core::ECS
