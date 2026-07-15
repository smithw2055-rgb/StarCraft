#include "UI/Hud.h"

#include "UI/HudButtonStyle.h"
#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"
#include "Game/Components.h"
#include "Core/Platform/Platform.h"
#include "Render/Draw2D.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "UI/HudText.h"

namespace Game::Client::UI
{

namespace
{
    using Game::Systems::TerranResearchId;

    constexpr int ZergDrone = 41;
    constexpr int ZergHatchery = 131;
    constexpr int ZergLair = 132;
    constexpr int ZergHive = 133;
    constexpr int ZergHydraliskDen = 135;
    constexpr int ZergDefilerMound = 136;
    constexpr int ZergGreaterSpire = 137;
    constexpr int ZergQueensNest = 138;
    constexpr int ZergEvolutionChamber = 139;
    constexpr int ZergUltraliskCavern = 140;
    constexpr int ZergSpire = 141;
    constexpr int ZergSpawningPool = 142;
    constexpr int ZergCreepColony = 143;

    constexpr int ProtoProbe = 64;
    constexpr int ProtoNexus = 154;
    constexpr int ProtoObservatory = 159;
    constexpr int ProtoRoboticsFacility = 155;
    constexpr int ProtoGateway = 160;
    constexpr int ProtoCitadelOfAdun = 163;
    constexpr int ProtoCyberneticsCore = 164;
    constexpr int ProtoTemplarArchives = 165;
    constexpr int ProtoForge = 166;
    constexpr int ProtoStargate = 167;
    constexpr int ProtoFleetBeacon = 169;
    constexpr int ProtoArbiterTribunal = 170;
    constexpr int ProtoRoboticsSupportBay = 171;
    constexpr int ProtoShieldBattery = 172;

    constexpr int IconBuild = 0;
    constexpr int IconBack = 1;
    constexpr int IconMore = 2;
    constexpr int IconCommand = -1;
    constexpr int IconWeapons = 4;
    constexpr int IconArmor = 5;
}

int Hud::GetDefaultPageForSelection(int SelectedChkUnitId) const
{
    switch (SelectedChkUnitId)
    {
        case ZergDrone: return PageZergDroneMain;
        case ZergHatchery:
        case ZergLair:
        case ZergHive: return PageZergHatchery;
        case ZergSpawningPool: return PageZergSpawningPool;
        case ZergEvolutionChamber: return PageZergEvolutionChamber;
        case ZergHydraliskDen: return PageZergHydraliskDen;
        case ZergSpire: return PageZergSpire;
        case ZergQueensNest: return PageZergQueensNest;
        case ZergGreaterSpire: return PageZergGreaterSpire;
        case ZergUltraliskCavern: return PageZergUltraliskCavern;
        case ZergDefilerMound: return PageZergDefilerMound;
        case ZergCreepColony: return PageZergCreepColony;
        case ProtoProbe: return PageProtossProbeMain;
        case ProtoNexus: return PageProtossNexus;
        case ProtoGateway: return PageProtossGateway;
        case ProtoForge: return PageProtossForge;
        case ProtoCyberneticsCore: return PageProtossCyberneticsCore;
        case ProtoRoboticsFacility: return PageProtossRoboticsFacility;
        case ProtoStargate: return PageProtossStargate;
        case ProtoCitadelOfAdun: return PageProtossCitadelOfAdun;
        case ProtoTemplarArchives: return PageProtossTemplarArchives;
        case ProtoRoboticsSupportBay: return PageProtossRoboticsSupportBay;
        case ProtoFleetBeacon: return PageProtossFleetBeacon;
        case ProtoArbiterTribunal: return PageProtossArbiterTribunal;
        case ProtoObservatory: return PageProtossObservatory;
        case ProtoShieldBattery: return PageProtossShieldBattery;
        default: return PageNone;
    }
}

int Hud::GetDefaultTerranPageForDef(int SelectedTerranDefId) const
{
    const int SCV = Game::Races::GetUnitDefID("SCV");
    const int Marine = Game::Races::GetUnitDefID("Marine");
    const int Firebat = Game::Races::GetUnitDefID("Firebat");
    const int Medic = Game::Races::GetUnitDefID("Medic");
    const int Ghost = Game::Races::GetUnitDefID("Ghost");
    const int Vulture = Game::Races::GetUnitDefID("Vulture");
    const int Goliath = Game::Races::GetUnitDefID("Goliath");
    const int SiegeTank = Game::Races::GetUnitDefID("SiegeTank");
    const int Wraith = Game::Races::GetUnitDefID("Wraith");
    const int Dropship = Game::Races::GetUnitDefID("Dropship");
    const int ScienceVessel = Game::Races::GetUnitDefID("ScienceVessel");
    const int Battlecruiser = Game::Races::GetUnitDefID("Battlecruiser");
    const int Valkyrie = Game::Races::GetUnitDefID("Valkyrie");
    const int CommandCenter = Game::Races::GetUnitDefID("CommandCenter");
    const int ComsatStation = Game::Races::GetUnitDefID("ComsatStation");
    const int NuclearSilo = Game::Races::GetUnitDefID("NuclearSilo");
    const int Bunker = Game::Races::GetUnitDefID("Bunker");
    const int Barracks = Game::Races::GetUnitDefID("Barracks");
    const int Factory = Game::Races::GetUnitDefID("Factory");
    const int Starport = Game::Races::GetUnitDefID("Starport");
    const int EngineeringBay = Game::Races::GetUnitDefID("EngineeringBay");
    const int Armory = Game::Races::GetUnitDefID("Armory");
    const int Academy = Game::Races::GetUnitDefID("Academy");
    const int ScienceFacility = Game::Races::GetUnitDefID("ScienceFacility");
    const int MachineShop = Game::Races::GetUnitDefID("MachineShop");
    const int ControlTower = Game::Races::GetUnitDefID("ControlTower");
    const int CovertOps = Game::Races::GetUnitDefID("CovertOps");
    const int PhysicsLab = Game::Races::GetUnitDefID("PhysicsLab");

    if (SelectedTerranDefId == SCV)
        return PageTerranScvMain;
    if (SelectedTerranDefId == Marine)
        return PageTerranMarine;
    if (SelectedTerranDefId == Firebat)
        return PageTerranFirebat;
    if (SelectedTerranDefId == Medic)
        return PageTerranMedic;
    if (SelectedTerranDefId == Ghost)
        return PageTerranGhost;
    if (SelectedTerranDefId == Vulture)
        return PageTerranVulture;
    if (SelectedTerranDefId == Goliath)
        return PageTerranGoliath;
    if (SelectedTerranDefId == SiegeTank)
        return PageTerranSiegeTank;
    if (SelectedTerranDefId == Wraith)
        return PageTerranWraith;
    if (SelectedTerranDefId == Dropship)
        return PageTerranDropship;
    if (SelectedTerranDefId == ScienceVessel)
        return PageTerranScienceVessel;
    if (SelectedTerranDefId == Battlecruiser)
        return PageTerranBattlecruiser;
    if (SelectedTerranDefId == Valkyrie)
        return PageTerranValkyrie;
    if (SelectedTerranDefId == CommandCenter)
        return PageTerranCommandCenter;
    if (SelectedTerranDefId == ComsatStation)
        return PageTerranComsatStation;
    if (SelectedTerranDefId == NuclearSilo)
        return PageTerranNuclearSilo;
    if (SelectedTerranDefId == Bunker)
        return PageTerranBunker;
    if (SelectedTerranDefId == Barracks)
        return PageTerranBarracks;
    if (SelectedTerranDefId == Factory)
        return PageTerranFactory;
    if (SelectedTerranDefId == Starport)
        return PageTerranStarport;
    if (SelectedTerranDefId == EngineeringBay)
        return PageTerranEngineeringBay;
    if (SelectedTerranDefId == Armory)
        return PageTerranArmory;
    if (SelectedTerranDefId == Academy)
        return PageTerranAcademy;
    if (SelectedTerranDefId == ScienceFacility)
        return PageTerranScienceFacility;
    if (SelectedTerranDefId == MachineShop)
        return PageTerranMachineShop;
    if (SelectedTerranDefId == ControlTower)
        return PageTerranControlTower;
    if (SelectedTerranDefId == CovertOps)
        return PageTerranCovertOps;
    if (SelectedTerranDefId == PhysicsLab)
        return PageTerranPhysicsLab;

    const auto Def = Game::Races::GetUnitDefByID(SelectedTerranDefId);
    return Def.IsBuilding ? PageTerranPassiveBuilding : PageTerranUnitCommands;
}

int Hud::ResolveSelectedTerranDefId(int SelectedEntityId,
                                    int SelectedChkUnitId,
                                    Core::ECS::EntityManager* EcsEntities) const
{
    if (EcsEntities && SelectedEntityId > 0)
    {
        if (const auto* Building =
                EcsEntities->GetComponent<Game::Components::BuildingComponent>(SelectedEntityId))
        {
            if (Building->BuildingDefID >= 0)
            {
                const auto Def = Game::Races::GetUnitDefByID(Building->BuildingDefID);
                return Def.RaceType == Game::Components::Race::Terran
                    ? Building->BuildingDefID
                    : -1;
            }
        }
    }
    const int DefID = SelectedChkUnitId == 30
        ? Game::Races::GetUnitDefID("SiegeTank")
        : Game::Races::GetUnitDefIDByChkUnitID(SelectedChkUnitId);
    const auto Def = Game::Races::GetUnitDefByID(DefID);
    return Def.RaceType == Game::Components::Race::Terran ? DefID : -1;
}

void Hud::BuildTerranPage(int SelectedEntityId,
                          int SelectedTerranDefId,
                          Core::ECS::EntityManager* EcsEntities)
{
    using Game::Systems::UpgradeSystem;

    auto TrainEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::ProductionSystem::CanTrain(*EcsEntities, SelectedEntityId, DefId);
    };

    auto BuildEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::BuildingSystem::CanBuild(
                   *EcsEntities, SelectedEntityId, DefId,
                   Game::Systems::BuildTarget{}, nullptr, false);
    };

    auto PlayerIdForSelection = [&]() -> int
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return 0;
        if (auto* Player = EcsEntities->GetComponent<Game::Components::PlayerComponent>(SelectedEntityId))
            return Player->PlayerID;
        return 0;
    };

    auto SetCommandButton = [&](int Slot, const std::string& Label, HudCommandId CommandId, bool Enabled)
    {
        SetButton(Slot, Label, ActionKind::Command, -1, CommandId,
                  TerranResearchId::None, IconCommand, Enabled);
    };

    auto SpellButton = [&](int Slot,
                           const std::string& Label,
                           HudCommandId CommandId,
                           Game::Systems::SpellType Spell,
                           TerranResearchId ResearchId,
                           bool RequiresResearch)
    {
        const auto* Unit = EcsEntities && SelectedEntityId > 0
            ? EcsEntities->GetComponent<Game::Components::UnitComponent>(SelectedEntityId)
            : nullptr;
        const auto* Cooldown = EcsEntities && SelectedEntityId > 0
            ? EcsEntities->GetComponent<Game::Components::SpellCooldown>(SelectedEntityId)
            : nullptr;
        const bool Researched = !RequiresResearch || UpgradeSystem::HasResearch(
            PlayerIdForSelection(), ToResearchId(ResearchId));
        const bool Enabled = Unit && Researched &&
            Unit->EnergyPoints >= Game::Systems::SpellSystem::GetSpellEnergyCost(Spell) &&
            (!Cooldown || Cooldown->CooldownRemaining <= 0.0f);
        SetCommandButton(Slot, Label, CommandId, Enabled);
    };

    auto DisabledAbilityButtonForDef = [&](int Slot, const std::string& Label, int IconDefId)
    {
        SetButton(Slot, Label, ActionKind::None, IconDefId, HudCommandId::None,
                  TerranResearchId::None, IconForDef(IconDefId), false);
    };

    auto TechAbilityButton = [&](int Slot,
                                 TerranResearchId ResearchId,
                                 const std::string& Label,
                                 int FallbackDefId)
    {
        const bool Enabled = UpgradeSystem::HasResearch(
            PlayerIdForSelection(), ToResearchId(ResearchId));
        SetButton(Slot,
                  Label,
                  Enabled ? ActionKind::UseTech : ActionKind::None,
                  FallbackDefId,
                  HudCommandId::None,
                  ResearchId,
                  IconForDef(FallbackDefId),
                  Enabled);
    };

    auto BuildButtonForDef = [&](int Slot, int DefId)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefId);
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Build,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), BuildEnabled(DefId), 0, HudVisualId::None,
                  Def.MineralCost, Def.GasCost, Def.SupplyCost);
    };

    auto TrainButtonForDef = [&](int Slot, int DefId)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefId);
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Train,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), TrainEnabled(DefId), 0, HudVisualId::None,
                  Def.MineralCost, Def.GasCost, Def.SupplyCost);
    };

    auto HasCancelableWork = [&]() -> bool
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return false;
        if (auto* Prod =
                EcsEntities->GetComponent<Game::Components::ProductionComponent>(SelectedEntityId))
        {
            if (!Prod->Queue.empty())
                return true;
        }
        if (auto* Building =
                EcsEntities->GetComponent<Game::Components::BuildingComponent>(SelectedEntityId))
        {
            if (Building->IsResearching)
                return true;
        }
        return false;
    };

    auto CancelButtonIfBusy = [&]()
    {
        if (HasCancelableWork())
            SetButton(8, "Cancel", ActionKind::Cancel, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, 0, HudVisualId::Cancel);
    };

    auto SelectedBuilding = [&]() -> Game::Components::BuildingComponent*
    {
        return EcsEntities && SelectedEntityId > 0
            ? EcsEntities->GetComponent<Game::Components::BuildingComponent>(SelectedEntityId)
            : nullptr;
    };

    auto CanLiftOffSelected = [&]() -> bool
    {
        const auto* Building = SelectedBuilding();
        return Building && Building->CanLiftOff && !Building->UnderConstruction &&
               !Building->IsLifted &&
               Building->FlightPhase == Game::Components::BuildingFlightPhase::Grounded;
    };

    auto CanRallySelected = [&]() -> bool
    {
        const auto* Building = SelectedBuilding();
        return Building && !Building->UnderConstruction && !Building->IsLifted;
    };

    auto HasLoadedPassengers = [&]() -> bool
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return false;
        const auto* Loaded =
            EcsEntities->GetComponent<Game::Components::LoadedUnitsComponent>(SelectedEntityId);
        return Loaded && !Loaded->PassengerIDs.empty();
    };

    auto ResearchButton = [&](int Slot,
                              TerranResearchId ResearchId,
                              const std::string& BaseLabel,
                              int IconIndex)
    {
        const int PlayerID = PlayerIdForSelection();
        const bool Enabled = EcsEntities && SelectedEntityId > 0 &&
                     UpgradeSystem::CanStartResearch(*EcsEntities, SelectedEntityId, ToResearchId(ResearchId));
        int MineralCost = 0;
        int GasCost = 0;
        const auto SystemResearchId = ToResearchId(ResearchId);
        UpgradeSystem::GetResearchCost(SystemResearchId, PlayerID, MineralCost, GasCost);
        SetButton(Slot,
                  ResearchLabel(PlayerID, ResearchId, BaseLabel),
                  UpgradeSystem::IsUpgradeType(SystemResearchId)
                      ? ActionKind::Upgrade
                      : ActionKind::Research,
                  -1,
                  HudCommandId::None,
                  ResearchId,
                  IconIndex,
                  Enabled,
                  0,
                  HudVisualId::None,
                  MineralCost,
                  GasCost);
    };

    const int CommandCenter = Game::Races::GetUnitDefID("CommandCenter");
    const int ComsatStation = Game::Races::GetUnitDefID("ComsatStation");
    const int NuclearSilo = Game::Races::GetUnitDefID("NuclearSilo");
    const int SupplyDepot = Game::Races::GetUnitDefID("SupplyDepot");
    const int Refinery = Game::Races::GetUnitDefID("Refinery");
    const int Barracks = Game::Races::GetUnitDefID("Barracks");
    const int EngineeringBay = Game::Races::GetUnitDefID("EngineeringBay");
    const int Bunker = Game::Races::GetUnitDefID("Bunker");
    const int Academy = Game::Races::GetUnitDefID("Academy");
    const int Factory = Game::Races::GetUnitDefID("Factory");
    const int Starport = Game::Races::GetUnitDefID("Starport");
    const int ScienceFacility = Game::Races::GetUnitDefID("ScienceFacility");
    const int Armory = Game::Races::GetUnitDefID("Armory");
    const int MissileTurret = Game::Races::GetUnitDefID("MissileTurret");
    const int Marine = Game::Races::GetUnitDefID("Marine");
    const int Firebat = Game::Races::GetUnitDefID("Firebat");
    const int Medic = Game::Races::GetUnitDefID("Medic");
    const int Ghost = Game::Races::GetUnitDefID("Ghost");
    const int Vulture = Game::Races::GetUnitDefID("Vulture");
    const int SiegeTank = Game::Races::GetUnitDefID("SiegeTank");
    const int Goliath = Game::Races::GetUnitDefID("Goliath");
    const int MachineShop = Game::Races::GetUnitDefID("MachineShop");
    const int Wraith = Game::Races::GetUnitDefID("Wraith");
    const int Dropship = Game::Races::GetUnitDefID("Dropship");
    const int ScienceVessel = Game::Races::GetUnitDefID("ScienceVessel");
    const int Battlecruiser = Game::Races::GetUnitDefID("Battlecruiser");
    const int Valkyrie = Game::Races::GetUnitDefID("Valkyrie");
    const int ControlTower = Game::Races::GetUnitDefID("ControlTower");
    const int SCV = Game::Races::GetUnitDefID("SCV");
    const int CovertOps = Game::Races::GetUnitDefID("CovertOps");
    const int PhysicsLab = Game::Races::GetUnitDefID("PhysicsLab");

    int EffectivePage = mState.CommandPanel.PanelPage;
    if (EffectivePage == PageNone)
        EffectivePage = GetDefaultTerranPageForDef(SelectedTerranDefId);

    if (const auto* Building = SelectedBuilding(); Building && Building->IsLifted)
    {
        const bool Airborne =
            Building->FlightPhase == Game::Components::BuildingFlightPhase::Airborne;
        SetCommandButton(0, "Move", HudCommandId::Move, Airborne);
        SetCommandButton(2, "Stop", HudCommandId::Stop, Airborne);
        SetCommandButton(4, "Patrol", HudCommandId::Patrol, Airborne);
        SetCommandButton(8, "Land", HudCommandId::Land, Airborne);
        return;
    }

    switch (EffectivePage)
    {
        case PageTerranScvMain:
            SetCommandButton(0, "Harvest", HudCommandId::Harvest, true);
            SetCommandButton(1, "Move", HudCommandId::Move, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            DisabledAbilityButtonForDef(3, "Repair", SCV);
            SetCommandButton(4, "Hold", HudCommandId::Hold, true);
            SetCommandButton(5, "Patrol", HudCommandId::Patrol, true);
            SetButton(6, "Basic Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconBuild, true, PageTerranScvBuild1, HudVisualId::BasicBuild);
            SetButton(7, "Advanced Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconMore, true, PageTerranScvBuild2, HudVisualId::AdvancedBuild);
            break;

        case PageTerranScvBuild1:
            BuildButtonForDef(0, CommandCenter);
            BuildButtonForDef(1, SupplyDepot);
            BuildButtonForDef(2, Refinery);
            BuildButtonForDef(3, Barracks);
            BuildButtonForDef(4, EngineeringBay);
            BuildButtonForDef(5, Bunker);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageTerranScvMain, HudVisualId::Back);
            break;

        case PageTerranScvBuild2:
            BuildButtonForDef(0, Academy);
            BuildButtonForDef(1, Factory);
            BuildButtonForDef(2, Starport);
            BuildButtonForDef(3, ScienceFacility);
            BuildButtonForDef(4, Armory);
            BuildButtonForDef(5, MissileTurret);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageTerranScvMain, HudVisualId::Back);
            break;

        case PageTerranUnitCommands:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(5, "Follow", HudCommandId::Follow, true);
            break;

        case PageTerranMarine:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            TechAbilityButton(6, TerranResearchId::StimPacks, "Stim", Marine);
            break;

        case PageTerranFirebat:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            TechAbilityButton(6, TerranResearchId::StimPacks, "Stim", Firebat);
            break;

        case PageTerranMedic:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Stop", HudCommandId::Stop, true);
            SetCommandButton(2, "Hold", HudCommandId::Hold, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            DisabledAbilityButtonForDef(6, "Heal", Medic);
            break;

        case PageTerranGhost:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            SpellButton(6, "Lockdown", HudCommandId::Lockdown,
                        Game::Systems::SpellType::Lockdown,
                        TerranResearchId::Lockdown, true);
            DisabledAbilityButtonForDef(7, "Cloak", Ghost);
            DisabledAbilityButtonForDef(8, "Nuke", NuclearSilo);
            break;

        case PageTerranVulture:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            TechAbilityButton(6, TerranResearchId::SpiderMines, "Mines", Vulture);
            break;

        case PageTerranGoliath:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(5, "Follow", HudCommandId::Follow, true);
            break;

        case PageTerranSiegeTank:
        {
            const auto* SelectedUnit = EcsEntities && SelectedEntityId > 0
                ? EcsEntities->GetComponent<Game::Components::UnitComponent>(SelectedEntityId)
                : nullptr;
            const bool IsSieged = SelectedUnit && SelectedUnit->ChkUnitID == 30;
            SetCommandButton(0, "Move", HudCommandId::Move, !IsSieged);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, !IsSieged);
            SetCommandButton(6,
                             IsSieged ? "Unsiege" : "Siege",
                             IsSieged ? HudCommandId::Unsiege : HudCommandId::Siege,
                             EcsEntities && SelectedEntityId > 0 &&
                                 Game::Systems::CommandSystem::CanSetSiegeMode(
                                     *EcsEntities, SelectedEntityId, !IsSieged));
            break;
        }

        case PageTerranWraith:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            TechAbilityButton(6, TerranResearchId::CloakingField, "Cloak", Wraith);
            break;

        case PageTerranDropship:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Stop", HudCommandId::Stop, true);
            SetCommandButton(2, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(3, "Follow", HudCommandId::Follow, true);
            SetCommandButton(6, "Load", HudCommandId::Load, true);
            SetCommandButton(7, "Unload", HudCommandId::UnloadAll, HasLoadedPassengers());
            break;

        case PageTerranScienceVessel:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Stop", HudCommandId::Stop, true);
            SetCommandButton(2, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(3, "Follow", HudCommandId::Follow, true);
            DisabledAbilityButtonForDef(6, "Matrix", ScienceVessel);
            SpellButton(7, "EMP", HudCommandId::EMPShockwave,
                        Game::Systems::SpellType::EMPShockwave,
                        TerranResearchId::None, false);
            DisabledAbilityButtonForDef(8, "Irradiate", ScienceVessel);
            break;

        case PageTerranBattlecruiser:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            SpellButton(6, "Yamato", HudCommandId::YamatoGun,
                        Game::Systems::SpellType::YamatoGun,
                        TerranResearchId::YamatoGun, true);
            break;

        case PageTerranValkyrie:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            break;

        case PageTerranPassiveBuilding:
            break;

        case PageTerranComsatStation:
            SetCommandButton(
                0, "Scan", HudCommandId::Scan,
                EcsEntities && SelectedEntityId > 0 &&
                    Game::Systems::SpellSystem::CanCastSpell(
                        *EcsEntities, SelectedEntityId,
                        Game::Systems::SpellType::ScannerSweep));
            break;

        case PageTerranNuclearSilo:
            DisabledAbilityButtonForDef(0, "Launch Nuke", NuclearSilo);
            break;

        case PageTerranBunker:
            SetCommandButton(0, "Load", HudCommandId::Load, true);
            SetCommandButton(1, "Unload", HudCommandId::UnloadAll, HasLoadedPassengers());
            break;

        case PageTerranCommandCenter:
            TrainButtonForDef(0, SCV);
            BuildButtonForDef(1, ComsatStation);
            BuildButtonForDef(2, NuclearSilo);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            SetCommandButton(8, "Lift Off", HudCommandId::LiftOff, CanLiftOffSelected());
            CancelButtonIfBusy();
            break;

        case PageTerranBarracks:
            TrainButtonForDef(0, Marine);
            TrainButtonForDef(1, Firebat);
            TrainButtonForDef(2, Medic);
            TrainButtonForDef(3, Ghost);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            SetCommandButton(8, "Lift Off", HudCommandId::LiftOff, CanLiftOffSelected());
            CancelButtonIfBusy();
            break;

        case PageTerranFactory:
            TrainButtonForDef(0, Vulture);
            TrainButtonForDef(1, SiegeTank);
            TrainButtonForDef(2, Goliath);
            BuildButtonForDef(3, MachineShop);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            SetCommandButton(8, "Lift Off", HudCommandId::LiftOff, CanLiftOffSelected());
            CancelButtonIfBusy();
            break;

        case PageTerranStarport:
            TrainButtonForDef(0, Wraith);
            TrainButtonForDef(1, Dropship);
            TrainButtonForDef(2, ScienceVessel);
            TrainButtonForDef(3, Battlecruiser);
            TrainButtonForDef(4, Valkyrie);
            BuildButtonForDef(5, ControlTower);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            SetCommandButton(8, "Lift Off", HudCommandId::LiftOff, CanLiftOffSelected());
            CancelButtonIfBusy();
            break;

        case PageTerranEngineeringBay:
            ResearchButton(0, TerranResearchId::InfantryWeapons, "Inf Wpn", IconWeapons);
            ResearchButton(1, TerranResearchId::InfantryArmor, "Inf Arm", IconArmor);
            CancelButtonIfBusy();
            break;

        case PageTerranArmory:
            ResearchButton(0, TerranResearchId::VehicleWeapons, "Veh Wpn", IconWeapons);
            ResearchButton(1, TerranResearchId::VehicleArmor, "Veh Arm", IconArmor);
            ResearchButton(2, TerranResearchId::ShipWeapons, "Ship Wpn", IconWeapons);
            ResearchButton(3, TerranResearchId::ShipArmor, "Ship Arm", IconArmor);
            CancelButtonIfBusy();
            break;

        case PageTerranAcademy:
            ResearchButton(0, TerranResearchId::StimPacks, "Stim Pack", IconWeapons);
            ResearchButton(1, TerranResearchId::U238Shells, "U-238", IconWeapons);
            CancelButtonIfBusy();
            break;

        case PageTerranScienceFacility:
            BuildButtonForDef(0, CovertOps);
            BuildButtonForDef(1, PhysicsLab);
            SetCommandButton(8, "Lift Off", HudCommandId::LiftOff, CanLiftOffSelected());
            break;

        case PageTerranMachineShop:
            ResearchButton(0, TerranResearchId::SiegeMode, "Siege Mode", IconWeapons);
            ResearchButton(1, TerranResearchId::SpiderMines, "Mines", IconWeapons);
            CancelButtonIfBusy();
            break;

        case PageTerranControlTower:
            ResearchButton(0, TerranResearchId::CloakingField, "Cloak", IconWeapons);
            ResearchButton(1, TerranResearchId::IonThrusters, "Boosters", IconWeapons);
            CancelButtonIfBusy();
            break;

        case PageTerranCovertOps:
            ResearchButton(0, TerranResearchId::Lockdown, "Lockdown", IconWeapons);
            ResearchButton(1, TerranResearchId::MoebiusReactor, "Moebius", IconWeapons);
            CancelButtonIfBusy();
            break;

        case PageTerranPhysicsLab:
            ResearchButton(0, TerranResearchId::YamatoGun, "Yamato", IconWeapons);
            ResearchButton(1, TerranResearchId::ColossusReactor, "Colossus", IconWeapons);
            CancelButtonIfBusy();
            break;

        default:
            break;
    }
}

void Hud::BuildZergPage(int SelectedEntityId,
                        int SelectedChkUnitId,
                        Core::ECS::EntityManager* EcsEntities)
{
    using Game::Systems::UpgradeSystem;

    auto TrainEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::ProductionSystem::CanTrain(*EcsEntities, SelectedEntityId, DefId);
    };

    auto BuildEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::ZergSystem::CanMorphDroneIntoBuilding(
                   *EcsEntities, SelectedEntityId, DefId);
    };

    auto PlayerIdForSelection = [&]() -> int
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return 0;
        if (auto* Player = EcsEntities->GetComponent<Game::Components::PlayerComponent>(SelectedEntityId))
            return Player->PlayerID;
        return 0;
    };

    auto SetCommandButton = [&](int Slot, const std::string& Label, HudCommandId CommandId, bool Enabled)
    {
        SetButton(Slot, Label, ActionKind::Command, -1, CommandId,
                  TerranResearchId::None, IconCommand, Enabled);
    };

    auto SpellButton = [&](int Slot,
                           const std::string& Label,
                           HudCommandId CommandId,
                           Game::Systems::SpellType Spell,
                           TerranResearchId ResearchId,
                           bool RequiresResearch)
    {
        const auto* Unit = EcsEntities && SelectedEntityId > 0
            ? EcsEntities->GetComponent<Game::Components::UnitComponent>(SelectedEntityId)
            : nullptr;
        const auto* Cooldown = EcsEntities && SelectedEntityId > 0
            ? EcsEntities->GetComponent<Game::Components::SpellCooldown>(SelectedEntityId)
            : nullptr;
        const bool Researched = !RequiresResearch || UpgradeSystem::HasResearch(
            PlayerIdForSelection(), ToResearchId(ResearchId));
        const bool Enabled = Unit && Researched &&
            Unit->EnergyPoints >= Game::Systems::SpellSystem::GetSpellEnergyCost(Spell) &&
            (!Cooldown || Cooldown->CooldownRemaining <= 0.0f);
        SetCommandButton(Slot, Label, CommandId, Enabled);
    };

    auto TrainButtonForDef = [&](int Slot, int DefId)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefId);
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Train,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), TrainEnabled(DefId), 0, HudVisualId::None,
                  Def.MineralCost, Def.GasCost, Def.SupplyCost);
    };

    auto BuildButtonForDef = [&](int Slot, int DefId)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefId);
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Build,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), BuildEnabled(DefId), 0, HudVisualId::None,
                  Def.MineralCost, Def.GasCost, Def.SupplyCost);
    };

    auto MorphButtonForDef = [&](int Slot, int DefId)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefId);
        const bool Enabled = EcsEntities && SelectedEntityId > 0 &&
            Game::Systems::ZergSystem::CanMorphBuilding(
                *EcsEntities, SelectedEntityId, DefId);
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::MorphBuilding,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), Enabled, 0, HudVisualId::None,
                  Def.MineralCost, Def.GasCost, Def.SupplyCost);
    };

    auto ResearchButton = [&](int Slot,
                              TerranResearchId ResearchId,
                              const std::string& BaseLabel,
                              int IconDefId)
    {
        const int PlayerID = PlayerIdForSelection();
        const auto SystemResearchId = ToResearchId(ResearchId);
        const bool Enabled = EcsEntities && SelectedEntityId > 0 &&
            UpgradeSystem::CanStartResearch(*EcsEntities, SelectedEntityId, SystemResearchId);
        int MineralCost = 0;
        int GasCost = 0;
        UpgradeSystem::GetResearchCost(SystemResearchId, PlayerID, MineralCost, GasCost);
        SetButton(Slot,
                  ResearchLabel(PlayerID, ResearchId, BaseLabel),
                  UpgradeSystem::IsUpgradeType(SystemResearchId)
                      ? ActionKind::Upgrade
                      : ActionKind::Research,
                  -1,
                  HudCommandId::None,
                  ResearchId,
                  IconForDef(IconDefId),
                  Enabled,
                  0,
                  HudVisualId::None,
                  MineralCost,
                  GasCost);
    };

    const int Hatchery = Game::Races::GetUnitDefID("Hatchery");
    const int SpawningPool = Game::Races::GetUnitDefID("Spawning Pool");
    const int EvolutionChamber = Game::Races::GetUnitDefID("Evolution Chamber");
    const int HydraliskDen = Game::Races::GetUnitDefID("Hydralisk Den");
    const int Spire = Game::Races::GetUnitDefID("Spire");
    const int NydusCanal = Game::Races::GetUnitDefID("Nydus Canal");
    const int QueensNest = Game::Races::GetUnitDefID("Queen's Nest");
    const int GreaterSpire = Game::Races::GetUnitDefID("Greater Spire");
    const int UltraliskCavern = Game::Races::GetUnitDefID("Ultralisk Cavern");
    const int DefilerMound = Game::Races::GetUnitDefID("Defiler Mound");
    const int Extractor = Game::Races::GetUnitDefID("Extractor");
    const int Drone = Game::Races::GetUnitDefID("Drone");
    const int Zergling = Game::Races::GetUnitDefID("Zergling");
    const int Hydralisk = Game::Races::GetUnitDefID("Hydralisk");
    const int Overlord = Game::Races::GetUnitDefID("Overlord");
    const int Mutalisk = Game::Races::GetUnitDefID("Mutalisk");
    const int Scourge = Game::Races::GetUnitDefID("Scourge");
    const int Queen = Game::Races::GetUnitDefID("Queen");
    const int Ultralisk = Game::Races::GetUnitDefID("Ultralisk");
    const int Defiler = Game::Races::GetUnitDefID("Defiler");
    const int Lurker = Game::Races::GetUnitDefID("Lurker");
    const int InfestedTerran = Game::Races::GetUnitDefID("Infested Terran");
    const int Lair = Game::Races::GetUnitDefID("Lair");
    const int Hive = Game::Races::GetUnitDefID("Hive");
    const int CreepColony = Game::Races::GetUnitDefID("Creep Colony");
    const int SporeColony = Game::Races::GetUnitDefID("Spore Colony");
    const int SunkenColony = Game::Races::GetUnitDefID("Sunken Colony");

    int SelectedDefId = Game::Races::GetUnitDefIDByChkUnitID(SelectedChkUnitId);
    if (EcsEntities && SelectedEntityId > 0)
    {
        if (auto* Building =
                EcsEntities->GetComponent<Game::Components::BuildingComponent>(SelectedEntityId))
        {
            if (Building->BuildingDefID >= 0)
                SelectedDefId = Building->BuildingDefID;
        }
    }

    auto CanRallySelected = [&]() -> bool
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return false;
        const auto* Building =
            EcsEntities->GetComponent<Game::Components::BuildingComponent>(SelectedEntityId);
        return Building && !Building->UnderConstruction && !Building->IsLifted &&
               (SelectedDefId == Hatchery || SelectedDefId == Lair || SelectedDefId == Hive);
    };

    auto CancelButtonIfBusy = [&]()
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return;
        const auto* Production =
            EcsEntities->GetComponent<Game::Components::ProductionComponent>(SelectedEntityId);
        const auto* Building =
            EcsEntities->GetComponent<Game::Components::BuildingComponent>(SelectedEntityId);
        if ((Production && !Production->Queue.empty()) ||
            (Building && Building->IsResearching))
        {
            SetButton(8, "Cancel", ActionKind::Cancel, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, 0, HudVisualId::Cancel);
        }
    };

    int EffectivePage = mState.CommandPanel.PanelPage;
    if (EffectivePage == PageNone)
        EffectivePage = GetDefaultPageForSelection(SelectedChkUnitId);
    if (EffectivePage == PageNone)
    {
        const auto SelectedDef = Game::Races::GetUnitDefByID(SelectedDefId);
        EffectivePage = SelectedDef.IsBuilding ? PageNone : PageZergUnitCommands;
    }

    switch (EffectivePage)
    {
        case PageZergDroneMain:
            SetCommandButton(0, "Harvest", HudCommandId::Harvest, true);
            SetCommandButton(1, "Move", HudCommandId::Move, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(4, "Hold", HudCommandId::Hold, true);
            SetCommandButton(5, "Patrol", HudCommandId::Patrol, true);
            SetButton(6, "Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconBuild, true, PageZergDroneBuild);
            if (const auto* Burrow = EcsEntities->GetComponent<Game::Components::BurrowComponent>(SelectedEntityId))
                SetCommandButton(3, Burrow->IsBurrowed ? "Unburrow" : "Burrow",
                                 HudCommandId::Burrow,
                                 UpgradeSystem::HasResearch(PlayerIdForSelection(),
                                     ToResearchId(TerranResearchId::ZergBurrow)) &&
                                     !Burrow->IsTransitioning());
            break;

        case PageZergDroneBuild:
            BuildButtonForDef(0, Hatchery);
            BuildButtonForDef(1, SpawningPool);
            BuildButtonForDef(2, Extractor);
            BuildButtonForDef(3, EvolutionChamber);
            BuildButtonForDef(4, HydraliskDen);
            BuildButtonForDef(5, Spire);
            BuildButtonForDef(6, CreepColony);
            SetButton(7, "Advanced", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconMore, true, PageZergDroneBuild2);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageZergDroneMain);
            break;

        case PageZergDroneBuild2:
            BuildButtonForDef(0, QueensNest);
            BuildButtonForDef(1, NydusCanal);
            BuildButtonForDef(2, UltraliskCavern);
            BuildButtonForDef(3, DefilerMound);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageZergDroneBuild);
            break;

        case PageZergUnitCommands:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(5, "Follow", HudCommandId::Follow, true);
            if (SelectedDefId == Overlord && UpgradeSystem::HasResearch(
                    PlayerIdForSelection(), ToResearchId(TerranResearchId::ZergVentralSacs)))
            {
                const auto* Loaded = EcsEntities->GetComponent<Game::Components::LoadedUnitsComponent>(SelectedEntityId);
                SetCommandButton(6, "Load", HudCommandId::Load, true);
                SetCommandButton(7, "Unload", HudCommandId::UnloadAll,
                                 Loaded && !Loaded->PassengerIDs.empty());
            }
            if (const auto* Burrow = EcsEntities->GetComponent<Game::Components::BurrowComponent>(SelectedEntityId))
            {
                const bool Innate = SelectedDefId == Lurker;
                const bool Researched = Innate || UpgradeSystem::HasResearch(
                    PlayerIdForSelection(), ToResearchId(TerranResearchId::ZergBurrow));
                if (SelectedDefId == Drone || SelectedDefId == Zergling ||
                    SelectedDefId == Hydralisk || SelectedDefId == Defiler ||
                    SelectedDefId == InfestedTerran || Innate)
                    SetCommandButton(8, Burrow->IsBurrowed ? "Unburrow" : "Burrow",
                                     HudCommandId::Burrow,
                                     Researched && !Burrow->IsTransitioning());
            }
            if (SelectedDefId == Queen)
            {
                SpellButton(6, "Parasite", HudCommandId::Parasite,
                            Game::Systems::SpellType::Parasite,
                            TerranResearchId::None, false);
                SpellButton(7, "Ensnare", HudCommandId::Ensnare,
                            Game::Systems::SpellType::Ensnare,
                            TerranResearchId::ZergEnsnare, true);
                SpellButton(8, "Broodling", HudCommandId::SpawnBroodling,
                            Game::Systems::SpellType::SpawnBroodling,
                            TerranResearchId::ZergSpawnBroodling, true);
            }
            else if (SelectedDefId == Defiler)
            {
                SpellButton(5, "Dark Swarm", HudCommandId::DarkSwarm,
                            Game::Systems::SpellType::DarkSwarm,
                            TerranResearchId::None, false);
                SpellButton(6, "Plague", HudCommandId::Plague,
                            Game::Systems::SpellType::Plague,
                            TerranResearchId::ZergPlague, true);
                SpellButton(7, "Consume", HudCommandId::Consume,
                            Game::Systems::SpellType::Consume,
                            TerranResearchId::ZergConsume, true);
            }
            break;

        case PageZergHatchery:
            TrainButtonForDef(0, Drone);
            TrainButtonForDef(1, Zergling);
            TrainButtonForDef(2, Hydralisk);
            TrainButtonForDef(3, Overlord);
            TrainButtonForDef(4, Mutalisk);
            TrainButtonForDef(5, Scourge);
            TrainButtonForDef(6, Queen);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            SetButton(8, "More", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconMore, true, PageZergHatcheryCommands,
                      HudVisualId::AdvancedBuild);
            CancelButtonIfBusy();
            break;

        case PageZergHatcheryCommands:
            TrainButtonForDef(0, Ultralisk);
            TrainButtonForDef(1, Defiler);
            ResearchButton(2, TerranResearchId::ZergBurrow, "Burrow", Drone);
            if (SelectedDefId == Hatchery)
                MorphButtonForDef(3, Lair);
            else if (SelectedDefId == Lair)
                MorphButtonForDef(3, Hive);
            if (SelectedDefId == Lair || SelectedDefId == Hive)
            {
                ResearchButton(4, TerranResearchId::ZergVentralSacs, "Ventral Sacs", Overlord);
                ResearchButton(5, TerranResearchId::ZergAntennae, "Antennae", Overlord);
                ResearchButton(6, TerranResearchId::ZergPneumatizedCarapace, "Carapace", Overlord);
            }
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageZergHatchery,
                      HudVisualId::Back);
            CancelButtonIfBusy();
            break;

        case PageZergCreepColony:
            MorphButtonForDef(0, SunkenColony);
            MorphButtonForDef(1, SporeColony);
            break;

        case PageZergSpawningPool:
            ResearchButton(0, TerranResearchId::ZergMetabolicBoost, "Metabolic", Zergling);
            ResearchButton(1, TerranResearchId::ZergAdrenalGlands, "Adrenal", Zergling);
            CancelButtonIfBusy();
            break;

        case PageZergEvolutionChamber:
            ResearchButton(0, TerranResearchId::ZergMeleeAttacks, "Melee", Zergling);
            ResearchButton(1, TerranResearchId::ZergMissileAttacks, "Missile", Hydralisk);
            ResearchButton(2, TerranResearchId::ZergCarapace, "Carapace", Ultralisk);
            CancelButtonIfBusy();
            break;

        case PageZergHydraliskDen:
            ResearchButton(0, TerranResearchId::ZergMuscularAugments, "Muscular", Hydralisk);
            ResearchButton(1, TerranResearchId::ZergGroovedSpines, "Spines", Hydralisk);
            ResearchButton(2, TerranResearchId::ZergLurkerMorph, "Lurker", Hydralisk);
            CancelButtonIfBusy();
            break;

        case PageZergSpire:
            ResearchButton(0, TerranResearchId::ZergFlyerAttacks, "Flyer Wpn", Mutalisk);
            ResearchButton(1, TerranResearchId::ZergFlyerCarapace, "Flyer Arm", Mutalisk);
            MorphButtonForDef(2, GreaterSpire);
            CancelButtonIfBusy();
            break;
        case PageZergGreaterSpire:
            ResearchButton(0, TerranResearchId::ZergFlyerAttacks, "Flyer Wpn", Mutalisk);
            ResearchButton(1, TerranResearchId::ZergFlyerCarapace, "Flyer Arm", Mutalisk);
            CancelButtonIfBusy();
            break;

        case PageZergQueensNest:
            ResearchButton(0, TerranResearchId::ZergEnsnare, "Ensnare", Queen);
            ResearchButton(1, TerranResearchId::ZergSpawnBroodling, "Broodling", Queen);
            ResearchButton(2, TerranResearchId::ZergQueenEnergy, "Gamete", Queen);
            CancelButtonIfBusy();
            break;

        case PageZergUltraliskCavern:
            ResearchButton(0, TerranResearchId::ZergAnabolicSynthesis, "Anabolic", Ultralisk);
            ResearchButton(1, TerranResearchId::ZergChitinousPlating, "Chitinous", Ultralisk);
            CancelButtonIfBusy();
            break;

        case PageZergDefilerMound:
            ResearchButton(0, TerranResearchId::ZergConsume, "Consume", Defiler);
            ResearchButton(1, TerranResearchId::ZergPlague, "Plague", Defiler);
            ResearchButton(2, TerranResearchId::ZergDefilerEnergy, "Node", Defiler);
            CancelButtonIfBusy();
            break;

        default:
            break;
    }
}

void Hud::BuildProtossPage(int SelectedEntityId,
                           int SelectedChkUnitId,
                           Core::ECS::EntityManager* EcsEntities)
{
    using Game::Systems::UpgradeSystem;

    auto TrainEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::ProductionSystem::CanTrain(*EcsEntities, SelectedEntityId, DefId);
    };

    auto BuildEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::BuildingSystem::CanBuild(
                   *EcsEntities, SelectedEntityId, DefId,
                   Game::Systems::BuildTarget{}, nullptr, false);
    };

    auto PlayerIdForSelection = [&]() -> int
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return 0;
        if (auto* Player = EcsEntities->GetComponent<Game::Components::PlayerComponent>(SelectedEntityId))
            return Player->PlayerID;
        return 0;
    };

    auto SetCommandButton = [&](int Slot, const std::string& Label, HudCommandId CommandId, bool Enabled)
    {
        SetButton(Slot, Label, ActionKind::Command, -1, CommandId,
                  TerranResearchId::None, IconCommand, Enabled);
    };

    auto SpellButton = [&](int Slot,
                           const std::string& Label,
                           HudCommandId CommandId,
                           Game::Systems::SpellType Spell,
                           TerranResearchId ResearchId,
                           bool RequiresResearch)
    {
        const auto* Unit = EcsEntities && SelectedEntityId > 0
            ? EcsEntities->GetComponent<Game::Components::UnitComponent>(SelectedEntityId)
            : nullptr;
        const auto* Cooldown = EcsEntities && SelectedEntityId > 0
            ? EcsEntities->GetComponent<Game::Components::SpellCooldown>(SelectedEntityId)
            : nullptr;
        const bool Researched = !RequiresResearch || UpgradeSystem::HasResearch(
            PlayerIdForSelection(), ToResearchId(ResearchId));
        const bool Enabled = Unit && Researched &&
            Unit->EnergyPoints >= Game::Systems::SpellSystem::GetSpellEnergyCost(Spell) &&
            (!Cooldown || Cooldown->CooldownRemaining <= 0.0f);
        SetCommandButton(Slot, Label, CommandId, Enabled);
    };

    auto TrainButtonForDef = [&](int Slot, int DefId)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefId);
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Train,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), TrainEnabled(DefId), 0, HudVisualId::None,
                  Def.MineralCost, Def.GasCost, Def.SupplyCost);
    };

    auto BuildButtonForDef = [&](int Slot, int DefId)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefId);
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Build,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), BuildEnabled(DefId), 0, HudVisualId::None,
                  Def.MineralCost, Def.GasCost, Def.SupplyCost);
    };

    auto ResearchButton = [&](int Slot,
                              TerranResearchId ResearchId,
                              const std::string& BaseLabel,
                              int IconDefId)
    {
        const int PlayerID = PlayerIdForSelection();
        const auto SystemResearchId = ToResearchId(ResearchId);
        const bool Enabled = EcsEntities && SelectedEntityId > 0 &&
            UpgradeSystem::CanStartResearch(*EcsEntities, SelectedEntityId, SystemResearchId);
        int MineralCost = 0;
        int GasCost = 0;
        UpgradeSystem::GetResearchCost(SystemResearchId, PlayerID, MineralCost, GasCost);
        SetButton(Slot,
                  ResearchLabel(PlayerID, ResearchId, BaseLabel),
                  UpgradeSystem::IsUpgradeType(SystemResearchId)
                      ? ActionKind::Upgrade
                      : ActionKind::Research,
                  -1,
                  HudCommandId::None,
                  ResearchId,
                  IconForDef(IconDefId),
                  Enabled,
                  0,
                  HudVisualId::None,
                  MineralCost,
                  GasCost);
    };

    const int Probe = Game::Races::GetUnitDefID("Probe");
    const int Nexus = Game::Races::GetUnitDefID("Nexus");
    const int Pylon = Game::Races::GetUnitDefID("Pylon");
    const int Assimilator = Game::Races::GetUnitDefID("Assimilator");
    const int Gateway = Game::Races::GetUnitDefID("Gateway");
    const int Forge = Game::Races::GetUnitDefID("Forge");
    const int PhotonCannon = Game::Races::GetUnitDefID("Photon Cannon");
    const int CyberneticsCore = Game::Races::GetUnitDefID("Cybernetics Core");
    const int ShieldBattery = Game::Races::GetUnitDefID("Shield Battery");
    const int RoboticsFacility = Game::Races::GetUnitDefID("Robotics Facility");
    const int Stargate = Game::Races::GetUnitDefID("Stargate");
    const int CitadelOfAdun = Game::Races::GetUnitDefID("Citadel of Adun");
    const int TemplarArchives = Game::Races::GetUnitDefID("Templar Archives");
    const int RoboticsSupportBay = Game::Races::GetUnitDefID("Robotics Support Bay");
    const int FleetBeacon = Game::Races::GetUnitDefID("Fleet Beacon");
    const int ArbiterTribunal = Game::Races::GetUnitDefID("Arbiter Tribunal");
    const int Observatory = Game::Races::GetUnitDefID("Observatory");
    const int Zealot = Game::Races::GetUnitDefID("Zealot");
    const int Dragoon = Game::Races::GetUnitDefID("Dragoon");
    const int HighTemplar = Game::Races::GetUnitDefID("High Templar");
    const int DarkTemplar = Game::Races::GetUnitDefID("Dark Templar");
    const int Shuttle = Game::Races::GetUnitDefID("Shuttle");
    const int Reaver = Game::Races::GetUnitDefID("Reaver");
    const int Observer = Game::Races::GetUnitDefID("Observer");
    const int Scout = Game::Races::GetUnitDefID("Scout");
    const int Corsair = Game::Races::GetUnitDefID("Corsair");
    const int Carrier = Game::Races::GetUnitDefID("Carrier");
    const int Arbiter = Game::Races::GetUnitDefID("Arbiter");

    int SelectedDefId = Game::Races::GetUnitDefIDByChkUnitID(SelectedChkUnitId);
    if (EcsEntities && SelectedEntityId > 0)
    {
        if (auto* Building =
                EcsEntities->GetComponent<Game::Components::BuildingComponent>(SelectedEntityId))
            if (Building->BuildingDefID >= 0)
                SelectedDefId = Building->BuildingDefID;
    }

    auto CanRallySelected = [&]() -> bool
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return false;
        const auto* Building =
            EcsEntities->GetComponent<Game::Components::BuildingComponent>(SelectedEntityId);
        if (!Building || Building->UnderConstruction || Building->IsLifted)
            return false;
        const int DefId = Building->BuildingDefID;
        return DefId == Nexus || DefId == Gateway ||
               DefId == RoboticsFacility || DefId == Stargate;
    };

    int EffectivePage = mState.CommandPanel.PanelPage;
    if (EffectivePage == PageNone)
        EffectivePage = GetDefaultPageForSelection(SelectedChkUnitId);
    if (EffectivePage == PageNone)
    {
        const auto SelectedDef = Game::Races::GetUnitDefByID(SelectedDefId);
        EffectivePage = SelectedDef.IsBuilding ? PageNone : PageProtossUnitCommands;
    }

    switch (EffectivePage)
    {
        case PageProtossProbeMain:
            SetCommandButton(0, "Harvest", HudCommandId::Harvest, true);
            SetCommandButton(1, "Move", HudCommandId::Move, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(4, "Hold", HudCommandId::Hold, true);
            SetCommandButton(5, "Patrol", HudCommandId::Patrol, true);
            SetButton(6, "Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconBuild, true, PageProtossProbeBuild);
            break;

        case PageProtossProbeBuild:
            BuildButtonForDef(0, Nexus);
            BuildButtonForDef(1, Pylon);
            BuildButtonForDef(2, Assimilator);
            BuildButtonForDef(3, Gateway);
            BuildButtonForDef(4, Forge);
            BuildButtonForDef(5, PhotonCannon);
            BuildButtonForDef(6, CyberneticsCore);
            SetButton(7, "Advanced", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconMore, true, PageProtossProbeBuild2);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageProtossProbeMain);
            break;

        case PageProtossProbeBuild2:
            BuildButtonForDef(0, ShieldBattery);
            BuildButtonForDef(1, RoboticsFacility);
            BuildButtonForDef(2, Stargate);
            BuildButtonForDef(3, CitadelOfAdun);
            BuildButtonForDef(4, TemplarArchives);
            BuildButtonForDef(5, RoboticsSupportBay);
            SetButton(7, "Advanced", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconMore, true, PageProtossProbeBuild3);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageProtossProbeBuild);
            break;

        case PageProtossProbeBuild3:
            BuildButtonForDef(0, FleetBeacon);
            BuildButtonForDef(1, ArbiterTribunal);
            BuildButtonForDef(2, Observatory);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageProtossProbeBuild2);
            break;

        case PageProtossUnitCommands:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(5, "Follow", HudCommandId::Follow, true);
            if (SelectedDefId == Shuttle)
            {
                const auto* Loaded = EcsEntities->GetComponent<Game::Components::LoadedUnitsComponent>(SelectedEntityId);
                SetCommandButton(6, "Load", HudCommandId::Load, true);
                SetCommandButton(7, "Unload", HudCommandId::UnloadAll,
                                 Loaded && !Loaded->PassengerIDs.empty());
            }
            else if (SelectedDefId == HighTemplar)
            {
                SpellButton(6, "Storm", HudCommandId::PsionicStorm,
                            Game::Systems::SpellType::PsionicStorm,
                            TerranResearchId::ProtossPsionicStorm, true);
                SpellButton(7, "Hallucination", HudCommandId::Hallucination,
                            Game::Systems::SpellType::Hallucination,
                            TerranResearchId::ProtossHallucination, true);
            }
            else if (SelectedDefId == Arbiter)
            {
                SpellButton(6, "Recall", HudCommandId::Recall,
                            Game::Systems::SpellType::Recall,
                            TerranResearchId::ProtossRecall, true);
                SpellButton(7, "Stasis", HudCommandId::StasisField,
                            Game::Systems::SpellType::StasisField,
                            TerranResearchId::ProtossStasisField, true);
            }
            break;

        case PageProtossNexus:
            TrainButtonForDef(0, Probe);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            break;

        case PageProtossGateway:
            TrainButtonForDef(0, Zealot);
            TrainButtonForDef(1, Dragoon);
            TrainButtonForDef(2, HighTemplar);
            TrainButtonForDef(3, DarkTemplar);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            break;

        case PageProtossForge:
            ResearchButton(0, TerranResearchId::ProtossGroundWeapons, "Ground Wpn", Zealot);
            ResearchButton(1, TerranResearchId::ProtossGroundArmor, "Ground Arm", Zealot);
            ResearchButton(2, TerranResearchId::ProtossPlasmaShields, "Shields", Zealot);
            break;

        case PageProtossCyberneticsCore:
            ResearchButton(0, TerranResearchId::ProtossAirWeapons, "Air Wpn", Scout);
            ResearchButton(1, TerranResearchId::ProtossAirArmor, "Air Arm", Scout);
            ResearchButton(2, TerranResearchId::ProtossSingularityCharge, "Singularity", Dragoon);
            break;

        case PageProtossRoboticsFacility:
            TrainButtonForDef(0, Shuttle);
            TrainButtonForDef(1, Reaver);
            TrainButtonForDef(2, Observer);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            break;

        case PageProtossStargate:
            TrainButtonForDef(0, Scout);
            TrainButtonForDef(1, Corsair);
            TrainButtonForDef(2, Carrier);
            TrainButtonForDef(3, Arbiter);
            SetCommandButton(7, "Rally", HudCommandId::Rally, CanRallySelected());
            break;

        case PageProtossCitadelOfAdun:
            ResearchButton(0, TerranResearchId::ProtossZealotSpeed, "Legs", Zealot);
            break;

        case PageProtossTemplarArchives:
            ResearchButton(0, TerranResearchId::ProtossPsionicStorm, "Storm", HighTemplar);
            ResearchButton(1, TerranResearchId::ProtossHallucination, "Hallucination", HighTemplar);
            ResearchButton(2, TerranResearchId::ProtossKhaydarinAmulet, "Amulet", HighTemplar);
            ResearchButton(3, TerranResearchId::ProtossMindControl, "Mind Control", DarkTemplar);
            ResearchButton(4, TerranResearchId::ProtossMaelstrom, "Maelstrom", DarkTemplar);
            ResearchButton(5, TerranResearchId::ProtossArgusTalisman, "Talisman", DarkTemplar);
            break;

        case PageProtossRoboticsSupportBay:
            ResearchButton(0, TerranResearchId::ProtossGraviticDrive, "Gravitic", Shuttle);
            ResearchButton(1, TerranResearchId::ProtossScarabDamage, "Scarab Dmg", Reaver);
            ResearchButton(2, TerranResearchId::ProtossReaverCapacity, "Capacity", Reaver);
            break;

        case PageProtossFleetBeacon:
            ResearchButton(0, TerranResearchId::ProtossApialSensors, "Apial", Scout);
            ResearchButton(1, TerranResearchId::ProtossArgusJewel, "Argus", Corsair);
            ResearchButton(2, TerranResearchId::ProtossCarrierCapacity, "Capacity", Carrier);
            break;

        case PageProtossArbiterTribunal:
            ResearchButton(0, TerranResearchId::ProtossRecall, "Recall", Arbiter);
            ResearchButton(1, TerranResearchId::ProtossStasisField, "Stasis", Arbiter);
            ResearchButton(2, TerranResearchId::ProtossKhaydarinCore, "Core", Arbiter);
            break;

        case PageProtossObservatory:
            ResearchButton(0, TerranResearchId::ProtossSensorArray, "Sensor", Observer);
            break;

        case PageProtossShieldBattery:
        {
            const auto* Unit = EcsEntities && SelectedEntityId > 0
                ? EcsEntities->GetComponent<Game::Components::UnitComponent>(SelectedEntityId)
                : nullptr;
            SetCommandButton(0, "Recharge", HudCommandId::RechargeShields,
                             Unit && Unit->EnergyPoints > 0);
            break;
        }

        default:
            break;
    }
}

} // namespace Game::Client::UI


