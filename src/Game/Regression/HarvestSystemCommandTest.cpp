#include "Game/Systems/HarvestSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/CombatSystem.h"

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
                                        const Core::Math::Vec2& Target)
{
    IssueMove(EM, {EntityID}, Target);
    if (auto* Path = EM.GetComponent<Components::PathfindingComponent>(EntityID))
        return !Path->Path.empty();
    return false;
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
    Require(EntityEdgeDistance(EM, Path->Path.front(), Depot) <= 17.0f,
        "return path targets the command center collision edge");
    Require(Path->Path.front().X < 300.0f - 64.0f,
        "return path stays outside the command center footprint instead of its center");

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

    ScvTrans->Position = {225.0f, 100.0f};
    Path->PathIndex = (int)Path->Path.size();
    Path->CurrentSpeed = 0.0f;
    Path->CurrentVelocity = {0.0f, 0.0f};
    HarvestSystem.Update(0.1f);
    Require(Resources.Minerals == 58,
        "worker deposits minerals after reaching the command center collision edge");
    Require(Harvest->CarryingAmount == 0 &&
            Harvest->State == Game::Components::HarvestState::MovingToResource,
        "mineral worker resumes the harvest loop after depositing at the depot");
    Require(Cmd->CurrentCommand == Game::Components::CommandType::Harvest &&
            Cmd->TargetEntityID == Mineral,
        "mineral worker keeps harvest command and mineral target after deposit");

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

    int Refinery = CreateCompletedRefinery(GasEM, {124.0f, 100.0f}, 0, 5000);
    const int GasDepot = CreateCompletedDepot(GasEM, {300.0f, 100.0f}, 0);

    Require(Game::Systems::HarvestSystem::IssueHarvest(GasEM, {GasScv}, Refinery),
        "SCV can receive a harvest order on a completed owned refinery");
    auto* GasCmd = GasEM.GetComponent<Game::Components::CommandComponent>(GasScv);
    auto* GasPath = GasEM.GetComponent<Game::Components::PathfindingComponent>(GasScv);
    auto* GasHarvest = GasEM.GetComponent<Game::Components::HarvestComponent>(GasScv);
    Require(GasCmd && GasCmd->CurrentCommand == Game::Components::CommandType::Harvest,
        "gas harvest command starts as a harvest order");
    Require(GasHarvest && GasHarvest->State == Game::Components::HarvestState::MovingToResource,
        "gas harvest starts by moving to the refinery");

    GasPath->Path.clear();
    GasPath->PathIndex = 0;
    Game::Systems::HarvestSystem GasHarvestSystem(GasSysMgr);
    GasHarvestSystem.Update(0.1f);
    Require(GasHarvest->State == Game::Components::HarvestState::Mining,
        "worker starts mining gas when inside refinery interaction range");

    GasHarvestSystem.Update(GasHarvest->MineInterval);
    Require(GasHarvest->State == Game::Components::HarvestState::MovingToDepot,
        "worker returns to depot after filling gas cargo");
    Require(GasHarvest->CarryingType == Game::Components::ResourceType::VespeneGas,
        "worker records carried gas before returning");

    GasScvTrans.Position = {225.0f, 100.0f};
    GasPath->Path.clear();
    GasPath->PathIndex = 0;
    GasHarvestSystem.Update(0.1f);
    Require(EntityEdgeDistance(GasEM, GasScvTrans.Position, GasDepot) <= 17.0f,
        "gas return test places the worker inside the depot collision boundary contact");
    Require(GasResources.VespeneGas == 8,
        "returning gas worker deposits vespene into player resources");
    Require(GasResources.Minerals == 50,
        "gas deposit does not change mineral count");
    Require(GasHarvest->CarryingAmount == 0 &&
            GasHarvest->State == Game::Components::HarvestState::MovingToResource,
        "gas worker resumes the refinery loop after depositing");
    Require(GasCmd->CurrentCommand == Game::Components::CommandType::Harvest &&
            GasCmd->TargetEntityID == Refinery,
        "gas worker keeps harvest command and refinery target after deposit");

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

    std::printf("HarvestSystemCommandTest passed\n");
    return 0;
}
