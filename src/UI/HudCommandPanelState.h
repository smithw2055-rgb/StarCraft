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
    ColossusReactor = static_cast<uint8_t>(ResearchId::Terran_ColossusReactor),
    CharonBoosters = static_cast<uint8_t>(ResearchId::Terran_CharonBoosters),

    ZergMeleeAttacks = static_cast<uint8_t>(ResearchId::Zerg_MeleeAttacks),
    ZergMissileAttacks = static_cast<uint8_t>(ResearchId::Zerg_MissileAttacks),
    ZergCarapace = static_cast<uint8_t>(ResearchId::Zerg_Carapace),
    ZergFlyerAttacks = static_cast<uint8_t>(ResearchId::Zerg_FlyerAttacks),
    ZergFlyerCarapace = static_cast<uint8_t>(ResearchId::Zerg_FlyerCarapace),
    ZergAdrenalGlands = static_cast<uint8_t>(ResearchId::Zerg_AdrenalGlands),
    ZergMetabolicBoost = static_cast<uint8_t>(ResearchId::Zerg_MetabolicBoost),
    ZergAnabolicSynthesis = static_cast<uint8_t>(ResearchId::Zerg_AnabolicSynthesis),
    ZergChitinousPlating = static_cast<uint8_t>(ResearchId::Zerg_ChitinousPlating),
    ZergMuscularAugments = static_cast<uint8_t>(ResearchId::Zerg_MuscularAugments),
    ZergGroovedSpines = static_cast<uint8_t>(ResearchId::Zerg_GroovedSpines),
    ZergVentralSacs = static_cast<uint8_t>(ResearchId::Zerg_VentralSacs),
    ZergAntennae = static_cast<uint8_t>(ResearchId::Zerg_Antennae),
    ZergPneumatizedCarapace = static_cast<uint8_t>(ResearchId::Zerg_PneumatizedCarapace),
    ZergBurrow = static_cast<uint8_t>(ResearchId::Zerg_Burrow),
    ZergConsume = static_cast<uint8_t>(ResearchId::Zerg_Consume),
    ZergEnsnare = static_cast<uint8_t>(ResearchId::Zerg_Ensnare),
    ZergPlague = static_cast<uint8_t>(ResearchId::Zerg_Plague),
    ZergSpawnBroodling = static_cast<uint8_t>(ResearchId::Zerg_SpawnBroodling),
    ZergQueenEnergy = static_cast<uint8_t>(ResearchId::Zerg_QueenEnergy),
    ZergDefilerEnergy = static_cast<uint8_t>(ResearchId::Zerg_DefilerEnergy),
    ZergLurkerMorph = static_cast<uint8_t>(ResearchId::Zerg_LurkerMorph),

    ProtossGroundWeapons = static_cast<uint8_t>(ResearchId::Protoss_GroundWeapons),
    ProtossGroundArmor = static_cast<uint8_t>(ResearchId::Protoss_GroundArmor),
    ProtossPlasmaShields = static_cast<uint8_t>(ResearchId::Protoss_PlasmaShields),
    ProtossAirWeapons = static_cast<uint8_t>(ResearchId::Protoss_AirWeapons),
    ProtossAirArmor = static_cast<uint8_t>(ResearchId::Protoss_AirArmor),
    ProtossSingularityCharge = static_cast<uint8_t>(ResearchId::Protoss_SingularityCharge),
    ProtossGraviticDrive = static_cast<uint8_t>(ResearchId::Protoss_GraviticDrive),
    ProtossScarabDamage = static_cast<uint8_t>(ResearchId::Protoss_ScarabDamage),
    ProtossZealotSpeed = static_cast<uint8_t>(ResearchId::Protoss_ZealotSpeed),
    ProtossSensorArray = static_cast<uint8_t>(ResearchId::Protoss_SensorArray),
    ProtossApialSensors = static_cast<uint8_t>(ResearchId::Protoss_ApialSensors),
    ProtossKhaydarinAmulet = static_cast<uint8_t>(ResearchId::Protoss_KhaydarinAmulet),
    ProtossArgusJewel = static_cast<uint8_t>(ResearchId::Protoss_ArgusJewel),
    ProtossArgusTalisman = static_cast<uint8_t>(ResearchId::Protoss_ArgusTalisman),
    ProtossKhaydarinCore = static_cast<uint8_t>(ResearchId::Protoss_KhaydarinCore),
    ProtossPsionicStorm = static_cast<uint8_t>(ResearchId::Protoss_PsionicStorm),
    ProtossHallucination = static_cast<uint8_t>(ResearchId::Protoss_Hallucination),
    ProtossRecall = static_cast<uint8_t>(ResearchId::Protoss_Recall),
    ProtossStasisField = static_cast<uint8_t>(ResearchId::Protoss_StasisField),
    ProtossMindControl = static_cast<uint8_t>(ResearchId::Protoss_MindControl),
    ProtossMaelstrom = static_cast<uint8_t>(ResearchId::Protoss_Maelstrom),
    ProtossReaverCapacity = static_cast<uint8_t>(ResearchId::Protoss_ReaverCapacity),
    ProtossCarrierCapacity = static_cast<uint8_t>(ResearchId::Protoss_CarrierCapacity)
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
    Upgrade,
    MorphBuilding,
    UseTech,
    Cancel,
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
    Harvest,
    LiftOff,
    Land,
    Rally,
    Scan,
    Load,
    UnloadAll,
    Siege,
    Unsiege,
    RechargeShields,
    Burrow,
    PsionicStorm,
    YamatoGun,
    Lockdown,
    EMPShockwave,
    Hallucination,
    StasisField,
    Recall,
    DarkSwarm,
    Plague,
    Ensnare,
    SpawnBroodling,
    Parasite,
    Consume
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
    Follow,
    Land,
    Rally,
    Scan,
    Load,
    RechargeShields,
    PsionicStorm,
    YamatoGun,
    Lockdown,
    EMPShockwave,
    Hallucination,
    StasisField,
    Recall,
    DarkSwarm,
    Plague,
    Ensnare,
    SpawnBroodling,
    Parasite,
    Consume
};

enum class HudVisualId : uint8_t
{
    None,
    BasicBuild,
    AdvancedBuild,
    Back,
    Cancel
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
    PageZergDroneBuild2,
    PageZergUnitCommands,
    PageZergHatchery,
    PageZergHatcheryCommands,
    PageZergCreepColony,
    PageZergSpawningPool,
    PageZergEvolutionChamber,
    PageZergHydraliskDen,
    PageZergSpire,
    PageZergQueensNest,
    PageZergGreaterSpire,
    PageZergUltraliskCavern,
    PageZergDefilerMound,
    PageProtossProbeMain,
    PageProtossProbeBuild,
    PageProtossProbeBuild2,
    PageProtossProbeBuild3,
    PageProtossUnitCommands,
    PageProtossNexus,
    PageProtossGateway,
    PageProtossForge,
    PageProtossCyberneticsCore,
    PageProtossRoboticsFacility,
    PageProtossStargate,
    PageProtossCitadelOfAdun,
    PageProtossTemplarArchives,
    PageProtossRoboticsSupportBay,
    PageProtossFleetBeacon,
    PageProtossArbiterTribunal,
    PageProtossObservatory,
    PageProtossShieldBattery
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
    int MineralCost = 0;
    int GasCost = 0;
    int SupplyCost = 0;
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
