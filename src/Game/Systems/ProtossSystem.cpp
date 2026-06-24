#include "Game/Systems/ProtossSystem.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Core/Utils/Logger.h"

namespace Game::Systems
{

using namespace Game::Components;

Game::Map::GameMap* ProtossSystem::sMap = nullptr;

ProtossSystem::ProtossSystem(Core::ECS::SystemManager& SysMgr, Game::Map::GameMap* Map)
    : mSystemMgr(SysMgr)
{
    sMap = Map;
}

void ProtossSystem::SetMap(Game::Map::GameMap* Map)
{
    sMap = Map;
}

bool ProtossSystem::HasPower(int TileX, int TileY)
{
    if (!sMap) return true; // If no map, assume powered
    if (TileX < 0 || TileX >= sMap->GetWidth() ||
        TileY < 0 || TileY >= sMap->GetHeight())
        return false;
    return sMap->GetTile(TileX, TileY).Buildable; // Reuse buildable flag for power
}

void ProtossSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;
    auto& EM = mSystemMgr.GetEntityManager();
    UpdatePowerGrid(EM);
    RechargeShields(EM, DeltaTime);
}

void ProtossSystem::UpdatePowerGrid(Core::ECS::EntityManager& EM)
{
    if (!sMap) return;
    int W = sMap->GetWidth();
    int H = sMap->GetHeight();

    // Reset all tiles to not powered
    for (int Y = 0; Y < H; Y++)
        for (int X = 0; X < W; X++)
        {
            auto T = sMap->GetTile(X, Y);
            T.Buildable = false; // Reset power
            sMap->SetTile(X, Y, T);
        }

    // Find Pylons and provide power in 7x7 area
    auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        if (!U || !T || U->ChkUnitID != 127) continue; // Pylon = 127
        if (U->HitPoints <= 0) continue;

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);
        int Radius = 4;

        for (int DY = -Radius; DY <= Radius; DY++)
            for (int DX = -Radius; DX <= Radius; DX++)
            {
                int X = TX + DX, Y = TY + DY;
                if (X >= 0 && X < W && Y >= 0 && Y < H)
                {
                    auto Tile = sMap->GetTile(X, Y);
                    Tile.Buildable = true;
                    sMap->SetTile(X, Y, Tile);
                }
            }
    }

    // Check all Protoss buildings for power
    auto Buildings = EM.GetEntitiesWith<TransformComponent, BuildingComponent, PlayerComponent>();
    for (int Eid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!B || !T || !P || !B->RequiresPylon) continue;

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);
        bool Powered = false;
        for (int DY = -1; DY <= 1 && !Powered; DY++)
            for (int DX = -1; DX <= 1 && !Powered; DX++)
            {
                int X = TX + DX, Y = TY + DY;
                if (X >= 0 && X < W && Y >= 0 && Y < H)
                    if (sMap->GetTile(X, Y).Buildable)
                        Powered = true;
            }
        B->HasPylonPower = Powered;
    }
}

void ProtossSystem::RechargeShields(Core::ECS::EntityManager& EM, float DeltaTime)
{
    // Shield Battery: recharge nearby units
    auto Units = EM.GetEntitiesWith<UnitComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        if (!U || U->MaxShieldPoints <= 0) continue;
        if (U->ShieldPoints >= U->MaxShieldPoints) continue;
        // Natural shield regen (1/sec) is already in CombatSystem
        // This is just an extra boost near Shield Batteries
    }
}

} // namespace Game::Systems