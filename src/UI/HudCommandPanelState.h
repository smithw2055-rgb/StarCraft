#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "Game/Systems/UpgradeSystem.h"

namespace Game::Systems
{
enum class TerranResearchId : uint8_t
{
    None = static_cast<uint8_t>(ResearchId::None),
    InfantryWeapons = static_cast<uint8_t>(ResearchId::Terran_InfantryWeapons),
    InfantryArmor = static_cast<uint8_t>(ResearchId::Terran_InfantryArmor),
    VehicleWeapons = static_cast<uint8_t>(ResearchId::Terran_VehicleWeapons),
    VehicleArmor = static_cast<uint8_t>(ResearchId::Terran_VehicleArmor),
    ShipWeapons = static_cast<uint8_t>(ResearchId::Terran_ShipWeapons),
    ShipArmor = static_cast<uint8_t>(ResearchId::Terran_ShipArmor),
    StimPacks = static_cast<uint8_t>(ResearchId::Terran_StimPacks),
    U238Shells = static_cast<uint8_t>(ResearchId::Terran_U238Shells),
    SiegeMode = static_cast<uint8_t>(ResearchId::Terran_SiegeMode),
    SpiderMines = static_cast<uint8_t>(ResearchId::Terran_SpiderMines),
    CloakingField = static_cast<uint8_t>(ResearchId::Terran_CloakingField),
    IonThrusters = static_cast<uint8_t>(ResearchId::Terran_IonThrusters),
    Lockdown = static_cast<uint8_t>(ResearchId::Terran_Lockdown),
    MoebiusReactor = static_cast<uint8_t>(ResearchId::Terran_MoebiusReactor),
    YamatoGun = static_cast<uint8_t>(ResearchId::Terran_YamatoGun),
    ColossusReactor = static_cast<uint8_t>(ResearchId::Terran_ColossusReactor)
};

inline ResearchId ToResearchId(TerranResearchId ID)
{
    return static_cast<ResearchId>(static_cast<uint8_t>(ID));
}
}

namespace Game::Client::UI
{

enum class ActionKind : uint8_t
{
    None,
    Command,
    Build,
    Train,
    Research,
    OpenSubmenu,
    Back
};

enum class HudCommandId : uint8_t
{
    None,
    Move,
    AttackMove,
    Stop,
    Hold,
    Patrol,
    Follow,
    Harvest
};

enum class PendingTargetMode : uint8_t
{
    None,
    BuildPlacement,
    BuildRefinery,
    Harvest,
    Move,
    AttackMove,
    Patrol,
    Follow
};

enum class HudVisualId : uint8_t
{
    None,
    BasicBuild,
    AdvancedBuild,
    Back
};

enum class HudPanelCategory : uint8_t
{
    None,
    Terran,
    Zerg,
    Protoss
};

enum PanelPage : int
{
    PageNone = 0,
    PageTerranScvMain,
    PageTerranScvBuild1,
    PageTerranScvBuild2,
    PageTerranUnitCommands,
    PageTerranMarine,
    PageTerranFirebat,
    PageTerranMedic,
    PageTerranGhost,
    PageTerranVulture,
    PageTerranGoliath,
    PageTerranSiegeTank,
    PageTerranWraith,
    PageTerranDropship,
    PageTerranScienceVessel,
    PageTerranBattlecruiser,
    PageTerranValkyrie,
    PageTerranPassiveBuilding,
    PageTerranComsatStation,
    PageTerranNuclearSilo,
    PageTerranBunker,
    PageTerranCommandCenter,
    PageTerranBarracks,
    PageTerranFactory,
    PageTerranStarport,
    PageTerranEngineeringBay,
    PageTerranArmory,
    PageTerranAcademy,
    PageTerranScienceFacility,
    PageTerranMachineShop,
    PageTerranControlTower,
    PageTerranCovertOps,
    PageTerranPhysicsLab,
    PageZergDroneMain,
    PageZergDroneBuild,
    PageZergHatchery,
    PageProtossProbeMain,
    PageProtossProbeBuild,
    PageProtossNexus,
    PageProtossGateway,
    PageProtossRoboticsFacility,
    PageProtossStargate
};

struct CmdButtonDef
{
    std::string Label;
    ActionKind Kind = ActionKind::None;
    HudCommandId CommandId = HudCommandId::None;
    Game::Systems::TerranResearchId ResearchId;
    int TargetUnitDefID = -1;
    int IconIndex = -1;
    int NextMenuPage = 0;
    bool Enabled = false;
    HudVisualId VisualId = HudVisualId::None;

    CmdButtonDef();
};

struct HudCommandPanelState
{
    int PanelPage = 0;
    HudPanelCategory PanelCategory = HudPanelCategory::None;
    std::array<CmdButtonDef, 9> ButtonDefs;
    PendingTargetMode PendingTargetModeValue = PendingTargetMode::None;
    int PendingBuildDefID = -1;
    HudCommandId PendingCommandId = HudCommandId::None;
};

} // namespace Game::Client::UI
