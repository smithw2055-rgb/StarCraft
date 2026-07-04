#include "Game/Systems/ProductionSystem.h"

#include "Game/Races/RaceDef.h"
#include "Game/Systems/HarvestSystem.h"
#include "Core/ECS/SystemManager.h"

#include <cstdio>
#include <cstdlib>

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

    int CreateCompletedBuilding(Core::ECS::EntityManager& EM,
                                const char* DefName,
                                int PlayerID,
                                Core::Math::Vec2 Position = {0.0f, 0.0f})
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.UnderConstruction = false;
        Building.BuildingDefID = DefID;
        return EntityID;
    }
}

int main()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();

    constexpr int PlayerID = 0;
    auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerID);
    Resources.Minerals = 100;
    Resources.VespeneGas = 0;
    Resources.SupplyUsed = 9;
    Resources.SupplyMax = 10;

    const int CommandCenter = CreateCompletedBuilding(EM, "CommandCenter", PlayerID);
    const int ScvDefID = Game::Races::GetUnitDefID("SCV");

    Require(Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "first SCV can be queued when one supply remains");
    Require(Resources.Minerals == 50, "training reserves mineral cost");
    Require(Resources.SupplyUsed == 10, "training reserves supply immediately");

    Require(!Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "second SCV is blocked by reserved supply");
    Require(Resources.Minerals == 50, "failed queue does not spend minerals");
    Require(Resources.SupplyUsed == 10, "failed queue does not change reserved supply");

    Require(Game::Systems::ProductionSystem::CancelTrain(EM, CommandCenter, 0),
        "cancel queued SCV succeeds");
    Require(Resources.Minerals == 100, "cancel refunds mineral cost");
    Require(Resources.SupplyUsed == 9, "cancel releases reserved supply");

    Resources.Minerals = 100;
    Resources.VespeneGas = 0;
    Resources.SupplyUsed = 8;
    Resources.SupplyMax = 10;

    Require(Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "first duplicate SCV can be queued for cancel regression");
    Require(Game::Systems::ProductionSystem::StartTrain(EM, CommandCenter, ScvDefID),
        "second duplicate SCV can be queued for cancel regression");

    Game::Systems::ProductionSystem Production(SysMgr);
    const auto ScvDef = Game::Races::GetUnitDefByID(ScvDefID);
    Production.Update((float)ScvDef.BuildTime * 0.5f);

    auto* Prod = EM.GetComponent<Game::Components::ProductionComponent>(CommandCenter);
    Require(Prod != nullptr, "command center has production component");
    Require(Prod->Queue.size() == 2, "duplicate SCV queue has two entries");
    Require(Prod->CurrentTrainingUnitDefID == ScvDefID,
        "first duplicate SCV is current training item");
    const float ProgressBeforeCancel = Prod->TrainingProgress;
    Require(ProgressBeforeCancel > 0.0f, "first duplicate SCV has training progress");

    Require(Game::Systems::ProductionSystem::CancelTrain(EM, CommandCenter, 1),
        "canceling second duplicate SCV succeeds");
    Require(Prod->Queue.size() == 1, "canceling second duplicate SCV removes only queued item");
    Require(Prod->CurrentTrainingUnitDefID == ScvDefID,
        "canceling second duplicate SCV keeps first SCV training");
    Require(Prod->TrainingProgress == ProgressBeforeCancel,
        "canceling second duplicate SCV preserves current training progress");
    Require(Resources.Minerals == 50, "canceling second duplicate SCV refunds one mineral cost");
    Require(Resources.SupplyUsed == 9, "canceling second duplicate SCV releases one reserved supply");

    Resources.Minerals = 200;
    Resources.VespeneGas = 100;
    Resources.SupplyUsed = 0;
    Resources.SupplyMax = 20;

    const int Factory = CreateCompletedBuilding(EM, "Factory", PlayerID, {400.0f, 400.0f});
    const int MachineShop = CreateCompletedBuilding(EM, "MachineShop", PlayerID, {448.0f, 400.0f});
    EM.GetComponent<Game::Components::BuildingComponent>(Factory)->AddonID = MachineShop;
    const int GoliathDefID = Game::Races::GetUnitDefID("Goliath");
    const auto GoliathDef = Game::Races::GetUnitDefByID(GoliathDefID);

    Require(Game::Systems::ProductionSystem::StartTrain(EM, Factory, GoliathDefID),
        "Goliath can be queued from a completed factory with machine shop");
    Production.Update((float)GoliathDef.BuildTime);

    int SpawnedGoliath = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                           Game::Components::CombatComponent>())
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        if (Unit && Unit->ChkUnitID == GoliathDef.ChkUnitID)
        {
            SpawnedGoliath = EntityID;
            break;
        }
    }

    Require(SpawnedGoliath >= 0, "completed production spawns the trained Goliath");
    auto* GoliathCombat = EM.GetComponent<Game::Components::CombatComponent>(SpawnedGoliath);
    Require(GoliathCombat && GoliathCombat->CanTargetGround,
        "produced combat units keep ground targeting capability");
    Require(GoliathCombat->CanTargetAir,
        "produced combat units keep air targeting capability");
    Require(GoliathCombat->GroundDamage == GoliathDef.GroundDamage &&
            GoliathCombat->AirDamage == GoliathDef.AirDamage,
        "produced combat units use their definition weapon damage");

    std::printf("ProductionEconomyTest passed\n");
    return 0;
}
