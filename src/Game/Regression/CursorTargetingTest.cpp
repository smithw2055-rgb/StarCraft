#include "Main/GameLoop/CursorTargeting.h"
#include "Main/GameLoop/CursorTypes.h"

#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
    void Require(bool Condition, const char* Message)
    {
        if (!Condition)
        {
            std::cerr << "Requirement failed: " << Message << "\n";
            std::exit(1);
        }
    }

    bool HitsResource(Core::Math::Vec2 Center,
                      Core::Math::Vec2 Click,
                      Game::Components::ResourceType Type,
                      int ChkUnitID)
    {
        const float Score =
            Game::Client::GameLoop::ResourceClickScore(Center, Click, Type, ChkUnitID);
        return Score < std::numeric_limits<float>::max();
    }

    Core::ECS::EntityID CreateCombatEntity(Core::ECS::EntityManager& EM,
                                           int GroundDamage,
                                           int AirDamage,
                                           bool CanTargetGround,
                                           bool CanTargetAir,
                                           bool IsAir)
    {
        const Core::ECS::EntityID Entity = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Entity).Position = {100.0f, 100.0f};
        auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(Entity);
        Combat.GroundDamage = GroundDamage;
        Combat.AirDamage = AirDamage;
        Combat.CanTargetGround = CanTargetGround;
        Combat.CanTargetAir = CanTargetAir;
        Combat.MyElevation = IsAir ? Game::Components::Elevation::Air
                                   : Game::Components::Elevation::Ground;
        if (IsAir)
            EM.AddComponent<Game::Components::PathfindingComponent>(Entity).IsAir = true;
        return Entity;
    }
}

int main()
{
    using Game::Components::ResourceType;

    float HotspotX = 0.0f;
    float HotspotY = 0.0f;
    Game::Client::GameLoop::CursorHotspot(
        Game::Client::GameLoop::GameCursorKind::Arrow, HotspotX, HotspotY);
    Require(HotspotX == 64.0f && HotspotY == 64.0f,
        "classic 128x128 cursor canvas keeps the click at its center hotspot");
    Game::Client::GameLoop::CursorHotspot(
        Game::Client::GameLoop::GameCursorKind::Drag, HotspotX, HotspotY);
    Require(HotspotX == 64.0f && HotspotY == 64.0f,
        "drag cursor hotspot aligns the visible cursor with the selection rectangle corner");

    Require(HitsResource({100.0f, 100.0f}, {140.0f, 100.0f},
                         ResourceType::Minerals, 176),
            "mineral edge clicks resolve to the mineral resource");
    Require(HitsResource({100.0f, 100.0f}, {100.0f, 128.0f},
                         ResourceType::Minerals, 176),
            "mineral lower edge clicks resolve to the mineral resource");
    Require(HitsResource({100.0f, 100.0f}, {140.0f, 120.0f},
                         ResourceType::Minerals, 176),
            "visible mineral corner clicks resolve to the mineral resource");
    Require(!HitsResource({100.0f, 100.0f}, {156.0f, 100.0f},
                          ResourceType::Minerals, 176),
            "clicks past the visible mineral footprint do not resolve to minerals");

    Require(HitsResource({200.0f, 200.0f}, {260.0f, 200.0f},
                         ResourceType::VespeneGas, 188),
            "geyser edge clicks resolve to the gas resource");
    Require(!HitsResource({200.0f, 200.0f}, {280.0f, 200.0f},
                          ResourceType::VespeneGas, 188),
            "clicks past the visible geyser footprint do not resolve to gas");

    Core::ECS::EntityManager EM;
    const auto GroundOnly =
        CreateCombatEntity(EM, 30, 0, true, false, false);
    const auto AntiAir =
        CreateCombatEntity(EM, 6, 6, true, true, false);
    const auto GroundTarget =
        CreateCombatEntity(EM, 0, 0, false, false, false);
    const auto AirTarget =
        CreateCombatEntity(EM, 0, 0, false, false, true);
    const auto ResourceTarget = EM.CreateEntity();
    EM.AddComponent<Game::Components::ResourceComponent>(ResourceTarget).Type =
        Game::Components::ResourceType::Minerals;

    const std::vector<Core::ECS::EntityID> Selection = {GroundOnly, AntiAir};
    Require(Game::Client::GameLoop::CanEntityAttackTarget(EM, GroundOnly, GroundTarget),
            "ground-only units can attack ground targets");
    Require(!Game::Client::GameLoop::CanEntityAttackTarget(EM, GroundOnly, AirTarget),
            "ground-only units cannot attack air targets");
    Require(Game::Client::GameLoop::CanEntityAttackTarget(EM, AntiAir, AirTarget),
            "anti-air capable units can attack air targets");
    Require(!Game::Client::GameLoop::CanEntityAttackTarget(EM, AntiAir, ResourceTarget),
            "resources are not attack targets for smart-click");

    auto AirAttackers =
        Game::Client::GameLoop::FilterAttackersForTarget(EM, Selection, AirTarget);
    Require(AirAttackers.size() == 1 && AirAttackers.front() == AntiAir,
            "smart-click filters mixed selections to only units that can attack the target");
    Require(Game::Client::GameLoop::HasAttackCapableSelection(EM, Selection, AirTarget),
            "cursor red targeting appears when at least one selected unit can attack");
    Require(!Game::Client::GameLoop::HasAttackCapableSelection(EM, {GroundOnly}, AirTarget),
            "cursor red targeting stays off when selected units cannot attack the target");

    const auto LargeBuilding = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(LargeBuilding).Position =
        {300.0f, 300.0f};
    auto& LargeBuildingUnit =
        EM.AddComponent<Game::Components::UnitComponent>(LargeBuilding);
    LargeBuildingUnit.HitPoints = 1000;
    LargeBuildingUnit.MaxHitPoints = 1000;
    LargeBuildingUnit.SizeType = Game::Components::UnitSize::Large;
    EM.AddComponent<Game::Components::PlayerComponent>(LargeBuilding).PlayerID = 1;
    auto& LargeBuildingPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(LargeBuilding);
    LargeBuildingPath.HasCollisionBounds = true;
    LargeBuildingPath.CollisionLeft = 64;
    LargeBuildingPath.CollisionUp = 48;
    LargeBuildingPath.CollisionRight = 63;
    LargeBuildingPath.CollisionDown = 47;

    Require(Game::Client::GameLoop::EntityClickScore(
                EM, LargeBuilding, {360.0f, 300.0f}) < std::numeric_limits<float>::max(),
            "right-click targeting uses collision bounds for large buildings");
    Require(Game::Client::GameLoop::EntityClickScore(
                EM, LargeBuilding, {370.0f, 300.0f}) == std::numeric_limits<float>::max(),
            "right-click targeting rejects clicks outside large building bounds");

    const auto SmallUnit = CreateCombatEntity(EM, 0, 0, false, false, false);
    Require(Game::Client::GameLoop::EntityClickScore(
                EM, SmallUnit, {130.0f, 100.0f}) == std::numeric_limits<float>::max(),
            "small units keep a tighter right-click target footprint than buildings");

    std::cout << "CursorTargetingTest passed\n";
    return 0;
}
