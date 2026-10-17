#include "Game/Systems/HarvestSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/MovementSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

namespace
{
    const Game::Map::GameMap* sCommandMap = nullptr;
}

void CombatSystem::IssueAttack(Core::ECS::EntityManager&,
                               const std::vector<int>&,
                               int)
{
}

void CommandSystem::SetMap(const Game::Map::GameMap* Map)
{
    sCommandMap = Map;
}

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                              const std::vector<Core::ECS::EntityID>& Entities,
                              const Core::Math::Vec2& Target)
{
    IssueMove(EM, Entities, Target, -1);
}

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                              const std::vector<Core::ECS::EntityID>& Entities,
                              const Core::Math::Vec2& Target,
                              int PathingIgnoreEntityID)
{
    for (Core::ECS::EntityID EntityID : Entities)
    {
        auto* Cmd = EM.GetComponent<Components::CommandComponent>(EntityID);
        if (!Cmd)
            Cmd = &EM.AddComponent<Components::CommandComponent>(EntityID);
        Cmd->CurrentCommand = Components::CommandType::Move;
        Cmd->TargetPosition = Target;

        auto* Path = EM.GetComponent<Components::PathfindingComponent>(EntityID);
        if (!Path)
            Path = &EM.AddComponent<Components::PathfindingComponent>(EntityID);
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->RequestedDestination = Target;
        Path->RequestedIgnoreEntityID = PathingIgnoreEntityID;

        auto* Trans = EM.GetComponent<Components::TransformComponent>(EntityID);
        if (sCommandMap && Trans)
        {
            auto Result = Game::Pathfinding::AStar::FindPath(*sCommandMap,
                Trans->Position, Target, 1000, 0);
            if (Result.Found)
                Path->Path = std::move(Result.Waypoints);
        }
        else
        {
            Path->Path.push_back(Target);
        }
    }
}

bool CommandSystem::IssueMoveAndHasPath(Core::ECS::EntityManager& EM,
                                        Core::ECS::EntityID EntityID,
                                        const Core::Math::Vec2& Target,
                                        int PathingIgnoreEntityID)
{
    IssueMove(EM, {EntityID}, Target, PathingIgnoreEntityID);
    if (auto* Path = EM.GetComponent<Components::PathfindingComponent>(EntityID))
        return !Path->Path.empty();
    return false;
}

void CommandSystem::AdvanceQueue(Core::ECS::EntityManager&,
                                 Core::ECS::EntityID)
{
}

} // namespace Game::Systems

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    void SetCollisionBounds(Game::Components::PathfindingComponent& Path,
                            int Left,
                            int Up,
                            int Right,
                            int Down)
    {
        Path.HasCollisionBounds = true;
        Path.CollisionLeft = Left;
        Path.CollisionUp = Up;
        Path.CollisionRight = Right;
        Path.CollisionDown = Down;
        Path.CollisionRadius =
            std::max(4, std::max(Left + Right + 1, Up + Down + 1) / 2);
    }

    float EntityEdgeDistance(Core::ECS::EntityManager& EM,
                             Core::Math::Vec2 From,
                             int TargetEntityID)
    {
        auto* TargetTrans =
            EM.GetComponent<Game::Components::TransformComponent>(TargetEntityID);
        auto* TargetPath =
            EM.GetComponent<Game::Components::PathfindingComponent>(TargetEntityID);
        if (!TargetTrans)
            return 1000000.0f;
        if (!TargetPath || !TargetPath->HasCollisionBounds)
            return From.DistanceTo(TargetTrans->Position);

        const Core::Math::Vec2 Delta = From - TargetTrans->Position;
        const float NearestX = std::clamp(
            Delta.X,
            -(float)TargetPath->CollisionLeft,
            (float)TargetPath->CollisionRight);
        const float NearestY = std::clamp(
            Delta.Y,
            -(float)TargetPath->CollisionUp,
            (float)TargetPath->CollisionDown);
        return From.DistanceTo({
            TargetTrans->Position.X + NearestX,
            TargetTrans->Position.Y + NearestY
        });
    }

    bool CollisionBoundsOverlap(
        const Game::Components::TransformComponent& ATransform,
        const Game::Components::PathfindingComponent& APath,
        const Game::Components::TransformComponent& BTransform,
        const Game::Components::PathfindingComponent& BPath)
    {
        return ATransform.Position.X - (float)APath.CollisionLeft <
                   BTransform.Position.X + (float)BPath.CollisionRight + 1.0f &&
               ATransform.Position.X + (float)APath.CollisionRight + 1.0f >
                   BTransform.Position.X - (float)BPath.CollisionLeft &&
               ATransform.Position.Y - (float)APath.CollisionUp <
                   BTransform.Position.Y + (float)BPath.CollisionDown + 1.0f &&
               ATransform.Position.Y + (float)APath.CollisionDown + 1.0f >
                   BTransform.Position.Y - (float)BPath.CollisionUp;
    }

    int CreateCompletedDepot(Core::ECS::EntityManager& EM,
                             Core::Math::Vec2 Position,
                             int PlayerID)
    {
        const int DefID = Game::Races::GetUnitDefID("CommandCenter");
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.UnitRace = Def.RaceType;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.UnderConstruction = false;
        Building.BuildingDefID = DefID;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
        SetCollisionBounds(Path, 64, 48, 63, 47);
        return EntityID;
    }

    int CreateCompletedRefinery(Core::ECS::EntityManager& EM,
                                Core::Math::Vec2 Position,
                                int PlayerID,
                                int GasAmount)
    {
        const int DefID = Game::Races::GetUnitDefID("Refinery");
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.UnitRace = Def.RaceType;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.UnderConstruction = false;
        Building.BuildingDefID = DefID;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Resource = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
        Resource.Type = Game::Components::ResourceType::VespeneGas;
        Resource.Amount = GasAmount;
        Resource.MaxAmount = GasAmount;
        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
        SetCollisionBounds(Path, 64, 40, 63, 39);
        return EntityID;
    }
}

int main()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();

    int Scv = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Scv).Position = {100.0f, 100.0f};
    auto& ScvUnit = EM.AddComponent<Game::Components::UnitComponent>(Scv);
    ScvUnit.ChkUnitID = 7;
    ScvUnit.Speed = 2.8f;
    EM.AddComponent<Game::Components::PlayerComponent>(Scv).PlayerID = 0;

    int Mineral = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Mineral).Position = {124.0f, 100.0f};
    auto& MineralUnit = EM.AddComponent<Game::Components::UnitComponent>(Mineral);
    MineralUnit.ChkUnitID = 176;
    MineralUnit.Speed = 0.0f;
    auto& Resource = EM.AddComponent<Game::Components::ResourceComponent>(Mineral);
    Resource.Type = Game::Components::ResourceType::Minerals;
    Resource.Amount = 1500;
    Resource.MaxAmount = 1500;
    auto& MineralPath = EM.AddComponent<Game::Components::PathfindingComponent>(Mineral);
    SetCollisionBounds(MineralPath, 32, 20, 31, 19);
    const int Depot = CreateCompletedDepot(EM, {300.0f, 100.0f}, 0);

    Require(Game::Systems::HarvestSystem::IssueHarvest(EM, {Scv}, Mineral),
        "SCV can receive a harvest order on mineral fields");

    auto* Cmd = EM.GetComponent<Game::Components::CommandComponent>(Scv);
    auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(Scv);
    auto* Harvest = EM.GetComponent<Game::Components::HarvestComponent>(Scv);
    Require(Cmd && Cmd->CurrentCommand == Game::Components::CommandType::Harvest,
        "harvest command survives path issuing");
    Require(Cmd->TargetEntityID == Mineral, "harvest command keeps the resource entity target");
    Require(Path && !Path->Path.empty(), "harvest command receives an approach waypoint");
    Require(Path->Path.front().X < 124.0f,
        "harvest approach waypoint is outside the mineral center");
    const float MineralApproachEdgeDistance =
        EntityEdgeDistance(EM, Path->Path.front(), Mineral);
    Require(MineralApproachEdgeDistance >= 16.0f &&
            MineralApproachEdgeDistance <= 20.1f &&
            Path->RequestedIgnoreEntityID == -1,
        "mineral approach keeps the worker footprint outside a solid target patch");
    Require(Harvest && Harvest->State == Game::Components::HarvestState::MovingToResource,
        "harvest component starts by moving to resource");

    int DeadScv = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(DeadScv).Position = {100.0f, 132.0f};
    auto& DeadScvUnit = EM.AddComponent<Game::Components::UnitComponent>(DeadScv);
    DeadScvUnit.ChkUnitID = 7;
    DeadScvUnit.HitPoints = 0;
    EM.AddComponent<Game::Components::PlayerComponent>(DeadScv).PlayerID = 0;
    Require(!Game::Systems::HarvestSystem::IssueHarvest(EM, {DeadScv}, Mineral),
        "dead workers cannot receive harvest orders");
    Require(!EM.GetComponent<Game::Components::HarvestComponent>(DeadScv),
        "dead worker harvest attempt does not create harvest state");
    Require(!EM.GetComponent<Game::Components::CommandComponent>(DeadScv),
        "dead worker harvest attempt does not create a harvest command");

    Path->Path.clear();
    Path->PathIndex = 0;
    Game::Systems::HarvestSystem HarvestSystem(SysMgr);
    HarvestSystem.Update(0.1f);

    Require(Harvest->State == Game::Components::HarvestState::Mining,
        "worker starts mining when already inside harvest range instead of re-pathing forever");

    HarvestSystem.Update(Harvest->MineInterval);

    Require(Harvest->State == Game::Components::HarvestState::MovingToDepot,
        "worker returns to a depot after filling cargo");
    Require(Harvest->ReturnBuildingID == Depot,
        "worker selects the nearest owned resource depot");
    Require(Path && !Path->Path.empty(),
        "returning worker receives a depot approach waypoint");
    Require(EntityEdgeDistance(EM, Path->Path.front(), Depot) <= 0.1f,
        "return path enters the command center collision footprint");
    Require(Path->Path.front().X >= 300.0f - 64.0f + 12.0f,
        "return path carries the worker visibly inside the command center");

    auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
    Resources.Minerals = 50;
    Resources.VespeneGas = 0;
    Resources.SupplyUsed = 0;
    Resources.SupplyMax = 10;
    auto* ScvTrans = EM.GetComponent<Game::Components::TransformComponent>(Scv);
    Require(ScvTrans != nullptr, "SCV transform is available for depot return regression");
    ScvTrans->Position = {180.0f, 100.0f};
    Path->PathIndex = (int)Path->Path.size();
    Path->CurrentSpeed = 0.0f;
    Path->CurrentVelocity = {0.0f, 0.0f};
    HarvestSystem.Update(0.1f);
    Require(Resources.Minerals == 50,
        "worker does not deposit minerals while still far from the depot collision edge");
    Require(Harvest->CarryingAmount == 8 &&
            Harvest->State == Game::Components::HarvestState::MovingToDepot,
        "worker keeps carried minerals until reaching the depot collision edge");
    for (int Tick = 0; Tick < 72; ++Tick)
        HarvestSystem.Update(1.0f / 24.0f);
    Require(Harvest->ReturnRecoveryAttempts == 1 &&
            Harvest->ReturnBuildingID == Depot &&
            !Path->Path.empty(),
        "stationary return worker performs one bounded depot-path recovery");

    ScvTrans->Position = {225.0f, 100.0f};
    Path->PathIndex = (int)Path->Path.size();
    Path->CurrentSpeed = 0.0f;
    Path->CurrentVelocity = {0.0f, 0.0f};
    HarvestSystem.Update(0.1f);
    Require(Resources.Minerals == 50 &&
            Harvest->CarryingAmount == 8,
        "worker collision box contact alone does not deposit before its center reaches the depot boundary");

    ScvTrans->Position = {236.0f, 100.0f};
    Path->PathIndex = (int)Path->Path.size();
    Path->CurrentSpeed = 0.0f;
    Path->CurrentVelocity = {0.0f, 0.0f};
    const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    const int LairDefID = Game::Races::GetUnitDefID("Lair");
    auto* MorphingMineralDepot =
        EM.GetComponent<Game::Components::BuildingComponent>(Depot);
    auto* MorphingMineralDepotUnit =
        EM.GetComponent<Game::Components::UnitComponent>(Depot);
    Require(MorphingMineralDepot && MorphingMineralDepotUnit &&
            HatcheryDefID >= 0 && LairDefID >= 0,
        "zerg depot definitions are available for upgrade delivery regression");
    MorphingMineralDepot->BuildingDefID = HatcheryDefID;
    MorphingMineralDepot->UnderConstruction = true;
    MorphingMineralDepotUnit->ChkUnitID =
        Game::Races::GetUnitDefByID(HatcheryDefID).ChkUnitID;
    auto& MineralDepotMorph =
        EM.AddComponent<Game::Components::MorphComponent>(Depot);
    MineralDepotMorph.IsMorphing = true;
    MineralDepotMorph.SourceUnitID = Depot;
    MineralDepotMorph.MorphTargetDefID = LairDefID;
    HarvestSystem.Update(0.1f);
    Require(Resources.Minerals == 58,
        "worker deposits minerals while its Hatchery is upgrading to Lair");
    Require(Harvest->CarryingAmount == 0 &&
            Harvest->State == Game::Components::HarvestState::MovingToResource,
        "edge-contact delivery resumes the harvest loop without a depot stall");
    Require(Cmd->CurrentCommand == Game::Components::CommandType::Harvest &&
            Cmd->TargetEntityID == Mineral,
        "mineral worker keeps harvest command and mineral target after deposit");
    Require(Path->RequestedIgnoreEntityID == Depot && !Path->Path.empty(),
        "worker leaving a completed delivery ignores the depot it starts inside");

    ScvTrans->Position = {200.0f, 100.0f};
    HarvestSystem.Update(0.1f);
    Require(Harvest->State == Game::Components::HarvestState::MovingToResource &&
            Path->RequestedIgnoreEntityID == -1 && !Path->Path.empty(),
        "worker keeps its mineral target solid after clearing the departed depot");

    int ReplacementMineral = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(ReplacementMineral).Position =
        {520.0f, 100.0f};
    auto& ReplacementMineralUnit =
        EM.AddComponent<Game::Components::UnitComponent>(ReplacementMineral);
    ReplacementMineralUnit.ChkUnitID = 176;
    auto& ReplacementResource =
        EM.AddComponent<Game::Components::ResourceComponent>(ReplacementMineral);
    ReplacementResource.Type = Game::Components::ResourceType::Minerals;
    ReplacementResource.Amount = 1500;
    ReplacementResource.MaxAmount = 1500;
    auto& ReplacementMineralPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(ReplacementMineral);
    SetCollisionBounds(ReplacementMineralPath, 32, 20, 31, 19);
    Resource.Amount = 0;
    Resource.IsDepleted = true;
    HarvestSystem.Update(0.1f);
    Require(Harvest->TargetResourceID == ReplacementMineral &&
            Harvest->DesiredResourceID == ReplacementMineral &&
            Harvest->State == Game::Components::HarvestState::MovingToResource,
        "depleted mineral field immediately selects a live replacement patch");
    Require(Path->RequestedDestination.X > 450.0f && !Path->Path.empty(),
        "depleted mineral field rebuilds a path toward the replacement patch");
    Require(std::find(Resource.QueuedGatherers.begin(),
                      Resource.QueuedGatherers.end(), Scv) ==
                Resource.QueuedGatherers.end() &&
            std::find(ReplacementResource.QueuedGatherers.begin(),
                      ReplacementResource.QueuedGatherers.end(), Scv) !=
                ReplacementResource.QueuedGatherers.end(),
        "depleted mineral reroute releases the old queue and joins the new one");

    for (int WorkerChkUnitID : {0x29, 0x40}) // Drone, Probe; SCV covered above.
    {
        Core::ECS::SystemManager RaceRerouteSysMgr;
        auto& RaceEM = RaceRerouteSysMgr.GetEntityManager();
        const int Worker = RaceEM.CreateEntity();
        RaceEM.AddComponent<Game::Components::TransformComponent>(Worker).Position =
            {64.0f, 64.0f};
        auto& WorkerUnit = RaceEM.AddComponent<Game::Components::UnitComponent>(Worker);
        WorkerUnit.ChkUnitID = WorkerChkUnitID;
        WorkerUnit.Speed = 2.8f;
        RaceEM.AddComponent<Game::Components::PlayerComponent>(Worker).PlayerID = 0;
        auto& WorkerPath =
            RaceEM.AddComponent<Game::Components::PathfindingComponent>(Worker);
        RaceEM.AddComponent<Game::Components::CommandComponent>(Worker).CurrentCommand =
            Game::Components::CommandType::Harvest;

        const int EmptyMineral = RaceEM.CreateEntity();
        RaceEM.AddComponent<Game::Components::TransformComponent>(EmptyMineral).Position =
            {96.0f, 64.0f};
        auto& EmptyResource =
            RaceEM.AddComponent<Game::Components::ResourceComponent>(EmptyMineral);
        EmptyResource.Type = Game::Components::ResourceType::Minerals;
        EmptyResource.Amount = 0;
        EmptyResource.IsDepleted = true;

        const int LiveMineral = RaceEM.CreateEntity();
        RaceEM.AddComponent<Game::Components::TransformComponent>(LiveMineral).Position =
            {320.0f, 64.0f};
        auto& LiveResource =
            RaceEM.AddComponent<Game::Components::ResourceComponent>(LiveMineral);
        LiveResource.Type = Game::Components::ResourceType::Minerals;
        LiveResource.Amount = 1500;
        LiveResource.MaxAmount = 1500;
        auto& LiveMineralPath =
            RaceEM.AddComponent<Game::Components::PathfindingComponent>(LiveMineral);
        SetCollisionBounds(LiveMineralPath, 32, 20, 31, 19);

        auto& WorkerHarvest =
            RaceEM.AddComponent<Game::Components::HarvestComponent>(Worker);
        WorkerHarvest.State = Game::Components::HarvestState::MovingToResource;
        WorkerHarvest.TargetResourceID = EmptyMineral;
        WorkerHarvest.DesiredResourceID = EmptyMineral;
        Game::Systems::HarvestSystem RaceHarvestSystem(RaceRerouteSysMgr);
        RaceHarvestSystem.Update(0.1f);
        Require(WorkerHarvest.TargetResourceID == LiveMineral &&
                WorkerHarvest.State == Game::Components::HarvestState::MovingToResource &&
                !WorkerPath.Path.empty(),
            WorkerChkUnitID == 0x29
                ? "Drone replans to a live mineral field after depletion"
                : "Probe replans to a live mineral field after depletion");
    }

    for (int WorkerChkUnitID : {0x07, 0x29, 0x40})
    {
        Core::ECS::SystemManager AdjacentMineralSysMgr;
        auto& AdjacentEM = AdjacentMineralSysMgr.GetEntityManager();

        const int Worker = AdjacentEM.CreateEntity();
        AdjacentEM.AddComponent<Game::Components::TransformComponent>(Worker).Position =
            {64.0f, 100.0f};
        auto& WorkerUnit =
            AdjacentEM.AddComponent<Game::Components::UnitComponent>(Worker);
        WorkerUnit.ChkUnitID = WorkerChkUnitID;
        WorkerUnit.Speed = 2.8f;
        AdjacentEM.AddComponent<Game::Components::PlayerComponent>(Worker).PlayerID = 0;
        auto& WorkerPath =
            AdjacentEM.AddComponent<Game::Components::PathfindingComponent>(Worker);
        SetCollisionBounds(WorkerPath, 8, 8, 7, 7);

        const int BlockingMineral = AdjacentEM.CreateEntity();
        AdjacentEM.AddComponent<Game::Components::TransformComponent>(BlockingMineral).Position =
            {100.0f, 100.0f};
        auto& BlockingUnit =
            AdjacentEM.AddComponent<Game::Components::UnitComponent>(BlockingMineral);
        BlockingUnit.ChkUnitID = 176;
        auto& BlockingResource =
            AdjacentEM.AddComponent<Game::Components::ResourceComponent>(BlockingMineral);
        BlockingResource.Type = Game::Components::ResourceType::Minerals;
        BlockingResource.Amount = 1500;
        BlockingResource.MaxAmount = 1500;
        auto& BlockingPath =
            AdjacentEM.AddComponent<Game::Components::PathfindingComponent>(BlockingMineral);
        SetCollisionBounds(BlockingPath, 32, 20, 31, 19);

        const int RequestedMineral = AdjacentEM.CreateEntity();
        AdjacentEM.AddComponent<Game::Components::TransformComponent>(RequestedMineral).Position =
            {180.0f, 100.0f};
        auto& RequestedUnit =
            AdjacentEM.AddComponent<Game::Components::UnitComponent>(RequestedMineral);
        RequestedUnit.ChkUnitID = 176;
        auto& RequestedResource =
            AdjacentEM.AddComponent<Game::Components::ResourceComponent>(RequestedMineral);
        RequestedResource.Type = Game::Components::ResourceType::Minerals;
        RequestedResource.Amount = 1500;
        RequestedResource.MaxAmount = 1500;
        auto& RequestedPath =
            AdjacentEM.AddComponent<Game::Components::PathfindingComponent>(RequestedMineral);
        SetCollisionBounds(RequestedPath, 32, 20, 31, 19);

        const bool BusyBlockingMineral = WorkerChkUnitID == 0x07;
        int ExistingGatherer = -1;
        if (BusyBlockingMineral)
        {
            ExistingGatherer = AdjacentEM.CreateEntity();
            auto& ExistingHarvest =
                AdjacentEM.AddComponent<Game::Components::HarvestComponent>(
                    ExistingGatherer);
            ExistingHarvest.State = Game::Components::HarvestState::Mining;
            ExistingHarvest.TargetResourceID = BlockingMineral;
            BlockingResource.ActiveGathererEntityID = ExistingGatherer;
        }

        Require(Game::Systems::HarvestSystem::IssueHarvest(
                    AdjacentEM, {Worker}, RequestedMineral),
            "worker can start toward a mineral hidden behind an adjacent patch");
        // Match a strict static-avoid request which found no exit: movement
        // leaves no route, marks the destination failed, and preserves the
        // exact mineral blocker for the owning harvest state machine.
        WorkerPath.Path.clear();
        WorkerPath.PathIndex = 0;
        WorkerPath.HasRecentMoveFailure = true;
        WorkerPath.RecentFailedMoveTarget = WorkerPath.RequestedDestination;
        WorkerPath.BlockedByEntityID = BlockingMineral;

        Game::Systems::HarvestSystem AdjacentHarvestSystem(AdjacentMineralSysMgr);
        AdjacentHarvestSystem.Update(0.1f);

        const auto* WorkerHarvest =
            AdjacentEM.GetComponent<Game::Components::HarvestComponent>(Worker);
        const auto* WorkerCommand =
            AdjacentEM.GetComponent<Game::Components::CommandComponent>(Worker);
        Require(WorkerHarvest &&
                WorkerHarvest->TargetResourceID == BlockingMineral &&
                WorkerHarvest->DesiredResourceID == BlockingMineral &&
                WorkerCommand &&
                WorkerCommand->CurrentCommand == Game::Components::CommandType::Harvest &&
                WorkerCommand->TargetEntityID == BlockingMineral,
            WorkerChkUnitID == 0x07
                ? "SCV retargets the adjacent mineral reported as its exact blocker"
                : WorkerChkUnitID == 0x29
                    ? "Drone retargets the adjacent mineral reported as its exact blocker"
                    : "Probe retargets the adjacent mineral reported as its exact blocker");
        Require(std::find(RequestedResource.QueuedGatherers.begin(),
                          RequestedResource.QueuedGatherers.end(), Worker) ==
                    RequestedResource.QueuedGatherers.end() &&
                (BlockingResource.ActiveGathererEntityID == Worker ||
                 std::find(BlockingResource.QueuedGatherers.begin(),
                           BlockingResource.QueuedGatherers.end(), Worker) !=
                     BlockingResource.QueuedGatherers.end()),
            "adjacent-mineral reroute leaves the unreachable queue and joins the blocker");
        Require(WorkerPath.Path.empty() &&
                WorkerPath.RequestedIgnoreEntityID == BlockingMineral &&
                !WorkerPath.HasRecentMoveFailure &&
                WorkerPath.RequestedDestination.DistanceTo({64.0f, 100.0f}) <= 0.1f,
            "no-route worker adopts the adjacent mineral without retrying the blocked route");

        if (BusyBlockingMineral)
        {
            Require(WorkerHarvest->State ==
                        Game::Components::HarvestState::QueuedAtResource,
                "SCV waits at an occupied blocking mineral instead of oscillating");
            AdjacentHarvestSystem.Update(0.1f);
            Require(WorkerHarvest->TargetResourceID == BlockingMineral &&
                    WorkerHarvest->State ==
                        Game::Components::HarvestState::QueuedAtResource &&
                    WorkerPath.Path.empty(),
                "queued SCV does not spread back to the unreachable mineral behind it");
            auto* ExistingHarvest =
                AdjacentEM.GetComponent<Game::Components::HarvestComponent>(
                    ExistingGatherer);
            Require(ExistingHarvest != nullptr,
                "busy mineral exposes its active gatherer state");
            ExistingHarvest->State = Game::Components::HarvestState::Idle;
            AdjacentHarvestSystem.Update(0.1f);
            Require(WorkerHarvest->State == Game::Components::HarvestState::Mining &&
                    BlockingResource.ActiveGathererEntityID == Worker,
                "stale active gatherer is pruned so the waiting SCV can mine");
        }
        else
        {
            Require(WorkerHarvest->State == Game::Components::HarvestState::Mining &&
                    BlockingResource.ActiveGathererEntityID == Worker,
                "free adjacent mineral begins mining immediately after blocker reroute");
        }
    }

    // Exercise the real runtime order (Movement then Harvest) on a map-backed
    // route.  The mineral approach is behind a neutral geyser whose exact DAT
    // rectangle intersects the straight segment even though tile centers do
    // not.  The old stall refresh cleared BlockedByEntityID and rebuilt the
    // same one-node route forever; the strict static-avoid refresh must carry
    // the worker around without treating the geyser as passable.
    Core::ECS::SystemManager OrderedDetourSysMgr;
    auto& OrderedDetourEM = OrderedDetourSysMgr.GetEntityManager();
    Game::Map::GameMap OrderedDetourMap;
    OrderedDetourMap.Generate(16, 8);
    Game::Systems::CommandSystem::SetMap(&OrderedDetourMap);

    const int OrderedProbe = OrderedDetourEM.CreateEntity();
    auto& OrderedProbeTransform =
        OrderedDetourEM.AddComponent<Game::Components::TransformComponent>(OrderedProbe);
    OrderedProbeTransform.Position = {267.0f, 111.0f};
    auto& OrderedProbeUnit =
        OrderedDetourEM.AddComponent<Game::Components::UnitComponent>(OrderedProbe);
    OrderedProbeUnit.ChkUnitID = 64;
    OrderedProbeUnit.Speed = 2.8f;
    OrderedProbeUnit.HitPoints = 20;
    OrderedDetourEM.AddComponent<Game::Components::PlayerComponent>(OrderedProbe).PlayerID = 0;
    auto& OrderedProbePath =
        OrderedDetourEM.AddComponent<Game::Components::PathfindingComponent>(OrderedProbe);
    SetCollisionBounds(OrderedProbePath, 11, 11, 11, 11);

    const int BlockingGeyser = OrderedDetourEM.CreateEntity();
    auto& BlockingGeyserTransform =
        OrderedDetourEM.AddComponent<Game::Components::TransformComponent>(BlockingGeyser);
    BlockingGeyserTransform.Position = {192.0f, 128.0f};
    auto& BlockingGeyserUnit =
        OrderedDetourEM.AddComponent<Game::Components::UnitComponent>(BlockingGeyser);
    BlockingGeyserUnit.ChkUnitID = 188;
    BlockingGeyserUnit.HitPoints = 500;
    auto& BlockingGeyserResource =
        OrderedDetourEM.AddComponent<Game::Components::ResourceComponent>(BlockingGeyser);
    BlockingGeyserResource.Type = Game::Components::ResourceType::VespeneGas;
    BlockingGeyserResource.Amount = 5000;
    auto& BlockingGeyserPath =
        OrderedDetourEM.AddComponent<Game::Components::PathfindingComponent>(BlockingGeyser);
    SetCollisionBounds(BlockingGeyserPath, 64, 32, 63, 31);

    const int OrderedMineral = OrderedDetourEM.CreateEntity();
    OrderedDetourEM.AddComponent<Game::Components::TransformComponent>(OrderedMineral).Position =
        {64.0f, 96.0f};
    auto& OrderedMineralUnit =
        OrderedDetourEM.AddComponent<Game::Components::UnitComponent>(OrderedMineral);
    OrderedMineralUnit.ChkUnitID = 176;
    OrderedMineralUnit.HitPoints = 1;
    auto& OrderedMineralResource =
        OrderedDetourEM.AddComponent<Game::Components::ResourceComponent>(OrderedMineral);
    OrderedMineralResource.Type = Game::Components::ResourceType::Minerals;
    OrderedMineralResource.Amount = 1500;
    OrderedMineralResource.MaxAmount = 1500;
    auto& OrderedMineralPath =
        OrderedDetourEM.AddComponent<Game::Components::PathfindingComponent>(OrderedMineral);
    SetCollisionBounds(OrderedMineralPath, 32, 16, 31, 15);

    Require(Game::Systems::HarvestSystem::IssueHarvest(
                OrderedDetourEM, {OrderedProbe}, OrderedMineral),
        "map-backed worker can start harvesting behind a static resource");
    Game::Systems::MovementSystem OrderedMovement(
        OrderedDetourSysMgr, &OrderedDetourMap);
    Game::Systems::HarvestSystem OrderedHarvest(OrderedDetourSysMgr);
    float OrderedDetourY = 0.0f;
    for (int Step = 0; Step < 320; ++Step)
    {
        OrderedMovement.Update(0.05f);
        OrderedHarvest.Update(0.05f);
        Require(OrderedProbePath.RequestedIgnoreEntityID != BlockingGeyser,
            "static detour never turns the blocking geyser into a pass-through ignore");
        Require(!CollisionBoundsOverlap(
                    OrderedProbeTransform, OrderedProbePath,
                    BlockingGeyserTransform, BlockingGeyserPath),
            "harvest detour never overlaps the non-target geyser DAT footprint");
        OrderedDetourY = std::max(
            OrderedDetourY, std::fabs(OrderedProbeTransform.Position.Y - 111.0f));
    }
    const auto* OrderedHarvestState =
        OrderedDetourEM.GetComponent<Game::Components::HarvestComponent>(OrderedProbe);
    Require(OrderedHarvestState &&
            OrderedHarvestState->TargetResourceID == OrderedMineral &&
            OrderedMineralResource.Amount < 1500,
        "Movement-then-Harvest loop reaches the mineral instead of repeating a cleared blocker");
    Require(OrderedProbeTransform.Position.X < 128.0f && OrderedDetourY > 16.0f,
        "harvest worker takes a real lateral route around the neutral static footprint");

    Core::ECS::SystemManager GasSysMgr;
    auto& GasEM = GasSysMgr.GetEntityManager();
    auto& GasResources = Game::Systems::HarvestSystem::GetResources(0);
    GasResources.Minerals = 50;
    GasResources.VespeneGas = 0;
    GasResources.SupplyUsed = 0;
    GasResources.SupplyMax = 10;

    int GasScv = GasEM.CreateEntity();
    auto& GasScvTrans = GasEM.AddComponent<Game::Components::TransformComponent>(GasScv);
    GasScvTrans.Position = {100.0f, 100.0f};
    auto& GasScvUnit = GasEM.AddComponent<Game::Components::UnitComponent>(GasScv);
    GasScvUnit.ChkUnitID = 7;
    GasScvUnit.Speed = 2.8f;
    GasEM.AddComponent<Game::Components::PlayerComponent>(GasScv).PlayerID = 0;
    GasEM.AddComponent<Game::Components::RenderComponent>(GasScv);

    int QueuedGasScv = GasEM.CreateEntity();
    auto& QueuedGasScvTrans =
        GasEM.AddComponent<Game::Components::TransformComponent>(QueuedGasScv);
    QueuedGasScvTrans.Position = {100.0f, 132.0f};
    auto& QueuedGasScvUnit =
        GasEM.AddComponent<Game::Components::UnitComponent>(QueuedGasScv);
    QueuedGasScvUnit.ChkUnitID = 7;
    QueuedGasScvUnit.Speed = 2.8f;
    GasEM.AddComponent<Game::Components::PlayerComponent>(QueuedGasScv).PlayerID = 0;
    GasEM.AddComponent<Game::Components::RenderComponent>(QueuedGasScv);

    int ThirdGasScv = GasEM.CreateEntity();
    auto& ThirdGasScvTrans =
        GasEM.AddComponent<Game::Components::TransformComponent>(ThirdGasScv);
    ThirdGasScvTrans.Position = {148.0f, 132.0f};
    auto& ThirdGasScvUnit =
        GasEM.AddComponent<Game::Components::UnitComponent>(ThirdGasScv);
    ThirdGasScvUnit.ChkUnitID = 7;
    ThirdGasScvUnit.Speed = 2.8f;
    GasEM.AddComponent<Game::Components::PlayerComponent>(ThirdGasScv).PlayerID = 0;
    GasEM.AddComponent<Game::Components::RenderComponent>(ThirdGasScv);

    int Refinery = CreateCompletedRefinery(GasEM, {124.0f, 100.0f}, 0, 5000);
    const int GasDepot = CreateCompletedDepot(GasEM, {300.0f, 100.0f}, 0);
    auto* GasResource =
        GasEM.GetComponent<Game::Components::ResourceComponent>(Refinery);

    Require(Game::Systems::HarvestSystem::IssueHarvest(
                GasEM, {GasScv, QueuedGasScv, ThirdGasScv}, Refinery),
        "multiple SCVs can queue a harvest order on a completed owned refinery");
    auto* GasCmd = GasEM.GetComponent<Game::Components::CommandComponent>(GasScv);
    auto* GasPath = GasEM.GetComponent<Game::Components::PathfindingComponent>(GasScv);
    auto* GasHarvest = GasEM.GetComponent<Game::Components::HarvestComponent>(GasScv);
    auto* QueuedGasPath =
        GasEM.GetComponent<Game::Components::PathfindingComponent>(QueuedGasScv);
    auto* QueuedGasHarvest =
        GasEM.GetComponent<Game::Components::HarvestComponent>(QueuedGasScv);
    auto* ThirdGasPath =
        GasEM.GetComponent<Game::Components::PathfindingComponent>(ThirdGasScv);
    Require(GasCmd && GasCmd->CurrentCommand == Game::Components::CommandType::Harvest,
        "gas harvest command starts as a harvest order");
    Require(GasHarvest && GasHarvest->State == Game::Components::HarvestState::MovingToResource,
        "gas harvest starts by moving to the refinery");
    Require(GasPath && QueuedGasPath && ThirdGasPath &&
            GasPath->RequestedDestination.X < 60.0f &&
            std::abs(GasPath->RequestedDestination.Y - 100.0f) <= 0.1f &&
            QueuedGasPath->RequestedDestination.Y > 139.0f &&
            ThirdGasPath->RequestedDestination.Y > 139.0f &&
            QueuedGasPath->RequestedDestination.X < 124.0f &&
            ThirdGasPath->RequestedDestination.X > 124.0f,
        "gas workers approach the nearest reachable refinery edge instead of one fixed doorway");

    // Like openbw, any refinery edge contact can enter once this SCV owns the
    // gather slot. It disappears at the boundary rather than walking visibly
    // to the refinery center.
    const Core::Math::Vec2 GasEntryContact{171.0f, 150.0f};
    GasScvTrans.Position = GasEntryContact;
    QueuedGasScvTrans.Position = {80.0f, 220.0f};
    ThirdGasScvTrans.Position = {220.0f, 220.0f};
    GasPath->Path.clear();
    GasPath->PathIndex = 0;
    Game::Systems::HarvestSystem GasHarvestSystem(GasSysMgr);
    GasHarvestSystem.Update(0.1f);
    auto* LiveGasScvRender =
        GasEM.GetComponent<Game::Components::RenderComponent>(GasScv);
    auto* LiveQueuedGasScvRender =
        GasEM.GetComponent<Game::Components::RenderComponent>(QueuedGasScv);
    Require(GasHarvest->State == Game::Components::HarvestState::Mining &&
            GasResource && GasResource->ActiveGathererEntityID == GasScv &&
            GasHarvest->InsideGasBuilding &&
            LiveGasScvRender && !LiveGasScvRender->Visible &&
            LiveQueuedGasScvRender && LiveQueuedGasScvRender->Visible,
        "front gas worker hides immediately upon valid refinery edge contact");

    auto* LiveGasScvTrans =
        GasEM.GetComponent<Game::Components::TransformComponent>(GasScv);
    Require(LiveGasScvTrans != nullptr,
        "front gas worker transform remains available after queue creation");
    Require(GasHarvest->State == Game::Components::HarvestState::Mining,
        "front gas worker starts mining immediately after reaching the refinery doorway");
    Require(GasHarvest->InsideGasBuilding,
        "front gas worker records that it is inside the refinery");
    Require(LiveGasScvRender && !LiveGasScvRender->Visible,
        "front gas worker hides while it mines inside the refinery");
    Require(GasResource->ActiveGathererEntityID == GasScv,
        "front gas worker retains the active refinery slot while mining");
    Require(QueuedGasPath->RequestedDestination.Y > 139.0f,
        "waiting gas worker retains its own nearest-edge approach while the refinery is occupied");

    GasHarvestSystem.Update(GasHarvest->MineInterval);
    Require(GasHarvest->State == Game::Components::HarvestState::MovingToDepot &&
            !GasHarvest->InsideGasBuilding && LiveGasScvRender->Visible &&
            LiveGasScvTrans->Position.X > 187.0f &&
            std::abs(LiveGasScvTrans->Position.Y - 100.0f) <= 0.1f &&
            LiveGasScvTrans->Position.DistanceTo(GasEntryContact) > 30.0f,
        "worker exits independently on the refinery side facing the nearest depot");
    Require(GasHarvest->CarryingType == Game::Components::ResourceType::VespeneGas,
        "worker records carried gas before returning");

    GasHarvestSystem.Update(0.1f);
    Require(QueuedGasHarvest && QueuedGasPath &&
            GasResource->ActiveGathererEntityID == -1 &&
            QueuedGasHarvest->State == Game::Components::HarvestState::MovingToResource &&
            QueuedGasPath->RequestedDestination.Y > 139.0f,
        "next queued gas worker advances to its nearest refinery edge after the previous worker exits");
    auto* LiveQueuedGasScvTrans =
        GasEM.GetComponent<Game::Components::TransformComponent>(QueuedGasScv);
    Require(LiveQueuedGasScvTrans != nullptr,
        "queued gas worker transform remains available after queue creation");
    LiveQueuedGasScvTrans->Position = QueuedGasPath->RequestedDestination;
    QueuedGasPath->Path.clear();
    QueuedGasPath->PathIndex = 0;
    GasHarvestSystem.Update(0.1f);
    Require(GasResource->ActiveGathererEntityID == QueuedGasScv &&
            QueuedGasHarvest->InsideGasBuilding &&
            LiveQueuedGasScvRender && !LiveQueuedGasScvRender->Visible,
        "next queued gas worker hides immediately at its own refinery edge contact");

    LiveGasScvTrans->Position = {252.0f, 100.0f};
    GasPath->Path.clear();
    GasPath->PathIndex = 0;
    auto* MorphingGasDepot =
        GasEM.GetComponent<Game::Components::BuildingComponent>(GasDepot);
    auto* MorphingGasDepotUnit =
        GasEM.GetComponent<Game::Components::UnitComponent>(GasDepot);
    Require(MorphingGasDepot && MorphingGasDepotUnit,
        "gas depot components are available for zerg upgrade delivery regression");
    MorphingGasDepot->BuildingDefID = HatcheryDefID;
    MorphingGasDepot->UnderConstruction = true;
    MorphingGasDepotUnit->ChkUnitID =
        Game::Races::GetUnitDefByID(HatcheryDefID).ChkUnitID;
    auto& GasDepotMorph =
        GasEM.AddComponent<Game::Components::MorphComponent>(GasDepot);
    GasDepotMorph.IsMorphing = true;
    GasDepotMorph.SourceUnitID = GasDepot;
    GasDepotMorph.MorphTargetDefID = LairDefID;
    GasHarvestSystem.Update(0.1f);
    Require(EntityEdgeDistance(GasEM, LiveGasScvTrans->Position, GasDepot) <= 0.1f,
        "gas return test places the worker inside the depot footprint");
    Require(GasResources.VespeneGas == 8,
        "returning gas worker deposits vespene while Hatchery upgrades to Lair");
    Require(GasResources.Minerals == 50,
        "gas deposit does not change mineral count");
    Require(GasHarvest->CarryingAmount == 0 &&
            GasHarvest->State == Game::Components::HarvestState::MovingToResource,
        "gas worker resumes the refinery loop after depositing");
    Require(GasCmd->CurrentCommand == Game::Components::CommandType::Harvest &&
            GasCmd->TargetEntityID == Refinery,
        "gas worker keeps harvest command and refinery target after deposit");
    Require(GasPath->RequestedDestination.DistanceTo({124.0f, 100.0f}) > 39.0f &&
            GasResource &&
            std::find(GasResource->QueuedGatherers.begin(),
                      GasResource->QueuedGatherers.end(), GasScv) !=
                GasResource->QueuedGatherers.end() &&
            GasPath->RequestedIgnoreEntityID == GasDepot &&
            !GasPath->Path.empty(),
        "gas worker rejoins the refinery doorway queue after delivery instead of targeting its blocked center");

    GasResource->Amount = 0;
    GasResource->IsDepleted = true;
    GasResource->ActiveGathererEntityID = GasScv;
    GasResource->QueuedGatherers.erase(
        std::remove(GasResource->QueuedGatherers.begin(),
                    GasResource->QueuedGatherers.end(), GasScv),
        GasResource->QueuedGatherers.end());
    GasHarvest->State = Game::Components::HarvestState::Mining;
    GasHarvest->CarryingAmount = 0;
    GasHarvest->CarryingType = Game::Components::ResourceType::None;
    GasHarvest->InsideGasBuilding = true;
    GasHarvest->MineTimer = 0.0f;
    LiveGasScvTrans->Position = {124.0f, 100.0f};
    LiveGasScvRender->Visible = false;
    Require(Game::Systems::HarvestSystem::CanHarvest(GasEM, GasScv, Refinery),
        "openbw-compatible depleted refinery remains a valid gas target");
    GasHarvestSystem.Update(GasHarvest->MineInterval);
    Require(GasHarvest->State == Game::Components::HarvestState::MovingToDepot &&
            GasHarvest->CarryingAmount == 2 &&
            GasHarvest->CarryingType == Game::Components::ResourceType::VespeneGas &&
            LiveGasScvRender->Visible,
        "depleted refinery yields two gas and releases the worker for its return trip");

    Core::ECS::SystemManager DeadDepotSysMgr;
    auto& DeadDepotEM = DeadDepotSysMgr.GetEntityManager();
    constexpr int DeadDepotPlayerID = 5;
    auto& DeadDepotResources =
        Game::Systems::HarvestSystem::GetResources(DeadDepotPlayerID);
    DeadDepotResources.Minerals = 0;
    DeadDepotResources.VespeneGas = 0;
    DeadDepotResources.SupplyUsed = 0;
    DeadDepotResources.SupplyMax = 10;

    int CarryingScv = DeadDepotEM.CreateEntity();
    DeadDepotEM.AddComponent<Game::Components::TransformComponent>(CarryingScv).Position =
        {200.0f, 132.0f};
    auto& CarryingScvUnit =
        DeadDepotEM.AddComponent<Game::Components::UnitComponent>(CarryingScv);
    CarryingScvUnit.ChkUnitID = 7;
    CarryingScvUnit.Speed = 2.8f;
    DeadDepotEM.AddComponent<Game::Components::PlayerComponent>(CarryingScv).PlayerID =
        DeadDepotPlayerID;
    auto& CarryingPath =
        DeadDepotEM.AddComponent<Game::Components::PathfindingComponent>(CarryingScv);
    auto& CarryingCmd =
        DeadDepotEM.AddComponent<Game::Components::CommandComponent>(CarryingScv);
    CarryingCmd.CurrentCommand = Game::Components::CommandType::Harvest;

    int DeadDepotMineral = DeadDepotEM.CreateEntity();
    DeadDepotEM.AddComponent<Game::Components::TransformComponent>(DeadDepotMineral).Position =
        {124.0f, 132.0f};
    auto& DeadDepotResource =
        DeadDepotEM.AddComponent<Game::Components::ResourceComponent>(DeadDepotMineral);
    DeadDepotResource.Type = Game::Components::ResourceType::Minerals;
    DeadDepotResource.Amount = 1500;
    DeadDepotResource.MaxAmount = 1500;

    const int DeadDepot =
        CreateCompletedDepot(DeadDepotEM, {200.0f, 132.0f}, DeadDepotPlayerID);
    auto* DeadDepotUnit =
        DeadDepotEM.GetComponent<Game::Components::UnitComponent>(DeadDepot);
    Require(DeadDepotUnit != nullptr, "dead depot regression has depot unit data");
    DeadDepotUnit->HitPoints = 0;

    auto& CarryingHarvest =
        DeadDepotEM.AddComponent<Game::Components::HarvestComponent>(CarryingScv);
    CarryingHarvest.State = Game::Components::HarvestState::MovingToDepot;
    CarryingHarvest.CarryingType = Game::Components::ResourceType::Minerals;
    CarryingHarvest.CarryingAmount = 8;
    CarryingHarvest.TargetResourceID = DeadDepotMineral;
    CarryingHarvest.ReturnBuildingID = DeadDepot;

    Game::Systems::HarvestSystem DeadDepotHarvestSystem(DeadDepotSysMgr);
    DeadDepotHarvestSystem.Update(0.1f);
    Require(DeadDepotResources.Minerals == 0,
        "worker cannot deposit carried minerals into a destroyed depot");
    Require(CarryingHarvest.CarryingAmount == 8 &&
            CarryingHarvest.CarryingType == Game::Components::ResourceType::Minerals,
        "failed deposit keeps worker cargo instead of losing resources");
    Require(CarryingHarvest.State == Game::Components::HarvestState::Idle,
        "worker stops when no valid depot remains for carried resources");
    Require(CarryingCmd.CurrentCommand == Game::Components::CommandType::Stop &&
            CarryingCmd.TargetEntityID == -1,
        "destroyed depot return clears harvest command target");
    Require(CarryingPath.Path.empty() &&
            CarryingPath.CurrentSpeed == 0.0f &&
            CarryingPath.CurrentVelocity.Length() == 0.0f,
        "destroyed depot return clears stale movement");
    Require(Game::Systems::HarvestSystem::IssueHarvest(
                DeadDepotEM, {CarryingScv}, DeadDepotMineral),
        "loaded worker can receive a replacement mineral order");
    Require(CarryingHarvest.CarryingAmount == 8 &&
            CarryingHarvest.CarryingType == Game::Components::ResourceType::Minerals,
        "replacement harvest order preserves visible carried minerals");

    const int LiftedDepot =
        CreateCompletedDepot(DeadDepotEM, {240.0f, 132.0f}, DeadDepotPlayerID);
    auto* LiftedDepotBuilding =
        DeadDepotEM.GetComponent<Game::Components::BuildingComponent>(LiftedDepot);
    Require(LiftedDepotBuilding != nullptr, "lifted depot regression has building data");
    LiftedDepotBuilding->IsLifted = true;
    CarryingHarvest.State = Game::Components::HarvestState::MovingToDepot;
    CarryingHarvest.ReturnBuildingID = LiftedDepot;
    CarryingCmd.CurrentCommand = Game::Components::CommandType::Harvest;
    DeadDepotHarvestSystem.Update(0.1f);
    Require(CarryingHarvest.CarryingAmount == 8 &&
            CarryingHarvest.State == Game::Components::HarvestState::Idle &&
            CarryingCmd.CurrentCommand == Game::Components::CommandType::Stop,
        "SCV cannot deliver cargo to a lifted Command Center");

    Core::ECS::SystemManager BlockedApproachSysMgr;
    auto& BlockedEM = BlockedApproachSysMgr.GetEntityManager();
    Game::Map::GameMap BlockedMap;
    BlockedMap.Generate(12, 8);
    Game::Map::Tile BlockedTile = BlockedMap.GetTile(4, 3);
    BlockedTile.Walkable = false;
    BlockedTile.Buildable = false;
    BlockedMap.SetTile(4, 3, BlockedTile);
    Game::Systems::CommandSystem::SetMap(&BlockedMap);

    int BlockedScv = BlockedEM.CreateEntity();
    BlockedEM.AddComponent<Game::Components::TransformComponent>(BlockedScv).Position =
        {100.0f, 100.0f};
    auto& BlockedScvUnit = BlockedEM.AddComponent<Game::Components::UnitComponent>(BlockedScv);
    BlockedScvUnit.ChkUnitID = 7;
    BlockedScvUnit.Speed = 2.8f;
    BlockedEM.AddComponent<Game::Components::PlayerComponent>(BlockedScv).PlayerID = 0;

    int BlockedMineral = BlockedEM.CreateEntity();
    BlockedEM.AddComponent<Game::Components::TransformComponent>(BlockedMineral).Position =
        {160.0f, 100.0f};
    auto& BlockedMineralUnit = BlockedEM.AddComponent<Game::Components::UnitComponent>(BlockedMineral);
    BlockedMineralUnit.ChkUnitID = 176;
    auto& BlockedResource = BlockedEM.AddComponent<Game::Components::ResourceComponent>(BlockedMineral);
    BlockedResource.Type = Game::Components::ResourceType::Minerals;
    BlockedResource.Amount = 1500;
    BlockedResource.MaxAmount = 1500;
    CreateCompletedDepot(BlockedEM, {240.0f, 100.0f}, 0);

    Require(Game::Systems::HarvestSystem::IssueHarvest(BlockedEM, {BlockedScv}, BlockedMineral),
        "SCV can receive harvest order when the nearest mineral edge tile is blocked");
    auto* BlockedPath =
        BlockedEM.GetComponent<Game::Components::PathfindingComponent>(BlockedScv);
    Require(BlockedPath && !BlockedPath->Path.empty(),
        "harvest command samples alternate approach points when the primary edge point is unreachable");
    Require(BlockedPath->Path.back().X != 128.0f || BlockedPath->Path.back().Y != 100.0f,
        "alternate harvest approach does not keep the blocked primary edge waypoint");
    Game::Systems::CommandSystem::SetMap(nullptr);

    Core::ECS::SystemManager QueueSysMgr;
    auto& QueueEM = QueueSysMgr.GetEntityManager();
    const auto CreateQueuedScv = [&](Core::Math::Vec2 Position)
    {
        const int ID = QueueEM.CreateEntity();
        QueueEM.AddComponent<Game::Components::TransformComponent>(ID).Position = Position;
        auto& Unit = QueueEM.AddComponent<Game::Components::UnitComponent>(ID);
        Unit.ChkUnitID = 7;
        Unit.Speed = 2.8f;
        QueueEM.AddComponent<Game::Components::PlayerComponent>(ID).PlayerID = 0;
        return ID;
    };
    const int QueueScvA = CreateQueuedScv({24.0f, 0.0f});
    const int QueueScvB = CreateQueuedScv({24.0f, 32.0f});
    std::vector<int> QueueScvs = {QueueScvA, QueueScvB};
    for (int I = 2; I < 14; ++I)
        QueueScvs.push_back(CreateQueuedScv({24.0f, 32.0f + (float)I * 4.0f}));
    const int QueueMineral = QueueEM.CreateEntity();
    QueueEM.AddComponent<Game::Components::TransformComponent>(QueueMineral).Position =
        {128.0f, 0.0f};
    auto& QueueMineralUnit = QueueEM.AddComponent<Game::Components::UnitComponent>(QueueMineral);
    QueueMineralUnit.ChkUnitID = 176;
    auto& QueueResource = QueueEM.AddComponent<Game::Components::ResourceComponent>(QueueMineral);
    QueueResource.Type = Game::Components::ResourceType::Minerals;
    QueueResource.Amount = 1500;
    QueueResource.MaxAmount = 1500;
    auto& QueueMineralPath =
        QueueEM.AddComponent<Game::Components::PathfindingComponent>(QueueMineral);
    SetCollisionBounds(QueueMineralPath, 32, 20, 31, 19);

    const int AlternateMineral = QueueEM.CreateEntity();
    QueueEM.AddComponent<Game::Components::TransformComponent>(AlternateMineral).Position =
        {128.0f, 64.0f};
    auto& AlternateMineralUnit =
        QueueEM.AddComponent<Game::Components::UnitComponent>(AlternateMineral);
    AlternateMineralUnit.ChkUnitID = 176;
    auto& AlternateResource =
        QueueEM.AddComponent<Game::Components::ResourceComponent>(AlternateMineral);
    AlternateResource.Type = Game::Components::ResourceType::Minerals;
    AlternateResource.Amount = 1500;
    AlternateResource.MaxAmount = 1500;
    auto& AlternatePath =
        QueueEM.AddComponent<Game::Components::PathfindingComponent>(AlternateMineral);
    SetCollisionBounds(AlternatePath, 32, 20, 31, 19);

    Require(Game::Systems::HarvestSystem::IssueHarvest(
                QueueEM, QueueScvs, QueueMineral),
        "multi-selection harvest order is accepted");
    const auto* QueuePathA =
        QueueEM.GetComponent<Game::Components::PathfindingComponent>(QueueScvA);
    const auto* QueuePathB =
        QueueEM.GetComponent<Game::Components::PathfindingComponent>(QueueScvB);
    const size_t PrimaryLoad = QueueResource.QueuedGatherers.size() +
        (QueueResource.ActiveGathererEntityID >= 0 ? 1u : 0u);
    const size_t AlternateLoad = AlternateResource.QueuedGatherers.size() +
        (AlternateResource.ActiveGathererEntityID >= 0 ? 1u : 0u);
    const size_t LoadDifference = PrimaryLoad > AlternateLoad
        ? PrimaryLoad - AlternateLoad
        : AlternateLoad - PrimaryLoad;
    Require(PrimaryLoad + AlternateLoad == QueueScvs.size() &&
            LoadDifference <= 1,
        "multi-selection harvest balances workers across nearby mineral patches");
    Require(QueuePathA && QueuePathB && !QueuePathA->Path.empty() &&
            !QueuePathB->Path.empty(),
        "balanced harvesters each receive a movement destination");

    for (int QueueScv : QueueScvs)
    {
        const auto* ApproachPath =
            QueueEM.GetComponent<Game::Components::PathfindingComponent>(QueueScv);
        const auto* AssignedHarvest =
            QueueEM.GetComponent<Game::Components::HarvestComponent>(QueueScv);
        Require(ApproachPath && !ApproachPath->Path.empty() &&
                AssignedHarvest &&
                (AssignedHarvest->TargetResourceID == QueueMineral ||
                 AssignedHarvest->TargetResourceID == AlternateMineral),
            "every SCV receives a path to one balanced mineral assignment");
    }
    const auto* QueueHarvestA =
        QueueEM.GetComponent<Game::Components::HarvestComponent>(QueueScvA);
    const auto* QueueHarvestB =
        QueueEM.GetComponent<Game::Components::HarvestComponent>(QueueScvB);
    Require(QueueHarvestA && QueueHarvestB &&
            QueueHarvestA->TargetResourceID != QueueHarvestB->TargetResourceID,
        "the first two SCVs spread immediately instead of stacking at one patch");

    std::printf("HarvestSystemCommandTest passed\n");
    return 0;
}
