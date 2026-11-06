#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Map/Map.h"
#include "Game/Components.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstring>

namespace Game::Systems
{
using Core::Utils::Logger;
using namespace Game::Components;

namespace
{
    struct VisionEmitterStamp
    {
        int TileX = 0;
        int TileY = 0;
        int SightRange = 1;
        int DetectorRange = 0;
        bool StampFog = true;
        bool StampDetector = false;
    };
}

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
    ++mRevision;
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
    const size_t SliceSize = (size_t)mMapWidth * (size_t)mMapHeight;
    if (SliceSize == 0)
        return;

    Core::Concurrency::JobSystem::Instance().ParallelFor(
        static_cast<uint32_t>(mMaxPlayers),
        1u,
        [&](uint32_t P)
        {
            const size_t Base = static_cast<size_t>(P) * SliceSize;
            if (!mDetectorGrid.empty())
                std::memset(mDetectorGrid.data() + Base, 0, SliceSize);
            uint8_t* FogSlice = mFogGrid.data() + Base;
            for (size_t I = 0; I < SliceSize; ++I)
            {
                if (FogSlice[I] == (uint8_t)FogState::Visible)
                    FogSlice[I] = (uint8_t)FogState::Explored;
            }
        });
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

    ++mRevision;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();

    // Partition vision emitter stamps per player so each player's fog + detector
    // grid slice can be decayed and rasterized in parallel with zero lock contention.
    std::vector<std::vector<VisionEmitterStamp>> EmittersPerPlayer((size_t)mMaxPlayers);

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

        const bool CanDetect = Unit->IsDetector && !IsBlind && BuildingCanDetect;
        const bool IsDefenseDetector = (Unit->ChkUnitID == 102 || Unit->ChkUnitID == 144 || Unit->ChkUnitID == 162);

        if (CanDetect && IsDefenseDetector)
        {
            EmittersPerPlayer[(size_t)PlayerID].push_back({
                TileX, TileY, EffectiveRange, 7, true, true
            });
        }
        else
        {
            EmittersPerPlayer[(size_t)PlayerID].push_back({
                TileX, TileY, EffectiveRange, EffectiveRange, true, CanDetect
            });
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

                EmittersPerPlayer[(size_t)ParasitePlayerID].push_back({
                    TileX, TileY, EffectiveRange, 0, true, false
                });
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
            EmittersPerPlayer[(size_t)PlayerID].push_back({
                TileX, TileY, Reveal->SightRange, Reveal->SightRange, true, Reveal->ProvidesDetection
            });
        }

        Reveal->DurationRemaining -= DeltaTime;
        if (Reveal->DurationRemaining <= 0.0f)
            EM.DestroyEntity(ID);
    }

    const size_t SliceSize = (size_t)mMapWidth * (size_t)mMapHeight;
    if (SliceSize > 0)
    {
        Core::Concurrency::JobSystem::Instance().ParallelFor(
            static_cast<uint32_t>(mMaxPlayers),
            1u,
            [&](uint32_t P)
            {
                const size_t SliceBase = static_cast<size_t>(P) * SliceSize;
                uint8_t* FogSlice = mFogGrid.data() + SliceBase;
                uint8_t* DetSlice = mDetectorGrid.empty() ? nullptr : (mDetectorGrid.data() + SliceBase);

                if (DetSlice)
                    std::memset(DetSlice, 0, SliceSize);
                for (size_t I = 0; I < SliceSize; ++I)
                {
                    if (FogSlice[I] == (uint8_t)FogState::Visible)
                        FogSlice[I] = (uint8_t)FogState::Explored;
                }

                for (const VisionEmitterStamp& Stamp : EmittersPerPlayer[P])
                {
                    if (Stamp.StampDetector && Stamp.DetectorRange != Stamp.SightRange)
                    {
                        RevealAround(Stamp.TileX, Stamp.TileY, Stamp.SightRange, FogSlice, nullptr);
                        RevealAround(Stamp.TileX, Stamp.TileY, Stamp.DetectorRange, nullptr, DetSlice);
                    }
                    else
                    {
                        RevealAround(
                            Stamp.TileX,
                            Stamp.TileY,
                            Stamp.SightRange,
                            Stamp.StampFog ? FogSlice : nullptr,
                            Stamp.StampDetector ? DetSlice : nullptr);
                    }
                }
            });
    }

    UpdateLastSeen(EM, Units);
}

} // namespace Game::Systems
