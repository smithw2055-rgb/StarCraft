#include "UI/Hud.h"

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Main/GameCommandDispatchRuntime.h"
#include "Main/GameLoop/CursorManager.h"
#include "UI/MainGui.h"
#include "UI/HudText.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{
    int gAttackCallCount = 0;
    int gAttackMoveCallCount = 0;
    int gLastAttackTarget = -1;
    std::vector<int> gLastAttackers;
    Core::Math::Vec2 gLastAttackMoveTarget;

    void ResetCombatStubCalls()
    {
        gAttackCallCount = 0;
        gAttackMoveCallCount = 0;
        gLastAttackTarget = -1;
        gLastAttackers.clear();
        gLastAttackMoveTarget = {};
    }
}

namespace Game::Systems
{

void CombatSystem::IssueAttack(Core::ECS::EntityManager&,
                               const std::vector<int>& Attackers,
                               int TargetEntityID)
{
    ++gAttackCallCount;
    gLastAttackers = Attackers;
    gLastAttackTarget = TargetEntityID;
}

void CombatSystem::IssueAttackMove(Core::ECS::EntityManager&,
                                   const std::vector<int>&,
                                   Core::Math::Vec2 Target)
{
    ++gAttackMoveCallCount;
    gLastAttackMoveTarget = Target;
}

void CombatSystem::ApplyDamage(Core::ECS::EntityManager&,
                               int,
                               const Game::Components::CombatComponent&,
                               int,
                               int,
                               int)
{
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

    int CreateTerranUnit(Core::ECS::EntityManager& EM,
                         int DefID,
                         int PlayerID)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = {64.0f, 64.0f};
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Game::Components::Race::Terran;
        Unit.Speed = Def.Speed;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;

        if (Def.IsBuilding)
        {
            auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
            Building.IsBuilding = true;
            Building.BuildingDefID = DefID;
            Building.UnderConstruction = false;
            Building.CanLiftOff = Def.CanLiftOff;
            Building.IsAddon = Def.IsAddon;
        }

        return EntityID;
    }

    int CreateZergUnit(Core::ECS::EntityManager& EM,
                       int DefID,
                       int PlayerID,
                       Core::Math::Vec2 Position)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Game::Components::Race::Zerg;
        Unit.Speed = Def.Speed;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        return EntityID;
    }

    int CreateProtossUnit(Core::ECS::EntityManager& EM,
                          int DefID,
                          int PlayerID,
                          Core::Math::Vec2 Position)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Game::Components::Race::Protoss;
        Unit.Speed = Def.Speed;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.ShieldPoints = Def.ShieldPoints;
        Unit.MaxShieldPoints = Def.ShieldPoints;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        return EntityID;
    }

    int CreateCompletedRaceBuilding(Core::ECS::EntityManager& EM,
                                    int DefID,
                                    int PlayerID,
                                    Core::Math::Vec2 Position)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.ShieldPoints = Def.ShieldPoints;
        Unit.MaxShieldPoints = Def.ShieldPoints;
        Unit.EnergyPoints = Def.EnergyPoints;
        Unit.MaxEnergyPoints = Def.EnergyPoints;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.BuildingDefID = DefID;
        Building.UnderConstruction = false;
        return EntityID;
    }

    const Game::Client::UI::CmdButtonDef& RequireButton(
        const Game::Client::UI::Hud& Hud,
        int Slot,
        const char* Label)
    {
        const auto& Buttons = Hud.GetButtonDefs();
        Require(Slot >= 0 && Slot < (int)Buttons.size(), "button slot is in range");
        const auto& Button = Buttons[(size_t)Slot];
        Require(Button.Label == Label, Label);
        return Button;
    }

    int CountBuildingsByDef(Core::ECS::EntityManager& EM, int BuildingDefID)
    {
        int Count = 0;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
        {
            auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
            if (Building && Building->BuildingDefID == BuildingDefID)
                ++Count;
        }
        return Count;
    }

    int CreateMineralField(Core::ECS::EntityManager& EM)
    {
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = {160.0f, 64.0f};
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = 176;
        Unit.Speed = 0.0f;
        auto& Resource = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
        Resource.Type = Game::Components::ResourceType::Minerals;
        Resource.Amount = 1500;
        Resource.MaxAmount = 1500;
        return EntityID;
    }

    int CreateVespeneGeyser(Core::ECS::EntityManager& EM, Core::Math::Vec2 Position)
    {
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = 188;
        Unit.Speed = 0.0f;
        Unit.HitPoints = 1;
        Unit.MaxHitPoints = 1;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = -1;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.BuildingDefID = -1;
        auto& Resource = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
        Resource.Type = Game::Components::ResourceType::VespeneGas;
        Resource.Amount = 5000;
        Resource.MaxAmount = 5000;
        EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        return EntityID;
    }
}

int main()
{
    Core::ECS::EntityManager EM;
    Game::Client::UI::Hud Hud;
    Game::Map::GameMap Map;
    Map.Generate(32, 32);
    Game::Systems::BuildingSystem::SetMap(&Map);
    Game::Systems::CommandSystem::SetMap(&Map);

    auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
    Resources.Minerals = 500;
    Resources.VespeneGas = 500;
    Resources.SupplyUsed = 0;
    Resources.SupplyMax = 20;

    const int CommandCenterDef = Game::Races::GetUnitDefID("CommandCenter");
    const int ScvDef = Game::Races::GetUnitDefID("SCV");
    const int CommandCenter = CreateTerranUnit(EM, CommandCenterDef, 0);

    Hud.UpdateContext(EM, {CommandCenter});
    const auto& ScvButton = RequireButton(Hud, 0, "SCV");
    const auto Scv = Game::Races::GetUnitDefByID(ScvDef);
    Require(ScvButton.Kind == Game::Client::UI::ActionKind::Train,
        "Command Center SCV button is a train action");
    Require(ScvButton.MineralCost == Scv.MineralCost &&
            ScvButton.GasCost == Scv.GasCost &&
            ScvButton.SupplyCost == Scv.SupplyCost,
        "train button exposes real unit resource costs");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {CommandCenter}, Hud, 0),
        "shared HUD command index dispatch accepts GUI or classic command-card clicks");
    Hud.UpdateContext(EM, {CommandCenter});
    const auto& QueuedInfo = Hud.GetState().InfoPanel;
    const auto GuiSelection =
        Game::Client::UI::BuildMainGuiSelectionPresentation(QueuedInfo);
    Require(QueuedInfo.QueueLabel == "Queue" &&
            !QueuedInfo.QueueEntries.empty() &&
            QueuedInfo.QueueEntries[0] == "SCV",
        "classic HUD info panel exposes the queued SCV after a train click");
    Require(GuiSelection.Name == "Command Center" &&
            GuiSelection.QueueLabel == "Queue" &&
            !GuiSelection.QueueEntries.empty() &&
            GuiSelection.QueueEntries[0] == "SCV",
        "GUI HUD selection presentation receives the queued SCV from classic HUD state");
    Require(GuiSelection.Progress >= 0.0f,
        "GUI HUD queue presentation remains visible before the first production tick");
    const auto& TrainCancelButton = RequireButton(Hud, 8, "Cancel");
    Require(TrainCancelButton.Kind == Game::Client::UI::ActionKind::Cancel &&
            TrainCancelButton.Enabled,
        "Command Center exposes an enabled cancel button while training is queued");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {CommandCenter}, Hud, 8),
        "shared HUD command index dispatch cancels active training from the command card");
    auto* CommandCenterProduction =
        EM.GetComponent<Game::Components::ProductionComponent>(CommandCenter);
    Require(CommandCenterProduction != nullptr &&
            CommandCenterProduction->Queue.empty(),
        "HUD cancel clears the active training queue");
    Require(Resources.Minerals == 500 && Resources.SupplyUsed == 0,
        "HUD cancel refunds queued SCV minerals and reserved supply");
    Hud.UpdateContext(EM, {CommandCenter});
    Require(Hud.GetButtonDefs()[8].Label != "Cancel",
        "training cancel button disappears after the queue is empty");

    for (const char* LiftableName :
         {"CommandCenter", "Barracks", "Factory", "Starport", "ScienceFacility"})
    {
        Require(Game::Races::GetUnitDefByName(LiftableName).CanLiftOff,
            "all project buildings with LiftOff and Landing assets are gameplay-liftable");
    }
    const int FlightCommandCenter = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, CommandCenterDef, Map.TileToWorld(26, 4), 0);
    Hud.UpdateContext(EM, {FlightCommandCenter});
    const auto& LiftOffButton = RequireButton(Hud, 8, "Lift Off");
    Require(LiftOffButton.Kind == Game::Client::UI::ActionKind::Command &&
            LiftOffButton.CommandId == Game::Client::UI::HudCommandId::LiftOff &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::LiftOff) == 282 &&
            LiftOffButton.Enabled,
        "grounded liftable building exposes an icon-backed Lift Off command");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {FlightCommandCenter}, Hud, 8),
        "Lift Off command dispatch starts the building flight transition");
    auto* FlightBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(FlightCommandCenter);
    auto* FlightUnit =
        EM.GetComponent<Game::Components::UnitComponent>(FlightCommandCenter);
    Require(FlightBuilding && FlightUnit && FlightBuilding->IsLifted &&
            FlightBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::LiftingOff &&
            FlightUnit->Speed == 0.0f,
        "HUD lift-off remains immobile until the real animation signal");

    Hud.UpdateContext(EM, {FlightCommandCenter});
    const auto& TransitionLandButton = RequireButton(Hud, 8, "Land");
    Require(!TransitionLandButton.Enabled,
        "Land command remains disabled while LiftOff animation is in progress");
    FlightBuilding->FlightAnimationComplete = true;
    Game::Systems::BuildingSystem::ProcessFlightTransition(EM, FlightCommandCenter);
    Require(FlightBuilding->FlightHeightTransitionActive &&
            FlightBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::LiftingOff &&
            FlightUnit->Speed == 0.0f,
        "LiftOff signal starts body rise only after the landing gear retracts");
    Game::Systems::BuildingSystem::AdvanceFlightHeightTransition(
        EM, FlightCommandCenter, 0.5f);
    Require(FlightBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::Airborne &&
            FlightUnit->Speed > 0.0f,
        "LiftOff animation signal completes the physical airborne transition");
    Hud.UpdateContext(EM, {FlightCommandCenter});
    const auto& FlyingMoveButton = RequireButton(Hud, 0, "Move");
    const auto& LandButton = RequireButton(Hud, 8, "Land");
    Require(FlyingMoveButton.Enabled && LandButton.Enabled &&
            LandButton.CommandId == Game::Client::UI::HudCommandId::Land &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::Land) == 283,
        "airborne building exposes movement and an icon-backed landing command");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {FlightCommandCenter}, Hud, 8) &&
            Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Land,
        "Land command enters pending target mode for landing placement");
    auto* FlightBuildingTransform =
        EM.GetComponent<Game::Components::TransformComponent>(FlightCommandCenter);
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
                EM, {FlightCommandCenter},
                Hud.GetPendingTargetMode(), -1,
                Game::Client::GameLoop::ClickTargetContext{},
                FlightBuildingTransform->Position),
        "dispatching land target placement starts landing");
    Hud.CancelPendingTarget();
    Require(FlightBuilding->IsLifted &&
            FlightBuilding->FlightPhase == Game::Components::BuildingFlightPhase::Landing,
        "HUD lands at the target position and keeps flight state until the Landing animation signals completion");

    const int ResumeMineral = CreateMineralField(EM);
    EM.GetComponent<Game::Components::TransformComponent>(ResumeMineral)->Position =
        Map.TileToWorld(29, 4);
    const int ResumeScv = CreateTerranUnit(EM, ScvDef, 0);
    EM.GetComponent<Game::Components::TransformComponent>(ResumeScv)->Position =
        Map.TileToWorld(27, 4);
    EM.AddComponent<Game::Components::PathfindingComponent>(ResumeScv);
    auto& ResumeHarvest =
        EM.AddComponent<Game::Components::HarvestComponent>(ResumeScv);
    ResumeHarvest.State = Game::Components::HarvestState::Idle;
    ResumeHarvest.TargetResourceID = ResumeMineral;
    ResumeHarvest.DesiredResourceID = ResumeMineral;
    ResumeHarvest.CarryingType = Game::Components::ResourceType::Minerals;
    ResumeHarvest.CarryingAmount = 8;

    Game::Systems::BuildingSystem::AdvanceFlightHeightTransition(
        EM, FlightCommandCenter, 0.5f);
    FlightBuilding->FlightAnimationComplete = true;
    Game::Systems::BuildingSystem::ProcessFlightTransition(EM, FlightCommandCenter);
    auto* FlightPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(FlightCommandCenter);
    auto* FlightRender =
        EM.GetComponent<Game::Components::RenderComponent>(FlightCommandCenter);
    Require(!FlightBuilding->IsLifted &&
            FlightBuilding->FlightPhase == Game::Components::BuildingFlightPhase::Grounded &&
            FlightPath && !FlightPath->IsAir &&
            (!FlightRender || FlightRender->Layer == Game::Components::RenderLayer::Buildings),
        "Landing animation signal restores ground pathing and rendering state");
    Require(ResumeHarvest.State == Game::Components::HarvestState::MovingToDepot &&
            ResumeHarvest.ReturnBuildingID == FlightCommandCenter,
        "landed Command Center resumes the nearby loaded SCV's interrupted mining loop");
    Hud.CancelPendingTarget();

    const int FactoryDef = Game::Races::GetUnitDefID("Factory");
    const int MachineShopDef = Game::Races::GetUnitDefID("MachineShop");
    const Core::Math::Vec2 FactoryPosition = Map.TileToWorld(18, 4);
    const int FactoryWithAddon = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, FactoryDef, FactoryPosition, 0);
    const int MachineShop = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, MachineShopDef, FactoryPosition + Core::Math::Vec2{96.0f, 16.0f},
        0, FactoryWithAddon);
    auto* FactoryBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(FactoryWithAddon);
    auto* MachineShopBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(MachineShop);
    auto* MachineShopPlayer =
        EM.GetComponent<Game::Components::PlayerComponent>(MachineShop);
    Require(FactoryBuilding && MachineShopBuilding && MachineShopPlayer &&
            FactoryBuilding->AddonID == MachineShop &&
            MachineShopBuilding->BuilderUnitID == FactoryWithAddon,
        "completed project add-on is linked to its parent");
    Require(Game::Systems::BuildingSystem::LiftOffBuilding(EM, FactoryWithAddon) &&
            FactoryBuilding->AddonID == -1 &&
            MachineShopBuilding->BuilderUnitID == -1 &&
            MachineShopPlayer->PlayerID == 0 &&
            MachineShopBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::LiftingOff,
        "parent lift-off preserves add-on ownership and starts its detach animation");
    FactoryBuilding->FlightAnimationComplete = true;
    Game::Systems::BuildingSystem::ProcessFlightTransition(EM, FactoryWithAddon);
    Game::Systems::BuildingSystem::AdvanceFlightHeightTransition(
        EM, FactoryWithAddon, 0.5f);
    MachineShopBuilding->FlightPhase = Game::Components::BuildingFlightPhase::Grounded;
    Require(Game::Systems::BuildingSystem::LandBuilding(
                EM, FactoryWithAddon, FactoryPosition),
        "airborne parent can land at the asset-derived add-on position");
    Game::Systems::BuildingSystem::AdvanceFlightHeightTransition(
        EM, FactoryWithAddon, 0.5f);
    FactoryBuilding->FlightAnimationComplete = true;
    Game::Systems::BuildingSystem::ProcessFlightTransition(EM, FactoryWithAddon);
    Require(FactoryBuilding->AddonID == MachineShop &&
            MachineShopBuilding->BuilderUnitID == FactoryWithAddon &&
            MachineShopPlayer->PlayerID == 0 &&
            MachineShopBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::Landing,
        "landing reconnects a compatible owned add-on and starts its Landing animation");

    const Core::Math::Vec2 RestoredFactoryPosition = Map.TileToWorld(10, 4);
    const int RestoredFactory = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, FactoryDef, RestoredFactoryPosition, 0);
    const int RestoredMachineShop = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, MachineShopDef,
        RestoredFactoryPosition + Core::Math::Vec2{96.0f, 16.0f}, 0);
    Game::Systems::BuildingSystem::RestoreSpawnedAddonLinks(EM);
    Require(EM.GetComponent<Game::Components::BuildingComponent>(RestoredFactory)->AddonID ==
                RestoredMachineShop &&
            EM.GetComponent<Game::Components::BuildingComponent>(RestoredMachineShop)->BuilderUnitID ==
                RestoredFactory,
        "campaign map spawn reconstructs parent/add-on links from current asset geometry");

    const int EngineeringBayDef = Game::Races::GetUnitDefID("EngineeringBay");
    const int EngineeringBay = CreateTerranUnit(EM, EngineeringBayDef, 0);
    Hud.UpdateContext(EM, {EngineeringBay});
    const auto& InfantryWeaponsButton = RequireButton(Hud, 0, "Inf Wpn +1");
    Require(InfantryWeaponsButton.Kind == Game::Client::UI::ActionKind::Upgrade &&
            InfantryWeaponsButton.Enabled,
        "Engineering Bay exposes Infantry Weapons as an upgrade action");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {EngineeringBay}, Hud, 0),
        "shared HUD command index dispatch starts active upgrades from the command card");
    auto* EngineeringBayBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(EngineeringBay);
    Require(EngineeringBayBuilding != nullptr, "Engineering Bay building component exists");
    EngineeringBayBuilding->BuildTimeRemaining =
        Game::Systems::UpgradeSystem::GetResearchTimeCost(
            Game::Systems::ResearchId::Terran_InfantryWeapons,
            0) * 0.5f;
    Hud.UpdateContext(EM, {EngineeringBay});
    const auto& ResearchInfo = Hud.GetState().InfoPanel;
    const auto GuiResearch =
        Game::Client::UI::BuildMainGuiSelectionPresentation(ResearchInfo);
    Require(ResearchInfo.QueueLabel == "Upgrade" &&
            !ResearchInfo.QueueEntries.empty() &&
            ResearchInfo.QueueEntries[0] == "Infantry Weapons",
        "classic HUD info panel exposes active upgrades as the building queue");
    Require(ResearchInfo.StatusLine == "Upgrading Infantry Weapons" &&
            ResearchInfo.Progress > 0.49f &&
            ResearchInfo.Progress < 0.51f,
        "classic HUD info panel exposes active upgrade progress");
    Require(GuiResearch.QueueLabel == "Upgrade" &&
            !GuiResearch.QueueEntries.empty() &&
            GuiResearch.QueueEntries[0] == "Infantry Weapons" &&
            GuiResearch.Progress > 0.49f &&
            GuiResearch.Progress < 0.51f,
        "GUI HUD selection presentation receives active upgrade progress");
    const auto& ResearchCancelButton = RequireButton(Hud, 8, "Cancel");
    Require(ResearchCancelButton.Kind == Game::Client::UI::ActionKind::Cancel &&
            ResearchCancelButton.Enabled,
        "Engineering Bay exposes an enabled cancel button while research is active");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {EngineeringBay}, Hud, 8),
        "shared HUD command index dispatch cancels active research from the command card");
    Require(!EngineeringBayBuilding->IsResearching &&
            EngineeringBayBuilding->ResearchingID == -1,
        "HUD cancel clears active research state");
    Require(Resources.Minerals == 500 && Resources.VespeneGas == 500,
        "HUD cancel refunds active research resources");
    Hud.UpdateContext(EM, {EngineeringBay});
    const auto& ResearchAfterCancel = Hud.GetState().InfoPanel;
    Require(ResearchAfterCancel.QueueLabel.empty() &&
            ResearchAfterCancel.QueueEntries.empty(),
        "research queue presentation clears after HUD cancel");
    Require(Hud.GetButtonDefs()[8].Label != "Cancel",
        "research cancel button disappears after research is cleared");

    const int AcademyDef = Game::Races::GetUnitDefID("Academy");
    const int Academy = CreateTerranUnit(EM, AcademyDef, 0);
    Hud.UpdateContext(EM, {Academy});
    const auto& StimPackButton = RequireButton(Hud, 0, "Stim Pack");
    Require(StimPackButton.Kind == Game::Client::UI::ActionKind::Research &&
            StimPackButton.Enabled,
        "Academy exposes Stim Packs as a research action");
    const auto& U238Button = RequireButton(Hud, 1, "U-238");
    Require(U238Button.Kind == Game::Client::UI::ActionKind::Upgrade &&
            U238Button.Enabled,
        "Academy exposes U-238 Shells as an upgrade action");

    const int ScvEntity = CreateTerranUnit(EM, ScvDef, 0);
    Hud.UpdateContext(EM, {ScvEntity});
    Hud.OnButtonInvoked(6);
    Hud.UpdateContext(EM, {ScvEntity});

    const auto& SupplyDepotButton = RequireButton(Hud, 1, "Supply Depot");
    const int SupplyDepotDef = Game::Races::GetUnitDefID("SupplyDepot");
    const auto SupplyDepot = Game::Races::GetUnitDefByID(SupplyDepotDef);
    Require(SupplyDepotButton.Kind == Game::Client::UI::ActionKind::Build,
        "SCV build page exposes Supply Depot as a build action");
    Require(SupplyDepotButton.MineralCost == SupplyDepot.MineralCost &&
            SupplyDepotButton.GasCost == SupplyDepot.GasCost,
        "build button exposes real building resource costs");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ScvEntity}, Hud, 1),
        "shared HUD command index dispatch accepts target-mode build clicks");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::BuildPlacement &&
            Hud.GetPendingBuildDefID() == SupplyDepotDef,
        "target-mode build click leaves HUD waiting for placement");
    Require(Hud.GetActiveCommandIndex() == 1,
        "target-mode build click exposes active command feedback for GUI highlights");

    const auto& BarracksButton = RequireButton(Hud, 3, "Barracks");
    const int BarracksDef = Game::Races::GetUnitDefID("Barracks");
    const auto Barracks = Game::Races::GetUnitDefByID(BarracksDef);
    Require(BarracksButton.MineralCost == Barracks.MineralCost &&
            BarracksButton.GasCost == Barracks.GasCost,
        "advanced build costs stay attached to command metadata");

    const int BuildingsBeforePlacement = CountBuildingsByDef(EM, SupplyDepotDef);
    const int MineralsBeforePlacement = Resources.Minerals;
    Game::Client::GameLoop::ClickTargetContext EmptyClickTarget;
    const Core::Math::Vec2 InvalidSupplyDepotPlacement = Map.TileToWorld(0, 0);
    Require(!Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ScvEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        InvalidSupplyDepotPlacement),
        "invalid world placement click reports failure");
    Require(CountBuildingsByDef(EM, SupplyDepotDef) == BuildingsBeforePlacement,
        "invalid world placement click does not create a building");
    Require(Resources.Minerals == MineralsBeforePlacement,
        "invalid world placement click does not spend resources");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::BuildPlacement &&
            Hud.GetPendingBuildDefID() == SupplyDepotDef,
        "invalid world placement keeps HUD waiting for a retry target");

    const Core::Math::Vec2 SupplyDepotPlacement = Map.TileToWorld(12, 12);
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ScvEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        SupplyDepotPlacement),
        "valid world placement click reports success");
    Require(CountBuildingsByDef(EM, SupplyDepotDef) == BuildingsBeforePlacement &&
            Game::Systems::BuildingSystem::CountPendingBuilds(0, SupplyDepotDef) == 1,
        "world placement click records a pending Supply Depot until the SCV reaches the site");
    Require(Resources.Minerals == MineralsBeforePlacement - SupplyDepot.MineralCost,
        "world placement click spends the building mineral cost");
    auto* ScvCommand = EM.GetComponent<Game::Components::CommandComponent>(ScvEntity);
    Require(ScvCommand &&
            (ScvCommand->CurrentCommand == Game::Components::CommandType::Move ||
             ScvCommand->CurrentCommand == Game::Components::CommandType::Build) &&
            Game::Systems::BuildingSystem::IsBuilderCommitted(EM, ScvEntity),
        "world placement click commits the worker to approaching the building site");
    Hud.CancelPendingTarget();

    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ScvEntity}, Hud, 2),
        "shared HUD command index dispatch accepts refinery build clicks");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::BuildRefinery,
        "refinery click leaves HUD waiting for a geyser target");
    Require(!Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ScvEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        Map.TileToWorld(13, 12)),
        "refinery target click reports failure when no geyser was clicked");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::BuildRefinery,
        "invalid refinery target keeps HUD waiting for a retry target");
    Hud.CancelPendingTarget();
    Hud.OnButtonInvoked(8);

    Hud.UpdateContext(EM, {ScvEntity});
    const auto& HarvestButton = RequireButton(Hud, 0, "Harvest");
    Require(HarvestButton.CommandId == Game::Client::UI::HudCommandId::Harvest,
        "SCV main page exposes harvest as a target command");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ScvEntity}, Hud, 0),
        "shared HUD command index dispatch accepts harvest clicks");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Harvest,
        "harvest click leaves HUD waiting for a resource target");
    Require(!Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ScvEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        {96.0f, 96.0f}),
        "harvest target click reports failure when no resource was clicked");
    Require(EM.GetComponent<Game::Components::HarvestComponent>(ScvEntity) == nullptr,
        "invalid harvest target does not create a harvest component");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Harvest,
        "invalid harvest target keeps HUD waiting for a retry target");
    const int MineralField = CreateMineralField(EM);
    Game::Client::GameLoop::ClickTargetContext ResourceClickTarget;
    ResourceClickTarget.ResourceEntityID = MineralField;
    ResourceClickTarget.AnyEntityID = MineralField;
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ScvEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        ResourceClickTarget,
        {160.0f, 64.0f}),
        "resource target click reports success");
    auto* Harvest = EM.GetComponent<Game::Components::HarvestComponent>(ScvEntity);
    Require(Harvest &&
            Harvest->TargetResourceID == MineralField &&
            Harvest->State == Game::Components::HarvestState::MovingToResource,
        "resource target click issues a real harvest order to the worker");
    Hud.CancelPendingTarget();

    Hud.UpdateContext(EM, {ScvEntity});
    const auto& MoveButton = RequireButton(Hud, 1, "Move");
    Require(MoveButton.CommandId == Game::Client::UI::HudCommandId::Move,
        "SCV main page exposes move as a target command");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ScvEntity}, Hud, 1),
        "shared HUD command index dispatch accepts move clicks");
    const Core::Math::Vec2 MoveTarget = Map.TileToWorld(18, 18);
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ScvEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        MoveTarget),
        "world move target click reports success");
    ScvCommand = EM.GetComponent<Game::Components::CommandComponent>(ScvEntity);
    Require(ScvCommand &&
            ScvCommand->CurrentCommand == Game::Components::CommandType::Move &&
            ScvCommand->TargetPosition.DistanceTo(MoveTarget) < 0.1f,
        "world target click issues a real move order to the worker");

    const int MedicDef = Game::Races::GetUnitDefID("Medic");
    const int MarineDef = Game::Races::GetUnitDefID("Marine");
    const int MarineEntity = CreateTerranUnit(EM, MarineDef, 0);
    auto* InitialMarineUnit = EM.GetComponent<Game::Components::UnitComponent>(MarineEntity);
    Require(InitialMarineUnit != nullptr, "Marine command test unit has unit data");
    InitialMarineUnit->HitPoints = Game::Races::GetUnitDefByID(MarineDef).HitPoints;
    InitialMarineUnit->MaxHitPoints = Game::Races::GetUnitDefByID(MarineDef).HitPoints;
    auto& MarineCombat = EM.AddComponent<Game::Components::CombatComponent>(MarineEntity);
    MarineCombat.GroundDamage = 6;
    MarineCombat.AttackRange = Game::Races::GetUnitDefByID(MarineDef).AttackRange;
    MarineCombat.CanTargetGround = true;
    MarineCombat.CanTargetAir = false;

    auto& PlayerUpgrades = Game::Systems::UpgradeSystem::GetUpgrades(0);
    PlayerUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Terran_U238Shells)] = 1;
    Hud.UpdateContext(EM, {MarineEntity});
    const auto& UpgradedMarineInfo = Hud.GetState().InfoPanel;
    Require(UpgradedMarineInfo.RangeValue == 5,
        "HUD info panel displays Marine range after U-238 Shells");
    Require(UpgradedMarineInfo.CombatLine.find("Range 5") != std::string::npos,
        "HUD combat line uses upgraded Marine range");

    const int EnemyMarineEntity = CreateTerranUnit(EM, MarineDef, 1);
    Game::Client::GameLoop::ClickTargetContext EnemyClickTarget;
    EnemyClickTarget.AnyEntityID = EnemyMarineEntity;
    EnemyClickTarget.EnemyEntityID = EnemyMarineEntity;

    Hud.UpdateContext(EM, {MarineEntity});
    const auto& StimButton = RequireButton(Hud, 6, "Stim");
    Require(!StimButton.Enabled,
        "Marine stim button is visible but disabled before Stim Packs research");
    Require(!Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {MarineEntity}, Hud, 6),
        "disabled visible command buttons do not report a handled dispatch");
    Require(!Hud.HasPendingTarget() && Hud.GetActiveCommandIndex() < 0,
        "disabled visible command buttons do not alter HUD command state");
    PlayerUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Terran_StimPacks)] = 1;
    Hud.UpdateContext(EM, {MarineEntity});
    const auto& ResearchedStimButton = RequireButton(Hud, 6, "Stim");
    Require(ResearchedStimButton.Enabled &&
            ResearchedStimButton.Kind == Game::Client::UI::ActionKind::UseTech &&
            ResearchedStimButton.ResearchId == Game::Systems::TerranResearchId::StimPacks,
        "Marine stim button becomes a use-tech action after Stim Packs research");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {MarineEntity}, Hud, 6),
        "shared HUD command index dispatch accepts researched use-tech buttons");
    auto* MarineCommand = EM.GetComponent<Game::Components::CommandComponent>(MarineEntity);
    const auto* StimOrder = MarineCommand ? MarineCommand->GetCurrentOrder() : nullptr;
    auto* MarineUnit = EM.GetComponent<Game::Components::UnitComponent>(MarineEntity);
    auto* MarineStatus = EM.GetComponent<Game::Components::StatusEffectComponent>(MarineEntity);
    Require(MarineCommand &&
            MarineCommand->CurrentCommand == Game::Components::CommandType::UseAbility &&
            StimOrder &&
            StimOrder->Type == Game::Components::OrderType::UseTech &&
            StimOrder->AbilityID ==
                static_cast<int>(Game::Systems::ResearchId::Terran_StimPacks),
        "researched stim click issues a use-tech order with the researched tech id");
    Require(MarineUnit && MarineUnit->HitPoints == Game::Races::GetUnitDefByID(MarineDef).HitPoints - 10,
        "stim use-tech click spends the Terran infantry hit point cost");
    Require(MarineStatus &&
            MarineStatus->HasEffect(Game::Components::StatusEffect::Stim),
        "stim use-tech click applies the Stim status effect");
    Require(!Hud.HasPendingTarget(),
        "instant researched use-tech buttons do not enter target mode");

    const auto& AttackButton = RequireButton(Hud, 1, "Attack");
    Require(AttackButton.CommandId == Game::Client::UI::HudCommandId::AttackMove,
        "combat unit page exposes attack as a target command");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {MarineEntity}, Hud, 1),
        "shared HUD command index dispatch accepts attack clicks");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::AttackMove,
        "attack click leaves HUD waiting for a target");
    ResetCombatStubCalls();
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {MarineEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EnemyClickTarget,
        {64.0f, 64.0f}),
        "enemy attack target click reports success");
    Require(gAttackCallCount == 1 &&
            gLastAttackTarget == EnemyMarineEntity &&
            gLastAttackers.size() == 1 &&
            gLastAttackers[0] == MarineEntity,
        "enemy attack target click issues a real targeted attack order");
    Require(gAttackMoveCallCount == 0,
        "enemy attack target click does not fall through to ground attack-move");
    Hud.CancelPendingTarget();

    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {MarineEntity}, Hud, 1),
        "shared HUD command index dispatch accepts ground attack clicks");
    const Core::Math::Vec2 AttackMoveTarget = Map.TileToWorld(20, 20);
    ResetCombatStubCalls();
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {MarineEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        AttackMoveTarget),
        "ground attack target click reports success");
    Require(gAttackMoveCallCount == 1 &&
            gLastAttackMoveTarget.DistanceTo(AttackMoveTarget) < 0.1f,
        "ground attack target click still issues attack-move");
    Require(gAttackCallCount == 0,
        "ground attack target click does not issue a targeted attack order");
    Hud.CancelPendingTarget();

    const int MedicEntity = CreateTerranUnit(EM, MedicDef, 0);
    Hud.UpdateContext(EM, {MedicEntity});
    const auto& FollowButton = RequireButton(Hud, 4, "Follow");
    Require(FollowButton.CommandId == Game::Client::UI::HudCommandId::Follow,
        "Medic page exposes follow as a target command");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {MedicEntity}, Hud, 4),
        "shared HUD command index dispatch accepts follow clicks");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Follow,
        "follow click leaves HUD waiting for an entity target");
    Require(!Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {MedicEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        {128.0f, 128.0f}),
        "follow target click reports failure when no entity was clicked");
    auto* MedicCommand = EM.GetComponent<Game::Components::CommandComponent>(MedicEntity);
    Require(!MedicCommand || MedicCommand->CurrentCommand != Game::Components::CommandType::Follow,
        "invalid follow target does not put the unit into follow command");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Follow,
        "invalid follow target keeps HUD waiting for a retry target");

    Resources.Minerals = 700;
    Resources.VespeneGas = 0;
    const int DroneDef = Game::Races::GetUnitDefID("Drone");
    const int HatcheryDef = Game::Races::GetUnitDefID("Hatchery");
    const auto Hatchery = Game::Races::GetUnitDefByID(HatcheryDef);
    const int DroneEntity = CreateZergUnit(EM, DroneDef, 0, Map.TileToWorld(24, 20));
    Hud.UpdateContext(EM, {DroneEntity});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {DroneEntity}, Hud, 6),
        "Zerg Drone build submenu click is dispatched through the shared HUD command path");
    Hud.UpdateContext(EM, {DroneEntity});
    const auto& HatcheryButton = RequireButton(Hud, 0, "Hatchery");
    Require(HatcheryButton.Kind == Game::Client::UI::ActionKind::Build &&
            HatcheryButton.Enabled &&
            HatcheryButton.MineralCost == Hatchery.MineralCost,
        "Zerg Drone build page exposes an enabled Hatchery button with real costs");
    Require(HatcheryButton.IconIndex == Hatchery.ChkUnitID,
        "Zerg Hatchery button uses the real command-icon frame keyed by CHK unit id");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {DroneEntity}, Hud, 0),
        "Zerg Hatchery build click enters target placement mode");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::BuildPlacement &&
            Hud.GetPendingBuildDefID() == HatcheryDef,
        "Zerg Hatchery build click leaves HUD waiting for placement");

    const int ZergBuildingsBefore = CountBuildingsByDef(EM, HatcheryDef);
    const int ZergMineralsBefore = Resources.Minerals;
    Core::Math::Vec2 ValidHatcheryPlacement = {};
    bool FoundHatcheryPlacement = false;
    for (int TileY = 4; TileY < 28 && !FoundHatcheryPlacement; ++TileY)
    {
        for (int TileX = 4; TileX < 28; ++TileX)
        {
            const Core::Math::Vec2 Candidate = Map.TileToWorld(TileX, TileY);
            if (Game::Systems::BuildingSystem::IsPlacementValid(EM, HatcheryDef, Candidate, 0))
            {
                ValidHatcheryPlacement = Candidate;
                FoundHatcheryPlacement = true;
                break;
            }
        }
    }
    Require(FoundHatcheryPlacement,
        "test map has a valid Hatchery placement");
    Require(!Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {DroneEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        Map.TileToWorld(0, 0)),
        "Zerg invalid building placement reports failure");
    Require(EM.IsAlive(DroneEntity),
        "Zerg invalid building placement does not consume the Drone");
    Require(Resources.Minerals == ZergMineralsBefore,
        "Zerg invalid building placement does not spend resources");
    Require(CountBuildingsByDef(EM, HatcheryDef) == ZergBuildingsBefore,
        "Zerg invalid building placement does not create a Hatchery");

    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {DroneEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        ValidHatcheryPlacement),
        "Zerg valid building placement queues a real Drone morph approach");
    Require(EM.IsAlive(DroneEntity) &&
            Game::Systems::BuildingSystem::IsBuilderCommitted(EM, DroneEntity),
        "Zerg valid building placement keeps the Drone alive while it approaches");
    Require(Resources.Minerals == ZergMineralsBefore - Hatchery.MineralCost,
        "Zerg valid building placement spends the building mineral cost");
    Require(CountBuildingsByDef(EM, HatcheryDef) == ZergBuildingsBefore &&
            Game::Systems::BuildingSystem::CountPendingBuilds(0, HatcheryDef) == 1,
        "Zerg Hatchery remains pending until its Drone reaches the footprint");

    Resources.Minerals = 500;
    const int ExtractorDef = Game::Races::GetUnitDefID("Extractor");
    const auto Extractor = Game::Races::GetUnitDefByID(ExtractorDef);
    const int ExtractorDrone = CreateZergUnit(EM, DroneDef, 0, Map.TileToWorld(26, 20));
    auto* ExtractorDroneUnit =
        EM.GetComponent<Game::Components::UnitComponent>(ExtractorDrone);
    const int ExtractorDroneSupplyCost =
        Game::Races::GetUnitDefByID(DroneDef).SupplyCost;
    ExtractorDroneUnit->SupplyCost = ExtractorDroneSupplyCost;
    Resources.SupplyUsed = ExtractorDroneSupplyCost;
    Hud.UpdateContext(EM, {ExtractorDrone});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ExtractorDrone}, Hud, 6),
        "Zerg Drone build submenu can be reopened for Extractor");
    Hud.UpdateContext(EM, {ExtractorDrone});
    const auto& ExtractorButton = RequireButton(Hud, 2, "Extractor");
    Require(ExtractorButton.Enabled && ExtractorButton.MineralCost == Extractor.MineralCost,
        "Zerg Drone build page exposes enabled Extractor with real cost");
    Require(ExtractorButton.IconIndex == Extractor.ChkUnitID,
        "Zerg Extractor button uses the real command-icon frame keyed by CHK unit id");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ExtractorDrone}, Hud, 2),
        "Zerg Extractor build click enters geyser target mode");
    Require(Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::BuildRefinery &&
            Hud.GetPendingBuildDefID() == ExtractorDef,
        "Zerg Extractor waits for a geyser target");
    const int ExtractorMineralsBefore = Resources.Minerals;
    Require(!Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ExtractorDrone},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        Map.TileToWorld(26, 20)),
        "Zerg Extractor target click fails when no geyser was clicked");
    Require(EM.IsAlive(ExtractorDrone) && Resources.Minerals == ExtractorMineralsBefore,
        "invalid Extractor target does not consume the Drone or resources");
    Require(Resources.SupplyUsed == ExtractorDroneSupplyCost,
        "invalid Extractor target keeps the Drone supply reserved");
    const int ZergGeyser = CreateVespeneGeyser(EM, Map.TileToWorld(26, 20));
    Game::Client::GameLoop::ClickTargetContext ZergGeyserTarget;
    ZergGeyserTarget.ResourceEntityID = ZergGeyser;
    ZergGeyserTarget.AnyEntityID = ZergGeyser;
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ExtractorDrone},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        ZergGeyserTarget,
        Map.TileToWorld(26, 20)),
        "Zerg Extractor target click claims a geyser");
    Require(EM.IsAlive(ExtractorDrone) &&
            Game::Systems::BuildingSystem::IsBuilderCommitted(EM, ExtractorDrone),
        "Zerg Extractor target keeps the Drone alive during its approach");
    Require(Resources.Minerals == ExtractorMineralsBefore - Extractor.MineralCost,
        "Zerg Extractor target click spends the mineral cost");
    Require(Resources.SupplyUsed == ExtractorDroneSupplyCost,
        "pending Zerg Extractor keeps the moving Drone supply reserved");
    auto* ExtractorBuilding = EM.GetComponent<Game::Components::BuildingComponent>(ZergGeyser);
    Require(ExtractorBuilding && ExtractorBuilding->BuildingDefID != ExtractorDef &&
            Game::Systems::BuildingSystem::CountPendingBuilds(0, ExtractorDef) == 1,
        "Zerg Extractor leaves the geyser intact until its Drone arrives");

    Resources.Minerals = 500;
    const int ProbeDef = Game::Races::GetUnitDefID("Probe");
    const int AssimilatorDef = Game::Races::GetUnitDefID("Assimilator");
    const auto Assimilator = Game::Races::GetUnitDefByID(AssimilatorDef);
    const int ProbeEntity = CreateProtossUnit(EM, ProbeDef, 0, Map.TileToWorld(28, 20));
    Hud.UpdateContext(EM, {ProbeEntity});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ProbeEntity}, Hud, 6),
        "Protoss Probe build submenu click is dispatched through the shared HUD command path");
    Hud.UpdateContext(EM, {ProbeEntity});
    const auto& AssimilatorButton = RequireButton(Hud, 2, "Assimilator");
    Require(AssimilatorButton.Enabled && AssimilatorButton.MineralCost == Assimilator.MineralCost,
        "Protoss Probe build page exposes enabled Assimilator with real cost");
    Require(AssimilatorButton.IconIndex == Assimilator.ChkUnitID,
        "Protoss Assimilator button uses the real command-icon frame keyed by CHK unit id");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ProbeEntity}, Hud, 2),
        "Protoss Assimilator build click enters geyser target mode");
    Require(Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::BuildRefinery &&
            Hud.GetPendingBuildDefID() == AssimilatorDef,
        "Protoss Assimilator waits for a geyser target");
    const int AssimilatorMineralsBefore = Resources.Minerals;
    const int ProtossGeyser = CreateVespeneGeyser(EM, Map.TileToWorld(28, 20));
    Game::Client::GameLoop::ClickTargetContext ProtossGeyserTarget;
    ProtossGeyserTarget.ResourceEntityID = ProtossGeyser;
    ProtossGeyserTarget.AnyEntityID = ProtossGeyser;
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {ProbeEntity},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        ProtossGeyserTarget,
        Map.TileToWorld(28, 20)),
        "Protoss Assimilator target click claims a geyser");
    Require(EM.IsAlive(ProbeEntity),
        "Protoss Assimilator target click keeps the Probe alive");
    Require(Resources.Minerals == AssimilatorMineralsBefore - Assimilator.MineralCost,
        "Protoss Assimilator target click spends the mineral cost");
    auto* AssimilatorBuilding = EM.GetComponent<Game::Components::BuildingComponent>(ProtossGeyser);
    Require(AssimilatorBuilding && AssimilatorBuilding->BuildingDefID == -1 &&
            Game::Systems::BuildingSystem::CountPendingBuilds(0, AssimilatorDef) == 1,
        "Protoss Assimilator target click reserves the geyser until the Probe reaches it");

    Resources.Minerals = 900;
    const int NexusDef = Game::Races::GetUnitDefID("Nexus");
    const int PylonDef = Game::Races::GetUnitDefID("Pylon");
    const int GatewayDef = Game::Races::GetUnitDefID("Gateway");
    const auto Gateway = Game::Races::GetUnitDefByID(GatewayDef);
    Require(Gateway.RequiresPylon,
        "Gateway definition requires Pylon power");
    Require(!Game::Races::GetUnitDefByID(NexusDef).RequiresPylon &&
            !Game::Races::GetUnitDefByID(PylonDef).RequiresPylon &&
            !Game::Races::GetUnitDefByID(AssimilatorDef).RequiresPylon,
        "Nexus, Assimilator, and Pylon can be placed without existing Pylon power");
    Require(!Game::Client::GameLoop::RequiresPylonChkId(154) &&
            !Game::Client::GameLoop::RequiresPylonChkId(156) &&
            !Game::Client::GameLoop::RequiresPylonChkId(157) &&
            Game::Client::GameLoop::RequiresPylonChkId(160),
        "CHK building power metadata exempts Nexus, Pylon, and Assimilator only");
    const int NexusEntity = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, NexusDef, Map.TileToWorld(4, 24), 0);
    Require(NexusEntity > 0,
        "completed Nexus satisfies Gateway prerequisite");
    const int GatewayProbe = CreateProtossUnit(EM, ProbeDef, 0, Map.TileToWorld(22, 24));
    const Core::Math::Vec2 GatewayPlacement = Map.TileToWorld(23, 24);
    Hud.UpdateContext(EM, {GatewayProbe});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {GatewayProbe}, Hud, 6),
        "Protoss Probe build submenu can be reopened for Gateway");
    Hud.UpdateContext(EM, {GatewayProbe});
    const auto& GatewayButton = RequireButton(Hud, 3, "Gateway");
    Require(GatewayButton.Enabled && GatewayButton.MineralCost == Gateway.MineralCost,
        "Gateway button is enabled once Nexus prerequisite and resources are available");
    Require(GatewayButton.IconIndex == Gateway.ChkUnitID,
        "Protoss Gateway button uses the real command-icon frame keyed by CHK unit id");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {GatewayProbe}, Hud, 3),
        "Protoss Gateway build click enters placement mode");
    Require(!Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {GatewayProbe},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        GatewayPlacement),
        "Protoss Gateway placement fails outside Pylon power");
    Require(EM.IsAlive(GatewayProbe),
        "failed Protoss Gateway placement keeps the Probe alive");
    Require(Resources.Minerals == 900,
        "failed Protoss Gateway placement does not spend resources");

    const int PylonEntity = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, PylonDef, Map.TileToWorld(20, 24), 0);
    Require(PylonEntity > 0,
        "completed Pylon can be spawned to power the Gateway placement");
    Require(Game::ProtossPower::ContainsTileOffset(6, 0),
        "Pylon circular power reaches the six-tile cardinal boundary");
    Require(!Game::ProtossPower::ContainsTileOffset(5, 5),
        "Pylon circular power excludes a five-by-five diagonal outside its radius");
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM,
        {GatewayProbe},
        Hud.GetPendingTargetMode(),
        Hud.GetPendingBuildDefID(),
        EmptyClickTarget,
        GatewayPlacement),
        "Protoss Gateway placement succeeds inside Pylon power");
    Require(EM.IsAlive(GatewayProbe),
        "successful Protoss Gateway placement keeps the Probe alive");
    Require(Resources.Minerals == 900 - Gateway.MineralCost,
        "successful Protoss Gateway placement spends the building mineral cost");
    Require(Game::Systems::BuildingSystem::CountPendingBuilds(0, GatewayDef) == 1 &&
            Game::Systems::BuildingSystem::IsBuilderCommitted(EM, GatewayProbe),
        "powered Gateway placement stays pending while the Probe approaches the warp site");

    Resources.Minerals = 3000;
    Resources.VespeneGas = 3000;
    Resources.SupplyUsed = 0;
    Resources.SupplyMax = 80;

    const int HydraliskDef = Game::Races::GetUnitDefID("Hydralisk");
    const int HydraliskEntity =
        CreateZergUnit(EM, HydraliskDef, 0, Map.TileToWorld(8, 28));
    Hud.UpdateContext(EM, {HydraliskEntity});
    const auto& ZergAttackButton = RequireButton(Hud, 1, "Attack");
    Require(ZergAttackButton.Kind == Game::Client::UI::ActionKind::Command &&
            ZergAttackButton.CommandId == Game::Client::UI::HudCommandId::AttackMove,
        "Zerg combat units default to a real command page instead of an empty Protoss page");

    const int ZergAdvancedDrone =
        CreateZergUnit(EM, DroneDef, 0, Map.TileToWorld(10, 28));
    Hud.UpdateContext(EM, {ZergAdvancedDrone});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ZergAdvancedDrone}, Hud, 6),
        "Zerg Drone opens the expanded build menu from the worker command page");
    Hud.UpdateContext(EM, {ZergAdvancedDrone});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ZergAdvancedDrone}, Hud, 7),
        "Zerg Drone build page exposes an advanced build submenu");
    Hud.UpdateContext(EM, {ZergAdvancedDrone});
    const auto& QueensNestButton = RequireButton(Hud, 0, "Queen's Nest");
    Require(QueensNestButton.Kind == Game::Client::UI::ActionKind::Build,
        "Zerg advanced build page exposes Queen's Nest as a build action");

    const int EvolutionChamberDef = Game::Races::GetUnitDefID("Evolution Chamber");
    const int EvolutionChamber = CreateCompletedRaceBuilding(
        EM, EvolutionChamberDef, 0, Map.TileToWorld(12, 28));
    Hud.UpdateContext(EM, {EvolutionChamber});
    const auto& ZergMeleeButton = RequireButton(Hud, 0, "Melee +1");
    Require(ZergMeleeButton.Kind == Game::Client::UI::ActionKind::Upgrade &&
            ZergMeleeButton.Enabled &&
            ZergMeleeButton.ResearchId == Game::Systems::TerranResearchId::ZergMeleeAttacks,
        "Zerg Evolution Chamber exposes melee upgrades as enabled command-card actions");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {EvolutionChamber}, Hud, 0),
        "Zerg Evolution Chamber upgrade button starts research");
    auto* EvolutionBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(EvolutionChamber);
    Require(EvolutionBuilding && EvolutionBuilding->IsResearching &&
            EvolutionBuilding->ResearchingID ==
                static_cast<int>(Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "Zerg Evolution Chamber records active melee upgrade research");

    const int HydraliskDenDef = Game::Races::GetUnitDefID("Hydralisk Den");
    const int HydraliskDen = CreateCompletedRaceBuilding(
        EM, HydraliskDenDef, 0, Map.TileToWorld(14, 28));
    Hud.UpdateContext(EM, {HydraliskDen});
    const auto& LockedLurkerButton = RequireButton(Hud, 2, "Lurker");
    Require(LockedLurkerButton.Kind == Game::Client::UI::ActionKind::Research &&
            !LockedLurkerButton.Enabled,
        "Zerg Hydralisk Den keeps Lurker Aspect locked before Lair technology");
    CreateCompletedRaceBuilding(
        EM, Game::Races::GetUnitDefID("Lair"), 0, Map.TileToWorld(16, 26));
    Hud.UpdateContext(EM, {HydraliskDen});
    const auto& LurkerButton = RequireButton(Hud, 2, "Lurker");
    Require(LurkerButton.Kind == Game::Client::UI::ActionKind::Research &&
            LurkerButton.Enabled,
        "completed Lair unlocks Lurker Aspect at the Hydralisk Den");

    const int DragoonDef = Game::Races::GetUnitDefID("Dragoon");
    const int DragoonEntity =
        CreateProtossUnit(EM, DragoonDef, 0, Map.TileToWorld(16, 28));
    Hud.UpdateContext(EM, {DragoonEntity});
    const auto& ProtossAttackButton = RequireButton(Hud, 1, "Attack");
    Require(ProtossAttackButton.Kind == Game::Client::UI::ActionKind::Command &&
            ProtossAttackButton.CommandId == Game::Client::UI::HudCommandId::AttackMove,
        "Protoss combat units default to a real command page");

    const int ProtossAdvancedProbe =
        CreateProtossUnit(EM, ProbeDef, 0, Map.TileToWorld(18, 28));
    Hud.UpdateContext(EM, {ProtossAdvancedProbe});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ProtossAdvancedProbe}, Hud, 6),
        "Protoss Probe opens the expanded build menu from the worker command page");
    Hud.UpdateContext(EM, {ProtossAdvancedProbe});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ProtossAdvancedProbe}, Hud, 7),
        "Protoss Probe first build page exposes an advanced submenu");
    Hud.UpdateContext(EM, {ProtossAdvancedProbe});
    const auto& RoboticsButton = RequireButton(Hud, 1, "Robotics Facility");
    Require(RoboticsButton.Kind == Game::Client::UI::ActionKind::Build,
        "Protoss advanced build page exposes Robotics Facility as a build action");

    const int ForgeDef = Game::Races::GetUnitDefID("Forge");
    const int Forge = CreateCompletedRaceBuilding(
        EM, ForgeDef, 0, Map.TileToWorld(20, 28));
    Hud.UpdateContext(EM, {Forge});
    const auto& GroundWeaponsButton = RequireButton(Hud, 0, "Ground Wpn +1");
    Require(GroundWeaponsButton.Kind == Game::Client::UI::ActionKind::Upgrade &&
            GroundWeaponsButton.Enabled &&
            GroundWeaponsButton.ResearchId == Game::Systems::TerranResearchId::ProtossGroundWeapons,
        "Protoss Forge exposes ground weapon upgrades");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {Forge}, Hud, 0),
        "Protoss Forge upgrade button starts research");
    auto* ForgeBuilding = EM.GetComponent<Game::Components::BuildingComponent>(Forge);
    Require(ForgeBuilding && ForgeBuilding->IsResearching &&
            ForgeBuilding->ResearchingID ==
                static_cast<int>(Game::Systems::ResearchId::Protoss_GroundWeapons),
        "Protoss Forge records active ground weapon research");

    const int CyberneticsCoreDef = Game::Races::GetUnitDefID("Cybernetics Core");
    const int CyberneticsCore = CreateCompletedRaceBuilding(
        EM, CyberneticsCoreDef, 0, Map.TileToWorld(22, 28));
    Hud.UpdateContext(EM, {CyberneticsCore});
    const auto& SingularityButton = RequireButton(Hud, 2, "Singularity");
    Require(SingularityButton.Kind == Game::Client::UI::ActionKind::Upgrade &&
            SingularityButton.Enabled,
        "Protoss Cybernetics Core exposes Singularity Charge as an upgrade action");

    Hud.UpdateContext(EM, {CommandCenter});
    const auto& RallyButton = RequireButton(Hud, 5, "Rally");
    Require(RallyButton.Enabled &&
            RallyButton.CommandId == Game::Client::UI::HudCommandId::Rally,
        "Terran production buildings expose an enabled rally command");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {CommandCenter}, Hud, 5),
        "Terran rally button enters target mode");
    const Core::Math::Vec2 RallyPoint = Map.TileToWorld(12, 12);
    Require(Game::Client::GameLoop::ExecutePendingTargetCommand(
                EM, {CommandCenter}, Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(), EmptyClickTarget, RallyPoint),
        "Terran rally target is accepted");
    const auto* RallyCommand =
        EM.GetComponent<Game::Components::CommandComponent>(CommandCenter);
    Require(RallyCommand && RallyCommand->HasRallyPoint &&
            RallyCommand->RallyPoint.DistanceTo(RallyPoint) < 0.1f,
        "Terran production building stores the selected rally point");

    const int ComsatDef = Game::Races::GetUnitDefID("ComsatStation");
    const int Comsat = CreateTerranUnit(EM, ComsatDef, 0);
    auto* ComsatUnit = EM.GetComponent<Game::Components::UnitComponent>(Comsat);
    ComsatUnit->EnergyPoints = 200;
    ComsatUnit->MaxEnergyPoints = 200;
    Hud.UpdateContext(EM, {Comsat});
    const auto& ScanButton = RequireButton(Hud, 0, "Scan");
    Require(ScanButton.Enabled &&
            ScanButton.CommandId == Game::Client::UI::HudCommandId::Scan,
        "Comsat exposes Scanner Sweep when it has enough energy");
    const size_t RevealCountBefore =
        EM.GetEntitiesWith<Game::Components::RevealAreaComponent>().size();
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {Comsat}, Hud, 0) &&
            Game::Client::GameLoop::ExecutePendingTargetCommand(
                EM, {Comsat}, Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(), EmptyClickTarget,
                Map.TileToWorld(16, 16)),
        "Comsat Scanner Sweep accepts a map target");
    Require(ComsatUnit->EnergyPoints == 150 &&
            EM.GetEntitiesWith<Game::Components::RevealAreaComponent>().size() ==
                RevealCountBefore + 1,
        "Scanner Sweep spends energy and creates a timed reveal area");

    const int DropshipDef = Game::Races::GetUnitDefID("Dropship");
    const int TransportMarineDef = Game::Races::GetUnitDefID("Marine");
    const int DropshipEntity = CreateTerranUnit(EM, DropshipDef, 0);
    const int Passenger = CreateTerranUnit(EM, TransportMarineDef, 0);
    EM.GetComponent<Game::Components::TransformComponent>(DropshipEntity)->Position =
        Map.TileToWorld(18, 18);
    EM.GetComponent<Game::Components::TransformComponent>(Passenger)->Position =
        Map.TileToWorld(19, 18);
    EM.AddComponent<Game::Components::RenderComponent>(Passenger).Visible = true;
    Hud.UpdateContext(EM, {DropshipEntity});
    const auto& LoadButton = RequireButton(Hud, 6, "Load");
    Require(LoadButton.Enabled &&
            LoadButton.CommandId == Game::Client::UI::HudCommandId::Load,
        "Dropship exposes a targeted load command");
    Game::Client::GameLoop::ClickTargetContext LoadTarget;
    LoadTarget.AnyEntityID = Passenger;
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {DropshipEntity}, Hud, 6) &&
            Game::Client::GameLoop::ExecutePendingTargetCommand(
                EM, {DropshipEntity}, Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(), LoadTarget,
                EM.GetComponent<Game::Components::TransformComponent>(Passenger)->Position),
        "Dropship loads a selected friendly ground unit");
    const auto* Loaded =
        EM.GetComponent<Game::Components::LoadedUnitsComponent>(DropshipEntity);
    Require(Loaded && Loaded->PassengerIDs.size() == 1 &&
            !EM.GetComponent<Game::Components::RenderComponent>(Passenger)->Visible,
        "loaded passenger is stored and hidden");
    const int SecondPassenger = CreateTerranUnit(EM, TransportMarineDef, 0);
    EM.GetComponent<Game::Components::TransformComponent>(SecondPassenger)->Position =
        Map.TileToWorld(19, 18);
    EM.AddComponent<Game::Components::RenderComponent>(SecondPassenger).Visible = true;
    Require(Game::Systems::TransportSystem::LoadUnit(
                EM, DropshipEntity, SecondPassenger),
        "Dropship accepts a second passenger for sequential unload regression");
    Hud.UpdateContext(EM, {DropshipEntity});
    const auto& UnloadButton = RequireButton(Hud, 7, "Unload");
    Require(UnloadButton.Enabled &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::Load) == 234 &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::UnloadAll) == 235 &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {DropshipEntity}, Hud, 7),
        "Dropship unload-all button is visible, icon-backed, and enabled while carrying units");
    const auto* PendingUnload =
        EM.GetComponent<Game::Components::PendingTransportUnloadComponent>(DropshipEntity);
    Require(PendingUnload && PendingUnload->Active &&
            Loaded->PassengerIDs.size() == 2,
        "unload-all starts a timed sequence instead of releasing every unit in one frame");
    Game::Systems::TransportSystem::UpdateOrders(EM, 0.0f);
    Require(Loaded->PassengerIDs.size() == 1,
        "sequential unload releases the first passenger on the next update");
    Game::Systems::TransportSystem::UpdateOrders(EM, 0.2f);
    Require(Loaded->PassengerIDs.size() == 1,
        "sequential unload keeps the next passenger aboard during the interval");
    Game::Systems::TransportSystem::UpdateOrders(EM, 0.2f);
    Require(Loaded->PassengerIDs.empty() &&
            EM.GetComponent<Game::Components::RenderComponent>(Passenger)->Visible &&
            EM.GetComponent<Game::Components::RenderComponent>(SecondPassenger)->Visible &&
            EM.GetComponent<Game::Components::UnitComponent>(Passenger)->Speed > 0.0f,
        "sequential unloading eventually restores passenger visibility and movement speed");

    const int Shuttle = CreateProtossUnit(
        EM, Game::Races::GetUnitDefID("Shuttle"), 0,
        Map.TileToWorld(20, 18));
    const int ShuttlePassenger = CreateTerranUnit(
        EM, TransportMarineDef, 0);
    EM.GetComponent<Game::Components::TransformComponent>(ShuttlePassenger)->Position =
        Map.TileToWorld(21, 18);
    Require(Game::Systems::TransportSystem::IsTransport(EM, Shuttle) &&
            Game::Systems::TransportSystem::CanLoad(
                EM, Shuttle, ShuttlePassenger),
        "Protoss Shuttle CHK 69 is recognized as a passenger transport");

    auto SavedTransportUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(0);
    auto& TransportUpgrades = Game::Systems::UpgradeSystem::GetUpgrades(0);
    TransportUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_VentralSacs)] = 0;
    const int TransportOverlord = CreateZergUnit(
        EM, Game::Races::GetUnitDefID("Overlord"), 0,
        Map.TileToWorld(22, 18));
    Require(!Game::Systems::TransportSystem::IsTransport(
                EM, TransportOverlord),
        "Overlord cannot transport units before Ventral Sacs");
    TransportUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_VentralSacs)] = 1;
    Require(Game::Systems::TransportSystem::IsTransport(
                EM, TransportOverlord),
        "Ventral Sacs enables Overlord CHK 42 passenger transport");
    Game::Systems::UpgradeSystem::GetUpgrades(0) = SavedTransportUpgrades;

    const int NydusEntrance = CreateCompletedRaceBuilding(
        EM, Game::Races::GetUnitDefID("Nydus Canal"), 0,
        Map.TileToWorld(24, 18));
    const int NydusExit = CreateCompletedRaceBuilding(
        EM, Game::Races::GetUnitDefID("Nydus Canal"), 0,
        Map.TileToWorld(28, 18));
    const int NydusPassenger = CreateZergUnit(
        EM, Game::Races::GetUnitDefID("Zergling"), 0,
        Map.TileToWorld(24, 18));
    Require(!Game::Systems::TransportSystem::IsNydusCanal(EM, MachineShop) &&
            Game::Systems::TransportSystem::IsNydusCanal(EM, NydusEntrance) &&
            Game::Systems::TransportSystem::EnterNydusCanal(
                EM, NydusPassenger, NydusEntrance),
        "Nydus uses CHK 134 and never aliases Machine Shop CHK 120");
    const auto* NydusPassengerTransform =
        EM.GetComponent<Game::Components::TransformComponent>(NydusPassenger);
    const auto* NydusExitTransform =
        EM.GetComponent<Game::Components::TransformComponent>(NydusExit);
    Require(NydusPassengerTransform && NydusExitTransform &&
            NydusPassengerTransform->Position.DistanceTo(
                Core::Math::Vec2(NydusExitTransform->Position.X + 24.0f,
                                 NydusExitTransform->Position.Y)) < 0.1f,
        "Nydus passenger exits beside the paired completed canal");

    auto& SiegePlayerUpgrades = Game::Systems::UpgradeSystem::GetUpgrades(0);
    SiegePlayerUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Terran_SiegeMode)] = 1;
    const int TankDef = Game::Races::GetUnitDefID("SiegeTank");
    const int Tank = CreateTerranUnit(EM, TankDef, 0);
    EM.AddComponent<Game::Components::CombatComponent>(Tank);
    auto& TankRender = EM.AddComponent<Game::Components::RenderComponent>(Tank);
    Game::Systems::CommandSystem::SetImageResolver(
        [](int ChkUnitID) { return static_cast<uint32_t>(1000 + ChkUnitID); });
    Game::Systems::CommandSystem::SetSubunitImageResolver(
        [](int ChkUnitID) { return static_cast<uint32_t>(2000 + ChkUnitID); });
    Hud.UpdateContext(EM, {Tank});
    const auto& SiegeButton = RequireButton(Hud, 6, "Siege");
    Require(SiegeButton.Enabled &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {Tank}, Hud, 6),
        "researched Siege Tank exposes and executes Siege Mode");
    auto* TankTransition =
        EM.GetComponent<Game::Components::SiegeTransitionComponent>(Tank);
    Require(TankTransition && TankTransition->Active && TankTransition->TargetSieged &&
            TankTransition->TargetImageID == 1030 &&
            TankTransition->TargetSubunitImageID == 2030 &&
            EM.GetComponent<Game::Components::UnitComponent>(Tank)->ChkUnitID == 5 &&
            EM.GetComponent<Game::Components::UnitComponent>(Tank)->Speed == 0.0f,
        "sieging queues the authored lowering animation before changing unit mode and weapon profile");
    EM.GetComponent<Game::Components::UnitComponent>(Tank)->ChkUnitID = 30;
    TankRender.ImageID = TankTransition->TargetImageID;
    TankRender.SubunitImageID = TankTransition->TargetSubunitImageID;
    *EM.GetComponent<Game::Components::CombatComponent>(Tank) = TankTransition->TargetCombat;
    TankTransition->Active = false;
    Hud.UpdateContext(EM, {Tank});
    const auto& UnsiegeButton = RequireButton(Hud, 6, "Unsiege");
    Require(UnsiegeButton.Enabled &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {Tank}, Hud, 6),
        "sieged tank exposes and executes Unsiege");
    Require(TankTransition->Active && !TankTransition->TargetSieged &&
            TankTransition->TargetImageID == 1005 &&
            TankTransition->TargetSubunitImageID == 2005 &&
            EM.GetComponent<Game::Components::UnitComponent>(Tank)->ChkUnitID == 30 &&
            EM.GetComponent<Game::Components::UnitComponent>(Tank)->Speed == 0.0f,
        "unsieging keeps the siege hull until authored SpecialState2 completes");

    Require(Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::Move) == 228 &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::Stop) == 229 &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::AttackMove) == 230 &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::Rally) == 286 &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::Scan) == 249,
        "common and building commands resolve to their canonical cmdicons.grp frames");

    Resources.Minerals = 5000;
    Resources.VespeneGas = 5000;
    const int SpawningPoolDef = Game::Races::GetUnitDefID("Spawning Pool");
    const int LairDef = Game::Races::GetUnitDefID("Lair");
    const int ZergHatchery = CreateCompletedRaceBuilding(
        EM, HatcheryDef, 0, Map.TileToWorld(4, 4));
    CreateCompletedRaceBuilding(EM, SpawningPoolDef, 0, Map.TileToWorld(8, 4));
    Hud.UpdateContext(EM, {ZergHatchery});
    RequireButton(Hud, 5, "Rally");
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {ZergHatchery}, Hud, 8),
        "Zerg Hatchery opens its building command page");
    Hud.UpdateContext(EM, {ZergHatchery});
    const auto& LairMorphButton = RequireButton(Hud, 3, "Lair");
    Require(LairMorphButton.Kind == Game::Client::UI::ActionKind::MorphBuilding &&
            LairMorphButton.Enabled &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {ZergHatchery}, Hud, 3),
        "Hatchery exposes and starts the resource-backed Lair morph command");
    const auto* HatcheryMorph =
        EM.GetComponent<Game::Components::MorphComponent>(ZergHatchery);
    Require(HatcheryMorph && HatcheryMorph->IsMorphing &&
            HatcheryMorph->MorphTargetDefID == LairDef,
        "Lair command dispatch records the building morph state");

    const int EvolutionDef = Game::Races::GetUnitDefID("Evolution Chamber");
    const int CreepColonyDef = Game::Races::GetUnitDefID("Creep Colony");
    const int SporeColonyDef = Game::Races::GetUnitDefID("Spore Colony");
    CreateCompletedRaceBuilding(EM, EvolutionDef, 0, Map.TileToWorld(12, 4));
    const int CreepColony = CreateCompletedRaceBuilding(
        EM, CreepColonyDef, 0, Map.TileToWorld(16, 4));
    Hud.UpdateContext(EM, {CreepColony});
    const auto& SporeMorphButton = RequireButton(Hud, 1, "Spore Colony");
    Require(SporeMorphButton.Enabled &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {CreepColony}, Hud, 1),
        "Creep Colony exposes and starts its Spore Colony morph command");
    Require(EM.GetComponent<Game::Components::MorphComponent>(CreepColony)->
                MorphTargetDefID == SporeColonyDef,
        "Spore Colony command dispatch uses the selected colony target definition");

    const int ProtossNexus = CreateCompletedRaceBuilding(
        EM, NexusDef, 0, Map.TileToWorld(4, 20));
    Hud.UpdateContext(EM, {ProtossNexus});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {ProtossNexus}, Hud, 5),
        "Protoss Nexus exposes the shared rally target command");

    const int ShieldBatteryDef = Game::Races::GetUnitDefID("Shield Battery");
    const int ZealotDef = Game::Races::GetUnitDefID("Zealot");
    const int ShieldBattery = CreateCompletedRaceBuilding(
        EM, ShieldBatteryDef, 0, Map.TileToWorld(20, 20));
    auto* BatteryUnit = EM.GetComponent<Game::Components::UnitComponent>(ShieldBattery);
    BatteryUnit->EnergyPoints = 100;
    BatteryUnit->MaxEnergyPoints = 200;
    const int ShieldTarget = CreateProtossUnit(
        EM, ZealotDef, 0, Map.TileToWorld(21, 20));
    auto* ShieldTargetUnit = EM.GetComponent<Game::Components::UnitComponent>(ShieldTarget);
    ShieldTargetUnit->ShieldPoints = 10;
    Hud.UpdateContext(EM, {ShieldBattery});
    const auto& RechargeButton = RequireButton(Hud, 0, "Recharge");
    Game::Client::GameLoop::ClickTargetContext RechargeTarget;
    RechargeTarget.AnyEntityID = ShieldTarget;
    Require(RechargeButton.Enabled &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(
                EM, {ShieldBattery}, Hud, 0) &&
            Game::Client::GameLoop::ExecutePendingTargetCommand(
                EM, {ShieldBattery}, Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(), RechargeTarget,
                Map.TileToWorld(21, 20)),
        "Shield Battery exposes and executes targeted shield recharge");
    Require(ShieldTargetUnit->ShieldPoints == ShieldTargetUnit->MaxShieldPoints &&
            BatteryUnit->EnergyPoints < 100,
        "Shield Battery command transfers stored energy into friendly shields");

    auto& AbilityUpgrades = Game::Systems::UpgradeSystem::GetUpgrades(0);
    AbilityUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_Ensnare)] = 1;
    const int QueenDef = Game::Races::GetUnitDefID("Queen");
    const int Queen = CreateZergUnit(EM, QueenDef, 0, Map.TileToWorld(24, 20));
    auto* QueenUnit = EM.GetComponent<Game::Components::UnitComponent>(Queen);
    QueenUnit->EnergyPoints = 200;
    const int EnsnareTarget = CreateTerranUnit(
        EM, Game::Races::GetUnitDefID("Marine"), 1);
    EM.GetComponent<Game::Components::TransformComponent>(EnsnareTarget)->Position =
        Map.TileToWorld(25, 20);
    Hud.UpdateContext(EM, {Queen});
    const auto& EnsnareButton = RequireButton(Hud, 7, "Ensnare");
    Game::Client::GameLoop::ClickTargetContext EnsnareClick;
    EnsnareClick.AnyEntityID = EnsnareTarget;
    Require(EnsnareButton.Enabled &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::Ensnare) >= 0 &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {Queen}, Hud, 7) &&
            Game::Client::GameLoop::ExecutePendingTargetCommand(
                EM, {Queen}, Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(), EnsnareClick,
                Map.TileToWorld(25, 20)),
        "Queen exposes an icon-backed Ensnare command and casts it from the embedded HUD");
    const auto* Ensnared =
        EM.GetComponent<Game::Components::StatusEffectComponent>(EnsnareTarget);
    Require(Ensnared && Ensnared->HasEffect(Game::Components::StatusEffect::Ensnare),
        "Ensnare HUD targeting reaches the gameplay spell system");

    const auto& InfestButton = RequireButton(Hud, 5, "Infest");
    Require(InfestButton.Enabled &&
            Game::Client::UI::CommandIconForCommand(
                Game::Client::UI::HudCommandId::Infest) >= 0 &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {Queen}, Hud, 5) &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Infest,
        "Queen exposes an icon-backed Infest command and enters Infest targeting mode");

    AbilityUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_PsionicStorm)] = 1;
    const int TemplarDef = Game::Races::GetUnitDefID("High Templar");
    const int Templar = CreateProtossUnit(
        EM, TemplarDef, 0, Map.TileToWorld(26, 20));
    auto* TemplarUnit = EM.GetComponent<Game::Components::UnitComponent>(Templar);
    TemplarUnit->EnergyPoints = 200;
    const int StormTarget = CreateTerranUnit(
        EM, Game::Races::GetUnitDefID("Marine"), 1);
    EM.GetComponent<Game::Components::TransformComponent>(StormTarget)->Position =
        Map.TileToWorld(27, 20);
    const int StormHpBefore =
        EM.GetComponent<Game::Components::UnitComponent>(StormTarget)->HitPoints;
    Hud.UpdateContext(EM, {Templar});
    const auto& StormButton = RequireButton(Hud, 6, "Storm");
    Game::Client::GameLoop::ClickTargetContext StormClick;
    StormClick.AnyEntityID = StormTarget;
    Require(StormButton.Enabled &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {Templar}, Hud, 6) &&
            Game::Client::GameLoop::ExecutePendingTargetCommand(
                EM, {Templar}, Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(), StormClick,
                Map.TileToWorld(27, 20)),
        "High Templar exposes and casts Psionic Storm from the embedded HUD");
    Require(EM.GetComponent<Game::Components::UnitComponent>(StormTarget)->HitPoints <
                StormHpBefore,
        "Psionic Storm HUD targeting applies spell damage");

    const int LurkerDef = Game::Races::GetUnitDefID("Lurker");
    const int Lurker = CreateZergUnit(EM, LurkerDef, 0, Map.TileToWorld(28, 20));
    auto& LurkerBurrow = EM.AddComponent<Game::Components::BurrowComponent>(Lurker);
    LurkerBurrow.CanBurrow = true;
    Hud.UpdateContext(EM, {Lurker});
    const auto& BurrowButton = RequireButton(Hud, 8, "Burrow");
    Require(BurrowButton.Enabled &&
            Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {Lurker}, Hud, 8) &&
            LurkerBurrow.Phase == Game::Components::BurrowPhase::Burrowing,
        "Lurker's innate Burrow command executes directly from the embedded HUD");

    // Test keyboard shortcut hotkey resolution and dispatch
    Hud.UpdateContext(EM, {MarineEntity});
    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {MarineEntity}, Hud, 'A'),
        "hotkey 'A' triggers attack command on selected marine");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::AttackMove,
        "hotkey 'A' activates attack-move targeting mode");
    Hud.CancelPendingTarget();

    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {MarineEntity}, Hud, 'H'),
        "hotkey 'H' triggers hold command on selected marine");
    auto* HotkeyMarineCommand = EM.GetComponent<Game::Components::CommandComponent>(MarineEntity);
    Require(HotkeyMarineCommand && HotkeyMarineCommand->CurrentCommand == Game::Components::CommandType::HoldPosition,
        "hotkey 'H' sets hold position command");

    // SCV Basic Build hotkey 'B' -> PageTerranScvBuild1 -> Barracks hotkey 'B'
    const int HotkeyScv = CreateTerranUnit(EM, ScvDef, 0);
    CreateTerranUnit(EM, Game::Races::GetUnitDefID("SupplyDepot"), 0);
    Hud.UpdateContext(EM, {HotkeyScv});
    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {HotkeyScv}, Hud, 'B'),
        "hotkey 'B' opens Basic Build submenu on selected SCV");
    Hud.UpdateContext(EM, {HotkeyScv});
    Require(Hud.GetState().CommandPanel.PanelPage == Game::Client::UI::PageTerranScvBuild1,
        "HUD command panel transitions to Basic Build page");
    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {HotkeyScv}, Hud, 'B'),
        "hotkey 'B' in Basic Build submenu selects Barracks");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::BuildPlacement &&
            Hud.GetPendingBuildDefID() == Game::Races::GetUnitDefID("Barracks"),
        "hotkey 'B' in Basic Build submenu activates Barracks placement targeting");
    Hud.CancelPendingTarget();

    // Command Center Train SCV hotkey 'S'
    Hud.UpdateContext(EM, {CommandCenter});
    const size_t ScvCountBefore = EM.GetComponent<Game::Components::ProductionComponent>(CommandCenter)->Queue.size();
    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {CommandCenter}, Hud, 'S'),
        "hotkey 'S' triggers Train SCV on Command Center");
    Require(EM.GetComponent<Game::Components::ProductionComponent>(CommandCenter)->Queue.size() == ScvCountBefore + 1,
        "hotkey 'S' queues an SCV in Command Center production queue");

    // ==========================================
    // Science Vessel, Medic & Dark Archon Spell Card Tests
    // ==========================================
    const int VesselDef = Game::Races::GetUnitDefID("ScienceVessel");
    const int VesselEntity = CreateTerranUnit(EM, VesselDef, 0);
    auto* VesselUnit = EM.GetComponent<Game::Components::UnitComponent>(VesselEntity);
    VesselUnit->EnergyPoints = 200;
    VesselUnit->MaxEnergyPoints = 250;
    Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
        static_cast<size_t>(Game::Systems::ResearchId::Terran_EMPShockwave)] = 1;
    Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
        static_cast<size_t>(Game::Systems::ResearchId::Terran_Irradiate)] = 1;

    Hud.UpdateContext(EM, {VesselEntity});
    const auto& VesselButtons = Hud.GetState().CommandPanel.ButtonDefs;
    Require(VesselButtons[6].Label == "Matrix" &&
            VesselButtons[6].CommandId == Game::Client::UI::HudCommandId::DefensiveMatrix &&
            VesselButtons[6].Enabled,
        "Science Vessel exposes Defensive Matrix spell button enabled with energy");
    Require(VesselButtons[7].Label == "EMP" &&
            VesselButtons[7].CommandId == Game::Client::UI::HudCommandId::EMPShockwave &&
            VesselButtons[7].Enabled,
        "Science Vessel exposes EMP Shockwave spell button enabled with energy");
    Require(VesselButtons[8].Label == "Irradiate" &&
            VesselButtons[8].CommandId == Game::Client::UI::HudCommandId::Irradiate &&
            VesselButtons[8].Enabled,
        "Science Vessel exposes Irradiate spell button enabled with energy");

    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {VesselEntity}, Hud, 6),
        "clicking Defensive Matrix button activates targeting");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::DefensiveMatrix,
        "Defensive Matrix sets pending target mode");
    Hud.CancelPendingTarget();

    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {VesselEntity}, Hud, 'I'),
        "hotkey 'I' triggers Irradiate targeting on Science Vessel");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Irradiate,
        "Irradiate hotkey sets pending target mode");
    Hud.CancelPendingTarget();

    // Medic: Restoration & Optical Flare
    const int TestMedicEntity = CreateTerranUnit(EM, Game::Races::GetUnitDefID("Medic"), 0);
    auto* MedicUnit = EM.GetComponent<Game::Components::UnitComponent>(TestMedicEntity);
    MedicUnit->EnergyPoints = 200;
    MedicUnit->MaxEnergyPoints = 250;
    Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
        static_cast<size_t>(Game::Systems::ResearchId::Terran_Restoration)] = 1;
    Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
        static_cast<size_t>(Game::Systems::ResearchId::Terran_OpticalFlare)] = 1;

    Hud.UpdateContext(EM, {TestMedicEntity});
    const auto& MedicButtons = Hud.GetState().CommandPanel.ButtonDefs;
    Require(MedicButtons[7].Label == "Restoration" &&
            MedicButtons[7].CommandId == Game::Client::UI::HudCommandId::Restoration &&
            MedicButtons[7].Enabled,
        "Medic exposes Restoration spell button enabled with energy");
    Require(MedicButtons[8].Label == "Opt Flare" &&
            MedicButtons[8].CommandId == Game::Client::UI::HudCommandId::OpticalFlare &&
            MedicButtons[8].Enabled,
        "Medic exposes Optical Flare spell button enabled with energy");

    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {TestMedicEntity}, Hud, 'R'),
        "hotkey 'R' triggers Restoration targeting on Medic");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Restoration,
        "Restoration hotkey sets pending target mode");
    Hud.CancelPendingTarget();

    // Dark Archon: Feedback, Maelstrom, Mind Control
    const int DarkArchonDef = Game::Races::GetUnitDefID("Dark Archon");
    const auto DarkArchonData = Game::Races::GetUnitDefByID(DarkArchonDef);
    const int DarkArchonEntity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(DarkArchonEntity).Position = {64.0f, 64.0f};
    auto& DaUnit = EM.AddComponent<Game::Components::UnitComponent>(DarkArchonEntity);
    DaUnit.ChkUnitID = DarkArchonData.ChkUnitID;
    DaUnit.UnitRace = Game::Components::Race::Protoss;
    DaUnit.HitPoints = 25;
    DaUnit.MaxHitPoints = 25;
    DaUnit.ShieldPoints = 200;
    DaUnit.MaxShieldPoints = 200;
    DaUnit.EnergyPoints = 200;
    DaUnit.MaxEnergyPoints = 200;
    EM.AddComponent<Game::Components::PlayerComponent>(DarkArchonEntity).PlayerID = 0;

    Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
        static_cast<size_t>(Game::Systems::ResearchId::Protoss_Maelstrom)] = 1;
    Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
        static_cast<size_t>(Game::Systems::ResearchId::Protoss_MindControl)] = 1;

    Hud.UpdateContext(EM, {DarkArchonEntity});
    const auto& DaButtons = Hud.GetState().CommandPanel.ButtonDefs;
    Require(DaButtons[6].Label == "Feedback" &&
            DaButtons[6].CommandId == Game::Client::UI::HudCommandId::Feedback &&
            DaButtons[6].Enabled,
        "Dark Archon exposes Feedback spell button enabled");
    Require(DaButtons[7].Label == "Maelstrom" &&
            DaButtons[7].CommandId == Game::Client::UI::HudCommandId::Maelstrom &&
            DaButtons[7].Enabled,
        "Dark Archon exposes Maelstrom spell button enabled when researched");
    Require(DaButtons[8].Label == "Mind Control" &&
            DaButtons[8].CommandId == Game::Client::UI::HudCommandId::MindControl &&
            DaButtons[8].Enabled,
        "Dark Archon exposes Mind Control spell button enabled when researched");

    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {DarkArchonEntity}, Hud, 'F'),
        "hotkey 'F' triggers Feedback targeting on Dark Archon");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Feedback,
        "Feedback hotkey sets pending target mode");
    Hud.CancelPendingTarget();

    // Test SCV Repair button, hotkey 'R', and targeting dispatch
    Hud.UpdateContext(EM, {HotkeyScv});
    const auto& ScvButtons = Hud.GetState().CommandPanel.ButtonDefs;
    Require(ScvButtons[3].Label == "Repair" &&
            ScvButtons[3].CommandId == Game::Client::UI::HudCommandId::Repair &&
            ScvButtons[3].Enabled,
        "SCV exposes Repair button enabled in slot 3");

    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {HotkeyScv}, Hud, 'R'),
        "hotkey 'R' activates Repair targeting on SCV");
    Require(Hud.HasPendingTarget() &&
            Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Repair,
        "Repair hotkey sets pending target mode to Repair");

    // Create a damaged siege tank to repair
    const int DamagedTank = CreateTerranUnit(EM, Game::Races::GetUnitDefID("SiegeTank"), 0);
    auto* TankUnit = EM.GetComponent<Game::Components::UnitComponent>(DamagedTank);
    TankUnit->HitPoints = 50;
    TankUnit->MaxHitPoints = 150;

    Game::Client::GameLoop::ClickTargetContext RepairClick;
    RepairClick.AnyEntityID = DamagedTank;
    const bool RepairDispatched = Game::Client::GameLoop::ExecutePendingTargetCommand(
        EM, {HotkeyScv}, Hud.GetPendingTargetMode(), -1, RepairClick,
        EM.GetComponent<Game::Components::TransformComponent>(DamagedTank)->Position);
    Require(RepairDispatched, "Repair command successfully dispatched targeting damaged mechanical unit");
    const auto* ScvCmd = EM.GetComponent<Game::Components::CommandComponent>(HotkeyScv);
    Require(ScvCmd && ScvCmd->CurrentCommand == Game::Components::CommandType::Repair &&
            ScvCmd->TargetEntityID == DamagedTank,
        "SCV command set to Repair target tank");
    Hud.CancelPendingTarget();

    // Test Wraith Cloak/Decloak dynamic button and hotkey 'C'
    const int WraithEntity = CreateTerranUnit(EM, Game::Races::GetUnitDefID("Wraith"), 0);
    auto* WraithUnit = EM.GetComponent<Game::Components::UnitComponent>(WraithEntity);
    WraithUnit->EnergyPoints = 100;
    WraithUnit->MaxEnergyPoints = 200;
    Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
        static_cast<size_t>(Game::Systems::ResearchId::Terran_CloakingField)] = 1;

    Hud.UpdateContext(EM, {WraithEntity});
    const auto& WraithButtons = Hud.GetState().CommandPanel.ButtonDefs;
    Require(WraithButtons[6].Label == "Cloak" && WraithButtons[6].Enabled,
        "Wraith exposes Cloak button enabled when researched and has energy");

    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {WraithEntity}, Hud, 'C'),
        "hotkey 'C' triggers Cloak tech command on Wraith");
    auto* WraithCloak = EM.GetComponent<Game::Components::CloakComponent>(WraithEntity);
    Require(WraithCloak && WraithCloak->IsCloaked && WraithUnit->EnergyPoints == 75,
        "Wraith activates Cloak and spends 25 upfront energy");

    Hud.UpdateContext(EM, {WraithEntity});
    const auto& DecloakButtons = Hud.GetState().CommandPanel.ButtonDefs;
    Require(DecloakButtons[6].Label == "Decloak" && DecloakButtons[6].Enabled,
        "Wraith command card updates button to Decloak while cloaked");

    Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {WraithEntity}, Hud, 'C'),
        "hotkey 'C' triggers Decloak on cloaked Wraith");
    Require(!WraithCloak->IsCloaked, "Wraith decloaks successfully");

    // Test CovertOps exposes Personal Cloaking research button in slot 2
    const int CovertOpsEntity = CreateTerranUnit(EM, Game::Races::GetUnitDefID("CovertOps"), 0);
    Hud.UpdateContext(EM, {CovertOpsEntity});
    const auto& CovertButtons = Hud.GetState().CommandPanel.ButtonDefs;
    Require(CovertButtons[2].Label == "Cloak" &&
            CovertButtons[2].ResearchId == Game::Systems::TerranResearchId::PersonalCloaking,
        "Covert Ops exposes Personal Cloaking research in slot 2");

    // Test Medic Heal command button and hotkey 'A'
    {
        const int TestMedic = CreateTerranUnit(EM, Game::Races::GetUnitDefID("Medic"), 0);
        auto* TestMedicUnit = EM.GetComponent<Game::Components::UnitComponent>(TestMedic);
        TestMedicUnit->EnergyPoints = 50;
        TestMedicUnit->MaxEnergyPoints = 200;

        Hud.UpdateContext(EM, {TestMedic});
        const auto& Buttons = Hud.GetState().CommandPanel.ButtonDefs;
        Require(Buttons[6].Label == "Heal" && Buttons[6].Enabled,
            "Medic exposes Heal button enabled when has energy");

        Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {TestMedic}, Hud, 'A'),
            "hotkey 'A' triggers Heal pending target on Medic");
        Require(Hud.HasPendingTarget() &&
                Hud.GetPendingTargetMode() == Game::Client::UI::PendingTargetMode::Heal,
            "Medic hotkey 'A' activates PendingTargetMode::Heal");

        const int DamagedMarineTarget = CreateTerranUnit(EM, Game::Races::GetUnitDefID("Marine"), 0);
        auto* TargetMarineUnit = EM.GetComponent<Game::Components::UnitComponent>(DamagedMarineTarget);
        TargetMarineUnit->HitPoints = 20;
        TargetMarineUnit->MaxHitPoints = 40;

        Game::Client::GameLoop::ClickTargetContext HealClick;
        HealClick.AnyEntityID = DamagedMarineTarget;
        const bool HealDispatched = Game::Client::GameLoop::ExecutePendingTargetCommand(
            EM, {TestMedic}, Hud.GetPendingTargetMode(), -1, HealClick,
            EM.GetComponent<Game::Components::TransformComponent>(DamagedMarineTarget)->Position);
        Require(HealDispatched, "Heal command successfully dispatched targeting damaged organic unit");
        const auto* MedicCmd = EM.GetComponent<Game::Components::CommandComponent>(TestMedic);
        Require(MedicCmd && MedicCmd->CurrentCommand == Game::Components::CommandType::Heal &&
                MedicCmd->TargetEntityID == DamagedMarineTarget,
            "Medic command set to Heal target marine");
        Hud.CancelPendingTarget();
    }

    // Test High Templar Archon warp button and hotkey 'C'
    {
        const int TemplarA = CreateProtossUnit(EM, Game::Races::GetUnitDefID("High Templar"), 0, {100.0f, 100.0f});
        const int TemplarB = CreateProtossUnit(EM, Game::Races::GetUnitDefID("High Templar"), 0, {120.0f, 100.0f});

        Hud.UpdateContext(EM, {TemplarA, TemplarB});
        const auto& Buttons = Hud.GetState().CommandPanel.ButtonDefs;
        Require(Buttons[8].Label == "Archon" && Buttons[8].Enabled,
            "High Templar exposes Archon warp button enabled in slot 8");

        Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {TemplarA, TemplarB}, Hud, 'C'),
            "hotkey 'C' triggers Archon warp on selected High Templars");

        // Verify that TemplarA and TemplarB were morphed (consumed)
        Require(!EM.IsAlive(TemplarA) && !EM.IsAlive(TemplarB),
            "source Templars are consumed upon morphing into Archon");
    }

    // Test Hydralisk Lurker morph button and hotkey 'L'
    {
        const int Hydra = CreateZergUnit(EM, Game::Races::GetUnitDefID("Hydralisk"), 0, {200.0f, 200.0f});
        Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
            static_cast<size_t>(Game::Systems::ResearchId::Zerg_LurkerMorph)] = 0;

        Hud.UpdateContext(EM, {Hydra});
        const auto& ButtonsBefore = Hud.GetState().CommandPanel.ButtonDefs;
        Require(ButtonsBefore[6].Label == "Lurker" && !ButtonsBefore[6].Enabled,
            "Hydralisk Lurker button is disabled before Lurker Aspect research");

        Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
            static_cast<size_t>(Game::Systems::ResearchId::Zerg_LurkerMorph)] = 1;
        auto& Res = Game::Systems::HarvestSystem::GetResources(0);
        Res.Minerals = 500;
        Res.VespeneGas = 500;

        Hud.UpdateContext(EM, {Hydra});
        const auto& ButtonsAfter = Hud.GetState().CommandPanel.ButtonDefs;
        Require(ButtonsAfter[6].Label == "Lurker" && ButtonsAfter[6].Enabled,
            "Hydralisk Lurker button is enabled after research and sufficient resources");

        Require(Game::Client::GameLoop::TryDispatchCommandHotkey(EM, {Hydra}, Hud, 'L'),
            "hotkey 'L' triggers Lurker morph on Hydralisk");

        // Verify Hydra is consumed and Lurker Egg is created
        Require(!EM.IsAlive(Hydra), "Hydralisk is consumed upon morphing");
        int FoundEgg = -1;
        for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent>())
        {
            auto* U = EM.GetComponent<Game::Components::UnitComponent>(Eid);
            if (U && U->ChkUnitID == 97)
            {
                FoundEgg = Eid;
                break;
            }
        }
        Require(FoundEgg > 0, "Lurker Egg entity (ChkUnitID 97) is created");
    }

    std::printf("HudCommandMetadataTest passed\n");
    return 0;
}
