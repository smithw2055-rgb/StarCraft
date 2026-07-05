#include "UI/Hud.h"

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Main/GameCommandDispatchRuntime.h"
#include "Main/GameLoop/CursorTypes.h"
#include "UI/MainGuiSelectionPresentation.h"

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
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;

        if (Def.IsBuilding)
        {
            auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
            Building.IsBuilding = true;
            Building.BuildingDefID = DefID;
            Building.UnderConstruction = false;
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
    Require(CountBuildingsByDef(EM, SupplyDepotDef) == BuildingsBeforePlacement + 1,
        "world placement click creates an under-construction Supply Depot entity");
    Require(Resources.Minerals == MineralsBeforePlacement - SupplyDepot.MineralCost,
        "world placement click spends the building mineral cost");
    auto* ScvCommand = EM.GetComponent<Game::Components::CommandComponent>(ScvEntity);
    Require(ScvCommand && ScvCommand->CurrentCommand == Game::Components::CommandType::Move,
        "world placement click sends the worker toward the building site");
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
    auto& MarineCombat = EM.AddComponent<Game::Components::CombatComponent>(MarineEntity);
    MarineCombat.GroundDamage = 6;
    MarineCombat.CanTargetGround = true;
    MarineCombat.CanTargetAir = false;

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
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {DroneEntity}, Hud, 0),
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
        "Zerg valid building placement starts a real Drone morph");
    Require(!EM.IsAlive(DroneEntity),
        "Zerg valid building placement consumes the Drone");
    Require(Resources.Minerals == ZergMineralsBefore - Hatchery.MineralCost,
        "Zerg valid building placement spends the building mineral cost");
    Require(CountBuildingsByDef(EM, HatcheryDef) == ZergBuildingsBefore + 1,
        "Zerg valid building placement creates an under-construction Hatchery");

    Resources.Minerals = 500;
    const int ExtractorDef = Game::Races::GetUnitDefID("Extractor");
    const auto Extractor = Game::Races::GetUnitDefByID(ExtractorDef);
    const int ExtractorDrone = CreateZergUnit(EM, DroneDef, 0, Map.TileToWorld(26, 20));
    Hud.UpdateContext(EM, {ExtractorDrone});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ExtractorDrone}, Hud, 0),
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
    Require(!EM.IsAlive(ExtractorDrone),
        "Zerg Extractor target click consumes the Drone");
    Require(Resources.Minerals == ExtractorMineralsBefore - Extractor.MineralCost,
        "Zerg Extractor target click spends the mineral cost");
    auto* ExtractorBuilding = EM.GetComponent<Game::Components::BuildingComponent>(ZergGeyser);
    Require(ExtractorBuilding &&
            ExtractorBuilding->BuildingDefID == ExtractorDef &&
            ExtractorBuilding->UnderConstruction,
        "Zerg Extractor target click converts the geyser into an under-construction Extractor");

    Resources.Minerals = 500;
    const int ProbeDef = Game::Races::GetUnitDefID("Probe");
    const int AssimilatorDef = Game::Races::GetUnitDefID("Assimilator");
    const auto Assimilator = Game::Races::GetUnitDefByID(AssimilatorDef);
    const int ProbeEntity = CreateProtossUnit(EM, ProbeDef, 0, Map.TileToWorld(28, 20));
    Hud.UpdateContext(EM, {ProbeEntity});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {ProbeEntity}, Hud, 0),
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
    Require(AssimilatorBuilding &&
            AssimilatorBuilding->BuildingDefID == AssimilatorDef &&
            AssimilatorBuilding->UnderConstruction,
        "Protoss Assimilator target click converts the geyser into an under-construction Assimilator");

    Resources.Minerals = 900;
    const int NexusDef = Game::Races::GetUnitDefID("Nexus");
    const int PylonDef = Game::Races::GetUnitDefID("Pylon");
    const int GatewayDef = Game::Races::GetUnitDefID("Gateway");
    const auto Gateway = Game::Races::GetUnitDefByID(GatewayDef);
    Require(Gateway.RequiresPylon,
        "Gateway definition requires Pylon power");
    Require(!Game::Races::GetUnitDefByID(NexusDef).RequiresPylon &&
            !Game::Races::GetUnitDefByID(PylonDef).RequiresPylon,
        "Nexus and Pylon can be placed without existing Pylon power");
    const int NexusEntity = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, NexusDef, Map.TileToWorld(4, 24), 0);
    Require(NexusEntity > 0,
        "completed Nexus satisfies Gateway prerequisite");
    const int GatewayProbe = CreateProtossUnit(EM, ProbeDef, 0, Map.TileToWorld(22, 24));
    const Core::Math::Vec2 GatewayPlacement = Map.TileToWorld(24, 24);
    Hud.UpdateContext(EM, {GatewayProbe});
    Require(Game::Client::GameLoop::ExecuteHudCommandIndex(EM, {GatewayProbe}, Hud, 0),
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
    int GatewayEntity = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        if (Building && Building->BuildingDefID == GatewayDef && Building->UnderConstruction)
        {
            GatewayEntity = EntityID;
            break;
        }
    }
    auto* GatewayBuilding = GatewayEntity > 0
        ? EM.GetComponent<Game::Components::BuildingComponent>(GatewayEntity)
        : nullptr;
    Require(GatewayBuilding &&
            GatewayBuilding->RequiresPylon &&
            GatewayBuilding->HasPylonPower,
        "new Protoss Gateway records required and available Pylon power");

    std::printf("HudCommandMetadataTest passed\n");
    return 0;
}
