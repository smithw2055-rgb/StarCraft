#pragma once

#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Main/GameLoop/InputHandler.h"
#include "UI/HudState.h"

namespace Game::Client::GameLoop
{

enum class KeyboardCommandShortcut
{
    None,
    Stop,
    Hold,
    Patrol,
    Follow
};

inline void ExecuteHudCommandSlot(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    const UI::CmdButtonDef& CommandDef)
{
    if (EntityIDs.empty() || !CommandDef.Enabled)
        return;

    switch (CommandDef.Kind)
    {
    case UI::ActionKind::Build:
    {
        auto BuildDef = Game::Races::GetUnitDefByID(CommandDef.TargetUnitDefID);
        if (BuildDef.IsAddon)
            Game::Systems::BuildingSystem::StartBuild(
                EntityManager, EntityIDs[0], CommandDef.TargetUnitDefID, Game::Systems::BuildTarget{});
        break;
    }
    case UI::ActionKind::Train:
        Game::Systems::ProductionSystem::StartTrain(
            EntityManager, EntityIDs[0], CommandDef.TargetUnitDefID);
        break;
    case UI::ActionKind::Research:
        Game::Systems::UpgradeSystem::StartResearch(
            EntityManager, EntityIDs[0], Game::Systems::ToResearchId(CommandDef.ResearchId));
        break;
    case UI::ActionKind::Command:
        if (CommandDef.CommandId == UI::HudCommandId::Stop)
            SetAllCommand(EntityManager, EntityIDs, Game::Components::CommandType::Stop);
        else if (CommandDef.CommandId == UI::HudCommandId::Hold)
            SetAllCommand(EntityManager, EntityIDs, Game::Components::CommandType::HoldPosition);
        break;
    default:
        break;
    }
}

inline void ExecuteKeyboardCommandShortcut(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    KeyboardCommandShortcut Shortcut)
{
    if (EntityIDs.empty() || Shortcut == KeyboardCommandShortcut::None)
        return;

    switch (Shortcut)
    {
    case KeyboardCommandShortcut::Stop:
        SetAllCommand(EntityManager, EntityIDs, Game::Components::CommandType::Stop);
        break;
    case KeyboardCommandShortcut::Hold:
        SetAllCommand(EntityManager, EntityIDs, Game::Components::CommandType::HoldPosition);
        break;
    case KeyboardCommandShortcut::Patrol:
        for (int EntityID : EntityIDs)
            if (auto* Command = EntityManager.GetComponent<Game::Components::CommandComponent>(EntityID))
                Command->CurrentCommand = Game::Components::CommandType::Patrol;
        break;
    case KeyboardCommandShortcut::Follow:
        for (int EntityID : EntityIDs)
            if (auto* Command = EntityManager.GetComponent<Game::Components::CommandComponent>(EntityID))
                Command->CurrentCommand = Game::Components::CommandType::Follow;
        break;
    default:
        break;
    }
}

inline void ExecutePendingTargetCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    UI::PendingTargetMode Mode,
    int BuildDefID,
    const ClickTargetContext& ClickTarget,
    const Core::Math::Vec2& Target)
{
    if (EntityIDs.empty())
        return;

    switch (Mode)
    {
    case UI::PendingTargetMode::BuildPlacement:
        Game::Systems::BuildingSystem::StartBuild(
            EntityManager, EntityIDs[0], BuildDefID, Game::Systems::BuildTarget::FromPosition(Target));
        break;
    case UI::PendingTargetMode::BuildRefinery:
        if (ClickTarget.ResourceEntityID >= 0)
            Game::Systems::BuildingSystem::StartBuild(
                EntityManager, EntityIDs[0], BuildDefID, Game::Systems::BuildTarget::FromEntity(ClickTarget.ResourceEntityID));
        break;
    case UI::PendingTargetMode::Harvest:
        if (ClickTarget.ResourceEntityID >= 0)
            Game::Systems::HarvestSystem::IssueHarvest(EntityManager, EntityIDs, ClickTarget.ResourceEntityID);
        break;
    case UI::PendingTargetMode::Move:
        Game::Systems::CommandSystem::IssueMove(EntityManager, EntityIDs, Target);
        break;
    case UI::PendingTargetMode::AttackMove:
        Game::Systems::CombatSystem::IssueAttackMove(EntityManager, EntityIDs, Target);
        break;
    case UI::PendingTargetMode::Patrol:
        IssuePatrolCommand(EntityManager, EntityIDs, Target);
        break;
    case UI::PendingTargetMode::Follow:
        IssueFollowCommand(EntityManager, EntityIDs, ClickTarget.AnyEntityID);
        break;
    default:
        break;
    }
}

inline void ExecuteAttackMoveCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    const Core::Math::Vec2& Target)
{
    if (EntityIDs.empty())
        return;

    Game::Systems::CombatSystem::IssueAttackMove(EntityManager, EntityIDs, Target);
}

inline void ExecuteSmartClickCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    const ClickTargetContext& ClickTarget,
    const Core::Math::Vec2& Target)
{
    if (EntityIDs.empty())
        return;

    if (ClickTarget.EnemyEntityID >= 0)
    {
        Game::Systems::CombatSystem::IssueAttack(EntityManager, EntityIDs, ClickTarget.EnemyEntityID);
    }
    else if (ClickTarget.ResourceEntityID >= 0)
    {
        auto Harvesters = FilterHarvesters(EntityManager, EntityIDs);
        if (!Harvesters.empty())
            Game::Systems::HarvestSystem::IssueHarvest(EntityManager, Harvesters, ClickTarget.ResourceEntityID);
    }
    else
    {
        Game::Systems::CommandSystem::IssueMove(EntityManager, EntityIDs, Target);
    }
}

} // namespace Game::Client::GameLoop
