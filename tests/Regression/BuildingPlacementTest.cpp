#include "Game/Systems/BuildingSystem.h"
#include "Client/EntityRenderer.h"

#include "Core/ECS/SystemManager.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/DeathEvents.h"

#include <algorithm>
#include <cmath>
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

    int CreateZergDrone(Core::ECS::EntityManager& EM,
                        int PlayerID,
                        Core::Math::Vec2 Position)
    {
        const int DefID = Game::Races::GetUnitDefID("Drone");
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.SupplyCost = Def.SupplyCost;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        EM.AddComponent<Game::Components::CommandComponent>(EntityID).CurrentCommand =
            Game::Components::CommandType::Stop;
        return EntityID;
    }

    int CreateVespeneGeyser(Core::ECS::EntityManager& EM,
                            Core::Math::Vec2 Position)
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

    constexpr int UnreachableBuilderPlayerID = 5;
    auto& UnreachableResources =
        Game::Systems::HarvestSystem::GetResources(UnreachableBuilderPlayerID);
    UnreachableResources.Minerals = 1000;
    UnreachableResources.VespeneGas = 1000;
    const int UnreachableProbe = CreateProtossProbe(
        EM, UnreachableBuilderPlayerID, Map.TileToWorld(88, 40));
    const Core::Math::Vec2 UnreachableNexusPos = Map.TileToWorld(104, 40);
    const int MineralsBeforeUnreachableBuild = UnreachableResources.Minerals;
    Require(Game::Systems::BuildingSystem::StartBuild(
                EM, UnreachableProbe, NexusDefID, UnreachableNexusPos),
        "Probe can queue a distant Nexus for unreachable-builder coverage");
    Require(UnreachableResources.Minerals ==
                MineralsBeforeUnreachableBuild - NexusDef.MineralCost &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                UnreachableBuilderPlayerID, NexusDefID) == 1 &&
            Game::Systems::BuildingSystem::IsBuilderCommitted(
                EM, UnreachableProbe),
        "pending distant build spends resources and commits its Probe");

    // This test's movement stub deliberately never advances the Probe. Each
    // update therefore exercises one bounded no-progress retry after the
    // initial distance sample, without relying on a wall-clock timeout.
    for (int Attempt = 0; Attempt < 9; ++Attempt)
        ProtossBuildingSystem.Update(4.1f);

    auto* UnreachableProbeCommand =
        EM.GetComponent<Game::Components::CommandComponent>(UnreachableProbe);
    auto* UnreachableProbePath =
        EM.GetComponent<Game::Components::PathfindingComponent>(UnreachableProbe);
    Require(Game::Systems::BuildingSystem::CountPendingBuilds(
                UnreachableBuilderPlayerID, NexusDefID) == 0 &&
            !Game::Systems::BuildingSystem::IsBuilderCommitted(
                EM, UnreachableProbe),
        "unreachable pending build is cancelled after bounded retries");
    Require(UnreachableResources.Minerals == MineralsBeforeUnreachableBuild &&
            UnreachableResources.VespeneGas == 1000,
        "unreachable pending build refunds its full resource cost");
    Require(UnreachableProbeCommand &&
            UnreachableProbeCommand->CurrentCommand ==
                Game::Components::CommandType::Stop &&
            !UnreachableProbeCommand->HasPendingOrders() &&
            UnreachableProbePath && UnreachableProbePath->Path.empty(),
        "unreachable cancellation releases the Probe command and path");

    const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    const int Hatchery = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
        EM, HatcheryDefID, Map.TileToWorld(18, 12), PlayerID);
    auto* HatcheryBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(Hatchery);
    Require(HatcheryBuilding && HatcheryBuilding->IsOrganic,
        "completed Zerg Hatchery is organic so the live Zerg system can spawn larva");
    EM.DestroyEntity(Hatchery);

    constexpr int ZergBaseBuilderPlayerID = 6;
    auto& ZergBaseResources =
        Game::Systems::HarvestSystem::GetResources(ZergBaseBuilderPlayerID);
    ZergBaseResources.Minerals = 1000;
    ZergBaseResources.VespeneGas = 1000;
    ZergBaseResources.SupplyUsed = 1;
    ZergBaseResources.SupplyMax = 10;
    const Core::Math::Vec2 DistantDronePos = Map.TileToWorld(42, 30);
    const Core::Math::Vec2 NewHatcheryPos = Map.TileToWorld(50, 30);
    const int BaseDrone =
        CreateZergDrone(EM, ZergBaseBuilderPlayerID, DistantDronePos);
    Require(Game::Systems::BuildingSystem::StartBuild(
                EM, BaseDrone, HatcheryDefID, NewHatcheryPos),
        "Drone can accept a distant Hatchery morph order");
    Require(Game::Systems::BuildingSystem::CountPendingBuilds(
                ZergBaseBuilderPlayerID, HatcheryDefID) == 1 &&
            EM.IsAlive(BaseDrone),
        "Hatchery remains pending and keeps its Drone alive during approach");
    const int MineralsAfterFirstHatcheryOrder = ZergBaseResources.Minerals;
    Require(!Game::Systems::BuildingSystem::StartBuild(
                EM, BaseDrone, HatcheryDefID, Map.TileToWorld(54, 30)) &&
            ZergBaseResources.Minerals == MineralsAfterFirstHatcheryOrder &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                ZergBaseBuilderPlayerID, HatcheryDefID) == 1,
        "committed Drone rejects a second build order without duplicate spending");

    constexpr int CancelledDronePlayerID = 9;
    auto& CancelledDroneResources =
        Game::Systems::HarvestSystem::GetResources(CancelledDronePlayerID);
    CancelledDroneResources.Minerals = 1000;
    CancelledDroneResources.VespeneGas = 0;
    const int CancelledDrone = CreateZergDrone(
        EM, CancelledDronePlayerID, Map.TileToWorld(42, 42));
    const int MineralsBeforeCancelledOrder = CancelledDroneResources.Minerals;
    Require(Game::Systems::BuildingSystem::StartBuild(
                EM, CancelledDrone, HatcheryDefID,
                Map.TileToWorld(50, 42)),
        "Drone can queue a distant build before replacement-command coverage");
    auto* CancelledDroneCommand =
        EM.GetComponent<Game::Components::CommandComponent>(CancelledDrone);
    Require(CancelledDroneCommand != nullptr,
        "pending Drone has command state for cancellation coverage");
    CancelledDroneCommand->CurrentCommand =
        Game::Components::CommandType::Move;
    CancelledDroneCommand->TargetPosition = Map.TileToWorld(40, 44);
    ProtossBuildingSystem.Update(0.001f);
    Require(EM.IsAlive(CancelledDrone) &&
            CancelledDroneCommand->CurrentCommand ==
                Game::Components::CommandType::Move &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                CancelledDronePlayerID, HatcheryDefID) == 0 &&
            CancelledDroneResources.Minerals == MineralsBeforeCancelledOrder,
        "replacement Move cancels and refunds pending morph without hijacking the Drone");
    ProtossBuildingSystem.Update(0.001f);
    int PendingHatchery = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        const auto* Building =
            EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (Building && Player && Player->PlayerID == ZergBaseBuilderPlayerID &&
            Building->BuildingDefID == HatcheryDefID)
            PendingHatchery = EntityID;
    }
    Require(PendingHatchery < 0 && EM.IsAlive(BaseDrone),
        "Drone does not remotely hatch a base before reaching its footprint");
    EM.GetComponent<Game::Components::TransformComponent>(BaseDrone)->Position =
        NewHatcheryPos;
    ProtossBuildingSystem.Update(0.001f);
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        const auto* Building =
            EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (Building && Player && Player->PlayerID == ZergBaseBuilderPlayerID &&
            Building->BuildingDefID == HatcheryDefID)
            PendingHatchery = EntityID;
    }
    Require(PendingHatchery > 0 &&
            EM.GetComponent<Game::Components::BuildingComponent>(PendingHatchery)->UnderConstruction &&
            !EM.IsAlive(BaseDrone),
        "Drone arrival consumes the worker and begins Hatchery morphing");
    const auto* PendingHatcheryUnit =
        EM.GetComponent<Game::Components::UnitComponent>(PendingHatchery);
    const auto HatcheryDef = Game::Races::GetUnitDefByID(HatcheryDefID);
    Require(PendingHatcheryUnit &&
            PendingHatcheryUnit->HitPoints ==
                std::max(1, HatcheryDef.HitPoints / 10),
        "newly morphing Zerg building starts at classic ten-percent vitality");
    Require(ZergBaseResources.SupplyUsed == 0,
        "morphing a Zerg base releases the consumed Drone supply");

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

    constexpr int ZergTechBuilderPlayerID = 7;
    auto& ZergTechResources =
        Game::Systems::HarvestSystem::GetResources(ZergTechBuilderPlayerID);
    ZergTechResources.Minerals = 1000;
    ZergTechResources.VespeneGas = 1000;
    ZergTechResources.SupplyUsed = 1;
    ZergTechResources.SupplyMax = 10;
    const int TechHatchery =
        Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
            EM, HatcheryDefID, Map.TileToWorld(58, 34), ZergTechBuilderPlayerID);
    Require(TechHatchery > 0,
        "completed Hatchery satisfies the Zerg tech-building prerequisite");
    const Core::Math::Vec2 TechMorphPos = Map.TileToWorld(64, 34);
    for (int Y = 33; Y <= 34; ++Y)
    {
        for (int X = 63; X <= 65; ++X)
        {
            auto Tile = Map.GetTile(X, Y);
            Tile.HasCreep = true;
            Map.SetTile(X, Y, Tile);
        }
    }
    const int TechDrone = CreateZergDrone(
        EM, ZergTechBuilderPlayerID, Map.TileToWorld(60, 34));
    Require(Game::Systems::BuildingSystem::StartBuild(
                EM, TechDrone, SpawningPoolDefID, TechMorphPos),
        "Drone can accept a creep-covered tech-building morph order");
    ProtossBuildingSystem.Update(0.001f);
    int PendingPool = -1;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        const auto* Building =
            EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (Building && Player && Player->PlayerID == ZergTechBuilderPlayerID &&
            Building->BuildingDefID == SpawningPoolDefID)
            PendingPool = EntityID;
    }
    Require(PendingPool < 0 && EM.IsAlive(TechDrone),
        "Zerg tech building does not hatch before its Drone arrives");
    EM.GetComponent<Game::Components::TransformComponent>(TechDrone)->Position =
        TechMorphPos;
    for (int Y = 33; Y <= 34; ++Y)
    {
        for (int X = 63; X <= 65; ++X)
        {
            auto Tile = Map.GetTile(X, Y);
            Tile.HasCreep = false;
            Map.SetTile(X, Y, Tile);
        }
    }
    ProtossBuildingSystem.Update(0.001f);
    Require(EM.IsAlive(TechDrone) &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                ZergTechBuilderPlayerID, SpawningPoolDefID) == 1,
        "Drone arrival revalidates creep instead of morphing after it recedes");
    for (int Y = 33; Y <= 34; ++Y)
    {
        for (int X = 63; X <= 65; ++X)
        {
            auto Tile = Map.GetTile(X, Y);
            Tile.HasCreep = true;
            Map.SetTile(X, Y, Tile);
        }
    }
    ProtossBuildingSystem.Update(0.001f);
    for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
    {
        const auto* Building =
            EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
        const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (Building && Player && Player->PlayerID == ZergTechBuilderPlayerID &&
            Building->BuildingDefID == SpawningPoolDefID)
            PendingPool = EntityID;
    }
    Require(PendingPool > 0 && !EM.IsAlive(TechDrone),
        "Drone arrival begins a normal Zerg building morph and consumes the worker");

    constexpr int ZergGasBuilderPlayerID = 8;
    auto& ZergGasResources =
        Game::Systems::HarvestSystem::GetResources(ZergGasBuilderPlayerID);
    ZergGasResources.Minerals = 1000;
    ZergGasResources.VespeneGas = 0;
    ZergGasResources.SupplyUsed = 1;
    ZergGasResources.SupplyMax = 10;
    const int ExtractorDefID = Game::Races::GetUnitDefID("Extractor");
    const Core::Math::Vec2 GeyserPosition = Map.TileToWorld(80, 38);
    const int Geyser = CreateVespeneGeyser(EM, GeyserPosition);
    const int GasDrone = CreateZergDrone(
        EM, ZergGasBuilderPlayerID, Map.TileToWorld(70, 38));
    Require(Game::Systems::BuildingSystem::StartBuild(
                EM, GasDrone, ExtractorDefID,
                Game::Systems::BuildTarget::FromEntity(Geyser)),
        "Drone can accept a distant Extractor morph order on a geyser");
    ProtossBuildingSystem.Update(0.001f);
    auto* PendingGeyserBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(Geyser);
    Require(EM.IsAlive(GasDrone) && PendingGeyserBuilding &&
            PendingGeyserBuilding->BuildingDefID != ExtractorDefID &&
            Game::Systems::BuildingSystem::CountPendingBuilds(
                ZergGasBuilderPlayerID, ExtractorDefID) == 1,
        "Extractor keeps its Drone and geyser intact until the approach completes");
    EM.GetComponent<Game::Components::TransformComponent>(GasDrone)->Position =
        GeyserPosition;
    ProtossBuildingSystem.Update(0.001f);
    auto* ExtractorBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(Geyser);
    Require(!EM.IsAlive(GasDrone) && ExtractorBuilding &&
            ExtractorBuilding->BuildingDefID == ExtractorDefID &&
            ExtractorBuilding->UnderConstruction &&
            ZergGasResources.SupplyUsed == 0,
        "Extractor consumes the Drone and releases supply only after geyser arrival");

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
    Require(NewDepotUnit->HitPoints ==
                std::max(1, SupplyDepotDef.HitPoints / 10) &&
            NewDepotUnit->MaxHitPoints == SupplyDepotDef.HitPoints,
        "under-construction building starts at classic ten-percent vitality and keeps max HP");

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
    constexpr int ConstructionCombatDamage = 25;
    NewDepotUnit->HitPoints -= ConstructionCombatDamage;
    const int DamagedConstructionHitPoints = NewDepotUnit->HitPoints;
    const float ProgressBeforeDamageTick = NewDepotBuilding->BuildProgress;
    BuildingSystem.Update(1.0f);
    const float DamageTickProgress =
        NewDepotBuilding->BuildProgress - ProgressBeforeDamageTick;
    const int MaximumEarnedHitPoints = (int)std::ceil(
        (float)(NewDepotUnit->MaxHitPoints -
                std::max(1, NewDepotUnit->MaxHitPoints / 10)) *
        DamageTickProgress / NewDepotBuilding->TotalBuildTime);
    Require(NewDepotUnit->HitPoints >= DamagedConstructionHitPoints &&
            NewDepotUnit->HitPoints <=
                DamagedConstructionHitPoints + MaximumEarnedHitPoints,
        "construction tick adds only earned vitality and does not heal combat damage");
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
    Require(NewDepotUnit->HitPoints ==
                NewDepotUnit->MaxHitPoints - ConstructionCombatDamage,
        "completed building preserves combat damage taken during construction");
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
    Require(StalledDepotUnit->HitPoints ==
                std::max(1, SupplyDepotDef.HitPoints / 10),
        "stalled under-construction building keeps its classic partial HP");
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

    // Addon Placement, Construction, Cancellation Refund, and Footprint Validation
    {
        const int TestFactoryDefID = Game::Races::GetUnitDefID("Factory");
        const int TestMachineShopDefID = Game::Races::GetUnitDefID("MachineShop");
        const auto TestMachineShopDef = Game::Races::GetUnitDefByID(TestMachineShopDefID);

        const int FactoryID = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
            EM, TestFactoryDefID, Map.TileToWorld(20, 20), 0);
        Require(FactoryID > 0, "Factory spawned");

        Resources.Minerals = 500;
        Resources.VespeneGas = 500;

        std::string FailReason;
        Require(Game::Systems::BuildingSystem::CanBuild(
            EM, FactoryID, TestMachineShopDefID, Game::Systems::BuildTarget{}, &FailReason),
            "CanBuild Machine Shop on unobstructed Factory should succeed");

        // Obstruct addon footprint at (20 + 4, 20 + 2) = (24, 22)
        const int ObstructingDepot = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
            EM, SupplyDepotDefID, Map.TileToWorld(24, 22), 0);
        Require(!Game::Systems::BuildingSystem::CanBuild(
            EM, FactoryID, TestMachineShopDefID, Game::Systems::BuildTarget{}, &FailReason),
            "CanBuild Machine Shop on obstructed tile footprint must fail");
        Require(FailReason == "invalid placement", "Obstructed addon placement gives invalid placement");

        // Remove obstruction and verify CanBuild succeeds again
        EM.DestroyEntity(ObstructingDepot);
        Require(Game::Systems::BuildingSystem::CanBuild(
            EM, FactoryID, TestMachineShopDefID, Game::Systems::BuildTarget{}, &FailReason),
            "CanBuild succeeds again once obstruction is removed");

        // Lift Factory -> CanBuild must fail with "add-on parent is lifted"
        auto* FactoryBuilding = EM.GetComponent<Game::Components::BuildingComponent>(FactoryID);
        FactoryBuilding->IsLifted = true;
        Require(!Game::Systems::BuildingSystem::CanBuild(
            EM, FactoryID, TestMachineShopDefID, Game::Systems::BuildTarget{}, &FailReason),
            "CanBuild addon on lifted building must fail");
        Require(FailReason == "add-on parent is lifted", "Fail reason must be add-on parent is lifted");
        FactoryBuilding->IsLifted = false;

        // Start construction of Machine Shop
        const int MineralsBefore = Resources.Minerals;
        const int GasBefore = Resources.VespeneGas;
        Require(Game::Systems::BuildingSystem::StartBuild(
            EM, FactoryID, TestMachineShopDefID, Game::Systems::BuildTarget{}),
            "StartBuild Machine Shop succeeded");
        const int MachineShopID = FactoryBuilding->AddonID;
        Require(MachineShopID > 0, "Factory AddonID populated");
        Require(Resources.Minerals == MineralsBefore - TestMachineShopDef.MineralCost,
            "Minerals deducted for addon build");
        Require(Resources.VespeneGas == GasBefore - TestMachineShopDef.GasCost,
            "Gas deducted for addon build");

        // Cannot build another addon while one is building/attached
        Require(!Game::Systems::BuildingSystem::CanBuild(
            EM, FactoryID, TestMachineShopDefID, Game::Systems::BuildTarget{}, &FailReason),
            "CanBuild addon when already attached must fail");
        Require(FailReason == "parent already has an add-on", "Fail reason must be parent already has an add-on");

        // Cancel construction: 75% refund and unbind
        const int MineralsAtCancel = Resources.Minerals;
        const int GasAtCancel = Resources.VespeneGas;
        Require(Game::Systems::BuildingSystem::CancelConstruction(EM, MachineShopID),
            "CancelConstruction on Machine Shop succeeded");
        Require(!EM.IsAlive(MachineShopID), "Cancelled Machine Shop entity destroyed");
        Require(FactoryBuilding->AddonID == -1, "Factory AddonID cleared on cancellation");
        Require(Resources.Minerals == MineralsAtCancel + (TestMachineShopDef.MineralCost * 3 / 4),
            "75% minerals refunded on cancellation");
        Require(Resources.VespeneGas == GasAtCancel + (TestMachineShopDef.GasCost * 3 / 4),
            "75% gas refunded on cancellation");
    }

    // Geyser Construction Cancellation and Restoration
    {
        const int RefineryDefID = Game::Races::GetUnitDefID("Refinery");
        const auto RefineryDef = Game::Races::GetUnitDefByID(RefineryDefID);
        const int GeyserID = CreateVespeneGeyser(EM, Map.TileToWorld(28, 28));

        // Simulate claiming geyser for refinery construction
        auto& Claimed = EM.AddComponent<Game::Components::ClaimedGeyserComponent>(GeyserID);
        Claimed.IsClaimed = true;
        Claimed.OriginalUnit = *EM.GetComponent<Game::Components::UnitComponent>(GeyserID);
        Claimed.OriginalBuilding = *EM.GetComponent<Game::Components::BuildingComponent>(GeyserID);
        Claimed.OriginalPlayer = *EM.GetComponent<Game::Components::PlayerComponent>(GeyserID);
        Claimed.OriginalRender = *EM.GetComponent<Game::Components::RenderComponent>(GeyserID);

        auto* RefineryBuilding = EM.GetComponent<Game::Components::BuildingComponent>(GeyserID);
        RefineryBuilding->UnderConstruction = true;
        RefineryBuilding->BuildingDefID = RefineryDefID;

        auto* RefineryPlayer = EM.GetComponent<Game::Components::PlayerComponent>(GeyserID);
        RefineryPlayer->PlayerID = 0;

        Resources.Minerals = 200;
        const int MineralsAtCancel = Resources.Minerals;
        Require(Game::Systems::BuildingSystem::CancelConstruction(EM, GeyserID),
            "CancelConstruction on Refinery succeeded");
        Require(Resources.Minerals == MineralsAtCancel + (RefineryDef.MineralCost * 3 / 4),
            "75% minerals refunded on Refinery cancellation");
        Require(EM.IsAlive(GeyserID), "Geyser entity remains alive after cancellation");
        auto* RestoredResource = EM.GetComponent<Game::Components::ResourceComponent>(GeyserID);
        Require(RestoredResource && RestoredResource->Type == Game::Components::ResourceType::VespeneGas,
            "Entity restored as VespeneGas resource");
        Require(EM.GetComponent<Game::Components::ClaimedGeyserComponent>(GeyserID) == nullptr,
            "ClaimedGeyserComponent removed from restored geyser");
    }

    // Addon and Parent Death Decoupling
    {
        const int TestFactoryDefID2 = Game::Races::GetUnitDefID("Factory");
        const int TestMachineShopDefID2 = Game::Races::GetUnitDefID("MachineShop");

        const int FactoryID = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
            EM, TestFactoryDefID2, Map.TileToWorld(40, 40), 0);
        const int AddonID = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
            EM, TestMachineShopDefID2, Map.TileToWorld(44, 42), 0);

        auto* FB = EM.GetComponent<Game::Components::BuildingComponent>(FactoryID);
        auto* AB = EM.GetComponent<Game::Components::BuildingComponent>(AddonID);
        FB->AddonID = AddonID;
        AB->IsAddon = true;
        AB->BuilderUnitID = FactoryID;

        // Parent death unlinks addon
        Game::Systems::RecordUnitDeath(EM, FactoryID);
        Require(AB->BuilderUnitID == -1, "Parent death clears addon BuilderUnitID");
        Require(FB->AddonID == -1, "Parent death clears Factory AddonID");

        // Reconnect and test addon death unlinks parent
        const int Factory2ID = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
            EM, TestFactoryDefID2, Map.TileToWorld(50, 50), 0);
        const int Addon2ID = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
            EM, TestMachineShopDefID2, Map.TileToWorld(54, 52), 0);
        auto* FB2 = EM.GetComponent<Game::Components::BuildingComponent>(Factory2ID);
        auto* AB2 = EM.GetComponent<Game::Components::BuildingComponent>(Addon2ID);
        FB2->AddonID = Addon2ID;
        AB2->IsAddon = true;
        AB2->BuilderUnitID = Factory2ID;

        Game::Systems::RecordUnitDeath(EM, Addon2ID);
        Require(FB2->AddonID == -1, "Addon death clears Factory AddonID");
    }

    // Multi-SCV collaborative construction regression
    {
        const Core::Math::Vec2 DepotBuildPos = Map.TileToWorld(20, 24);
        auto& PlayerZeroRes = Game::Systems::HarvestSystem::GetResources(0);
        PlayerZeroRes.Minerals = 500;
        PlayerZeroRes.VespeneGas = 0;

        const int PrimaryScv = CreateTerranUnit(EM, "SCV", 0, DepotBuildPos);
        const int AssistantA = CreateTerranUnit(EM, "SCV", 0, DepotBuildPos + Core::Math::Vec2{-100.0f, 0.0f});
        const int AssistantB = CreateTerranUnit(EM, "SCV", 0, DepotBuildPos + Core::Math::Vec2{100.0f, 0.0f});

        Require(Game::Systems::BuildingSystem::StartBuild(
                    EM, PrimaryScv, SupplyDepotDefID, DepotBuildPos, {AssistantA, AssistantB}),
                "StartBuild with multiple assistant SCVs succeeds");

        BuildingSystem.Update(0.001f);

        int DepotUnderConstruction = -1;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent>())
        {
            const auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
            if (Building && Building->BuildingDefID == SupplyDepotDefID &&
                Building->UnderConstruction && Building->BuilderUnitID == PrimaryScv)
            {
                DepotUnderConstruction = EntityID;
                break;
            }
        }
        Require(DepotUnderConstruction > 0, "Supply Depot under construction created");
        auto* DepotComp = EM.GetComponent<Game::Components::BuildingComponent>(DepotUnderConstruction);
        Require(DepotComp != nullptr, "Depot has building component");
        Require(DepotComp->AssistantBuilderUnitIDs.size() == 2,
                "both assistant SCVs registered to collaborative construction");

        // Simulate assistant SCVs having arrived at building footprint perimeter
        auto* TransA = EM.GetComponent<Game::Components::TransformComponent>(AssistantA);
        auto* TransB = EM.GetComponent<Game::Components::TransformComponent>(AssistantB);
        if (TransA) TransA->Position = DepotBuildPos + Core::Math::Vec2{-48.0f - 16.0f, 0.0f};
        if (TransB) TransB->Position = DepotBuildPos + Core::Math::Vec2{48.0f + 16.0f, 0.0f};

        const float ProgressBefore = DepotComp->BuildProgress;
        BuildingSystem.Update(1.0f);
        // With 3 SCVs nearby, progress advances by ~3x
        Require(DepotComp->BuildProgress - ProgressBefore >= 2.9f,
                "three SCVs accelerate construction at 3x rate");

        // Finish construction and verify all 3 SCVs are released and safely unstuck
        DepotComp->BuildProgress = DepotComp->TotalBuildTime - 1.0f;
        BuildingSystem.Update(1.0f);
        Require(!DepotComp->UnderConstruction, "construction finishes cleanly");
        Require(DepotComp->AssistantBuilderUnitIDs.empty(), "assistant builders cleared on finish");

        for (int BuilderID : {PrimaryScv, AssistantA, AssistantB})
        {
            const auto* Cmd = EM.GetComponent<Game::Components::CommandComponent>(BuilderID);
            Require(Cmd && Cmd->CurrentCommand == Game::Components::CommandType::Stop,
                    "all builders returned to Stop after completion");
        }
    }

    {
        // Verify defensive structure detection and combat component activation lifecycle
        const int TurretDefID = Game::Races::GetUnitDefID("MissileTurret");
        const auto TurretDef = Game::Races::GetUnitDefByID(TurretDefID);
        const int Turret = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Turret).Position = {600.0f, 600.0f};
        EM.AddComponent<Game::Components::PlayerComponent>(Turret).PlayerID = 0;
        auto& TurretUnit = EM.AddComponent<Game::Components::UnitComponent>(Turret);
        TurretUnit.ChkUnitID = TurretDef.ChkUnitID;
        TurretUnit.HitPoints = TurretDef.HitPoints / 10;
        TurretUnit.MaxHitPoints = TurretDef.HitPoints;
        TurretUnit.UnitRace = TurretDef.RaceType;
        TurretUnit.SightRange = TurretDef.SightRange;
        TurretUnit.IsDetector = false; // under construction
        auto& TurretComp = EM.AddComponent<Game::Components::BuildingComponent>(Turret);
        TurretComp.IsBuilding = true;
        TurretComp.UnderConstruction = true;
        TurretComp.BuildingDefID = TurretDefID;
        TurretComp.TotalBuildTime = (float)TurretDef.BuildTime;
        TurretComp.BuildProgress = TurretComp.TotalBuildTime - 0.5f;

        const int BuilderScv = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(BuilderScv).Position = {600.0f - 32.0f, 600.0f};
        EM.AddComponent<Game::Components::PlayerComponent>(BuilderScv).PlayerID = 0;
        auto& BuilderScvUnit = EM.AddComponent<Game::Components::UnitComponent>(BuilderScv);
        BuilderScvUnit.ChkUnitID = 7;
        BuilderScvUnit.HitPoints = 60;
        TurretComp.BuilderUnitID = BuilderScv;

        Require(!TurretUnit.IsDetector,
                "Turret under construction does not act as detector");
        Require(!EM.GetComponent<Game::Components::CombatComponent>(Turret),
                "Turret under construction has no active combat component");

        // Complete construction
        BuildingSystem.Update(1.0f);
        Require(!TurretComp.UnderConstruction, "Turret finishes construction");
        Require(TurretUnit.IsDetector,
                "Completed Turret gains active detector status");
        Require(EM.GetComponent<Game::Components::CombatComponent>(Turret) != nullptr,
                "Completed Turret receives CombatComponent for defense");
        auto* BuilderCmd = EM.GetComponent<Game::Components::CommandComponent>(BuilderScv);
        Require(!BuilderCmd || BuilderCmd->CurrentCommand != Game::Components::CommandType::Harvest,
                "SCV stays idle upon non-gas building completion and does not harvest minerals");
    }

    std::printf("BuildingPlacementTest passed\n");
    return 0;
}
