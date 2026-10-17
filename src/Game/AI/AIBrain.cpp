#include "Game/AI/AIBrain.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/GeyserState.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Map/Map.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    int ResolveUnitDefID(const UnitComponent& Unit)
    {
        // The in-engine siege-mode representation uses a distinct CHK id
        // while remaining the same logical production unit.
        if (Unit.ChkUnitID == 30)
            return Game::Races::GetUnitDefID("SiegeTank");
        return Game::Races::GetUnitDefIDByChkUnitID(Unit.ChkUnitID);
    }

    int NormalizeDefIDForRace(int DefID, Race RaceType)
    {
        if (DefID < 0)
            return DefID;

        const auto Candidate = Game::Races::GetUnitDefByID(DefID);
        if (Candidate.Name != "Unknown" && Candidate.RaceType == RaceType)
            return DefID;

        if (RaceType == Race::Zerg)
            return (int)Game::Races::GetTerranDefs().size() + DefID;
        if (RaceType == Race::Protoss)
            return (int)Game::Races::GetTerranDefs().size() +
                   (int)Game::Races::GetZergDefs().size() + DefID;
        return DefID;
    }

    int ResolveOwnedDefID(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
            Building && Building->BuildingDefID >= 0)
        {
            return Building->BuildingDefID;
        }

        if (auto* Unit = EM.GetComponent<UnitComponent>(EntityID))
            return ResolveUnitDefID(*Unit);
        return -1;
    }

    bool IsResourceDepotDefID(int DefID)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        return Def.Name == "CommandCenter" ||
               Def.Name == "Hatchery" ||
               Def.Name == "Lair" ||
               Def.Name == "Hive" ||
               Def.Name == "Nexus";
    }

    bool IsUnclaimedResourceEntity(Core::ECS::EntityManager& EM, int EntityID)
    {
        // Gas buildings claim the original geyser entity and intentionally
        // retain ResourceComponent. The shared predicate distinguishes those
        // attackable structures from neutral mineral/geyser nodes.
        return IsProtectedResourceNode(EM, EntityID);
    }

    float DistanceToNearestOwnedDepot(Core::ECS::EntityManager& EM,
                                      int PlayerID,
                                      const Core::Math::Vec2& Position)
    {
        float BestDistance = std::numeric_limits<float>::max();
        for (int EntityID : EM.GetEntitiesWith<BuildingComponent, PlayerComponent,
                                               TransformComponent>())
        {
            auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
            auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
            auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
            if (!Building || !Player || !Transform ||
                Player->PlayerID != PlayerID || Building->UnderConstruction ||
                !IsResourceDepotDefID(ResolveOwnedDefID(EM, EntityID)))
            {
                continue;
            }
            BestDistance = std::min(
                BestDistance, Transform->Position.DistanceTo(Position));
        }
        return BestDistance;
    }

    bool IsWorkerIdle(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Command = EM.GetComponent<CommandComponent>(EntityID))
        {
            const auto* Order = Command->GetCurrentOrder();
            if (Order && Order->Type == OrderType::Repair)
                return false;
        }
        if (auto* Harvest = EM.GetComponent<HarvestComponent>(EntityID))
            return Harvest->State == HarvestState::Idle;

        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        return !Command || Command->CurrentCommand == CommandType::Stop;
    }

    bool IsAssignedToConstruction(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (BuildingSystem::IsBuilderCommitted(EM, EntityID))
            return true;
        for (int BuildingID : EM.GetEntitiesWith<BuildingComponent>())
        {
            auto* Building = EM.GetComponent<BuildingComponent>(BuildingID);
            if (Building && Building->UnderConstruction &&
                Building->HasBuilderUnit(EntityID))
            {
                return true;
            }
        }
        return false;
    }

    bool IsWorkerRepairing(Core::ECS::EntityManager& EM, int EntityID)
    {
        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* Order = Command ? Command->GetCurrentOrder() : nullptr;
        return Order && Order->Type == OrderType::Repair;
    }

    bool IsCombatUnit(const UnitComponent& Unit, const CombatComponent& Combat)
    {
        return Unit.HitPoints > 0 && (Combat.GroundDamage > 0 || Combat.AirDamage > 0);
    }

    bool IsSupportUnit(const UnitComponent& Unit)
    {
        return Unit.HitPoints > 0 &&
               (Unit.IsDetector || Unit.MaxEnergyPoints > 0);
    }

    bool IsArmyUnit(const UnitComponent& Unit, const CombatComponent& Combat)
    {
        return IsCombatUnit(Unit, Combat) || IsSupportUnit(Unit);
    }

    bool IsArmyUnit(const UnitComponent& Unit, const CombatComponent* Combat)
    {
        return Combat ? IsArmyUnit(Unit, *Combat) : IsSupportUnit(Unit);
    }

    float EstimateCombatPower(const UnitComponent& Unit,
                              const CombatComponent& Combat)
    {
        const float Durability =
            (float)std::max(0, Unit.HitPoints + Unit.ShieldPoints) / 20.0f;
        const float Damage =
            (float)std::max(Combat.GroundDamage, Combat.AirDamage);
        return std::max(1.0f, Durability + Damage);
    }

    float EstimateCombatPower(const UnitComponent& Unit,
                              const CombatComponent* Combat)
    {
        if (Combat)
            return EstimateCombatPower(Unit, *Combat);
        return std::max(1.0f,
            (float)std::max(0, Unit.HitPoints + Unit.ShieldPoints) / 20.0f +
            (IsSupportUnit(Unit) ? 4.0f : 0.0f));
    }

    bool IsWorkerUnit(const UnitComponent& Unit)
    {
        return Unit.ChkUnitID == 0x07 || Unit.ChkUnitID == 0x29 ||
               Unit.ChkUnitID == 0x40;
    }

    bool IsTerranMechanicalUnit(const UnitComponent& Unit)
    {
        if (Unit.UnitRace != Race::Terran || IsWorkerUnit(Unit))
            return false;
        const int DefID = ResolveUnitDefID(Unit);
        if (DefID < 0)
            return false;
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        return !Def.IsBuilding && Def.Name != "Marine" &&
               Def.Name != "Firebat" && Def.Name != "Medic" &&
               Def.Name != "Ghost";
    }

    bool IsTerranProductionBuilding(const std::string& Name)
    {
        return Name == "Barracks" || Name == "Factory" ||
               Name == "Starport" || Name == "MachineShop" ||
               Name == "ControlTower" || Name == "Armory";
    }
}

AIBrain::AIBrain(Core::ECS::SystemManager& SysMgr, int AIPlayerID, int TargetPlayerID,
                  bool ShowcaseMode, const Game::Map::GameMap* Map,
                  AIDifficulty Difficulty, const FogOfWarSystem* Fog)
    : mSystemMgr(SysMgr)
    , mAIPlayerID(AIPlayerID)
    , mTargetPlayerID(TargetPlayerID)
    , mShowcaseMode(ShowcaseMode)
    , mDifficulty(Difficulty)
    , mMap(Map)
    , mFog(Fog)
{
    mDecisionInterval = GetAIDifficultyProfile(mDifficulty).DecisionIntervalSeconds;
    mDecisionTimer = (float)(mAIPlayerID % 8) * 0.23f;
}

Core::Math::Vec2 AIBrain::FindBaseLocation(Core::ECS::EntityManager& EM)
{
    auto Entities = EM.GetEntitiesWith<TransformComponent, PlayerComponent,
                                       BuildingComponent, UnitComponent>();
    Core::Math::Vec2 Fallback(1000.0f, 1000.0f);
    for (int Eid : Entities)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        auto* Building = EM.GetComponent<BuildingComponent>(Eid);
        if (!Player || Player->PlayerID != mAIPlayerID || !Trans || !Building ||
            Building->UnderConstruction || Building->IsAddon ||
            !IsMeleeStructureEntity(EM, Eid))
        {
            continue;
        }

        if (IsResourceDepotDefID(ResolveOwnedDefID(EM, Eid)))
            return Trans->Position;

        Fallback = Trans->Position;
    }
    return Fallback;
}

void AIBrain::BuildEnemySearchWaypoints(Core::ECS::EntityManager& EM)
{
    if (!mEnemySearchWaypoints.empty())
        return;

    const auto SnapToWalkable = [&](const Core::Math::Vec2& Position, bool& OutValid) -> Core::Math::Vec2
    {
        OutValid = false;
        if (!mMap)
            return Position;

        int TileX = 0;
        int TileY = 0;
        mMap->WorldToTile(Position, TileX, TileY);
        
        const auto IsOpenWalkable = [&](int X, int Y) -> bool
        {
            if (!mMap->IsInBounds(X, Y) || !mMap->IsWalkable(X, Y))
                return false;
            // Ensure at least two cardinal neighbors are also walkable so units don't get stuck in 1-tile dead-ends
            int OpenNeighbors = 0;
            if (mMap->IsInBounds(X + 1, Y) && mMap->IsWalkable(X + 1, Y)) ++OpenNeighbors;
            if (mMap->IsInBounds(X - 1, Y) && mMap->IsWalkable(X - 1, Y)) ++OpenNeighbors;
            if (mMap->IsInBounds(X, Y + 1) && mMap->IsWalkable(X, Y + 1)) ++OpenNeighbors;
            if (mMap->IsInBounds(X, Y - 1) && mMap->IsWalkable(X, Y - 1)) ++OpenNeighbors;
            return OpenNeighbors >= 2;
        };

        if (IsOpenWalkable(TileX, TileY))
        {
            OutValid = true;
            return mMap->TileToWorld(TileX, TileY);
        }

        // Only search nearby up to radius 2. If it's deep inside a giant platform or cliff, discard it!
        for (int Radius = 1; Radius <= 2; ++Radius)
        {
            for (int DY = -Radius; DY <= Radius; ++DY)
            {
                for (int DX = -Radius; DX <= Radius; ++DX)
                {
                    if (std::max(std::abs(DX), std::abs(DY)) != Radius)
                        continue;
                    const int X = TileX + DX;
                    const int Y = TileY + DY;
                    if (IsOpenWalkable(X, Y))
                    {
                        OutValid = true;
                        return mMap->TileToWorld(X, Y);
                    }
                }
            }
        }
        return Position;
    };

    const auto AppendUnique = [&](const Core::Math::Vec2& RawPosition)
    {
        bool Valid = false;
        const Core::Math::Vec2 Position = SnapToWalkable(RawPosition, Valid);
        if (!Valid)
            return;
        for (const auto& Existing : mEnemySearchWaypoints)
            if (Existing.DistanceTo(Position) <= 192.0f)
                return;
        mEnemySearchWaypoints.push_back(Position);
    };

    // Start locations are map knowledge, not evidence of a hidden occupant.
    if (mMap)
    {
        for (const auto& Start : mMap->GetStartLocations())
        {
            if (Start.PlayerIndex != mAIPlayerID)
                AppendUnique(mMap->TileToWorld(Start.TileX, Start.TileY));
        }
    }

    struct ResourceCluster
    {
        Core::Math::Vec2 Center;
        int Count = 0;
    };
    std::vector<ResourceCluster> ResourceClusters;
    for (int EntityID : EM.GetEntitiesWith<ResourceComponent,
                                             TransformComponent>())
    {
        const auto* Resource = EM.GetComponent<ResourceComponent>(EntityID);
        const auto* Position = EM.GetComponent<TransformComponent>(EntityID);
        if (!Resource || !Position || Resource->Type != ResourceType::Minerals)
            continue;

        ResourceCluster* Match = nullptr;
        for (auto& Cluster : ResourceClusters)
        {
            if (Cluster.Center.DistanceTo(Position->Position) <= 448.0f)
            {
                Match = &Cluster;
                break;
            }
        }
        if (!Match)
        {
            ResourceClusters.push_back({Position->Position, 1});
            continue;
        }
        Match->Center = (Match->Center * (float)Match->Count +
                         Position->Position) / (float)(Match->Count + 1);
        ++Match->Count;
    }
    for (const auto& Cluster : ResourceClusters)
        if (Cluster.Count >= 4)
            AppendUnique(Cluster.Center);

    // A bounded serpentine grid eventually reveals proxy structures away from
    // starts and mineral lines without querying hidden player-owned entities.
    if (mMap)
    {
        constexpr int GridStrideTiles = 12;
        int Row = 0;
        for (int Y = GridStrideTiles / 2; Y < mMap->GetHeight();
             Y += GridStrideTiles, ++Row)
        {
            if ((Row & 1) == 0)
            {
                for (int X = GridStrideTiles / 2; X < mMap->GetWidth();
                     X += GridStrideTiles)
                    AppendUnique(mMap->TileToWorld(X, Y));
            }
            else
            {
                int X = GridStrideTiles / 2;
                while (X + GridStrideTiles < mMap->GetWidth())
                    X += GridStrideTiles;
                for (; X >= GridStrideTiles / 2; X -= GridStrideTiles)
                    AppendUnique(mMap->TileToWorld(X, Y));
            }
        }
    }

    if (mEnemySearchWaypoints.empty())
        mEnemySearchWaypoints.push_back({2000.0f, 2000.0f});
}

Core::Math::Vec2 AIBrain::SelectEnemySearchWaypoint(
    Core::ECS::EntityManager& EM,
    bool AdvanceSearchWaypoint)
{
    BuildEnemySearchWaypoints(EM);

    // If current waypoint is already fully explored in visibility and has no enemy structures, advance to next
    if (!AdvanceSearchWaypoint && mHasLastEnemySearchWaypoint && mFog && mMap)
    {
        int TileX = 0;
        int TileY = 0;
        mMap->WorldToTile(mLastEnemySearchWaypoint, TileX, TileY);
        if (mFog->GetGameplayFog(TileX, TileY, mAIPlayerID) == FogState::Visible)
        {
            bool HasEnemyNear = false;
            for (int Eid : EM.GetEntitiesWith<BuildingComponent, PlayerComponent, TransformComponent>())
            {
                auto* P = EM.GetComponent<PlayerComponent>(Eid);
                auto* T = EM.GetComponent<TransformComponent>(Eid);
                if (P && P->PlayerID == mTargetPlayerID && T &&
                    T->Position.DistanceTo(mLastEnemySearchWaypoint) <= 384.0f)
                {
                    HasEnemyNear = true;
                    break;
                }
            }
            if (!HasEnemyNear)
                AdvanceSearchWaypoint = true;
        }
    }

    if (!AdvanceSearchWaypoint && mHasLastEnemySearchWaypoint)
        return mLastEnemySearchWaypoint;

    Core::Math::Vec2 Selected;
    size_t SelectedIndex = 0;
    const size_t Attempts = AdvanceSearchWaypoint
        ? mEnemySearchWaypoints.size() : 1u;
    for (size_t Attempt = 0; Attempt < Attempts; ++Attempt)
    {
        SelectedIndex = mEnemySearchWaypointIndex %
            mEnemySearchWaypoints.size();
        Selected = mEnemySearchWaypoints[SelectedIndex];
        if (AdvanceSearchWaypoint)
        {
            mEnemySearchWaypointIndex = AINextEnemySearchWaypointIndex(
                mEnemySearchWaypointIndex, mEnemySearchWaypoints.size());
        }

        bool CurrentlyVisible = false;
        if (mFog && mMap)
        {
            int TileX = 0;
            int TileY = 0;
            mMap->WorldToTile(Selected, TileX, TileY);
            CurrentlyVisible = mFog->GetGameplayFog(
                TileX, TileY, mAIPlayerID) == FogState::Visible;
        }
        if (!AdvanceSearchWaypoint || !CurrentlyVisible)
            break;
    }

    mLastEnemySearchWaypoint = Selected;
    mHasLastEnemySearchWaypoint = true;
    if (AdvanceSearchWaypoint)
    {
        Core::Utils::Logger::Info(
            "AIBrain", "Player " + std::to_string(mAIPlayerID) +
            " map_search=" + std::to_string(SelectedIndex + 1u) + "/" +
            std::to_string(mEnemySearchWaypoints.size()) + " target=" +
            std::to_string((int)Selected.X) + "," +
            std::to_string((int)Selected.Y));
    }
    return Selected;
}

Core::Math::Vec2 AIBrain::FindEnemyBaseLocation(Core::ECS::EntityManager& EM,
                                                 bool AdvanceSearchWaypoint)
{
    auto Entities = EM.GetEntitiesWith<TransformComponent, PlayerComponent, BuildingComponent>();
    for (int Eid : Entities)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        if (Player && Player->PlayerID == mTargetPlayerID && Trans &&
            IsMeleeStructureEntity(EM, Eid) &&
            IsVisibleToAI(Trans->Position, Eid, EM))
            return Trans->Position;
    }

    // Current vision is newer evidence than memory. Hidden survivors remain
    // inaccessible until an actual scout or attack group reveals them.
    for (int Eid : EM.GetEntitiesWith<TransformComponent, PlayerComponent,
                                      UnitComponent>())
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        if (Player && Player->PlayerID == mTargetPlayerID && Trans && Unit &&
            Unit->HitPoints > 0 && !IsUnclaimedResourceEntity(EM, Eid) &&
            IsVisibleToAI(Trans->Position, Eid, EM))
        {
            return Trans->Position;
        }
    }
    bool LastSeenPositionGameplayVisible = false;
    if (mEnemyIntel.LastSeenSeconds >= 0.0f)
    {
        if (!mFog || !mMap)
        {
            LastSeenPositionGameplayVisible = true;
        }
        else
        {
            int TileX = 0;
            int TileY = 0;
            mMap->WorldToTile(mEnemyIntel.LastSeenPosition, TileX, TileY);
            LastSeenPositionGameplayVisible = mFog->GetGameplayFog(
                TileX, TileY, mAIPlayerID) == FogState::Visible;
        }
    }
    if (AIShouldUseRememberedEnemyPosition(
            mEnemyIntel.LastSeenSeconds, mElapsedSeconds,
            LastSeenPositionGameplayVisible))
    {
        return mEnemyIntel.LastSeenPosition;
    }
    if (LastSeenPositionGameplayVisible)
        mEnemyIntel.LastSeenSeconds = -1.0f;

    return SelectEnemySearchWaypoint(EM, AdvanceSearchWaypoint);
}

bool AIBrain::HasLivingStructures(Core::ECS::EntityManager& EM, int PlayerID) const
{
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                      BuildingComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Building = EM.GetComponent<BuildingComponent>(Eid);
        if (Unit && Player && Player->PlayerID == PlayerID &&
            !Player->IsRescuePassive && Building &&
            IsMeleeStructureEntity(EM, Eid))
        {
            return true;
        }
    }
    return false;
}

void AIBrain::RetargetIfNeeded(Core::ECS::EntityManager& EM)
{
    if (HasLivingStructures(EM, mTargetPlayerID) &&
        (!mFog || !mFog->AreAllied(mAIPlayerID, mTargetPlayerID)))
        return;

    const int PreviousTarget = mTargetPlayerID;
    for (int PlayerID = 0; PlayerID < 8; ++PlayerID)
    {
        const bool Allied = mFog && mFog->AreAllied(mAIPlayerID, PlayerID);
        const bool HasStructures = HasLivingStructures(EM, PlayerID);
        if (!AIShouldConsiderRetargetPlayer(
                mAIPlayerID, PlayerID, Allied, false, HasStructures))
            continue;
        mTargetPlayerID = PlayerID;
        mEnemyIntel = EnemyIntel{};
        mScoutUnitID = -1;
        mEnemySearchWaypointIndex = 0;
        mHasLastEnemySearchWaypoint = false;
        mOpeningScoutSent = false;
        mLastScoutOrderSeconds = -10000.0f;
        mEnemyBasePos = FindEnemyBaseLocation(EM);
        mActiveAttackGroup.clear();
        mActiveAttackGroupAge = 0.0f;
        mActiveDefenseGroup.clear();
        mHasDefenseTarget = false;
        mAttackCooldownRemaining = 0.0f;
        Core::Utils::Logger::Info(
            "AIBrain", "Player " + std::to_string(mAIPlayerID) +
            " retargeted from player " + std::to_string(PreviousTarget) +
            " to player " + std::to_string(mTargetPlayerID));
        return;
    }
}

Core::Math::Vec2 AIBrain::FindExpansionLocation(Core::ECS::EntityManager& EM)
{
    std::vector<Core::Math::Vec2> OccupiedDepots;
    for (int EntityID : EM.GetEntitiesWith<BuildingComponent, PlayerComponent,
                                           TransformComponent>())
    {
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
        auto* Position = EM.GetComponent<TransformComponent>(EntityID);
        const bool PositionKnown = Player &&
            (Player->PlayerID == mAIPlayerID ||
             IsVisibleToAI(Position ? Position->Position : Core::Math::Vec2{},
                           EntityID, EM));
        if (Player && Building && Position && PositionKnown &&
            IsMeleeStructureEntity(EM, EntityID) &&
            IsResourceDepotDefID(ResolveOwnedDefID(EM, EntityID)))
        {
            OccupiedDepots.push_back(Position->Position);
        }
    }

    const auto IsFreeExpansion = [&](const Core::Math::Vec2& Position)
    {
        for (const auto& Depot : OccupiedDepots)
            if (Depot.DistanceTo(Position) < 360.0f)
                return false;
        return true;
    };

    Core::Math::Vec2 Best;
    float BestDistance = std::numeric_limits<float>::max();
    // Resource clusters are more useful than player start slots: on a melee
    // map the latter are usually occupied enemy mains, not natural expansions.
    for (int MineralID : EM.GetEntitiesWith<TransformComponent,
                                             ResourceComponent>())
    {
        auto* Resource = EM.GetComponent<ResourceComponent>(MineralID);
        auto* Position = EM.GetComponent<TransformComponent>(MineralID);
        if (!Resource || !Position || Resource->Type != ResourceType::Minerals ||
            Resource->IsDepleted || Resource->Amount <= 0 ||
            !IsFreeExpansion(Position->Position))
        {
            continue;
        }
        const float Distance = Position->Position.DistanceTo(mAiBasePos);
        if (Distance < BestDistance)
        {
            Best = Position->Position;
            BestDistance = Distance;
        }
    }
    if (BestDistance < std::numeric_limits<float>::max())
        return Best;

    // A mineral-depleted map can still reuse an unoccupied start location.
    if (mMap)
    {
        for (const auto& Start : mMap->GetStartLocations())
        {
            const Core::Math::Vec2 Candidate =
                mMap->TileToWorld(Start.TileX, Start.TileY);
            const float Distance = Candidate.DistanceTo(mAiBasePos);
            if (IsFreeExpansion(Candidate) && Distance < BestDistance)
            {
                Best = Candidate;
                BestDistance = Distance;
            }
        }
    }
    return BestDistance < std::numeric_limits<float>::max()
        ? Best : Core::Math::Vec2{mAiBasePos.X + 400.0f, mAiBasePos.Y};
}

int AIBrain::CountUnits(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID)
{
    int Count = 0;
    auto Entities = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (Unit && Player && Player->PlayerID == PlayerID &&
            (ChkUnitID < 0 || Unit->ChkUnitID == ChkUnitID))
            Count++;
    }
    return Count;
}

int AIBrain::CountBuildings(Core::ECS::EntityManager& EM, int PlayerID, bool ExcludeAddons)
{
    int Count = 0;
    auto Entities = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (B && P && P->PlayerID == PlayerID && B->IsBuilding && !B->UnderConstruction)
        {
            if (!ExcludeAddons || !B->IsAddon)
                Count++;
        }
    }
    return Count;
}

void AIBrain::BuildEntityCountCache(Core::ECS::EntityManager& EM)
{
    mCachedCompletedCount.clear();
    mCachedTotalCount.clear();
    auto Entities = EM.GetEntitiesWith<PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!P || P->PlayerID != mAIPlayerID) continue;

        auto* Bldg = EM.GetComponent<BuildingComponent>(Eid);
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);

        if (Bldg)
        {
            if (Unit && Unit->HitPoints > 0 && Bldg->BuildingDefID >= 0)
            {
                mCachedTotalCount[Bldg->BuildingDefID]++;
                if (!Bldg->UnderConstruction)
                    mCachedCompletedCount[Bldg->BuildingDefID]++;
            }
            continue;
        }

        if (Unit && Unit->HitPoints > 0)
        {
            const int DefID = ResolveUnitDefID(*Unit);
            if (DefID >= 0)
            {
                mCachedTotalCount[DefID]++;
                mCachedCompletedCount[DefID]++;
            }
        }
    }
    mEntityCountCacheValid = true;
}

int AIBrain::CountByDefID(Core::ECS::EntityManager& EM, int PlayerID, int DefID,
                          bool IncludeUnderConstruction)
{
    if (mEntityCountCacheValid && PlayerID == mAIPlayerID)
    {
        const int Pending = IncludeUnderConstruction
            ? BuildingSystem::CountPendingBuilds(PlayerID, DefID) : 0;
        const auto& Map = IncludeUnderConstruction ? mCachedTotalCount : mCachedCompletedCount;
        auto It = Map.find(DefID);
        return Pending + (It != Map.end() ? It->second : 0);
    }

    int Count = IncludeUnderConstruction
        ? BuildingSystem::CountPendingBuilds(PlayerID, DefID) : 0;
    auto Entities = EM.GetEntitiesWith<PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!P || P->PlayerID != PlayerID) continue;

        auto* Bldg = EM.GetComponent<BuildingComponent>(Eid);
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);

        if (Bldg)
        {
            if (Unit && Unit->HitPoints > 0 &&
                Bldg->BuildingDefID == DefID &&
                (IncludeUnderConstruction || !Bldg->UnderConstruction))
            {
                Count++;
            }
            continue;
        }

        if (Unit && Unit->HitPoints > 0 && ResolveUnitDefID(*Unit) == DefID)
        {
            Count++;
        }
    }
    return Count;
}

int AIBrain::CountQueuedByDefID(Core::ECS::EntityManager& EM,
                                 int PlayerID, int DefID) const
{
    int Count = 0;
    for (int Eid : EM.GetEntitiesWith<ProductionComponent, PlayerComponent>())
    {
        auto* Production = EM.GetComponent<ProductionComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Production || !Player || Player->PlayerID != PlayerID)
            continue;
        Count += (int)std::count(
            Production->Queue.begin(), Production->Queue.end(), DefID);
    }
    return Count;
}

int AIBrain::CountOwnedResourceDepots(Core::ECS::EntityManager& EM,
                                      bool IncludeUnderConstruction) const
{
    int Count = 0;
    if (IncludeUnderConstruction)
    {
        // Worker-travel construction does not create the depot entity until
        // arrival. Count those reservations so a long cross-map approach does
        // not let the expansion cooldown order and charge for another base.
        for (const char* DepotName : {"CommandCenter", "Hatchery", "Nexus"})
        {
            const int DepotDefID = Game::Races::GetUnitDefID(DepotName);
            if (DepotDefID >= 0)
            {
                Count += BuildingSystem::CountPendingBuilds(
                    mAIPlayerID, DepotDefID);
            }
        }
    }
    for (int EntityID : EM.GetEntitiesWith<BuildingComponent, PlayerComponent>())
    {
        auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        if (!Building || !Player || Player->PlayerID != mAIPlayerID ||
            (!IncludeUnderConstruction && Building->UnderConstruction))
        {
            continue;
        }
        if (IsMeleeStructureEntity(EM, EntityID) &&
            IsResourceDepotDefID(ResolveOwnedDefID(EM, EntityID)))
            ++Count;
    }
    return Count;
}

bool AIBrain::IsVisibleToAI(const Core::Math::Vec2& Position, int EntityID,
                            Core::ECS::EntityManager& EM) const
{
    if (!mFog)
        return true;

    int TileX = (int)std::floor(Position.X / 32.0f);
    int TileY = (int)std::floor(Position.Y / 32.0f);
    if (mMap)
        mMap->WorldToTile(Position, TileX, TileY);
    if (!mFog->IsGameplayVisible(TileX, TileY, mAIPlayerID))
        return false;

    const auto* Cloak = EM.GetComponent<CloakComponent>(EntityID);
    return !Cloak ||
           (!Cloak->IsCloaked && !Cloak->IsPassiveCloak &&
            !Cloak->IsBurrowed) ||
           mFog->IsGameplayDetected(TileX, TileY, mAIPlayerID);
}

bool AIBrain::TryStartResearch(Core::ECS::EntityManager& EM,
                               const std::vector<int>& ResearchIDs,
                               int MaxLevel)
{
    for (int RawID : ResearchIDs)
    {
        const auto Research = static_cast<ResearchId>(RawID);
        if (Research <= ResearchId::None || Research >= ResearchId::Count ||
            UpgradeSystem::GetResearchLevel(mAIPlayerID, Research) >= MaxLevel)
        {
            continue;
        }
        for (int BuildingID : EM.GetEntitiesWith<BuildingComponent,
                                                 PlayerComponent>())
        {
            auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
            if (Player && Player->PlayerID == mAIPlayerID &&
                UpgradeSystem::CanStartResearch(EM, BuildingID, Research) &&
                UpgradeSystem::StartResearch(EM, BuildingID, Research))
            {
                Core::Utils::Logger::Info(
                    "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                    " research=" +
                    std::string(UpgradeSystem::GetResearchDisplayName(Research)) +
                    " difficulty=" +
                    std::string(AIDifficultyName(mDifficulty)));
                return true;
            }
        }
    }
    return false;
}

int AIBrain::GetRace(Core::ECS::EntityManager& EM)
{
    auto Entities = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (U && P && P->PlayerID == mAIPlayerID && U->HitPoints > 0)
            return (int)U->UnitRace;
    }
    return 0; // Terran default
}

int AIBrain::FindProducerFor(Core::ECS::EntityManager& EM, int UnitDefID)
{
    const auto& Def = Game::Races::GetUnitDefByID(UnitDefID);
    if (Def.ProducedBy < 0) return -1;
    const int ProducerDefID = NormalizeDefIDForRace(Def.ProducedBy, Def.RaceType);

    int BestProducer = -1;
    size_t BestQueueSize = std::numeric_limits<size_t>::max();
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (int Bid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Bid);
        auto* P = EM.GetComponent<PlayerComponent>(Bid);
        if (B && P && P->PlayerID == mAIPlayerID &&
            B->BuildingDefID == ProducerDefID && !B->UnderConstruction &&
            ProductionSystem::CanTrain(EM, Bid, UnitDefID))
        {
            auto* Production = EM.GetComponent<ProductionComponent>(Bid);
            const size_t QueueSize = Production ? Production->Queue.size() : 0u;
            if (QueueSize < BestQueueSize)
            {
                BestProducer = Bid;
                BestQueueSize = QueueSize;
            }
        }
    }
    // Keep every production building active without reserving an excessive
    // amount of resources in one long queue.
    return BestQueueSize < 2u ? BestProducer : -1;
}

int AIBrain::FindIdleWorker(Core::ECS::EntityManager& EM)
{
    int RaceVal = GetRace(EM);
    int WorkerChkIDs[] = {0x07, 0x29, 0x40}; // SCV, Drone, Probe

    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!U || !P) continue;
        if (P->PlayerID != mAIPlayerID) continue;
        if (Eid == mScoutUnitID) continue;
        if (U->HitPoints <= 0) continue;
        if (U->ChkUnitID != WorkerChkIDs[RaceVal]) continue;
        if (auto* Morph = EM.GetComponent<MorphComponent>(Eid); Morph && Morph->IsMorphing)
            continue;
        if (IsAssignedToConstruction(EM, Eid))
            continue;
        if (IsWorkerIdle(EM, Eid))
            return Eid;
    }
    return -1;
}

bool AIBrain::FindBuildPositionNear(Core::ECS::EntityManager& EM, int BuildingDefID,
                                     const Core::Math::Vec2& NearPos, Core::Math::Vec2& OutPos)
{
    if (BuildingDefID < 0)
        return false;

    Core::Math::Vec2 SearchCenter = NearPos;
    const auto& Def = Game::Races::GetUnitDefByID(BuildingDefID);
    if (Def.RequiresPylon && !BuildingSystem::HasPylonPowerAtPosition(EM, NearPos, mAIPlayerID))
    {
        float BestDist = std::numeric_limits<float>::max();
        Core::Math::Vec2 BestPylonPos = NearPos;
        bool FoundPylon = false;
        const int PylonDefID = Game::Races::GetUnitDefID("Pylon");
        const int PylonChkID = PylonDefID >= 0 ? Game::Races::GetUnitDefByID(PylonDefID).ChkUnitID : 156;
        for (int EntityID : EM.GetEntitiesWith<TransformComponent, BuildingComponent, UnitComponent, PlayerComponent>())
        {
            auto* P = EM.GetComponent<PlayerComponent>(EntityID);
            auto* B = EM.GetComponent<BuildingComponent>(EntityID);
            auto* U = EM.GetComponent<UnitComponent>(EntityID);
            auto* T = EM.GetComponent<TransformComponent>(EntityID);
            if (P && B && U && T && P->PlayerID == mAIPlayerID && !B->UnderConstruction && U->HitPoints > 0)
            {
                if (U->ChkUnitID == PylonChkID)
                {
                    float Dist = T->Position.DistanceTo(NearPos);
                    if (Dist < BestDist)
                    {
                        BestDist = Dist;
                        BestPylonPos = T->Position;
                        FoundPylon = true;
                    }
                }
            }
        }
        if (FoundPylon)
            SearchCenter = BestPylonPos;
    }

    const int SearchSerial = mShowcaseMode ? 0 : mBuildPlacementSerial++;
    const int BaseHeight = mMap ? mMap->GetGroundHeightAtWorld(mAiBasePos) : -1;
    const auto IsOnBasePlateau = [&](const Core::Math::Vec2& Candidate)
    {
        if (!mShowcaseMode || !mMap)
            return true;
        // Keep the showcase compact and entirely on the mineral base's high
        // ground. Sampling around the center also rejects footprints that
        // straddle a cliff even when their center is still on the plateau.
        if (Candidate.DistanceTo(mAiBasePos) > 760.0f)
            return false;
        constexpr float HalfExtent = 64.0f;
        for (float Y : {-HalfExtent, 0.0f, HalfExtent})
        {
            for (float X : {-HalfExtent, 0.0f, HalfExtent})
            {
                const Core::Math::Vec2 Sample{Candidate.X + X, Candidate.Y + Y};
                int TileX = 0;
                int TileY = 0;
                mMap->WorldToTile(Sample, TileX, TileY);
                if (!mMap->IsInBounds(TileX, TileY) ||
                    mMap->GetGroundHeight(TileX, TileY) != BaseHeight)
                {
                    return false;
                }
            }
        }
        return true;
    };
    const auto IsAvailableBuildSite = [&](const Core::Math::Vec2& Candidate)
    {
        return IsOnBasePlateau(Candidate) &&
               !BuildingSystem::WasRecentlyUnreachable(
                   mAIPlayerID, BuildingDefID, Candidate) &&
               BuildingSystem::IsPlacementValid(
                   EM, BuildingDefID, Candidate, mAIPlayerID);
    };

    const float Step = 64.0f;
    if ((mShowcaseMode || SearchSerial == 0) && IsAvailableBuildSite(SearchCenter))
    {
        OutPos = SearchCenter;
        return true;
    }

    const int MaxSearchRadius = mShowcaseMode ? 5 : 10;
    for (int Radius = 1; Radius <= MaxSearchRadius; ++Radius)
    {
        std::vector<Core::Math::Vec2> Candidates;
        for (int Y = -Radius; Y <= Radius; ++Y)
        {
            for (int X = -Radius; X <= Radius; ++X)
            {
                if (std::max(std::abs(X), std::abs(Y)) != Radius)
                    continue;

                Candidates.emplace_back(
                    SearchCenter.X + (float)X * Step,
                    SearchCenter.Y + (float)Y * Step);
            }
        }
        const size_t CandidateStart = Candidates.empty()
            ? 0u : (size_t)(SearchSerial * 3 + Radius) % Candidates.size();
        for (size_t Offset = 0; Offset < Candidates.size(); ++Offset)
        {
            const auto& Candidate =
                Candidates[(CandidateStart + Offset) % Candidates.size()];
            if (IsAvailableBuildSite(Candidate))
            {
                OutPos = Candidate;
                return true;
            }
        }
    }

    // A later retry deliberately starts on a different ring edge.  Keep the
    // exact requested point as the final fallback so expansion/depot placement
    // still succeeds on maps with a single legal footprint.
    if (!mShowcaseMode && SearchSerial != 0 &&
        IsAvailableBuildSite(SearchCenter))
    {
        OutPos = SearchCenter;
        return true;
    }

    return false;
}

bool AIBrain::StartBuildingNear(Core::ECS::EntityManager& EM, int BuilderID,
                                int BuildingDefID, const Core::Math::Vec2& NearPos)
{
    if (!mShowcaseMode &&
        mElapsedSeconds - mLastConstructionOrderSeconds < 3.0f)
    {
        return false;
    }

    const auto& Def = Game::Races::GetUnitDefByID(BuildingDefID);
    bool Started = false;
    if (Def.IsAddon)
    {
        Started = BuildingSystem::StartBuild(
            EM, BuilderID, BuildingDefID, BuildTarget{});
    }
    else
    {
        Core::Math::Vec2 BuildPos;
        if (!FindBuildPositionNear(EM, BuildingDefID, NearPos, BuildPos))
            return false;
        Started = BuildingSystem::StartBuild(
            EM, BuilderID, BuildingDefID, BuildPos);
    }

    if (Started && !mShowcaseMode)
        mLastConstructionOrderSeconds = mElapsedSeconds;
    return Started;
}

bool AIBrain::StartZergBuildingNear(Core::ECS::EntityManager& EM, int DroneID,
                                    int BuildingDefID, const Core::Math::Vec2& NearPos)
{
    if (!mShowcaseMode &&
        mElapsedSeconds - mLastConstructionOrderSeconds < 3.0f)
    {
        return false;
    }

    Core::Math::Vec2 BuildPos;
    if (!FindBuildPositionNear(EM, BuildingDefID, NearPos, BuildPos))
        return false;
    if (!ZergSystem::CanMorphDroneIntoBuilding(EM, DroneID, BuildingDefID))
        return false;
    const bool Started = ZergSystem::StartMorphDroneIntoBuilding(
        EM, DroneID, BuildingDefID, BuildPos);
    if (Started && !mShowcaseMode)
        mLastConstructionOrderSeconds = mElapsedSeconds;
    return Started;
}

bool AIBrain::TrainUnitIfPossible(Core::ECS::EntityManager& EM, int UnitDefID)
{
    int Producer = FindProducerFor(EM, UnitDefID);
    return Producer >= 0 && ProductionSystem::StartTrain(EM, Producer, UnitDefID);
}

int AIBrain::FindAvailableAddonParent(Core::ECS::EntityManager& EM, int ParentDefID)
{
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (int Bid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Bid);
        auto* P = EM.GetComponent<PlayerComponent>(Bid);
        if (B && P && P->PlayerID == mAIPlayerID &&
            B->BuildingDefID == ParentDefID && !B->UnderConstruction &&
            (B->AddonID < 0 || !EM.IsAlive(B->AddonID)))
        {
            return Bid;
        }
    }
    return -1;
}

static bool HasConstructionWorkerReserve(Core::ECS::EntityManager& EM,
                                         int PlayerID,
                                         int WorkerDefID)
{
    int AvailableWorkers = 0;
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
    {
        const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        if (Unit && Player && Player->PlayerID == PlayerID &&
            Unit->HitPoints > 0 &&
            Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) ==
                WorkerDefID)
        {
            ++AvailableWorkers;
        }
    }
    // Never let failed construction retries steal the final mineral line.
    return AvailableWorkers > 2;
}
int AIBrain::FindTerranBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID)
{
    const int ScvDefID = Game::Races::GetUnitDefID("SCV");
    if (!HasConstructionWorkerReserve(EM, mAIPlayerID, ScvDefID))
        return -1;
    int MineralWorker = -1;
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Unit || !Player || Player->PlayerID != mAIPlayerID ||
            Unit->HitPoints <= 0 ||
            Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) != ScvDefID)
        {
            continue;
        }
        if (Eid == mScoutUnitID)
            continue;
        if (IsWorkerRepairing(EM, Eid))
            continue;
        if (const auto* Path = EM.GetComponent<PathfindingComponent>(Eid);
            Path && Path->HasRecentMoveFailure &&
            Path->RecentMoveFailureCooldown > 0.0f)
        {
            continue;
        }
        // Never pull one of the deliberately saturated gas workers for normal
        // construction; use an idle SCV first, then a mineral worker.
        if (const auto* Harvest = EM.GetComponent<HarvestComponent>(Eid))
        {
            const int ResourceID = Harvest->DesiredResourceID >= 0
                ? Harvest->DesiredResourceID
                : Harvest->TargetResourceID;
            const auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
            if (Resource && Resource->Type == ResourceType::VespeneGas)
                continue;
        }
        // Placement is resolved after selection.  This check also rejects an
        // SCV already assigned to an unfinished Terran structure while still
        // allowing the brain to pull a normal mineral harvester.
        if (BuildingSystem::CanBuild(
                EM, Eid, BuildingDefID, BuildTarget{}, nullptr, false))
        {
            if (IsWorkerIdle(EM, Eid))
                return Eid;
            if (MineralWorker < 0)
                MineralWorker = Eid;
        }
    }
    return MineralWorker;
}

int AIBrain::FindZergBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID)
{
    const int DroneDefID = Game::Races::GetUnitDefID("Drone");
    if (!HasConstructionWorkerReserve(EM, mAIPlayerID, DroneDefID))
        return -1;
    const int ExtractorDefID = Game::Races::GetUnitDefID("Extractor");
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Unit || !Player || Player->PlayerID != mAIPlayerID ||
            Unit->HitPoints <= 0 ||
            Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) != DroneDefID)
        {
            continue;
        }
        if (Eid == mScoutUnitID)
            continue;
        if (IsAssignedToConstruction(EM, Eid))
            continue;
        if (const auto* Path = EM.GetComponent<PathfindingComponent>(Eid);
            Path && Path->HasRecentMoveFailure &&
            Path->RecentMoveFailureCooldown > 0.0f)
        {
            continue;
        }
        if (const auto* Harvest = EM.GetComponent<HarvestComponent>(Eid))
        {
            const int ResourceID = Harvest->DesiredResourceID >= 0
                ? Harvest->DesiredResourceID
                : Harvest->TargetResourceID;
            const auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
            if (Resource && Resource->Type == ResourceType::VespeneGas)
                continue;
        }
        const bool CanBuild = BuildingDefID == ExtractorDefID
            ? BuildingSystem::CanBuild(
                EM, Eid, BuildingDefID, BuildTarget{}, nullptr, false)
            : ZergSystem::CanMorphDroneIntoBuilding(EM, Eid, BuildingDefID);
        if (CanBuild)
            return Eid;
    }
    return -1;
}

int AIBrain::FindProtossBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID)
{
    const int ProbeDefID = Game::Races::GetUnitDefID("Probe");
    if (!HasConstructionWorkerReserve(EM, mAIPlayerID, ProbeDefID))
        return -1;
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Unit || !Player || Player->PlayerID != mAIPlayerID ||
            Unit->HitPoints <= 0 ||
            Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) != ProbeDefID)
        {
            continue;
        }
        if (Eid == mScoutUnitID)
            continue;
        if (const auto* Path = EM.GetComponent<PathfindingComponent>(Eid);
            Path && Path->HasRecentMoveFailure &&
            Path->RecentMoveFailureCooldown > 0.0f)
        {
            continue;
        }
        if (const auto* Harvest = EM.GetComponent<HarvestComponent>(Eid))
        {
            const int ResourceID = Harvest->DesiredResourceID >= 0
                ? Harvest->DesiredResourceID
                : Harvest->TargetResourceID;
            const auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
            if (Resource && Resource->Type == ResourceType::VespeneGas)
                continue;
        }
        if (BuildingSystem::CanBuild(
                EM, Eid, BuildingDefID, BuildTarget{}, nullptr, false))
        {
            return Eid;
        }
    }
    return -1;
}

bool AIBrain::StartGasBuilding(Core::ECS::EntityManager& EM, int BuilderID,
                              int GasBuildingDefID)
{
    if (!mShowcaseMode &&
        mElapsedSeconds - mLastConstructionOrderSeconds < 3.0f)
    {
        return false;
    }

    int BestGeyser = -1;
    float BestDistance = 1e30f;
    for (int Eid : EM.GetEntitiesWith<TransformComponent, ResourceComponent>())
    {
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        auto* R = EM.GetComponent<ResourceComponent>(Eid);
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        if (!T || !R || R->Type != ResourceType::VespeneGas ||
            R->IsDepleted || R->Amount <= 0)
        {
            continue;
        }
        if (B && B->BuildingDefID >= 0)
            continue;
        const float Distance =
            DistanceToNearestOwnedDepot(EM, mAIPlayerID, T->Position);
        if (Distance <= 640.0f && Distance < BestDistance)
        {
            BestDistance = Distance;
            BestGeyser = Eid;
        }
    }
    if (BestGeyser < 0 ||
        !BuildingSystem::StartBuild(
            EM, BuilderID, GasBuildingDefID,
            BuildTarget::FromEntity(BestGeyser)))
    {
        return false;
    }
    if (!mShowcaseMode)
        mLastConstructionOrderSeconds = mElapsedSeconds;
    return true;
}

bool AIBrain::StartZergGasBuilding(Core::ECS::EntityManager& EM, int DroneID,
                                   int ExtractorDefID)
{
    if (!mShowcaseMode &&
        mElapsedSeconds - mLastConstructionOrderSeconds < 3.0f)
    {
        return false;
    }

    int BestGeyser = -1;
    float BestDistance = 1e30f;
    for (int Eid : EM.GetEntitiesWith<TransformComponent, ResourceComponent>())
    {
        auto* Transform = EM.GetComponent<TransformComponent>(Eid);
        auto* Resource = EM.GetComponent<ResourceComponent>(Eid);
        auto* Building = EM.GetComponent<BuildingComponent>(Eid);
        if (!Transform || !Resource || Resource->Type != ResourceType::VespeneGas ||
            Resource->IsDepleted || Resource->Amount <= 0)
        {
            continue;
        }
        if (Building && Building->BuildingDefID >= 0)
            continue;
        const float Distance = DistanceToNearestOwnedDepot(
            EM, mAIPlayerID, Transform->Position);
        if (Distance <= 640.0f && Distance < BestDistance)
        {
            BestDistance = Distance;
            BestGeyser = Eid;
        }
    }
    if (BestGeyser < 0)
        return false;

    if (!BuildingSystem::StartBuild(
            EM, DroneID, ExtractorDefID, BuildTarget::FromEntity(BestGeyser)))
    {
        return false;
    }
    if (!mShowcaseMode)
        mLastConstructionOrderSeconds = mElapsedSeconds;
    Core::Utils::Logger::Info(
        "AIBrain", "Ordered Extractor on geyser=" +
        std::to_string(BestGeyser) + "; drone " + std::to_string(DroneID) +
        " approaching morph site");
    return true;
}

void AIBrain::ManageGas(Core::ECS::EntityManager& EM, int RefineryDefID)
{
    std::vector<int> Refineries;
    for (int Eid : EM.GetEntitiesWith<BuildingComponent, PlayerComponent>())
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (B && P && P->PlayerID == mAIPlayerID &&
            B->BuildingDefID == RefineryDefID && !B->UnderConstruction &&
            IsMeleeStructureEntity(EM, Eid))
        {
            Refineries.push_back(Eid);
        }
    }
    if (Refineries.empty())
        return;

    const size_t ActiveRefineryCount = mDifficulty == AIDifficulty::Easy
        ? 1u
        : (mDifficulty == AIDifficulty::Medium
            ? std::min<size_t>(2u, Refineries.size()) : Refineries.size());
    for (size_t RefineryIndex = 0; RefineryIndex < ActiveRefineryCount;
         ++RefineryIndex)
    {
        const int RefineryID = Refineries[RefineryIndex];
        int Assigned = 0;
        for (int Eid : EM.GetEntitiesWith<HarvestComponent, PlayerComponent>())
        {
            auto* Harvest = EM.GetComponent<HarvestComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (Harvest && Player && Player->PlayerID == mAIPlayerID &&
                (Harvest->TargetResourceID == RefineryID ||
                 Harvest->DesiredResourceID == RefineryID))
            {
                ++Assigned;
            }
        }

        const int RaceVal = GetRace(EM);
        const int WorkerChkIDs[] = {0x07, 0x29, 0x40};
        int TotalWorkers = 0;
        int MineralWorkers = 0;
        for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<UnitComponent>(Eid);
            auto* Owner = EM.GetComponent<PlayerComponent>(Eid);
            if (Unit && Owner && Owner->PlayerID == mAIPlayerID &&
                RaceVal >= 0 && RaceVal <= 2 && Unit->ChkUnitID == WorkerChkIDs[RaceVal] &&
                Unit->HitPoints > 0)
            {
                ++TotalWorkers;
                auto* Harvest = EM.GetComponent<HarvestComponent>(Eid);
                const int ResID = Harvest
                    ? (Harvest->DesiredResourceID >= 0 ? Harvest->DesiredResourceID : Harvest->TargetResourceID)
                    : -1;
                const auto* Res = EM.GetComponent<ResourceComponent>(ResID);
                if (Res && Res->Type == ResourceType::Minerals)
                    ++MineralWorkers;
            }
        }

        while (Assigned < 3)
        {
            if (MineralWorkers <= 4 && TotalWorkers < 8)
                break;

            int Worker = -1;
            for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
            {
                auto* Unit = EM.GetComponent<UnitComponent>(Eid);
                auto* Owner = EM.GetComponent<PlayerComponent>(Eid);
                auto* Harvest = EM.GetComponent<HarvestComponent>(Eid);
                if (!Unit || !Owner || Owner->PlayerID != mAIPlayerID ||
                    RaceVal < 0 || RaceVal > 2 ||
                    Unit->ChkUnitID != WorkerChkIDs[RaceVal] ||
                    Unit->HitPoints <= 0 || Eid == mScoutUnitID ||
                    IsAssignedToConstruction(EM, Eid) ||
                    IsWorkerRepairing(EM, Eid))
                {
                    continue;
                }
                const int ResourceID = Harvest
                    ? (Harvest->DesiredResourceID >= 0
                        ? Harvest->DesiredResourceID : Harvest->TargetResourceID)
                    : -1;
                const auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
                if (Resource && Resource->Type == ResourceType::VespeneGas)
                    continue;
                Worker = Eid;
                break;
            }
            if (Worker < 0 ||
                !HarvestSystem::IssueHarvest(EM, {Worker}, RefineryID))
            {
                break;
            }
            ++Assigned;
            --MineralWorkers;
            Core::Utils::Logger::Info(
                "AIBrain", "Assigned worker " + std::to_string(Worker) +
                " to gas building " + std::to_string(RefineryID));
        }
    }
}

Core::Math::Vec2 AIBrain::FindShowcaseMapCenter(Core::ECS::EntityManager& EM) const
{
    Core::Math::Vec2 MinPos{1e30f, 1e30f};
    Core::Math::Vec2 MaxPos{-1e30f, -1e30f};
    bool Found = false;
    for (int Eid : EM.GetEntitiesWith<TransformComponent>())
    {
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        if (!T)
            continue;
        MinPos.X = std::min(MinPos.X, T->Position.X);
        MinPos.Y = std::min(MinPos.Y, T->Position.Y);
        MaxPos.X = std::max(MaxPos.X, T->Position.X);
        MaxPos.Y = std::max(MaxPos.Y, T->Position.Y);
        Found = true;
    }
    return Found ? Core::Math::Vec2{(MinPos.X + MaxPos.X) * 0.5f,
                                    (MinPos.Y + MaxPos.Y) * 0.5f}
                 : mAiBasePos + Core::Math::Vec2{1024.0f, 0.0f};
}

Core::Math::Vec2 AIBrain::ShowcaseBuildPosition(Core::ECS::EntityManager& EM,
                                                 size_t LayoutIndex) const
{
    Core::Math::Vec2 Forward = FindShowcaseMapCenter(EM) - mAiBasePos;
    if (Forward.LengthSq() < 1.0f)
        Forward = {1.0f, 0.0f};
    Forward = Forward.Normalized();
    const Core::Math::Vec2 Side{-Forward.Y, Forward.X};

    // Four staggered lanes preserve unit corridors without letting later tech
    // structures drift down the ramp or into a distant expansion. Final
    // placement additionally enforces the base plateau and a 760px radius.
    const size_t Row = LayoutIndex / 4;
    const int Lane = (int)(LayoutIndex % 4);
    const float ForwardDistance = 180.0f + (float)Row * 125.0f;
    const float SideDistance = ((float)Lane - 1.5f) * 150.0f +
                               ((Row & 1u) ? 50.0f : 0.0f);
    return mAiBasePos + Forward * ForwardDistance + Side * SideDistance;
}

void AIBrain::UpdateSafeTerranRallyPoints(Core::ECS::EntityManager& EM)
{
    if (!mMap)
        return;

    const Core::Math::Vec2 Directions[] = {
        {0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {-1.0f, 0.0f},
        {0.70710678f, 0.70710678f},
        {0.70710678f, -0.70710678f},
        {-0.70710678f, 0.70710678f},
        {-0.70710678f, -0.70710678f}
    };
    const float Rings[] = {96.0f, 128.0f, 160.0f, 192.0f};
    const Game::Pathfinding::NavigationFootprint RallyFootprint{
        16, 16, 16, 16};

    std::vector<Core::Math::Vec2> ActiveResourcePositions;
    for (int ResourceID :
         EM.GetEntitiesWith<TransformComponent, ResourceComponent>())
    {
        const auto* Resource =
            EM.GetComponent<ResourceComponent>(ResourceID);
        const auto* ResourcePosition =
            EM.GetComponent<TransformComponent>(ResourceID);
        if (Resource && ResourcePosition && Resource->Amount > 0)
        {
            ActiveResourcePositions.push_back(ResourcePosition->Position);
        }
    }

    const auto IsSafe = [&](const Core::Math::Vec2& Candidate,
                            int ProducerHeight)
    {
        if (mMap->GetGroundHeightAtWorld(Candidate) != ProducerHeight ||
            !mMap->GetNavigationGrid().CanOccupyWorld(
                Candidate, RallyFootprint))
        {
            return false;
        }

        // Reject a rally point before it reaches the visible cliff lip. The
        // navigation footprint handles the unit itself; this wider ring gives
        // a forming squad enough room to spread without hugging the edge.
        constexpr float TerrainClearance = 64.0f;
        const Core::Math::Vec2 Samples[] = {
            {Candidate.X - TerrainClearance, Candidate.Y},
            {Candidate.X + TerrainClearance, Candidate.Y},
            {Candidate.X, Candidate.Y - TerrainClearance},
            {Candidate.X, Candidate.Y + TerrainClearance},
            {Candidate.X - TerrainClearance,
             Candidate.Y - TerrainClearance},
            {Candidate.X + TerrainClearance,
             Candidate.Y - TerrainClearance},
            {Candidate.X - TerrainClearance,
             Candidate.Y + TerrainClearance},
            {Candidate.X + TerrainClearance,
             Candidate.Y + TerrainClearance}
        };
        for (const auto& Sample : Samples)
        {
            int X = 0;
            int Y = 0;
            mMap->WorldToTile(Sample, X, Y);
            if (!mMap->IsInBounds(X, Y) ||
                !mMap->IsWalkable(X, Y) ||
                mMap->GetGroundHeightAtWorld(Sample) != ProducerHeight)
            {
                return false;
            }
        }

        for (const auto& ResPos : ActiveResourcePositions)
        {
            if (Candidate.DistanceTo(ResPos) < 112.0f)
            {
                return false;
            }
        }
        return true;
    };

    for (int BuildingID :
         EM.GetEntitiesWith<BuildingComponent, PlayerComponent,
                            TransformComponent>())
    {
        const auto* Building =
            EM.GetComponent<BuildingComponent>(BuildingID);
        const auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
        const auto* Position =
            EM.GetComponent<TransformComponent>(BuildingID);
        if (!Building || !Player || !Position ||
            Player->PlayerID != mAIPlayerID ||
            Building->UnderConstruction ||
            std::find(mSafeRallyConfiguredBuildings.begin(),
                      mSafeRallyConfiguredBuildings.end(), BuildingID) !=
                mSafeRallyConfiguredBuildings.end())
        {
            continue;
        }

        const auto Def =
            Game::Races::GetUnitDefByID(Building->BuildingDefID);
        if (Def.Name != "Barracks" && Def.Name != "Factory" &&
            Def.Name != "Starport")
        {
            continue;
        }

        const int ProducerHeight =
            mMap->GetGroundHeightAtWorld(Position->Position);
        Core::Math::Vec2 Best;
        float BestScore = std::numeric_limits<float>::max();
        bool Found = false;
        for (float Ring : Rings)
        {
            for (const auto& Direction : Directions)
            {
                const Core::Math::Vec2 Candidate =
                    Position->Position + Direction * Ring;
                if (!IsSafe(Candidate, ProducerHeight))
                    continue;
                const float Score = Candidate.DistanceTo(mAiBasePos);
                if (!Found || Score < BestScore)
                {
                    Found = true;
                    Best = Candidate;
                    BestScore = Score;
                }
            }
        }
        if (!Found)
            continue;

        CommandSystem::SetRallyPoint(EM, BuildingID, Best);
        mSafeRallyConfiguredBuildings.push_back(BuildingID);
        Core::Utils::Logger::Info(
            "AIBrain", "Safe rally set building=" +
            std::to_string(BuildingID) + " name=" + Def.Name + " point=" +
            std::to_string((int)Best.X) + "," +
            std::to_string((int)Best.Y));
    }
}

void AIBrain::UpdateShowcaseRallyPoints(Core::ECS::EntityManager& EM)
{
    Core::Math::Vec2 TowardCenter = FindShowcaseMapCenter(EM) - mAiBasePos;
    const float CenterDistance = TowardCenter.Length();
    if (CenterDistance < 1.0f)
        TowardCenter = {1.0f, 0.0f};
    else
        TowardCenter = TowardCenter / CenterDistance;
    const float RallyDistance = std::max(
        900.0f, std::min(1800.0f, CenterDistance * 0.90f));
    const Core::Math::Vec2 RallyPoint = mAiBasePos + TowardCenter * RallyDistance;

    for (int Eid : EM.GetEntitiesWith<BuildingComponent, PlayerComponent>())
    {
        auto* Building = EM.GetComponent<BuildingComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Building || !Player || Player->PlayerID != mAIPlayerID ||
            Building->UnderConstruction)
        {
            continue;
        }
        const auto Def = Game::Races::GetUnitDefByID(Building->BuildingDefID);
        if (Def.Name != "Barracks" && Def.Name != "Factory" &&
            Def.Name != "Starport" && Def.Name != "Gateway" &&
            Def.Name != "Robotics Facility" && Def.Name != "Stargate")
        {
            continue;
        }
        auto* Command = EM.GetComponent<CommandComponent>(Eid);
        if (!Command)
            Command = &EM.AddComponent<CommandComponent>(Eid);
        Command->RallyPoint = RallyPoint;
        Command->HasRallyPoint = true;
        if (std::find(mShowcaseRallyConfiguredBuildings.begin(),
                      mShowcaseRallyConfiguredBuildings.end(), Eid) ==
            mShowcaseRallyConfiguredBuildings.end())
        {
            mShowcaseRallyConfiguredBuildings.push_back(Eid);
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase rally set building=" +
                std::to_string(Eid) + " name=" + Def.Name + " point=" +
                std::to_string((int)RallyPoint.X) + "," +
                std::to_string((int)RallyPoint.Y));
        }
    }
}

void AIBrain::RunTerranShowcase(Core::ECS::EntityManager& EM)
{
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    const int ScvDefID = Game::Races::GetUnitDefID("SCV");
    const int SupplyDepotDefID = Game::Races::GetUnitDefID("SupplyDepot");
    const int RefineryDefID = Game::Races::GetUnitDefID("Refinery");

    ManageGas(EM, RefineryDefID);
    ManageEconomy(EM);
    UpdateShowcaseRallyPoints(EM);
    int QueuedScvs = 0;
    for (int Eid : EM.GetEntitiesWith<ProductionComponent, PlayerComponent>())
    {
        auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Prod || !Player || Player->PlayerID != mAIPlayerID)
            continue;
        QueuedScvs += (int)std::count(Prod->Queue.begin(), Prod->Queue.end(), ScvDefID);
    }
    if (CountByDefID(EM, mAIPlayerID, ScvDefID) + QueuedScvs < 12)
        TrainUnitIfPossible(EM, ScvDefID);

    // Maintain real supply headroom throughout the production phase.
    if (Res.SupplyUsed + 8 > Res.SupplyMax &&
        CountByDefID(EM, mAIPlayerID, SupplyDepotDefID, true) ==
            CountByDefID(EM, mAIPlayerID, SupplyDepotDefID))
    {
        const int Worker = FindTerranBuilderFor(EM, SupplyDepotDefID);
        const size_t DepotLayoutIndex = 18u + (size_t)CountByDefID(
            EM, mAIPlayerID, SupplyDepotDefID, true);
        if (Worker >= 0 && StartBuildingNear(
                EM, Worker, SupplyDepotDefID,
                ShowcaseBuildPosition(EM, DepotLayoutIndex)))
        {
            Core::Utils::Logger::Info("AIBrain", "Showcase ordered SupplyDepot");
            return;
        }
    }

    struct BuildingGoal { const char* Name; int TargetCount; };
    static const BuildingGoal Goals[] = {
        {"SupplyDepot", 1}, {"Refinery", 1}, {"Barracks", 1},
        {"Academy", 1}, {"EngineeringBay", 1}, {"Factory", 1},
        {"MachineShop", 1}, {"Armory", 1}, {"Starport", 1},
        {"ControlTower", 1}, {"ScienceFacility", 2}, {"CovertOps", 1},
        {"PhysicsLab", 1}, {"CommandCenter", 2}, {"ComsatStation", 1},
        {"NuclearSilo", 1}, {"Bunker", 1}, {"MissileTurret", 1}
    };

    for (size_t GoalIndex = 0; GoalIndex < std::size(Goals); ++GoalIndex)
    {
        const int DefID = Game::Races::GetUnitDefID(Goals[GoalIndex].Name);
        if (DefID < 0 || CountByDefID(EM, mAIPlayerID, DefID, true) >= Goals[GoalIndex].TargetCount)
            continue;

        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int Builder = -1;
        bool Started = false;
        if (Def.IsAddon)
        {
            Builder = FindAvailableAddonParent(EM, Def.AddonOnBuildingDefID);
            Started = Builder >= 0 && StartBuildingNear(EM, Builder, DefID, mAiBasePos);
        }
        else
        {
            Builder = FindTerranBuilderFor(EM, DefID);
            if (Builder >= 0 && DefID == RefineryDefID)
                Started = StartGasBuilding(EM, Builder, DefID);
            else if (Builder >= 0)
            {
                Started = StartBuildingNear(
                    EM, Builder, DefID,
                    ShowcaseBuildPosition(EM, GoalIndex));
            }
        }
        if (Started)
        {
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase ordered building " + Def.Name +
                " target=" + std::to_string(Goals[GoalIndex].TargetCount));
        }
        return; // Respect tech order and wait for prerequisites/resources/builders.
    }

    bool AllUnitsComplete = true;
    const auto& TerranDefs = Game::Races::GetTerranDefs();
    for (size_t I = 0; I < TerranDefs.size(); ++I)
    {
        const int DefID = (int)I;
        const auto& Def = TerranDefs[I];
        if (Def.IsBuilding || Def.ProducedBy < 0)
            continue;
        if (CountByDefID(EM, mAIPlayerID, DefID) > 0)
            continue;
        AllUnitsComplete = false;
        if (std::find(mShowcaseQueuedUnits.begin(), mShowcaseQueuedUnits.end(), DefID) !=
            mShowcaseQueuedUnits.end())
        {
            continue;
        }
        if (TrainUnitIfPossible(EM, DefID))
        {
            mShowcaseQueuedUnits.push_back(DefID);
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase queued real production " + Def.Name);
        }
        return;
    }

    if (AllUnitsComplete && !mShowcaseCompleteLogged)
    {
        mShowcaseCompleteLogged = true;
        Core::Utils::Logger::Info(
            "AIBrain", "Showcase complete: all Terran buildings and trainable units ready");
    }
    ManageTerranMedics(EM);
}

void AIBrain::RunProtossShowcase(Core::ECS::EntityManager& EM)
{
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    const int ProbeDefID = Game::Races::GetUnitDefID("Probe");
    const int PylonDefID = Game::Races::GetUnitDefID("Pylon");
    const int AssimilatorDefID = Game::Races::GetUnitDefID("Assimilator");

    ManageEconomy(EM);
    UpdateShowcaseRallyPoints(EM);

    Core::Math::Vec2 Forward = FindShowcaseMapCenter(EM) - mAiBasePos;
    if (Forward.LengthSq() < 1.0f)
        Forward = {1.0f, 0.0f};
    Forward = Forward.Normalized();
    const Core::Math::Vec2 Side{-Forward.Y, Forward.X};
    const auto PylonAnchor = [&](int AnchorIndex)
    {
        static const float AnchorOffsets[5][2] = {
            {180.0f, -260.0f}, {180.0f, 260.0f},
            {430.0f, -260.0f}, {430.0f, 260.0f},
            {650.0f, 0.0f}
        };
        AnchorIndex = std::clamp(AnchorIndex, 0, 4);
        return mAiBasePos + Forward * AnchorOffsets[AnchorIndex][0] +
               Side * AnchorOffsets[AnchorIndex][1];
    };
    const auto PoweredBuildingPosition = [&](int AnchorIndex, int SlotIndex)
    {
        static const float SlotOffsets[3][2] = {
            {0.0f, -112.0f}, {0.0f, 112.0f}, {96.0f, 0.0f}
        };
        SlotIndex = std::clamp(SlotIndex, 0, 2);
        return PylonAnchor(AnchorIndex) + Forward * SlotOffsets[SlotIndex][0] +
               Side * SlotOffsets[SlotIndex][1];
    };

    // Protoss progression begins with a completed Pylon. Do not let the gas
    // manager or the general building-goal loop place anything in parallel
    // with the first power source: the original build order opens the warp
    // anchor, waits for the Pylon to finish, then starts powered structures.
    const int CompletedPylons =
        CountByDefID(EM, mAIPlayerID, PylonDefID, false);
    const int ExistingPylons =
        CountByDefID(EM, mAIPlayerID, PylonDefID, true);
    if (CompletedPylons < 1)
    {
        if (ExistingPylons < 1)
        {
            const int Probe = FindProtossBuilderFor(EM, PylonDefID);
            if (Probe >= 0 && StartBuildingNear(
                    EM, Probe, PylonDefID, PylonAnchor(0)))
            {
                Core::Utils::Logger::Info(
                    "AIBrain", "Showcase ordered first Pylon; waiting for completion");
            }
        }
        return;
    }

    ManageGas(EM, AssimilatorDefID);

    int QueuedProbes = 0;
    for (int Eid : EM.GetEntitiesWith<ProductionComponent, PlayerComponent>())
    {
        auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (!Prod || !Player || Player->PlayerID != mAIPlayerID)
            continue;
        QueuedProbes += (int)std::count(
            Prod->Queue.begin(), Prod->Queue.end(), ProbeDefID);
    }
    if (CountByDefID(EM, mAIPlayerID, ProbeDefID) + QueuedProbes < 12)
        TrainUnitIfPossible(EM, ProbeDefID);

    // Keep enough psi supply for one of every normal Protoss unit plus the
    // additional templars used by the two real Archon merges.
    if (Res.SupplyUsed + 8 > Res.SupplyMax &&
        CountByDefID(EM, mAIPlayerID, PylonDefID, true) ==
            CountByDefID(EM, mAIPlayerID, PylonDefID))
    {
        const int Probe = FindProtossBuilderFor(EM, PylonDefID);
        const int AnchorIndex = std::min(
            4, CountByDefID(EM, mAIPlayerID, PylonDefID, true));
        if (Probe >= 0 && StartBuildingNear(
                EM, Probe, PylonDefID,
                PylonAnchor(AnchorIndex)))
        {
            Core::Utils::Logger::Info("AIBrain", "Showcase ordered Pylon for supply");
            return;
        }
    }

    struct BuildingGoal
    {
        const char* Name;
        int TargetCount;
        int PylonAnchorIndex;
        int SlotIndex;
    };
    // Five separated Pylons form visible powered neighborhoods. Every powered
    // structure is placed around its neighborhood's Pylon rather than forming
    // one dense lane or drifting away from the starting plateau.
    static const BuildingGoal Goals[] = {
        {"Pylon", 1, 0, -1}, {"Assimilator", 1, 0, -1},
        {"Gateway", 1, 0, 0}, {"Forge", 1, 0, 1},
        {"Photon Cannon", 1, 0, 2}, {"Pylon", 2, 1, -1},
        {"Cybernetics Core", 1, 1, 0}, {"Shield Battery", 1, 1, 1},
        {"Robotics Facility", 1, 1, 2}, {"Pylon", 3, 2, -1},
        {"Stargate", 1, 2, 0}, {"Citadel of Adun", 1, 2, 1},
        {"Templar Archives", 1, 2, 2}, {"Pylon", 4, 3, -1},
        {"Robotics Support Bay", 1, 3, 0}, {"Fleet Beacon", 1, 3, 1},
        {"Arbiter Tribunal", 1, 3, 2}, {"Pylon", 5, 4, -1},
        {"Observatory", 1, 4, 0}, {"Nexus", 2, 4, 1}
    };

    for (const auto& Goal : Goals)
    {
        const int DefID = Game::Races::GetUnitDefID(Goal.Name);
        if (DefID < 0 ||
            CountByDefID(EM, mAIPlayerID, DefID, true) >= Goal.TargetCount)
        {
            continue;
        }

        const int Probe = FindProtossBuilderFor(EM, DefID);
        bool Started = false;
        if (Probe >= 0 && DefID == AssimilatorDefID)
            Started = StartGasBuilding(EM, Probe, DefID);
        else if (Probe >= 0)
        {
            const Core::Math::Vec2 Position = DefID == PylonDefID
                ? PylonAnchor(Goal.PylonAnchorIndex)
                : PoweredBuildingPosition(Goal.PylonAnchorIndex, Goal.SlotIndex);
            Started = StartBuildingNear(
                EM, Probe, DefID, Position);
        }
        if (Started)
        {
            const auto Def = Game::Races::GetUnitDefByID(DefID);
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase ordered Protoss building " + Def.Name +
                " target=" + std::to_string(Goal.TargetCount));
        }
        return;
    }

    bool AllTrainableUnitsComplete = true;
    const auto& ProtossDefs = Game::Races::GetProtossDefs();
    const int ProtossOffset = (int)Game::Races::GetTerranDefs().size() +
                              (int)Game::Races::GetZergDefs().size();
    for (size_t I = 0; I < ProtossDefs.size(); ++I)
    {
        const int DefID = ProtossOffset + (int)I;
        const auto& Def = ProtossDefs[I];
        if (Def.IsBuilding || Def.ProducedBy < 0)
            continue;
        if (CountByDefID(EM, mAIPlayerID, DefID) > 0)
            continue;
        AllTrainableUnitsComplete = false;
        if (std::find(mShowcaseQueuedUnits.begin(), mShowcaseQueuedUnits.end(), DefID) !=
            mShowcaseQueuedUnits.end())
        {
            return;
        }
        if (TrainUnitIfPossible(EM, DefID))
        {
            mShowcaseQueuedUnits.push_back(DefID);
            Core::Utils::Logger::Info(
                "AIBrain", "Showcase queued real Protoss production " + Def.Name);
        }
        return;
    }
    if (!AllTrainableUnitsComplete)
        return;

    auto CountQueued = [&](int DefID)
    {
        int Count = 0;
        for (int Eid : EM.GetEntitiesWith<ProductionComponent, PlayerComponent>())
        {
            auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (Prod && Player && Player->PlayerID == mAIPlayerID)
                Count += (int)std::count(Prod->Queue.begin(), Prod->Queue.end(), DefID);
        }
        return Count;
    };
    auto FindOwnedUnits = [&](int DefID)
    {
        std::vector<int> Result;
        for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<UnitComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (Unit && Player && Player->PlayerID == mAIPlayerID &&
                Unit->HitPoints > 0 &&
                Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) == DefID)
            {
                Result.push_back(Eid);
            }
        }
        return Result;
    };

    const int HighTemplarDefID = Game::Races::GetUnitDefID("High Templar");
    const int ArchonDefID = Game::Races::GetUnitDefID("Archon");
    if (CountByDefID(EM, mAIPlayerID, ArchonDefID) == 0)
    {
        const int Total = CountByDefID(EM, mAIPlayerID, HighTemplarDefID) +
                          CountQueued(HighTemplarDefID);
        if (Total < 3)
        {
            TrainUnitIfPossible(EM, HighTemplarDefID);
            return;
        }
        auto Templars = FindOwnedUnits(HighTemplarDefID);
        if (Templars.size() < 3)
            return;
        ProtossSystem::MorphArchon(EM, Templars[0], Templars[1], false);
        return;
    }

    const int DarkTemplarDefID = Game::Races::GetUnitDefID("Dark Templar");
    const int DarkArchonDefID = Game::Races::GetUnitDefID("Dark Archon");
    if (CountByDefID(EM, mAIPlayerID, DarkArchonDefID) == 0)
    {
        const int Total = CountByDefID(EM, mAIPlayerID, DarkTemplarDefID) +
                          CountQueued(DarkTemplarDefID);
        if (Total < 3)
        {
            TrainUnitIfPossible(EM, DarkTemplarDefID);
            return;
        }
        auto Templars = FindOwnedUnits(DarkTemplarDefID);
        if (Templars.size() < 3)
            return;
        ProtossSystem::MorphArchon(EM, Templars[0], Templars[1], true);
        return;
    }

    if (!mShowcaseCompleteLogged)
    {
        mShowcaseCompleteLogged = true;
        Core::Utils::Logger::Info(
            "AIBrain", "Showcase complete: all Protoss buildings and normal units ready");
    }
}

int AIBrain::FindNearestFreeBuilding(Core::ECS::EntityManager& EM, int BuildingDefID)
{
    int BestID = -1;
    float BestDist = 1e9f;
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent, TransformComponent>();
    for (int Bid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Bid);
        auto* P = EM.GetComponent<PlayerComponent>(Bid);
        auto* T = EM.GetComponent<TransformComponent>(Bid);
        if (!B || !P || !T || P->PlayerID != mAIPlayerID) continue;
        if (B->BuildingDefID == BuildingDefID && !B->UnderConstruction)
        {
            int QSize = 0;
            auto* Prod = EM.GetComponent<ProductionComponent>(Bid);
            if (Prod) QSize = (int)Prod->Queue.size();
            if (QSize < 3)
            {
                float D = T->Position.DistanceTo(mAiBasePos);
                if (D < BestDist) { BestDist = D; BestID = Bid; }
            }
        }
    }
    return BestID;
}

void AIBrain::TrainWorkerIfNeeded(Core::ECS::EntityManager& EM,
                                   int ProducerDefID, int WorkerDefID, int MaxWorkers)
{
    (void)ProducerDefID;
    int WorkerCount = CountByDefID(EM, mAIPlayerID, WorkerDefID);
    const int QueuedWorkers = CountQueuedByDefID(EM, mAIPlayerID, WorkerDefID);
    if (WorkerCount + QueuedWorkers < MaxWorkers)
        TrainUnitIfPossible(EM, WorkerDefID);
}

void AIBrain::RunTerranAI(Core::ECS::EntityManager& EM)
{
    int SvcDefID = Game::Races::GetUnitDefID("SCV");
    int MarineDefID = Game::Races::GetUnitDefID("Marine");
    int FirebatDefID = Game::Races::GetUnitDefID("Firebat");
    int MedicDefID = Game::Races::GetUnitDefID("Medic");
    int VultureDefID = Game::Races::GetUnitDefID("Vulture");
    int TankDefID = Game::Races::GetUnitDefID("SiegeTank");
    int GoliathDefID = Game::Races::GetUnitDefID("Goliath");
    int WraithDefID = Game::Races::GetUnitDefID("Wraith");
    int ScienceVesselDefID = Game::Races::GetUnitDefID("ScienceVessel");
    int ValkyrieDefID = Game::Races::GetUnitDefID("Valkyrie");
    int BattlecruiserDefID = Game::Races::GetUnitDefID("Battlecruiser");
    int SupplyDepotDefID = Game::Races::GetUnitDefID("SupplyDepot");
    int BarracksDefID = Game::Races::GetUnitDefID("Barracks");
    int FactoryDefID = Game::Races::GetUnitDefID("Factory");
    int StarportDefID = Game::Races::GetUnitDefID("Starport");
    int AcademyDefID = Game::Races::GetUnitDefID("Academy");
    int MachineShopDefID = Game::Races::GetUnitDefID("MachineShop");
    int ControlTowerDefID = Game::Races::GetUnitDefID("ControlTower");
    int ScienceFacilityDefID = Game::Races::GetUnitDefID("ScienceFacility");
    int PhysicsLabDefID = Game::Races::GetUnitDefID("PhysicsLab");
    int EngineeringBayDefID = Game::Races::GetUnitDefID("EngineeringBay");
    int ArmoryDefID = Game::Races::GetUnitDefID("Armory");
    int BunkerDefID = Game::Races::GetUnitDefID("Bunker");
    int MissileTurretDefID = Game::Races::GetUnitDefID("MissileTurret");
    int CommandCenterDefID = Game::Races::GetUnitDefID("CommandCenter");
    int RefineryDefID = Game::Races::GetUnitDefID("Refinery");

    UpdateSafeTerranRallyPoints(EM);

    const auto& Profile = GetAIDifficultyProfile(mDifficulty);
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    const int completedBases = std::max(1, CountOwnedResourceDepots(EM, false));
    const int startedBases = CountOwnedResourceDepots(EM, true);
    const int workerTarget = CalculateAIWorkerTarget(Profile, completedBases);
    int scvCount = CountByDefID(EM, mAIPlayerID, SvcDefID);
    int supplyDepotCount = CountByDefID(EM, mAIPlayerID, SupplyDepotDefID);
    int supplyDepotStartedCount =
        CountByDefID(EM, mAIPlayerID, SupplyDepotDefID, true);
    int barracksCount = CountByDefID(EM, mAIPlayerID, BarracksDefID);
    int barracksStartedCount = CountByDefID(EM, mAIPlayerID, BarracksDefID, true);
    int factoryCount = CountByDefID(EM, mAIPlayerID, FactoryDefID);
    int factoryStartedCount = CountByDefID(EM, mAIPlayerID, FactoryDefID, true);
    int starportCount = CountByDefID(EM, mAIPlayerID, StarportDefID);
    int starportStartedCount = CountByDefID(EM, mAIPlayerID, StarportDefID, true);
    int academyStartedCount = CountByDefID(EM, mAIPlayerID, AcademyDefID, true);
    int refineryStartedCount = CountByDefID(EM, mAIPlayerID, RefineryDefID, true);

    int marineCount = CountByDefID(EM, mAIPlayerID, MarineDefID);
    int firebatCount = CountByDefID(EM, mAIPlayerID, FirebatDefID);
    int medicCount = CountByDefID(EM, mAIPlayerID, MedicDefID);
    int vultureCount = CountByDefID(EM, mAIPlayerID, VultureDefID);
    int tankCount = CountByDefID(EM, mAIPlayerID, TankDefID);
    int goliathCount = CountByDefID(EM, mAIPlayerID, GoliathDefID);
    int wraithCount = CountByDefID(EM, mAIPlayerID, WraithDefID);
    int vesselCount = CountByDefID(EM, mAIPlayerID, ScienceVesselDefID);
    int valkyrieCount = CountByDefID(EM, mAIPlayerID, ValkyrieDefID);
    int battlecruiserCount = CountByDefID(EM, mAIPlayerID, BattlecruiserDefID);

    // Stage progression
    if (mStage < 1 && scvCount >= 12 && barracksCount >= 1 && marineCount >= 6)
        mStage = 1;
    if (Profile.MaxTechStage >= 2 && mStage < 2 &&
        factoryCount >= 1 && tankCount >= 3)
        mStage = 2;
    if (Profile.MaxTechStage >= 3 && mStage < 3 &&
        starportCount >= 1 && wraithCount >= 2)
        mStage = 3;
    mStage = std::min(mStage, Profile.MaxTechStage);

    // Always: workers + supply
    TrainWorkerIfNeeded(EM, CommandCenterDefID, SvcDefID, workerTarget);
    const int maxConcurrentDepots = Res.SupplyUsed >= 70 ? 3 : (Res.SupplyUsed >= 40 ? 2 : 1);
    if (scvCount >= 8 &&
        Res.SupplyMax < 200 &&
        Res.SupplyUsed + Profile.SupplyBuffer >= Res.SupplyMax &&
        supplyDepotStartedCount < supplyDepotCount + maxConcurrentDepots &&
        Res.Minerals >= 100)
    {
        int idleSCV = FindTerranBuilderFor(EM, SupplyDepotDefID);
        if (idleSCV >= 0)
            StartBuildingNear(EM, idleSCV, SupplyDepotDefID,
                Core::Math::Vec2(mAiBasePos.X + 60.0f * (float)(supplyDepotCount + 1),
                                 mAiBasePos.Y));
    }

    const int desiredRefineries = std::min(
        completedBases,
        mDifficulty == AIDifficulty::Easy ? 1 :
            (mDifficulty == AIDifficulty::Medium ? 2 : 3));
    const int gasWorkerThreshold = mDifficulty == AIDifficulty::Easy ? 10 :
        (mDifficulty == AIDifficulty::Medium ? 9 : 8);
    if (scvCount >= gasWorkerThreshold &&
        refineryStartedCount < desiredRefineries && Res.Minerals >= 100)
    {
        const int Builder = FindTerranBuilderFor(EM, RefineryDefID);
        if (Builder >= 0)
            StartGasBuilding(EM, Builder, RefineryDefID);
    }

    // Barracks and bio production remain active through every stage.
    const int desiredBarracks = mDifficulty == AIDifficulty::Easy ? 2 :
        (mDifficulty == AIDifficulty::Medium ? 3 : std::min(5, 2 + completedBases));
    if (barracksStartedCount < desiredBarracks && Res.Minerals >= 150)
    {
        int idleSCV = FindTerranBuilderFor(EM, BarracksDefID);
        if (idleSCV >= 0)
            StartBuildingNear(EM, idleSCV, BarracksDefID,
                Core::Math::Vec2(mAiBasePos.X + 100.0f,
                                 mAiBasePos.Y + 80.0f * (float)(barracksCount + 1)));
    }
    if (academyStartedCount < 1 && barracksCount >= 1 &&
        Res.Minerals >= 150)
    {
        int idleSCV = FindTerranBuilderFor(EM, AcademyDefID);
        if (idleSCV >= 0)
            StartBuildingNear(EM, idleSCV, AcademyDefID,
                Core::Math::Vec2(mAiBasePos.X + 180.0f, mAiBasePos.Y));
    }

    const int marineTarget = mDifficulty == AIDifficulty::Easy ? 14 :
        (mDifficulty == AIDifficulty::Medium ? 18 : 24);
    const int firebatTarget = mDifficulty == AIDifficulty::Easy ? 0 :
        (mDifficulty == AIDifficulty::Medium ? 4 :
            (mEnemyIntel.VisibleGroundCombat >= 8 ? 8 : 6));
    const int medicTarget = mDifficulty == AIDifficulty::Easy ? 2 :
        (mDifficulty == AIDifficulty::Medium ? 4 : 6);
    if (marineCount + CountQueuedByDefID(EM, mAIPlayerID, MarineDefID) <
        marineTarget)
        TrainUnitIfPossible(EM, MarineDefID);
    if (academyStartedCount > 0 &&
        firebatCount + CountQueuedByDefID(EM, mAIPlayerID, FirebatDefID) <
            firebatTarget)
        TrainUnitIfPossible(EM, FirebatDefID);
    if (academyStartedCount > 0 &&
        medicCount + CountQueuedByDefID(EM, mAIPlayerID, MedicDefID) <
            medicTarget)
        TrainUnitIfPossible(EM, MedicDefID);

    if (CountByDefID(EM, mAIPlayerID, EngineeringBayDefID, true) < 1 &&
        barracksCount >= 1 &&
        (mDifficulty != AIDifficulty::Easy || mElapsedSeconds >= 600.0f) &&
        Res.Minerals >= 125)
    {
        const int Builder = FindTerranBuilderFor(EM, EngineeringBayDefID);
        if (Builder >= 0)
            StartBuildingNear(EM, Builder, EngineeringBayDefID,
                Core::Math::Vec2(mAiBasePos.X + 220.0f, mAiBasePos.Y - 80.0f));
    }
    if (CountByDefID(EM, mAIPlayerID, BunkerDefID, true) <
            (mDifficulty == AIDifficulty::Hard ? completedBases : 1) &&
        barracksCount >= 1 && Res.Minerals >= 100)
    {
        const int Builder = FindTerranBuilderFor(EM, BunkerDefID);
        if (Builder >= 0)
            StartBuildingNear(EM, Builder, BunkerDefID,
                Core::Math::Vec2(mAiBasePos.X, mAiBasePos.Y + 180.0f));
    }

    // Stage 1: Factory + Machine Shop
    if (mStage >= 1)
    {
        const int desiredFactories = mDifficulty == AIDifficulty::Easy ? 1 :
            (mDifficulty == AIDifficulty::Medium ? 2 : std::min(4, completedBases + 1));
        if (factoryStartedCount < desiredFactories &&
            Res.Minerals >= 200 && Res.VespeneGas >= 100)
        {
            int idleSCV = FindTerranBuilderFor(EM, FactoryDefID);
            if (idleSCV >= 0)
                StartBuildingNear(EM, idleSCV, FactoryDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }
        const int desiredMachineShops = mDifficulty == AIDifficulty::Hard
            ? std::min(2, factoryCount) : std::min(1, factoryCount);
        if (factoryCount >= 1 &&
            CountByDefID(EM, mAIPlayerID, MachineShopDefID, true) <
                desiredMachineShops &&
            Res.Minerals >= 50 && Res.VespeneGas >= 50)
        {
            int factory = FindAvailableAddonParent(EM, FactoryDefID);
            if (factory >= 0)
                StartBuildingNear(EM, factory, MachineShopDefID,
                    Core::Math::Vec2(mAiBasePos.X - 120.0f, mAiBasePos.Y));
        }
        const int tankTarget = mDifficulty == AIDifficulty::Easy ? 4 :
            (mDifficulty == AIDifficulty::Medium ? 6 : 8);
        const int goliathTarget = mDifficulty == AIDifficulty::Easy ? 2 :
            (mDifficulty == AIDifficulty::Medium ? 4 :
                (mEnemyIntel.HasSeenAir ? 10 : 6));
        const int vultureTarget = mDifficulty == AIDifficulty::Easy ? 0 :
            (mDifficulty == AIDifficulty::Medium ? 3 : 6);
        if (tankCount + CountQueuedByDefID(EM, mAIPlayerID, TankDefID) <
            tankTarget)
            TrainUnitIfPossible(EM, TankDefID);
        if (goliathCount + CountQueuedByDefID(EM, mAIPlayerID, GoliathDefID) <
            goliathTarget)
            TrainUnitIfPossible(EM, GoliathDefID);
        if (vultureCount + CountQueuedByDefID(EM, mAIPlayerID, VultureDefID) <
            vultureTarget)
            TrainUnitIfPossible(EM, VultureDefID);

        if (mDifficulty != AIDifficulty::Easy &&
            CountByDefID(EM, mAIPlayerID, ArmoryDefID, true) < 1 &&
            factoryCount >= 1 && Res.Minerals >= 100 && Res.VespeneGas >= 50)
        {
            const int Builder = FindTerranBuilderFor(EM, ArmoryDefID);
            if (Builder >= 0)
                StartBuildingNear(EM, Builder, ArmoryDefID,
                    Core::Math::Vec2(mAiBasePos.X - 120.0f, mAiBasePos.Y + 160.0f));
        }
    }

    // Stage 2: Starport + Wraiths
    if (mStage >= 2)
    {
        const int desiredStarports = mDifficulty == AIDifficulty::Hard ? 2 : 1;
        if (starportStartedCount < desiredStarports &&
            Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            int idleSCV = FindTerranBuilderFor(EM, StarportDefID);
            if (idleSCV >= 0)
                StartBuildingNear(EM, idleSCV, StarportDefID,
                    Core::Math::Vec2(mAiBasePos.X - 160.0f, mAiBasePos.Y - 80.0f));
        }
        if (starportCount >= 1 &&
            CountByDefID(EM, mAIPlayerID, ControlTowerDefID, true) < 1 &&
            Res.Minerals >= 50 && Res.VespeneGas >= 50)
        {
            const int Starport = FindAvailableAddonParent(EM, StarportDefID);
            if (Starport >= 0)
                StartBuildingNear(EM, Starport, ControlTowerDefID, mAiBasePos);
        }
        const int wraithTarget = mDifficulty == AIDifficulty::Hard ? 4 : 2;
        if (wraithCount + CountQueuedByDefID(EM, mAIPlayerID, WraithDefID) <
            wraithTarget)
            TrainUnitIfPossible(EM, WraithDefID);
        const int valkyrieTarget = mEnemyIntel.HasSeenAir
            ? (mDifficulty == AIDifficulty::Hard ? 4 : 2) : 0;
        if (valkyrieCount + CountQueuedByDefID(
                EM, mAIPlayerID, ValkyrieDefID) < valkyrieTarget)
        {
            TrainUnitIfPossible(EM, ValkyrieDefID);
        }
    }

    // Stage 3: Science Facility + Battlecruisers
    if (mStage >= 3)
    {
        if (CountByDefID(EM, mAIPlayerID, ScienceFacilityDefID, true) < 1 &&
            Res.Minerals >= 100 && Res.VespeneGas >= 150)
        {
            int idleSCV = FindTerranBuilderFor(EM, ScienceFacilityDefID);
            if (idleSCV >= 0)
                StartBuildingNear(EM, idleSCV, ScienceFacilityDefID,
                    Core::Math::Vec2(mAiBasePos.X - 200.0f, mAiBasePos.Y - 160.0f));
        }
        if (CountByDefID(EM, mAIPlayerID, ScienceFacilityDefID) >= 1 &&
            CountByDefID(EM, mAIPlayerID, PhysicsLabDefID, true) < 1 &&
            Res.Minerals >= 50 && Res.VespeneGas >= 50)
        {
            const int Facility = FindAvailableAddonParent(EM, ScienceFacilityDefID);
            if (Facility >= 0)
                StartBuildingNear(EM, Facility, PhysicsLabDefID, mAiBasePos);
        }
        const int vesselTarget = mEnemyIntel.HasSeenCloak ? 2 : 1;
        if (vesselCount + CountQueuedByDefID(
                EM, mAIPlayerID, ScienceVesselDefID) < vesselTarget)
        {
            TrainUnitIfPossible(EM, ScienceVesselDefID);
        }
        if (battlecruiserCount +
                CountQueuedByDefID(EM, mAIPlayerID, BattlecruiserDefID) < 2)
            TrainUnitIfPossible(EM, BattlecruiserDefID);
    }

    const bool SeenAirOrCloak =
        mEnemyIntel.HasSeenAir || mEnemyIntel.HasSeenCloak;
    const int TurretsPerBase =
        AITerranTurretsPerBase(mDifficulty, SeenAirOrCloak);
    const int desiredTurrets = completedBases * TurretsPerBase;
    Core::Math::Vec2 TurretBasePosition = mAiBasePos;
    bool HasUnderDefendedBase = TurretsPerBase == 0;
    if (TurretsPerBase > 0)
    {
        for (int DepotID : EM.GetEntitiesWith<BuildingComponent, PlayerComponent,
                                               TransformComponent>())
        {
            auto* DepotBuilding = EM.GetComponent<BuildingComponent>(DepotID);
            auto* DepotPlayer = EM.GetComponent<PlayerComponent>(DepotID);
            auto* DepotPosition = EM.GetComponent<TransformComponent>(DepotID);
            if (!DepotBuilding || !DepotPlayer || !DepotPosition ||
                DepotPlayer->PlayerID != mAIPlayerID ||
                DepotBuilding->UnderConstruction ||
                !IsResourceDepotDefID(ResolveOwnedDefID(EM, DepotID)))
            {
                continue;
            }

            int NearbyTurrets = 0;
            for (int TurretID : EM.GetEntitiesWith<BuildingComponent,
                                                   PlayerComponent,
                                                   TransformComponent>())
            {
                auto* TurretPlayer = EM.GetComponent<PlayerComponent>(TurretID);
                auto* TurretPosition = EM.GetComponent<TransformComponent>(TurretID);
                if (TurretPlayer && TurretPosition &&
                    TurretPlayer->PlayerID == mAIPlayerID &&
                    ResolveOwnedDefID(EM, TurretID) == MissileTurretDefID &&
                    TurretPosition->Position.DistanceTo(DepotPosition->Position) <=
                        480.0f)
                {
                    ++NearbyTurrets;
                }
            }
            if (NearbyTurrets < TurretsPerBase)
            {
                TurretBasePosition = DepotPosition->Position;
                HasUnderDefendedBase = true;
                break;
            }
        }
    }
    if (CountByDefID(EM, mAIPlayerID, MissileTurretDefID, true) < desiredTurrets &&
        HasUnderDefendedBase &&
        CountByDefID(EM, mAIPlayerID, EngineeringBayDefID) >= 1 &&
        Res.Minerals >= 75)
    {
        const int Builder = FindTerranBuilderFor(EM, MissileTurretDefID);
        if (Builder >= 0)
            StartBuildingNear(EM, Builder, MissileTurretDefID,
                Core::Math::Vec2(TurretBasePosition.X + 120.0f,
                                 TurretBasePosition.Y + 120.0f));
    }

    const float nextExpansionTime = Profile.ExpansionStartSeconds +
        (float)std::max(0, startedBases - 1) * Profile.ExpansionIntervalSeconds;
    if (startedBases < Profile.MaxBases &&
        mElapsedSeconds >= nextExpansionTime &&
        mElapsedSeconds - mLastExpansionOrderSeconds >= 60.0f &&
        scvCount >= std::min(workerTarget, 16 + 8 * (startedBases - 1)) &&
        marineCount + tankCount + goliathCount >=
            std::max(6, Profile.AttackWaveSize[0] / 2) &&
        Res.Minerals >= 400)
    {
        mExpansionPos = FindExpansionLocation(EM);
        const int Builder = FindTerranBuilderFor(EM, CommandCenterDefID);
        if (Builder >= 0 &&
            StartBuildingNear(EM, Builder, CommandCenterDefID, mExpansionPos))
        {
            mLastExpansionOrderSeconds = mElapsedSeconds;
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " expansion ordered race=Terran base_target=" +
                std::to_string(startedBases + 1) + " difficulty=" +
                std::string(AIDifficultyName(mDifficulty)));
        }
    }

    ManageGas(EM, RefineryDefID);

    // Send harvesters to work
    ManageEconomy(EM);

    // Attack wave
    int totalCombat = marineCount + firebatCount + medicCount + vultureCount +
        tankCount + goliathCount + wraithCount + vesselCount + valkyrieCount +
        battlecruiserCount;
    const int waveSize = Profile.AttackWaveSize[0];
    const int earlyWaveSize = std::min(waveSize, std::max(3, waveSize / 4));
    const int effectiveWaveSize = (mElapsedSeconds >= 60.0f && totalCombat >= earlyWaveSize)
        ? earlyWaveSize : waveSize;
    if (!mDefendingBase &&
        (totalCombat >= effectiveWaveSize || mElapsedSeconds >= 1800.0f))
        SendAttackWave(EM, effectiveWaveSize);
}

void AIBrain::RunZergShowcase(Core::ECS::EntityManager& EM)
{
    auto& Resources = HarvestSystem::GetResources(mAIPlayerID);
    const int DroneDefID = Game::Races::GetUnitDefID("Drone");
    const int OverlordDefID = Game::Races::GetUnitDefID("Overlord");
    const int HydraliskDefID = Game::Races::GetUnitDefID("Hydralisk");
    const int LurkerDefID = Game::Races::GetUnitDefID("Lurker");
    const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    const int LairDefID = Game::Races::GetUnitDefID("Lair");
    const int HiveDefID = Game::Races::GetUnitDefID("Hive");
    const int SpireDefID = Game::Races::GetUnitDefID("Spire");
    const int GreaterSpireDefID = Game::Races::GetUnitDefID("Greater Spire");
    const int ExtractorDefID = Game::Races::GetUnitDefID("Extractor");

    const auto CountPendingMorphs = [&](int TargetDefID)
    {
        int Count = 0;
        for (int Eid : EM.GetEntitiesWith<MorphComponent, PlayerComponent>())
        {
            auto* Morph = EM.GetComponent<MorphComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (Morph && Player && Player->PlayerID == mAIPlayerID &&
                Morph->IsMorphing && Morph->MorphTargetDefID == TargetDefID)
            {
                ++Count;
            }
        }
        return Count;
    };
    const auto FindIdleLarva = [&]()
    {
        for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<UnitComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            auto* Morph = EM.GetComponent<MorphComponent>(Eid);
            if (Unit && Player && Player->PlayerID == mAIPlayerID &&
                Unit->ChkUnitID == 0x23 && Unit->HitPoints > 0 &&
                (!Morph || !Morph->IsMorphing))
            {
                return Eid;
            }
        }
        return -1;
    };
    const auto MorphLarva = [&](int TargetDefID, const char* Label)
    {
        const int LarvaID = FindIdleLarva();
        if (LarvaID < 0 ||
            !ZergSystem::StartMorphLarvaIntoUnit(EM, LarvaID, TargetDefID))
        {
            return false;
        }
        Core::Utils::Logger::Info(
            "AIBrain", "Showcase ordered real larva morph: " +
            std::string(Label));
        return true;
    };
    const auto MorphOwnedBuilding = [&](int SourceDefID, int TargetDefID)
    {
        for (int Eid : EM.GetEntitiesWith<BuildingComponent, PlayerComponent>())
        {
            auto* Building = EM.GetComponent<BuildingComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            if (!Building || !Player || Player->PlayerID != mAIPlayerID ||
                Building->BuildingDefID != SourceDefID || Building->UnderConstruction)
            {
                continue;
            }
            if (ZergSystem::StartMorphBuilding(EM, Eid, TargetDefID))
            {
                Core::Utils::Logger::Info(
                    "AIBrain", "Showcase ordered real building morph " +
                    Game::Races::GetUnitDefByID(SourceDefID).Name + " -> " +
                    Game::Races::GetUnitDefByID(TargetDefID).Name);
                return true;
            }
        }
        return false;
    };
    const auto RunBuildingGoals = [&](const auto& Goals, size_t LayoutOffset)
    {
        for (size_t GoalIndex = 0; GoalIndex < std::size(Goals); ++GoalIndex)
        {
            const int DefID = Game::Races::GetUnitDefID(Goals[GoalIndex].Name);
            if (DefID < 0 || CountByDefID(
                    EM, mAIPlayerID, DefID, true) >= Goals[GoalIndex].TargetCount)
            {
                continue;
            }
            const int DroneID = FindZergBuilderFor(EM, DefID);
            bool Started = false;
            if (DroneID >= 0 && DefID == ExtractorDefID)
                Started = StartZergGasBuilding(EM, DroneID, DefID);
            else if (DroneID >= 0)
            {
                Started = StartZergBuildingNear(
                    EM, DroneID, DefID,
                    ShowcaseBuildPosition(EM, LayoutOffset + GoalIndex));
            }
            if (Started)
            {
                Core::Utils::Logger::Info(
                    "AIBrain", "Showcase ordered Zerg building " +
                    Game::Races::GetUnitDefByID(DefID).Name +
                    "; Drone is moving to the morph site");
            }
            return false;
        }
        return true;
    };

    ManageGas(EM, ExtractorDefID);
    ManageEconomy(EM);

    // Supply must be resolved before worker growth. Otherwise a capped Drone
    // morph fails every decision tick and prevents the Brain from ever
    // reaching its Overlord rule.
    if (Resources.SupplyUsed + 4 > Resources.SupplyMax &&
        CountPendingMorphs(OverlordDefID) == 0)
    {
        MorphLarva(OverlordDefID, "Overlord");
        return;
    }

    // Every ordinary structure removes one live Drone after it reaches the
    // morph site. Keep workforce replacement in the same real larva/supply/
    // resource pipeline so the count recovers only after a new egg completes.
    constexpr int DesiredDroneCount = 16;
    const int ActiveDrones = CountByDefID(EM, mAIPlayerID, DroneDefID);
    const int PendingDrones = CountPendingMorphs(DroneDefID);
    if (ActiveDrones + PendingDrones < DesiredDroneCount)
    {
        MorphLarva(DroneDefID, "Drone");
        return;
    }

    struct BuildingGoal { const char* Name; int TargetCount; };
    static const BuildingGoal EarlyGoals[] = {
        {"Extractor", 1}, {"Spawning Pool", 1},
        {"Evolution Chamber", 1}, {"Hatchery", 2}
    };
    if (!RunBuildingGoals(EarlyGoals, 0u))
        return;

    if (CountByDefID(EM, mAIPlayerID, LairDefID) < 1)
    {
        MorphOwnedBuilding(HatcheryDefID, LairDefID);
        return;
    }

    static const BuildingGoal LairGoals[] = {
        {"Hydralisk Den", 1}, {"Spire", 1}, {"Queen's Nest", 1},
        {"Nydus Canal", 1}
    };
    if (!RunBuildingGoals(LairGoals, 6u))
        return;

    if (CountByDefID(EM, mAIPlayerID, HiveDefID) < 1)
    {
        MorphOwnedBuilding(LairDefID, HiveDefID);
        return;
    }

    static const BuildingGoal HiveGoals[] = {
        {"Ultralisk Cavern", 1}, {"Defiler Mound", 1}
    };
    if (!RunBuildingGoals(HiveGoals, 12u))
        return;

    if (CountByDefID(EM, mAIPlayerID, GreaterSpireDefID) < 1)
    {
        MorphOwnedBuilding(SpireDefID, GreaterSpireDefID);
        return;
    }

    static const char* UnitGoals[] = {
        "Zergling", "Hydralisk", "Mutalisk", "Scourge",
        "Queen", "Ultralisk", "Defiler"
    };
    for (const char* Name : UnitGoals)
    {
        const int DefID = Game::Races::GetUnitDefID(Name);
        if (CountByDefID(EM, mAIPlayerID, DefID) > 0)
            continue;
        if (CountPendingMorphs(DefID) == 0)
            MorphLarva(DefID, Name);
        return;
    }

    if (CountByDefID(EM, mAIPlayerID, LurkerDefID) == 0)
    {
        if (!UpgradeSystem::HasResearch(
                mAIPlayerID, ResearchId::Zerg_LurkerMorph))
        {
            TryStartResearch(
                EM, {(int)ResearchId::Zerg_LurkerMorph}, 1);
            return;
        }
        if (CountPendingMorphs(LurkerDefID) == 0)
        {
            for (int EntityID : EM.GetEntitiesWith<UnitComponent,
                                                    PlayerComponent>())
            {
                auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
                auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
                if (Unit && Player && Player->PlayerID == mAIPlayerID &&
                    Unit->HitPoints > 0 &&
                    ResolveUnitDefID(*Unit) == HydraliskDefID &&
                    ZergSystem::StartMorphHydraliskIntoLurker(EM, EntityID))
                {
                    Core::Utils::Logger::Info(
                        "AIBrain",
                        "Showcase ordered Hydralisk -> Lurker morph");
                    break;
                }
            }
        }
        return;
    }

    if (!mShowcaseCompleteLogged)
    {
        mShowcaseCompleteLogged = true;
        Core::Utils::Logger::Info(
            "AIBrain",
            "Showcase complete: Zerg tech tree and normal larva units ready; "
            "all buildings used real Drone consumption");
    }
}

void AIBrain::RunZergAI(Core::ECS::EntityManager& EM)
{
    int DroneDefID = Game::Races::GetUnitDefID("Drone");
    int OverlordDefID = Game::Races::GetUnitDefID("Overlord");
    int ZerglingDefID = Game::Races::GetUnitDefID("Zergling");
    int HydraliskDefID = Game::Races::GetUnitDefID("Hydralisk");
    int LurkerDefID = Game::Races::GetUnitDefID("Lurker");
    int MutaliskDefID = Game::Races::GetUnitDefID("Mutalisk");
    int ScourgeDefID = Game::Races::GetUnitDefID("Scourge");
    int UltraliskDefID = Game::Races::GetUnitDefID("Ultralisk");
    int DefilerDefID = Game::Races::GetUnitDefID("Defiler");
    int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    int LairDefID = Game::Races::GetUnitDefID("Lair");
    int HiveDefID = Game::Races::GetUnitDefID("Hive");
    int SpawningPoolDefID = Game::Races::GetUnitDefID("Spawning Pool");
    int HydraliskDenDefID = Game::Races::GetUnitDefID("Hydralisk Den");
    int SpireDefID = Game::Races::GetUnitDefID("Spire");
    int QueensNestDefID = Game::Races::GetUnitDefID("Queen's Nest");
    int UltraliskCavernDefID = Game::Races::GetUnitDefID("Ultralisk Cavern");
    int DefilerMoundDefID = Game::Races::GetUnitDefID("Defiler Mound");
    int EvolutionChamberDefID = Game::Races::GetUnitDefID("Evolution Chamber");
    int CreepColonyDefID = Game::Races::GetUnitDefID("Creep Colony");
    int SporeColonyDefID = Game::Races::GetUnitDefID("Spore Colony");
    int SunkenColonyDefID = Game::Races::GetUnitDefID("Sunken Colony");
    int ExtractorDefID = Game::Races::GetUnitDefID("Extractor");

    const auto& Profile = GetAIDifficultyProfile(mDifficulty);
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    const int completedBases = std::max(1, CountOwnedResourceDepots(EM, false));
    const int startedBases = CountOwnedResourceDepots(EM, true);
    const int workerTarget = CalculateAIWorkerTarget(Profile, completedBases);
    int droneCount = CountByDefID(EM, mAIPlayerID, DroneDefID);
    int hatcheryCount = CountByDefID(EM, mAIPlayerID, HatcheryDefID);
    int lairCount = CountByDefID(EM, mAIPlayerID, LairDefID);
    int poolCount = CountByDefID(EM, mAIPlayerID, SpawningPoolDefID);
    int poolStartedCount = CountByDefID(
        EM, mAIPlayerID, SpawningPoolDefID, true);
    int denCount = CountByDefID(EM, mAIPlayerID, HydraliskDenDefID);
    int spireCount = CountByDefID(EM, mAIPlayerID, SpireDefID);
    int zerglingCount = CountByDefID(EM, mAIPlayerID, ZerglingDefID);
    int hydraCount = CountByDefID(EM, mAIPlayerID, HydraliskDefID);
    int lurkerCount = CountByDefID(EM, mAIPlayerID, LurkerDefID);
    int mutaCount = CountByDefID(EM, mAIPlayerID, MutaliskDefID);
    int scourgeCount = CountByDefID(EM, mAIPlayerID, ScourgeDefID);
    int ultraCount = CountByDefID(EM, mAIPlayerID, UltraliskDefID);
    int defilerCount = CountByDefID(EM, mAIPlayerID, DefilerDefID);
    int extractorStartedCount = CountByDefID(
        EM, mAIPlayerID, ExtractorDefID, true);

    int hiveCount = CountByDefID(EM, mAIPlayerID, HiveDefID);

    const bool LurkerAspectResearched = UpgradeSystem::HasResearch(
        mAIPlayerID, ResearchId::Zerg_LurkerMorph);
    bool LurkerAspectResearchInProgress = false;
    for (int BuildingID : EM.GetEntitiesWith<BuildingComponent,
                                               PlayerComponent>())
    {
        const auto* Building = EM.GetComponent<BuildingComponent>(BuildingID);
        const auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
        if (Building && Player && Player->PlayerID == mAIPlayerID &&
            Building->IsResearching &&
            Building->ResearchingID == (int)ResearchId::Zerg_LurkerMorph)
        {
            LurkerAspectResearchInProgress = true;
            break;
        }
    }
    const bool ReserveLurkerAspect = AIShouldReserveForLurkerAspect(
        Profile.MaxTechStage,
        lairCount > 0 || hiveCount > 0,
        denCount > 0,
        hydraCount > 0,
        LurkerAspectResearched);

    // Find idle larvae
    auto FindIdleLarva = [&]() -> int {
        auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
        for (int Eid : Units)
        {
            auto* U = EM.GetComponent<UnitComponent>(Eid);
            auto* P = EM.GetComponent<PlayerComponent>(Eid);
            auto* Morph = EM.GetComponent<MorphComponent>(Eid);
            if (U && P && P->PlayerID == mAIPlayerID &&
                U->ChkUnitID == 0x23 && U->HitPoints > 0 &&
                (!Morph || !Morph->IsMorphing))
                return Eid;
        }
        return -1;
    };

    auto CountMorphingInto = [&](int DefID)
    {
        int Count = 0;
        for (int EntityID : EM.GetEntitiesWith<MorphComponent, PlayerComponent>())
        {
            auto* Morph = EM.GetComponent<MorphComponent>(EntityID);
            auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
            if (Morph && Player && Player->PlayerID == mAIPlayerID &&
                Morph->IsMorphing && Morph->MorphTargetDefID == DefID)
            {
                ++Count;
            }
        }
        return Count;
    };

    // Stage progression
    if (mStage < 1 && droneCount >= 10 && poolCount >= 1 && zerglingCount >= 6)
        mStage = 1;
    if (Profile.MaxTechStage >= 2 && mStage < 2 &&
        lairCount >= 1 && denCount >= 1 && hydraCount >= 6)
        mStage = 2;
    if (Profile.MaxTechStage >= 3 && mStage < 3 && hiveCount >= 1)
        mStage = 3;
    mStage = std::min(mStage, Profile.MaxTechStage);

    // Supply: morph overlords
    const int maxConcurrentOverlords = Res.SupplyUsed >= 60 ? 3 : (Res.SupplyUsed >= 32 ? 2 : 1);
    if (Res.SupplyMax < 200 &&
        Res.SupplyUsed + Profile.SupplyBuffer >= Res.SupplyMax &&
        CountMorphingInto(OverlordDefID) < maxConcurrentOverlords &&
        Res.Minerals >= 100)
    {
        int larva = FindIdleLarva();
        if (larva >= 0 &&
            ZergSystem::StartMorphLarvaIntoUnit(EM, larva, OverlordDefID))
        {
        }
    }

    const int desiredExtractors = std::min(
        completedBases,
        mDifficulty == AIDifficulty::Easy ? 1 :
            (mDifficulty == AIDifficulty::Medium ? 2 : 3));
    const bool NeedExtractor = droneCount >= (mDifficulty == AIDifficulty::Easy ? 10 : 8) &&
        extractorStartedCount < desiredExtractors;
    const int GasMineralReserve = (NeedExtractor && zerglingCount >= 4) ? 50 : 0;
    if (NeedExtractor && Res.Minerals >= 50)
    {
        const int Drone = FindZergBuilderFor(EM, ExtractorDefID);
        if (Drone >= 0)
            StartZergGasBuilding(EM, Drone, ExtractorDefID);
    }

    if (AIShouldPauseZergMacroForLurkerAspect(
            ReserveLurkerAspect, LurkerAspectResearchInProgress))
    {
        // ManageUpgrades already tried to start the Aspect this tick. Keep
        // harvesting (and permit an Extractor rebuild above), but do not let
        // replacement Hydralisks or later tech consume its reserved 200/200.
        ManageGas(EM, ExtractorDefID);
        ManageEconomy(EM);
        return;
    }

    const int openingWorkerTarget = mDifficulty == AIDifficulty::Easy ? 12 : 14;
    const int currentWorkerTarget = mStage < 1
        ? std::min(workerTarget, openingWorkerTarget) : workerTarget;
    if (droneCount + CountMorphingInto(DroneDefID) < currentWorkerTarget &&
        Res.Minerals >= 50 + GasMineralReserve &&
        (poolCount == 0 || zerglingCount >= 4 || droneCount < 6))
    {
        const int Larva = FindIdleLarva();
        if (Larva >= 0)
            ZergSystem::StartMorphLarvaIntoUnit(EM, Larva, DroneDefID);
    }

    // The Pool is a foundational dependency, not a one-time opening step. If
    // it is destroyed after stage progression, rebuild it before every later
    // production attempt keeps failing its prerequisite check.
    if (AIShouldBuildSpawningPool(poolStartedCount, droneCount, Res.Minerals))
    {
        int drone = FindZergBuilderFor(EM, SpawningPoolDefID);
        if (drone >= 0)
            StartZergBuildingNear(EM, drone, SpawningPoolDefID,
                Core::Math::Vec2(mAiBasePos.X + 100.0f, mAiBasePos.Y));
    }

    // Stage 0: Drones + Zerglings.
    if (mStage < 1)
    {
        // Zerglings
        if (poolCount >= 1 &&
            zerglingCount + CountMorphingInto(ZerglingDefID) < 6 &&
            Res.Minerals >= 50 + GasMineralReserve)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, ZerglingDefID);
        }
    }

    const int zerglingTarget = mDifficulty == AIDifficulty::Easy ? 10 :
        (mDifficulty == AIDifficulty::Medium ? 14 : 20);
    if (poolCount >= 1 &&
        zerglingCount + CountMorphingInto(ZerglingDefID) < zerglingTarget &&
        Res.Minerals >= 50 + GasMineralReserve)
    {
        const int Larva = FindIdleLarva();
        if (Larva >= 0)
            ZergSystem::StartMorphLarvaIntoUnit(EM, Larva, ZerglingDefID);
    }

    if (CountByDefID(EM, mAIPlayerID, EvolutionChamberDefID, true) < 1 &&
        poolCount >= 1 &&
        (mDifficulty != AIDifficulty::Easy || mElapsedSeconds >= 600.0f) &&
        Res.Minerals >= 75)
    {
        const int Drone = FindZergBuilderFor(EM, EvolutionChamberDefID);
        if (Drone >= 0)
            StartZergBuildingNear(EM, Drone, EvolutionChamberDefID,
                Core::Math::Vec2(mAiBasePos.X + 160.0f, mAiBasePos.Y + 80.0f));
    }

    // Stage 1: Lair + Hydralisk Den + Hydralisks.
    if (mStage >= 1)
    {
        // Morph Hatchery→Lair
        if (lairCount < 1 && hiveCount < 1 && hatcheryCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
            for (int Bid : Buildings)
            {
                auto* B = EM.GetComponent<BuildingComponent>(Bid);
                auto* P = EM.GetComponent<PlayerComponent>(Bid);
                if (B && P && P->PlayerID == mAIPlayerID &&
                    B->BuildingDefID == HatcheryDefID && !B->UnderConstruction)
                {
                    ZergSystem::StartMorphBuilding(EM, Bid, LairDefID);
                    break;
                }
            }
        }

        // Hydralisk Den
        if (CountByDefID(EM, mAIPlayerID, HydraliskDenDefID, true) < 1 &&
            poolCount >= 1 && Res.Minerals >= 100 && Res.VespeneGas >= 50)
        {
            int drone = FindZergBuilderFor(EM, HydraliskDenDefID);
            if (drone >= 0)
                StartZergBuildingNear(EM, drone, HydraliskDenDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }

        // Hydralisks remain the reliable core for every difficulty.
        const int hydraTarget = mDifficulty == AIDifficulty::Easy ? 8 :
            (mDifficulty == AIDifficulty::Medium ? 14 :
                (mEnemyIntel.HasSeenAir ? 22 : 18));
        if (denCount >= 1 &&
            hydraCount + CountMorphingInto(HydraliskDefID) < hydraTarget &&
            Res.Minerals >= 75 && Res.VespeneGas >= 25)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, HydraliskDefID);
        }

        // Medium and Hard branch into air once the Lair economy is stable.
        if (Profile.MaxTechStage >= 2 &&
            CountByDefID(EM, mAIPlayerID, SpireDefID, true) < 1 &&
            lairCount >= 1 && Res.Minerals >= 200 && Res.VespeneGas >= 150)
        {
            int drone = FindZergBuilderFor(EM, SpireDefID);
            if (drone >= 0)
                StartZergBuildingNear(EM, drone, SpireDefID,
                    Core::Math::Vec2(mAiBasePos.X - 140.0f, mAiBasePos.Y));
        }

        const int mutaTarget = Profile.MaxTechStage >= 2
            ? (mDifficulty == AIDifficulty::Hard && !mEnemyIntel.HasSeenAir ? 9 : 6)
            : 0;
        if (spireCount >= 1 &&
            mutaCount + CountMorphingInto(MutaliskDefID) < mutaTarget &&
            Res.Minerals >= 100 && Res.VespeneGas >= 100)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, MutaliskDefID);
        }

        const int lurkerTarget = Profile.MaxTechStage >= 2
            ? (mDifficulty == AIDifficulty::Hard ? 5 : 3) : 0;
        if (AIShouldMorphLurker(
                lurkerCount, CountMorphingInto(LurkerDefID), lurkerTarget,
                UpgradeSystem::HasResearch(
                    mAIPlayerID, ResearchId::Zerg_LurkerMorph),
                Res.Minerals, Res.VespeneGas))
        {
            for (int EntityID : EM.GetEntitiesWith<UnitComponent,
                                                    PlayerComponent>())
            {
                auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
                auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
                if (Unit && Player && Player->PlayerID == mAIPlayerID &&
                    Unit->HitPoints > 0 &&
                    ResolveUnitDefID(*Unit) == HydraliskDefID &&
                    ZergSystem::StartMorphHydraliskIntoLurker(EM, EntityID))
                {
                    break;
                }
            }
        }

        const int scourgeTarget = mEnemyIntel.HasSeenAir
            ? (mDifficulty == AIDifficulty::Hard ? 8 : 4) : 0;
        if (spireCount >= 1 &&
            scourgeCount + CountMorphingInto(ScourgeDefID) < scourgeTarget &&
            Res.Minerals >= 25 && Res.VespeneGas >= 75)
        {
            const int Larva = FindIdleLarva();
            if (Larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, Larva, ScourgeDefID);
        }
    }

    // Hard Stage 3: complete the real Queen's Nest -> Hive dependency chain,
    // then add durable Ultralisks and spell support.
    if (Profile.MaxTechStage >= 3 && mStage >= 2)
    {
        if (CountByDefID(EM, mAIPlayerID, QueensNestDefID, true) < 1 &&
            lairCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            const int Drone = FindZergBuilderFor(EM, QueensNestDefID);
            if (Drone >= 0)
                StartZergBuildingNear(EM, Drone, QueensNestDefID,
                    Core::Math::Vec2(mAiBasePos.X - 180.0f,
                                     mAiBasePos.Y + 80.0f));
        }

        if (hiveCount < 1 && lairCount >= 1 &&
            CountByDefID(EM, mAIPlayerID, QueensNestDefID) >= 1 &&
            Res.Minerals >= 200 && Res.VespeneGas >= 150)
        {
            auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
            for (int Bid : Buildings)
            {
                auto* B = EM.GetComponent<BuildingComponent>(Bid);
                auto* P = EM.GetComponent<PlayerComponent>(Bid);
                if (B && P && P->PlayerID == mAIPlayerID &&
                    B->BuildingDefID == LairDefID && !B->UnderConstruction)
                {
                    ZergSystem::StartMorphBuilding(EM, Bid, HiveDefID);
                    break;
                }
            }
        }

        if (CountByDefID(EM, mAIPlayerID, UltraliskCavernDefID, true) < 1 &&
            hiveCount >= 1 &&
            Res.Minerals >= 150 && Res.VespeneGas >= 200)
        {
            int drone = FindZergBuilderFor(EM, UltraliskCavernDefID);
            if (drone >= 0)
                StartZergBuildingNear(EM, drone, UltraliskCavernDefID,
                    Core::Math::Vec2(mAiBasePos.X - 180.0f, mAiBasePos.Y - 80.0f));
        }

        if (ultraCount + CountMorphingInto(UltraliskDefID) < 4 &&
            CountByDefID(EM, mAIPlayerID, UltraliskCavernDefID) >= 1 &&
            Res.Minerals >= 200 && Res.VespeneGas >= 200)
        {
            int larva = FindIdleLarva();
            if (larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, larva, UltraliskDefID);
        }

        if (CountByDefID(EM, mAIPlayerID, DefilerMoundDefID, true) < 1 &&
            hiveCount >= 1 && Res.Minerals >= 100 && Res.VespeneGas >= 150)
        {
            const int Drone = FindZergBuilderFor(EM, DefilerMoundDefID);
            if (Drone >= 0)
                StartZergBuildingNear(EM, Drone, DefilerMoundDefID,
                    Core::Math::Vec2(mAiBasePos.X - 220.0f,
                                     mAiBasePos.Y - 80.0f));
        }
        if (CountByDefID(EM, mAIPlayerID, DefilerMoundDefID) >= 1 &&
            defilerCount + CountMorphingInto(DefilerDefID) < 2 &&
            Res.Minerals >= 50 && Res.VespeneGas >= 150)
        {
            const int Larva = FindIdleLarva();
            if (Larva >= 0)
                ZergSystem::StartMorphLarvaIntoUnit(EM, Larva, DefilerDefID);
        }
    }

    const int desiredColonies = mDifficulty == AIDifficulty::Easy ? 1 :
        (mDifficulty == AIDifficulty::Medium ? 2 : completedBases * 2);
    const bool NeedSpore = mEnemyIntel.HasSeenAir || mEnemyIntel.HasSeenCloak;
    const int creepColonyCount =
        CountByDefID(EM, mAIPlayerID, CreepColonyDefID, true);
    const int sporeColonyCount =
        CountByDefID(EM, mAIPlayerID, SporeColonyDefID, true);
    const int sunkenColonyCount =
        CountByDefID(EM, mAIPlayerID, SunkenColonyDefID, true);
    const int existingColonies =
        creepColonyCount + sporeColonyCount + sunkenColonyCount;
    const bool NeedAdditionalSpore = NeedSpore &&
        sporeColonyCount < completedBases && creepColonyCount == 0;
    if (poolCount >= 1 &&
        (existingColonies < desiredColonies || NeedAdditionalSpore) &&
        Res.Minerals >= 75)
    {
        const int Drone = FindZergBuilderFor(EM, CreepColonyDefID);
        if (Drone >= 0)
            StartZergBuildingNear(EM, Drone, CreepColonyDefID,
                Core::Math::Vec2(mAiBasePos.X + 120.0f,
                                 mAiBasePos.Y + 160.0f));
    }
    for (int BuildingID : EM.GetEntitiesWith<BuildingComponent,
                                              PlayerComponent>())
    {
        auto* Building = EM.GetComponent<BuildingComponent>(BuildingID);
        auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
        if (!Building || !Player || Player->PlayerID != mAIPlayerID ||
            Building->UnderConstruction ||
            Building->BuildingDefID != CreepColonyDefID)
        {
            continue;
        }
        const int Target = NeedSpore &&
            sporeColonyCount < completedBases
                ? SporeColonyDefID : SunkenColonyDefID;
        if (ZergSystem::StartMorphBuilding(EM, BuildingID, Target))
            break;
    }

    const float nextExpansionTime = Profile.ExpansionStartSeconds +
        (float)std::max(0, startedBases - 1) * Profile.ExpansionIntervalSeconds;
    if (startedBases < Profile.MaxBases &&
        mElapsedSeconds >= nextExpansionTime &&
        mElapsedSeconds - mLastExpansionOrderSeconds >= 60.0f &&
        droneCount >= std::min(workerTarget, 16 + 8 * (startedBases - 1)) &&
        zerglingCount + hydraCount + lurkerCount >=
            std::max(6, Profile.AttackWaveSize[1] / 2) &&
        Res.Minerals >= 300)
    {
        mExpansionPos = FindExpansionLocation(EM);
        const int Drone = FindZergBuilderFor(EM, HatcheryDefID);
        if (Drone >= 0 && StartZergBuildingNear(
                EM, Drone, HatcheryDefID, mExpansionPos))
        {
            mLastExpansionOrderSeconds = mElapsedSeconds;
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " expansion ordered race=Zerg base_target=" +
                std::to_string(startedBases + 1) + " difficulty=" +
                std::string(AIDifficultyName(mDifficulty)));
        }
    }

    ManageGas(EM, ExtractorDefID);
    ManageEconomy(EM);

    int totalCombat = zerglingCount + hydraCount + lurkerCount + mutaCount +
        scourgeCount + ultraCount + defilerCount;
    const int waveSize = Profile.AttackWaveSize[1];
    const int earlyWaveSize = std::min(waveSize, std::max(3, waveSize / 4));
    const int effectiveWaveSize = (mElapsedSeconds >= 60.0f && totalCombat >= earlyWaveSize)
        ? earlyWaveSize : waveSize;
    if (!mDefendingBase &&
        (totalCombat >= effectiveWaveSize || mElapsedSeconds >= 1800.0f))
        SendAttackWave(EM, effectiveWaveSize);
}

void AIBrain::RunProtossAI(Core::ECS::EntityManager& EM)
{
    int ProbeDefID = Game::Races::GetUnitDefID("Probe");
    int ZealotDefID = Game::Races::GetUnitDefID("Zealot");
    int DragoonDefID = Game::Races::GetUnitDefID("Dragoon");
    int DarkTemplarDefID = Game::Races::GetUnitDefID("Dark Templar");
    int HighTemplarDefID = Game::Races::GetUnitDefID("High Templar");
    int ObserverDefID = Game::Races::GetUnitDefID("Observer");
    int ScoutDefID = Game::Races::GetUnitDefID("Scout");
    int CorsairDefID = Game::Races::GetUnitDefID("Corsair");
    int CarrierDefID = Game::Races::GetUnitDefID("Carrier");
    int NexusDefID = Game::Races::GetUnitDefID("Nexus");
    int PylonDefID = Game::Races::GetUnitDefID("Pylon");
    int GatewayDefID = Game::Races::GetUnitDefID("Gateway");
    int CyberneticsCoreDefID = Game::Races::GetUnitDefID("Cybernetics Core");
    int CitadelOfAdunDefID = Game::Races::GetUnitDefID("Citadel of Adun");
    int TemplarArchivesDefID = Game::Races::GetUnitDefID("Templar Archives");
    int RoboticsFacilityDefID = Game::Races::GetUnitDefID("Robotics Facility");
    int ObservatoryDefID = Game::Races::GetUnitDefID("Observatory");
    int StargateDefID = Game::Races::GetUnitDefID("Stargate");
    int FleetBeaconDefID = Game::Races::GetUnitDefID("Fleet Beacon");
    int ForgeDefID = Game::Races::GetUnitDefID("Forge");
    int PhotonCannonDefID = Game::Races::GetUnitDefID("Photon Cannon");
    int AssimilatorDefID = Game::Races::GetUnitDefID("Assimilator");

    const auto& Profile = GetAIDifficultyProfile(mDifficulty);
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);
    const int completedBases = std::max(1, CountOwnedResourceDepots(EM, false));
    const int startedBases = CountOwnedResourceDepots(EM, true);
    const int workerTarget = CalculateAIWorkerTarget(Profile, completedBases);
    int probeCount = CountByDefID(EM, mAIPlayerID, ProbeDefID);
    int pylonCount = CountByDefID(EM, mAIPlayerID, PylonDefID);
    int pylonStartedCount = CountByDefID(EM, mAIPlayerID, PylonDefID, true);
    int gatewayCount = CountByDefID(EM, mAIPlayerID, GatewayDefID);
    int gatewayStartedCount = CountByDefID(EM, mAIPlayerID, GatewayDefID, true);
    int coreCount = CountByDefID(EM, mAIPlayerID, CyberneticsCoreDefID);
    int coreStartedCount = CountByDefID(
        EM, mAIPlayerID, CyberneticsCoreDefID, true);
    int stargateCount = CountByDefID(EM, mAIPlayerID, StargateDefID);
    int zealotCount = CountByDefID(EM, mAIPlayerID, ZealotDefID);
    int dragoonCount = CountByDefID(EM, mAIPlayerID, DragoonDefID);
    int darkTemplarCount = CountByDefID(EM, mAIPlayerID, DarkTemplarDefID);
    int highTemplarCount = CountByDefID(EM, mAIPlayerID, HighTemplarDefID);
    int observerCount = CountByDefID(EM, mAIPlayerID, ObserverDefID);
    int scoutCount = CountByDefID(EM, mAIPlayerID, ScoutDefID);
    int corsairCount = CountByDefID(EM, mAIPlayerID, CorsairDefID);
    int carrierCount = CountByDefID(EM, mAIPlayerID, CarrierDefID);

    int NearbyCompletedPylons = 0;
    int NearbyStartedPylons = 0;
    for (int PylonID : EM.GetEntitiesWith<BuildingComponent, PlayerComponent,
                                           TransformComponent>())
    {
        auto* Building = EM.GetComponent<BuildingComponent>(PylonID);
        auto* Player = EM.GetComponent<PlayerComponent>(PylonID);
        auto* Position = EM.GetComponent<TransformComponent>(PylonID);
        if (!Building || !Player || !Position ||
            Player->PlayerID != mAIPlayerID ||
            ResolveOwnedDefID(EM, PylonID) != PylonDefID ||
            Position->Position.DistanceTo(mAiBasePos) > 320.0f)
        {
            continue;
        }
        ++NearbyStartedPylons;
        if (!Building->UnderConstruction)
            ++NearbyCompletedPylons;
    }
    int assimilatorStartedCount = CountByDefID(
        EM, mAIPlayerID, AssimilatorDefID, true);

    // Stage progression
    if (mStage < 1 && gatewayCount >= 1 && zealotCount >= 4) mStage = 1;
    if (Profile.MaxTechStage >= 2 && mStage < 2 &&
        coreCount >= 1 && dragoonCount >= 4)
        mStage = 2;
    if (Profile.MaxTechStage >= 3 && mStage < 3 &&
        stargateCount >= 1 && scoutCount >= 2)
        mStage = 3;
    mStage = std::min(mStage, Profile.MaxTechStage);

    // Probes from Nexus
    TrainWorkerIfNeeded(EM, NexusDefID, ProbeDefID, workerTarget);

    // Pylons for power and supply
    const bool NeedsLocalPower = NearbyCompletedPylons < 1;
    const bool NeedsSupply =
        Res.SupplyMax < 200 &&
        Res.SupplyUsed + Profile.SupplyBuffer >= Res.SupplyMax;
    const int maxConcurrentPylons = Res.SupplyUsed >= 70 ? 3 : (Res.SupplyUsed >= 40 ? 2 : 1);
    if (probeCount >= 6 && (NeedsLocalPower || NeedsSupply) &&
        ((NeedsLocalPower && NearbyStartedPylons == 0) ||
         (!NeedsLocalPower && pylonStartedCount < pylonCount + maxConcurrentPylons)) &&
        Res.Minerals >= 100)
    {
        int probe = FindProtossBuilderFor(EM, PylonDefID);
        if (probe >= 0)
        {
            static const Core::Math::Vec2 PylonOffsets[] = {
                {80.0f, 80.0f}, {-80.0f, 80.0f}, {80.0f, -80.0f}, {-80.0f, -80.0f},
                {120.0f, 0.0f}, {-120.0f, 0.0f}, {0.0f, 120.0f}, {0.0f, -120.0f},
                {140.0f, 80.0f}, {-140.0f, 80.0f}, {140.0f, -80.0f}, {-140.0f, -80.0f}
            };
            const int idx = NearbyStartedPylons % (sizeof(PylonOffsets) / sizeof(PylonOffsets[0]));
            StartBuildingNear(EM, probe, PylonDefID,
                Core::Math::Vec2(mAiBasePos.X + PylonOffsets[idx].X,
                                 mAiBasePos.Y + PylonOffsets[idx].Y));
        }
    }

    const int desiredAssimilators = std::min(
        completedBases,
        mDifficulty == AIDifficulty::Easy ? 1 :
            (mDifficulty == AIDifficulty::Medium ? 2 : 3));
    const bool NeedAssimilator = probeCount >= 8 &&
        assimilatorStartedCount < desiredAssimilators;
    const int GasMineralReserve = (NeedAssimilator && zealotCount >= 2) ? 100 : 0;
    if (NeedAssimilator && Res.Minerals >= 100)
    {
        const int Probe = FindProtossBuilderFor(EM, AssimilatorDefID);
        if (Probe >= 0)
            StartGasBuilding(EM, Probe, AssimilatorDefID);
    }

    // Gateways and Zealots stay active while the higher-tech branches grow.
    const int desiredGateways = mDifficulty == AIDifficulty::Easy ? 2 :
        (mDifficulty == AIDifficulty::Medium ? 3 : std::min(5, 2 + completedBases));
    if (gatewayStartedCount < desiredGateways && pylonCount >= 1 &&
        (gatewayStartedCount == 0 || zealotCount >= 2) &&
        Res.Minerals >= 150 + GasMineralReserve)
    {
        const int Probe = FindProtossBuilderFor(EM, GatewayDefID);
        if (Probe >= 0)
            StartBuildingNear(EM, Probe, GatewayDefID,
                Core::Math::Vec2(mAiBasePos.X + 100.0f,
                                 mAiBasePos.Y + 80.0f * (float)(gatewayCount + 1)));
    }
    const int zealotTarget = mDifficulty == AIDifficulty::Easy ? 8 :
        (mDifficulty == AIDifficulty::Medium ? 12 : 16);
    if (zealotCount + CountQueuedByDefID(EM, mAIPlayerID, ZealotDefID) <
        zealotTarget && Res.Minerals >= 100 + GasMineralReserve)
        TrainUnitIfPossible(EM, ZealotDefID);

    // Stage 1: Cybernetics Core + Dragoons + Citadel
    if (mStage >= 1)
    {
        if (coreStartedCount < 1 && gatewayCount >= 1 && Res.Minerals >= 200)
        {
            int probe = FindProtossBuilderFor(EM, CyberneticsCoreDefID);
            if (probe >= 0)
                StartBuildingNear(EM, probe, CyberneticsCoreDefID,
                    Core::Math::Vec2(mAiBasePos.X - 100.0f, mAiBasePos.Y));
        }
        const int dragoonTarget = mDifficulty == AIDifficulty::Easy ? 4 :
            (mDifficulty == AIDifficulty::Medium ? 8 :
                (mEnemyIntel.HasSeenAir ? 16 : 12));
        if (dragoonCount + CountQueuedByDefID(EM, mAIPlayerID, DragoonDefID) <
                dragoonTarget &&
            coreCount >= 1)
            TrainUnitIfPossible(EM, DragoonDefID);

        if (Profile.MaxTechStage >= 2 &&
            CountByDefID(EM, mAIPlayerID, CitadelOfAdunDefID, true) < 1 &&
            coreCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 100)
        {
            int probe = FindProtossBuilderFor(EM, CitadelOfAdunDefID);
            if (probe >= 0)
                StartBuildingNear(EM, probe, CitadelOfAdunDefID,
                    Core::Math::Vec2(mAiBasePos.X - 140.0f, mAiBasePos.Y));
        }
    }

    // Stage 2: Stargate + Scouts + Robotics
    if (mStage >= 2)
    {
        if (CountByDefID(EM, mAIPlayerID, TemplarArchivesDefID, true) < 1 &&
            CountByDefID(EM, mAIPlayerID, CitadelOfAdunDefID) >= 1 &&
            Res.Minerals >= 150 && Res.VespeneGas >= 200)
        {
            const int Probe = FindProtossBuilderFor(EM, TemplarArchivesDefID);
            if (Probe >= 0)
                StartBuildingNear(EM, Probe, TemplarArchivesDefID,
                    Core::Math::Vec2(mAiBasePos.X - 180.0f,
                                     mAiBasePos.Y - 80.0f));
        }
        const int darkTemplarTarget = !mEnemyIntel.HasSeenDetector
            ? (mDifficulty == AIDifficulty::Hard ? 3 : 2) : 0;
        if (darkTemplarCount + CountQueuedByDefID(
                EM, mAIPlayerID, DarkTemplarDefID) < darkTemplarTarget)
            TrainUnitIfPossible(EM, DarkTemplarDefID);
        const int highTemplarTarget = mDifficulty == AIDifficulty::Hard ? 4 : 2;
        if (highTemplarCount + CountQueuedByDefID(
                EM, mAIPlayerID, HighTemplarDefID) < highTemplarTarget)
            TrainUnitIfPossible(EM, HighTemplarDefID);

        // Morph Archons from low-energy or surplus High Templars
        std::vector<int> ReadyTemplars;
        for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<UnitComponent>(Eid);
            auto* Player = EM.GetComponent<PlayerComponent>(Eid);
            auto* Warp = EM.GetComponent<ArchonWarpComponent>(Eid);
            if (Unit && Player && Player->PlayerID == mAIPlayerID &&
                Unit->ChkUnitID == 67 && Unit->HitPoints > 0 && !Warp)
            {
                const bool HasPsionicStorm = UpgradeSystem::HasResearch(mAIPlayerID, ResearchId::Protoss_PsionicStorm);
                if (HasPsionicStorm)
                {
                    if ((Unit->EnergyPoints < 25.0f && Unit->HitPoints < 40) ||
                        (highTemplarCount > highTemplarTarget && Unit->EnergyPoints < 40.0f))
                    {
                        ReadyTemplars.push_back(Eid);
                    }
                }
                else
                {
                    if (highTemplarCount >= 2)
                        ReadyTemplars.push_back(Eid);
                }
            }
        }
        for (size_t i = 1; i < ReadyTemplars.size(); i += 2)
        {
            ProtossSystem::StartArchonWarp(EM, ReadyTemplars[i - 1], ReadyTemplars[i], false);
        }

        if (CountByDefID(EM, mAIPlayerID, RoboticsFacilityDefID, true) < 1 &&
            coreCount >= 1 && Res.Minerals >= 200 && Res.VespeneGas >= 200)
        {
            const int Probe = FindProtossBuilderFor(EM, RoboticsFacilityDefID);
            if (Probe >= 0)
                StartBuildingNear(EM, Probe, RoboticsFacilityDefID,
                    Core::Math::Vec2(mAiBasePos.X - 220.0f, mAiBasePos.Y));
        }
        if (CountByDefID(EM, mAIPlayerID, RoboticsFacilityDefID) >= 1 &&
            CountByDefID(EM, mAIPlayerID, ObservatoryDefID, true) < 1 &&
            Res.Minerals >= 50 && Res.VespeneGas >= 100)
        {
            const int Probe = FindProtossBuilderFor(EM, ObservatoryDefID);
            if (Probe >= 0)
                StartBuildingNear(EM, Probe, ObservatoryDefID,
                    Core::Math::Vec2(mAiBasePos.X - 260.0f, mAiBasePos.Y));
        }
        const int observerTarget = mEnemyIntel.HasSeenCloak
            ? (mDifficulty == AIDifficulty::Hard ? 3 : 2) : 1;
        if (observerCount + CountQueuedByDefID(
                EM, mAIPlayerID, ObserverDefID) < observerTarget)
            TrainUnitIfPossible(EM, ObserverDefID);

        const int desiredStargates = mDifficulty == AIDifficulty::Hard ? 2 : 1;
        if (CountByDefID(EM, mAIPlayerID, StargateDefID, true) <
                desiredStargates &&
            coreCount >= 1 && Res.Minerals >= 150 && Res.VespeneGas >= 150)
        {
            int probe = FindProtossBuilderFor(EM, StargateDefID);
            if (probe >= 0)
                StartBuildingNear(EM, probe, StargateDefID,
                    Core::Math::Vec2(mAiBasePos.X - 180.0f, mAiBasePos.Y + 80.0f));
        }
        const int scoutTarget = mDifficulty == AIDifficulty::Hard ? 4 : 2;
        if (scoutCount + CountQueuedByDefID(EM, mAIPlayerID, ScoutDefID) <
                scoutTarget &&
            stargateCount >= 1)
            TrainUnitIfPossible(EM, ScoutDefID);

        if (Profile.MaxTechStage >= 3 &&
            CountByDefID(EM, mAIPlayerID, FleetBeaconDefID, true) < 1 &&
            stargateCount >= 1 &&
            Res.Minerals >= 300 && Res.VespeneGas >= 200)
        {
            int probe = FindProtossBuilderFor(EM, FleetBeaconDefID);
            if (probe >= 0)
                StartBuildingNear(EM, probe, FleetBeaconDefID,
                    Core::Math::Vec2(mAiBasePos.X - 220.0f, mAiBasePos.Y));
        }

        const int corsairTarget =
            mDifficulty == AIDifficulty::Hard && mEnemyIntel.HasSeenAir ? 4 : 0;
        if (corsairCount + CountQueuedByDefID(
                EM, mAIPlayerID, CorsairDefID) < corsairTarget)
            TrainUnitIfPossible(EM, CorsairDefID);
    }

    // Stage 3: Carriers + Robotics
    if (mStage >= 3)
    {
        const int carrierTarget = mEnemyIntel.HasSeenAir ? 1 : 3;
        if (carrierCount + CountQueuedByDefID(EM, mAIPlayerID, CarrierDefID) <
                carrierTarget &&
            CountByDefID(EM, mAIPlayerID, FleetBeaconDefID) >= 1)
            TrainUnitIfPossible(EM, CarrierDefID);
    }

    if (CountByDefID(EM, mAIPlayerID, ForgeDefID, true) < 1 &&
        gatewayCount >= 1 &&
        (mDifficulty != AIDifficulty::Easy || mElapsedSeconds >= 600.0f) &&
        Res.Minerals >= 150)
    {
        const int Probe = FindProtossBuilderFor(EM, ForgeDefID);
        if (Probe >= 0)
            StartBuildingNear(EM, Probe, ForgeDefID,
                Core::Math::Vec2(mAiBasePos.X + 180.0f, mAiBasePos.Y - 80.0f));
    }
    const int desiredCannons = mDifficulty == AIDifficulty::Easy ? 1 :
        (mDifficulty == AIDifficulty::Medium ? completedBases : completedBases * 2);
    if (CountByDefID(EM, mAIPlayerID, PhotonCannonDefID, true) < desiredCannons &&
        CountByDefID(EM, mAIPlayerID, ForgeDefID) >= 1 && Res.Minerals >= 100)
    {
        const int Probe = FindProtossBuilderFor(EM, PhotonCannonDefID);
        if (Probe >= 0)
            StartBuildingNear(EM, Probe, PhotonCannonDefID,
                Core::Math::Vec2(mAiBasePos.X + 100.0f,
                                 mAiBasePos.Y + 160.0f));
    }

    const float nextExpansionTime = Profile.ExpansionStartSeconds +
        (float)std::max(0, startedBases - 1) * Profile.ExpansionIntervalSeconds;
    if (startedBases < Profile.MaxBases &&
        mElapsedSeconds >= nextExpansionTime &&
        mElapsedSeconds - mLastExpansionOrderSeconds >= 60.0f &&
        probeCount >= std::min(workerTarget, 16 + 8 * (startedBases - 1)) &&
        zealotCount + dragoonCount >=
            std::max(6, Profile.AttackWaveSize[2] / 2) &&
        Res.Minerals >= 400)
    {
        mExpansionPos = FindExpansionLocation(EM);
        const int Probe = FindProtossBuilderFor(EM, NexusDefID);
        if (Probe >= 0 &&
            StartBuildingNear(EM, Probe, NexusDefID, mExpansionPos))
        {
            mLastExpansionOrderSeconds = mElapsedSeconds;
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " expansion ordered race=Protoss base_target=" +
                std::to_string(startedBases + 1) + " difficulty=" +
                std::string(AIDifficultyName(mDifficulty)));
        }
    }

    // Hard Protoss keeps its Gateways productive after all fixed composition
    // targets are met. This turns mineral-heavy, gas-starved economies into a
    // replenishing Zealot front line instead of banking five figures.
    const auto& ZealotDef = Game::Races::GetUnitDefByID(ZealotDefID);
    if (AIShouldSpendMineralOverflow(
            mDifficulty, Res.Minerals, Res.SupplyUsed, Res.SupplyMax,
            ZealotDef.SupplyCost))
    {
        TrainUnitIfPossible(EM, ZealotDefID);
    }

    ManageGas(EM, AssimilatorDefID);
    ManageEconomy(EM);

    int totalCombat = zealotCount + dragoonCount + darkTemplarCount +
        highTemplarCount + observerCount + scoutCount + corsairCount + carrierCount;
    const int waveSize = Profile.AttackWaveSize[2];
    const int earlyWaveSize = std::min(waveSize, std::max(3, waveSize / 4));
    const int effectiveWaveSize = (mElapsedSeconds >= 60.0f && totalCombat >= earlyWaveSize)
        ? earlyWaveSize : waveSize;
    if (!mDefendingBase &&
        (totalCombat >= effectiveWaveSize || mElapsedSeconds >= 1800.0f))
        SendAttackWave(EM, effectiveWaveSize);
}

void AIBrain::ManageEconomy(Core::ECS::EntityManager& EM)
{
    std::vector<int> ActiveMinerals;
    auto Minerals = EM.GetEntitiesWith<TransformComponent, ResourceComponent>();
    for (int Mid : Minerals)
    {
        auto* Res = EM.GetComponent<ResourceComponent>(Mid);
        auto* Pos = EM.GetComponent<TransformComponent>(Mid);
        if (Res && Pos && Res->Type == ResourceType::Minerals &&
            !Res->IsDepleted && Res->Amount > 0)
        {
            const float DistToDepot = DistanceToNearestOwnedDepot(EM, mAIPlayerID, Pos->Position);
            if (DistToDepot <= 450.0f)
                ActiveMinerals.push_back(Mid);
        }
    }
    if (ActiveMinerals.empty())
    {
        for (int Mid : Minerals)
        {
            auto* Res = EM.GetComponent<ResourceComponent>(Mid);
            if (Res && Res->Type == ResourceType::Minerals &&
                !Res->IsDepleted && Res->Amount > 0)
            {
                ActiveMinerals.push_back(Mid);
            }
        }
    }
    if (ActiveMinerals.empty()) return;

    const int RaceVal = GetRace(EM);
    const int WorkerChkIDs[] = {0x07, 0x29, 0x40}; // SCV, Drone, Probe
    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (!U || !P || U->HitPoints <= 0) continue;
        if (P->PlayerID != mAIPlayerID) continue;
        if (RaceVal < 0 || RaceVal > 2 || U->ChkUnitID != WorkerChkIDs[RaceVal]) continue;
        if (Eid == mScoutUnitID) continue;
        if (IsAssignedToConstruction(EM, Eid)) continue;
        if (IsWorkerRepairing(EM, Eid)) continue;
        auto* H = EM.GetComponent<HarvestComponent>(Eid);
        if (!H)
            H = &EM.AddComponent<HarvestComponent>(Eid);
        // Never steal a worker from a live owned gas building.
        const int AssignedResourceID = H->DesiredResourceID >= 0
            ? H->DesiredResourceID : H->TargetResourceID;
        auto* CurrentResource = EM.GetComponent<ResourceComponent>(AssignedResourceID);
        if (CurrentResource && CurrentResource->Type == ResourceType::VespeneGas)
        {
            const auto* GasPlayer =
                EM.GetComponent<PlayerComponent>(AssignedResourceID);
            const auto* GasUnit = EM.GetComponent<UnitComponent>(AssignedResourceID);
            const auto* GasBldg = EM.GetComponent<BuildingComponent>(AssignedResourceID);
            if (GasPlayer && GasUnit && GasPlayer->PlayerID == mAIPlayerID &&
                GasUnit->HitPoints > 0 && (!GasBldg || GasBldg->BuildingDefID >= 0))
            {
                continue;
            }
        }

        const auto* WorkerTrans = EM.GetComponent<TransformComponent>(Eid);
        const auto* ResourceTrans = EM.GetComponent<TransformComponent>(AssignedResourceID);
        const auto* Path = EM.GetComponent<PathfindingComponent>(Eid);
        const bool WorkerSettled = !Path || Path->Path.empty() ||
            Path->PathIndex >= (int)Path->Path.size();
        const bool StuckOutsideRange = WorkerSettled &&
            H->State != HarvestState::Mining &&
            H->State != HarvestState::Depositing &&
            (!WorkerTrans || !ResourceTrans ||
             WorkerTrans->Position.DistanceTo(ResourceTrans->Position) > 64.0f);

        const bool NeedsAssignment = H->State == HarvestState::Idle ||
            !CurrentResource || CurrentResource->Type != ResourceType::Minerals ||
            CurrentResource->IsDepleted || CurrentResource->Amount <= 0 ||
            StuckOutsideRange;
        if (!NeedsAssignment)
            continue;

        int BestMineral = -1;
        float BestScore = std::numeric_limits<float>::max();
        const auto* WorkerPos = EM.GetComponent<TransformComponent>(Eid);
        for (int MineralID : ActiveMinerals)
        {
            auto* Resource = EM.GetComponent<ResourceComponent>(MineralID);
            auto* MineralPos = EM.GetComponent<TransformComponent>(MineralID);
            if (!Resource || !MineralPos)
                continue;
            const size_t Load = Resource->QueuedGatherers.size() +
                (Resource->ActiveGathererEntityID >= 0 ? 1u : 0u);
            const float Distance = WorkerPos
                ? WorkerPos->Position.DistanceTo(MineralPos->Position) : 0.0f;
            // Distance is deliberately part of the primary score.  Comparing
            // load first made a worker cross the whole map for one unused
            // patch even when its local mineral line still had capacity.
            const float Score = Distance + (float)Load * 112.0f;
            if (Score < BestScore)
            {
                BestScore = Score;
                BestMineral = MineralID;
            }
        }
        if (BestMineral >= 0)
            HarvestSystem::IssueHarvest(EM, {Eid}, BestMineral);
    }
}

void AIBrain::ManageMilitary(Core::ECS::EntityManager& EM)
{
    RefreshEnemyIntel(EM);
    ManageScouting(EM);
    ManageCombatAbilities(EM);
    if (GetRace(EM) == (int)Race::Terran)
    {
        ManageTerranMedics(EM);
    }
    ManageMilitaryPatrol(EM);
}

void AIBrain::ManageMilitaryPatrol(Core::ECS::EntityManager& EM)
{
    if (mShowcaseMode || mDefendingBase)
        return;

    const bool HasActiveAttack = !mActiveAttackGroup.empty();

    std::vector<int> IdleCombatUnits;
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                           TransformComponent, CombatComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Position = EM.GetComponent<TransformComponent>(EntityID);
        auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
        if (!Unit || !Player || !Position || !Combat || Unit->HitPoints <= 0 ||
            Player->PlayerID != mAIPlayerID || IsWorkerUnit(*Unit) ||
            EM.GetComponent<BuildingComponent>(EntityID) ||
            !IsCombatUnit(*Unit, *Combat))
        {
            continue;
        }

        if (std::find(mActiveAttackGroup.begin(), mActiveAttackGroup.end(), EntityID) != mActiveAttackGroup.end() ||
            std::find(mActiveDefenseGroup.begin(), mActiveDefenseGroup.end(), EntityID) != mActiveDefenseGroup.end())
        {
            continue;
        }

        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* Order = Command ? Command->GetCurrentOrder() : nullptr;
        const bool IsIdle = !Order || Order->Type == OrderType::None ||
            Order->Type == OrderType::Stop;
        const float DistToBase = Position->Position.DistanceTo(mAiBasePos);
        if (IsIdle && DistToBase <= 450.0f)
        {
            IdleCombatUnits.push_back(EntityID);
        }
    }

    if (IdleCombatUnits.empty())
        return;

    const auto& Profile = GetAIDifficultyProfile(mDifficulty);
    if (Profile.DefenderReservePercent > 0 && !HasActiveAttack)
    {
        const size_t Reserve = (IdleCombatUnits.size() *
            (size_t)Profile.DefenderReservePercent) / 100u;
        if (Reserve >= IdleCombatUnits.size())
            return;
        IdleCombatUnits.resize(IdleCombatUnits.size() - Reserve);
    }

    constexpr size_t kMaxSimultaneousPatrolOrders = 8;
    if (IdleCombatUnits.size() > kMaxSimultaneousPatrolOrders)
    {
        IdleCombatUnits.resize(kMaxSimultaneousPatrolOrders);
    }

    if (HasActiveAttack)
    {
        CombatSystem::IssueAttackMove(EM, IdleCombatUnits, mEnemyBasePos);
    }
    else if (mElapsedSeconds >= 45.0f && !IdleCombatUnits.empty())
    {
        const Core::Math::Vec2 PatrolTarget = FindEnemyBaseLocation(EM, false);
        CombatSystem::IssueAttackMove(EM, IdleCombatUnits, PatrolTarget);
    }
}

void AIBrain::RefreshEnemyIntel(Core::ECS::EntityManager& EM)
{
    const bool PreviouslySawAir = mEnemyIntel.HasSeenAir;
    const bool PreviouslySawCloak = mEnemyIntel.HasSeenCloak;
    const bool PreviouslySawDetector = mEnemyIntel.HasSeenDetector;
    mEnemyIntel.VisibleGroundCombat = 0;
    mEnemyIntel.VisibleAirCombat = 0;
    mEnemyIntel.VisibleWorkers = 0;
    mEnemyIntel.VisibleBuildings = 0;
    mEnemyIntel.VisibleDetectors = 0;
    mEnemyIntel.VisibleCloaked = 0;

    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Position = EM.GetComponent<TransformComponent>(EntityID);
        if (!Unit || !Player || !Position || Unit->HitPoints <= 0 ||
            Player->PlayerID != mTargetPlayerID || Player->IsRescuePassive ||
            IsUnclaimedResourceEntity(EM, EntityID) ||
            !IsVisibleToAI(Position->Position, EntityID, EM))
        {
            continue;
        }

        mEnemyIntel.KnownRace = (int)Unit->UnitRace;
        mEnemyIntel.LastSeenPosition = Position->Position;
        mEnemyIntel.LastSeenSeconds = mElapsedSeconds;
        if (EM.GetComponent<BuildingComponent>(EntityID))
            ++mEnemyIntel.VisibleBuildings;
        if (IsWorkerUnit(*Unit))
            ++mEnemyIntel.VisibleWorkers;
        if (Unit->IsDetector)
        {
            ++mEnemyIntel.VisibleDetectors;
            mEnemyIntel.HasSeenDetector = true;
        }
        if (const auto* Cloak = EM.GetComponent<CloakComponent>(EntityID);
            Cloak && (Cloak->IsCloaked || Cloak->IsPassiveCloak ||
                      Cloak->IsBurrowed))
        {
            ++mEnemyIntel.VisibleCloaked;
            mEnemyIntel.HasSeenCloak = true;
        }

        const auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
        if (!Combat || !IsCombatUnit(*Unit, *Combat))
            continue;
        const auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
        if (Path && Path->IsAir)
        {
            ++mEnemyIntel.VisibleAirCombat;
            mEnemyIntel.HasSeenAir = true;
        }
        else
        {
            ++mEnemyIntel.VisibleGroundCombat;
        }
    }

    if ((!PreviouslySawAir && mEnemyIntel.HasSeenAir) ||
        (!PreviouslySawCloak && mEnemyIntel.HasSeenCloak) ||
        (!PreviouslySawDetector && mEnemyIntel.HasSeenDetector))
    {
        Core::Utils::Logger::Info(
            "AIBrain", "Player " + std::to_string(mAIPlayerID) +
            " intel air=" + std::to_string(mEnemyIntel.HasSeenAir ? 1 : 0) +
            " cloak=" + std::to_string(mEnemyIntel.HasSeenCloak ? 1 : 0) +
            " detector=" +
                std::to_string(mEnemyIntel.HasSeenDetector ? 1 : 0));
    }
}

void AIBrain::ManageScouting(Core::ECS::EntityManager& EM)
{
    if (mShowcaseMode)
        return;
    const auto& Profile = GetAIDifficultyProfile(mDifficulty);
    const bool OpeningDue = !mOpeningScoutSent &&
        mElapsedSeconds >= Profile.FirstScoutSeconds;
    const bool RepeatDue = mOpeningScoutSent && Profile.ScoutIntervalSeconds > 0.0f &&
        mElapsedSeconds - mLastScoutOrderSeconds >= Profile.ScoutIntervalSeconds;

    if (mScoutUnitID >= 0 && EM.IsAlive(mScoutUnitID))
    {
        const auto* Command = EM.GetComponent<CommandComponent>(mScoutUnitID);
        const auto* Order = Command ? Command->GetCurrentOrder() : nullptr;
        if (Order && (Order->Type == OrderType::Move ||
                       Order->Type == OrderType::AttackMove))
        {
            return;
        }
        // The scout reached its destination or was interrupted. Release the
        // worker back to ManageEconomy until the next scheduled information run.
        mScoutUnitID = -1;
    }
    else if (mScoutUnitID >= 0)
    {
        mScoutUnitID = -1;
    }

    if (!OpeningDue && !RepeatDue)
        return;

    int ScoutID = -1;
    float BestDistance = std::numeric_limits<float>::max();
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Position = EM.GetComponent<TransformComponent>(EntityID);
        if (!Unit || !Player || !Position || Unit->HitPoints <= 0 ||
            Player->PlayerID != mAIPlayerID || !IsWorkerUnit(*Unit) ||
            IsAssignedToConstruction(EM, EntityID) ||
            IsWorkerRepairing(EM, EntityID))
        {
            continue;
        }
        const float Distance = Position->Position.DistanceTo(mAiBasePos);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            ScoutID = EntityID;
        }
    }
    if (ScoutID < 0)
        return;

    mEnemyBasePos = FindEnemyBaseLocation(EM, true);
    CommandSystem::InterruptHarvest(EM, ScoutID);
    Order ScoutOrder;
    ScoutOrder.Type = OrderType::Move;
    ScoutOrder.TargetPosition = mEnemyBasePos;
    CommandSystem::IssueOrder(EM, ScoutID, ScoutOrder, false);
    mScoutUnitID = ScoutID;
    mOpeningScoutSent = true;
    mLastScoutOrderSeconds = mElapsedSeconds;
    Core::Utils::Logger::Info(
        "AIBrain", "Player " + std::to_string(mAIPlayerID) +
        " scout dispatched unit=" + std::to_string(ScoutID) +
        " target=" + std::to_string((int)mEnemyBasePos.X) + "," +
        std::to_string((int)mEnemyBasePos.Y) + " difficulty=" +
        std::string(AIDifficultyName(mDifficulty)));
}

void AIBrain::ManageUpgrades(Core::ECS::EntityManager& EM, int RaceValue)
{
    const float StartSeconds = mDifficulty == AIDifficulty::Easy
        ? 600.0f : (mDifficulty == AIDifficulty::Medium ? 300.0f : 180.0f);
    if (mElapsedSeconds < StartSeconds)
        return;
    const int MaxLevel = mDifficulty == AIDifficulty::Easy
        ? 1 : (mDifficulty == AIDifficulty::Medium ? 2 : 3);

    if (RaceValue == (int)Race::Terran)
    {
        TryStartResearch(EM, {
            (int)ResearchId::Terran_StimPacks,
            (int)ResearchId::Terran_U238Shells,
            (int)ResearchId::Terran_SiegeMode,
            (int)ResearchId::Terran_InfantryWeapons,
            (int)ResearchId::Terran_InfantryArmor,
            (int)ResearchId::Terran_VehicleWeapons,
            (int)ResearchId::Terran_VehicleArmor,
            (int)ResearchId::Terran_CharonBoosters,
            (int)ResearchId::Terran_ShipWeapons,
            (int)ResearchId::Terran_ShipArmor}, MaxLevel);
    }
    else if (RaceValue == (int)Race::Zerg)
    {
        const int LairDefID = Game::Races::GetUnitDefID("Lair");
        const int HiveDefID = Game::Races::GetUnitDefID("Hive");
        const int HydraliskDenDefID =
            Game::Races::GetUnitDefID("Hydralisk Den");
        const int HydraliskDefID = Game::Races::GetUnitDefID("Hydralisk");
        const bool HasAdvancedBase =
            CountByDefID(EM, mAIPlayerID, LairDefID) > 0 ||
            CountByDefID(EM, mAIPlayerID, HiveDefID) > 0;
        const bool AspectResearched = UpgradeSystem::HasResearch(
            mAIPlayerID, ResearchId::Zerg_LurkerMorph);
        if (AIShouldReserveForLurkerAspect(
                GetAIDifficultyProfile(mDifficulty).MaxTechStage,
                HasAdvancedBase,
                CountByDefID(EM, mAIPlayerID, HydraliskDenDefID) > 0,
                CountByDefID(EM, mAIPlayerID, HydraliskDefID) > 0,
                AspectResearched))
        {
            // Once the branch is operational, stop buying cheaper upgrades
            // until the 200/200 Lurker Aspect can start. Otherwise the generic
            // loop continually spends the gas the Aspect is waiting for.
            TryStartResearch(
                EM, {(int)ResearchId::Zerg_LurkerMorph}, 1);
            return;
        }

        TryStartResearch(EM, {
            (int)ResearchId::Zerg_MetabolicBoost,
            (int)ResearchId::Zerg_AdrenalGlands,
            (int)ResearchId::Zerg_PneumatizedCarapace,
            (int)ResearchId::Zerg_GroovedSpines,
            (int)ResearchId::Zerg_MuscularAugments,
            (int)ResearchId::Zerg_AnabolicSynthesis,
            (int)ResearchId::Zerg_ChitinousPlating,
            (int)ResearchId::Zerg_LurkerMorph,
            (int)ResearchId::Zerg_Consume,
            (int)ResearchId::Zerg_Plague,
            (int)ResearchId::Zerg_MissileAttacks,
            (int)ResearchId::Zerg_Carapace,
            (int)ResearchId::Zerg_MeleeAttacks,
            (int)ResearchId::Zerg_FlyerAttacks,
            (int)ResearchId::Zerg_FlyerCarapace}, MaxLevel);
    }
    else if (RaceValue == (int)Race::Protoss)
    {
        TryStartResearch(EM, {
            (int)ResearchId::Protoss_SingularityCharge,
            (int)ResearchId::Protoss_ZealotSpeed,
            (int)ResearchId::Protoss_GraviticDrive,
            (int)ResearchId::Protoss_GraviticBoosters,
            (int)ResearchId::Protoss_GraviticThrusters,
            (int)ResearchId::Protoss_GroundWeapons,
            (int)ResearchId::Protoss_GroundArmor,
            (int)ResearchId::Protoss_PsionicStorm,
            (int)ResearchId::Protoss_PlasmaShields,
            (int)ResearchId::Protoss_AirWeapons,
            (int)ResearchId::Protoss_AirArmor,
            (int)ResearchId::Protoss_ScarabDamage,
            (int)ResearchId::Protoss_ReaverCapacity,
            (int)ResearchId::Protoss_CarrierCapacity}, MaxLevel);
    }
}

void AIBrain::ManageCombatAbilities(Core::ECS::EntityManager& EM)
{
    if (mDifficulty == AIDifficulty::Easy)
        return;

    std::vector<int> VisibleEnemies;
    std::vector<int> VisibleLurkerGroundThreats;
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Position = EM.GetComponent<TransformComponent>(EntityID);
        if (!Unit || !Player || !Position || Unit->HitPoints <= 0 ||
            IsUnclaimedResourceEntity(EM, EntityID) ||
            !IsVisibleToAI(Position->Position, EntityID, EM))
        {
            continue;
        }

        if (Player->PlayerID == mTargetPlayerID)
            VisibleEnemies.push_back(EntityID);

        const auto* EnemyCombat = EM.GetComponent<CombatComponent>(EntityID);
        const auto* EnemyPath = EM.GetComponent<PathfindingComponent>(EntityID);
        const bool EnemyIsAir =
            (EnemyCombat && EnemyCombat->MyElevation == Elevation::Air) ||
            (EnemyPath && EnemyPath->IsAir);
        const bool EnemyIsAllied =
            mFog && mFog->AreAllied(mAIPlayerID, Player->PlayerID);
        if (!EnemyIsAir && AIShouldConsiderLurkerThreatPlayer(
                mAIPlayerID, Player->PlayerID,
                Player->IsRescuePassive, EnemyIsAllied))
        {
            VisibleLurkerGroundThreats.push_back(EntityID);
        }
    }
    int ClusterAnchor = VisibleEnemies.empty() ? -1 : VisibleEnemies.front();
    int ClusterSize = VisibleEnemies.empty() ? 0 : 1;
    for (int CandidateID : VisibleEnemies)
    {
        const auto* Candidate = EM.GetComponent<TransformComponent>(CandidateID);
        if (!Candidate) continue;
        int Count = 0;
        for (int OtherID : VisibleEnemies)
        {
            const auto* Other = EM.GetComponent<TransformComponent>(OtherID);
            if (Other && Other->Position.DistanceTo(Candidate->Position) <= 160.0f)
                ++Count;
        }
        if (Count > ClusterSize)
        {
            ClusterSize = Count;
            ClusterAnchor = CandidateID;
        }
    }
    const auto* TargetPosition = ClusterAnchor >= 0
        ? EM.GetComponent<TransformComponent>(ClusterAnchor) : nullptr;
    if (!TargetPosition)
        mEnemyBasePos = FindEnemyBaseLocation(EM);

    auto FindBestSpellTargetNear = [&](const Core::Math::Vec2& CasterPos, float MaxCastRange, int MinEnemies) -> std::pair<Core::Math::Vec2, bool>
    {
        Core::Math::Vec2 BestTarget;
        int BestScore = -1;
        constexpr float SplashRadius = 96.0f;

        for (int EnemyID : VisibleEnemies)
        {
            const auto* EPos = EM.GetComponent<TransformComponent>(EnemyID);
            if (!EPos) continue;
            const float DistToCaster = CasterPos.DistanceTo(EPos->Position);
            if (DistToCaster > MaxCastRange) continue;

            int EnemyCount = 0;
            for (int OtherID : VisibleEnemies)
            {
                const auto* OtherPos = EM.GetComponent<TransformComponent>(OtherID);
                if (OtherPos && OtherPos->Position.DistanceTo(EPos->Position) <= SplashRadius)
                    ++EnemyCount;
            }
            if (EnemyCount < MinEnemies) continue;

            int FriendlyCount = 0;
            EM.ForEachEntityWith<UnitComponent, PlayerComponent, TransformComponent>(
                [&](int UnitID)
                {
                    auto* UP = EM.GetComponent<PlayerComponent>(UnitID);
                    auto* UU = EM.GetComponent<UnitComponent>(UnitID);
                    auto* UPos = EM.GetComponent<TransformComponent>(UnitID);
                    if (UP && UU && UPos && UP->PlayerID == mAIPlayerID && UU->HitPoints > 0)
                    {
                        if (UPos->Position.DistanceTo(EPos->Position) <= SplashRadius)
                            ++FriendlyCount;
                    }
                });

            if (FriendlyCount > EnemyCount / 2) continue;

            int Score = EnemyCount * 2 - FriendlyCount * 3;
            if (Score > BestScore)
            {
                BestScore = Score;
                BestTarget = EPos->Position;
            }
        }
        return {BestTarget, BestScore >= 0};
    };

    bool StormCast = false;
    bool EmpCast = false;
    bool PlagueCast = false;
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Position = EM.GetComponent<TransformComponent>(EntityID);
        if (!Unit || !Player || !Position || Unit->HitPoints <= 0 ||
            Player->PlayerID != mAIPlayerID)
        {
            continue;
        }
        const float Distance = TargetPosition
            ? Position->Position.DistanceTo(TargetPosition->Position)
            : std::numeric_limits<float>::max();
        if (Unit->ChkUnitID == 30)
        {
            if (!TargetPosition || Distance > 640.0f)
                CommandSystem::SetSiegeMode(EM, EntityID, false);
            continue;
        }

        const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        if (DefID < 0)
            continue;
        const auto Def = Game::Races::GetUnitDefByID(DefID);

        if ((Def.Name == "Marine" || Def.Name == "Firebat") && Distance <= 240.0f &&
            UpgradeSystem::HasResearch(mAIPlayerID, ResearchId::Terran_StimPacks))
        {
            auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
            if ((!Status || !Status->HasEffect(StatusEffect::Stim)) && Unit->HitPoints > 20)
            {
                Order Stim;
                Stim.Type = OrderType::UseTech;
                Stim.AbilityID = (int)ResearchId::Terran_StimPacks;
                CommandSystem::IssueOrder(EM, EntityID, Stim, false);
            }
        }
        else if (Def.Name == "SiegeTank" && Distance <= 420.0f)
        {
            CommandSystem::SetSiegeMode(EM, EntityID, true);
        }
        else if (Def.Name == "Lurker")
        {
            auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID);
            if (Burrow && !Burrow->IsTransitioning())
            {
                float NearestGroundEnemy = std::numeric_limits<float>::max();
                for (int EnemyID : VisibleLurkerGroundThreats)
                {
                    const auto* EnemyPosition =
                        EM.GetComponent<TransformComponent>(EnemyID);
                    if (EnemyPosition)
                    {
                        NearestGroundEnemy = std::min(
                            NearestGroundEnemy,
                            Position->Position.DistanceTo(EnemyPosition->Position));
                    }
                }

                const float LurkerAttackRange =
                    AILurkerAttackRangePixels(Def.AttackRange);
                if (ShouldAIBurrowLurker(
                        NearestGroundEnemy, Def.AttackRange) &&
                    !Burrow->IsBurrowed)
                {
                    // Start immediately so a same-tick attack-wave refresh
                    // cannot overwrite the pending Burrow order before the
                    // command system observes it.
                    ZergSystem::BurrowUnit(EM, EntityID);
                }
                else if (NearestGroundEnemy > LurkerAttackRange &&
                         Burrow->IsBurrowed)
                {
                    Order UnburrowOrder;
                    UnburrowOrder.Type = OrderType::Unburrow;
                    CommandSystem::IssueOrder(EM, EntityID, UnburrowOrder, false);

                    Order ResumeAdvance;
                    ResumeAdvance.Type = OrderType::AttackMove;
                    ResumeAdvance.TargetPosition = mEnemyBasePos;
                    CommandSystem::IssueOrder(EM, EntityID, ResumeAdvance, true);
                }
            }
        }
        else if (!StormCast && Def.Name == "High Templar" &&
                 UpgradeSystem::HasResearch(
                     mAIPlayerID, ResearchId::Protoss_PsionicStorm))
        {
            auto [SpellTarget, HasTarget] =
                FindBestSpellTargetNear(Position->Position, 320.0f, 3);
            if (HasTarget && SpellSystem::CastSpell(EM, EntityID, SpellType::PsionicStorm,
                                                    -1, SpellTarget))
            {
                StormCast = true;
            }
        }
        else if (!EmpCast && Def.Name == "ScienceVessel")
        {
            auto [SpellTarget, HasTarget] =
                FindBestSpellTargetNear(Position->Position, 320.0f, 2);
            if (HasTarget && SpellSystem::CastSpell(EM, EntityID, SpellType::EMPShockwave,
                                                    -1, SpellTarget))
            {
                EmpCast = true;
            }
        }
        else if (!PlagueCast && Def.Name == "Defiler" &&
                 UpgradeSystem::HasResearch(mAIPlayerID, ResearchId::Zerg_Plague))
        {
            auto [SpellTarget, HasTarget] =
                FindBestSpellTargetNear(Position->Position, 320.0f, 3);
            if (HasTarget && SpellSystem::CastSpell(EM, EntityID, SpellType::Plague,
                                                    -1, SpellTarget))
            {
                PlagueCast = true;
            }
        }
    }
}

bool AIBrain::DefendBaseIfNeeded(Core::ECS::EntityManager& EM)
{
    const auto& Profile = GetAIDifficultyProfile(mDifficulty);
    const auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent,
                                               TransformComponent>();
    const auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                         TransformComponent>();

    std::vector<Core::Math::Vec2> OwnedBuildingPositions;
    for (int BuildingID : Buildings)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
        auto* Position = EM.GetComponent<TransformComponent>(BuildingID);
        auto* Unit = EM.GetComponent<UnitComponent>(BuildingID);
        if (Player && Position && Unit && Player->PlayerID == mAIPlayerID &&
            IsMeleeStructureEntity(EM, BuildingID))
        {
            OwnedBuildingPositions.push_back(Position->Position);
        }
    }

    int ThreatID = -1;
    Core::Math::Vec2 ThreatPosition;
    float ThreatPower = 0.0f;
    float GroundThreatPower = 0.0f;
    float AirThreatPower = 0.0f;
    int ThreatCount = 0;
    float NearestThreatDistance = std::numeric_limits<float>::max();
    for (int UnitID : Units)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(UnitID);
        auto* Player = EM.GetComponent<PlayerComponent>(UnitID);
        auto* Position = EM.GetComponent<TransformComponent>(UnitID);
        auto* Combat = EM.GetComponent<CombatComponent>(UnitID);
        if (!Unit || !Player || !Position || Unit->HitPoints <= 0 ||
            Player->PlayerID == mAIPlayerID || Player->IsRescuePassive ||
            (mFog && mFog->AreAllied(mAIPlayerID, Player->PlayerID)) ||
            IsUnclaimedResourceEntity(EM, UnitID) ||
            (!IsWorkerUnit(*Unit) && (!Combat || !IsCombatUnit(*Unit, *Combat))) ||
            !IsVisibleToAI(Position->Position, UnitID, EM))
        {
            continue;
        }

        float DistanceToBase = std::numeric_limits<float>::max();
        for (const auto& BuildingPosition : OwnedBuildingPositions)
        {
            DistanceToBase = std::min(
                DistanceToBase,
                BuildingPosition.DistanceTo(Position->Position));
        }
        if (DistanceToBase > Profile.ThreatRadius)
            continue;

        ++ThreatCount;
        const float UnitThreatPower =
            Combat ? EstimateCombatPower(*Unit, *Combat) : 1.0f;
        ThreatPower += UnitThreatPower;
        const auto* ThreatPath = EM.GetComponent<PathfindingComponent>(UnitID);
        const bool IsAirThreat =
            (Combat && Combat->MyElevation == Elevation::Air) ||
            (ThreatPath && ThreatPath->IsAir);
        if (IsAirThreat)
            AirThreatPower += UnitThreatPower;
        else
            GroundThreatPower += UnitThreatPower;
        if (DistanceToBase < NearestThreatDistance)
        {
            NearestThreatDistance = DistanceToBase;
            ThreatID = UnitID;
            ThreatPosition = Position->Position;
        }
    }

    if (ThreatID < 0)
    {
        if (mDefendingBase)
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " base defense cleared");
        mActiveDefenseGroup.clear();
        mHasDefenseTarget = false;
        return false;
    }

    struct DefenderCandidate
    {
        int EntityID = -1;
        float GroundPower = 0.0f;
        float AirPower = 0.0f;
        float Distance = 0.0f;
        bool Support = false;
    };
    std::vector<DefenderCandidate> Candidates;
    for (int UnitID : Units)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(UnitID);
        auto* Player = EM.GetComponent<PlayerComponent>(UnitID);
        auto* Position = EM.GetComponent<TransformComponent>(UnitID);
        auto* Combat = EM.GetComponent<CombatComponent>(UnitID);
        if (!Unit || !Player || !Position ||
            Player->PlayerID != mAIPlayerID || IsWorkerUnit(*Unit) ||
            EM.GetComponent<BuildingComponent>(UnitID) ||
            !IsArmyUnit(*Unit, Combat))
        {
            continue;
        }
        const float Durability =
            (float)std::max(0, Unit->HitPoints + Unit->ShieldPoints) / 20.0f;
        const float GroundPower = Combat && Combat->CanTargetGround &&
            Combat->GroundDamage > 0
            ? std::max(1.0f, Durability + (float)Combat->GroundDamage) : 0.0f;
        const float AirPower = Combat && Combat->CanTargetAir &&
            Combat->AirDamage > 0
            ? std::max(1.0f, Durability + (float)Combat->AirDamage) : 0.0f;
        Candidates.push_back({UnitID, GroundPower, AirPower,
            Position->Position.DistanceTo(ThreatPosition), IsSupportUnit(*Unit)});
    }

    std::sort(Candidates.begin(), Candidates.end(),
        [](const DefenderCandidate& Left, const DefenderCandidate& Right)
        {
            return Left.Distance < Right.Distance;
        });

    std::vector<int> Defenders;
    float GroundDefenderPower = 0.0f;
    float AirDefenderPower = 0.0f;
    const float RequiredGroundPower = GroundThreatPower > 0.0f
        ? std::max(1.0f, GroundThreatPower * 1.15f) : 0.0f;
    const float RequiredAirPower = AirThreatPower > 0.0f
        ? std::max(1.0f, AirThreatPower * 1.15f) : 0.0f;
    for (const auto& Candidate : Candidates)
    {
        if (Candidate.GroundPower <= 0.0f && Candidate.AirPower <= 0.0f)
            continue;
        if (Candidate.Distance > Profile.DefenderRadius)
        {
            // Outside local base perimeter: only recall distant units if local defenders
            // cannot meet the required power or no defenders exist yet
            if (GroundDefenderPower >= RequiredGroundPower &&
                AirDefenderPower >= RequiredAirPower &&
                !Defenders.empty())
            {
                break;
            }
        }
        Defenders.push_back(Candidate.EntityID);
        GroundDefenderPower += Candidate.GroundPower;
        AirDefenderPower += Candidate.AirPower;
    }

    // Detectors and spell/support units may accompany the response, but never
    // count as the combat power that decides whether enough defenders exist.
    int AddedSupport = 0;
    if (!Defenders.empty())
    {
        for (const auto& Candidate : Candidates)
        {
            if (!Candidate.Support ||
                std::find(Defenders.begin(), Defenders.end(),
                          Candidate.EntityID) != Defenders.end())
            {
                continue;
            }
            Defenders.push_back(Candidate.EntityID);
            if (++AddedSupport >= 2)
                break;
        }
    }
    const float DefenderPower = GroundDefenderPower + AirDefenderPower;

    if (Defenders.empty())
    {
        mActiveDefenseGroup.clear();
        mHasDefenseTarget = false;
        return false;
    }

    auto DefenseGroupKey = Defenders;
    std::sort(DefenseGroupKey.begin(), DefenseGroupKey.end());
    const bool DefenseGroupChanged =
        DefenseGroupKey != mActiveDefenseGroup;
    const bool DefenseTargetMoved = !mHasDefenseTarget ||
        mLastDefenseTarget.DistanceTo(ThreatPosition) > 96.0f;
    const bool RefreshDefenseOrder = !mDefendingBase ||
        DefenseGroupChanged || DefenseTargetMoved;
    if (RefreshDefenseOrder)
    {
        if (mDifficulty == AIDifficulty::Easy)
        {
            mActiveAttackGroup.clear();
            mActiveAttackGroupAge = 0.0f;
        }
        else
        {
            mActiveAttackGroup.erase(
                std::remove_if(
                    mActiveAttackGroup.begin(), mActiveAttackGroup.end(),
                    [&](int EntityID)
                    {
                        return std::find(Defenders.begin(), Defenders.end(),
                                         EntityID) != Defenders.end();
                    }),
                mActiveAttackGroup.end());
            if (mActiveAttackGroup.empty())
                mActiveAttackGroupAge = 0.0f;
        }
        std::vector<int> CombatDefenders;
        std::vector<int> MedicDefenders;
        for (int Eid : Defenders)
        {
            auto* Unit = EM.GetComponent<UnitComponent>(Eid);
            if (!Unit) continue;
            if (Unit->ChkUnitID == 34)
            {
                MedicDefenders.push_back(Eid);
                continue;
            }
            if (Unit->ChkUnitID == 30)
            {
                if (auto* Trans = EM.GetComponent<TransformComponent>(Eid))
                {
                    if (Trans->Position.DistanceTo(ThreatPosition) > 384.0f)
                    {
                        CommandSystem::SetSiegeMode(EM, Eid, false);
                    }
                }
            }
            else if (Unit->ChkUnitID == 103)
            {
                auto* Burrow = EM.GetComponent<BurrowComponent>(Eid);
                auto* Trans = EM.GetComponent<TransformComponent>(Eid);
                if (Burrow && Burrow->IsBurrowed && Trans &&
                    Trans->Position.DistanceTo(ThreatPosition) > 192.0f &&
                    !Burrow->IsTransitioning())
                {
                    Order UnburrowOrder;
                    UnburrowOrder.Type = OrderType::Unburrow;
                    CommandSystem::IssueOrder(EM, Eid, UnburrowOrder, false);
                }
            }
            CombatDefenders.push_back(Eid);
        }
        if (!CombatDefenders.empty())
        {
            CombatSystem::IssueAttackMove(EM, CombatDefenders, ThreatPosition);
        }
        if (!MedicDefenders.empty())
        {
            int EscortLeader = -1;
            for (int Cid : CombatDefenders)
            {
                auto* CUnit = EM.GetComponent<UnitComponent>(Cid);
                if (CUnit && (CUnit->ChkUnitID == 0 || CUnit->ChkUnitID == 32 || CUnit->ChkUnitID == 1))
                {
                    EscortLeader = Cid;
                    break;
                }
            }
            if (EscortLeader < 0 && !CombatDefenders.empty())
                EscortLeader = CombatDefenders.front();

            for (int Mid : MedicDefenders)
            {
                if (EscortLeader >= 0)
                {
                    auto* LeadTrans = EM.GetComponent<TransformComponent>(EscortLeader);
                    Order FollowOrder;
                    FollowOrder.Type = OrderType::Follow;
                    FollowOrder.TargetEntityID = EscortLeader;
                    FollowOrder.TargetPosition = LeadTrans ? LeadTrans->Position : ThreatPosition;
                    CommandSystem::IssueOrder(EM, Mid, FollowOrder, false);
                }
            }
        }
        mActiveDefenseGroup = std::move(DefenseGroupKey);
        mLastDefenseTarget = ThreatPosition;
        mHasDefenseTarget = true;
    }
    if (!mDefendingBase)
    {
        Core::Utils::Logger::Info(
            "AIBrain", "Player " + std::to_string(mAIPlayerID) +
            " base defense engaged defenders=" +
            std::to_string(Defenders.size()) + " threat=" +
            std::to_string(ThreatID) + " threats=" +
            std::to_string(ThreatCount) + " defender_power=" +
            std::to_string((int)DefenderPower) + " threat_power=" +
            std::to_string((int)ThreatPower) + " difficulty=" +
            std::string(AIDifficultyName(mDifficulty)));
    }
    return true;
}

void AIBrain::ManageTerranRepairs(Core::ECS::EntityManager& EM)
{
    const auto& Profile = GetAIDifficultyProfile(mDifficulty);
    struct RepairCandidate
    {
        int EntityID = -1;
        int Priority = 3;
        float HealthRatio = 1.0f;
        Core::Math::Vec2 Position;
        const char* PriorityName = "other_building";
    };

    const auto IsUnsafeToRepair = [&](const Core::Math::Vec2& Position)
    {
        if (mDifficulty == AIDifficulty::Easy)
            return false;
        for (int EnemyID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                               TransformComponent>())
        {
            auto* Enemy = EM.GetComponent<UnitComponent>(EnemyID);
            auto* EnemyPlayer = EM.GetComponent<PlayerComponent>(EnemyID);
            auto* EnemyPosition = EM.GetComponent<TransformComponent>(EnemyID);
            auto* EnemyCombat = EM.GetComponent<CombatComponent>(EnemyID);
            if (Enemy && EnemyPlayer && EnemyPosition && EnemyCombat &&
                Enemy->HitPoints > 0 &&
                EnemyPlayer->PlayerID != mAIPlayerID &&
                !EnemyPlayer->IsRescuePassive &&
                (!mFog || !mFog->AreAllied(
                    mAIPlayerID, EnemyPlayer->PlayerID)) &&
                IsCombatUnit(*Enemy, *EnemyCombat) &&
                EnemyPosition->Position.DistanceTo(Position) <= 160.0f &&
                IsVisibleToAI(EnemyPosition->Position, EnemyID, EM))
            {
                return true;
            }
        }
        return false;
    };

    int ActiveRepairers = 0;
    std::vector<int> AssignedTargets;
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            CommandComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* Order = Command ? Command->GetCurrentOrder() : nullptr;
        if (Unit && Player && Player->PlayerID == mAIPlayerID &&
            Unit->ChkUnitID == 7 && Order && Order->Type == OrderType::Repair)
        {
            const int RepairTargetID = Order->TargetEntityID;
            const auto* TargetPosition =
                EM.GetComponent<TransformComponent>(RepairTargetID);
            if (TargetPosition && IsUnsafeToRepair(TargetPosition->Position))
            {
                CommandSystem::IssueMove(EM, {EntityID}, mAiBasePos);
                Core::Utils::Logger::Info(
                    "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                    " stopped unsafe repair scv=" + std::to_string(EntityID) +
                    " target=" + std::to_string(RepairTargetID));
                continue;
            }
            ++ActiveRepairers;
            AssignedTargets.push_back(RepairTargetID);
        }
    }
    if (ActiveRepairers >= Profile.MaxRepairers)
        return;

    RepairCandidate Best;
    bool Found = false;
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
        auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
        if (!Unit || !Player || !Transform ||
            Player->PlayerID != mAIPlayerID || Unit->HitPoints <= 0 ||
            Unit->HitPoints >= Unit->MaxHitPoints ||
            Unit->UnitRace != Race::Terran ||
            IsUnclaimedResourceEntity(EM, EntityID))
        {
            continue;
        }

        RepairCandidate Candidate;
        Candidate.EntityID = EntityID;
        Candidate.Position = Transform->Position;
        Candidate.HealthRatio = Unit->MaxHitPoints > 0
            ? (float)Unit->HitPoints / (float)Unit->MaxHitPoints : 1.0f;
        if (Candidate.HealthRatio > Profile.RepairHealthThreshold)
            continue;

        // Medium and Hard do not feed SCVs into concentrated enemy fire.
        // Easy intentionally keeps the slower, more literal repair response.
        if (IsUnsafeToRepair(Candidate.Position))
            continue;
        const auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
        int TargetRepairerCap = 1;
        if (!Building && IsTerranMechanicalUnit(*Unit) && Combat &&
            IsCombatUnit(*Unit, *Combat))
        {
            Candidate.Priority = 0;
            Candidate.PriorityName = "combat_mechanical";
        }
        else if (Building && !Building->UnderConstruction)
        {
            const auto Def = Game::Races::GetUnitDefByID(
                ResolveOwnedDefID(EM, EntityID));
            const bool KeyStructure = Def.Name == "CommandCenter" ||
                Def.Name == "Bunker" || Def.Name == "MissileTurret";
            Candidate.Priority = KeyStructure ? 0 :
                (IsTerranProductionBuilding(Def.Name) ? 1 : 2);
            Candidate.PriorityName = KeyStructure ? "key_structure" :
                (Candidate.Priority == 1
                    ? "production_building" : "other_building");
            if (KeyStructure)
                TargetRepairerCap = Profile.MaxRepairers;
        }
        else
        {
            continue;
        }

        const int AssignedToCandidate = (int)std::count(
            AssignedTargets.begin(), AssignedTargets.end(), EntityID);
        if (AssignedToCandidate >= TargetRepairerCap)
            continue;

        if (!Found || Candidate.Priority < Best.Priority ||
            (Candidate.Priority == Best.Priority &&
             Candidate.HealthRatio < Best.HealthRatio))
        {
            Best = Candidate;
            Found = true;
        }
    }
    if (!Found)
        return;

    int BestScv = -1;
    float BestDistance = std::numeric_limits<float>::max();
    for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                            TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
        auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* Order = Command ? Command->GetCurrentOrder() : nullptr;
        if (!Unit || !Player || !Transform || Unit->HitPoints <= 0 ||
            Unit->ChkUnitID != 7 || Player->PlayerID != mAIPlayerID ||
            IsAssignedToConstruction(EM, EntityID) ||
            (Order && Order->Type == OrderType::Repair))
        {
            continue;
        }
        const float Distance = Transform->Position.DistanceTo(Best.Position);
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            BestScv = EntityID;
        }
    }
    if (BestScv < 0)
        return;

    CommandSystem::InterruptHarvest(EM, BestScv);
    Order RepairOrder;
    RepairOrder.Type = OrderType::Repair;
    RepairOrder.TargetEntityID = Best.EntityID;
    RepairOrder.TargetPosition = Best.Position;
    RepairOrder.PathingIgnoreEntityID = Best.EntityID;
    CommandSystem::IssueOrder(EM, BestScv, RepairOrder, false);
    Core::Utils::Logger::Info(
        "AIBrain", "Player " + std::to_string(mAIPlayerID) +
        " repair assigned scv=" + std::to_string(BestScv) +
        " target=" + std::to_string(Best.EntityID) +
        " priority=" + Best.PriorityName + " difficulty=" +
        std::string(AIDifficultyName(mDifficulty)));
}

void AIBrain::ManageTerranMedics(Core::ECS::EntityManager& EM)
{
    // Gather all alive owned Medics
    std::vector<int> Medics;
    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent, TransformComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (Unit && Player && Unit->HitPoints > 0 &&
            Unit->ChkUnitID == 34 && Player->PlayerID == mAIPlayerID)
        {
            Medics.push_back(Eid);
        }
    }
    if (Medics.empty())
        return;

    // Gather potential patients: alive friendly organic units with missing HP
    struct PatientCandidate
    {
        int EntityID = -1;
        Core::Math::Vec2 Position;
        int ChkUnitID = -1;
        float Urgency = 0.0f;
    };
    std::vector<PatientCandidate> Patients;

    // Also gather living combat infantry leaders (Marine, Firebat, Ghost) to escort when no one is wounded
    struct EscortCandidate
    {
        int EntityID = -1;
        Core::Math::Vec2 Position;
        bool InAttackGroup = false;
    };
    std::vector<EscortCandidate> InfantryLeaders;

    for (int Eid : EM.GetEntitiesWith<UnitComponent, PlayerComponent, TransformComponent>())
    {
        if (EM.GetComponent<BuildingComponent>(Eid))
            continue;
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        if (!Unit || !Player || !Trans || Unit->HitPoints <= 0)
            continue;

        const bool IsFriendly = (Player->PlayerID == mAIPlayerID) ||
            (mFog && mFog->AreAllied(mAIPlayerID, Player->PlayerID));
        if (!IsFriendly)
            continue;

        if (Game::Races::IsOrganicChkUnitID(Unit->ChkUnitID))
        {
            if (Unit->HitPoints < Unit->MaxHitPoints)
            {
                float MissingRatio = 1.0f - ((float)Unit->HitPoints / (float)Unit->MaxHitPoints);
                float PriorityBonus = 0.0f;
                if (Unit->ChkUnitID == 0 || Unit->ChkUnitID == 32 || Unit->ChkUnitID == 1)
                    PriorityBonus = 50.0f;
                else if (Unit->ChkUnitID == 7) // SCV
                    PriorityBonus = 20.0f;

                Patients.push_back({Eid, Trans->Position, Unit->ChkUnitID, MissingRatio * 100.0f + PriorityBonus});
            }
            if (Player->PlayerID == mAIPlayerID &&
                (Unit->ChkUnitID == 0 || Unit->ChkUnitID == 32 || Unit->ChkUnitID == 1))
            {
                bool InAttack = std::find(mActiveAttackGroup.begin(), mActiveAttackGroup.end(), Eid) != mActiveAttackGroup.end();
                InfantryLeaders.push_back({Eid, Trans->Position, InAttack});
            }
        }
    }

    for (int MedicID : Medics)
    {
        auto* MedicUnit = EM.GetComponent<UnitComponent>(MedicID);
        auto* MedicTrans = EM.GetComponent<TransformComponent>(MedicID);
        auto* Command = EM.GetComponent<CommandComponent>(MedicID);
        if (!MedicUnit || !MedicTrans)
            continue;

        const Order* CurrentOrder = Command ? Command->GetCurrentOrder() : nullptr;

        // If currently executing Heal and target is still valid & wounded, let it continue
        if (CurrentOrder && CurrentOrder->Type == OrderType::Heal && CurrentOrder->TargetEntityID >= 0)
        {
            auto* TargetUnit = EM.GetComponent<UnitComponent>(CurrentOrder->TargetEntityID);
            if (TargetUnit && TargetUnit->HitPoints > 0 &&
                TargetUnit->HitPoints < TargetUnit->MaxHitPoints &&
                MedicUnit->EnergyPoints > 0)
            {
                continue;
            }
        }

        // Priority 1: Heal nearby wounded friendly organic unit
        int BestPatientID = -1;
        float BestScore = -1e9f;
        constexpr float HealScanRadius = 384.0f; // ~12 tiles

        if (MedicUnit->EnergyPoints > 0)
        {
            for (const auto& P : Patients)
            {
                float Dist = MedicTrans->Position.DistanceTo(P.Position);
                if (Dist <= HealScanRadius)
                {
                    float Score = P.Urgency - Dist * 0.15f;
                    if (Score > BestScore)
                    {
                        BestScore = Score;
                        BestPatientID = P.EntityID;
                    }
                }
            }
        }

        if (BestPatientID >= 0)
        {
            auto* PatientTrans = EM.GetComponent<TransformComponent>(BestPatientID);
            Order HealOrder;
            HealOrder.Type = OrderType::Heal;
            HealOrder.TargetEntityID = BestPatientID;
            HealOrder.TargetPosition = PatientTrans ? PatientTrans->Position : MedicTrans->Position;
            CommandSystem::IssueOrder(EM, MedicID, HealOrder, false);
            continue;
        }

        // Priority 2: Escort infantry, stay in formation backline
        int BestLeaderID = -1;
        float BestLeaderDist = 1e9f;
        bool MedicInAttack = std::find(mActiveAttackGroup.begin(), mActiveAttackGroup.end(), MedicID) != mActiveAttackGroup.end();

        for (const auto& L : InfantryLeaders)
        {
            float Dist = MedicTrans->Position.DistanceTo(L.Position);
            if (MedicInAttack && !L.InAttackGroup)
                Dist += 500.0f;
            else if (!MedicInAttack && L.InAttackGroup)
                Dist += 300.0f;

            if (Dist < BestLeaderDist)
            {
                BestLeaderDist = Dist;
                BestLeaderID = L.EntityID;
            }
        }

        if (BestLeaderID >= 0)
        {
            auto* LeadTrans = EM.GetComponent<TransformComponent>(BestLeaderID);
            float RealDist = LeadTrans ? MedicTrans->Position.DistanceTo(LeadTrans->Position) : 0.0f;

            if (RealDist > 80.0f)
            {
                Order FollowOrder;
                FollowOrder.Type = OrderType::Follow;
                FollowOrder.TargetEntityID = BestLeaderID;
                FollowOrder.TargetPosition = LeadTrans->Position;
                CommandSystem::IssueOrder(EM, MedicID, FollowOrder, false);
            }
            else
            {
                if (CurrentOrder && (CurrentOrder->Type == OrderType::Move || CurrentOrder->Type == OrderType::AttackMove))
                {
                    Order StopOrder;
                    StopOrder.Type = OrderType::Stop;
                    CommandSystem::IssueOrder(EM, MedicID, StopOrder, false);
                }
            }
            continue;
        }

        // Priority 3: Stranded with no infantry around; retreat to base if far away
        const float DistToBase = MedicTrans->Position.DistanceTo(mAiBasePos);
        if (DistToBase > 350.0f)
        {
            if (!CurrentOrder || CurrentOrder->Type != OrderType::Move ||
                CurrentOrder->TargetPosition.DistanceTo(mAiBasePos) > 64.0f)
            {
                Order RetreatOrder;
                RetreatOrder.Type = OrderType::Move;
                RetreatOrder.TargetPosition = mAiBasePos;
                CommandSystem::IssueOrder(EM, MedicID, RetreatOrder, false);
            }
        }
        else if (CurrentOrder && CurrentOrder->Type == OrderType::Move)
        {
            Order StopOrder;
            StopOrder.Type = OrderType::Stop;
            CommandSystem::IssueOrder(EM, MedicID, StopOrder, false);
        }
    }
}

void AIBrain::SendAttackWave(Core::ECS::EntityManager& EM, int WaveSize)
{
    const auto& Profile = GetAIDifficultyProfile(mDifficulty);
    if (mAttackCooldownRemaining > 0.0f)
        return;

    // Resource-depleted late games must not settle into two armies guarding
    // opposite corners forever.  After 45 simulated minutes, release any
    // surviving old group and commit every available combat unit to the next
    // living opponent.  The normal cooldown still limits order churn.
    const bool FinalAssault = mElapsedSeconds >= 2700.0f;

    mActiveAttackGroup.erase(
        std::remove_if(
            mActiveAttackGroup.begin(), mActiveAttackGroup.end(),
            [&](int EntityID)
            {
                auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
                auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
                auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
                return !EM.IsAlive(EntityID) || !Unit || !Player ||
                       Player->PlayerID != mAIPlayerID ||
                       EM.GetComponent<BuildingComponent>(EntityID) ||
                       !IsArmyUnit(*Unit, Combat) ||
                       !AIActiveAttackGroupAcceptsWorker(
                           FinalAssault, IsWorkerUnit(*Unit));
            }),
        mActiveAttackGroup.end());

    const auto FindVisibleDirectTarget = [&]()
    {
        int UnitFallback = -1;
        for (int EntityID : EM.GetEntitiesWith<PlayerComponent, UnitComponent,
                                                TransformComponent>())
        {
            auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
            auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            auto* Position = EM.GetComponent<TransformComponent>(EntityID);
            if (!Player || !Unit || !Position ||
                Player->PlayerID != mTargetPlayerID || Unit->HitPoints <= 0 ||
                IsUnclaimedResourceEntity(EM, EntityID) ||
                !IsVisibleToAI(Position->Position, EntityID, EM))
            {
                continue;
            }
            if (EM.GetComponent<BuildingComponent>(EntityID))
                return EntityID;
            if (UnitFallback < 0)
                UnitFallback = EntityID;
        }
        return UnitFallback;
    };

    const auto GatherAvailableArmy = [&](bool IncludeWorkers)
    {
        std::vector<int> Result;
        for (int EntityID : EM.GetEntitiesWith<UnitComponent, PlayerComponent,
                                               TransformComponent>())
        {
            auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
            auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
            if (!Unit || !Player || Unit->HitPoints <= 0 ||
                Player->PlayerID != mAIPlayerID ||
                EM.GetComponent<BuildingComponent>(EntityID) ||
                !IsArmyUnit(*Unit, Combat) ||
                std::find(mActiveAttackGroup.begin(), mActiveAttackGroup.end(),
                          EntityID) != mActiveAttackGroup.end())
            {
                continue;
            }

            const bool Worker = IsWorkerUnit(*Unit);
            if (Worker && !IncludeWorkers)
                continue;
            if (Worker)
            {
                const auto* Harvest = EM.GetComponent<HarvestComponent>(EntityID);
                const auto* Resource = Harvest
                    ? EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID)
                    : nullptr;
                if (Harvest && Harvest->State != HarvestState::Idle && Resource &&
                    Resource->Type == ResourceType::Minerals &&
                    !Resource->IsDepleted && Resource->Amount > 0)
                {
                    continue;
                }
            }
            Result.push_back(EntityID);
        }

        // Units already away from the mineral line leave first.  Keeping the
        // nearest slice behind gives Medium/Hard a real base-defense reserve.
        std::sort(Result.begin(), Result.end(), [&](int Left, int Right)
        {
            const auto* LeftPos = EM.GetComponent<TransformComponent>(Left);
            const auto* RightPos = EM.GetComponent<TransformComponent>(Right);
            const float LeftDistance = LeftPos
                ? LeftPos->Position.DistanceTo(mAiBasePos) : 0.0f;
            const float RightDistance = RightPos
                ? RightPos->Position.DistanceTo(mAiBasePos) : 0.0f;
            return LeftDistance > RightDistance;
        });

        if (!IncludeWorkers && Profile.DefenderReservePercent > 0 && !Result.empty())
        {
            const size_t Reserve = (Result.size() *
                (size_t)Profile.DefenderReservePercent + 99u) / 100u;
            if (Reserve < Result.size())
                Result.resize(Result.size() - Reserve);
            else
                Result.clear();
        }
        return Result;
    };

    const auto OrderAttackers = [&](const std::vector<int>& Attackers)
    {
        const int DirectTarget = FindVisibleDirectTarget();
        mEnemyBasePos = FindEnemyBaseLocation(
            EM, AIShouldAdvanceEnemyMapSearch(FinalAssault, DirectTarget));
        const auto* DirectTargetPosition = DirectTarget >= 0
            ? EM.GetComponent<TransformComponent>(DirectTarget) : nullptr;
        std::vector<int> ReadyAttackers;
        ReadyAttackers.reserve(Attackers.size());
        for (int EntityID : Attackers)
        {
            auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID);
            auto* Position = EM.GetComponent<TransformComponent>(EntityID);
            if (!Unit || Unit->ChkUnitID != 103 || !Burrow || !Position ||
                Burrow->Phase == BurrowPhase::Surface)
            {
                ReadyAttackers.push_back(EntityID);
                continue;
            }

            const int LurkerDefID =
                Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
            const float LurkerAttackRangeTiles = LurkerDefID >= 0
                ? Game::Races::GetUnitDefByID(LurkerDefID).AttackRange : 6.0f;
            const bool CanFightFromHere = DirectTargetPosition &&
                CanAIHoldBurrowedLurker(
                    Position->Position.DistanceTo(DirectTargetPosition->Position),
                    LurkerAttackRangeTiles);
            if (CanFightFromHere &&
                (Burrow->IsBurrowed || Burrow->Phase == BurrowPhase::Burrowing))
            {
                ReadyAttackers.push_back(EntityID);
                continue;
            }

            // A buried Lurker cannot accept a movement order.  Make unburrow
            // the current order and queue the advance so the command system
            // constructs its path as soon as the animation completes.
            if (Burrow->IsBurrowed && !Burrow->IsTransitioning())
            {
                Order UnburrowOrder;
                UnburrowOrder.Type = OrderType::Unburrow;
                CommandSystem::IssueOrder(EM, EntityID, UnburrowOrder, false);
            }
            if (Burrow->Phase == BurrowPhase::Unburrowing || Burrow->IsBurrowed)
            {
                Order ResumeAdvance;
                ResumeAdvance.Type = OrderType::AttackMove;
                ResumeAdvance.TargetPosition = DirectTargetPosition
                    ? DirectTargetPosition->Position : mEnemyBasePos;
                CommandSystem::IssueOrder(EM, EntityID, ResumeAdvance, true);
            }
        }
        std::vector<int> DirectCombatUnits;
        std::vector<int> MedicUnits;
        std::vector<int> OtherSupportUnits;
        for (int EntityID : ReadyAttackers)
        {
            auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
            if (Unit && Unit->ChkUnitID == 34)
                MedicUnits.push_back(EntityID);
            else if (Unit && Combat && IsCombatUnit(*Unit, *Combat))
                DirectCombatUnits.push_back(EntityID);
            else
                OtherSupportUnits.push_back(EntityID);
        }

        Core::Math::Vec2 TargetPos = mEnemyBasePos;
        if (DirectTarget >= 0)
        {
            const auto* Position =
                EM.GetComponent<TransformComponent>(DirectTarget);
            if (Position)
                TargetPos = Position->Position;
        }

        if (!DirectCombatUnits.empty())
            CombatSystem::IssueAttackMove(EM, DirectCombatUnits, TargetPos);
        if (!OtherSupportUnits.empty())
            CombatSystem::IssueAttackMove(EM, OtherSupportUnits, TargetPos);

        if (!MedicUnits.empty())
        {
            // Medics escort combat infantry or ground combat leaders instead of rushing ahead
            int EscortLeader = -1;
            for (int Cid : DirectCombatUnits)
            {
                auto* CUnit = EM.GetComponent<UnitComponent>(Cid);
                if (CUnit && (CUnit->ChkUnitID == 0 || CUnit->ChkUnitID == 32 || CUnit->ChkUnitID == 1))
                {
                    EscortLeader = Cid;
                    break;
                }
            }
            if (EscortLeader < 0 && !DirectCombatUnits.empty())
            {
                for (int Cid : DirectCombatUnits)
                {
                    auto* CPath = EM.GetComponent<PathfindingComponent>(Cid);
                    if (!CPath || !CPath->IsAir)
                    {
                        EscortLeader = Cid;
                        break;
                    }
                }
            }

            if (EscortLeader >= 0)
            {
                auto* LeadTrans = EM.GetComponent<TransformComponent>(EscortLeader);
                for (int Mid : MedicUnits)
                {
                    Order FollowOrder;
                    FollowOrder.Type = OrderType::Follow;
                    FollowOrder.TargetEntityID = EscortLeader;
                    FollowOrder.TargetPosition = LeadTrans ? LeadTrans->Position : mEnemyBasePos;
                    CommandSystem::IssueOrder(EM, Mid, FollowOrder, false);
                }
            }
            else
            {
                for (int Mid : MedicUnits)
                {
                    Order ReturnOrder;
                    ReturnOrder.Type = OrderType::Move;
                    ReturnOrder.TargetPosition = mAiBasePos;
                    CommandSystem::IssueOrder(EM, Mid, ReturnOrder, false);
                }
            }
        }
        return DirectTarget;
    };

    // Hard continuously feeds small groups into a surviving main force.
    if (!FinalAssault && !mActiveAttackGroup.empty() &&
        mActiveAttackGroupAge < Profile.AttackGroupMaxAgeSeconds)
    {
        if (Profile.ContinuousReinforcement && Profile.ReinforcementSize > 0)
        {
            auto Reinforcements = GatherAvailableArmy(false);
            if ((int)Reinforcements.size() >= Profile.ReinforcementSize)
            {
                Reinforcements.resize((size_t)Profile.ReinforcementSize);
                const int DirectTarget = OrderAttackers(Reinforcements);
                mActiveAttackGroup.insert(mActiveAttackGroup.end(),
                                          Reinforcements.begin(),
                                          Reinforcements.end());
                mAttackCooldownRemaining = Profile.AttackCooldownSeconds;
                Core::Utils::Logger::Info(
                    "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                    " reinforcement=" +
                    std::to_string(Reinforcements.size()) +
                    " active_group=" +
                    std::to_string(mActiveAttackGroup.size()) +
                    " direct_target=" + std::to_string(DirectTarget));
            }
        }
        else
        {
            auto NextWave = GatherAvailableArmy(false);
            const int MinWaveAttackers = std::max(2, WaveSize * (100 - Profile.DefenderReservePercent) / 100);
            if ((int)NextWave.size() >= MinWaveAttackers)
            {
                if ((int)NextWave.size() > WaveSize)
                    NextWave.resize((size_t)WaveSize);
                const int DirectTarget = OrderAttackers(NextWave);
                mActiveAttackGroup.insert(
                    mActiveAttackGroup.end(),
                    NextWave.begin(),
                    NextWave.end());
                mAttackWaveCount++;
                mAttackCooldownRemaining = Profile.AttackCooldownSeconds;
                Core::Utils::Logger::Info(
                    "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                    " subsequent_wave=" + std::to_string(mAttackWaveCount) +
                    " units=" + std::to_string(NextWave.size()) +
                    " active_group=" + std::to_string(mActiveAttackGroup.size()) +
                    " direct_target=" + std::to_string(DirectTarget));
            }
        }
        return;
    }

    // Final assault releases the reserve, but it must not overwrite every
    // unit's attack/path order on each short cooldown.  Reissue only after the
    // bounded group age so a genuinely blocked survivor still gets a fresh
    // route without creating permanent command churn.
    if (FinalAssault && !mActiveAttackGroup.empty() &&
        mActiveAttackGroupAge < Profile.AttackGroupMaxAgeSeconds)
    {
        return;
    }

    // Simple and Medium brains form a new fixed wave only after the previous
    // one is mostly gone. Every profile recycles a route-blocked group after
    // its bounded age so later waves cannot freeze forever.
    const int SurvivingThreshold = std::max(2, mLastAttackWaveSize * 3 / 10);
    if (!FinalAssault && (int)mActiveAttackGroup.size() > SurvivingThreshold &&
        mActiveAttackGroupAge < Profile.AttackGroupMaxAgeSeconds)
    {
        return;
    }
    mActiveAttackGroup.clear();
    mActiveAttackGroupAge = 0.0f;

    std::vector<int> Attackers = GatherAvailableArmy(FinalAssault);

    const int MinWaveAttackers = std::max(2, WaveSize * (100 - Profile.DefenderReservePercent) / 100);
    if (!Attackers.empty() &&
        (FinalAssault || (int)Attackers.size() >= MinWaveAttackers))
    {
        if (!FinalAssault && (int)Attackers.size() > WaveSize)
            Attackers.resize((size_t)WaveSize);
        const int DirectTarget = OrderAttackers(Attackers);
        mActiveAttackGroup = Attackers;
        mActiveAttackGroupAge = 0.0f;
        mLastAttackWaveSize = (int)Attackers.size();
        mAttackWaveCount++;
        mAttackCooldownRemaining = Profile.AttackCooldownSeconds;
        Core::Utils::Logger::Info(
            "AIBrain", "Player " + std::to_string(mAIPlayerID) +
            " attack_wave=" + std::to_string(mAttackWaveCount) +
            " units=" + std::to_string(Attackers.size()) +
            " target_player=" + std::to_string(mTargetPlayerID) +
            " target=" + std::to_string((int)mEnemyBasePos.X) + "," +
            std::to_string((int)mEnemyBasePos.Y) +
            " direct_target=" + std::to_string(DirectTarget) +
            " final_assault=" + std::to_string(FinalAssault ? 1 : 0) +
            " difficulty=" + std::string(AIDifficultyName(mDifficulty)));
    }
}

void AIBrain::BuildOrTrain(Core::ECS::EntityManager& EM, int DefID, const Core::Math::Vec2& NearPos)
{
    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.IsBuilding)
    {
        int Builder = FindIdleWorker(EM);
        if (Builder >= 0)
            StartBuildingNear(EM, Builder, DefID, NearPos);
    }
    else
    {
        TrainUnitIfPossible(EM, DefID);
    }
}

void AIBrain::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;
    auto& EM = mSystemMgr.GetEntityManager();
    mElapsedSeconds += DeltaTime;
    if (!mActiveAttackGroup.empty())
        mActiveAttackGroupAge += DeltaTime;

    if (!mShowcaseMode)
        RetargetIfNeeded(EM);

    mAttackCooldownRemaining =
        std::max(0.0f, mAttackCooldownRemaining - DeltaTime);

    if (!mPositionsResolved)
    {
        mAiBasePos = FindBaseLocation(EM);
        mEnemyBasePos = FindEnemyBaseLocation(EM);
        mExpansionPos = FindExpansionLocation(EM);
        if (mAiBasePos.LengthSq() > 0)
            mPositionsResolved = true;
    }

    mDecisionTimer += DeltaTime;
    if (mDecisionTimer < mDecisionInterval) return;
    mDecisionTimer = 0.0f;

    BuildEntityCountCache(EM);
    struct CacheGuard {
        AIBrain& Brain;
        ~CacheGuard() { Brain.InvalidateEntityCountCache(); }
    } Guard{*this};

    if (AIShouldStopAfterElimination(
            mShowcaseMode, mElapsedSeconds,
            HasLivingStructures(EM, mAIPlayerID)))
    {
        if (!mEliminationLogged)
        {
            mEliminationLogged = true;
            mActiveAttackGroup.clear();
            mActiveDefenseGroup.clear();
            mHasDefenseTarget = false;
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " stopped after losing all melee structures");
        }
        return;
    }

    bool HasDepotAtCurrentBase = false;
    for (int DepotID : EM.GetEntitiesWith<BuildingComponent, PlayerComponent,
                                           TransformComponent, UnitComponent>())
    {
        auto* Player = EM.GetComponent<PlayerComponent>(DepotID);
        auto* Position = EM.GetComponent<TransformComponent>(DepotID);
        auto* Unit = EM.GetComponent<UnitComponent>(DepotID);
        if (Player && Position && Unit && Player->PlayerID == mAIPlayerID &&
            Unit->HitPoints > 0 &&
            IsResourceDepotDefID(ResolveOwnedDefID(EM, DepotID)) &&
            Position->Position.DistanceTo(mAiBasePos) <= 320.0f)
        {
            HasDepotAtCurrentBase = true;
            break;
        }
    }
    if (!HasDepotAtCurrentBase)
    {
        const Core::Math::Vec2 PreviousBase = mAiBasePos;
        mAiBasePos = FindBaseLocation(EM);
        mExpansionPos = FindExpansionLocation(EM);
        if (mAiBasePos.DistanceTo(PreviousBase) > 32.0f)
        {
            Core::Utils::Logger::Info(
                "AIBrain", "Player " + std::to_string(mAIPlayerID) +
                " relocated build anchor from " +
                std::to_string((int)PreviousBase.X) + "," +
                std::to_string((int)PreviousBase.Y) + " to " +
                std::to_string((int)mAiBasePos.X) + "," +
                std::to_string((int)mAiBasePos.Y));
        }
    }

    int RaceVal = GetRace(EM);
    if (!mConfigurationLogged)
    {
        mConfigurationLogged = true;
        const auto& Profile = GetAIDifficultyProfile(mDifficulty);
        Core::Utils::Logger::Info(
            "AIBrain", "Player " + std::to_string(mAIPlayerID) +
            " configured race=" + std::to_string(RaceVal) +
            " difficulty=" + std::string(AIDifficultyName(mDifficulty)) +
            " decision_interval=" +
            std::to_string(Profile.DecisionIntervalSeconds) +
            " worker_cap=" + std::to_string(Profile.WorkerCap) +
            " max_bases=" + std::to_string(Profile.MaxBases) +
            " max_tech_stage=" + std::to_string(Profile.MaxTechStage));
    }

    if (!mShowcaseMode)
    {
        ManageMilitary(EM);
        mDefendingBase = DefendBaseIfNeeded(EM);
        ManageUpgrades(EM, RaceVal);
        if (RaceVal == (int)Race::Terran)
        {
            ManageTerranRepairs(EM);
            ManageTerranMedics(EM);
        }
    }
    else
    {
        mDefendingBase = false;
    }
    if (mShowcaseMode && !mShowcaseStartedLogged)
    {
        mShowcaseStartedLogged = true;
        Core::Utils::Logger::Info(
            "AIBrain", "Showcase decision loop active race=" +
            std::to_string(RaceVal) + " player=" + std::to_string(mAIPlayerID) +
            " base=" + std::to_string((int)mAiBasePos.X) + "," +
            std::to_string((int)mAiBasePos.Y));
    }
    if (mShowcaseMode && RaceVal == (int)Race::Terran)
    {
        RunTerranShowcase(EM);
        return;
    }
    if (mShowcaseMode && RaceVal == (int)Race::Zerg)
    {
        RunZergShowcase(EM);
        return;
    }
    if (mShowcaseMode && RaceVal == (int)Race::Protoss)
    {
        RunProtossShowcase(EM);
        return;
    }
    switch (RaceVal)
    {
        case 0: RunTerranAI(EM); break;
        case 1: RunZergAI(EM); break;
        case 2: RunProtossAI(EM); break;
        default: RunTerranAI(EM); break;
    }
}

} // namespace Game::Systems
