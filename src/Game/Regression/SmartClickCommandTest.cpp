#include "Main/GameCommandDispatchRuntime.h"

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Systems/CombatSystem.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

void CombatSystem::IssueAttack(Core::ECS::EntityManager& EM,
                               const std::vector<int>& Attackers,
                               int TargetEntityID)
{
    for (int EntityID : Attackers)
    {
        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityID);
        if (!Command)
            Command = &EM.AddComponent<Game::Components::CommandComponent>(EntityID);
        Command->CurrentCommand = Game::Components::CommandType::Attack;
        Command->TargetEntityID = TargetEntityID;
    }
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

    int CreateUnit(Core::ECS::EntityManager& EM,
                   int ChkUnitID,
                   Core::Math::Vec2 Position,
                   int PlayerID)
    {
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = ChkUnitID;
        Unit.HitPoints = 100;
        Unit.MaxHitPoints = 100;
        Unit.Speed = 3.0f;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        return EntityID;
    }

    Game::Components::CombatComponent& AddCombat(
        Core::ECS::EntityManager& EM,
        int EntityID,
        int GroundDamage,
        int AirDamage,
        bool CanTargetGround,
        bool CanTargetAir,
        bool IsAir = false)
    {
        auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
        Combat.GroundDamage = GroundDamage;
        Combat.AirDamage = AirDamage;
        Combat.CanTargetGround = CanTargetGround;
        Combat.CanTargetAir = CanTargetAir;
        Combat.AttackRange = 32.0f;
        Combat.MyElevation = IsAir ? Game::Components::Elevation::Air
                                   : Game::Components::Elevation::Ground;
        if (IsAir)
            EM.AddComponent<Game::Components::PathfindingComponent>(EntityID).IsAir = true;
        return Combat;
    }

    void TestMixedSelectionSmartClicksAirTarget()
    {
        Core::ECS::EntityManager EM;
        const int Marine = CreateUnit(EM, 0, {32.0f, 32.0f}, 0);
        AddCombat(EM, Marine, 6, 0, true, false);

        const int Goliath = CreateUnit(EM, 3, {48.0f, 32.0f}, 0);
        AddCombat(EM, Goliath, 12, 12, true, true);

        const int Mutalisk = CreateUnit(EM, 43, {160.0f, 96.0f}, 1);
        AddCombat(EM, Mutalisk, 9, 9, true, true, true);

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 96.0f};
        Target.AnyEntityID = Mutalisk;
        Target.EnemyEntityID = Mutalisk;

        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM,
            {Marine, Goliath},
            Target,
            Target.WorldPosition);

        auto* MarineCmd = EM.GetComponent<Game::Components::CommandComponent>(Marine);
        Require(MarineCmd && MarineCmd->CurrentCommand == Game::Components::CommandType::Move,
            "smart-click moves units that cannot attack an air target");
        Require(MarineCmd->TargetPosition.X == Target.WorldPosition.X &&
                MarineCmd->TargetPosition.Y == Target.WorldPosition.Y,
            "smart-click movement keeps the clicked air target position");

        auto* GoliathCmd = EM.GetComponent<Game::Components::CommandComponent>(Goliath);
        Require(GoliathCmd && GoliathCmd->CurrentCommand == Game::Components::CommandType::Attack,
            "smart-click attacks with units that can attack an air target");
        Require(GoliathCmd->TargetEntityID == Mutalisk,
            "smart-click attack targets the clicked air unit");
    }

    void TestWorkerSmartClicksMinerals()
    {
        Core::ECS::EntityManager EM;
        const int Scv = CreateUnit(EM, 7, {64.0f, 64.0f}, 0);

        const int Marine = CreateUnit(EM, 0, {80.0f, 64.0f}, 0);
        AddCombat(EM, Marine, 6, 0, true, false);

        const int Mineral = CreateUnit(EM, 176, {160.0f, 64.0f}, 11);
        auto& Resource = EM.AddComponent<Game::Components::ResourceComponent>(Mineral);
        Resource.Type = Game::Components::ResourceType::Minerals;
        Resource.Amount = 1500;
        Resource.MaxAmount = 1500;

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = {160.0f, 64.0f};
        Target.AnyEntityID = Mineral;
        Target.ResourceEntityID = Mineral;

        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM,
            {Scv, Marine},
            Target,
            Target.WorldPosition);

        auto* ScvCmd = EM.GetComponent<Game::Components::CommandComponent>(Scv);
        auto* ScvHarvest = EM.GetComponent<Game::Components::HarvestComponent>(Scv);
        Require(ScvCmd && ScvCmd->CurrentCommand == Game::Components::CommandType::Harvest,
            "smart-clicking minerals issues harvest to workers");
        Require(ScvCmd->TargetEntityID == Mineral,
            "smart-click harvest keeps the clicked mineral target");
        Require(ScvHarvest && ScvHarvest->TargetResourceID == Mineral,
            "smart-click harvest creates worker harvest state");
        Require(!EM.GetComponent<Game::Components::HarvestComponent>(Marine),
            "smart-clicking minerals does not create harvest state for non-workers");
    }

    void TestSmartClickEmptyGroundMovesSelection()
    {
        Core::ECS::EntityManager EM;
        const int Scv = CreateUnit(EM, 7, {64.0f, 64.0f}, 0);
        const Core::Math::Vec2 TargetPos{192.0f, 128.0f};

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.WorldPosition = TargetPos;

        Game::Client::GameLoop::ExecuteSmartClickCommand(
            EM,
            {Scv},
            Target,
            TargetPos);

        auto* Cmd = EM.GetComponent<Game::Components::CommandComponent>(Scv);
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(Scv);
        Require(Cmd && Cmd->CurrentCommand == Game::Components::CommandType::Move,
            "smart-clicking empty ground issues move");
        Require(Cmd->TargetPosition.X == TargetPos.X && Cmd->TargetPosition.Y == TargetPos.Y,
            "smart-click empty-ground move keeps clicked world position");
        Require(Path && !Path->Path.empty(),
            "smart-click empty-ground move creates a movement path");
    }
}

int main()
{
    TestMixedSelectionSmartClicksAirTarget();
    TestWorkerSmartClicksMinerals();
    TestSmartClickEmptyGroundMovesSelection();

    std::printf("SmartClickCommandTest passed\n");
    return 0;
}
