#include "Game/Systems/ZergSystem.h"
#include "Game/Systems/BuildingSystem.h"
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

    std::pair<int, int> ZergBuildingFootprintTiles(int ChkUnitID)
    {
        switch (ChkUnitID)
        {
            case 131: // Hatchery
            case 132: // Lair
            case 133: // Hive
                return {4, 3};
            case 149: // Extractor
                return {4, 2};
            case 137: // Greater Spire
            case 141: // Spire
            case 143: // Creep Colony
            case 144: // Spore Colony
            case 146: // Sunken Colony
                return {2, 2};
            default:
                return {3, 2};
        }
    }

    bool IsCreepSpreadingBuilding(int ChkUnitID)
    {
        return ChkUnitID == 131 || ChkUnitID == 132 ||
               ChkUnitID == 133 || ChkUnitID == 143 ||
               ChkUnitID == 144 || ChkUnitID == 146;
    }

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
        const auto [Width, Height] = ZergBuildingFootprintTiles(ChkUnitID);
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
        return EM.AnyEntityWith<BuildingComponent, PlayerComponent>(
            [&](int Bid)
            {
                auto* B = EM.GetComponent<BuildingComponent>(Bid);
                auto* BP = EM.GetComponent<PlayerComponent>(Bid);
                if (!B || !BP || BP->PlayerID != PlayerID || B->UnderConstruction)
                    return false;

                return Game::Races::BuildingDefSatisfiesRequirement(
                    ResolveBuildingDefID(EM, Bid), RequiredDefID);
            });
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
        case 50:  // Infested Terran
        case 51:  // Hero Infested Kerrigan
        case 52:  // Hero Unclean One (Defiler)
        case 53:  // Hero Hunter Killer (Hydralisk)
        case 54:  // Hero Devouring One (Zergling)
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

    // Authored Burrow/UnBurrow scripts normally finish through sigorder 4 in
    // AnimationSystem. Keep a gameplay-side timeout so a missing/broken script
    // cannot leave the unit permanently unable to move, attack, or advance its
    // queued order.
    for (int EntityID : EM.GetEntitiesWith<BurrowComponent>())
    {
        auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID);
        if (!Burrow || !Burrow->IsTransitioning())
            continue;
        Burrow->BurrowTransitionTime =
            std::max(0.0f, Burrow->BurrowTransitionTime - DeltaTime);
        if (Burrow->BurrowTransitionTime > 0.0f)
            continue;
        if (BurrowState::CompleteTransition(EM, EntityID))
        {
            Logger::Warning(
                "ZergSystem", "Burrow transition timed out; completed fallback for entity " +
                    std::to_string(EntityID));
        }
    }

    if (!mGridInitialized && sMap)
    {
        mMapWidth = sMap->GetWidth();
        mMapHeight = sMap->GetHeight();
        mCreepGrid.resize(mMapWidth * mMapHeight, 0);
        mCreepRecedingGrid.resize(mCreepGrid.size(), 0);
        for (int Y = 0; Y < mMapHeight; ++Y)
            for (int X = 0; X < mMapWidth; ++X)
                mCreepGrid[(size_t)Y * (size_t)mMapWidth + (size_t)X] =
                    sMap->GetTile(X, Y).HasCreep ? 1u : 0u;
        mGridInitialized = true;
    }

    if (!sMap || mCreepGrid.empty())
    {
        ProcessLarvae(EM, DeltaTime);
        ProcessZergRegeneration(EM, DeltaTime);
        ProcessMorphs(EM, DeltaTime);
        return;
    }

    // Only Hatchery/Lair/Hive and the three colony forms expand creep. Every
    // living grounded Zerg building is still a provider for the creep under
    // its own footprint, matching OpenBW's creep_is_being_sustained query.
    struct CreepProvider
    {
        int EntityID = -1;
        int TileX = 0;
        int TileY = 0;
        int FootprintWidth = 0;
        int FootprintHeight = 0;
        bool SustainsFullEllipse = false;
        bool UnderConstruction = false;
    };
    std::vector<CreepProvider> CreepProviders;
    std::vector<CreepProvider> CreepSources;
    auto Buildings = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
    for (int Eid : Buildings)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* T = EM.GetComponent<TransformComponent>(Eid);
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        if (!B || !T || !U || U->HitPoints <= 0 ||
            U->UnitRace != Race::Zerg || B->IsLifted)
        {
            continue;
        }

        const int DefID = Game::Races::GetUnitDefIDByChkUnitID(U->ChkUnitID);
        if (DefID < 0 || !Game::Races::GetUnitDefByID(DefID).IsBuilding)
            continue; // Eggs use BuildingComponent only as morph progress state.

        int TX = (int)(T->Position.X / 32.0f);
        int TY = (int)(T->Position.Y / 32.0f);
        const auto [FootprintWidth, FootprintHeight] =
            ZergBuildingFootprintTiles(U->ChkUnitID);
        const auto* Morph = EM.GetComponent<MorphComponent>(Eid);
        const bool IsExistingSourceUpgrade = Morph && Morph->IsMorphing &&
            Morph->SourceUnitID == Eid;
        const bool SpreadsCreep = IsCreepSpreadingBuilding(U->ChkUnitID);
        CreepProvider Provider{Eid, TX, TY, FootprintWidth, FootprintHeight,
            SpreadsCreep, B->UnderConstruction && !IsExistingSourceUpgrade};
        CreepProviders.push_back(Provider);
        if (SpreadsCreep)
            CreepSources.push_back(Provider);
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
    const auto SeedBuildingFootprint = [&](const CreepProvider& Src)
    {
        // Seed the actual footprint before incremental spread begins.
        const int Left = Src.TileX - Src.FootprintWidth / 2;
        const int Top = Src.TileY - Src.FootprintHeight / 2;
        for (int Y = Top; Y < Top + Src.FootprintHeight; ++Y)
            for (int X = Left; X < Left + Src.FootprintWidth; ++X)
                SetCreep(X, Y);
    };
    const auto SpreadOne = [&](const CreepProvider& Src, CreepProviderState& State)
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

    constexpr float SpreadIntervalSeconds = 16.0f / 30.0f;
    for (const auto& Src : CreepSources)
    {
        auto [It, Inserted] = mCreepProviderStates.try_emplace(Src.EntityID);
        auto& State = It->second;
        State.TileX = Src.TileX;
        State.TileY = Src.TileY;
        State.FootprintWidth = Src.FootprintWidth;
        State.FootprintHeight = Src.FootprintHeight;
        State.SustainsFullEllipse = true;
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

    std::vector<int> LiveProviderIDs;
    LiveProviderIDs.reserve(CreepProviders.size());
    for (const auto& Provider : CreepProviders)
    {
        LiveProviderIDs.push_back(Provider.EntityID);
        auto [It, Inserted] =
            mCreepProviderStates.try_emplace(Provider.EntityID);
        auto& State = It->second;
        State.TileX = Provider.TileX;
        State.TileY = Provider.TileY;
        State.FootprintWidth = Provider.FootprintWidth;
        State.FootprintHeight = Provider.FootprintHeight;
        State.SustainsFullEllipse = Provider.SustainsFullEllipse;
        if (Inserted)
        {
            State.SelectionSequence =
                (uint32_t)Provider.EntityID * 2654435761u;
            State.Frozen = !Provider.SustainsFullEllipse;
        }
    }

    const auto VisitProviderTiles = [&](int TileX,
                                        int TileY,
                                        int FootprintWidth,
                                        int FootprintHeight,
                                        bool FullEllipse,
                                        auto&& Visitor)
    {
        if (!FullEllipse)
        {
            const int Left = TileX - FootprintWidth / 2;
            const int Top = TileY - FootprintHeight / 2;
            for (int Y = Top; Y < Top + FootprintHeight; ++Y)
                for (int X = Left; X < Left + FootprintWidth; ++X)
                    if (InBounds(X, Y))
                        Visitor(X, Y);
            return;
        }

        for (int Y = std::max(0, TileY - 7);
             Y <= std::min(mMapHeight - 1, TileY + 7); ++Y)
        {
            for (int X = std::max(0, TileX - 10);
                 X <= std::min(mMapWidth - 1, TileX + 10); ++X)
            {
                const int DX = (X - TileX) * 32;
                const int DY = (Y - TileY) * 32;
                if (DX * DX * 25 + DY * DY * 64 <= 320 * 320 * 25)
                    Visitor(X, Y);
            }
        }
    };

    // Expanding providers sustain their ellipse; every other living grounded
    // Zerg building sustains only its occupied footprint. No ordinary tech
    // building can chain new creep beyond the field that already exists.
    std::vector<uint8_t> SustainedCreep(mCreepGrid.size(), 0);
    for (const auto& Provider : CreepProviders)
    {
        VisitProviderTiles(
            Provider.TileX, Provider.TileY,
            Provider.FootprintWidth, Provider.FootprintHeight,
            Provider.SustainsFullEllipse,
            [&](int X, int Y)
            {
                const size_t Index =
                    (size_t)Y * (size_t)mMapWidth + (size_t)X;
                if (mCreepGrid[Index])
                    SustainedCreep[Index] = 1;
            });
    }

    // A source appearing over tiles that were already receding sustains them
    // again. OpenBW clears the receding state instead of allowing those tiles
    // to disappear underneath a new provider.
    for (size_t Index = 0; Index < SustainedCreep.size(); ++Index)
        if (SustainedCreep[Index])
            mCreepRecedingGrid[Index] = 0;

    // Only tiles inside a removed provider's maximum support area begin to
    // recede. Pre-placed map creep outside that area remains untouched; creep
    // inside it follows the same OpenBW removal rule as dynamically grown
    // creep. A normal building's recorded footprint becomes eligible here
    // after that building itself is destroyed.
    for (auto It = mCreepProviderStates.begin();
         It != mCreepProviderStates.end();)
    {
        if (std::find(LiveProviderIDs.begin(), LiveProviderIDs.end(), It->first) !=
            LiveProviderIDs.end())
        {
            ++It;
            continue;
        }

        const auto& RemovedProvider = It->second;
        VisitProviderTiles(
            RemovedProvider.TileX, RemovedProvider.TileY,
            RemovedProvider.FootprintWidth, RemovedProvider.FootprintHeight,
            RemovedProvider.SustainsFullEllipse,
            [&](int X, int Y)
            {
                const size_t Index =
                    (size_t)Y * (size_t)mMapWidth + (size_t)X;
                if (mCreepGrid[Index] && !SustainedCreep[Index])
                    mCreepRecedingGrid[Index] = 1;
            });
        It = mCreepProviderStates.erase(It);
    }

    const auto RecedeOneUnsupportedBoundaryTile = [&]()
    {
        int BestNeighborCount = 10;
        std::vector<size_t> Candidates;
        for (int Y = 0; Y < mMapHeight; ++Y)
        {
            for (int X = 0; X < mMapWidth; ++X)
            {
                const size_t Index =
                    (size_t)Y * (size_t)mMapWidth + (size_t)X;
                if (!mCreepGrid[Index] || !mCreepRecedingGrid[Index])
                    continue;

                const int Neighbors = NeighborCount(X, Y);
                if (Neighbors < BestNeighborCount)
                {
                    BestNeighborCount = Neighbors;
                    Candidates.clear();
                }
                if (Neighbors == BestNeighborCount)
                    Candidates.push_back(Index);
            }
        }
        if (Candidates.empty())
            return false;

        // OpenBW selects a tile from the least-supported bucket. Keep that
        // choice deterministic for repeatable simulations and regression tests.
        mCreepRegressionSequence =
            mCreepRegressionSequence * 1103515245u + 12345u;
        const size_t Candidate = Candidates[
            (mCreepRegressionSequence >> 4u) % Candidates.size()];
        mCreepGrid[Candidate] = 0;
        mCreepRecedingGrid[Candidate] = 0;
        return true;
    };

    mCreepRegressionTimer += DeltaTime;
    constexpr float RegressionIntervalSeconds = 3.0f / 30.0f;
    constexpr int MaxRegressionStepsPerUpdate = 8;
    int RegressionSteps = std::min(MaxRegressionStepsPerUpdate,
        (int)(mCreepRegressionTimer / RegressionIntervalSeconds));
    if (RegressionSteps > 0)
    {
        mCreepRegressionTimer -=
            (float)RegressionSteps * RegressionIntervalSeconds;
        // Do not accumulate an unbounded backlog after a large simulation
        // step: regression must remain visibly progressive, never one-frame
        // blanket removal.
        mCreepRegressionTimer = std::min(
            mCreepRegressionTimer, RegressionIntervalSeconds);
        for (int Step = 0; Step < RegressionSteps; ++Step)
            if (!RecedeOneUnsupportedBoundaryTile())
                break;
    }

    for (int Y = 0; Y < mMapHeight; ++Y)
        for (int X = 0; X < mMapWidth; ++X)
        {
            const bool HasCreepNow =
                mCreepGrid[(size_t)Y * (size_t)mMapWidth + (size_t)X] != 0;
            auto Tile = sMap->GetTile(X, Y);
            if (Tile.HasCreep != HasCreepNow)
            {
                Tile.HasCreep = HasCreepNow;
                sMap->SetTile(X, Y, Tile);
            }
        }

    ProcessLarvae(EM, DeltaTime);
    // Regenerate before morph completion. Otherwise a morph that finishes in
    // this update receives the entire (potentially large) DeltaTime as if the
    // target building had already been complete for the whole interval.
    ProcessZergRegeneration(EM, DeltaTime);
    ProcessMorphs(EM, DeltaTime);
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
        if (!Unit || Unit->HitPoints <= 0) continue;
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
    if (BuildingSystem::IsBuilderCommitted(EM, DroneID))
    {
        if (OutReason) *OutReason = "drone is already committed to construction";
        return false;
    }

    const auto& BldgDef = Game::Races::GetUnitDefByID(BuildingDefID);
    if (BldgDef.Name.empty() || !BldgDef.IsBuilding ||
        BldgDef.RaceType != Race::Zerg)
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

    const auto& BldgDef = Game::Races::GetUnitDefByID(BuildingDefID);

    // Keep the rule at the actual morph boundary as well as in placement
    // preview/AI search, so direct commands cannot bypass creep validation.
    if (BldgDef.RequiresCreep &&
        !HasCreepAcrossBuildingFootprint(BldgDef.ChkUnitID, Position))
    {
        Logger::Debug("ZergSystem", "Cannot morph drone: building footprint requires creep");
        return false;
    }

    // Keep the Drone as a targetable moving unit until it actually reaches
    // the chosen footprint. The shared pending-build flow provides bounded
    // repathing, cancellation/refund, and consumes the Drone only after a
    // successful placement starts the autonomous Zerg construction.
    return BuildingSystem::StartBuild(
        EM, DroneID, BuildingDefID, BuildTarget::FromPosition(Position));
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

    // Only these canonical units hatch from a Larva. Guardian and Devourer
    // morph from Mutalisks, Lurkers morph from Hydralisks, Broodlings are
    // spell-created, and Infested Terrans are trained by an Infested Command
    // Center.
    const bool IsCanonicalLarvaProduct =
        UnitDef.Name == "Drone" ||
        UnitDef.Name == "Overlord" ||
        UnitDef.Name == "Zergling" ||
        UnitDef.Name == "Hydralisk" ||
        UnitDef.Name == "Mutalisk" ||
        UnitDef.Name == "Scourge" ||
        UnitDef.Name == "Queen" ||
        UnitDef.Name == "Ultralisk" ||
        UnitDef.Name == "Defiler";
    if (!IsCanonicalLarvaProduct)
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
    const int EffectiveSupplyMax = std::min(200, Res.SupplyMax);
    if (Res.SupplyUsed + UnitDef.SupplyCost > EffectiveSupplyMax)
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
    auto* SourceUnit = EM.GetComponent<UnitComponent>(LarvaID);
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

    constexpr int EggChkUnitID = 36;
    constexpr int EggHitPoints = 200;
    constexpr int EggArmor = 10;
    EUnit.ChkUnitID = EggChkUnitID;
    EUnit.UnitRace = Race::Zerg;
    EUnit.MaxHitPoints = EggHitPoints;
    EUnit.HitPoints = SourceUnit && SourceUnit->MaxHitPoints > 0
        ? std::clamp(SourceUnit->HitPoints * EggHitPoints /
                         SourceUnit->MaxHitPoints,
                     1, EggHitPoints)
        : EggHitPoints;
    EUnit.ShieldPoints = 0;
    EUnit.MaxShieldPoints = 0;
    EUnit.EnergyPoints = 0;
    EUnit.MaxEnergyPoints = 0;
    EUnit.Armor = EggArmor;
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
// Hydralisk morph into Lurker
// ============================================================

bool ZergSystem::CanMorphHydraliskIntoLurker(
    Core::ECS::EntityManager& EM, int HydraliskID, std::string* OutReason)
{
    auto* Unit = EM.GetComponent<UnitComponent>(HydraliskID);
    auto* Player = EM.GetComponent<PlayerComponent>(HydraliskID);
    if (!Unit || !Player)
    {
        if (OutReason) *OutReason = "missing Hydralisk unit or owner";
        return false;
    }
    if (Unit->ChkUnitID != 38)
    {
        if (OutReason) *OutReason = "not a Hydralisk";
        return false;
    }
    if (Unit->HitPoints <= 0)
    {
        if (OutReason) *OutReason = "Hydralisk is dead";
        return false;
    }
    if (!EM.GetComponent<TransformComponent>(HydraliskID))
    {
        if (OutReason) *OutReason = "Hydralisk is missing transform";
        return false;
    }
    if (const auto* Morph = EM.GetComponent<MorphComponent>(HydraliskID);
        Morph && Morph->IsMorphing)
    {
        if (OutReason) *OutReason = "Hydralisk is already morphing";
        return false;
    }
    if (!UpgradeSystem::HasResearch(
            Player->PlayerID, ResearchId::Zerg_LurkerMorph))
    {
        if (OutReason) *OutReason = "Lurker Aspect is not researched";
        return false;
    }

    // Lurker Aspect is the durable prerequisite. OpenBW's Hydralisk morph
    // action does not require the Lair or Hydralisk Den to remain alive after
    // the research has completed.
    const int LurkerDefID = Game::Races::GetUnitDefID("Lurker");
    const auto& LurkerDef = Game::Races::GetUnitDefByID(LurkerDefID);

    auto& Res = HarvestSystem::GetResources(Player->PlayerID);
    if (Res.Minerals < LurkerDef.MineralCost ||
        Res.VespeneGas < LurkerDef.GasCost)
    {
        if (OutReason) *OutReason = "not enough resources";
        return false;
    }

    const int AdditionalSupply =
        std::max(0, LurkerDef.SupplyCost - Unit->SupplyCost);
    const int EffectiveSupplyMax = std::min(200, Res.SupplyMax);
    if (Res.SupplyUsed + AdditionalSupply > EffectiveSupplyMax)
    {
        if (OutReason) *OutReason = "not enough supply";
        return false;
    }

    return true;
}

bool ZergSystem::StartMorphHydraliskIntoLurker(
    Core::ECS::EntityManager& EM, int HydraliskID)
{
    std::string Reason;
    if (!CanMorphHydraliskIntoLurker(EM, HydraliskID, &Reason))
    {
        Logger::Debug("ZergSystem", "Cannot morph Hydralisk: " + Reason);
        return false;
    }

    auto* SourceUnit = EM.GetComponent<UnitComponent>(HydraliskID);
    auto* SourcePlayer = EM.GetComponent<PlayerComponent>(HydraliskID);
    auto* SourceTransform = EM.GetComponent<TransformComponent>(HydraliskID);
    if (!SourceUnit || !SourcePlayer || !SourceTransform)
        return false;

    const int PlayerID = SourcePlayer->PlayerID;
    const int SourceSupply = SourceUnit->SupplyCost;
    const Core::Math::Vec2 MorphPosition = SourceTransform->Position;
    const int LurkerDefID = Game::Races::GetUnitDefID("Lurker");
    const auto& LurkerDef = Game::Races::GetUnitDefByID(LurkerDefID);

    auto& Res = HarvestSystem::GetResources(PlayerID);
    Res.Minerals -= LurkerDef.MineralCost;
    Res.VespeneGas -= LurkerDef.GasCost;
    Res.SupplyUsed +=
        std::max(0, LurkerDef.SupplyCost - SourceSupply);

    constexpr int LurkerEggChkUnitID = 97;
    constexpr int LurkerEggHitPoints = 200;
    constexpr int LurkerEggArmor = 10;
    const int EggID = EM.CreateEntity();
    auto& EggTransform = EM.AddComponent<TransformComponent>(EggID);
    auto& EggUnit = EM.AddComponent<UnitComponent>(EggID);
    auto& EggPlayer = EM.AddComponent<PlayerComponent>(EggID);
    auto& EggBuilding = EM.AddComponent<BuildingComponent>(EggID);
    auto& EggMorph = EM.AddComponent<MorphComponent>(EggID);
    auto& EggPath = EM.AddComponent<PathfindingComponent>(EggID);
    auto& EggRender = EM.AddComponent<RenderComponent>(EggID);

    EggTransform.Position = MorphPosition;
    EggPlayer.PlayerID = PlayerID;

    EggUnit.ChkUnitID = LurkerEggChkUnitID;
    EggUnit.UnitRace = Race::Zerg;
    EggUnit.MaxHitPoints = LurkerEggHitPoints;
    EggUnit.HitPoints = SourceUnit->MaxHitPoints > 0
        ? std::clamp(SourceUnit->HitPoints * LurkerEggHitPoints /
                         SourceUnit->MaxHitPoints,
                     1, LurkerEggHitPoints)
        : LurkerEggHitPoints;
    EggUnit.Armor = LurkerEggArmor;
    EggUnit.SupplyCost = 0;
    EggUnit.Speed = 0.0f;
    EggUnit.SightRange = 2;
    EggUnit.SizeType = UnitSize::Medium;

    EggBuilding.IsBuilding = true;
    EggBuilding.UnderConstruction = true;
    EggBuilding.BuildProgress = 0.0f;
    EggBuilding.TotalBuildTime = (float)LurkerDef.BuildTime;
    EggBuilding.BuildingDefID = -1;
    EggBuilding.BuildTimeRemaining = (float)LurkerDef.BuildTime;
    EggBuilding.IsOrganic = true;

    EggMorph.MorphTargetDefID = LurkerDefID;
    EggMorph.MorphDuration = (float)LurkerDef.BuildTime;
    EggMorph.MorphTimer = (float)LurkerDef.BuildTime;
    EggMorph.IsMorphing = true;
    EggMorph.SourceUnitID = HydraliskID;

    EggPath.IsAir = false;
    ApplyCollisionBounds(
        EggPath, ResolveCollisionBounds(LurkerEggChkUnitID));
    EggRender.ImageID = ResolveImageID(LurkerEggChkUnitID);

    EM.DestroyEntity(HydraliskID);

    Logger::Info(
        "ZergSystem", "Player " + std::to_string(PlayerID) +
        " started Hydralisk -> Lurker morph (Lurker Egg " +
        std::to_string(EggID) + ")");
    return true;
}

bool ZergSystem::CanMorphMutaliskIntoGuardian(
    Core::ECS::EntityManager& EM, int MutaliskID, std::string* OutReason)
{
    if (!EM.IsAlive(MutaliskID))
    {
        if (OutReason) *OutReason = "Mutalisk is dead";
        return false;
    }
    const auto* Unit = EM.GetComponent<UnitComponent>(MutaliskID);
    const auto* Player = EM.GetComponent<PlayerComponent>(MutaliskID);
    if (!Unit || !Player)
    {
        if (OutReason) *OutReason = "Mutalisk missing required components";
        return false;
    }
    if (Unit->UnitRace != Race::Zerg || Unit->ChkUnitID != 43)
    {
        if (OutReason) *OutReason = "unit is not a Mutalisk";
        return false;
    }
    if (Unit->HitPoints <= 0)
    {
        if (OutReason) *OutReason = "Mutalisk is dead";
        return false;
    }
    if (!EM.GetComponent<TransformComponent>(MutaliskID))
    {
        if (OutReason) *OutReason = "Mutalisk is missing transform";
        return false;
    }
    if (const auto* Morph = EM.GetComponent<MorphComponent>(MutaliskID);
        Morph && Morph->IsMorphing)
    {
        if (OutReason) *OutReason = "Mutalisk is already morphing";
        return false;
    }

    const int GreaterSpireDefID = NormalizeDefIDForRace(
        Game::Races::GetUnitDefID("Greater Spire"), Race::Zerg);
    if (!HasCompletedBuildingDef(EM, Player->PlayerID, GreaterSpireDefID))
    {
        if (OutReason) *OutReason = "Greater Spire is required";
        return false;
    }

    const int GuardianDefID = Game::Races::GetUnitDefID("Guardian");
    const auto& GuardianDef = Game::Races::GetUnitDefByID(GuardianDefID);

    auto& Res = HarvestSystem::GetResources(Player->PlayerID);
    if (Res.Minerals < GuardianDef.MineralCost ||
        Res.VespeneGas < GuardianDef.GasCost)
    {
        if (OutReason) *OutReason = "not enough resources";
        return false;
    }

    const int AdditionalSupply =
        std::max(0, GuardianDef.SupplyCost - Unit->SupplyCost);
    const int EffectiveSupplyMax = std::min(200, Res.SupplyMax);
    if (Res.SupplyUsed + AdditionalSupply > EffectiveSupplyMax)
    {
        if (OutReason) *OutReason = "not enough supply";
        return false;
    }

    return true;
}

bool ZergSystem::StartMorphMutaliskIntoGuardian(
    Core::ECS::EntityManager& EM, int MutaliskID)
{
    std::string Reason;
    if (!CanMorphMutaliskIntoGuardian(EM, MutaliskID, &Reason))
    {
        Logger::Debug("ZergSystem", "Cannot morph Mutalisk into Guardian: " + Reason);
        return false;
    }

    auto* SourceUnit = EM.GetComponent<UnitComponent>(MutaliskID);
    auto* SourcePlayer = EM.GetComponent<PlayerComponent>(MutaliskID);
    auto* SourceTransform = EM.GetComponent<TransformComponent>(MutaliskID);
    if (!SourceUnit || !SourcePlayer || !SourceTransform)
        return false;

    const int PlayerID = SourcePlayer->PlayerID;
    const int SourceSupply = SourceUnit->SupplyCost;
    const Core::Math::Vec2 MorphPosition = SourceTransform->Position;
    const int GuardianDefID = Game::Races::GetUnitDefID("Guardian");
    const auto& GuardianDef = Game::Races::GetUnitDefByID(GuardianDefID);

    auto& Res = HarvestSystem::GetResources(PlayerID);
    Res.Minerals -= GuardianDef.MineralCost;
    Res.VespeneGas -= GuardianDef.GasCost;
    Res.SupplyUsed +=
        std::max(0, GuardianDef.SupplyCost - SourceSupply);

    constexpr int CocoonChkUnitID = 59;
    constexpr int CocoonHitPoints = 200;
    constexpr int CocoonArmor = 10;
    const int EggID = EM.CreateEntity();
    auto& EggTransform = EM.AddComponent<TransformComponent>(EggID);
    auto& EggUnit = EM.AddComponent<UnitComponent>(EggID);
    auto& EggPlayer = EM.AddComponent<PlayerComponent>(EggID);
    auto& EggBuilding = EM.AddComponent<BuildingComponent>(EggID);
    auto& EggMorph = EM.AddComponent<MorphComponent>(EggID);
    auto& EggPath = EM.AddComponent<PathfindingComponent>(EggID);
    auto& EggRender = EM.AddComponent<RenderComponent>(EggID);

    EggTransform.Position = MorphPosition;
    EggPlayer.PlayerID = PlayerID;

    EggUnit.ChkUnitID = CocoonChkUnitID;
    EggUnit.UnitRace = Race::Zerg;
    EggUnit.MaxHitPoints = CocoonHitPoints;
    EggUnit.HitPoints = SourceUnit->MaxHitPoints > 0
        ? std::clamp(SourceUnit->HitPoints * CocoonHitPoints /
                         SourceUnit->MaxHitPoints,
                     1, CocoonHitPoints)
        : CocoonHitPoints;
    EggUnit.Armor = CocoonArmor;
    EggUnit.SupplyCost = 0;
    EggUnit.Speed = 0.0f;
    EggUnit.SightRange = 4;
    EggUnit.SizeType = UnitSize::Medium;

    EggBuilding.IsBuilding = true;
    EggBuilding.UnderConstruction = true;
    EggBuilding.BuildProgress = 0.0f;
    EggBuilding.TotalBuildTime = (float)GuardianDef.BuildTime;
    EggBuilding.BuildingDefID = -1;
    EggBuilding.BuildTimeRemaining = (float)GuardianDef.BuildTime;
    EggBuilding.IsOrganic = true;

    EggMorph.MorphTargetDefID = GuardianDefID;
    EggMorph.MorphDuration = (float)GuardianDef.BuildTime;
    EggMorph.MorphTimer = (float)GuardianDef.BuildTime;
    EggMorph.IsMorphing = true;
    EggMorph.SourceUnitID = MutaliskID;

    EggPath.IsAir = true;
    ApplyCollisionBounds(
        EggPath, ResolveCollisionBounds(CocoonChkUnitID));
    EggRender.ImageID = ResolveImageID(CocoonChkUnitID);

    EM.DestroyEntity(MutaliskID);

    Logger::Info(
        "ZergSystem", "Player " + std::to_string(PlayerID) +
        " started Mutalisk -> Guardian morph (Cocoon " +
        std::to_string(EggID) + ")");
    return true;
}

bool ZergSystem::CanMorphMutaliskIntoDevourer(
    Core::ECS::EntityManager& EM, int MutaliskID, std::string* OutReason)
{
    if (!EM.IsAlive(MutaliskID))
    {
        if (OutReason) *OutReason = "Mutalisk is dead";
        return false;
    }
    const auto* Unit = EM.GetComponent<UnitComponent>(MutaliskID);
    const auto* Player = EM.GetComponent<PlayerComponent>(MutaliskID);
    if (!Unit || !Player)
    {
        if (OutReason) *OutReason = "Mutalisk missing required components";
        return false;
    }
    if (Unit->UnitRace != Race::Zerg || Unit->ChkUnitID != 43)
    {
        if (OutReason) *OutReason = "unit is not a Mutalisk";
        return false;
    }
    if (Unit->HitPoints <= 0)
    {
        if (OutReason) *OutReason = "Mutalisk is dead";
        return false;
    }
    if (!EM.GetComponent<TransformComponent>(MutaliskID))
    {
        if (OutReason) *OutReason = "Mutalisk is missing transform";
        return false;
    }
    if (const auto* Morph = EM.GetComponent<MorphComponent>(MutaliskID);
        Morph && Morph->IsMorphing)
    {
        if (OutReason) *OutReason = "Mutalisk is already morphing";
        return false;
    }

    const int GreaterSpireDefID = NormalizeDefIDForRace(
        Game::Races::GetUnitDefID("Greater Spire"), Race::Zerg);
    if (!HasCompletedBuildingDef(EM, Player->PlayerID, GreaterSpireDefID))
    {
        if (OutReason) *OutReason = "Greater Spire is required";
        return false;
    }

    const int DevourerDefID = Game::Races::GetUnitDefID("Devourer");
    const auto& DevourerDef = Game::Races::GetUnitDefByID(DevourerDefID);

    auto& Res = HarvestSystem::GetResources(Player->PlayerID);
    if (Res.Minerals < DevourerDef.MineralCost ||
        Res.VespeneGas < DevourerDef.GasCost)
    {
        if (OutReason) *OutReason = "not enough resources";
        return false;
    }

    const int AdditionalSupply =
        std::max(0, DevourerDef.SupplyCost - Unit->SupplyCost);
    const int EffectiveSupplyMax = std::min(200, Res.SupplyMax);
    if (Res.SupplyUsed + AdditionalSupply > EffectiveSupplyMax)
    {
        if (OutReason) *OutReason = "not enough supply";
        return false;
    }

    return true;
}

bool ZergSystem::StartMorphMutaliskIntoDevourer(
    Core::ECS::EntityManager& EM, int MutaliskID)
{
    std::string Reason;
    if (!CanMorphMutaliskIntoDevourer(EM, MutaliskID, &Reason))
    {
        Logger::Debug("ZergSystem", "Cannot morph Mutalisk into Devourer: " + Reason);
        return false;
    }

    auto* SourceUnit = EM.GetComponent<UnitComponent>(MutaliskID);
    auto* SourcePlayer = EM.GetComponent<PlayerComponent>(MutaliskID);
    auto* SourceTransform = EM.GetComponent<TransformComponent>(MutaliskID);
    if (!SourceUnit || !SourcePlayer || !SourceTransform)
        return false;

    const int PlayerID = SourcePlayer->PlayerID;
    const int SourceSupply = SourceUnit->SupplyCost;
    const Core::Math::Vec2 MorphPosition = SourceTransform->Position;
    const int DevourerDefID = Game::Races::GetUnitDefID("Devourer");
    const auto& DevourerDef = Game::Races::GetUnitDefByID(DevourerDefID);

    auto& Res = HarvestSystem::GetResources(PlayerID);
    Res.Minerals -= DevourerDef.MineralCost;
    Res.VespeneGas -= DevourerDef.GasCost;
    Res.SupplyUsed +=
        std::max(0, DevourerDef.SupplyCost - SourceSupply);

    constexpr int CocoonChkUnitID = 59;
    constexpr int CocoonHitPoints = 200;
    constexpr int CocoonArmor = 10;
    const int EggID = EM.CreateEntity();
    auto& EggTransform = EM.AddComponent<TransformComponent>(EggID);
    auto& EggUnit = EM.AddComponent<UnitComponent>(EggID);
    auto& EggPlayer = EM.AddComponent<PlayerComponent>(EggID);
    auto& EggBuilding = EM.AddComponent<BuildingComponent>(EggID);
    auto& EggMorph = EM.AddComponent<MorphComponent>(EggID);
    auto& EggPath = EM.AddComponent<PathfindingComponent>(EggID);
    auto& EggRender = EM.AddComponent<RenderComponent>(EggID);

    EggTransform.Position = MorphPosition;
    EggPlayer.PlayerID = PlayerID;

    EggUnit.ChkUnitID = CocoonChkUnitID;
    EggUnit.UnitRace = Race::Zerg;
    EggUnit.MaxHitPoints = CocoonHitPoints;
    EggUnit.HitPoints = SourceUnit->MaxHitPoints > 0
        ? std::clamp(SourceUnit->HitPoints * CocoonHitPoints /
                         SourceUnit->MaxHitPoints,
                     1, CocoonHitPoints)
        : CocoonHitPoints;
    EggUnit.Armor = CocoonArmor;
    EggUnit.SupplyCost = 0;
    EggUnit.Speed = 0.0f;
    EggUnit.SightRange = 4;
    EggUnit.SizeType = UnitSize::Medium;

    EggBuilding.IsBuilding = true;
    EggBuilding.UnderConstruction = true;
    EggBuilding.BuildProgress = 0.0f;
    EggBuilding.TotalBuildTime = (float)DevourerDef.BuildTime;
    EggBuilding.BuildingDefID = -1;
    EggBuilding.BuildTimeRemaining = (float)DevourerDef.BuildTime;
    EggBuilding.IsOrganic = true;

    EggMorph.MorphTargetDefID = DevourerDefID;
    EggMorph.MorphDuration = (float)DevourerDef.BuildTime;
    EggMorph.MorphTimer = (float)DevourerDef.BuildTime;
    EggMorph.IsMorphing = true;
    EggMorph.SourceUnitID = MutaliskID;

    EggPath.IsAir = true;
    ApplyCollisionBounds(
        EggPath, ResolveCollisionBounds(CocoonChkUnitID));
    EggRender.ImageID = ResolveImageID(CocoonChkUnitID);

    EM.DestroyEntity(MutaliskID);

    Logger::Info(
        "ZergSystem", "Player " + std::to_string(PlayerID) +
        " started Mutalisk -> Devourer morph (Cocoon " +
        std::to_string(EggID) + ")");
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
            const int SourceHitPoints = Unit->HitPoints;
            const int SourceMaxHitPoints = Unit->MaxHitPoints;
            Unit->ChkUnitID = TargetDef.ChkUnitID;
            Unit->MaxHitPoints = TargetDef.HitPoints;
            // OpenBW preserves the source building's missing HP across a
            // Hatchery/Lair/Spire/Colony upgrade rather than granting a free
            // full heal when the target type completes.
            Unit->HitPoints = std::clamp(
                SourceHitPoints + TargetDef.HitPoints - SourceMaxHitPoints,
                1, TargetDef.HitPoints);
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
            const Core::Math::Vec2 Candidate = ProductionSystem::FindSpawnPosition(
                EM, Larva->ParentHatcheryEntityID, TargetDef.ChkUnitID,
                MorphEntityID);
            // A saturated hatchery exit may have no free replacement point.
            // In that case keep the egg's already-reserved position instead of
            // propagating FindSpawnPosition's explicit failure sentinel.
            if (Candidate.X >= 0.0f && Candidate.Y >= 0.0f)
                SpawnPos = Candidate;
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

        int PairedUnitID = -1;
        if (TargetDef.Name == "Zergling" || TargetDef.Name == "Scourge")
        {
            // One larva/egg produces two Zerglings or two Scourge for the
            // listed pair cost and one shared supply reservation.
            Core::Math::Vec2 PairSpawnPos = SpawnPos + Core::Math::Vec2{24.0f, 0.0f};
            if (const auto* Larva = EM.GetComponent<LarvaComponent>(MorphEntityID);
                Larva && Larva->ParentHatcheryEntityID >= 0 &&
                EM.IsAlive(Larva->ParentHatcheryEntityID))
            {
                const Core::Math::Vec2 Candidate = ProductionSystem::FindSpawnPosition(
                    EM, Larva->ParentHatcheryEntityID, TargetDef.ChkUnitID,
                    MorphEntityID);
                if (Candidate.X >= 0.0f && Candidate.Y >= 0.0f)
                    PairSpawnPos = Candidate;
            }

            // Snapshot before growing any component pool; AddComponent for
            // the sibling may invalidate references held for the first unit.
            const TransformComponent FirstTransformCopy = NTrans;
            const UnitComponent FirstUnitCopy = NUnit;
            const PlayerComponent FirstPlayerCopy = NPlayer;
            const CommandComponent FirstCommandCopy = NCmd;
            const PathfindingComponent FirstPathCopy = NPath;
            const RenderComponent FirstRenderCopy = NRender;
            const auto* FirstCombat = EM.GetComponent<CombatComponent>(NewUnitID);
            const auto* FirstBurrow = EM.GetComponent<BurrowComponent>(NewUnitID);
            const bool HasFirstCombat = FirstCombat != nullptr;
            const bool HasFirstBurrow = FirstBurrow != nullptr;
            const CombatComponent FirstCombatCopy =
                FirstCombat ? *FirstCombat : CombatComponent{};
            const BurrowComponent FirstBurrowCopy =
                FirstBurrow ? *FirstBurrow : BurrowComponent{};

            PairedUnitID = EM.CreateEntity();
            auto& PairTransform = EM.AddComponent<TransformComponent>(PairedUnitID);
            auto& PairUnit = EM.AddComponent<UnitComponent>(PairedUnitID);
            auto& PairPlayer = EM.AddComponent<PlayerComponent>(PairedUnitID);
            auto& PairCommand = EM.AddComponent<CommandComponent>(PairedUnitID);
            auto& PairPath = EM.AddComponent<PathfindingComponent>(PairedUnitID);
            auto& PairRender = EM.AddComponent<RenderComponent>(PairedUnitID);
            PairTransform = FirstTransformCopy;
            PairTransform.Position = PairSpawnPos;
            PairUnit = FirstUnitCopy;
            PairPlayer = FirstPlayerCopy;
            PairCommand = FirstCommandCopy;
            PairPath = FirstPathCopy;
            PairRender = FirstRenderCopy;
            if (HasFirstCombat)
                EM.AddComponent<CombatComponent>(PairedUnitID) = FirstCombatCopy;
            if (HasFirstBurrow)
                EM.AddComponent<BurrowComponent>(PairedUnitID) = FirstBurrowCopy;

            EM.GetComponent<UnitComponent>(NewUnitID)->SupplyCost = 0;
            PairUnit.SupplyCost = 0;
            auto& FirstShared = EM.AddComponent<SharedSupplyComponent>(NewUnitID);
            FirstShared.PartnerEntityID = PairedUnitID;
            FirstShared.ReservedSupply = TargetDef.SupplyCost;
            auto& SecondShared = EM.AddComponent<SharedSupplyComponent>(PairedUnitID);
            SecondShared.PartnerEntityID = NewUnitID;
            SecondShared.ReservedSupply = TargetDef.SupplyCost;
        }

        // Destroy the egg
        EM.DestroyEntity(MorphEntityID);

        Logger::Info("ZergSystem",
            "Unit morph complete: " + TargetDef.Name +
            " (entity " + std::to_string(NewUnitID) +
            (PairedUnitID >= 0
                ? ", paired entity " + std::to_string(PairedUnitID)
                : std::string()) + ")");
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
    constexpr int HitPointSubPointsPerSecond = 4 * 24;

    auto Units = EM.GetEntitiesWith<UnitComponent>();
    for (int Eid : Units)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        if (!Unit || Unit->UnitRace != Race::Zerg)
            continue;

        // OpenBW only regenerates completed units. Zerg eggs and buildings
        // under construction use BuildingComponent for their completion state.
        const auto* Building = EM.GetComponent<BuildingComponent>(Eid);
        if (Unit->HitPoints <= 0 || Unit->MaxHitPoints <= 0 ||
            (Building && Building->UnderConstruction))
        {
            Unit->HealthRegenAccumulator = 0.0f;
            continue;
        }

        const int MaxHitPointsRaw = Unit->MaxHitPoints * 256;
        int HitPointsRaw = std::max(
            0, Unit->HitPoints * 256 -
               std::clamp(Unit->HitPointSubDamage, 0, 255));
        if (HitPointsRaw >= MaxHitPointsRaw)
        {
            Unit->HitPoints = Unit->MaxHitPoints;
            Unit->HitPointSubDamage = 0;
            Unit->HealthRegenAccumulator = 0.0f;
            continue;
        }

        // Brood War restores 4/256 HP every game tick at 24 Hz. Accumulate
        // raw 1/256 sub-points so the result is independent of frame slicing.
        Unit->HealthRegenAccumulator +=
            DeltaTime * (float)HitPointSubPointsPerSecond;
        const int RegenRaw =
            (int)(Unit->HealthRegenAccumulator + 0.0001f);
        if (RegenRaw <= 0)
            continue;

        Unit->HealthRegenAccumulator -= (float)RegenRaw;
        if (Unit->HealthRegenAccumulator < 0.0f)
            Unit->HealthRegenAccumulator = 0.0f;

        HitPointsRaw = std::min(MaxHitPointsRaw, HitPointsRaw + RegenRaw);
        Unit->HitPoints = (HitPointsRaw + 255) / 256;
        Unit->HitPointSubDamage = Unit->HitPoints * 256 - HitPointsRaw;
        if (HitPointsRaw >= MaxHitPointsRaw)
        {
            Unit->HitPoints = Unit->MaxHitPoints;
            Unit->HitPointSubDamage = 0;
            Unit->HealthRegenAccumulator = 0.0f;
        }
    }
}

} // namespace Game::Systems
