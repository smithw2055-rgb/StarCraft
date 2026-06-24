#include "Game/Systems/ZergSystem.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Core/Utils/Logger.h"

#include <cmath>

namespace Game::Systems
{

using namespace Game::Components;

Game::Map::GameMap* ZergSystem::sMap = nullptr;

ZergSystem::ZergSystem(Core::ECS::SystemManager& SysMgr, Game::Map::GameMap* Map)
    : mSystemMgr(SysMgr)
{
    sMap = Map;
    if (Map)
    {
        mMapWidth = Map->GetWidth();
        mMapHeight = Map->GetHeight();
    }
}

void ZergSystem::SetMap(Game::Map::GameMap* Map)
{
    sMap = Map;
}

bool ZergSystem::HasCreep(int TileX, int TileY)
{
    if (!sMap) return false;
    if (TileX < 0 || TileX >= sMap->GetWidth() ||
        TileY < 0 || TileY >= sMap->GetHeight())
        return false;
    return sMap->GetTile(TileX, TileY).HasCreep;
}

void ZergSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;
    auto& EM = mSystemMgr.GetEntityManager();

    // Initialize creep grid from map
    if (!mGridInitialized && sMap)
    {
        mMapWidth = sMap->GetWidth();
        mMapHeight = sMap->GetHeight();
        mCreepGrid.resize(mMapWidth * mMapHeight, 0);
        mGridInitialized = true;
    }

    if (!sMap || mCreepGrid.empty()) return;

    SpreadCreep(EM);
    ProcessLarvae(EM, DeltaTime);
}

void ZergSystem::SpreadCreep(Core::ECS::EntityManager& EM)
{
    mCreepTimer += 0.016f; // Approximate per-frame timing
    if (mCreepTimer < mCreepInterval) return;
    mCreepTimer = 0.0f;

    // Reset creep grid
    std::fill(mCreepGrid.begin(), mCreepGrid.end(), (uint8_t)0);

    // Find all organic Zerg buildings
    auto Buildings = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
    for (int Eid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        if (!B || !T || !B->IsOrganic || B->UnderConstruction) continue;

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);
        int Radius = 6; // tiles

        for (int DY = -Radius; DY <= Radius; DY++)
        {
            for (int DX = -Radius; DX <= Radius; DX++)
            {
                int X = TX + DX, Y = TY + DY;
                if (X < 0 || X >= mMapWidth || Y < 0 || Y >= mMapHeight)
                    continue;
                if (DX * DX + DY * DY <= Radius * Radius)
                    mCreepGrid[Y * mMapWidth + X] = 1;
            }
        }
    }

    // Sync back to GameMap tiles
    for (int Y = 0; Y < mMapHeight; Y++)
        for (int X = 0; X < mMapWidth; X++)
        {
            auto Tile = sMap->GetTile(X, Y);
            Tile.HasCreep = (mCreepGrid[Y * mMapWidth + X] != 0);
            sMap->SetTile(X, Y, Tile);
        }
}

void ZergSystem::ProcessLarvae(Core::ECS::EntityManager& EM, float DeltaTime)
{
    auto Buildings = EM.GetEntitiesWith<BuildingComponent>();
    for (int Eid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        if (!B || !B->IsOrganic || B->UnderConstruction) continue;

        // Spawn larvae
        if (B->LarvaCount < 3)
        {
            B->LarvaTimer += DeltaTime;
            if (B->LarvaTimer >= 15.0f) // 1 larva every 15 seconds
            {
                B->LarvaTimer = 0.0f;
                B->LarvaCount++;
            }
        }
    }
}

} // namespace Game::Systems