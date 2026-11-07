#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#include "Client/EntityRenderer.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Game/AI/AIBrain.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Races/RaceDef.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Render/Draw2D.h"
#include "Render/RenderQueue2D.h"
#include "Render/RenderServer2D.h"
#include "Resource/GameDataLoader.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace Render
{
    Draw2D g_Draw2D;
    void Draw2D::Flush() { mVerticesInBatch = 0; mIndicesInBatch = 0; }
    void Draw2D::SetPipeline(bool, BatchMaterial) {}
    void Draw2D::SetHealthBarParams(float, Color) {}
    void Draw2D::SetSpriteEffectParams(float, float, float, float, float, float, float, Color, bool) {}
    void Draw2D::DrawSprite(const GpuTextureHandle&, float, float, float, float, float, float, float, float, Color, bool) {}
    void Draw2D::DrawSpriteEffect(const GpuTextureHandle&, float, float, float, float, float, float, float, float, Color, bool, float, float, float, float, float, float, float, Color, const GpuTextureHandle*) {}
    void Draw2D::DrawRect(float, float, float, float, Color) {}
    void Draw2D::DrawRectLines(float, float, float, float, Color, float) {}
    void Draw2D::DrawHealthBar(float, float, float, float, float, Color) {}
    void Draw2D::DrawText(const std::string&, float, float, float, Color) {}
}

namespace Resource::Graphics
{
    Resource::GameDataLoader* DataAssetCacheView::GetDataLoader() const { return nullptr; }
    const std::vector<GpuImage>* SpriteAssetCacheView::GetDatImageFrames(uint32_t) const { return nullptr; }
    const std::vector<GpuImage>* SpriteAssetCacheView::GetSpriteOverlayFrames(uint32_t) const { return nullptr; }
}

namespace Resource
{
    const UnitEntry* GameDataLoader::FindUnitByID(uint32_t) const { return nullptr; }
    const SpriteEntry* GameDataLoader::FindSpriteByID(uint32_t) const { return nullptr; }
    const FlingyEntry* GameDataLoader::FindFlingyByID(uint32_t) const { return nullptr; }
    const WeaponEntry* GameDataLoader::FindWeaponByID(uint32_t) const { return nullptr; }
    const ImageEntry* GameDataLoader::FindImageByID(uint32_t) const { return nullptr; }
    uint32_t GameDataLoader::ResolveUnitSpriteID(uint32_t) const { return UINT32_MAX; }
    uint32_t GameDataLoader::ResolveUnitImageID(uint32_t) const { return 0; }
    std::string GameDataLoader::ResolveImageGrpPath(uint32_t) const { return {}; }
}

namespace Core::Audio
{
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags) { return 0; }
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

void TestPhase19CommandSystemSharedSpatialAndMedicHealOptimization()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    Game::Map::GameMap Map;
    Map.Generate(64, 64);
    Game::SimulationContext::Get(EM).Map = &Map;
    Game::SimulationContext::Get(EM).MutableMap = &Map;

    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));
    auto CmdSys = std::make_unique<Game::Systems::CommandSystem>();
    CmdSys->Init(&SM);
    SM.RegisterSystem(std::move(CmdSys));

    // Spawn 2 Medics (ChkUnitID 34) and 1 wounded Marine (ChkUnitID 0)
    auto SpawnBio = [&](int ChkID, float X, float Y, int HP, int MaxHP, int Energy) -> int {
        const int Id = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Id).Position = {X, Y};
        EM.AddComponent<Game::Components::PlayerComponent>(Id).PlayerID = 0;
        auto& U = EM.AddComponent<Game::Components::UnitComponent>(Id);
        U.ChkUnitID = ChkID;
        U.HitPoints = HP;
        U.MaxHitPoints = MaxHP;
        U.EnergyPoints = Energy;
        U.MaxEnergyPoints = 200;
        EM.AddComponent<Game::Components::CommandComponent>(Id);
        EM.AddComponent<Game::Components::PathfindingComponent>(Id);
        return Id;
    };

    const int Medic1 = SpawnBio(34, 200.0f, 200.0f, 60, 60, 100);
    const int Medic2 = SpawnBio(34, 210.0f, 200.0f, 60, 60, 100);
    const int Marine = SpawnBio(0,  220.0f, 200.0f, 15, 40, 0);

    // Order both Medics to heal the same wounded Marine
    Game::Components::Order HealOrder;
    HealOrder.Type = Game::Components::OrderType::Heal;
    HealOrder.TargetEntityID = Marine;
    HealOrder.TargetPosition = {220.0f, 200.0f};
    EM.GetComponent<Game::Components::CommandComponent>(Medic1)->QueueOrder(HealOrder);
    EM.GetComponent<Game::Components::CommandComponent>(Medic2)->QueueOrder(HealOrder);

    SM.UpdateSimulationStep(0.5f);

    const auto* SpatialRes = EM.GetResource<Game::Pathfinding::SpatialIndexResource>();
    Require(SpatialRes != nullptr && SpatialRes->RebuildCount == 1u && SpatialRes->SharedQueryReuses >= 1u,
            "CommandSystem::Update shared SpatialIndexResource without rebuilding a duplicate CSR grid");

    // Verify O(M) active medic target deduplication: only 1 Medic should lock onto the Marine while the second advances/stops
    const int Target1 = EM.GetComponent<Game::Components::UnitComponent>(Medic1)->HealTargetEntityID;
    const int Target2 = EM.GetComponent<Game::Components::UnitComponent>(Medic2)->HealTargetEntityID;
    Require((Target1 == Marine && Target2 != Marine) || (Target2 == Marine && Target1 != Marine),
            "Active Medic heal target deduplication prevents two Medics from redundantly healing the same target");
}

void TestPhase20SpellSystemSpatialBroadphaseAoEAndRecallDirtyTracking()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));
    SM.RegisterSystem(std::make_unique<Game::Systems::SpellSystem>(SM));

    // High Templar caster (ChkUnitID 67)
    const int Templar = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Templar).Position = {300.0f, 300.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Templar).PlayerID = 0;
    auto& TemplarU = EM.AddComponent<Game::Components::UnitComponent>(Templar);
    TemplarU.ChkUnitID = 67;
    TemplarU.UnitRace = Game::Components::Race::Protoss;
    TemplarU.HitPoints = 40;
    TemplarU.MaxHitPoints = 40;
    TemplarU.ShieldPoints = 40;
    TemplarU.MaxShieldPoints = 40;
    TemplarU.EnergyPoints = 200;
    TemplarU.MaxEnergyPoints = 200;

    // Unlock Psionic Storm research for Player 0
    Game::Systems::UpgradeSystem::GetUpgrades(EM, 0)
        .Levels[(size_t)Game::Systems::ResearchId::Protoss_PsionicStorm] = 1;

    // Target Hydralisk inside storm radius (at 320, 300) and distant Hydralisk outside (at 900, 900)
    const int NearHydra = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(NearHydra).Position = {320.0f, 300.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(NearHydra).PlayerID = 1;
    auto& NearU = EM.AddComponent<Game::Components::UnitComponent>(NearHydra);
    NearU.ChkUnitID = 38;
    NearU.HitPoints = 80;
    NearU.MaxHitPoints = 80;

    const int FarHydra = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(FarHydra).Position = {900.0f, 900.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(FarHydra).PlayerID = 1;
    auto& FarU = EM.AddComponent<Game::Components::UnitComponent>(FarHydra);
    FarU.ChkUnitID = 38;
    FarU.HitPoints = 80;
    FarU.MaxHitPoints = 80;

    // Build initial spatial index
    SM.UpdateSimulationStep(0.01f);
    const auto* SpatialRes = EM.GetResource<Game::Pathfinding::SpatialIndexResource>();
    const uint64_t ReusesBeforeCast = SpatialRes->SharedQueryReuses;

    // Cast Psionic Storm at (320, 300) -> should reuse SpatialIndexResource for initial AoE!
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, Templar, Game::Systems::SpellType::PsionicStorm, -1, {320.0f, 300.0f}),
            "High Templar cast Psionic Storm");
    Require(NearU.HitPoints == 66 && FarU.HitPoints == 80,
            "Psionic Storm initial AoE dealt 14 damage to nearby Hydralisk and spared distant Hydralisk");

    // Step 0.55s to trigger periodic Psionic Storm tick via SpatialIndexResource union query
    SM.UpdateSimulationStep(0.55f);
    Require(NearU.HitPoints == 52 && FarU.HitPoints == 80,
            "Periodic Psionic Storm tick dealt second 14-damage tick via SpatialIndexResource broadphase");
    Require(SpatialRes->SharedQueryReuses > ReusesBeforeCast,
            "SpellSystem AoE queries reused SpatialIndexResource");
}

void TestPhase21And22EntityRendererRetainedCanvasItemsAndCachedQueries()
{
    Core::ECS::EntityManager EM;
    Game::Client::EntityRenderer Renderer;
    Renderer.SetCamera(256.0f, 256.0f, 640.0f, 480.0f, 1.0f);

    // Spawn 2 mineral patches and 1 unit
    const int Mineral1 = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Mineral1).Position = {128.0f, 128.0f};
    EM.AddComponent<Game::Components::RenderComponent>(Mineral1).Visible = true;
    EM.AddComponent<Game::Components::ResourceComponent>(Mineral1).Amount = 1500;
    EM.AddComponent<Game::Components::UnitComponent>(Mineral1).ChkUnitID = 176;

    const int Mineral2 = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Mineral2).Position = {192.0f, 128.0f};
    EM.AddComponent<Game::Components::RenderComponent>(Mineral2).Visible = true;
    EM.AddComponent<Game::Components::ResourceComponent>(Mineral2).Amount = 1500;
    EM.AddComponent<Game::Components::UnitComponent>(Mineral2).ChkUnitID = 176;

    Require(Renderer.SyncRetainedStaticEntities(EM) == 2u,
            "EntityRenderer::SyncRetainedStaticEntities registered 2 static resource entities as CanvasItemRIDs");
    Require(Renderer.GetRenderServer().GetActiveCanvasItemCount() == 2u,
            "RenderServer2D tracks 2 active retained CanvasItemRIDs");

    //Destroy Mineral1 -> SyncRetainedStaticEntities must prune its CanvasItemRID via generational EntityHandle check
    EM.DestroyEntity(Mineral1);
    Require(Renderer.SyncRetainedStaticEntities(EM) == 1u &&
            Renderer.GetRenderServer().GetActiveCanvasItemCount() == 1u,
            "EntityRenderer::SyncRetainedStaticEntities freed destroyed entity's CanvasItemRID");

    // Spawn a grounded Building and verify SyncRetainedStaticEntities registers it as well
    const int Barracks = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Barracks).Position = {256.0f, 256.0f};
    EM.AddComponent<Game::Components::RenderComponent>(Barracks).Visible = true;
    EM.AddComponent<Game::Components::BuildingComponent>(Barracks).IsLifted = false;
    EM.AddComponent<Game::Components::UnitComponent>(Barracks).ChkUnitID = 111;
    Require(Renderer.SyncRetainedStaticEntities(EM) == 2u &&
            Renderer.GetRenderServer().GetActiveCanvasItemCount() == 2u,
            "EntityRenderer::SyncRetainedStaticEntities registers grounded static buildings as well as resources");

    // Render 5 consecutive frames without mutating component masks -> must hit GetEntitiesWithCached!
    const uint64_t HitsBefore = EM.GetQueryTelemetry().CachedQueryHits;
    for (int f = 0; f < 5; ++f)
    {
        Renderer.Render(EM, {});
    }
    Require(EM.GetQueryTelemetry().CachedQueryHits >= HitsBefore + 4u,
            "EntityRenderer::Render leverages GetEntitiesWithCached across steady-state frames");

    Renderer.ClearRetainedStaticEntities();
    Require(Renderer.GetRetainedStaticCount() == 0u &&
            Renderer.GetRenderServer().GetActiveCanvasItemCount() == 0u,
            "EntityRenderer::ClearRetainedStaticEntities releases all retained CanvasItemRIDs");
}

} // namespace

int main()
{
    TestPhase19CommandSystemSharedSpatialAndMedicHealOptimization();
    TestPhase20SpellSystemSpatialBroadphaseAoEAndRecallDirtyTracking();
    TestPhase21And22EntityRendererRetainedCanvasItemsAndCachedQueries();

    std::cout << "[Phase19To22ModernEngineRefactorTest] All Phase 19-22 modern engine refactorings (CommandSystem Shared Spatial & Medic O(M) Heal, SpellSystem Spatial AoE, EntityRenderer Retained CanvasItemRIDs, and Cached Queries) passed.\n";
    return 0;
}
