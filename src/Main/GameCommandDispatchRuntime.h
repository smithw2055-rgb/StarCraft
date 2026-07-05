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
#include "Game/Systems/ZergSystem.h"
#include "Main/GameLoop/CursorTypes.h"
#include "Main/GameLoop/InputHandler.h"
#include "UI/Hud.h"

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
    case UI::ActionKind::Upgrade:
        Game::Systems::UpgradeSystem::StartResearch(
            EntityManager, EntityIDs[0], Game::Systems::ToResearchId(CommandDef.ResearchId));
        break;
    case UI::ActionKind::Cancel:
    {
        if (!Game::Systems::ProductionSystem::CancelTrain(EntityManager, EntityIDs[0]))
            Game::Systems::UpgradeSystem::CancelResearch(EntityManager, EntityIDs[0]);
        break;
    }
    case UI::ActionKind::UseTech:
    {
        Game::Components::Order Order;
        Order.Type = Game::Components::OrderType::UseTech;
        Order.AbilityID = static_cast<int>(
            Game::Systems::ToResearchId(CommandDef.ResearchId));
        Game::Systems::CommandSystem::IssueOrder(EntityManager, EntityIDs, Order, false);
        break;
    }
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

inline bool ExecuteHudCommandIndex(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    UI::Hud& Hud,
    int CommandIndex)
{
    auto& Defs = Hud.GetButtonDefs();
    if (CommandIndex < 0 || CommandIndex >= static_cast<int>(Defs.size()))
        return false;

    const auto& CommandDef = Defs[static_cast<size_t>(CommandIndex)];
    if (CommandDef.Label.empty())
        return false;
    if (!CommandDef.Enabled)
        return false;

    ExecuteHudCommandSlot(EntityManager, EntityIDs, CommandDef);
    Hud.OnButtonInvoked(CommandIndex);
    return true;
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

inline bool ExecutePendingTargetCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    UI::PendingTargetMode Mode,
    int BuildDefID,
    const ClickTargetContext& ClickTarget,
    const Core::Math::Vec2& Target)
{
    if (EntityIDs.empty())
        return false;

    switch (Mode)
    {
    case UI::PendingTargetMode::BuildPlacement:
    {
        const auto BuildDef = Game::Races::GetUnitDefByID(BuildDefID);
        if (BuildDef.RaceType == Game::Components::Race::Zerg)
        {
            int PlayerID = 0;
            if (auto* Player = EntityManager.GetComponent<Game::Components::PlayerComponent>(EntityIDs[0]))
                PlayerID = Player->PlayerID;
            if (!Game::Systems::BuildingSystem::IsPlacementValid(EntityManager, BuildDefID, Target, PlayerID))
                return false;
            return Game::Systems::ZergSystem::StartMorphDroneIntoBuilding(
                EntityManager, EntityIDs[0], BuildDefID, Target);
        }
        return Game::Systems::BuildingSystem::StartBuild(
            EntityManager, EntityIDs[0], BuildDefID, Game::Systems::BuildTarget::FromPosition(Target));
    }
    case UI::PendingTargetMode::BuildRefinery:
        if (ClickTarget.ResourceEntityID >= 0)
        {
            const auto BuildDef = Game::Races::GetUnitDefByID(BuildDefID);
            const bool Built = Game::Systems::BuildingSystem::StartBuild(
                EntityManager, EntityIDs[0], BuildDefID, Game::Systems::BuildTarget::FromEntity(ClickTarget.ResourceEntityID));
            if (Built && BuildDef.RaceType == Game::Components::Race::Zerg)
                EntityManager.DestroyEntity(EntityIDs[0]);
            return Built;
        }
        return false;
    case UI::PendingTargetMode::Harvest:
        if (ClickTarget.ResourceEntityID >= 0)
            return Game::Systems::HarvestSystem::IssueHarvest(EntityManager, EntityIDs, ClickTarget.ResourceEntityID);
        return false;
    case UI::PendingTargetMode::Move:
        Game::Systems::CommandSystem::IssueMove(EntityManager, EntityIDs, Target);
        return true;
    case UI::PendingTargetMode::AttackMove:
        if (ClickTarget.EnemyEntityID >= 0)
        {
            auto Attackers = FilterAttackersForTarget(EntityManager, EntityIDs, ClickTarget.EnemyEntityID);
            if (Attackers.empty())
                return false;
            Game::Systems::CombatSystem::IssueAttack(EntityManager, Attackers, ClickTarget.EnemyEntityID);
            return true;
        }
        Game::Systems::CombatSystem::IssueAttackMove(EntityManager, EntityIDs, Target);
        return true;
    case UI::PendingTargetMode::Patrol:
        IssuePatrolCommand(EntityManager, EntityIDs, Target);
        return true;
    case UI::PendingTargetMode::Follow:
        if (ClickTarget.AnyEntityID < 0)
            return false;
        IssueFollowCommand(EntityManager, EntityIDs, ClickTarget.AnyEntityID);
        return true;
    default:
        return false;
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
        auto Attackers = FilterAttackersForTarget(EntityManager, EntityIDs, ClickTarget.EnemyEntityID);
        if (!Attackers.empty())
            Game::Systems::CombatSystem::IssueAttack(EntityManager, Attackers, ClickTarget.EnemyEntityID);

        if (Attackers.size() < EntityIDs.size())
        {
            std::vector<int> Movers;
            Movers.reserve(EntityIDs.size() - Attackers.size());
            for (int EntityID : EntityIDs)
            {
                if (!CanEntityAttackTarget(EntityManager, EntityID, ClickTarget.EnemyEntityID))
                    Movers.push_back(EntityID);
            }
            if (!Movers.empty())
                Game::Systems::CommandSystem::IssueMove(EntityManager, Movers, Target);
        }
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
