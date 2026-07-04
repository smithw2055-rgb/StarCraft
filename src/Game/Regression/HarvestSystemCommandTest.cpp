#include "Game/Systems/HarvestSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/CombatSystem.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

void CombatSystem::IssueAttack(Core::ECS::EntityManager&,
                               const std::vector<int>&,
                               int)
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
    const int Depot = CreateCompletedDepot(EM, {200.0f, 100.0f}, 0);

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
    const float DistToDepot = Path->Path.front().DistanceTo({200.0f, 100.0f});
    const float DistToMineral = Path->Path.front().DistanceTo({124.0f, 100.0f});
    Require(DistToDepot < DistToMineral,
        "return path approaches the depot instead of returning to the mineral edge");

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
