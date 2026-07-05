#include "UI/Hud.h"

#include "UI/HudButtonStyle.h"
#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"
#include "Game/Components.h"
#include "Core/Platform/Platform.h"
#include "Render/Draw2D.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"
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

    constexpr int ProtoProbe = 64;
    constexpr int ProtoNexus = 154;
    constexpr int ProtoRoboticsFacility = 155;
    constexpr int ProtoGateway = 160;
    constexpr int ProtoStargate = 167;

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
        case ProtoProbe: return PageProtossProbeMain;
        case ProtoNexus: return PageProtossNexus;
        case ProtoGateway: return PageProtossGateway;
        case ProtoRoboticsFacility: return PageProtossRoboticsFacility;
        case ProtoStargate: return PageProtossStargate;
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
    const int DefID = Game::Races::GetUnitDefIDByChkUnitID(SelectedChkUnitId);
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
                      TerranResearchId::None, IconBack, true);
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
            TechAbilityButton(6, TerranResearchId::Lockdown, "Lockdown", Ghost);
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
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            TechAbilityButton(6, TerranResearchId::SiegeMode, "Siege", SiegeTank);
            break;

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
            DisabledAbilityButtonForDef(6, "Load", Dropship);
            DisabledAbilityButtonForDef(7, "Unload", Dropship);
            break;

        case PageTerranScienceVessel:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Stop", HudCommandId::Stop, true);
            SetCommandButton(2, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(3, "Follow", HudCommandId::Follow, true);
            DisabledAbilityButtonForDef(6, "Matrix", ScienceVessel);
            DisabledAbilityButtonForDef(7, "EMP", ScienceVessel);
            DisabledAbilityButtonForDef(8, "Irradiate", ScienceVessel);
            break;

        case PageTerranBattlecruiser:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Attack", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            TechAbilityButton(6, TerranResearchId::YamatoGun, "Yamato", Battlecruiser);
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
            DisabledAbilityButtonForDef(0, "Scan", ComsatStation);
            break;

        case PageTerranNuclearSilo:
            DisabledAbilityButtonForDef(0, "Launch Nuke", NuclearSilo);
            break;

        case PageTerranBunker:
            DisabledAbilityButtonForDef(0, "Load", Bunker);
            DisabledAbilityButtonForDef(1, "Unload", Bunker);
            break;

        case PageTerranCommandCenter:
            TrainButtonForDef(0, SCV);
            BuildButtonForDef(1, ComsatStation);
            BuildButtonForDef(2, NuclearSilo);
            DisabledAbilityButtonForDef(8, "Lift Off", CommandCenter);
            CancelButtonIfBusy();
            break;

        case PageTerranBarracks:
            TrainButtonForDef(0, Marine);
            TrainButtonForDef(1, Firebat);
            TrainButtonForDef(2, Medic);
            TrainButtonForDef(3, Ghost);
            DisabledAbilityButtonForDef(8, "Lift Off", Barracks);
            CancelButtonIfBusy();
            break;

        case PageTerranFactory:
            TrainButtonForDef(0, Vulture);
            TrainButtonForDef(1, SiegeTank);
            TrainButtonForDef(2, Goliath);
            BuildButtonForDef(3, MachineShop);
            DisabledAbilityButtonForDef(8, "Lift Off", Factory);
            CancelButtonIfBusy();
            break;

        case PageTerranStarport:
            TrainButtonForDef(0, Wraith);
            TrainButtonForDef(1, Dropship);
            TrainButtonForDef(2, ScienceVessel);
            TrainButtonForDef(3, Battlecruiser);
            TrainButtonForDef(4, Valkyrie);
            BuildButtonForDef(5, ControlTower);
            DisabledAbilityButtonForDef(8, "Lift Off", Starport);
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
            DisabledAbilityButtonForDef(8, "Lift Off", ScienceFacility);
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

    const int Hatchery = Game::Races::GetUnitDefID("Hatchery");
    const int SpawningPool = Game::Races::GetUnitDefID("Spawning Pool");
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

    int EffectivePage = mState.CommandPanel.PanelPage;
    if (EffectivePage == PageNone)
        EffectivePage = GetDefaultPageForSelection(SelectedChkUnitId);

    switch (EffectivePage)
    {
        case PageZergDroneMain:
            SetButton(0, "Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconBuild, true, PageZergDroneBuild);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageZergDroneMain);
            break;

        case PageZergDroneBuild:
            BuildButtonForDef(0, Hatchery);
            BuildButtonForDef(1, SpawningPool);
            BuildButtonForDef(2, Extractor);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageZergDroneMain);
            break;

        case PageZergHatchery:
            TrainButtonForDef(0, Drone);
            TrainButtonForDef(1, Zergling);
            TrainButtonForDef(2, Hydralisk);
            TrainButtonForDef(3, Overlord);
            TrainButtonForDef(4, Mutalisk);
            TrainButtonForDef(5, Scourge);
            TrainButtonForDef(6, Queen);
            TrainButtonForDef(7, Ultralisk);
            TrainButtonForDef(8, Defiler);
            break;

        default:
            break;
    }
}

void Hud::BuildProtossPage(int SelectedEntityId,
                           int SelectedChkUnitId,
                           Core::ECS::EntityManager* EcsEntities)
{
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

    const int Probe = Game::Races::GetUnitDefID("Probe");
    const int Nexus = Game::Races::GetUnitDefID("Nexus");
    const int Pylon = Game::Races::GetUnitDefID("Pylon");
    const int Assimilator = Game::Races::GetUnitDefID("Assimilator");
    const int Gateway = Game::Races::GetUnitDefID("Gateway");
    const int Forge = Game::Races::GetUnitDefID("Forge");
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

    int EffectivePage = mState.CommandPanel.PanelPage;
    if (EffectivePage == PageNone)
        EffectivePage = GetDefaultPageForSelection(SelectedChkUnitId);

    switch (EffectivePage)
    {
        case PageProtossProbeMain:
            SetButton(0, "Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconBuild, true, PageProtossProbeBuild);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageProtossProbeMain);
            break;

        case PageProtossProbeBuild:
            BuildButtonForDef(0, Nexus);
            BuildButtonForDef(1, Pylon);
            BuildButtonForDef(2, Assimilator);
            BuildButtonForDef(3, Gateway);
            BuildButtonForDef(4, Forge);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageProtossProbeMain);
            break;

        case PageProtossNexus:
            TrainButtonForDef(0, Probe);
            break;

        case PageProtossGateway:
            TrainButtonForDef(0, Zealot);
            TrainButtonForDef(1, Dragoon);
            TrainButtonForDef(2, HighTemplar);
            TrainButtonForDef(3, DarkTemplar);
            break;

        case PageProtossRoboticsFacility:
            TrainButtonForDef(0, Shuttle);
            TrainButtonForDef(1, Reaver);
            TrainButtonForDef(2, Observer);
            break;

        case PageProtossStargate:
            TrainButtonForDef(0, Scout);
            TrainButtonForDef(1, Corsair);
            TrainButtonForDef(2, Carrier);
            TrainButtonForDef(3, Arbiter);
            break;

        default:
            break;
    }
}

} // namespace Game::Client::UI


