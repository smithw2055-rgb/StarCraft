#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Core/Audio/AudioSystem.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Core::Audio
{
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags)
    {
        return -1;
    }
}

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

    int SpawnDefUnit(Core::ECS::EntityManager& EM,
                     const char* DefName,
                     Core::Math::Vec2 Pos,
                     int PlayerID,
                     bool CompletedBuilding = false)
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Pos;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.ShieldPoints = Def.ShieldPoints;
        Unit.MaxShieldPoints = Def.ShieldPoints;
        Unit.Armor = Def.Armor;
        Unit.SupplyCost = Def.SupplyCost;
        Unit.SizeType = Def.SizeType;
        Unit.Speed = Def.Speed;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
        Path.IsAir = Game::Data::IsAirUnitType(Def.ChkUnitID);
        auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.Visible = true;

        if (CompletedBuilding)
        {
            auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
            Building.IsBuilding = true;
            Building.UnderConstruction = false;
            Building.BuildingDefID = DefID;
        }

        if (Def.GroundDamage > 0 || Def.AirDamage > 0)
        {
            auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
            Game::Races::ApplyCombatDefinition(Combat, Def, Path.IsAir);
        }

        return EntityID;
    }

    void TestTransportCapacityAndSlotSizes(Core::ECS::EntityManager& EM)
    {
        constexpr int PlayerID = 0;

        // 1. Dropship: capacity 8
        const int Dropship = SpawnDefUnit(EM, "Dropship", {100.0f, 100.0f}, PlayerID);
        Require(Game::Systems::TransportSystem::IsTransport(EM, Dropship),
                "Dropship is recognized as a transport");

        // Slot sizes
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(0) == 1, "Marine takes 1 slot");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(7) == 1, "SCV takes 1 slot");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(2) == 2, "Vulture takes 2 slots");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(3) == 2, "Goliath takes 2 slots");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(5) == 4, "Tank Mode Tank takes 4 slots");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(30) == 0, "Siege Mode Tank cannot load (0 slots)");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(65) == 2, "Zealot takes 2 slots");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(66) == 4, "Dragoon takes 4 slots");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(83) == 4, "Reaver takes 4 slots");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(37) == 1, "Zergling takes 1 slot");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(38) == 2, "Hydralisk takes 2 slots");
        Require(Game::Systems::TransportSystem::GetUnitSpaceRequired(39) == 4, "Ultralisk takes 4 slots");

        // Load 1 Marine (1) + 1 SCV (1) + 1 Vulture (2) + 1 Tank (4) = 8 slots
        const int Marine = SpawnDefUnit(EM, "Marine", {100.0f, 100.0f}, PlayerID);
        const int Scv = SpawnDefUnit(EM, "SCV", {100.0f, 100.0f}, PlayerID);
        const int Vulture = SpawnDefUnit(EM, "Vulture", {100.0f, 100.0f}, PlayerID);
        const int Tank = SpawnDefUnit(EM, "SiegeTank", {100.0f, 100.0f}, PlayerID);

        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, Marine), "Marine loaded into Dropship");
        Require(Game::Systems::TransportSystem::GetLoadedSpace(EM, Dropship) == 1, "Dropship space is 1");

        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, Scv), "SCV loaded into Dropship");
        Require(Game::Systems::TransportSystem::GetLoadedSpace(EM, Dropship) == 2, "Dropship space is 2");

        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, Vulture), "Vulture loaded into Dropship");
        Require(Game::Systems::TransportSystem::GetLoadedSpace(EM, Dropship) == 4, "Dropship space is 4");

        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, Tank), "Tank loaded into Dropship");
        Require(Game::Systems::TransportSystem::GetLoadedSpace(EM, Dropship) == 8, "Dropship space is 8 (full)");

        // 9th slot attempt must fail
        const int ExtraMarine = SpawnDefUnit(EM, "Marine", {100.0f, 100.0f}, PlayerID);
        std::string FailReason;
        Require(!Game::Systems::TransportSystem::CanLoad(EM, Dropship, ExtraMarine, &FailReason),
                "Extra Marine cannot load when Dropship is at 8/8 capacity");
        Require(FailReason == "transport full", "Reason is transport full");

        // Unload all
        Require(Game::Systems::TransportSystem::UnloadUnit(EM, Dropship, 3), "Unloaded Tank");
        Require(Game::Systems::TransportSystem::UnloadUnit(EM, Dropship, 2), "Unloaded Vulture");
        Require(Game::Systems::TransportSystem::UnloadUnit(EM, Dropship, 1), "Unloaded SCV");
        Require(Game::Systems::TransportSystem::UnloadUnit(EM, Dropship, 0), "Unloaded Marine");
        Require(Game::Systems::TransportSystem::GetLoadedSpace(EM, Dropship) == 0, "Dropship space back to 0");

        // 2 Tanks (4+4 = 8) fill Dropship
        const int Tank2 = SpawnDefUnit(EM, "SiegeTank", {100.0f, 100.0f}, PlayerID);
        const int Tank3 = SpawnDefUnit(EM, "SiegeTank", {100.0f, 100.0f}, PlayerID);
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, Tank), "Tank 1 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, Tank2), "Tank 2 loaded");
        Require(Game::Systems::TransportSystem::GetLoadedSpace(EM, Dropship) == 8, "2 Tanks occupy 8 slots");
        Require(!Game::Systems::TransportSystem::CanLoad(EM, Dropship, Tank3, &FailReason),
                "Tank 3 cannot load into Dropship with 2 Tanks");

        // Siege Mode tank cannot board
        auto* Tank3Unit = EM.GetComponent<Game::Components::UnitComponent>(Tank3);
        Tank3Unit->ChkUnitID = 30; // Siege Mode
        Require(!Game::Systems::TransportSystem::CanLoad(EM, Dropship, Tank3, &FailReason),
                "Siege Mode Tank cannot board Dropship");

        // 2. Protoss Shuttle: capacity 8
        const int Shuttle = SpawnDefUnit(EM, "Shuttle", {200.0f, 200.0f}, PlayerID);
        const int Zealot1 = SpawnDefUnit(EM, "Zealot", {200.0f, 200.0f}, PlayerID);
        const int Zealot2 = SpawnDefUnit(EM, "Zealot", {200.0f, 200.0f}, PlayerID);
        const int Zealot3 = SpawnDefUnit(EM, "Zealot", {200.0f, 200.0f}, PlayerID);
        const int Zealot4 = SpawnDefUnit(EM, "Zealot", {200.0f, 200.0f}, PlayerID);
        const int Zealot5 = SpawnDefUnit(EM, "Zealot", {200.0f, 200.0f}, PlayerID);
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Shuttle, Zealot1), "Zealot 1 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Shuttle, Zealot2), "Zealot 2 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Shuttle, Zealot3), "Zealot 3 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Shuttle, Zealot4), "Zealot 4 loaded");
        Require(Game::Systems::TransportSystem::GetLoadedSpace(EM, Shuttle) == 8, "4 Zealots occupy 8 slots");
        Require(!Game::Systems::TransportSystem::CanLoad(EM, Shuttle, Zealot5, &FailReason),
                "5th Zealot cannot load into Shuttle");

        // 3. Zerg Overlord: requires Ventral Sacs
        const int Overlord = SpawnDefUnit(EM, "Overlord", {300.0f, 300.0f}, PlayerID);
        const int Ling = SpawnDefUnit(EM, "Zergling", {300.0f, 300.0f}, PlayerID);
        Require(!Game::Systems::TransportSystem::CanLoad(EM, Overlord, Ling, &FailReason),
                "Overlord cannot load before Ventral Sacs research");

        Game::Systems::UpgradeSystem::GetUpgrades(PlayerID).Levels[
            static_cast<size_t>(Game::Systems::ResearchId::Zerg_VentralSacs)] = 1;
        Require(Game::Systems::TransportSystem::CanLoad(EM, Overlord, Ling, &FailReason),
                "Overlord CAN load after Ventral Sacs research");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Overlord, Ling), "Zergling loaded into Overlord");

        std::printf("  [PASS] TestTransportCapacityAndSlotSizes\n");
    }

    void TestBunkerRulesAndCapacity(Core::ECS::EntityManager& EM)
    {
        constexpr int PlayerID = 0;
        const int Bunker = SpawnDefUnit(EM, "Bunker", {400.0f, 400.0f}, PlayerID, true);
        Require(Game::Systems::TransportSystem::IsTransport(EM, Bunker), "Bunker is recognized as a transport");

        // Test who can enter bunker
        Require(Game::Systems::TransportSystem::CanEnterBunker(0), "Marine can enter bunker");
        Require(Game::Systems::TransportSystem::CanEnterBunker(32), "Firebat can enter bunker");
        Require(Game::Systems::TransportSystem::CanEnterBunker(1), "Ghost can enter bunker");
        Require(Game::Systems::TransportSystem::CanEnterBunker(34), "Medic can enter bunker");
        Require(!Game::Systems::TransportSystem::CanEnterBunker(7), "SCV CANNOT enter bunker");
        Require(!Game::Systems::TransportSystem::CanEnterBunker(2), "Vulture CANNOT enter bunker");
        Require(!Game::Systems::TransportSystem::CanEnterBunker(5), "Tank CANNOT enter bunker");
        Require(!Game::Systems::TransportSystem::CanEnterBunker(3), "Goliath CANNOT enter bunker");
        Require(!Game::Systems::TransportSystem::CanEnterBunker(37), "Zergling CANNOT enter bunker");
        Require(!Game::Systems::TransportSystem::CanEnterBunker(65), "Zealot CANNOT enter bunker");

        const int SCV = SpawnDefUnit(EM, "SCV", {400.0f, 400.0f}, PlayerID);
        std::string FailReason;
        Require(!Game::Systems::TransportSystem::CanLoad(EM, Bunker, SCV, &FailReason),
                "SCV rejected from entering Bunker");
        Require(FailReason == "unit cannot enter bunker", "Reason is unit cannot enter bunker");

        // Load 4 infantry into Bunker
        const int M1 = SpawnDefUnit(EM, "Marine", {400.0f, 400.0f}, PlayerID);
        const int M2 = SpawnDefUnit(EM, "Marine", {400.0f, 400.0f}, PlayerID);
        const int M3 = SpawnDefUnit(EM, "Firebat", {400.0f, 400.0f}, PlayerID);
        const int M4 = SpawnDefUnit(EM, "Ghost", {400.0f, 400.0f}, PlayerID);
        const int M5 = SpawnDefUnit(EM, "Marine", {400.0f, 400.0f}, PlayerID);

        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, M1), "Marine 1 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, M2), "Marine 2 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, M3), "Firebat loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, M4), "Ghost loaded");
        Require(Game::Systems::TransportSystem::GetLoadedSpace(EM, Bunker) == 4, "Bunker has 4/4 occupied");

        Require(!Game::Systems::TransportSystem::CanLoad(EM, Bunker, M5, &FailReason),
                "5th infantry rejected when Bunker is full");
        Require(FailReason == "bunker full", "Reason is bunker full");

        std::printf("  [PASS] TestBunkerRulesAndCapacity\n");
    }

    void TestTransportDestructionPassengerLifecycleAndSupplySettlement(Core::ECS::EntityManager& EM)
    {
        constexpr int PlayerID = 1;
        auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerID);
        Resources.SupplyUsed = 0;
        Resources.SupplyMax = 200;

        // 1. Dropship (supply 2) + 4 Marines (supply 1 each = 4). Total supply = 6.
        const int Dropship = SpawnDefUnit(EM, "Dropship", {500.0f, 500.0f}, PlayerID);
        const int M1 = SpawnDefUnit(EM, "Marine", {500.0f, 500.0f}, PlayerID);
        const int M2 = SpawnDefUnit(EM, "Marine", {500.0f, 500.0f}, PlayerID);
        const int M3 = SpawnDefUnit(EM, "Marine", {500.0f, 500.0f}, PlayerID);
        const int M4 = SpawnDefUnit(EM, "Marine", {500.0f, 500.0f}, PlayerID);

        Resources.SupplyUsed = 2 + 1 + 1 + 1 + 1; // 6
        Require(Resources.SupplyUsed == 6, "Initial supply used is 6");

        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, M1), "M1 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, M2), "M2 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, M3), "M3 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, M4), "M4 loaded");

        const int Killer = SpawnDefUnit(EM, "Goliath", {600.0f, 600.0f}, 2);

        // Dropship is killed
        auto* DropshipUnit = EM.GetComponent<Game::Components::UnitComponent>(Dropship);
        DropshipUnit->HitPoints = 0;
        Game::Systems::RecordUnitDeath(EM, Dropship, Killer, true);
        EM.DestroyEntity(Dropship);

        // Verify Dropship and all 4 Marines are dead and destroyed
        Require(!EM.IsAlive(Dropship), "Dropship entity destroyed");
        Require(!EM.IsAlive(M1), "Passenger M1 destroyed");
        Require(!EM.IsAlive(M2), "Passenger M2 destroyed");
        Require(!EM.IsAlive(M3), "Passenger M3 destroyed");
        Require(!EM.IsAlive(M4), "Passenger M4 destroyed");

        // Verify supply was settled cleanly back to 0!
        Require(Resources.SupplyUsed == 0, "All 6 supply returned to player upon Dropship destruction");

        // 2. Bunker with 4 Marines
        const int Bunker = SpawnDefUnit(EM, "Bunker", {700.0f, 700.0f}, PlayerID, true);
        const int BM1 = SpawnDefUnit(EM, "Marine", {700.0f, 700.0f}, PlayerID);
        const int BM2 = SpawnDefUnit(EM, "Marine", {700.0f, 700.0f}, PlayerID);
        const int BM3 = SpawnDefUnit(EM, "Marine", {700.0f, 700.0f}, PlayerID);
        const int BM4 = SpawnDefUnit(EM, "Marine", {700.0f, 700.0f}, PlayerID);
        Resources.SupplyUsed = 4;

        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, BM1), "BM1 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, BM2), "BM2 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, BM3), "BM3 loaded");
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, BM4), "BM4 loaded");

        auto* BunkerUnit = EM.GetComponent<Game::Components::UnitComponent>(Bunker);
        BunkerUnit->HitPoints = 0;
        Game::Systems::RecordUnitDeath(EM, Bunker, Killer, true);
        EM.DestroyEntity(Bunker);

        Require(!EM.IsAlive(Bunker), "Bunker destroyed");
        Require(!EM.IsAlive(BM1), "Bunker passenger BM1 destroyed");
        Require(!EM.IsAlive(BM2), "Bunker passenger BM2 destroyed");
        Require(!EM.IsAlive(BM3), "Bunker passenger BM3 destroyed");
        Require(!EM.IsAlive(BM4), "Bunker passenger BM4 destroyed");
        Require(Resources.SupplyUsed == 0, "All 4 supply returned upon Bunker destruction");

        std::printf("  [PASS] TestTransportDestructionPassengerLifecycleAndSupplySettlement\n");
    }

    void TestBunkerAndTransportCombatRules()
    {
        constexpr int FriendlyPlayer = 0;
        constexpr int EnemyPlayer = 1;

        Core::ECS::SystemManager SysMgr;
        auto& EM = SysMgr.GetEntityManager();
        Game::Map::GameMap Map;
        Map.Generate(32, 32);

        Game::Systems::BulletSystem BulletSys(SysMgr);
        Game::Systems::CombatSystem CombatSys(SysMgr, BulletSys, &Map);

        // 1. Bunker with Marine
        const int Bunker = SpawnDefUnit(EM, "Bunker", {400.0f, 400.0f}, FriendlyPlayer, true);
        const int BunkerMarine = SpawnDefUnit(EM, "Marine", {400.0f, 400.0f}, FriendlyPlayer);
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Bunker, BunkerMarine), "Marine loaded into Bunker");

        // 2. Dropship with Marine
        const int Dropship = SpawnDefUnit(EM, "Dropship", {800.0f, 800.0f}, FriendlyPlayer);
        const int DropshipMarine = SpawnDefUnit(EM, "Marine", {800.0f, 800.0f}, FriendlyPlayer);
        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, DropshipMarine), "Marine loaded into Dropship");

        // 3. Enemy dragoon near bunker: dragoon attacks bunker, cannot target BunkerMarine directly
        const int EnemyDragoon = SpawnDefUnit(EM, "Dragoon", {400.0f, 450.0f}, EnemyPlayer);

        // Enemy near dropship:
        const int EnemyScout = SpawnDefUnit(EM, "Dragoon", {800.0f, 850.0f}, EnemyPlayer);

        // Update combat
        CombatSys.Update(0.1f);

        auto* DragoonCombat = EM.GetComponent<Game::Components::CombatComponent>(EnemyDragoon);
        Require(DragoonCombat && DragoonCombat->TargetEntityID == Bunker,
                "Dragoon auto-targets the Bunker itself, not the passenger inside");

        auto* ScoutCombat = EM.GetComponent<Game::Components::CombatComponent>(EnemyScout);
        Require(ScoutCombat && ScoutCombat->TargetEntityID == Dropship,
                "Enemy auto-targets the Dropship itself, not the passenger inside");

        // Marine in dropship does NOT fire or acquire target
        auto* DropshipCombat = EM.GetComponent<Game::Components::CombatComponent>(DropshipMarine);
        Require(!DropshipCombat || DropshipCombat->TargetEntityID == -1,
                "Dropship Marine cannot initiate combat");

        // 4. Bunker Marine extended range test (+32px bunker bonus)
        // Marine range: 128px + 32px bunker bonus = 160px.
        // Place an enemy at distance 145px from bunker:
        const int DistantZergling = SpawnDefUnit(EM, "Zergling", {400.0f, 400.0f + 145.0f}, EnemyPlayer);
        // Clear dragoon
        EM.DestroyEntity(EnemyDragoon);

        CombatSys.Update(0.1f);
        auto* BunkerMarineCombat = EM.GetComponent<Game::Components::CombatComponent>(BunkerMarine);
        Require(BunkerMarineCombat && BunkerMarineCombat->TargetEntityID == DistantZergling,
                "Bunker Marine acquired enemy at 145px using extended bunker range");

        std::printf("  [PASS] TestBunkerAndTransportCombatRules\n");
    }

    void TestBuildingPlacementFogOfWar()
    {
        Core::ECS::SystemManager SysMgr;
        auto& EM = SysMgr.GetEntityManager();
        Game::Map::GameMap Map;
        Map.Generate(64, 64);
        Game::Systems::FogOfWarSystem Fog(SysMgr, &Map);

        Game::Systems::BuildingSystem::SetMap(&Map);
        Game::Systems::BuildingSystem::SetFogOfWarSystem(&Fog);

        const int BarracksDefID = Game::Races::GetUnitDefID("Barracks");
        const auto BuildPos = Map.TileToWorld(10, 10);

        auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
        Resources.Minerals = 1000;

        // Tile (10, 10) is unexplored -> placement must fail
        Require(!Fog.IsExplored(10, 10, 0), "Tile (10, 10) is unexplored initially");
        Require(!Game::Systems::BuildingSystem::IsPlacementValid(EM, BarracksDefID, BuildPos, 0),
                "Building placement fails on unexplored fog");

        // Reveal area around (10, 10)
        // Spawn an SCV with sight 7 at (6, 10) (outside Barracks footprint) and update Fog
        const auto ScoutPos = Map.TileToWorld(6, 10);
        const int ScoutScv = SpawnDefUnit(EM, "SCV", ScoutPos, 0);
        Fog.Update(0.1f);

        Require(Fog.IsExplored(10, 10, 0), "Tile (10, 10) is explored after SCV reveals it");
        Require(Game::Systems::BuildingSystem::IsPlacementValid(EM, BarracksDefID, BuildPos, 0),
                "Building placement succeeds once area is explored");

        // Clean up
        EM.DestroyEntity(ScoutScv);
        Game::Systems::BuildingSystem::SetFogOfWarSystem(nullptr);

        std::printf("  [PASS] TestBuildingPlacementFogOfWar\n");
    }

    void TestSpiderMineMechanics()
    {
        Core::ECS::SystemManager SysMgr;
        auto& EM = SysMgr.GetEntityManager();
        Game::Systems::BulletSystem BulletSys(SysMgr);
        Game::Systems::NukeAndSpiderMineSystem MineSys(SysMgr, BulletSys);

        // Research Spider Mines for Player 0
        Game::Systems::UpgradeSystem::GetUpgrades(0).Levels[
            static_cast<size_t>(Game::Systems::ResearchId::Terran_SpiderMines)] = 1;

        // Spawn a Vulture (Player 0) at (100, 100)
        const int Vulture = SpawnDefUnit(EM, "Vulture", {100.0f, 100.0f}, 0);
        Require(MineSys.PlaceSpiderMine(Vulture), "Vulture places spider mine");

        // Inventory should decrease from 3 to 2
        auto* Inv = EM.GetComponent<Game::Components::VultureMineInventoryComponent>(Vulture);
        Require(Inv && Inv->RemainingMines == 2, "Vulture remaining mines decremented to 2");

        // Advance 1.1s to allow the mine entity to be created
        MineSys.Update(1.1f);
        // Advance another 1.1s to allow the mine to finish burrowing
        MineSys.Update(1.1f);

        // Find the mine entity
        int MineID = -1;
        for (int Eid : EM.GetEntitiesWith<Game::Components::SpiderMineComponent>())
        {
            MineID = Eid;
            break;
        }
        Require(MineID >= 0, "Spider mine entity spawned");
        auto* MineComp = EM.GetComponent<Game::Components::SpiderMineComponent>(MineID);
        Require(MineComp && MineComp->IsBurrowed, "Spider mine is burrowed");
        auto* Cloak = EM.GetComponent<Game::Components::CloakComponent>(MineID);
        Require(Cloak && Cloak->IsBurrowed && Cloak->IsCloaked, "Burrowed spider mine has active cloak component");

        // 1. Hover unit test: Archon (Player 1) at (120, 100)
        const int Archon = SpawnDefUnit(EM, "Archon", {120.0f, 100.0f}, 1);
        MineSys.Update(0.1f);
        Require(!MineComp->IsTriggered, "Hover unit (Archon) does not trigger spider mine");

        // 1b. Hover unit test: Dark Archon (Player 1) at (120, 100)
        const int DarkArchon = SpawnDefUnit(EM, "Dark Archon", {120.0f, 100.0f}, 1);
        (void)DarkArchon;
        MineSys.Update(0.1f);
        Require(!MineComp->IsTriggered, "Hover unit (Dark Archon) does not trigger spider mine");

        // 2. Air unit test: Mutalisk (Player 1) at (110, 100)
        const int Mutalisk = SpawnDefUnit(EM, "Mutalisk", {110.0f, 100.0f}, 1);
        (void)Mutalisk;
        MineSys.Update(0.1f);
        Require(!MineComp->IsTriggered, "Air unit (Mutalisk) does not trigger spider mine");

        // 3. Worker test: Probe (Player 1) at (105, 100)
        const int Probe = SpawnDefUnit(EM, "Probe", {105.0f, 100.0f}, 1);
        (void)Probe;
        MineSys.Update(0.1f);
        Require(!MineComp->IsTriggered, "Hover worker (Probe) does not trigger spider mine");

        // 4. Undetected cloaked unit: Dark Templar (Player 1) at (115, 100)
        const int DarkTemplar = SpawnDefUnit(EM, "DarkTemplar", {115.0f, 100.0f}, 1);
        auto& DTCloak = EM.AddComponent<Game::Components::CloakComponent>(DarkTemplar);
        DTCloak.IsCloaked = true;
        DTCloak.IsPassiveCloak = true;
        MineSys.Update(0.1f);
        Require(!MineComp->IsTriggered, "Undetected cloaked unit does not trigger spider mine");

        // 5. Valid trigger: Zealot (Player 1) at (120, 100)
        const int Zealot = SpawnDefUnit(EM, "Zealot", {120.0f, 100.0f}, 1);
        auto* ZealotUnit = EM.GetComponent<Game::Components::UnitComponent>(Zealot);
        const int InitialZealotHP = ZealotUnit->HitPoints;
        const int InitialArchonShield = EM.GetComponent<Game::Components::UnitComponent>(Archon)->ShieldPoints;

        MineSys.Update(0.1f);
        Require(MineComp->IsTriggered, "Zealot triggers spider mine");
        Require(!Cloak->IsBurrowed && !Cloak->IsCloaked, "Triggered spider mine unburrows and uncloaks");

        // Update mine chase and detonation: mine moves at 400px/s, distance is 20px -> detonates in 0.2s
        MineSys.Update(0.2f);
        Require(!EM.IsAlive(MineID), "Spider mine detonated and destroyed itself");
        Require(ZealotUnit->HitPoints < InitialZealotHP, "Zealot took damage from spider mine");
        // Archon (within 64px splash radius) should also take radial splash damage!
        auto* ArchonUnit = EM.GetComponent<Game::Components::UnitComponent>(Archon);
        Require(ArchonUnit->ShieldPoints < InitialArchonShield, "Hover Archon caught in splash takes radial damage");

        std::printf("  [PASS] TestSpiderMineMechanics\n");
    }

    void TestCombatDetectorCoverage()
    {
        Core::ECS::SystemManager SysMgr;
        auto& EM = SysMgr.GetEntityManager();
        Game::Map::GameMap Map;
        Map.Generate(64, 64);
        Game::Systems::FogOfWarSystem Fog(SysMgr, &Map);
        Game::Systems::BulletSystem BulletSys(SysMgr);
        Game::Systems::CombatSystem Combat(SysMgr, BulletSys, &Map);

        Game::Systems::CombatSystem::SetFogOfWarSystem(&Fog);

        // Spawn a friendly Marine (Player 0) at (100, 100)
        const int Marine = SpawnDefUnit(EM, "Marine", {100.0f, 100.0f}, 0);
        auto* MarineCombat = EM.GetComponent<Game::Components::CombatComponent>(Marine);
        Require(MarineCombat != nullptr, "Marine has combat component");

        // Spawn an enemy Dark Templar (Player 1, cloaked) at (160, 100)
        const int DarkTemplar = SpawnDefUnit(EM, "DarkTemplar", {160.0f, 100.0f}, 1);
        auto& DTCloak = EM.AddComponent<Game::Components::CloakComponent>(DarkTemplar);
        DTCloak.IsCloaked = true;
        DTCloak.IsPassiveCloak = true;

        // Fog update without detector
        Fog.Update(0.1f);
        Combat.Update(0.1f);

        Require(MarineCombat->TargetEntityID != DarkTemplar &&
                MarineCombat->AutoTargetEntityID != DarkTemplar,
                "Marine cannot target undetected cloaked Dark Templar");

        // Spawn a friendly detector: Science Vessel (Player 0, IsDetector=true, SightRange=10) at (130, 100)
        const int ScienceVessel = SpawnDefUnit(EM, "ScienceVessel", {130.0f, 100.0f}, 0);
        auto* VesselUnit = EM.GetComponent<Game::Components::UnitComponent>(ScienceVessel);
        VesselUnit->IsDetector = true;

        Fog.Update(0.1f);
        Combat.Update(0.1f);

        Require(MarineCombat->TargetEntityID == DarkTemplar ||
                MarineCombat->AutoTargetEntityID == DarkTemplar,
                "Marine auto-acquires cloaked Dark Templar once detected by Science Vessel");

        // Destroy Science Vessel
        EM.DestroyEntity(ScienceVessel);
        Fog.Update(0.1f);
        Combat.Update(0.1f);

        Require(MarineCombat->TargetEntityID != DarkTemplar &&
                MarineCombat->AutoTargetEntityID != DarkTemplar,
                "Marine clears target once detector coverage is lost");

        Game::Systems::CombatSystem::SetFogOfWarSystem(nullptr);
        std::printf("  [PASS] TestCombatDetectorCoverage\n");
    }

    void TestTransportUnloadTerrainValidation()
    {
        Core::ECS::SystemManager SysMgr;
        auto& EM = SysMgr.GetEntityManager();
        Game::Map::GameMap Map;
        Map.Generate(16, 16);

        // Make top half of map unwalkable (water / abyss)
        for (int Y = 0; Y < 8; ++Y)
        {
            for (int X = 0; X < 16; ++X)
            {
                Game::Map::Tile T = Map.GetTile(X, Y);
                T.Walkable = false;
                Map.SetTile(X, Y, T);
            }
        }

        Game::Systems::TransportSystem::SetMap(&Map);

        // Dropship flying over deep water at tile (8, 3) (far from any walkable land)
        const auto DeepWaterPos = Map.TileToWorld(8, 3);
        const int WaterDropship = SpawnDefUnit(EM, "Dropship", DeepWaterPos, 0);
        const int WaterMarine = SpawnDefUnit(EM, "Marine", DeepWaterPos, 0);
        Require(Game::Systems::TransportSystem::LoadUnit(EM, WaterDropship, WaterMarine), "Marine loaded into water dropship");

        std::string FailReason;
        Require(!Game::Systems::TransportSystem::CanUnload(EM, WaterDropship, 0, &FailReason),
                "CanUnload rejected over deep water");
        Require(FailReason == "terrain is impassable", "CanUnload reason is 'terrain is impassable'");

        Require(!Game::Systems::TransportSystem::UnloadUnit(EM, WaterDropship, 0),
                "UnloadUnit fails when transport is completely over unwalkable terrain");

        // Move Dropship to tile (8, 12) (over solid walkable land)
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(WaterDropship);
        Trans->Position = Map.TileToWorld(8, 12);

        Require(Game::Systems::TransportSystem::CanUnload(EM, WaterDropship, 0),
                "CanUnload accepted over walkable land");
        Require(Game::Systems::TransportSystem::UnloadUnit(EM, WaterDropship, 0),
                "UnloadUnit succeeds over walkable land");

        auto* MarineTrans = EM.GetComponent<Game::Components::TransformComponent>(WaterMarine);
        int MarineTileX = 0, MarineTileY = 0;
        Map.WorldToTile(MarineTrans->Position, MarineTileX, MarineTileY);
        Require(Map.IsWalkable(MarineTileX, MarineTileY),
                "Unloaded passenger is placed on a walkable tile");

        Game::Systems::TransportSystem::SetMap(nullptr);
        std::printf("  [PASS] TestTransportUnloadTerrainValidation\n");
    }
}

int main()
{
    std::printf("Running TransportSystemTest...\n");

    {
        Core::ECS::EntityManager EM;
        TestTransportCapacityAndSlotSizes(EM);
    }
    {
        Core::ECS::EntityManager EM;
        TestBunkerRulesAndCapacity(EM);
    }
    {
        Core::ECS::EntityManager EM;
        TestTransportDestructionPassengerLifecycleAndSupplySettlement(EM);
    }
    TestBunkerAndTransportCombatRules();
    TestBuildingPlacementFogOfWar();
    TestSpiderMineMechanics();
    TestCombatDetectorCoverage();
    TestTransportUnloadTerrainValidation();

    std::printf("TransportSystemTest passed successfully!\n");
    return 0;
}
