#pragma once

#include <algorithm>
#include <cctype>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/TransportSystem.h"
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

inline bool CanUseRallyPoint(Core::ECS::EntityManager& EntityManager, int EntityID)
{
    auto* Building = EntityManager.GetComponent<Game::Components::BuildingComponent>(EntityID);
    if (!Building || Building->UnderConstruction || Building->IsLifted)
        return false;

    int DefID = Building->BuildingDefID;
    if (DefID < 0)
    {
        if (auto* Unit = EntityManager.GetComponent<Game::Components::UnitComponent>(EntityID))
            DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
    }

    const auto Def = Game::Races::GetUnitDefByID(DefID);
    if (!Def.IsBuilding || Def.IsAddon)
        return false;

    return Def.Name == "CommandCenter" ||
           Def.Name == "Barracks" ||
           Def.Name == "Factory" ||
           Def.Name == "Starport" ||
           Def.Name == "Hatchery" ||
           Def.Name == "Lair" ||
           Def.Name == "Hive" ||
           Def.Name == "Nexus" ||
           Def.Name == "Gateway" ||
           Def.Name == "Robotics Facility" ||
           Def.Name == "Stargate";
}

inline std::vector<int> ApplyRallyPointDefaultOrder(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    const Core::Math::Vec2& Target)
{
    std::vector<int> Remaining;
    Remaining.reserve(EntityIDs.size());
    for (int EntityID : EntityIDs)
    {
        if (CanUseRallyPoint(EntityManager, EntityID))
            Game::Systems::CommandSystem::SetRallyPoint(EntityManager, EntityID, Target);
        else
            Remaining.push_back(EntityID);
    }
    return Remaining;
}

inline bool TryIssueArchonWarpCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    bool IsDarkArchon)
{
    const int RequiredDefID = Game::Races::GetUnitDefID(IsDarkArchon ? "Dark Templar" : "High Templar");
    const auto RequiredDef = Game::Races::GetUnitDefByID(RequiredDefID);
    if (RequiredDef.Name == "Unknown")
        return false;

    std::vector<int> Eligible;
    for (int EntityID : EntityIDs)
    {
        const auto* Unit = EntityManager.GetComponent<Game::Components::UnitComponent>(EntityID);
        if (Unit && Unit->ChkUnitID == RequiredDef.ChkUnitID && Unit->HitPoints > 0)
        {
            Eligible.push_back(EntityID);
        }
    }

    bool MorphedAny = false;
    for (size_t i = 0; i + 1 < Eligible.size(); i += 2)
    {
        if (Game::Systems::ProtossSystem::MorphArchon(EntityManager, Eligible[i], Eligible[i + 1], IsDarkArchon))
        {
            MorphedAny = true;
        }
    }
    return MorphedAny;
}

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
    {
        const auto* Unit =
            EntityManager.GetComponent<Game::Components::UnitComponent>(
                EntityIDs[0]);
        const bool IsLarva =
            Unit && Unit->ChkUnitID == 35 &&
            EntityManager.GetComponent<Game::Components::LarvaComponent>(
                EntityIDs[0]);
        if (IsLarva)
        {
            Game::Systems::ZergSystem::StartMorphLarvaIntoUnit(
                EntityManager, EntityIDs[0], CommandDef.TargetUnitDefID);
        }
        else
        {
            Game::Systems::ProductionSystem::StartTrain(
                EntityManager, EntityIDs[0], CommandDef.TargetUnitDefID);
        }
        break;
    }
    case UI::ActionKind::MorphBuilding:
        Game::Systems::ZergSystem::StartMorphBuilding(
            EntityManager, EntityIDs[0], CommandDef.TargetUnitDefID);
        break;
    case UI::ActionKind::Research:
    case UI::ActionKind::Upgrade:
        Game::Systems::UpgradeSystem::StartResearch(
            EntityManager, EntityIDs[0], Game::Systems::ToResearchId(CommandDef.ResearchId));
        break;
    case UI::ActionKind::Cancel:
    {
        if (!Game::Systems::BuildingSystem::CancelConstruction(EntityManager, EntityIDs[0]))
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
        else if (CommandDef.CommandId == UI::HudCommandId::LiftOff)
            for (int EntityID : EntityIDs)
                Game::Systems::BuildingSystem::LiftOffBuilding(EntityManager, EntityID);
        else if (CommandDef.CommandId == UI::HudCommandId::UnloadAll)
            for (int EntityID : EntityIDs)
                Game::Systems::TransportSystem::UnloadAll(EntityManager, EntityID);
        else if (CommandDef.CommandId == UI::HudCommandId::Siege ||
                 CommandDef.CommandId == UI::HudCommandId::Unsiege)
            for (int EntityID : EntityIDs)
                Game::Systems::CommandSystem::SetSiegeMode(
                    EntityManager, EntityID,
                    CommandDef.CommandId == UI::HudCommandId::Siege);
        else if (CommandDef.CommandId == UI::HudCommandId::LaySpiderMine)
            for (int EntityID : EntityIDs)
                Game::Systems::NukeAndSpiderMineSystem::
                    PlaceSpiderMineForActiveSession(EntityID);
        else if (CommandDef.CommandId == UI::HudCommandId::Burrow)
            for (int EntityID : EntityIDs)
                Game::Systems::ZergSystem::BurrowUnit(EntityManager, EntityID);
        else if (CommandDef.CommandId == UI::HudCommandId::MorphLurker)
            for (int EntityID : EntityIDs)
                Game::Systems::ZergSystem::StartMorphHydraliskIntoLurker(EntityManager, EntityID);
        else if (CommandDef.CommandId == UI::HudCommandId::MorphGuardian)
            for (int EntityID : EntityIDs)
                Game::Systems::ZergSystem::StartMorphMutaliskIntoGuardian(EntityManager, EntityID);
        else if (CommandDef.CommandId == UI::HudCommandId::MorphDevourer)
            for (int EntityID : EntityIDs)
                Game::Systems::ZergSystem::StartMorphMutaliskIntoDevourer(EntityManager, EntityID);
        else if (CommandDef.CommandId == UI::HudCommandId::ArchonWarp ||
                 CommandDef.CommandId == UI::HudCommandId::DarkArchonWarp)
            TryIssueArchonWarpCommand(
                EntityManager, EntityIDs,
                CommandDef.CommandId == UI::HudCommandId::DarkArchonWarp);
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

inline char ResolveButtonHotkey(const UI::CmdButtonDef& Button)
{
    if (Button.Hotkey != '\0')
        return static_cast<char>(std::toupper(static_cast<unsigned char>(Button.Hotkey)));

    if (Button.Kind == UI::ActionKind::Command)
    {
        switch (Button.CommandId)
        {
        case UI::HudCommandId::AttackMove: return 'A';
        case UI::HudCommandId::Move: return 'M';
        case UI::HudCommandId::Stop: return 'S';
        case UI::HudCommandId::Hold: return 'H';
        case UI::HudCommandId::Patrol: return 'P';
        case UI::HudCommandId::Follow: return 'F';
        case UI::HudCommandId::Harvest: return 'G';
        case UI::HudCommandId::LiftOff: return 'L';
        case UI::HudCommandId::Land: return 'L';
        case UI::HudCommandId::Siege: return 'S';
        case UI::HudCommandId::Unsiege: return 'O';
        case UI::HudCommandId::Burrow: return 'B';
        case UI::HudCommandId::Load: return 'L';
        case UI::HudCommandId::UnloadAll: return 'U';
        case UI::HudCommandId::Rally: return 'R';
        case UI::HudCommandId::Scan: return 'S';
        case UI::HudCommandId::RechargeShields: return 'R';
        case UI::HudCommandId::PsionicStorm: return 'T';
        case UI::HudCommandId::YamatoGun: return 'Y';
        case UI::HudCommandId::Lockdown: return 'L';
        case UI::HudCommandId::EMPShockwave: return 'E';
        case UI::HudCommandId::Hallucination: return 'H';
        case UI::HudCommandId::StasisField: return 'T';
        case UI::HudCommandId::Recall: return 'R';
        case UI::HudCommandId::DarkSwarm: return 'W';
        case UI::HudCommandId::Plague: return 'G';
        case UI::HudCommandId::Ensnare: return 'E';
        case UI::HudCommandId::SpawnBroodling: return 'B';
        case UI::HudCommandId::Parasite: return 'P';
        case UI::HudCommandId::Consume: return 'C';
        case UI::HudCommandId::DefensiveMatrix: return 'D';
        case UI::HudCommandId::Irradiate: return 'I';
        case UI::HudCommandId::Restoration: return 'R';
        case UI::HudCommandId::OpticalFlare: return 'F';
        case UI::HudCommandId::Feedback: return 'F';
        case UI::HudCommandId::Maelstrom: return 'E';
        case UI::HudCommandId::MindControl: return 'C';
        case UI::HudCommandId::Repair: return 'R';
        case UI::HudCommandId::Heal: return 'A';
        case UI::HudCommandId::ArchonWarp: return 'C';
        case UI::HudCommandId::DarkArchonWarp: return 'C';
        case UI::HudCommandId::MorphLurker: return 'L';
        case UI::HudCommandId::MorphGuardian: return 'G';
        case UI::HudCommandId::MorphDevourer: return 'D';
        case UI::HudCommandId::Infest: return 'I';
        default: break;
        }
    }
    else if (Button.Kind == UI::ActionKind::OpenSubmenu)
    {
        if (Button.VisualId == UI::HudVisualId::BasicBuild ||
            Button.Label.find("Basic") != std::string::npos ||
            Button.Label == "Build")
            return 'B';
        if (Button.VisualId == UI::HudVisualId::AdvancedBuild ||
            Button.Label.find("Advanced") != std::string::npos ||
            Button.Label == "Advanced")
            return 'V';
        if (Button.Label == "More")
            return 'M';
    }
    else if (Button.Kind == UI::ActionKind::Cancel ||
             Button.Kind == UI::ActionKind::Back)
    {
        return '\0';
    }
    else if (Button.Kind == UI::ActionKind::UseTech)
    {
        if (Button.ResearchId == Game::Systems::TerranResearchId::StimPacks ||
            Button.Label.find("Stim") != std::string::npos)
            return 'T';
        if (Button.ResearchId == Game::Systems::TerranResearchId::CloakingField ||
            Button.ResearchId == Game::Systems::TerranResearchId::PersonalCloaking ||
            Button.Label.find("Cloak") != std::string::npos)
            return 'C';
    }
    else if (Button.Kind == UI::ActionKind::Train ||
             Button.Kind == UI::ActionKind::Build ||
             Button.Kind == UI::ActionKind::MorphBuilding)
    {
        if (Button.TargetUnitDefID >= 0)
        {
            const auto Def = Game::Races::GetUnitDefByID(Button.TargetUnitDefID);
            const std::string& Name = Def.Name;
            // Terran Units
            if (Name == "SCV") return 'S';
            if (Name == "Marine") return 'M';
            if (Name == "Firebat") return 'F';
            if (Name == "Medic") return 'C';
            if (Name == "Ghost") return 'G';
            if (Name == "Vulture") return 'V';
            if (Name == "SiegeTank") return 'T';
            if (Name == "Goliath") return 'G';
            if (Name == "Wraith") return 'W';
            if (Name == "Dropship") return 'D';
            if (Name == "ScienceVessel") return 'V';
            if (Name == "Battlecruiser") return 'B';
            if (Name == "Valkyrie") return 'Y';
            // Terran Buildings
            if (Name == "CommandCenter") return 'C';
            if (Name == "SupplyDepot") return 'S';
            if (Name == "Refinery") return 'R';
            if (Name == "Barracks") return 'B';
            if (Name == "EngineeringBay") return 'E';
            if (Name == "MissileTurret") return 'T';
            if (Name == "Academy") return 'A';
            if (Name == "Bunker") return 'U';
            if (Name == "Factory") return 'F';
            if (Name == "Starport") return 'S';
            if (Name == "ScienceFacility") return 'I';
            if (Name == "Armory") return 'A';
            if (Name == "ComsatStation") return 'C';
            if (Name == "NuclearSilo") return 'N';
            if (Name == "MachineShop") return 'C';
            if (Name == "ControlTower") return 'C';
            if (Name == "CovertOps") return 'C';
            if (Name == "PhysicsLab") return 'P';
            // Zerg Units
            if (Name == "Drone") return 'D';
            if (Name == "Zergling") return 'Z';
            if (Name == "Overlord") return 'O';
            if (Name == "Hydralisk") return 'H';
            if (Name == "Mutalisk") return 'M';
            if (Name == "Scourge") return 'S';
            if (Name == "Queen") return 'Q';
            if (Name == "Ultralisk") return 'U';
            if (Name == "Defiler") return 'D';
            if (Name == "Lurker") return 'L';
            if (Name == "Guardian") return 'G';
            if (Name == "Devourer") return 'D';
            // Zerg Buildings
            if (Name == "Hatchery") return 'H';
            if (Name == "CreepColony") return 'C';
            if (Name == "SpawningPool") return 'S';
            if (Name == "EvolutionChamber") return 'V';
            if (Name == "HydraliskDen") return 'D';
            if (Name == "Spire") return 'S';
            if (Name == "QueensNest") return 'Q';
            if (Name == "UltraliskCavern") return 'U';
            if (Name == "DefilerMound") return 'D';
            if (Name == "SunkenColony") return 'U';
            if (Name == "SporeColony") return 'S';
            if (Name == "Lair") return 'L';
            if (Name == "Hive") return 'H';
            if (Name == "GreaterSpire") return 'G';
            if (Name == "Extractor") return 'E';
            // Protoss Units
            if (Name == "Probe") return 'P';
            if (Name == "Zealot") return 'Z';
            if (Name == "Dragoon") return 'D';
            if (Name == "HighTemplar") return 'T';
            if (Name == "DarkTemplar") return 'K';
            if (Name == "Shuttle") return 'S';
            if (Name == "Reaver") return 'V';
            if (Name == "Observer") return 'O';
            if (Name == "Scout") return 'S';
            if (Name == "Carrier") return 'C';
            if (Name == "Arbiter") return 'A';
            if (Name == "Corsair") return 'O';
            // Protoss Buildings
            if (Name == "Nexus") return 'N';
            if (Name == "Pylon") return 'P';
            if (Name == "Assimilator") return 'A';
            if (Name == "Gateway") return 'G';
            if (Name == "Forge") return 'F';
            if (Name == "PhotonCannon") return 'C';
            if (Name == "CyberneticsCore") return 'Y';
            if (Name == "ShieldBattery") return 'B';
            if (Name == "RoboticsFacility") return 'R';
            if (Name == "Stargate") return 'S';
            if (Name == "CitadelOfAdun") return 'C';
            if (Name == "RoboticsSupportBay") return 'B';
            if (Name == "FleetBeacon") return 'F';
            if (Name == "TemplarArchives") return 'T';
            if (Name == "Observatory") return 'O';
            if (Name == "ArbiterTribunal") return 'A';
        }
    }
    else if (Button.Kind == UI::ActionKind::Research ||
             Button.Kind == UI::ActionKind::Upgrade)
    {
        if (Button.ResearchId == Game::Systems::TerranResearchId::StimPacks) return 'T';
        if (Button.ResearchId == Game::Systems::TerranResearchId::U238Shells) return 'U';
        if (Button.ResearchId == Game::Systems::TerranResearchId::SiegeMode) return 'S';
        if (Button.ResearchId == Game::Systems::TerranResearchId::SpiderMines) return 'I';
        if (Button.ResearchId == Game::Systems::TerranResearchId::CloakingField) return 'C';
        if (Button.ResearchId == Game::Systems::TerranResearchId::IonThrusters) return 'I';
        if (Button.ResearchId == Game::Systems::TerranResearchId::YamatoGun) return 'Y';
        if (Button.ResearchId == Game::Systems::TerranResearchId::Lockdown) return 'L';
        if (Button.ResearchId == Game::Systems::TerranResearchId::CharonBoosters) return 'B';
        if (Button.ResearchId == Game::Systems::TerranResearchId::PersonalCloaking) return 'C';
    }

    if (!Button.Label.empty())
    {
        for (char Ch : Button.Label)
        {
            if (std::isalpha(static_cast<unsigned char>(Ch)))
                return static_cast<char>(std::toupper(static_cast<unsigned char>(Ch)));
        }
    }

    return '\0';
}

inline bool TryDispatchCommandHotkey(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    UI::Hud& Hud,
    int KeyCode)
{
    if (EntityIDs.empty())
        return false;

    char PressedChar = '\0';
    if (KeyCode >= 'A' && KeyCode <= 'Z')
        PressedChar = static_cast<char>(KeyCode);
    else if (KeyCode >= 'a' && KeyCode <= 'z')
        PressedChar = static_cast<char>(std::toupper(KeyCode));

    if (PressedChar == '\0')
        return false;

    const auto& Defs = Hud.GetButtonDefs();
    for (int Slot = 0; Slot < static_cast<int>(Defs.size()); ++Slot)
    {
        const auto& Button = Defs[static_cast<size_t>(Slot)];
        if (Button.Label.empty() || !Button.Enabled)
            continue;

        const char Hotkey = ResolveButtonHotkey(Button);
        if (Hotkey != '\0' && Hotkey == PressedChar)
        {
            return ExecuteHudCommandIndex(EntityManager, EntityIDs, Hud, Slot);
        }
    }

    return false;
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

inline bool TryIssueScvRepairCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    int TargetEntityID);

inline bool TryIssueMedicHealCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    int TargetEntityID);

inline bool TryIssueProtossRechargeShieldsCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    int TargetEntityID);

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
        std::vector<int> Assistants;
        if (EntityIDs.size() > 1 && BuildDef.RaceType == Game::Components::Race::Terran && !BuildDef.IsAddon)
        {
            for (size_t I = 1; I < EntityIDs.size(); ++I)
            {
                auto* Unit = EntityManager.GetComponent<Game::Components::UnitComponent>(EntityIDs[I]);
                if (Unit && Unit->HitPoints > 0 && Unit->ChkUnitID == 7)
                {
                    Assistants.push_back(EntityIDs[I]);
                }
            }
        }
        return Game::Systems::BuildingSystem::StartBuild(
            EntityManager, EntityIDs[0], BuildDefID, Game::Systems::BuildTarget::FromPosition(Target), Assistants);
    }
    case UI::PendingTargetMode::BuildRefinery:
        if (ClickTarget.ResourceEntityID >= 0)
        {
            // Refinery-type structures follow the same pending approach as
            // every other building. In particular, an Extractor must not
            // consume its Drone remotely at click time; BuildingSystem does
            // so only after the Drone actually reaches the selected geyser.
            return Game::Systems::BuildingSystem::StartBuild(
                EntityManager, EntityIDs[0], BuildDefID, Game::Systems::BuildTarget::FromEntity(ClickTarget.ResourceEntityID));
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
    case UI::PendingTargetMode::Land:
    {
        bool LandedAny = false;
        for (int EntityID : EntityIDs)
            LandedAny = Game::Systems::BuildingSystem::LandBuilding(
                EntityManager, EntityID, Target) || LandedAny;
        return LandedAny;
    }
    case UI::PendingTargetMode::Rally:
    {
        bool SetAny = false;
        for (int EntityID : EntityIDs)
        {
            if (!CanUseRallyPoint(EntityManager, EntityID))
                continue;
            Game::Systems::CommandSystem::SetRallyPoint(EntityManager, EntityID, Target);
            SetAny = true;
        }
        return SetAny;
    }
    case UI::PendingTargetMode::Scan:
    {
        for (int EntityID : EntityIDs)
        {
            if (Game::Systems::SpellSystem::CastSpell(
                    EntityManager, EntityID,
                    Game::Systems::SpellType::ScannerSweep, -1, Target))
                return true;
        }
        return false;
    }
    case UI::PendingTargetMode::Load:
        if (ClickTarget.AnyEntityID < 0)
            return false;
        for (int EntityID : EntityIDs)
            if (Game::Systems::TransportSystem::IssueLoad(
                    EntityManager, EntityID, ClickTarget.AnyEntityID))
                return true;
        return false;
    case UI::PendingTargetMode::RechargeShields:
        if (ClickTarget.AnyEntityID < 0)
            return false;
        for (int EntityID : EntityIDs)
            if (Game::Systems::ProtossSystem::RechargeShieldsTarget(
                    EntityManager, EntityID, ClickTarget.AnyEntityID))
                return true;
        return false;
    case UI::PendingTargetMode::PsionicStorm:
    case UI::PendingTargetMode::YamatoGun:
    case UI::PendingTargetMode::Lockdown:
    case UI::PendingTargetMode::EMPShockwave:
    case UI::PendingTargetMode::Hallucination:
    case UI::PendingTargetMode::StasisField:
    case UI::PendingTargetMode::Recall:
    case UI::PendingTargetMode::DarkSwarm:
    case UI::PendingTargetMode::Plague:
    case UI::PendingTargetMode::Ensnare:
    case UI::PendingTargetMode::SpawnBroodling:
    case UI::PendingTargetMode::Parasite:
    case UI::PendingTargetMode::Consume:
    case UI::PendingTargetMode::DefensiveMatrix:
    case UI::PendingTargetMode::Irradiate:
    case UI::PendingTargetMode::Restoration:
    case UI::PendingTargetMode::OpticalFlare:
    case UI::PendingTargetMode::Feedback:
    case UI::PendingTargetMode::Maelstrom:
    case UI::PendingTargetMode::MindControl:
    case UI::PendingTargetMode::Infest:
    {
        Game::Systems::SpellType Spell = Game::Systems::SpellType::None;
        switch (Mode)
        {
            case UI::PendingTargetMode::PsionicStorm: Spell = Game::Systems::SpellType::PsionicStorm; break;
            case UI::PendingTargetMode::YamatoGun: Spell = Game::Systems::SpellType::YamatoGun; break;
            case UI::PendingTargetMode::Lockdown: Spell = Game::Systems::SpellType::Lockdown; break;
            case UI::PendingTargetMode::EMPShockwave: Spell = Game::Systems::SpellType::EMPShockwave; break;
            case UI::PendingTargetMode::Hallucination: Spell = Game::Systems::SpellType::Hallucination; break;
            case UI::PendingTargetMode::StasisField: Spell = Game::Systems::SpellType::StasisField; break;
            case UI::PendingTargetMode::Recall: Spell = Game::Systems::SpellType::Recall; break;
            case UI::PendingTargetMode::DarkSwarm: Spell = Game::Systems::SpellType::DarkSwarm; break;
            case UI::PendingTargetMode::Plague: Spell = Game::Systems::SpellType::Plague; break;
            case UI::PendingTargetMode::Ensnare: Spell = Game::Systems::SpellType::Ensnare; break;
            case UI::PendingTargetMode::SpawnBroodling: Spell = Game::Systems::SpellType::SpawnBroodling; break;
            case UI::PendingTargetMode::Parasite: Spell = Game::Systems::SpellType::Parasite; break;
            case UI::PendingTargetMode::Consume: Spell = Game::Systems::SpellType::Consume; break;
            case UI::PendingTargetMode::DefensiveMatrix: Spell = Game::Systems::SpellType::DefensiveMatrix; break;
            case UI::PendingTargetMode::Irradiate: Spell = Game::Systems::SpellType::Irradiate; break;
            case UI::PendingTargetMode::Restoration: Spell = Game::Systems::SpellType::Restoration; break;
            case UI::PendingTargetMode::OpticalFlare: Spell = Game::Systems::SpellType::OpticalFlare; break;
            case UI::PendingTargetMode::Feedback: Spell = Game::Systems::SpellType::Feedback; break;
            case UI::PendingTargetMode::Maelstrom: Spell = Game::Systems::SpellType::Maelstrom; break;
            case UI::PendingTargetMode::MindControl: Spell = Game::Systems::SpellType::MindControl; break;
            case UI::PendingTargetMode::Infest: Spell = Game::Systems::SpellType::Infestation; break;
            default: break;
        }
        for (int EntityID : EntityIDs)
            if (Game::Systems::SpellSystem::CastSpell(
                    EntityManager, EntityID, Spell,
                    ClickTarget.AnyEntityID, Target))
                return true;
        return false;
    }
    case UI::PendingTargetMode::Repair:
        if (ClickTarget.AnyEntityID >= 0)
        {
            return TryIssueScvRepairCommand(EntityManager, EntityIDs, ClickTarget.AnyEntityID);
        }
        return false;
    case UI::PendingTargetMode::Heal:
        if (ClickTarget.AnyEntityID >= 0)
        {
            return TryIssueMedicHealCommand(EntityManager, EntityIDs, ClickTarget.AnyEntityID);
        }
        return false;
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

inline bool TryIssueScvRepairCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    int TargetEntityID)
{
    if (!Game::Systems::CommandSystem::IsRepairableTarget(EntityManager, TargetEntityID))
    {
        return false;
    }
    auto* TargetPlayer =
        EntityManager.GetComponent<Game::Components::PlayerComponent>(TargetEntityID);
    auto* TargetTransform =
        EntityManager.GetComponent<Game::Components::TransformComponent>(TargetEntityID);
    if (!TargetPlayer || !TargetTransform)
    {
        return false;
    }

    bool IssuedAny = false;
    for (int EntityID : EntityIDs)
    {
        const auto* Unit =
            EntityManager.GetComponent<Game::Components::UnitComponent>(EntityID);
        const auto* Player =
            EntityManager.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (!Unit || !Player || Unit->ChkUnitID != 7 || Unit->HitPoints <= 0 ||
            Player->PlayerID != TargetPlayer->PlayerID)
        {
            continue;
        }

        Game::Systems::CommandSystem::InterruptHarvest(EntityManager, EntityID);
        Game::Components::Order RepairOrder;
        RepairOrder.Type = Game::Components::OrderType::Repair;
        RepairOrder.TargetEntityID = TargetEntityID;
        RepairOrder.TargetPosition = TargetTransform->Position;
        RepairOrder.PathingIgnoreEntityID = TargetEntityID;
        Game::Systems::CommandSystem::IssueOrder(
            EntityManager, EntityID, RepairOrder, false);
        IssuedAny = true;
    }
    return IssuedAny;
}

inline bool TryIssueMedicHealCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    int TargetEntityID)
{
    auto* TargetUnit =
        EntityManager.GetComponent<Game::Components::UnitComponent>(TargetEntityID);
    auto* TargetPlayer =
        EntityManager.GetComponent<Game::Components::PlayerComponent>(TargetEntityID);
    auto* TargetTransform =
        EntityManager.GetComponent<Game::Components::TransformComponent>(TargetEntityID);
    if (!TargetUnit || !TargetPlayer || !TargetTransform || TargetUnit->HitPoints <= 0 ||
        TargetUnit->HitPoints >= TargetUnit->MaxHitPoints ||
        !Game::Races::IsOrganicChkUnitID(TargetUnit->ChkUnitID))
    {
        return false;
    }

    bool IssuedAny = false;
    for (int EntityID : EntityIDs)
    {
        const auto* Unit =
            EntityManager.GetComponent<Game::Components::UnitComponent>(EntityID);
        const auto* Player =
            EntityManager.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (!Unit || !Player || Unit->ChkUnitID != 34 || Unit->HitPoints <= 0 ||
            Player->PlayerID != TargetPlayer->PlayerID)
        {
            continue;
        }

        Game::Components::Order HealOrder;
        HealOrder.Type = Game::Components::OrderType::Heal;
        HealOrder.TargetEntityID = TargetEntityID;
        HealOrder.TargetPosition = TargetTransform->Position;
        Game::Systems::CommandSystem::IssueOrder(
            EntityManager, EntityID, HealOrder, false);
        IssuedAny = true;
    }
    return IssuedAny;
}

inline bool TryIssueProtossRechargeShieldsCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    int TargetEntityID)
{
    auto* BatteryUnit =
        EntityManager.GetComponent<Game::Components::UnitComponent>(TargetEntityID);
    auto* BatteryBuilding =
        EntityManager.GetComponent<Game::Components::BuildingComponent>(TargetEntityID);
    auto* BatteryPlayer =
        EntityManager.GetComponent<Game::Components::PlayerComponent>(TargetEntityID);
    auto* BatteryTrans =
        EntityManager.GetComponent<Game::Components::TransformComponent>(TargetEntityID);
    if (!BatteryUnit || !BatteryBuilding || !BatteryPlayer || !BatteryTrans ||
        BatteryUnit->ChkUnitID != 172 || BatteryBuilding->UnderConstruction ||
        BatteryUnit->EnergyPoints <= 0)
    {
        return false;
    }

    bool HandledAny = false;
    std::vector<int> OutOfRangeUnits;
    for (int EntityID : EntityIDs)
    {
        auto* Unit =
            EntityManager.GetComponent<Game::Components::UnitComponent>(EntityID);
        auto* Player =
            EntityManager.GetComponent<Game::Components::PlayerComponent>(EntityID);
        auto* Trans =
            EntityManager.GetComponent<Game::Components::TransformComponent>(EntityID);
        if (!Unit || !Player || !Trans || Unit->HitPoints <= 0 ||
            Player->PlayerID != BatteryPlayer->PlayerID ||
            Unit->MaxShieldPoints <= 0 || Unit->ShieldPoints >= Unit->MaxShieldPoints)
        {
            continue;
        }

        if (BatteryTrans->Position.DistanceTo(Trans->Position) <= 128.0f)
        {
            if (Game::Systems::ProtossSystem::RechargeShieldsTarget(
                    EntityManager, TargetEntityID, EntityID))
            {
                HandledAny = true;
            }
        }
        else
        {
            OutOfRangeUnits.push_back(EntityID);
        }
    }

    if (!OutOfRangeUnits.empty())
    {
        Game::Systems::CommandSystem::IssueMove(
            EntityManager, OutOfRangeUnits, BatteryTrans->Position, TargetEntityID);
        HandledAny = true;
    }

    return HandledAny;
}

inline void ExecuteSmartClickCommand(
    Core::ECS::EntityManager& EntityManager,
    const std::vector<int>& EntityIDs,
    const ClickTargetContext& ClickTarget,
    const Core::Math::Vec2& Target)
{
    if (EntityIDs.empty())
        return;

    const std::vector<int> ActingEntityIDs =
        ApplyRallyPointDefaultOrder(EntityManager, EntityIDs, Target);
    if (ActingEntityIDs.empty())
        return;

    if (ClickTarget.AnyEntityID >= 0 &&
        Game::Systems::TransportSystem::IsTransport(
            EntityManager, ClickTarget.AnyEntityID) &&
        Game::Systems::TransportSystem::IssueBoard(
            EntityManager, ActingEntityIDs, ClickTarget.AnyEntityID))
    {
        return;
    }

    if (ClickTarget.AnyEntityID >= 0 &&
        Game::Systems::BuildingSystem::AssistConstruction(
            EntityManager, ActingEntityIDs, ClickTarget.AnyEntityID))
    {
        return;
    }

    if (ClickTarget.AnyEntityID >= 0 &&
        TryIssueScvRepairCommand(
            EntityManager, ActingEntityIDs, ClickTarget.AnyEntityID))
    {
        return;
    }

    if (ClickTarget.AnyEntityID >= 0 &&
        TryIssueMedicHealCommand(
            EntityManager, ActingEntityIDs, ClickTarget.AnyEntityID))
    {
        return;
    }

    if (ClickTarget.AnyEntityID >= 0 &&
        TryIssueProtossRechargeShieldsCommand(
            EntityManager, ActingEntityIDs, ClickTarget.AnyEntityID))
    {
        return;
    }

    if (ClickTarget.EnemyEntityID >= 0)
    {
        auto Attackers = FilterAttackersForTarget(EntityManager, ActingEntityIDs, ClickTarget.EnemyEntityID);
        if (!Attackers.empty())
            Game::Systems::CombatSystem::IssueAttack(EntityManager, Attackers, ClickTarget.EnemyEntityID);

        if (Attackers.size() < ActingEntityIDs.size())
        {
            std::vector<int> Movers;
            Movers.reserve(ActingEntityIDs.size() - Attackers.size());
            for (int EntityID : ActingEntityIDs)
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
        auto Harvesters = FilterHarvesters(EntityManager, ActingEntityIDs);
        if (!Harvesters.empty())
            Game::Systems::HarvestSystem::IssueHarvest(EntityManager, Harvesters, ClickTarget.ResourceEntityID);
    }
    else
    {
        Game::Systems::CommandSystem::IssueMove(EntityManager, ActingEntityIDs, Target);
    }
}

} // namespace Game::Client::GameLoop
