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
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Main/GameLoop/CursorManager.h"
#include "UI/Hud.h"

#include <cstdlib>
#include <iostream>
#include <vector>

#include "Resource/GameDataLoader.h"
#include "Resource/UIAssetCacheView.h"
#include "Render/Draw2D.h"

namespace Resource::Graphics
{
    Resource::GameDataLoader* DataAssetCacheView::GetDataLoader() const { return nullptr; }
    const std::vector<GpuImage>* UIAssetCacheView::GetCursorFrames(const std::string&) const { return nullptr; }
}

namespace Resource
{
    const UnitEntry* GameDataLoader::FindUnitByID(uint32_t) const { return nullptr; }
    const SpriteEntry* GameDataLoader::FindSpriteByID(uint32_t) const { return nullptr; }
    uint32_t GameDataLoader::ResolveUnitSpriteID(uint32_t) const { return UINT32_MAX; }
}

namespace Render
{
    void Draw2D::DrawSpriteFromImage(const GpuTextureHandle&, float, float, float, float, Core::Color) {}
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
    void SetMouseCursorVisible(bool) {}
}

#include "Main/GameLoop/EcsSync.h"

namespace Game::Client::UI
{
    bool Hud::IsPointInHudBand(int, int, int, int) const { return false; }
    bool Hud::CheckMinimapClick(int, int, int, int, int, int, float&, float&) const { return false; }
    int Hud::CheckCommandPanelClick(int, int, int, int) const { return -1; }
    int Hud::GetHudHeight(int, int) const { return kHudHeight; }
}

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

namespace
{
void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::cerr << "FAIL: " << Message << "\n";
        std::exit(1);
    }
}

void TestPhase27MovementSystemCachedQueriesAndSharedSpatialFallback()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();
    Game::Map::GameMap Map;
    Map.Generate(64, 64);

    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));
    SM.RegisterSystem(std::make_unique<Game::Systems::MovementSystem>(SM, &Map));

    const int Marine = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Marine).Position = {256.0f, 256.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Marine).PlayerID = 0;
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(Marine);
    Unit.ChkUnitID = 0;
    Unit.HitPoints = 40;
    Unit.MaxHitPoints = 40;
    Unit.Speed = 4.0f;
    auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(Marine);
    Path.Path.push_back({320.0f, 256.0f});
    Path.PathIndex = 0;
    auto& Cmd = EM.AddComponent<Game::Components::CommandComponent>(Marine);
    Cmd.CurrentCommand = Game::Components::CommandType::Move;
    Cmd.TargetPosition = {320.0f, 256.0f};

    SM.UpdateSimulationStep(0.05f);

    const auto& CachedMoveEnts = EM.GetEntitiesWithCached<Game::Components::TransformComponent, Game::Components::PathfindingComponent>();
    const auto& CachedUnitEnts = EM.GetEntitiesWithCached<Game::Components::TransformComponent, Game::Components::UnitComponent>();
    Require(CachedMoveEnts.size() == 1 && CachedMoveEnts[0] == Marine,
            "MovementSystem cached query for TransformComponent + PathfindingComponent populated");
    Require(CachedUnitEnts.size() == 1 && CachedUnitEnts[0] == Marine,
            "MovementSystem cached query for TransformComponent + UnitComponent populated");
}

void TestPhase28HarvestAndProductionSpatialBroadphase()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();
    Game::Map::GameMap Map;
    Map.Generate(128, 128);
    Game::Systems::ProductionSystem::SetMap(&Map);

    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));

    // Create a mineral patch at (220, 300)
    const int Mineral = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Mineral).Position = {220.0f, 300.0f};
    auto& MineralUnit = EM.AddComponent<Game::Components::UnitComponent>(Mineral);
    MineralUnit.ChkUnitID = 176;
    MineralUnit.HitPoints = 1000;
    auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(Mineral);
    Res.Type = Game::Components::ResourceType::Minerals;
    Res.Amount = 1500;
    Res.IsDepleted = false;

    // Create a completed Command Center at (300, 300)
    const int Depot = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Depot).Position = {300.0f, 300.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Depot).PlayerID = 0;
    auto& DepotUnit = EM.AddComponent<Game::Components::UnitComponent>(Depot);
    DepotUnit.ChkUnitID = 106;
    DepotUnit.HitPoints = 1500;
    DepotUnit.MaxHitPoints = 1500;
    auto& DepotBldg = EM.AddComponent<Game::Components::BuildingComponent>(Depot);
    DepotBldg.UnderConstruction = false;

    // Create an idle SCV near the depot waiting to resume harvesting
    const int NearbySCV = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(NearbySCV).Position = {360.0f, 300.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(NearbySCV).PlayerID = 0;
    auto& ScvUnit = EM.AddComponent<Game::Components::UnitComponent>(NearbySCV);
    ScvUnit.ChkUnitID = 7;
    ScvUnit.HitPoints = 60;
    ScvUnit.MaxHitPoints = 60;
    EM.AddComponent<Game::Components::PathfindingComponent>(NearbySCV);
    EM.AddComponent<Game::Components::CommandComponent>(NearbySCV);
    auto& Harv = EM.AddComponent<Game::Components::HarvestComponent>(NearbySCV);
    Harv.State = Game::Components::HarvestState::Idle;
    Harv.TargetResourceID = Mineral;
    Harv.DesiredResourceID = Mineral;

    // Create a far-away idle SCV at (2800, 2800) outside kResumeRadiusPixels (512 px)
    const int FarSCV = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(FarSCV).Position = {2800.0f, 2800.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(FarSCV).PlayerID = 0;
    auto& FarUnit = EM.AddComponent<Game::Components::UnitComponent>(FarSCV);
    FarUnit.ChkUnitID = 7;
    FarUnit.HitPoints = 60;
    FarUnit.MaxHitPoints = 60;
    EM.AddComponent<Game::Components::PathfindingComponent>(FarSCV);
    EM.AddComponent<Game::Components::CommandComponent>(FarSCV);
    auto& FarHarv = EM.AddComponent<Game::Components::HarvestComponent>(FarSCV);
    FarHarv.State = Game::Components::HarvestState::Idle;
    FarHarv.TargetResourceID = Mineral;
    FarHarv.DesiredResourceID = Mineral;

    SM.UpdateSimulationStep(0.05f);
    const auto* SpatialRes = EM.Context().GetResource<Game::Pathfinding::SpatialIndexResource>();
    Require(SpatialRes != nullptr, "SpatialIndexResource initialized");
    const uint64_t ReusesBeforeHarvest = SpatialRes->SharedQueryReuses;

    const int ResumedCount = Game::Systems::HarvestSystem::ResumeNearbyHarvestersForDepot(EM, Depot);
    Require(ResumedCount == 1, "HarvestSystem::ResumeNearbyHarvestersForDepot resumed only the nearby SCV via O(1) spatial query");
    Require(SpatialRes->SharedQueryReuses > ReusesBeforeHarvest,
            "HarvestSystem::ResumeNearbyHarvestersForDepot reused SpatialIndexResource");

    const uint64_t ReusesBeforeSpawn = SpatialRes->SharedQueryReuses;
    const Core::Math::Vec2 SpawnPos = Game::Systems::ProductionSystem::FindSpawnPosition(EM, Depot, 7);
    Require(SpawnPos.X > 0.0f && SpawnPos.Y > 0.0f, "ProductionSystem::FindSpawnPosition resolved valid spawn position");
    Require(SpatialRes->SharedQueryReuses > ReusesBeforeSpawn,
            "ProductionSystem::FindSpawnPosition reused SpatialIndexResource O(1) broadphase");

    Game::Systems::ProductionSystem::SetMap(nullptr);
}

void TestPhase29SelectionAndCursorManagerSpatialBroadphase()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();
    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));

    // Spawn a local Marine at (320, 320) and an enemy Zergling at (960, 960)
    const int LocalMarine = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(LocalMarine).Position = {320.0f, 320.0f};
    auto& MarineRender = EM.AddComponent<Game::Components::RenderComponent>(LocalMarine);
        MarineRender.Visible = true;
    EM.AddComponent<Game::Components::PlayerComponent>(LocalMarine).PlayerID = 0;
    auto& MarineUnit = EM.AddComponent<Game::Components::UnitComponent>(LocalMarine);
    MarineUnit.ChkUnitID = 0;
    MarineUnit.HitPoints = 40;
    MarineUnit.MaxHitPoints = 40;

    const int EnemyZergling = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(EnemyZergling).Position = {960.0f, 960.0f};
    auto& ZergRender = EM.AddComponent<Game::Components::RenderComponent>(EnemyZergling);
        ZergRender.Visible = true;
    EM.AddComponent<Game::Components::PlayerComponent>(EnemyZergling).PlayerID = 1;
    auto& ZergUnit = EM.AddComponent<Game::Components::UnitComponent>(EnemyZergling);
    ZergUnit.ChkUnitID = 37;
    ZergUnit.HitPoints = 35;
    ZergUnit.MaxHitPoints = 35;

    SM.UpdateSimulationStep(0.05f);
    const auto* SpatialRes = EM.Context().GetResource<Game::Pathfinding::SpatialIndexResource>();
    const uint64_t ReusesBeforePick = SpatialRes->SharedQueryReuses;

    Game::Client::Selection Sel;
    Sel.SetLocalPlayerId(0);
    const int Picked = Sel.PickEntityAtWorld(EM, 320.0f, 320.0f);
    Require(Picked == LocalMarine, "Selection::PickEntityAtWorld picked LocalMarine at (320, 320)");
    Require(SpatialRes->SharedQueryReuses > ReusesBeforePick,
            "Selection::PickEntityAtWorld reused SpatialIndexResource O(1) broadphase");

    const uint64_t ReusesBeforeRect = SpatialRes->SharedQueryReuses;
    const auto BoxPicked = Sel.CollectRectSelection(EM, 280.0f, 280.0f, 80.0f, 80.0f);
    Require(BoxPicked.size() == 1 && BoxPicked[0] == LocalMarine,
            "Selection::CollectRectSelection returned LocalMarine in box");
    Require(SpatialRes->SharedQueryReuses > ReusesBeforeRect,
            "Selection::CollectRectSelection reused SpatialIndexResource O(1) broadphase");

    const uint64_t ReusesBeforeCursor = SpatialRes->SharedQueryReuses;
    Game::Client::GameLoop::CursorManager CursorMgr;
    const auto ClickCtx = CursorMgr.ResolveClickTargetContext(EM, {LocalMarine}, {960.0f, 960.0f});
    Require(ClickCtx.EnemyEntityID == EnemyZergling && ClickCtx.AnyEntityID == EnemyZergling,
            "CursorManager::ResolveClickTargetContext identified EnemyZergling at (960, 960)");
    Require(SpatialRes->SharedQueryReuses > ReusesBeforeCursor,
            "CursorManager::ResolveClickTargetContext reused SpatialIndexResource O(1) broadphase");
}

void TestPhase30TransportUpgradeAndZergCachedQueries()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();
    SM.RegisterSystem(std::make_unique<Game::Systems::UpgradeSystem>(SM));

    // Create two linked Nydus Canals for Player 0
    const int NydusA = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(NydusA).Position = {200.0f, 200.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(NydusA).PlayerID = 0;
    auto& B1 = EM.AddComponent<Game::Components::BuildingComponent>(NydusA);
    B1.UnderConstruction = false;
    auto& U1 = EM.AddComponent<Game::Components::UnitComponent>(NydusA);
    U1.ChkUnitID = 0x86;
    U1.HitPoints = 250;

    const int NydusB = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(NydusB).Position = {1200.0f, 1200.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(NydusB).PlayerID = 0;
    auto& B2 = EM.AddComponent<Game::Components::BuildingComponent>(NydusB);
    B2.UnderConstruction = false;
    auto& U2 = EM.AddComponent<Game::Components::UnitComponent>(NydusB);
    U2.ChkUnitID = 0x86;
    U2.HitPoints = 250;

    const int Zergling = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Zergling).Position = {210.0f, 200.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Zergling).PlayerID = 0;
    auto& ZU = EM.AddComponent<Game::Components::UnitComponent>(Zergling);
    ZU.ChkUnitID = 37;
    ZU.HitPoints = 35;

    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));
    SM.UpdateSimulationStep(0.05f);
    Require(Game::Systems::TransportSystem::EnterNydusCanal(EM, Zergling, NydusA),
            "TransportSystem::EnterNydusCanal teleported Zergling using cached building query");
    const auto* ZTrans = EM.GetComponent<Game::Components::TransformComponent>(Zergling);
    Require(ZTrans != nullptr && ZTrans->Position.X > 1000.0f,
            "Zergling arrived at NydusB exit");
    const auto* SpatialRes = EM.Context().GetResource<Game::Pathfinding::SpatialIndexResource>();
    Require(SpatialRes != nullptr && SpatialRes->PositionsDirty,
            "EnterNydusCanal marked SpatialIndexResource positions dirty");

    SM.UpdateSimulationStep(0.05f);
    const auto& CachedBuildings = EM.GetEntitiesWithCached<Game::Components::BuildingComponent, Game::Components::PlayerComponent>();
    Require(CachedBuildings.size() == 2,
            "UpgradeSystem::Update populated cached query for BuildingComponent + PlayerComponent");
}

} // namespace

int main()
{
    TestPhase27MovementSystemCachedQueriesAndSharedSpatialFallback();
    TestPhase28HarvestAndProductionSpatialBroadphase();
    TestPhase29SelectionAndCursorManagerSpatialBroadphase();
    TestPhase30TransportUpgradeAndZergCachedQueries();

    std::cout << "[Phase27To30ModernEngineRefactorTest] All Phase 27-30 modern engine refactorings "
                 "(MovementSystem Cached & Shared Spatial Fallback, Harvest/Production Spatial Broadphase, "
                 "Selection/CursorManager O(1) Picking & Box-Select, and Transport/Upgrade/Zerg Cached Queries) passed.\n";
    return 0;
}
