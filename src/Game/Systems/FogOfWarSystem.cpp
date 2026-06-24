#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Map/Map.h"
#include "Game/Components.h"
#include "Core/Utils/Logger.h"

#include <algorithm>

namespace Game::Systems
{

FogOfWarSystem::FogOfWarSystem(Core::ECS::SystemManager& SysMgr, const Game::Map::GameMap* Map)
    : mSystemMgr(SysMgr), mMap(Map)
{
    if (mMap)
    {
        mMapWidth = mMap->GetWidth();
        mMapHeight = mMap->GetHeight();
    }
    else
    {
        mMapWidth = 128;
        mMapHeight = 128;
    }

    mFogGrid.resize(mMapWidth * mMapHeight * mMaxPlayers, (uint8_t)FogState::Unexplored);
    Core::Utils::Logger::Info("FogOfWar",
        "Grid: " + std::to_string(mMapWidth) + "x" + std::to_string(mMapHeight) +
        " x " + std::to_string(mMaxPlayers) + " players");
}

FogState FogOfWarSystem::GetFog(int TileX, int TileY, int PlayerID) const
{
    if (TileX < 0 || TileX >= mMapWidth || TileY < 0 || TileY >= mMapHeight)
        return FogState::Unexplored;
    if (PlayerID < 0 || PlayerID >= mMaxPlayers)
        return FogState::Unexplored;
    size_t Idx = (size_t)PlayerID * mMapWidth * mMapHeight + (size_t)TileY * mMapWidth + (size_t)TileX;
    return (FogState)mFogGrid[Idx];
}

bool FogOfWarSystem::IsVisible(int TileX, int TileY, int PlayerID) const
{
    return GetFog(TileX, TileY, PlayerID) == FogState::Visible;
}

bool FogOfWarSystem::IsExplored(int TileX, int TileY, int PlayerID) const
{
    return GetFog(TileX, TileY, PlayerID) >= FogState::Explored;
}

void FogOfWarSystem::ResetVisibility()
{
    // Demote all Visible tiles to Explored
    for (int P = 0; P < mMaxPlayers; P++)
    {
        size_t Base = (size_t)P * mMapWidth * mMapHeight;
        for (int I = 0; I < mMapWidth * mMapHeight; I++)
        {
            if (mFogGrid[Base + I] == (uint8_t)FogState::Visible)
                mFogGrid[Base + I] = (uint8_t)FogState::Explored;
        }
    }
}

void FogOfWarSystem::RevealAround(int CX, int CY, int SightRange, uint8_t* Grid)
{
    int R = SightRange;
    if (R < 1) R = 1;

    int MinX = std::max(0, CX - R);
    int MaxX = std::min(mMapWidth - 1, CX + R);
    int MinY = std::max(0, CY - R);
    int MaxY = std::min(mMapHeight - 1, CY + R);

    for (int Y = MinY; Y <= MaxY; Y++)
    {
        int RowBase = Y * mMapWidth;
        for (int X = MinX; X <= MaxX; X++)
        {
            int DX = X - CX;
            int DY = Y - CY;
            if (DX * DX + DY * DY <= R * R)
            {
                size_t Idx = (size_t)RowBase + (size_t)X;
                if (Grid[Idx] < (uint8_t)FogState::Visible)
                    Grid[Idx] = (uint8_t)FogState::Visible;
            }
        }
    }
}

void FogOfWarSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    ResetVisibility();

    auto& EM = mSystemMgr.GetEntityManager();
    auto Units = EM.GetEntitiesWith<Game::Components::TransformComponent,
                                     Game::Components::UnitComponent>();

    for (auto ID : Units)
    {
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(ID);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(ID);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(ID);

        if (!Trans || !Unit || !Player) continue;
        if (Unit->HitPoints <= 0) continue;

        int PlayerID = Player->PlayerID;
        if (PlayerID < 0 || PlayerID >= mMaxPlayers) continue;

        int TileX = (int)(Trans->Position.X / 32.0f);
        int TileY = (int)(Trans->Position.Y / 32.0f);

        // Get this player's grid slice
        size_t SliceBase = (size_t)PlayerID * mMapWidth * mMapHeight;
        RevealAround(TileX, TileY, Unit->SightRange, mFogGrid.data() + SliceBase);
    }
}

} // namespace Game::Systems
