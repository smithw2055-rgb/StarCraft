#pragma once

#include <string>

#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/UpgradeSystem.h"
#include "UI/HudState.h"

namespace Game::Client::UI
{

inline std::string DisplayLabelForDef(int DefID)
{
    const auto Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.Name == "CommandCenter") return "Command Center";
    if (Def.Name == "SupplyDepot") return "Supply Depot";
    if (Def.Name == "ScienceFacility") return "Science Facility";
    if (Def.Name == "ScienceVessel") return "Science Vessel";
    if (Def.Name == "SiegeTank") return "Siege Tank";
    if (Def.Name == "EngineeringBay") return "Engineering Bay";
    if (Def.Name == "ControlTower") return "Control Tower";
    if (Def.Name == "MachineShop") return "Machine Shop";
    if (Def.Name == "ComsatStation") return "Comsat Station";
    if (Def.Name == "NuclearSilo") return "Nuclear Silo";
    if (Def.Name == "CovertOps") return "Covert Ops";
    if (Def.Name == "PhysicsLab") return "Physics Lab";
    if (Def.Name == "MissileTurret") return "Missile Turret";
    return Def.Name;
}

inline std::string Ellipsize(const std::string& Text, size_t MaxChars)
{
    if (Text.size() <= MaxChars || MaxChars <= 3)
        return Text.size() <= MaxChars ? Text : Text.substr(0, MaxChars);
    return Text.substr(0, MaxChars - 3) + "...";
}

inline int IconForDef(int DefID)
{
    const auto Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.DefaultIconIndex >= 0)
        return Def.DefaultIconIndex;
    if (Def.ChkUnitID > 0)
        return Def.ChkUnitID;
    return DefID;
}

inline int CommandIconForCommand(HudCommandId CommandId)
{
    switch (CommandId)
    {
        case HudCommandId::Move: return 231;
        case HudCommandId::AttackMove: return 290;
        case HudCommandId::Stop: return 232;
        case HudCommandId::Hold: return 249;
        case HudCommandId::Patrol: return 254;
        case HudCommandId::Follow: return 309;
        case HudCommandId::Harvest: return 346;
        default: return -1;
    }
}

inline int CommandIconForResearch(Game::Systems::TerranResearchId ResearchId)
{
    using Game::Systems::TerranResearchId;
    switch (ResearchId)
    {
        case TerranResearchId::InfantryWeapons:
        case TerranResearchId::InfantryArmor:
        case TerranResearchId::StimPacks:
        case TerranResearchId::U238Shells:
            return IconForDef(Game::Races::GetUnitDefID("Marine"));
        case TerranResearchId::VehicleWeapons:
        case TerranResearchId::VehicleArmor:
        case TerranResearchId::SpiderMines:
        case TerranResearchId::IonThrusters:
            return IconForDef(Game::Races::GetUnitDefID("Vulture"));
        case TerranResearchId::SiegeMode:
            return IconForDef(Game::Races::GetUnitDefID("SiegeTank"));
        case TerranResearchId::ShipWeapons:
        case TerranResearchId::ShipArmor:
        case TerranResearchId::CloakingField:
            return IconForDef(Game::Races::GetUnitDefID("Wraith"));
        case TerranResearchId::Lockdown:
        case TerranResearchId::MoebiusReactor:
            return IconForDef(Game::Races::GetUnitDefID("Ghost"));
        case TerranResearchId::YamatoGun:
        case TerranResearchId::ColossusReactor:
            return IconForDef(Game::Races::GetUnitDefID("Battlecruiser"));
        default:
            return -1;
    }
}

inline bool IsLeveledResearch(Game::Systems::TerranResearchId ResearchId)
{
    using Game::Systems::TerranResearchId;
    switch (ResearchId)
    {
        case TerranResearchId::InfantryWeapons:
        case TerranResearchId::InfantryArmor:
        case TerranResearchId::VehicleWeapons:
        case TerranResearchId::VehicleArmor:
        case TerranResearchId::ShipWeapons:
        case TerranResearchId::ShipArmor:
            return true;
        default:
            return false;
    }
}

inline std::string ResearchLabel(int PlayerID,
                                 Game::Systems::TerranResearchId ResearchId,
                                 const std::string& BaseLabel)
{
    if (!IsLeveledResearch(ResearchId))
        return BaseLabel;

    const int Level = Game::Systems::UpgradeSystem::GetResearchLevel(
        PlayerID, Game::Systems::ToResearchId(ResearchId));
    if (Level >= 3)
        return BaseLabel + " Max";
    return BaseLabel + " +" + std::to_string(Level + 1);
}

inline std::string DisplayLabelForChkUnitId(int ChkUnitId)
{
    switch (ChkUnitId)
    {
        case 41: return "Drone";
        case 44: return "Overlord";
        case 42: return "Zergling";
        case 43: return "Hydralisk";
        case 46: return "Mutalisk";
        case 47: return "Scourge";
        case 49: return "Queen";
        case 48: return "Ultralisk";
        case 86: return "Hatchery";
        case 87: return "Lair";
        case 88: return "Hive";
        case 64: return "Probe";
        case 129: return "Gateway";
        case 126: return "Nexus";
        case 134: return "Robotics Facility";
        case 135: return "Stargate";
        case 11:
        case 12:
        case 95:
        case 99:
        case 100:
        case 101:
        case 102:
        case 176:
        case 177:
        case 178:
            return "Mineral Field";
        case 103:
        case 188:
            return "Vespene Geyser";
        default:
            break;
    }
    return "Unit " + std::to_string(ChkUnitId);
}

inline std::string RaceLabel(Game::Components::Race RaceType)
{
    switch (RaceType)
    {
        case Game::Components::Race::Terran: return "Terran";
        case Game::Components::Race::Zerg: return "Zerg";
        case Game::Components::Race::Protoss: return "Protoss";
    }
    return "Unknown";
}

inline std::string CommandLabel(Game::Components::CommandType CurrentCommand)
{
    using Game::Components::CommandType;
    switch (CurrentCommand)
    {
        case CommandType::Move: return "Moving";
        case CommandType::AttackMove: return "Attack Move";
        case CommandType::Attack: return "Attacking";
        case CommandType::Patrol: return "Patrolling";
        case CommandType::HoldPosition: return "Holding";
        case CommandType::Stop: return "Idle";
        case CommandType::Harvest: return "Harvesting";
        case CommandType::Build: return "Building";
        case CommandType::Train: return "Training";
        case CommandType::Upgrade: return "Researching";
        case CommandType::UseAbility: return "Using Ability";
        case CommandType::Follow: return "Following";
    }
    return "Idle";
}

inline std::string HarvestStateLabel(Game::Components::HarvestState State)
{
    using Game::Components::HarvestState;
    switch (State)
    {
        case HarvestState::Idle: return "Idle";
        case HarvestState::MovingToResource: return "To resource";
        case HarvestState::Mining: return "Mining";
        case HarvestState::MovingToDepot: return "Returning";
        case HarvestState::Depositing: return "Depositing";
    }
    return "Idle";
}

inline std::string RoleLabelForScv(int PlayerID)
{
    if (PlayerID <= 0)
        return "Private";
    return "Private P" + std::to_string(PlayerID);
}

} // namespace Game::Client::UI
