#include "Game/sokol/Entity.h"

namespace Game::SokolDemo
{

void EntityManager::Clear()
{
    mEntities.clear();
    mNextId = 1;
}

int EntityManager::AddEntity(EntityKind Kind)
{
    Entity E;
    E.Id = mNextId++;
    E.Kind = Kind;
    mEntities.push_back(E);
    return E.Id;
}

Entity& EntityManager::GetEntity(int Id)
{
    for (auto& E : mEntities)
        if (E.Id == Id) return E;
    static Entity Dummy;
    return Dummy;
}

const Entity& EntityManager::GetEntity(int Id) const
{
    for (const auto& E : mEntities)
        if (E.Id == Id) return E;
    static Entity Dummy;
    return Dummy;
}

int EntityManager::SpawnCommandCenter(int PlayerId, int WorldX, int WorldY, int ChkId, int FrameIndex)
{
    int Id = AddEntity(EntityKind::Building);
    Entity& E = GetEntity(Id);
    E.X = static_cast<float>(WorldX);
    E.Y = static_cast<float>(WorldY);
    E.ChkUnitId = ChkId;
    E.ImageId = ChkId;
    E.PlayerId = PlayerId;
    E.HitPoints = E.MaxHitPoints = 1500;
    E.Size = UnitSize::Large;
    E.FrameIndex = FrameIndex;
    E.Visible = true;
    return Id;
}

int EntityManager::SpawnScv(int PlayerId, int WorldX, int WorldY, int ChkId, int FrameIndex)
{
    int Id = AddEntity(EntityKind::Unit);
    Entity& E = GetEntity(Id);
    E.X = static_cast<float>(WorldX);
    E.Y = static_cast<float>(WorldY);
    E.ChkUnitId = ChkId;
    E.ImageId = ChkId;
    E.PlayerId = PlayerId;
    E.HitPoints = E.MaxHitPoints = 60;
    E.Size = UnitSize::Small;
    E.FrameIndex = FrameIndex;
    E.Visible = true;
    return Id;
}

int EntityManager::SpawnStartUnits(const GameMap& Map, int PlayerId, int SpawnX, int SpawnY,
                                   int CommandCenterChkId, int ScvChkId,
                                   int Frame0CommandCenter, int Frame0Scv)
{
    (void)Map;
    SpawnCommandCenter(PlayerId, SpawnX, SpawnY, CommandCenterChkId, Frame0CommandCenter);
    for (int J = 0; J < 4; J++)
    {
        SpawnScv(PlayerId,
                 SpawnX - 30 + J * 20,
                 SpawnY + 50,
                 ScvChkId, Frame0Scv);
    }
    return PlayerId;
}
} // namespace Game::SokolDemo
