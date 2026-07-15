#include "Game/Systems/ZergSystem.h"
#include "Game/Systems/BurrowState.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Core/Utils/Logger.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <utility>

namespace Game::Systems
{

using namespace Game::Components;
using Core::Utils::Logger;

Game::Map::GameMap* ZergSystem::sMap = nullptr;

namespace
{
    ZergSystem::ImageIDResolver gImageResolver;
    ZergSystem::CollisionBoundsResolver gCollisionBoundsResolver;
    ZergSystem::BurrowCapabilityResolver gBurrowCapabilityResolver;

    uint32_t ResolveImageID(int ChkUnitID)
    {
        return gImageResolver ? gImageResolver(ChkUnitID) : (uint32_t)ChkUnitID;
    }

    UnitCollisionBounds ResolveCollisionBounds(int ChkUnitID)
    {
        return gCollisionBoundsResolver ? gCollisionBoundsResolver(ChkUnitID) : UnitCollisionBounds{};
    }

    bool HasCreepAcrossBuildingFootprint(int ChkUnitID,
                                         const Core::Math::Vec2& Position)
    {
        int Width = 3;
        int Height = 2;
        if (ChkUnitID == 131 || ChkUnitID == 132 || ChkUnitID == 133)
        {
            Width = 4;
            Height = 3;
        }
        else if (ChkUnitID == 137 || ChkUnitID == 141)
        {
            Width = 2;
            Height = 2;
        }
        const int CenterX = (int)(Position.X / 32.0f);
        const int CenterY = (int)(Position.Y / 32.0f);
        const int Left = CenterX - Width / 2;
        const int Top = CenterY - Height / 2;
        for (int Y = Top; Y < Top + Height; ++Y)
            for (int X = Left; X < Left + Width; ++X)
                if (!ZergSystem::HasCreep(X, Y))
                    return false;
        return true;
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

    int ResolveBuildingDefID(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
        {
            if (Building->BuildingDefID >= 0)
                return Building->BuildingDefID;
        }
        if (auto* Unit = EM.GetComponent<UnitComponent>(EntityID))
            return Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
    }

    bool HasCompletedBuildingDef(Core::ECS::EntityManager& EM, int PlayerID, int RequiredDefID)
    {
        auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
        for (int Bid : Buildings)
        {
            auto* B = EM.GetComponent<BuildingComponent>(Bid);
            auto* BP = EM.GetComponent<PlayerComponent>(Bid);
            if (!B || !BP || BP->PlayerID != PlayerID || B->UnderConstruction)
                continue;

            if (Game::Races::BuildingDefSatisfiesRequirement(
                    ResolveBuildingDefID(EM, Bid), RequiredDefID))
                return true;
        }
        return false;
    }

    bool HasRequiredBuildings(Core::ECS::EntityManager& EM,
                              int PlayerID,
                              const std::vector<int>& RequiredBuildings,
                              Race RaceType,
                              std::string* OutReason)
    {
        for (int ReqID : RequiredBuildings)
        {
            const int NormalizedReqID = NormalizeDefIDForRace(ReqID, RaceType);
            if (HasCompletedBuildingDef(EM, PlayerID, NormalizedReqID))
                continue;

            if (OutReason)
                *OutReason = "missing required building: " +
                    Game::Races::GetUnitDefByID(NormalizedReqID).Name;
            return false;
        }
        return true;
    }

    bool IsValidZergBuildingMorph(int SourceDefID, int TargetDefID)
    {
        const auto SourceDef = Game::Races::GetUnitDefByID(SourceDefID);
        const auto TargetDef = Game::Races::GetUnitDefByID(TargetDefID);
        if (SourceDef.RaceType != Race::Zerg || TargetDef.RaceType != Race::Zerg)
            return false;

        return (SourceDef.Name == "Hatchery" && TargetDef.Name == "Lair") ||
               (SourceDef.Name == "Lair" && TargetDef.Name == "Hive") ||
               (SourceDef.Name == "Spire" && TargetDef.Name == "Greater Spire") ||
               (SourceDef.Name == "Creep Colony" && TargetDef.Name == "Spore Colony") ||
               (SourceDef.Name == "Creep Colony" && TargetDef.Name == "Sunken Colony");
    }
}


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

void ZergSystem::SetCollisionBoundsResolver(CollisionBoundsResolver Resolver)
{
    gCollisionBoundsResolver = std::move(Resolver);
}

void ZergSystem::SetImageResolver(ImageIDResolver Resolver)
{
    gImageResolver = std::move(Resolver);
}

void ZergSystem::SetBurrowCapabilityResolver(BurrowCapabilityResolver Resolver)
{
    gBurrowCapabilityResolver = std::move(Resolver);
}

bool ZergSystem::CanUnitBurrow(int ChkUnitID, uint32_t ImageID)
{
    if (gBurrowCapabilityResolver)
        return gBurrowCapabilityResolver(ChkUnitID, ImageID);

    switch (ChkUnitID)
    {
        case 37:  // Zergling
        case 38:  // Hydralisk
        case 41:  // Drone
        case 46:  // Defiler
        case 50:  // Infested Kerrigan
        case 51:  // Unclean One
        case 53:  // Hunter Killer
        case 54:  // Devouring One
        case 103: // Lurker
            return true;
        default:
            return false;
    }
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

    if (!mGridInitialized && sMap)
    {
        mMapWidth = sMap->GetWidth();
        mMapHeight = sMap->GetHeight();
        mCreepGrid.resize(mMapWidth * mMapHeight, 0);
        mGridInitialized = true;
    }

    if (!sMap || mCreepGrid.empty()) return;

    // Brood War creep sources are Hatchery/Lair/Hive (and Creep Colony), not
    // every organic Zerg structure.  Keeping this source list explicit stops
    // ordinary tech buildings from chaining creep across the entire map.
    struct CreepSource
    {
        int EntityID = -1;
        int TileX = 0;
        int TileY = 0;
        bool UnderConstruction = false;
    };
    std::vector<CreepSource> CreepSources;
    auto Buildings = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
    for (int Eid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        if (!B || !T || !B->IsOrganic) continue;

        auto* U = EM.GetComponent<UnitComponent>(Eid);
        if (!U || (U->ChkUnitID != 131 && U->ChkUnitID != 132 &&
                   U->ChkUnitID != 133 && U->ChkUnitID != 143))
            continue;

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);

        const auto* Morph = EM.GetComponent<MorphComponent>(Eid);
        const bool IsExistingSourceUpgrade = Morph && Morph->IsMorphing &&
            Morph->SourceUnitID == Eid;
        CreepSources.push_back({Eid, TX, TY,
            B->UnderConstruction && !IsExistingSourceUpgrade});
    }

    const auto InBounds = [&](int X, int Y)
    {
        return X >= 0 && X < mMapWidth && Y >= 0 && Y < mMapHeight;
    };
    const auto CanHaveCreep = [&](int X, int Y)
    {
        if (!InBounds(X, Y)) return false;
        const auto& Tile = sMap->GetTile(X, Y);
        if (!Tile.Buildable) return false;
        if (Y + 1 < mMapHeight && !sMap->GetTile(X, Y + 1).Buildable)
            return false;
        return true;
    };
    const auto SetCreep = [&](int X, int Y)
    {
        if (InBounds(X, Y) && CanHaveCreep(X, Y))
            mCreepGrid[(size_t)Y * (size_t)mMapWidth + (size_t)X] = 1;
    };
    const auto NeighborCount = [&](int X, int Y)
    {
        int Count = 0;
        for (int DY = -1; DY <= 1; ++DY)
            for (int DX = -1; DX <= 1; ++DX)
                if ((DX != 0 || DY != 0) && InBounds(X + DX, Y + DY) &&
                    mCreepGrid[(size_t)(Y + DY) * (size_t)mMapWidth + (size_t)(X + DX)])
                    ++Count;
        return Count;
    };
    const auto SeedBuildingFootprint = [&](const CreepSource& Src)
    {
        // Hatchery/Lair/Hive placement is 4x3 tiles. This mirrors
        // set_creep_building_tiles before incremental SpreadCreep begins.
        const int Left = Src.TileX - 2;
        const int Top = Src.TileY - 1;
        for (int Y = Top; Y < Top + 3; ++Y)
            for (int X = Left; X < Left + 4; ++X)
                SetCreep(X, Y);
    };
    const auto SpreadOne = [&](const CreepSource& Src, CreepSourceState& State)
    {
        std::array<std::vector<size_t>, 8> Targets;
        // Exact OpenBW max creep bounding box: center +/- (320, 200) px.
        for (int Y = std::max(0, Src.TileY - 7);
             Y <= std::min(mMapHeight - 1, Src.TileY + 7); ++Y)
        {
            for (int X = std::max(0, Src.TileX - 10);
                 X <= std::min(mMapWidth - 1, Src.TileX + 10); ++X)
            {
                const size_t Index = (size_t)Y * (size_t)mMapWidth + (size_t)X;
                if (mCreepGrid[Index] || !CanHaveCreep(X, Y)) continue;
                const int Dx = (X - Src.TileX) * 32;
                const int Dy = (Y - Src.TileY) * 32;
                if (Dx * Dx * 25 + Dy * Dy * 64 > 320 * 320 * 25)
                    continue;
                const int Neighbors = NeighborCount(X, Y);
                if (Neighbors > 0)
                    Targets[(size_t)Neighbors - 1].push_back(Index);
            }
        }
        for (int Group = 7; Group >= 0; --Group)
        {
            auto& Candidates = Targets[(size_t)Group];
            if (Candidates.empty()) continue;
            // Deterministic equivalent of OpenBW's LCG selection.
            State.SelectionSequence = State.SelectionSequence * 1103515245u + 12345u;
            const size_t Index = Candidates[(State.SelectionSequence >> 4u) % Candidates.size()];
            mCreepGrid[Index] = 1;
            return true;
        }
        return false;
    };

    std::vector<int> LiveSourceIDs;
    LiveSourceIDs.reserve(CreepSources.size());
    constexpr float SpreadIntervalSeconds = 16.0f / 30.0f;
    for (const auto& Src : CreepSources)
    {
        LiveSourceIDs.push_back(Src.EntityID);
        auto [It, Inserted] = mCreepSourceStates.try_emplace(Src.EntityID);
        auto& State = It->second;
        if (Inserted)
        {
            SeedBuildingFootprint(Src);
            State.SelectionSequence = (uint32_t)Src.EntityID * 2654435761u;
            if (!Src.UnderConstruction)
            {
                while (SpreadOne(Src, State)) {}
                State.Frozen = true;
            }
        }
        if (!State.Frozen && Src.UnderConstruction)
        {
            State.SpreadTimer += DeltaTime;
            while (State.SpreadTimer >= SpreadIntervalSeconds)
            {
                State.SpreadTimer -= SpreadIntervalSeconds;
                if (!SpreadOne(Src, State))
                    break;
            }
        }
        else if (!Src.UnderConstruction)
        {
            // User-visible construction semantics: once the building is
            // complete, preserve the reached creep mask and stop expanding.
            State.Frozen = true;
        }
    }
    for (auto It = mCreepSourceStates.begin(); It != mCreepSourceStates.end();)
    {
        if (std::find(LiveSourceIDs.begin(), LiveSourceIDs.end(), It->first) ==
            LiveSourceIDs.end())
            It = mCreepSourceStates.erase(It);
        else
            ++It;
    }

    mCreepRegressionTimer += DeltaTime;
    if (mCreepRegressionTimer >= 1.0f)
    {
        mCreepRegressionTimer = 0.0f;
        for (int Y = 0; Y < mMapHeight; Y++)
            for (int X = 0; X < mMapWidth; X++)
            {
                if (mCreepGrid[Y * mMapWidth + X] == 0)
                {
                    auto Tile = sMap->GetTile(X, Y);
                    if (Tile.HasCreep)
                    {
                        Tile.HasCreep = false;
                        sMap->SetTile(X, Y, Tile);
                    }
                }
            }
    }

    for (int Y = 0; Y < mMapHeight; Y++)
        for (int X = 0; X < mMapWidth; X++)
        {
            if (mCreepGrid[Y * mMapWidth + X] > 0)
            {
                auto Tile = sMap->GetTile(X, Y);
                Tile.HasCreep = true;
                sMap->SetTile(X, Y, Tile);
            }
        }

    ProcessLarvae(EM, DeltaTime);
    ProcessMorphs(EM, DeltaTime);
    ProcessZergRegeneration(EM, DeltaTime);
}

void ZergSystem::SpreadCreep(Core::ECS::EntityManager& EM)
{
    (void)EM;
}

void ZergSystem::ProcessLarvae(Core::ECS::EntityManager& EM, float DeltaTime)
{
    auto Buildings = EM.GetEntitiesWith<BuildingComponent>();
    for (int Eid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        if (!B || !B->IsOrganic || B->UnderConstruction) continue;

        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        if (!Unit) continue;
        const int BuildingDefID =
            Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        const auto BuildingDef = Game::Races::GetUnitDefByID(BuildingDefID);
        if (BuildingDef.Name != "Hatchery" &&
            BuildingDef.Name != "Lair" &&
            BuildingDef.Name != "Hive")
        {
            continue;
        }

        auto* HatcheryPlayer = EM.GetComponent<PlayerComponent>(Eid);
        auto* HatcheryTrans = EM.GetComponent<TransformComponent>(Eid);
        if (!HatcheryPlayer || !HatcheryTrans)
            continue;

        // Larvae are owned by an individual Hatchery/Lair/Hive in OpenBW.
        // Adopt nearby map-placed larvae that predate the explicit link.
        int ExistingLarvae = 0;
        auto AllUnits = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
        for (int Uid : AllUnits)
        {
            auto* UU = EM.GetComponent<UnitComponent>(Uid);
            auto* UP = EM.GetComponent<PlayerComponent>(Uid);
            if (!UU || !UP || UU->ChkUnitID != 0x23 || UU->HitPoints <= 0 ||
                UP->PlayerID != HatcheryPlayer->PlayerID)
            {
                continue;
            }

            auto* Larva = EM.GetComponent<LarvaComponent>(Uid);
            if (!Larva)
            {
                auto* LarvaTrans = EM.GetComponent<TransformComponent>(Uid);
                if (!LarvaTrans ||
                    LarvaTrans->Position.DistanceTo(HatcheryTrans->Position) > 96.0f)
                {
                    continue;
                }
                Larva = &EM.AddComponent<LarvaComponent>(Uid);
                Larva->ParentHatcheryEntityID = Eid;
            }
            if (Larva->ParentHatcheryEntityID == Eid)
                ExistingLarvae++;
        }

        if (ExistingLarvae < 3)
        {
            B->LarvaTimer += DeltaTime;
            if (B->LarvaTimer >= 15.0f)
            {
                B->LarvaTimer = 0.0f;
                B->LarvaCount++;

                auto* Trans = EM.GetComponent<TransformComponent>(Eid);
                if (Trans)
                {
                    int LarvaEid = EM.CreateEntity();
                    auto& LTrans = EM.AddComponent<TransformComponent>(LarvaEid);
                    auto& LUnit = EM.AddComponent<UnitComponent>(LarvaEid);
                    auto& LPlayer = EM.AddComponent<PlayerComponent>(LarvaEid);
                    auto& LRender = EM.AddComponent<RenderComponent>(LarvaEid);
                    auto& LLarva = EM.AddComponent<LarvaComponent>(LarvaEid);

                    LTrans.Position = Trans->Position;
                    LTrans.Position.X += (float)((B->LarvaCount % 3) * 24.0f - 24.0f);
                    LTrans.Position.Y += (float)((B->LarvaCount / 3) * 24.0f);
                    LUnit.UnitRace = Race::Zerg;
                    LUnit.ChkUnitID = 0x23;
                    LUnit.HitPoints = 25;
                    LUnit.MaxHitPoints = 25;
                    LUnit.SupplyCost = 0;
                    LUnit.Speed = 0.0f;
                    LUnit.SightRange = 4;
                    LRender.ImageID = ResolveImageID(LUnit.ChkUnitID);
                    LLarva.ParentHatcheryEntityID = Eid;

                    auto* P = EM.GetComponent<PlayerComponent>(Eid);
                    if (P) LPlayer = *P;

                    Logger::Info("ZergSystem", "Spawned larva " + std::to_string(LarvaEid) +
                        " near hatchery " + std::to_string(Eid));
                }
            }
        }
    }
}

// ============================================================
// Drone morph into building
// ============================================================

bool ZergSystem::CanMorphDroneIntoBuilding(Core::ECS::EntityManager& EM,
                                            int DroneID, int BuildingDefID,
                                            std::string* OutReason)
{
    auto* Unit = EM.GetComponent<UnitComponent>(DroneID);
    auto* Player = EM.GetComponent<PlayerComponent>(DroneID);
    if (!Unit || !Player) return false;
    if (Unit->UnitRace != Race::Zerg) return false;
    if (Unit->ChkUnitID != 0x29) // Drone
    {
        if (OutReason) *OutReason = "not a drone";
        return false;
    }
    if (Unit->HitPoints <= 0)
    {
        if (OutReason) *OutReason = "drone is dead";
        return false;
    }

    const auto& BldgDef = Game::Races::GetUnitDefByID(BuildingDefID);
    if (BldgDef.Name.empty() || !BldgDef.IsBuilding)
    {
        if (OutReason) *OutReason = "not a building";
        return false;
    }

    auto& Res = HarvestSystem::GetResources(Player->PlayerID);
    if (Res.Minerals < BldgDef.MineralCost || Res.VespeneGas < BldgDef.GasCost)
    {
        if (OutReason) *OutReason = "not enough resources";
        return false;
    }

    // Check required buildings (predecessor tech)
    if (!HasRequiredBuildings(EM, Player->PlayerID, BldgDef.RequiredBuildings,
                              BldgDef.RaceType, OutReason))
        return false;

    return true;
}

bool ZergSystem::StartMorphDroneIntoBuilding(Core::ECS::EntityManager& EM,
                                              int DroneID, int BuildingDefID,
                                              const Core::Math::Vec2& Position)
{
    std::string Reason;
    if (!CanMorphDroneIntoBuilding(EM, DroneID, BuildingDefID, &Reason))
    {
        Logger::Debug("ZergSystem", "Cannot morph drone: " + Reason);
        return false;
    }

    auto* Player = EM.GetComponent<PlayerComponent>(DroneID);
    int PlayerID = Player ? Player->PlayerID : 0;

    const auto& BldgDef = Game::Races::GetUnitDefByID(BuildingDefID);

    // Keep the rule at the actual morph boundary as well as in placement
    // preview/AI search, so direct commands cannot bypass creep validation.
    if (BldgDef.RequiresCreep &&
        !HasCreepAcrossBuildingFootprint(BldgDef.ChkUnitID, Position))
    {
        Logger::Debug("ZergSystem", "Cannot morph drone: building footprint requires creep");
        return false;
    }

    // Deduct costs
    auto& Res = HarvestSystem::GetResources(PlayerID);
    Res.Minerals -= BldgDef.MineralCost;
    Res.VespeneGas -= BldgDef.GasCost;
    if (auto* DroneUnit = EM.GetComponent<UnitComponent>(DroneID))
        Res.SupplyUsed = std::max(0, Res.SupplyUsed - DroneUnit->SupplyCost);

    // Create building entity
    int BldgID = EM.CreateEntity();
    auto& BTrans = EM.AddComponent<TransformComponent>(BldgID);
    auto& BUnit = EM.AddComponent<UnitComponent>(BldgID);
    auto& BPlayer = EM.AddComponent<PlayerComponent>(BldgID);
    auto& BBuild = EM.AddComponent<BuildingComponent>(BldgID);
    auto& BPath = EM.AddComponent<PathfindingComponent>(BldgID);
    auto& BRender = EM.AddComponent<RenderComponent>(BldgID);

    BTrans.Position = Position;
    BPlayer.PlayerID = PlayerID;

    BUnit.ChkUnitID = BldgDef.ChkUnitID;
    BUnit.UnitRace = Race::Zerg;
    BUnit.HitPoints = std::max(1, BldgDef.HitPoints / 10); // 10% HP while building
    BUnit.MaxHitPoints = BldgDef.HitPoints;
    BUnit.ShieldPoints = BldgDef.ShieldPoints;
    BUnit.MaxShieldPoints = BldgDef.ShieldPoints;
    BUnit.EnergyPoints = BldgDef.EnergyPoints;
    BUnit.MaxEnergyPoints = BldgDef.EnergyPoints;
    BUnit.Armor = BldgDef.Armor;
    BUnit.SupplyCost = 0;
    BUnit.Speed = 0.0f;
    BUnit.SightRange = 4;
    BUnit.SizeType = BldgDef.SizeType;
    BRender.ImageID = ResolveImageID(BUnit.ChkUnitID);

    BBuild.IsBuilding = true;
    BBuild.UnderConstruction = true;
    BBuild.BuildProgress = 0.0f;
    BBuild.TotalBuildTime = (float)BldgDef.BuildTime;
    BBuild.RequiresCreep = BldgDef.RequiresCreep;
    BBuild.RequiresPylon = BldgDef.RequiresPylon;
    BBuild.IsOrganic = true; // Zerg buildings are organic
    BBuild.BuildingDefID = BuildingDefID;
    BBuild.BuildTimeRemaining = (float)BldgDef.BuildTime;
    ApplyCollisionBounds(BPath, ResolveCollisionBounds(BldgDef.ChkUnitID));

    // Add morph component so ProcessMorphs handles completion
    auto& DroneMorph = EM.AddComponent<MorphComponent>(BldgID);
    DroneMorph.MorphTargetDefID = BuildingDefID;
    DroneMorph.MorphDuration = (float)BldgDef.BuildTime;
    DroneMorph.MorphTimer = (float)BldgDef.BuildTime;
    DroneMorph.IsMorphing = true;
    DroneMorph.SourceUnitID = DroneID;

    // Consume the Drone (destroy it)
    EM.DestroyEntity(DroneID);

    Logger::Info("ZergSystem",
        "Player " + std::to_string(PlayerID) + " started morphing " +
        BldgDef.Name + " at (" + std::to_string((int)Position.X) + "," +
        std::to_string((int)Position.Y) + "), drone consumed");

    return true;
}

// ============================================================
// Larva morph into unit
// ============================================================

bool ZergSystem::CanMorphLarvaIntoUnit(Core::ECS::EntityManager& EM,
                                        int LarvaID, int UnitDefID,
                                        std::string* OutReason)
{
    auto* Unit = EM.GetComponent<UnitComponent>(LarvaID);
    auto* Player = EM.GetComponent<PlayerComponent>(LarvaID);
    if (!Unit || !Player) return false;
    if (Unit->ChkUnitID != 0x23) // Larva
    {
        if (OutReason) *OutReason = "not a larva";
        return false;
    }
    if (Unit->HitPoints <= 0)
    {
        if (OutReason) *OutReason = "larva is dead";
        return false;
    }

    const auto& UnitDef = Game::Races::GetUnitDefByID(UnitDefID);
    if (UnitDef.Name.empty() || UnitDef.IsBuilding)
    {
        if (OutReason) *OutReason = "not a valid zerg unit";
        return false;
    }
    if (UnitDef.RaceType != Race::Zerg)
    {
        if (OutReason) *OutReason = "not a zerg unit";
        return false;
    }

    // Larva and egg are source/intermediate forms, not larva morph targets.
    if (UnitDef.Name == "Larva" || UnitDef.Name == "Egg")
    {
        if (OutReason) *OutReason = "this unit cannot morph from larva";
        return false;
    }

    auto& Res = HarvestSystem::GetResources(Player->PlayerID);
    if (Res.Minerals < UnitDef.MineralCost || Res.VespeneGas < UnitDef.GasCost)
    {
        if (OutReason) *OutReason = "not enough resources";
        return false;
    }

    // Check supply
    if (Res.SupplyUsed + UnitDef.SupplyCost > Res.SupplyMax)
    {
        if (OutReason) *OutReason = "not enough supply";
        return false;
    }

    // Check required buildings
    if (!HasRequiredBuildings(EM, Player->PlayerID, UnitDef.RequiredBuildings,
                              UnitDef.RaceType, OutReason))
        return false;

    return true;
}

bool ZergSystem::StartMorphLarvaIntoUnit(Core::ECS::EntityManager& EM,
                                          int LarvaID, int UnitDefID)
{
    std::string Reason;
    if (!CanMorphLarvaIntoUnit(EM, LarvaID, UnitDefID, &Reason))
    {
        Logger::Debug("ZergSystem", "Cannot morph larva: " + Reason);
        return false;
    }

    auto* Player = EM.GetComponent<PlayerComponent>(LarvaID);
    auto* LarvaTrans = EM.GetComponent<TransformComponent>(LarvaID);
    auto* LarvaOwner = EM.GetComponent<LarvaComponent>(LarvaID);
    int PlayerID = Player ? Player->PlayerID : 0;

    const auto& UnitDef = Game::Races::GetUnitDefByID(UnitDefID);

    // Deduct costs
    auto& Res = HarvestSystem::GetResources(PlayerID);
    Res.Minerals -= UnitDef.MineralCost;
    Res.VespeneGas -= UnitDef.GasCost;
    Res.SupplyUsed += UnitDef.SupplyCost;

    // Create egg entity
    int EggID = EM.CreateEntity();
    auto& ETrans = EM.AddComponent<TransformComponent>(EggID);
    auto& EUnit = EM.AddComponent<UnitComponent>(EggID);
    auto& EPlayer = EM.AddComponent<PlayerComponent>(EggID);
    auto& EBuild = EM.AddComponent<BuildingComponent>(EggID);
    auto& EMorph = EM.AddComponent<MorphComponent>(EggID);
    auto& EPath = EM.AddComponent<PathfindingComponent>(EggID);
    auto& ERender = EM.AddComponent<RenderComponent>(EggID);
    auto& ELarva = EM.AddComponent<LarvaComponent>(EggID);

    ETrans.Position = LarvaTrans ? LarvaTrans->Position : Core::Math::Vec2();
    EPlayer.PlayerID = PlayerID;
    ELarva.ParentHatcheryEntityID =
        LarvaOwner ? LarvaOwner->ParentHatcheryEntityID : -1;

    EUnit.ChkUnitID = 0x24; // Egg
    EUnit.UnitRace = Race::Zerg;
    EUnit.HitPoints = (int)((float)UnitDef.HitPoints * 0.5f);
    EUnit.MaxHitPoints = UnitDef.HitPoints;
    EUnit.ShieldPoints = UnitDef.ShieldPoints;
    EUnit.MaxShieldPoints = UnitDef.ShieldPoints;
    EUnit.EnergyPoints = UnitDef.EnergyPoints;
    EUnit.MaxEnergyPoints = UnitDef.EnergyPoints;
    EUnit.Armor = UnitDef.Armor;
    EUnit.SupplyCost = 0;
    EUnit.Speed = 0.0f;
    EUnit.SightRange = 2;
    EUnit.SizeType = UnitSize::Medium;
    ERender.ImageID = ResolveImageID(EUnit.ChkUnitID);

    EBuild.IsBuilding = true;
    EBuild.UnderConstruction = true;
    EBuild.BuildProgress = 0.0f;
    EBuild.TotalBuildTime = (float)UnitDef.BuildTime;
    EBuild.IsOrganic = true;
    EBuild.BuildingDefID = -1;
    EBuild.BuildTimeRemaining = (float)UnitDef.BuildTime;
    ApplyCollisionBounds(EPath, ResolveCollisionBounds(EUnit.ChkUnitID));

    EMorph.MorphTargetDefID = UnitDefID;
    EMorph.MorphDuration = (float)UnitDef.BuildTime;
    EMorph.MorphTimer = (float)UnitDef.BuildTime;
    EMorph.IsMorphing = true;
    EMorph.SourceUnitID = LarvaID;

    // Consume the Larva
    EM.DestroyEntity(LarvaID);

    Logger::Info("ZergSystem",
        "Player " + std::to_string(PlayerID) + " started morphing " +
        UnitDef.Name + " from larva (egg " + std::to_string(EggID) + ")");

    return true;
}

// ============================================================
// Building morph (Hatchery -> Lair -> Hive, Spire -> Greater Spire, etc.)
// ============================================================

bool ZergSystem::CanMorphBuilding(Core::ECS::EntityManager& EM,
                                   int BuildingID, int TargetDefID,
                                   std::string* OutReason)
{
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    auto* Unit = EM.GetComponent<UnitComponent>(BuildingID);
    auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    if (!Bldg || !Unit || !Player) return false;
    if (Unit->UnitRace != Race::Zerg) return false;
    if (Bldg->UnderConstruction)
    {
        if (OutReason) *OutReason = "building is still under construction";
        return false;
    }

    const auto& TargetDef = Game::Races::GetUnitDefByID(TargetDefID);
    if (TargetDef.Name.empty() || !TargetDef.IsBuilding || TargetDef.RaceType != Race::Zerg)
    {
        if (OutReason) *OutReason = "not a valid building morph target";
        return false;
    }

    const int SourceDefID = ResolveBuildingDefID(EM, BuildingID);
    if (!IsValidZergBuildingMorph(SourceDefID, TargetDefID))
    {
        if (OutReason) *OutReason = "invalid building morph source";
        return false;
    }

    if (!HasRequiredBuildings(EM, Player->PlayerID, TargetDef.RequiredBuildings,
                              TargetDef.RaceType, OutReason))
        return false;

    // Check if morph is already in progress
    auto* Morph = EM.GetComponent<MorphComponent>(BuildingID);
    if (Morph && Morph->IsMorphing)
    {
        if (OutReason) *OutReason = "already morphing";
        return false;
    }

    auto& Res = HarvestSystem::GetResources(Player->PlayerID);
    if (Res.Minerals < TargetDef.MineralCost || Res.VespeneGas < TargetDef.GasCost)
    {
        if (OutReason) *OutReason = "not enough resources";
        return false;
    }

    return true;
}

bool ZergSystem::StartMorphBuilding(Core::ECS::EntityManager& EM,
                                     int BuildingID, int TargetDefID)
{
    std::string Reason;
    if (!CanMorphBuilding(EM, BuildingID, TargetDefID, &Reason))
    {
        Logger::Debug("ZergSystem", "Cannot morph building: " + Reason);
        return false;
    }

    auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    int PlayerID = Player ? Player->PlayerID : 0;

    const auto& TargetDef = Game::Races::GetUnitDefByID(TargetDefID);

    auto& Res = HarvestSystem::GetResources(PlayerID);
    Res.Minerals -= TargetDef.MineralCost;
    Res.VespeneGas -= TargetDef.GasCost;

    // Set morph state on building
    auto& Morph = EM.AddComponent<MorphComponent>(BuildingID);
    Morph.MorphTargetDefID = TargetDefID;
    Morph.MorphDuration = (float)TargetDef.BuildTime;
    Morph.MorphTimer = (float)TargetDef.BuildTime;
    Morph.IsMorphing = true;
    Morph.SourceUnitID = BuildingID;

    // Set building progress
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    if (Bldg)
    {
        Bldg->BuildProgress = 0.0f;
        Bldg->TotalBuildTime = std::max(1.0f, (float)TargetDef.BuildTime);
        Bldg->BuildTimeRemaining = (float)TargetDef.BuildTime;
        Bldg->UnderConstruction = true;
    }

    // Use the target building's authored construction sequence immediately,
    // just as a newly placed Zerg building does.  Gameplay identity remains
    // the source type until completion so the upgrading base can still
    // receive workers and resources.
    auto* Render = EM.GetComponent<RenderComponent>(BuildingID);
    if (!Render)
        Render = &EM.AddComponent<RenderComponent>(BuildingID);
    Render->ImageID = ResolveImageID(TargetDef.ChkUnitID);
    Render->FrameIndex = 0;
    Render->TotalFrames = 1;

    Logger::Info("ZergSystem",
        "Player " + std::to_string(PlayerID) + " started morphing building into " +
        TargetDef.Name);

    return true;
}

// ============================================================
// Morph processing
// ============================================================

void ZergSystem::ProcessMorphs(Core::ECS::EntityManager& EM, float DeltaTime)
{
    auto Entities = EM.GetEntitiesWith<MorphComponent>();
    for (int Eid : Entities)
    {
        auto* Morph = EM.GetComponent<MorphComponent>(Eid);
        auto* Bldg = EM.GetComponent<BuildingComponent>(Eid);
        if (!Morph || !Morph->IsMorphing) continue;

        Morph->MorphTimer = std::max(0.0f, Morph->MorphTimer - DeltaTime);

        // Update building progress for HP scaling (eggs and buildings)
        if (Bldg)
        {
            const float Duration = std::max(1.0f, Morph->MorphDuration);
            Bldg->TotalBuildTime = Duration;
            Bldg->BuildTimeRemaining = Morph->MorphTimer;
            Bldg->BuildProgress = std::clamp(
                Morph->MorphDuration - Morph->MorphTimer, 0.0f, Duration);
        }

        if (Morph->MorphTimer <= 0.0f)
        {
            Morph->MorphTimer = 0.0f;
            Morph->IsMorphing = false;
            CompleteMorph(EM, Eid);
        }
    }
}

void ZergSystem::CompleteMorph(Core::ECS::EntityManager& EM, int MorphEntityID)
{
    auto* Morph = EM.GetComponent<MorphComponent>(MorphEntityID);
    if (!Morph) return;

    int TargetDefID = Morph->MorphTargetDefID;
    const auto& TargetDef = Game::Races::GetUnitDefByID(TargetDefID);
    if (TargetDef.Name.empty()) return;

    auto* Unit = EM.GetComponent<UnitComponent>(MorphEntityID);
    auto* Bldg = EM.GetComponent<BuildingComponent>(MorphEntityID);
    auto* Player = EM.GetComponent<PlayerComponent>(MorphEntityID);

    if (TargetDef.IsBuilding)
    {
        // Building morph (e.g. Hatchery -> Lair, Spire -> Greater Spire)
        // Update the existing building entity to the new type
        if (Unit)
        {
            Unit->ChkUnitID = TargetDef.ChkUnitID;
            Unit->MaxHitPoints = TargetDef.HitPoints;
            Unit->HitPoints = TargetDef.HitPoints;
            Unit->MaxShieldPoints = TargetDef.ShieldPoints;
            Unit->ShieldPoints = TargetDef.ShieldPoints;
            Unit->Armor = TargetDef.Armor;
            Unit->SupplyCost = TargetDef.SupplyCost;
            Unit->SightRange = TargetDef.SightRange;
            Unit->Speed = TargetDef.Speed;
            Unit->IsDetector = Game::Races::IsDetectorChkUnitID(Unit->ChkUnitID);
        }
        if (Bldg)
        {
            Bldg->UnderConstruction = false;
            Bldg->BuildingDefID = TargetDefID;
            Bldg->BuildTimeRemaining = 0.0f;
            Bldg->BuildProgress = Bldg->TotalBuildTime;
            Bldg->IsResearching = false;
        }
        if (Player && Morph->SourceUnitID != MorphEntityID)
        {
            HarvestSystem::GetResources(Player->PlayerID).SupplyMax +=
                Game::Races::GetSupplyProvidedByDefID(TargetDefID);
        }
        if (auto* Path = EM.GetComponent<PathfindingComponent>(MorphEntityID))
        {
            Path->IsAir = false;
            ApplyCollisionBounds(*Path, ResolveCollisionBounds(TargetDef.ChkUnitID));
        }
        auto* Render = EM.GetComponent<RenderComponent>(MorphEntityID);
        if (!Render)
            Render = &EM.AddComponent<RenderComponent>(MorphEntityID);
        Render->ImageID = ResolveImageID(TargetDef.ChkUnitID);

        Logger::Info("ZergSystem",
            "Building morph complete: " + TargetDef.Name +
            " (entity " + std::to_string(MorphEntityID) + ")");
    }
    else
    {
        // Unit morph (egg -> unit)
        // Destroy the egg, spawn the unit
        if (!Player) return;

        Core::Math::Vec2 SpawnPos;
        if (auto* Trans = EM.GetComponent<TransformComponent>(MorphEntityID))
            SpawnPos = Trans->Position;
        if (const auto* Larva = EM.GetComponent<LarvaComponent>(MorphEntityID);
            Larva && Larva->ParentHatcheryEntityID >= 0 &&
            EM.IsAlive(Larva->ParentHatcheryEntityID))
        {
            SpawnPos = ProductionSystem::FindSpawnPosition(
                EM, Larva->ParentHatcheryEntityID, TargetDef.ChkUnitID,
                MorphEntityID);
        }

        int PlayerID = Player->PlayerID;

        // Spawn the morphed unit
        int NewUnitID = EM.CreateEntity();
        auto& NTrans = EM.AddComponent<TransformComponent>(NewUnitID);
        auto& NUnit = EM.AddComponent<UnitComponent>(NewUnitID);
        auto& NPlayer = EM.AddComponent<PlayerComponent>(NewUnitID);
        auto& NCmd = EM.AddComponent<CommandComponent>(NewUnitID);
        auto& NPath = EM.AddComponent<PathfindingComponent>(NewUnitID);
        auto& NRender = EM.AddComponent<RenderComponent>(NewUnitID);

        NTrans.Position = SpawnPos;
        NPlayer.PlayerID = PlayerID;

        NUnit.ChkUnitID = TargetDef.ChkUnitID;
        NUnit.UnitRace = Race::Zerg;
        NUnit.HitPoints = TargetDef.HitPoints;
        NUnit.MaxHitPoints = TargetDef.HitPoints;
        NUnit.ShieldPoints = TargetDef.ShieldPoints;
        NUnit.MaxShieldPoints = TargetDef.ShieldPoints;
        NUnit.EnergyPoints = TargetDef.EnergyPoints;
        NUnit.MaxEnergyPoints = TargetDef.EnergyPoints;
        NUnit.Armor = TargetDef.Armor;
        NUnit.SupplyCost = TargetDef.SupplyCost;
        NUnit.Speed = TargetDef.Speed;
        NUnit.SightRange = TargetDef.SightRange;
        NUnit.SizeType = TargetDef.SizeType;
        NUnit.IsDetector = Game::Races::IsDetectorChkUnitID(NUnit.ChkUnitID);
        NRender.ImageID = ResolveImageID(NUnit.ChkUnitID);
        UpgradeSystem::RefreshUnitEnergyCap(EM, NewUnitID);

        HarvestSystem::GetResources(PlayerID).SupplyMax +=
            Game::Races::GetSupplyProvidedByDefID(TargetDefID);

        NCmd.CurrentCommand = CommandType::Stop;
        NPath.NodeTolerance = 8.0f;
        NPath.IsAir = Game::Data::IsAirUnitType(TargetDef.ChkUnitID);
        ApplyCollisionBounds(NPath, ResolveCollisionBounds(TargetDef.ChkUnitID));

        // Add combat component if unit has weapons
        if (TargetDef.GroundDamage > 0 || TargetDef.AirDamage > 0)
        {
            auto& NCombat = EM.AddComponent<CombatComponent>(NewUnitID);
            Races::ApplyCombatDefinition(
                NCombat, TargetDef, Game::Data::IsAirUnitType(TargetDef.ChkUnitID));
        }

        // The project assets are authoritative for whether this unit exposes
        // both Burrow and UnBurrow iscript entry points.
        if (CanUnitBurrow(NUnit.ChkUnitID, NRender.ImageID))
        {
            auto& NBurrow = EM.AddComponent<BurrowComponent>(NewUnitID);
            NBurrow.CanBurrow = true;
            NBurrow.IsBurrowed = false;
            NBurrow.Phase = BurrowPhase::Surface;
            NBurrow.BurrowTransitionTime = 0.0f;
        }

        // Destroy the egg
        EM.DestroyEntity(MorphEntityID);

        Logger::Info("ZergSystem",
            "Unit morph complete: " + TargetDef.Name +
            " (entity " + std::to_string(NewUnitID) + ")");
    }
}

bool ZergSystem::BurrowUnit(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID);
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Burrow || !Unit || !Burrow->CanBurrow || Unit->HitPoints <= 0)
        return false;
    if (Burrow->IsTransitioning())
        return false;

    if (auto* Command = EM.GetComponent<CommandComponent>(EntityID))
        Command->CancelAll();

    const bool FullyBurrowed =
        Burrow->IsBurrowed || Burrow->Phase == BurrowPhase::Burrowed;
    return BurrowState::BeginTransition(EM, EntityID, !FullyBurrowed);
}

void ZergSystem::ProcessZergRegeneration(Core::ECS::EntityManager& EM, float DeltaTime)
{
    auto Units = EM.GetEntitiesWith<UnitComponent, TransformComponent>();
    for (int Eid : Units)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        if (!Unit || !Trans) continue;
        if (Unit->UnitRace != Race::Zerg) continue;
        if (Unit->HitPoints <= 0 || Unit->HitPoints >= Unit->MaxHitPoints) continue;

        // Check if on creep (buildings or ground units on creep tiles regen)
        bool OnCreep = false;
        int TX = (int)(Trans->Position.X / 32.0f);
        int TY = (int)(Trans->Position.Y / 32.0f);
        if (HasCreep(TX, TY)) OnCreep = true;

        if (OnCreep)
        {
            // Zerg regen: ~4 HP/sec on creep
            float RegenAmount = 4.0f * DeltaTime;
            Unit->HitPoints = std::min(Unit->MaxHitPoints,
                Unit->HitPoints + (int)std::ceil(RegenAmount));
        }
    }
}

} // namespace Game::Systems
