#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#include "Client/Selection.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Races/RaceDef.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Resource/GameDataLoader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace Core::Audio
{
    struct AudioSystem::Impl {};
    AudioSystem::AudioSystem() = default;
    AudioSystem::~AudioSystem() = default;
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags) { return 0; }
    void AudioSystem::Update(float) {}
    void AudioSystem::SetSoundType(int, const SoundType&) {}
    void AudioSystem::SetSoundPath(int, std::string) {}
}

namespace Resource::Graphics
{
    Resource::GameDataLoader* DataAssetCacheView::GetDataLoader() const { return nullptr; }
}

namespace Resource
{
    const UnitEntry* GameDataLoader::FindUnitByID(uint32_t) const { return nullptr; }
    const SpriteEntry* GameDataLoader::FindSpriteByID(uint32_t) const { return nullptr; }
    uint32_t GameDataLoader::ResolveUnitSpriteID(uint32_t) const { return UINT32_MAX; }
}

namespace Core::Platform
{
    bool IsKeyDown(int) { return false; }
    bool IsMouseButtonDown(int) { return false; }
    bool IsMouseButtonPressed(int) { return false; }
    bool IsMouseButtonReleased(int) { return false; }
    float GetMouseX() { return -1.0f; }
    float GetMouseY() { return -1.0f; }
    bool IsWindowFocused() { return true; }
    bool IsMouseInClientArea() { return true; }
    bool IsMouseInGameContentArea() { return true; }
    double GetAbsoluteTime() { return 0.0; }
}

namespace
{

void Require(bool Condition, const char* Message)
{
    if (Condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message);
    std::exit(1);
}

void TestPhase15SharedSpatialBroadphaseAcrossCombatBulletProtossAndMines()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    Game::Map::GameMap Map;
    Map.Generate(64, 64);
    Game::SimulationContext::Get(EM).Map = &Map;
    Game::SimulationContext::Get(EM).MutableMap = &Map;

    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));
    auto BulletSys = std::make_unique<Game::Systems::BulletSystem>(SM);
    auto* BulletRaw = BulletSys.get();
    SM.RegisterSystem(std::move(BulletSys));
    SM.RegisterSystem(std::make_unique<Game::Systems::CombatSystem>(SM, *BulletRaw, &Map));
    SM.RegisterSystem(std::make_unique<Game::Systems::ProtossSystem>(SM, &Map));
    SM.RegisterSystem(std::make_unique<Game::Systems::NukeAndSpiderMineSystem>(SM, *BulletRaw));

    // Spawn a Protoss Pylon (ChkUnitID 156) + Shield Battery (ChkUnitID 172) + wounded Zealot
    const int Pylon = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Pylon).Position = {256.0f, 256.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Pylon).PlayerID = 0;
    auto& PylonU = EM.AddComponent<Game::Components::UnitComponent>(Pylon);
    PylonU.ChkUnitID = 156;
    PylonU.HitPoints = 300;
    PylonU.UnitRace = Game::Components::Race::Protoss;
    auto& PylonB = EM.AddComponent<Game::Components::BuildingComponent>(Pylon);
    PylonB.UnderConstruction = false;

    const int Battery = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Battery).Position = {288.0f, 256.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Battery).PlayerID = 0;
    auto& BatU = EM.AddComponent<Game::Components::UnitComponent>(Battery);
    BatU.ChkUnitID = 172;
    BatU.HitPoints = 200;
    BatU.EnergyPoints = 100;
    BatU.MaxEnergyPoints = 200;
    BatU.UnitRace = Game::Components::Race::Protoss;
    auto& BatB = EM.AddComponent<Game::Components::BuildingComponent>(Battery);
    BatB.UnderConstruction = false;
    BatB.RequiresPylon = true;
    BatB.HasPylonPower = true;

    const int Zealot = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Zealot).Position = {310.0f, 256.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Zealot).PlayerID = 0;
    auto& ZealU = EM.AddComponent<Game::Components::UnitComponent>(Zealot);
    ZealU.ChkUnitID = 65;
    ZealU.HitPoints = 100;
    ZealU.ShieldPoints = 10;
    ZealU.MaxShieldPoints = 60;
    EM.AddComponent<Game::Components::CombatComponent>(Zealot).GroundDamage = 16;

    const int Mineral = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Mineral).Position = {128.0f, 128.0f};
    EM.AddComponent<Game::Components::ResourceComponent>(Mineral).Amount = 1500;

    // Step simulation: WorldSpatialIndexSystem builds SpatialIndexResource once;
    // BulletSystem, CombatSystem, and ProtossSystem must all reuse it without rebuilding!
    SM.UpdateSimulationStep(0.5f);

    const auto* SpatialRes = EM.GetResource<Game::Pathfinding::SpatialIndexResource>();
    Require(SpatialRes != nullptr, "SpatialIndexResource exists on WorldContext");
    Require(SpatialRes->RebuildCount == 1u,
            "SpatialIndexResource was built exactly once during the tick (eliminated redundant per-system CSR rebuilds)");
    Require(SpatialRes->SharedQueryReuses >= 3u,
            "BulletSystem, CombatSystem, and ProtossSystem shared the CSR SpatialIndexResource in O(1)");
    Require(!SpatialRes->BuildingsHash.Empty() && !SpatialRes->ResourcesHash.Empty(),
            "SpatialIndexResource partitioned BuildingsHash and ResourcesHash CSR layers");
    Require(ZealU.ShieldPoints > 10,
            "ProtossSystem Shield Battery recharged nearby Zealot shields using shared SpatialIndexResource");
}

void TestPhase16MultiWorldProtossAndZergIsolationWithNullStaticMap()
{
    Core::ECS::SystemManager SysA;
    Core::ECS::SystemManager SysB;
    auto& WorldA = SysA.GetEntityManager();
    auto& WorldB = SysB.GetEntityManager();

    Game::Map::GameMap MapA;
    MapA.Generate(32, 32);
    Game::Map::GameMap MapB;
    MapB.Generate(32, 32);

    // Explicitly null out static sMap pointers on both ProtossSystem and ZergSystem!
    Game::Systems::ProtossSystem::SetMap(nullptr);
    Game::Systems::ZergSystem::SetMap(nullptr);

    Game::SimulationContext::Get(WorldA).Map = &MapA;
    Game::SimulationContext::Get(WorldA).MutableMap = &MapA;
    Game::SimulationContext::Get(WorldB).Map = &MapB;
    Game::SimulationContext::Get(WorldB).MutableMap = &MapB;

    auto ProtossA = std::make_unique<Game::Systems::ProtossSystem>(SysA, nullptr);
    auto ProtossB = std::make_unique<Game::Systems::ProtossSystem>(SysB, nullptr);
    auto ZergA    = std::make_unique<Game::Systems::ZergSystem>(SysA, nullptr);
    auto ZergB    = std::make_unique<Game::Systems::ZergSystem>(SysB, nullptr);

    SysA.RegisterSystem(std::move(ProtossA));
    SysA.RegisterSystem(std::move(ZergA));
    SysB.RegisterSystem(std::move(ProtossB));
    SysB.RegisterSystem(std::move(ZergB));

    // Clear static sMap again after construction to ensure Update() strictly uses SimulationContext
    Game::Systems::ProtossSystem::SetMap(nullptr);
    Game::Systems::ZergSystem::SetMap(nullptr);

    // Spawn Pylon at tile (10, 10) in WorldA only, and Gateway at (11, 10) in both WorldA and WorldB
    const int PylonA = WorldA.CreateEntity();
    WorldA.AddComponent<Game::Components::TransformComponent>(PylonA).Position = MapA.TileToWorld(10, 10);
    auto& PU = WorldA.AddComponent<Game::Components::UnitComponent>(PylonA);
    PU.ChkUnitID = 156;
    PU.HitPoints = 300;
    PU.UnitRace = Game::Components::Race::Protoss;
    WorldA.AddComponent<Game::Components::BuildingComponent>(PylonA).UnderConstruction = false;

    const int GatewayA = WorldA.CreateEntity();
    WorldA.AddComponent<Game::Components::TransformComponent>(GatewayA).Position = MapA.TileToWorld(11, 10);
    WorldA.AddComponent<Game::Components::UnitComponent>(GatewayA).UnitRace = Game::Components::Race::Protoss;
    auto& GWA = WorldA.AddComponent<Game::Components::BuildingComponent>(GatewayA);
    GWA.RequiresPylon = true;
    GWA.UnderConstruction = false;

    const int GatewayB = WorldB.CreateEntity();
    WorldB.AddComponent<Game::Components::TransformComponent>(GatewayB).Position = MapB.TileToWorld(11, 10);
    WorldB.AddComponent<Game::Components::UnitComponent>(GatewayB).UnitRace = Game::Components::Race::Protoss;
    auto& GWB = WorldB.AddComponent<Game::Components::BuildingComponent>(GatewayB);
    GWB.RequiresPylon = true;
    GWB.UnderConstruction = false;

    // Seed creep at tile (20, 20) in MapA only, not in MapB
    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            auto TileA = MapA.GetTile(20 + dx, 20 + dy);
            TileA.HasCreep = true;
            MapA.SetTile(20 + dx, 20 + dy, TileA);
        }
    }

    SysA.UpdateSimulationStep(0.1f);
    SysB.UpdateSimulationStep(0.1f);

    Require(Game::Systems::ProtossSystem::HasPower(WorldA, 11, 10) && GWA.HasPylonPower,
            "WorldA ProtossPowerGridResource powers GatewayA via SimulationContext even when sMap is nullptr");
    Require(!Game::Systems::ProtossSystem::HasPower(WorldB, 11, 10) && !GWB.HasPylonPower,
            "WorldB ProtossPowerGridResource remains unpowered with zero cross-world leakage");

    Require(Game::Systems::ZergSystem::HasCreep(WorldA, 20, 20),
            "ZergSystem::HasCreep(WorldA, 20, 20) resolves true from WorldA SimulationContext");
    Require(!Game::Systems::ZergSystem::HasCreep(WorldB, 20, 20),
            "ZergSystem::HasCreep(WorldB, 20, 20) resolves false from WorldB SimulationContext");
}

void TestPhase17And18SelectionGenerationalControlGroupSafety()
{
    Core::ECS::EntityManager EM;
    Game::Client::Selection SelA;
    Game::Client::Selection SelB;
    SelA.SetLocalPlayerId(0);
    SelB.SetLocalPlayerId(1);

    auto SpawnUnit = [&](int PlayerID, Core::Math::Vec2 Pos) -> int {
        const int Id = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Id).Position = Pos;
        auto& U = EM.AddComponent<Game::Components::UnitComponent>(Id);
        U.ChkUnitID = 0; // Marine
        U.HitPoints = 40;
        U.MaxHitPoints = 40;
        EM.AddComponent<Game::Components::PlayerComponent>(Id).PlayerID = PlayerID;
        EM.AddComponent<Game::Components::RenderComponent>(Id).Visible = true;
        return Id;
    };

    const int Marine1 = SpawnUnit(0, {100.0f, 100.0f});
    const int Marine2 = SpawnUnit(0, {132.0f, 100.0f});

    SelA.SetSelectedWithHandles(EM, {Marine1, Marine2});
    Require(SelA.Count() == 2 && SelA.GetSelectedHandles().size() == 2u,
            "Selection tracks generational EntityHandles for selected units");

    // Save to Control Group 1
    SelA.SaveControlGroup(1, EM);
    Require(!SelA.IsControlGroupEmpty(1), "Control group 1 saved 2 Marines");

    // Now Marine1 dies and its EntityID slot is recycled for a new friendly unit!
    const uint32_t OldGen = EM.GetGeneration(Marine1);
    EM.DestroyEntity(Marine1);
    EM.CreateEntityWithID(Marine1, OldGen + 1);
    EM.AddComponent<Game::Components::TransformComponent>(Marine1).Position = {500.0f, 500.0f};
    auto& RecycledUnit = EM.AddComponent<Game::Components::UnitComponent>(Marine1);
    RecycledUnit.ChkUnitID = 0;
    RecycledUnit.HitPoints = 40;
    RecycledUnit.MaxHitPoints = 40;
    EM.AddComponent<Game::Components::PlayerComponent>(Marine1).PlayerID = 0;
    EM.AddComponent<Game::Components::RenderComponent>(Marine1).Visible = true;

    // Without generational EntityHandle checking, PruneAgainst and RecallControlGroup would
    // falsely keep/recall the unrelated unit that reused Marine1's ID slot!
    SelA.PruneAgainst(EM);
    Require(SelA.Count() == 1 && SelA.Get(0) == Marine2,
            "Selection::PruneAgainst pruned recycled ABA slot (Marine1) via EntityHandle generation check");

    SelA.RecallControlGroup(1, EM);
    Require(SelA.Count() == 1 && SelA.Get(0) == Marine2,
            "Selection::RecallControlGroup rejected recycled ABA slot (Marine1) and recalled only surviving Marine2");

    // Verify AppendControlGroup can append the newly spawned unit occupying recycled slot Marine1
    SelA.SetSelectedWithHandles(EM, {Marine1});
    SelA.AppendControlGroup(1, EM);
    SelA.RecallControlGroup(1, EM);
    Require(SelA.Count() == 2,
            "Selection::AppendControlGroup pruned stale handle and appended new unit with recycled slot ID");
}

} // namespace

int main()
{
    TestPhase15SharedSpatialBroadphaseAcrossCombatBulletProtossAndMines();
    TestPhase16MultiWorldProtossAndZergIsolationWithNullStaticMap();
    TestPhase17And18SelectionGenerationalControlGroupSafety();

    std::cout << "[Phase15To18ModernEngineRefactorTest] All Phase 15-18 modern engine refactorings (Shared CSR SpatialBroadphase, Multi-World Protoss/Zerg Isolation, Cached Queries, and Generational Selection Control Groups) passed.\n";
    return 0;
}
