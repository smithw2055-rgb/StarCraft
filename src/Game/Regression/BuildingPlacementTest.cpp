#include "Game/Systems/BuildingSystem.h"
#include "Client/EntityRenderer.h"

#include "Core/ECS/SystemManager.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                              const std::vector<Core::ECS::EntityID>& Units,
                              const Core::Math::Vec2& Target)
{
    IssueMove(EM, Units, Target, -1);
}

void CommandSystem::IssueMove(Core::ECS::EntityManager&,
                              const std::vector<Core::ECS::EntityID>&,
                              const Core::Math::Vec2&,
                              int)
{
}

bool CommandSystem::IssueMoveAndHasPath(Core::ECS::EntityManager& EM,
                                        Core::ECS::EntityID EntityID,
                                        const Core::Math::Vec2& Target,
                                        int PathingIgnoreEntityID)
{
    IssueMove(EM, {EntityID}, Target, PathingIgnoreEntityID);
    auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
    if (!Path)
        Path = &EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
    Path->Path = {Target};
    Path->PathIndex = 0;
    Path->RequestedDestination = Target;
    Path->RequestedIgnoreEntityID = PathingIgnoreEntityID;
    return true;
}

void CommandSystem::InterruptHarvest(Core::ECS::EntityManager& EM,
                                     Core::ECS::EntityID EntityID)
{
    auto* Harvest = EM.GetComponent<Game::Components::HarvestComponent>(EntityID);
    if (!Harvest)
        return;
    if (auto* Resource =
            EM.GetComponent<Game::Components::ResourceComponent>(Harvest->TargetResourceID))
    {
        if (Resource->ActiveGathererEntityID == EntityID)
            Resource->ActiveGathererEntityID = -1;
        Resource->QueuedGatherers.erase(
            std::remove(Resource->QueuedGatherers.begin(),
                        Resource->QueuedGatherers.end(), EntityID),
            Resource->QueuedGatherers.end());
    }
    Harvest->State = Game::Components::HarvestState::Idle;
    Harvest->TargetResourceID = -1;
    Harvest->DesiredResourceID = -1;
    Harvest->ReturnBuildingID = -1;
    Harvest->QueuedAtResource = false;
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
                         const char* DefName,
                         int PlayerID,
                         Core::Math::Vec2 Position)
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Game::Components::Race::Terran;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.SupplyCost = Def.SupplyCost;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Command = EM.AddComponent<Game::Components::CommandComponent>(EntityID);
        Command.CurrentCommand = Game::Components::CommandType::Build;
        return EntityID;
    }

    int CreateProtossProbe(Core::ECS::EntityManager& EM,
                           int PlayerID,
                           Core::Math::Vec2 Position)
    {
        const int DefID = Game::Races::GetUnitDefID("Probe");
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        EM.AddComponent<Game::Components::CommandComponent>(EntityID).CurrentCommand =
            Game::Components::CommandType::Stop;
        return EntityID;
    }
}

int main()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();

    Game::Map::GameMap Map;
    Game::Systems::BuildingSystem::SetMap(&Map);
    Game::Systems::BuildingSystem::SetImageResolver(
        [](int ChkUnitID) { return (uint32_t)(10000 + ChkUnitID); });
    Game::Systems::BuildingSystem::SetSubunitImageResolver(
        [](int ChkUnitID) { return (uint32_t)(30000 + ChkUnitID); });
    Game::Systems::BuildingSystem::SetConstructionImageResolver(
        [](int ChkUnitID) { return (uint32_t)(20000 + ChkUnitID); });

    constexpr int PlayerID = 0;
    auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerID);
    Resources.Minerals = 500;
    Resources.VespeneGas = 0;
    Resources.SupplyUsed = 8;
    Resources.SupplyMax = 10;

    const int SupplyDepotDefID = Game::Races::GetUnitDefID("SupplyDepot");
    const int ExistingDepot = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, SupplyDepotDefID, Map.TileToWorld(6, 6), 0);
    Require(ExistingDepot > 0, "existing supply depot can be spawned");

    Require(Game::Systems::BuildingSystem::IsPlacementValid(
                EM, SupplyDepotDefID, Map.TileToWorld(9, 6), 0),
            "adjacent 3x2 supply depots are allowed by footprint placement");

    Require(!Game::Systems::BuildingSystem::IsPlacementValid(
                EM, SupplyDepotDefID, Map.TileToWorld(8, 6), 0),
            "overlapping supply depot footprints are rejected");

    const int PlacementCommandCenterDefID = Game::Races::GetUnitDefID("CommandCenter");
    Require(!Game::Systems::BuildingSystem::IsPlacementValid(
                EM, PlacementCommandCenterDefID, Map.TileToWorld(8, 6), 0),
            "large building footprint is rejected when it overlaps an existing building");

    const int NexusDefID = Game::Races::GetUnitDefID("Nexus");
    const auto NexusDef = Game::Races::GetUnitDefByID(NexusDefID);
    const int Nexus = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, NexusDefID, Map.TileToWorld(14, 6), 0);
    auto* NexusUnit = EM.GetComponent<Game::Components::UnitComponent>(Nexus);
    Require(NexusUnit != nullptr, "debug-spawned Nexus has unit data");
    Require(NexusUnit->MaxShieldPoints == NexusDef.ShieldPoints,
        "completed Protoss building keeps max shields from its unit definition");
    Require(NexusUnit->ShieldPoints == NexusDef.ShieldPoints,
        "completed Protoss building starts with full shields for UI and combat");

    constexpr int ProtossBuilderPlayerID = 4;
    auto& ProtossResources =
        Game::Systems::HarvestSystem::GetResources(ProtossBuilderPlayerID);
    ProtossResources.Minerals = 1000;
    ProtossResources.VespeneGas = 1000;
    const Core::Math::Vec2 ProbeNexusPos = Map.TileToWorld(80, 40);
    const int Probe = CreateProtossProbe(EM, ProtossBuilderPlayerID, ProbeNexusPos);
    Require(Game::Systems::BuildingSystem::StartBuild(
                EM, Probe, NexusDefID, ProbeNexusPos),
        "Probe can accept a Nexus warp-in order at valid placement");
    Require(Game::Systems::BuildingSystem::CountPendingBuilds(
                ProtossBuilderPlayerID, NexusDefID) == 1,
        "Protoss build order remains pending until the Probe reaches the site");
    int PendingNexus = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        const auto* Building =
            EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (Building && Player && Player->PlayerID == ProtossBuilderPlayerID &&
            Building->BuildingDefID == NexusDefID)
            PendingNexus = EntityID;
    }
    Require(PendingNexus < 0,
        "Protoss building entity is not created before Probe arrival");

    Game::Systems::BuildingSystem ProtossBuildingSystem(SysMgr);
    ProtossBuildingSystem.Update(0.001f);
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        const auto* Building =
            EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (Building && Player && Player->PlayerID == ProtossBuilderPlayerID &&
            Building->BuildingDefID == NexusDefID)
            PendingNexus = EntityID;
    }
    auto* PendingNexusBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(PendingNexus);
    auto* PendingNexusRender =
        EM.GetComponent<Game::Components::RenderComponent>(PendingNexus);
    auto* ProbeCommand = EM.GetComponent<Game::Components::CommandComponent>(Probe);
    Require(PendingNexus > 0 && PendingNexusBuilding &&
            PendingNexusBuilding->UnderConstruction &&
            PendingNexusBuilding->BuilderUnitID == -1,
        "Probe arrival creates an autonomous Protoss warp-in entity");
    Require(PendingNexusRender && PendingNexusRender->ImageID == 211,
        "Probe arrival starts the Protoss warp-anchor visual");
    Require(ProbeCommand &&
            ProbeCommand->CurrentCommand == Game::Components::CommandType::Stop,
        "Probe is released immediately after opening the warp-in rift");

    const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    const int Hatchery = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, HatcheryDefID, Map.TileToWorld(18, 12), PlayerID);
    auto* HatcheryBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(Hatchery);
    Require(HatcheryBuilding && HatcheryBuilding->IsOrganic,
        "completed Zerg Hatchery is organic so the live Zerg system can spawn larva");
    EM.DestroyEntity(Hatchery);

    const int SpawningPoolDefID = Game::Races::GetUnitDefID("Spawning Pool");
    const Core::Math::Vec2 CreepBuildPos = Map.TileToWorld(25, 12);
    Require(!Game::Systems::BuildingSystem::IsPlacementValid(
                EM, SpawningPoolDefID, CreepBuildPos, PlayerID),
            "Zerg tech building is rejected when its footprint is off creep");
    for (int Y = 11; Y <= 12; ++Y)
    {
        for (int X = 24; X <= 26; ++X)
        {
            auto Tile = Map.GetTile(X, Y);
            Tile.HasCreep = true;
            Map.SetTile(X, Y, Tile);
        }
    }
    Require(Game::Systems::BuildingSystem::IsPlacementValid(
                EM, SpawningPoolDefID, CreepBuildPos, PlayerID),
            "Zerg tech building is allowed when its entire footprint has creep");
    auto MissingCreepTile = Map.GetTile(26, 12);
    MissingCreepTile.HasCreep = false;
    Map.SetTile(26, 12, MissingCreepTile);
    Require(!Game::Systems::BuildingSystem::IsPlacementValid(
                EM, SpawningPoolDefID, CreepBuildPos, PlayerID),
            "partial creep coverage does not satisfy a Zerg building footprint");

    const int CommandCenterDefID = Game::Races::GetUnitDefID("CommandCenter");
    const int CommandCenter = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, CommandCenterDefID, Map.TileToWorld(20, 6), PlayerID);
    Require(CommandCenter > 0, "completed Command Center satisfies Supply Depot prerequisite");

    const Core::Math::Vec2 OccupiedDepotPos = Map.TileToWorld(10, 12);
    const int BlockingScv = CreateTerranUnit(EM, "SCV", PlayerID, OccupiedDepotPos);
    Require(BlockingScv > 0, "ground unit can be created for occupied placement coverage");
    Require(!Game::Systems::BuildingSystem::IsPlacementValid(
                EM, SupplyDepotDefID, OccupiedDepotPos, PlayerID),
            "building placement is rejected when another ground unit occupies the footprint");

    const Core::Math::Vec2 NewDepotPos = Map.TileToWorld(20, 12);
    const int Scv = CreateTerranUnit(EM, "SCV", PlayerID, NewDepotPos);
    const int HarvestMineral = EM.CreateEntity();
    auto& HarvestResource =
        EM.AddComponent<Game::Components::ResourceComponent>(HarvestMineral);
    const int OtherMiningScv = EM.CreateEntity();
    HarvestResource.Type = Game::Components::ResourceType::Minerals;
    HarvestResource.Amount = 1500;
    HarvestResource.ActiveGathererEntityID = Scv;
    HarvestResource.QueuedGatherers = {Scv, OtherMiningScv};
    auto& BuilderHarvest =
        EM.AddComponent<Game::Components::HarvestComponent>(Scv);
    BuilderHarvest.State = Game::Components::HarvestState::Mining;
    BuilderHarvest.TargetResourceID = HarvestMineral;
    BuilderHarvest.DesiredResourceID = HarvestMineral;
    const auto SupplyDepotDef = Game::Races::GetUnitDefByID(SupplyDepotDefID);
    const int MineralsBeforeBuild = Resources.Minerals;
    const int SupplyMaxBeforeBuild = Resources.SupplyMax;
    Require(Game::Systems::BuildingSystem::StartBuild(EM, Scv, SupplyDepotDefID, NewDepotPos),
        "SCV can start a Supply Depot build at a valid placement");
    Require(Resources.Minerals == MineralsBeforeBuild - SupplyDepotDef.MineralCost,
        "starting construction spends the building mineral cost");
    Require(Resources.SupplyMax == SupplyMaxBeforeBuild,
        "Supply Depot does not grant supply before construction completes");
    Require(BuilderHarvest.State == Game::Components::HarvestState::Idle &&
            BuilderHarvest.TargetResourceID < 0 &&
            HarvestResource.ActiveGathererEntityID < 0 &&
            HarvestResource.QueuedGatherers.size() == 1 &&
            HarvestResource.QueuedGatherers.front() == OtherMiningScv,
        "forced construction releases only the builder from the mineral queue");

    Game::Systems::BuildingSystem BuildingSystem(SysMgr);
    int NewDepot = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        if (Building &&
            Building->BuildingDefID == SupplyDepotDefID &&
            Building->UnderConstruction &&
            Building->BuilderUnitID == Scv)
        {
            NewDepot = EntityID;
            break;
        }
    }
    Require(NewDepot < 0 &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                PlayerID, SupplyDepotDefID) == 1,
        "starting construction queues the build without creating a building entity");

    BuildingSystem.Update(0.001f);
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        if (Building &&
            Building->BuildingDefID == SupplyDepotDefID &&
            Building->UnderConstruction &&
            Building->BuilderUnitID == Scv)
        {
            NewDepot = EntityID;
            break;
        }
    }
    Require(NewDepot > 0,
        "SCV arrival creates the under-construction building entity");

    auto* NewDepotBuilding = EM.GetComponent<Game::Components::BuildingComponent>(NewDepot);
    auto* NewDepotUnit = EM.GetComponent<Game::Components::UnitComponent>(NewDepot);
    auto* NewDepotRender = EM.GetComponent<Game::Components::RenderComponent>(NewDepot);
    Require(NewDepotBuilding && NewDepotUnit, "new Supply Depot has building and unit data");
    Require(NewDepotRender &&
            NewDepotRender->ImageID == (uint32_t)(20000 + SupplyDepotDef.ChkUnitID),
        "under-construction Supply Depot uses the construction animation image");
    Require(NewDepotRender && NewDepotRender->Tint.A == 255,
        "construction image keeps its authored per-pixel alpha without a ghost tint");
    Require(NewDepotBuilding->BuildProgress > 0.0f &&
            NewDepotBuilding->BuildProgress <= 0.0011f &&
            NewDepotBuilding->TotalBuildTime == (float)SupplyDepotDef.BuildTime,
        "new Supply Depot starts near zero progress with definition build time");
    Require(NewDepotUnit->HitPoints == 1 &&
            NewDepotUnit->MaxHitPoints == SupplyDepotDef.HitPoints,
        "under-construction building starts damaged but keeps max HP for progress UI");

    const int MineralsBeforeRejectedConcurrentBuild = Resources.Minerals;
    Require(!Game::Systems::BuildingSystem::StartBuild(
                EM, Scv, SupplyDepotDefID, Map.TileToWorld(24, 12)),
        "SCV already assigned to an unfinished Terran building cannot start another building");
    Require(Resources.Minerals == MineralsBeforeRejectedConcurrentBuild,
        "rejected concurrent SCV construction does not spend minerals");

    auto* NewDepotBuilderPath =
        &EM.AddComponent<Game::Components::PathfindingComponent>(Scv);
    NewDepotBuilderPath->Path = {{NewDepotPos.X + 96.0f, NewDepotPos.Y}};
    NewDepotBuilderPath->PathIndex = 0;
    NewDepotBuilderPath->CurrentSpeed = 32.0f;
    NewDepotBuilderPath->TargetSpeed = 32.0f;
    NewDepotBuilderPath->CurrentVelocity = {32.0f, 0.0f};
    BuildingSystem.Update((float)SupplyDepotDef.BuildTime * 0.19f);
    Require(NewDepotBuilding->UnderConstruction &&
            NewDepotRender->ImageID == (uint32_t)(20000 + SupplyDepotDef.ChkUnitID),
        "early Terran construction keeps the generic construction image");
    Require(NewDepotBuilderPath->Path.empty() &&
            NewDepotBuilderPath->CurrentSpeed == 0.0f &&
            NewDepotBuilderPath->TargetSpeed == 0.0f &&
            NewDepotBuilderPath->CurrentVelocity.LengthSq() == 0.0f,
        "SCV stops its approach motion once it reaches the construction footprint");
    BuildingSystem.Update((float)SupplyDepotDef.BuildTime * 0.42f);
    Require(NewDepotBuilding->UnderConstruction &&
            NewDepotRender->ImageID == (uint32_t)(10000 + SupplyDepotDef.ChkUnitID) &&
            NewDepotRender->Tint.A == 255,
        "Terran construction switches to the opaque finished graphic after three fifths progress");
    BuildingSystem.Update((float)SupplyDepotDef.BuildTime);
    Require(!NewDepotBuilding->UnderConstruction,
        "construction update completes the Supply Depot when the builder is nearby");
    Require(NewDepotBuilding->BuildProgress == NewDepotBuilding->TotalBuildTime,
        "completed building records full build progress");
    Require(NewDepotUnit->HitPoints == NewDepotUnit->MaxHitPoints,
        "completed building restores full HP");
    Require(NewDepotRender->ImageID == (uint32_t)(10000 + SupplyDepotDef.ChkUnitID),
        "completed Supply Depot swaps back to the finished building image");
    Require(Resources.SupplyMax == SupplyMaxBeforeBuild + SupplyDepotDef.SupplyCost,
        "completed Supply Depot increases the player's supply cap");
    auto* ScvCommand = EM.GetComponent<Game::Components::CommandComponent>(Scv);
    Require(ScvCommand && ScvCommand->CurrentCommand == Game::Components::CommandType::Stop,
        "builder command returns to stop after construction completes");

    const int SupplyMaxAfterCompletedDepot = Resources.SupplyMax;
    const Core::Math::Vec2 StalledDepotPos = Map.TileToWorld(25, 12);
    const int StalledScv = CreateTerranUnit(EM, "SCV", PlayerID, StalledDepotPos);
    Require(Game::Systems::BuildingSystem::StartBuild(EM, StalledScv, SupplyDepotDefID, StalledDepotPos),
        "live SCV can start a second Supply Depot for construction-stall coverage");
    BuildingSystem.Update(0.001f);

    int StalledDepot = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        if (Building &&
            Building->BuildingDefID == SupplyDepotDefID &&
            Building->UnderConstruction &&
            Building->BuilderUnitID == StalledScv)
        {
            StalledDepot = EntityID;
            break;
        }
    }
    Require(StalledDepot > 0, "second Supply Depot starts under construction");

    auto* StalledDepotBuilding = EM.GetComponent<Game::Components::BuildingComponent>(StalledDepot);
    auto* StalledDepotUnit = EM.GetComponent<Game::Components::UnitComponent>(StalledDepot);
    auto* StalledScvUnit = EM.GetComponent<Game::Components::UnitComponent>(StalledScv);
    Require(StalledDepotBuilding && StalledDepotUnit && StalledScvUnit,
        "stalled construction scenario has all required unit data");
    const float StalledProgressBeforeDeath = StalledDepotBuilding->BuildProgress;
    StalledScvUnit->HitPoints = 0;
    BuildingSystem.Update((float)SupplyDepotDef.BuildTime);
    Require(StalledDepotBuilding->UnderConstruction,
        "Terran construction remains unfinished when the assigned SCV dies");
    Require(StalledDepotBuilding->BuildProgress == StalledProgressBeforeDeath,
        "dead builder does not advance Terran building progress");
    Require(StalledDepotUnit->HitPoints == 1,
        "stalled under-construction building keeps its partial HP");
    Require(Resources.SupplyMax == SupplyMaxAfterCompletedDepot,
        "stalled Supply Depot does not grant supply");

    const Core::Math::Vec2 DeadBuilderDepotPos = Map.TileToWorld(30, 12);
    const int DeadScv = CreateTerranUnit(EM, "SCV", PlayerID, DeadBuilderDepotPos);
    auto* DeadScvUnit = EM.GetComponent<Game::Components::UnitComponent>(DeadScv);
    Require(DeadScvUnit != nullptr, "dead-builder coverage has SCV unit data");
    DeadScvUnit->HitPoints = 0;
    Require(!Game::Systems::BuildingSystem::StartBuild(EM, DeadScv, SupplyDepotDefID, DeadBuilderDepotPos),
        "dead SCV cannot start a new building");

    const int AddonPlayerID = 2;
    auto& AddonResources = Game::Systems::HarvestSystem::GetResources(AddonPlayerID);
    AddonResources.Minerals = 1000;
    AddonResources.VespeneGas = 1000;
    const int FactoryDefID = Game::Races::GetUnitDefID("Factory");
    const int MachineShopDefID = Game::Races::GetUnitDefID("MachineShop");
    const int LiftedFactory = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, FactoryDefID, Map.TileToWorld(34, 18), AddonPlayerID);
    auto* LiftedFactoryBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(LiftedFactory);
    Require(LiftedFactoryBuilding != nullptr,
        "lifted add-on parent coverage has factory building data");
    LiftedFactoryBuilding->IsLifted = true;
    Require(!Game::Systems::BuildingSystem::StartBuild(
                EM, LiftedFactory, MachineShopDefID, Game::Systems::BuildTarget{}),
            "lifted Terran parent building cannot start an add-on");
    Require(AddonResources.Minerals == 1000 && AddonResources.VespeneGas == 1000,
        "failed lifted add-on start does not spend resources");

    const Core::Math::Vec2 FactoryWithAddonPos = Map.TileToWorld(44, 18);
    const int FactoryWithAddon = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, FactoryDefID, FactoryWithAddonPos, AddonPlayerID);
    const int MachineShop = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, MachineShopDefID, FactoryWithAddonPos + Core::Math::Vec2{96.0f, 16.0f},
        AddonPlayerID, FactoryWithAddon);
    auto* FactoryWithAddonBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(FactoryWithAddon);
    auto* MachineShopBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(MachineShop);
    auto* MachineShopPlayer =
        EM.GetComponent<Game::Components::PlayerComponent>(MachineShop);
    Require(FactoryWithAddonBuilding && MachineShopBuilding && MachineShopPlayer &&
            FactoryWithAddonBuilding->AddonID == MachineShop &&
            MachineShopBuilding->BuilderUnitID == FactoryWithAddon,
        "completed Terran add-on starts linked to its parent building");
    Require(Game::Systems::BuildingSystem::LiftOffBuilding(EM, FactoryWithAddon),
        "Factory with a Machine Shop can begin lift-off");
    Require(FactoryWithAddonBuilding->AddonID == -1 &&
            MachineShopBuilding->BuilderUnitID == -1 &&
            MachineShopPlayer->PlayerID == AddonPlayerID &&
            MachineShopBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::LiftingOff,
        "lift-off detaches the still-owned add-on and starts its detach animation");
    FactoryWithAddonBuilding->FlightAnimationComplete = true;
    BuildingSystem.Update(1.0f / 24.0f);
    Require(FactoryWithAddonBuilding->FlightHeightTransitionActive &&
            FactoryWithAddonBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::LiftingOff,
        "parent retracts its landing gear before the body rises");
    BuildingSystem.Update(0.5f);
    MachineShopBuilding->FlightPhase = Game::Components::BuildingFlightPhase::Grounded;
    Require(FactoryWithAddonBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::Airborne,
        "lift-off completion signal makes the parent building airborne");
    Require(Game::Systems::BuildingSystem::LandBuilding(
                EM, FactoryWithAddon, FactoryWithAddonPos),
        "airborne Factory can start landing beside its abandoned Machine Shop");
    BuildingSystem.Update(0.5f);
    FactoryWithAddonBuilding->FlightAnimationComplete = true;
    BuildingSystem.Update(1.0f / 24.0f);
    Require(!FactoryWithAddonBuilding->IsLifted &&
            FactoryWithAddonBuilding->AddonID == MachineShop &&
            MachineShopBuilding->BuilderUnitID == FactoryWithAddon &&
            MachineShopPlayer->PlayerID == AddonPlayerID &&
            MachineShopBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::Landing,
        "landing reconnects the compatible owned add-on and starts its landing animation");

    const Core::Math::Vec2 RestoredFactoryPos = Map.TileToWorld(52, 18);
    const int RestoredFactory = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, FactoryDefID, RestoredFactoryPos, AddonPlayerID);
    const int RestoredMachineShop = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, MachineShopDefID, RestoredFactoryPos + Core::Math::Vec2{96.0f, 16.0f},
        AddonPlayerID);
    Game::Systems::BuildingSystem::RestoreSpawnedAddonLinks(EM);
    Require(EM.GetComponent<Game::Components::BuildingComponent>(RestoredFactory)->AddonID ==
                RestoredMachineShop &&
            EM.GetComponent<Game::Components::BuildingComponent>(RestoredMachineShop)->BuilderUnitID ==
                RestoredFactory,
        "map-spawned parent and add-on relationships are reconstructed from project positions and definitions");

    const Core::Math::Vec2 RestoredCommandCenterPos = Map.TileToWorld(62, 18);
    const int RestoredCommandCenter = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, CommandCenterDefID, RestoredCommandCenterPos, AddonPlayerID);
    const int RestoredComsat = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, Game::Races::GetUnitDefID("ComsatStation"),
        RestoredCommandCenterPos + Core::Math::Vec2{96.0f, 16.0f}, AddonPlayerID);
    Game::Systems::BuildingSystem::RestoreSpawnedAddonLinks(EM);
    auto* RestoredComsatRender = EM.GetComponent<Game::Components::RenderComponent>(RestoredComsat);
    Require(EM.GetComponent<Game::Components::BuildingComponent>(RestoredCommandCenter)->AddonID ==
                RestoredComsat &&
            RestoredComsatRender && RestoredComsatRender->AddonConnectorImageID == 272,
        "map-spawned Comsat receives its dedicated bridge connector image");

    Resources.Minerals = 1000;
    const Core::Math::Vec2 CommandCenterBuildPos = Map.TileToWorld(72, 28);
    const int CommandCenterScv = CreateTerranUnit(
        EM, "SCV", PlayerID,
        CommandCenterBuildPos + Core::Math::Vec2{88.0f, 0.0f});
    Require(Game::Systems::BuildingSystem::StartBuild(
                EM, CommandCenterScv, CommandCenterDefID, CommandCenterBuildPos),
        "SCV can place a Command Center foundation for edge-contact coverage");
    BuildingSystem.Update(0.001f);
    const int AssistantCommandCenterScv = CreateTerranUnit(
        EM, "SCV", PlayerID,
        CommandCenterBuildPos + Core::Math::Vec2{-88.0f, 0.0f});
    int CommandCenterUnderConstruction = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        const auto* Building =
            EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        if (Building && Building->BuildingDefID == CommandCenterDefID &&
            Building->UnderConstruction && Building->BuilderUnitID == CommandCenterScv)
        {
            CommandCenterUnderConstruction = EntityID;
            break;
        }
    }
    Require(CommandCenterUnderConstruction > 0,
        "Command Center foundation retains its assigned SCV");
    auto* CommandCenterConstruction =
        EM.GetComponent<Game::Components::BuildingComponent>(CommandCenterUnderConstruction);
    Require(Game::Systems::BuildingSystem::AssistConstruction(
                EM, {AssistantCommandCenterScv}, CommandCenterUnderConstruction),
        "a second SCV can assist an unfinished Command Center");
    Require(CommandCenterConstruction &&
            CommandCenterConstruction->AssistantBuilderUnitIDs.size() == 1 &&
            CommandCenterConstruction->AssistantBuilderUnitIDs.front() ==
                AssistantCommandCenterScv,
        "unfinished Command Center records its assisting SCV");
    BuildingSystem.Update(1.0f);
    Require(CommandCenterConstruction && CommandCenterConstruction->BuildProgress >= 1.99f,
        "two SCVs advance Command Center construction at twice the base rate");
    CommandCenterConstruction->BuildProgress =
        CommandCenterConstruction->TotalBuildTime - 1.0f;
    BuildingSystem.Update(1.0f);
    const auto* AssistantCommand =
        EM.GetComponent<Game::Components::CommandComponent>(AssistantCommandCenterScv);
    Require(!CommandCenterConstruction->UnderConstruction &&
            CommandCenterConstruction->AssistantBuilderUnitIDs.empty() &&
            AssistantCommand &&
            AssistantCommand->CurrentCommand == Game::Components::CommandType::Stop,
        "completion releases every assisting SCV and clears the construction team");

    constexpr int LandingPlayerID = 1;
    auto& PlayerZeroResources = Game::Systems::HarvestSystem::GetResources(0);
    auto& LandingResources = Game::Systems::HarvestSystem::GetResources(LandingPlayerID);
    PlayerZeroResources.Minerals = 0;
    PlayerZeroResources.VespeneGas = 0;
    LandingResources.Minerals = 0;
    LandingResources.VespeneGas = 0;

    const int LiftedCommandCenter = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, CommandCenterDefID, Map.TileToWorld(34, 6), LandingPlayerID);
    auto* LiftedCommand =
        EM.GetComponent<Game::Components::CommandComponent>(LiftedCommandCenter);
    if (!LiftedCommand)
        LiftedCommand = &EM.AddComponent<Game::Components::CommandComponent>(LiftedCommandCenter);
    LiftedCommand->CurrentCommand = Game::Components::CommandType::Move;
    LiftedCommand->TargetPosition = Map.TileToWorld(50, 6);
    Require(Game::Systems::BuildingSystem::LiftOffBuilding(EM, LiftedCommandCenter),
        "completed Terran Command Center can lift off before landing");
    auto* LiftedBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(LiftedCommandCenter);
    auto* LiftedUnit =
        EM.GetComponent<Game::Components::UnitComponent>(LiftedCommandCenter);
    Require(LiftedBuilding && LiftedBuilding->IsLifted &&
            LiftedBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::LiftingOff &&
            LiftedUnit && LiftedUnit->Speed == 0.0f,
        "lift-off begins as an immobile script-driven transition");
    Require(Game::Client::BuildingFlightBodyOffsetY(LiftedBuilding) == 0.0f,
        "lift-off keeps the building body grounded while the landing gear retracts");
    auto* FlyingRender =
        EM.GetComponent<Game::Components::RenderComponent>(LiftedCommandCenter);
    auto* FlyingPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(LiftedCommandCenter);
    Require(FlyingRender &&
            FlyingRender->Layer == Game::Components::RenderLayer::AirUnits,
        "lifted building renders on the air-unit layer");
    Require(FlyingPath && FlyingPath->IsAir,
        "lifted building switches to air pathing");
    Require(LiftedCommand->CurrentCommand == Game::Components::CommandType::Stop &&
            LiftedCommand->TargetEntityID == -1 && FlyingPath->Path.empty(),
        "lift-off clears stale movement so the building does not fly away by itself");
    Require(!Game::Systems::BuildingSystem::LandBuilding(
                EM, LiftedCommandCenter, Map.TileToWorld(34, 12)),
        "building cannot land before its LiftOff animation signals completion");
    LiftedBuilding->FlightAnimationComplete = true;
    BuildingSystem.Update(1.0f / 24.0f);
    Require(LiftedBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::LiftingOff &&
            LiftedBuilding->FlightHeightTransitionActive &&
            LiftedUnit->Speed == 0.0f &&
            LiftedBuilding->FlightVisualHeight > 0.0f &&
            LiftedBuilding->FlightVisualHeight < 24.0f,
        "landing-gear completion starts a separate immobile body-rise stage");
    BuildingSystem.Update(0.5f);
    Require(LiftedBuilding->FlightPhase ==
                Game::Components::BuildingFlightPhase::Airborne &&
            LiftedUnit->Speed > 0.0f,
        "LiftOff completion signal enables airborne movement");
    Require(Game::Client::BuildingFlightBodyOffsetY(LiftedBuilding) == -24.0f,
        "airborne building body holds a stable flight height above its shadow");
    Require(!Game::Systems::BuildingSystem::LandBuilding(
                EM, LiftedCommandCenter, Map.TileToWorld(6, 6)),
            "lifted building cannot land on an occupied building footprint");
    Require(Game::Systems::BuildingSystem::LandBuilding(
                EM, LiftedCommandCenter, Map.TileToWorld(34, 12)),
        "lifted building can land on a valid footprint without checking player resources");
    auto* LiftedPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(LiftedCommandCenter);
    auto* LandedRender =
        EM.GetComponent<Game::Components::RenderComponent>(LiftedCommandCenter);
    Require(LiftedBuilding && LiftedBuilding->IsLifted &&
            LiftedBuilding->FlightPhase == Game::Components::BuildingFlightPhase::Landing,
        "valid landing starts the Landing iscript before clearing flight state");
    Require(Game::Client::BuildingFlightBodyOffsetY(LiftedBuilding) == -24.0f,
        "landing begins from the airborne body height while the shadow stays grounded");
    Require(LiftedUnit && LiftedUnit->Speed == 0.0f,
        "landing transition makes the building immobile");
    Require(LiftedPath && LiftedPath->IsAir && LiftedPath->Path.empty(),
        "landing clears movement while retaining flight pathing until the script signal");
    Require(LandedRender &&
            LandedRender->Layer == Game::Components::RenderLayer::AirUnits,
        "landing transition remains on the air layer until completion");
    BuildingSystem.Update(0.25f);
    Require(LiftedBuilding->FlightHeightTransitionActive &&
            LiftedBuilding->FlightVisualHeight > 0.0f &&
            LiftedBuilding->FlightVisualHeight < 24.0f,
        "landing lowers the body while keeping the landing gear retracted");
    BuildingSystem.Update(0.25f);
    Require(!LiftedBuilding->FlightHeightTransitionActive &&
            LiftedBuilding->FlightPhase == Game::Components::BuildingFlightPhase::Landing &&
            Game::Client::BuildingFlightBodyOffsetY(LiftedBuilding) == 0.0f,
        "landing body reaches ground height before the landing gear deploys");
    LiftedBuilding->FlightAnimationComplete = true;
    BuildingSystem.Update(1.0f / 24.0f);
    Require(!LiftedBuilding->IsLifted &&
            LiftedBuilding->FlightPhase == Game::Components::BuildingFlightPhase::Grounded,
        "Landing completion signal clears lifted state");
    Require(Game::Client::BuildingFlightBodyOffsetY(LiftedBuilding) == 0.0f,
        "completed landing returns the building body to ground height");
    Require(LiftedPath && !LiftedPath->IsAir && LiftedPath->Path.empty(),
        "completed landing restores ground pathing state");
    Require(LandedRender &&
            LandedRender->Layer == Game::Components::RenderLayer::Buildings,
        "completed landing renders on the building layer again");

    std::printf("BuildingPlacementTest passed\n");
    return 0;
}
