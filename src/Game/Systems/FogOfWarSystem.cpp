#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Map/Map.h"
#include "Game/Components.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstring>

namespace Game::Systems
{
using Core::Utils::Logger;
using namespace Game::Components;

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

    size_t TotalTiles = (size_t)mMapWidth * mMapHeight;
    mFogGrid.resize(TotalTiles * mMaxPlayers, (uint8_t)FogState::Unexplored);
    mDetectorGrid.resize(TotalTiles * mMaxPlayers, false);

    // Alliance matrix: by default, only same-player is allied (self=allied)
    mAlliance.resize((size_t)mMaxPlayers * mMaxPlayers, false);
    for (int P = 0; P < mMaxPlayers; P++)
        SetAlliance(P, P, true);

    Logger::Info("FogOfWar",
        "Grid: " + std::to_string(mMapWidth) + "x" + std::to_string(mMapHeight) +
        " x " + std::to_string(mMaxPlayers) + " players (with detector + alliance support)");
}

void FogOfWarSystem::SetAlliance(int PlayerA, int PlayerB, bool Allied)
{
    if (PlayerA < 0 || PlayerA >= mMaxPlayers) return;
    if (PlayerB < 0 || PlayerB >= mMaxPlayers) return;
    mAlliance[(size_t)PlayerA * mMaxPlayers + PlayerB] = Allied;
    mAlliance[(size_t)PlayerB * mMaxPlayers + PlayerA] = Allied;
}

bool FogOfWarSystem::AreAllied(int PlayerA, int PlayerB) const
{
    if (PlayerA < 0 || PlayerA >= mMaxPlayers ||
        PlayerB < 0 || PlayerB >= mMaxPlayers)
    {
        return false;
    }
    return mAlliance[(size_t)PlayerA * mMaxPlayers + PlayerB];
}

FogState FogOfWarSystem::GetFog(int TileX, int TileY, int PlayerID) const
{
    if (mObserverRevealAll && PlayerID == mLocalPlayerID)
        return FogState::Visible;
    return GetGameplayFog(TileX, TileY, PlayerID);
}

FogState FogOfWarSystem::GetGameplayFog(int TileX, int TileY, int PlayerID) const
{
    if (TileX < 0 || TileX >= mMapWidth || TileY < 0 || TileY >= mMapHeight)
        return FogState::Unexplored;
    if (PlayerID < 0 || PlayerID >= mMaxPlayers)
        return FogState::Unexplored;

    // Check own visibility
    size_t Idx = (size_t)PlayerID * mMapWidth * mMapHeight + (size_t)TileY * mMapWidth + (size_t)TileX;
    uint8_t Best = mFogGrid[Idx];

    // Also check allied players' visibility
    for (int P = 0; P < mMaxPlayers; P++)
    {
        if (P == PlayerID) continue;
        if (mAlliance[(size_t)PlayerID * mMaxPlayers + P])
        {
            size_t AIdx = (size_t)P * mMapWidth * mMapHeight + (size_t)TileY * mMapWidth + (size_t)TileX;
            if (mFogGrid[AIdx] > Best)
                Best = mFogGrid[AIdx];
        }
    }

    return (FogState)Best;
}

bool FogOfWarSystem::IsVisible(int TileX, int TileY, int PlayerID) const
{
    return GetFog(TileX, TileY, PlayerID) == FogState::Visible;
}

bool FogOfWarSystem::IsGameplayVisible(int TileX, int TileY, int PlayerID) const
{
    return GetGameplayFog(TileX, TileY, PlayerID) == FogState::Visible;
}

bool FogOfWarSystem::IsExplored(int TileX, int TileY, int PlayerID) const
{
    return GetFog(TileX, TileY, PlayerID) >= FogState::Explored;
}

bool FogOfWarSystem::IsDetected(int TileX, int TileY, int PlayerID) const
{
    if (mObserverRevealAll && PlayerID == mLocalPlayerID)
        return true;
    return IsGameplayDetected(TileX, TileY, PlayerID);
}

bool FogOfWarSystem::IsGameplayDetected(int TileX, int TileY, int PlayerID) const
{
    if (TileX < 0 || TileX >= mMapWidth || TileY < 0 || TileY >= mMapHeight)
        return false;
    if (PlayerID < 0 || PlayerID >= mMaxPlayers)
        return false;

    size_t Idx = (size_t)PlayerID * mMapWidth * mMapHeight + (size_t)TileY * mMapWidth + (size_t)TileX;

    // Check own detectors
    if (mDetectorGrid[Idx]) return true;

    // Check allied detectors
    for (int P = 0; P < mMaxPlayers; P++)
    {
        if (P == PlayerID) continue;
        if (mAlliance[(size_t)PlayerID * mMaxPlayers + P])
        {
            size_t AIdx = (size_t)P * mMapWidth * mMapHeight + (size_t)TileY * mMapWidth + (size_t)TileX;
            if (mDetectorGrid[AIdx]) return true;
        }
    }

    return false;
}

void FogOfWarSystem::ResetVisibility()
{
    if (!mDetectorGrid.empty())
        std::memset(mDetectorGrid.data(), 0, mDetectorGrid.size());
    const size_t SliceSize = (size_t)mMapWidth * (size_t)mMapHeight;
    for (int P = 0; P < mMaxPlayers; P++)
    {
        size_t Base = (size_t)P * SliceSize;
        for (size_t I = 0; I < SliceSize; I++)
        {
            if (mFogGrid[Base + I] == (uint8_t)FogState::Visible)
                mFogGrid[Base + I] = (uint8_t)FogState::Explored;
        }
    }
}

void FogOfWarSystem::RevealAround(int CX, int CY, int SightRange, uint8_t* Grid, uint8_t* DetGrid)
{
    int R = SightRange;
    if (R < 1) R = 1;

    int MinY = std::max(0, CY - R);
    int MaxY = std::min(mMapHeight - 1, CY + R);
    const int R2 = R * R;

    for (int Y = MinY; Y <= MaxY; Y++)
    {
        const int DY = Y - CY;
        const int HalfSpan = (int)std::sqrt(R2 - DY * DY);
        const int MinX = std::max(0, CX - HalfSpan);
        const int MaxX = std::min(mMapWidth - 1, CX + HalfSpan);
        if (MinX > MaxX) continue;

        const size_t RowBase = (size_t)Y * (size_t)mMapWidth;
        if (Grid)
        {
            for (int X = MinX; X <= MaxX; X++)
            {
                if (Grid[RowBase + (size_t)X] < (uint8_t)FogState::Visible)
                    Grid[RowBase + (size_t)X] = (uint8_t)FogState::Visible;
            }
        }
        if (DetGrid)
        {
            std::memset(DetGrid + RowBase + MinX, 1, static_cast<size_t>(MaxX - MinX + 1));
        }
    }
}

void FogOfWarSystem::UpdateLastSeen(Core::ECS::EntityManager& EM, const std::vector<int>& Units)
{
    mLastSeen.clear();

    for (auto ID : Units)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Player = EM.GetComponent<PlayerComponent>(ID);
        if (!Unit || !Trans || !Player) continue;
        if (Unit->HitPoints <= 0) continue;

        int TileX = (int)(Trans->Position.X / 32.0f);
        int TileY = (int)(Trans->Position.Y / 32.0f);

        if (!IsVisible(TileX, TileY, mLocalPlayerID)) continue;

        // Check cloak
        auto* Cloak = EM.GetComponent<Components::CloakComponent>(ID);
        if (Cloak && (Cloak->IsCloaked || Cloak->IsBurrowed) &&
            !IsDetected(TileX, TileY, mLocalPlayerID))
            continue;

        LastSeenSnapshot Snap;
        Snap.UnitEntityID = ID;
        Snap.ChkUnitID = Unit->ChkUnitID;
        Snap.Position = Trans->Position;
        Snap.HitPoints = Unit->HitPoints;
        Snap.MaxHitPoints = Unit->MaxHitPoints;
        Snap.PlayerID = Player->PlayerID;
        Snap.Valid = true;
        mLastSeen.push_back(Snap);
    }
}

void FogOfWarSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    ResetVisibility();

    auto& EM = mSystemMgr.GetEntityManager();
    auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();

    for (auto ID : Units)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Player = EM.GetComponent<PlayerComponent>(ID);

        if (!Trans || !Unit || !Player) continue;
        if (Unit->HitPoints <= 0) continue;

        int PlayerID = Player->PlayerID;
        if (PlayerID < 0 || PlayerID >= mMaxPlayers) continue;

        int TileX = (int)(Trans->Position.X / 32.0f);
        int TileY = (int)(Trans->Position.Y / 32.0f);

        size_t SliceBase = (size_t)PlayerID * mMapWidth * mMapHeight;

        // Check status effects that reduce sight
        int EffectiveRange =
            Unit->SightRange + UpgradeSystem::GetSightRangeBonusTiles(EM, ID);
        auto* Status = EM.GetComponent<Components::StatusEffectComponent>(ID);
        const bool IsBlind = Status && Status->HasEffect(Components::StatusEffect::Blind);
        if (IsBlind)
            EffectiveRange = 2; // Optical Flare blind reduces vision to 2 tiles in Brood War

        bool BuildingCanDetect = true;
        const auto* Bldg = EM.GetComponent<BuildingComponent>(ID);
        if (Bldg)
        {
            if (Bldg->UnderConstruction)
            {
                EffectiveRange = 4; // Incomplete buildings have fixed sight range 4
                BuildingCanDetect = false;
            }
            else if (Bldg->RequiresPylon && !Bldg->HasPylonPower)
            {
                BuildingCanDetect = false;
            }
        }

        uint8_t* FogSlice = mFogGrid.data() + SliceBase;
        const bool CanDetect = Unit->IsDetector && !IsBlind && BuildingCanDetect;
        const bool IsDefenseDetector = (Unit->ChkUnitID == 102 || Unit->ChkUnitID == 144 || Unit->ChkUnitID == 162);

        if (CanDetect && IsDefenseDetector)
        {
            // Defense structures (Missile Turret, Spore Colony, Photon Cannon) have fixed detector range 7
            RevealAround(TileX, TileY, EffectiveRange, FogSlice, nullptr);
            RevealAround(TileX, TileY, 7, nullptr, mDetectorGrid.data() + SliceBase);
        }
        else
        {
            uint8_t* DetSlice = CanDetect ? (mDetectorGrid.data() + SliceBase) : nullptr;
            RevealAround(TileX, TileY, EffectiveRange, FogSlice, DetSlice);
        }

        if (Status)
        {
            for (int I = 0; I < Components::StatusEffectComponent::kMaxEffects; ++I)
            {
                if (Status->ActiveEffects[I] != Components::StatusEffect::Parasite ||
                    Status->EffectTimers[I] <= 0.0f)
                {
                    continue;
                }

                const int SourceEntityID = Status->EffectSources[I];
                auto* SourcePlayer = EM.GetComponent<PlayerComponent>(SourceEntityID);
                if (!SourcePlayer) continue;
                const int ParasitePlayerID = SourcePlayer->PlayerID;
                if (ParasitePlayerID < 0 || ParasitePlayerID >= mMaxPlayers) continue;

                size_t ParasiteSliceBase =
                    (size_t)ParasitePlayerID * mMapWidth * mMapHeight;
                uint8_t* ParasiteFogSlice = mFogGrid.data() + ParasiteSliceBase;
                RevealAround(TileX, TileY, EffectiveRange, ParasiteFogSlice, nullptr);
            }
        }
    }

    auto RevealAreas = EM.GetEntitiesWith<TransformComponent, RevealAreaComponent>();
    for (auto ID : RevealAreas)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Reveal = EM.GetComponent<RevealAreaComponent>(ID);
        if (!Trans || !Reveal) continue;

        if (Reveal->DurationRemaining <= 0.0f)
        {
            EM.DestroyEntity(ID);
            continue;
        }

        const int PlayerID = Reveal->PlayerID;
        if (PlayerID >= 0 && PlayerID < mMaxPlayers)
        {
            int TileX = (int)(Trans->Position.X / 32.0f);
            int TileY = (int)(Trans->Position.Y / 32.0f);
            size_t SliceBase = (size_t)PlayerID * mMapWidth * mMapHeight;
            uint8_t* FogSlice = mFogGrid.data() + SliceBase;
            uint8_t* DetSlice = Reveal->ProvidesDetection ?
                (mDetectorGrid.data() + SliceBase) : nullptr;
            RevealAround(TileX, TileY, Reveal->SightRange, FogSlice, DetSlice);
        }

        Reveal->DurationRemaining -= DeltaTime;
        if (Reveal->DurationRemaining <= 0.0f)
            EM.DestroyEntity(ID);
    }

    UpdateLastSeen(EM, Units);
}

} // namespace Game::Systems
