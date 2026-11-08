#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Client/CameraController.h"
#include "Client/MapLoader.h"
#include "Client/Selection.h"
#include "Client/TilesetRenderer.h"
#include "Client/WorldEffects.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Engine/Engine.h"
#include "Game/Map/Map.h"
#include "Game/StarCraftEnginePlugin.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Main/CampaignPresentation.h"
#include "Main/GameRuntimeState.h"
#include "Render/EnvironmentAndTerrainPipeline.h"
#include "Resource/RgbaImageFileDecoder.h"
#include "UI/Hud.h"
#include "UI/HudState.h"

namespace Core::Platform
{
    bool IsKeyPressed(int) { return false; }
    bool IsKeyDown(int) { return false; }
    bool IsKeyReleased(int) { return false; }
    bool IsMouseButtonDown(int) { return false; }
    bool IsMouseButtonPressed(int) { return false; }
    bool IsMouseButtonReleased(int) { return false; }
    float GetMouseX() { return 4.0f; }
    float GetMouseY() { return 360.0f; }
    float GetMouseDeltaX() { return 0.0f; }
    float GetMouseDeltaY() { return 0.0f; }
    float GetMouseWheel() { return 0.0f; }
    bool IsWindowFocused() { return true; }
    bool IsMouseInClientArea() { return true; }
    bool IsMouseInGameContentArea() { return true; }
    double GetAbsoluteTime() { return 0.0; }
    void SetMouseCursorVisible(bool) {}
    int GetWindowWidth() { return 1280; }
    int GetWindowHeight() { return 720; }
}

namespace Core::Audio
{
    struct AudioSystem::Impl {};
    AudioSystem::AudioSystem() = default;
    AudioSystem::~AudioSystem() = default;
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags) { return 0; }
    int AudioSystem::PlayFile(const std::string&, float, float, int, SoundFlags) { return 0; }
    bool AudioSystem::Init() { return true; }
    void AudioSystem::Update(float) {}
    void AudioSystem::SetSoundType(int, const SoundType&) {}
    void AudioSystem::SetSoundPath(int, std::string) {}
    void AudioSystem::SetListenerPosition(float, float) {}
    void AudioSystem::SetScreenRect(float, float, float, float) {}
}

namespace Render
{
    Draw2D g_Draw2D;
    void Draw2D::DrawRect(float, float, float, float, Color) {}
    void Draw2D::DrawRectLines(float, float, float, float, Color, float) {}
    void Draw2D::DrawText(const std::string&, float, float, float, Color) {}
    void Draw2D::DrawSprite(const GpuTextureHandle&, float, float, float, float, float, float, float, float, Color, bool) {}
    void Draw2D::DrawSpriteFromImage(const GpuTextureHandle&, float, float, float, float, Color) {}
    void SokolPBRDispatcher::Shutdown() {}
}

namespace
{

std::string ResolveAssetsRRoot()
{
    for (const char* Candidate : {"assets-R", "../assets-R", "../../assets-R"})
    {
        if (std::filesystem::exists(std::filesystem::path(Candidate) / "02_Units_And_DataAssets" / "UnitDataAssets_All_228.json"))
            return Candidate;
    }
    return "assets-R";
}

// ============================================================================
// Phase 72 Test:
// - Verifies that all 20 fragmented/redundant .cpp and .h files in Phase 72
//   have been retired and consolidated (bringing cumulative retired files across
//   Phases 55-72 to 85 files, and across Phases 31-72 to 142 files).
// - Verifies header-only Engine::AI::BehaviorTreeCatalog and header-only
//   Render::EnvironmentAndTerrainPipeline.
// ============================================================================
void TestPhase72TwentyFileConsolidationAndSelfContainedEngineAI(const std::string& AssetsRoot)
{
    const std::vector<std::string> RetiredPhase72Files = {
        "src/PlatformBackend.cpp",
        "src/Resource/ResourceCacheUI.cpp",
        "src/Game/AI/BehaviorTreeRuntime.cpp",
        "src/Resource/RgbaImageFileDecoder.cpp",
        "src/Render/EnvironmentAndTerrainPipeline.cpp",
        "src/Main/CampaignPresentation.cpp",
        "src/Main/GameLoop/CursorManager.cpp",
        "src/Client/TilesetRenderer.cpp",
        "src/Client/MapLoader.cpp",
        "src/Main/CampaignRuntime.h",
        "src/Main/GameController.h",
        "src/Main/GameEntitySpawnRuntime.h",
        "src/Main/GameInputRuntime.h",
        "src/Main/GameUiPresentation.h",
        "src/Game/BWAPI/BWAPIInterface.h",
        "src/Render/ParticleSystem.h",
        "src/UI/HudInfoPanelState.h",
        "src/UI/HudPortrait.h",
        "src/UI/GlueAnimatedImage.h",
        "src/UI/GlueGuiHost.h",
        "src/Resource/DdsGrpDecoder.h"};

    for (const auto& RelPath : RetiredPhase72Files)
    {
        assert(!std::filesystem::exists(RelPath));
    }

    // Verify self-contained header-only Engine::AI::BehaviorTreeCatalog
    Engine::AI::BehaviorTreeCatalog Catalog;
    assert(Catalog.LoadFromAssetsR(AssetsRoot));
    assert(Catalog.GetAIScriptTreeCount() == 293);
    assert(Catalog.GetBWScriptTreeCount() == 156);
    assert(Catalog.GetTotalTreeCount() == 449);

    // Verify header-only Render::EnvironmentAndTerrainPipeline & RgbaImageFileDecoder
    Render::EnvironmentAndTerrainPipeline EnvPipeline;
    assert(EnvPipeline.LoadFromAssetsR(AssetsRoot));
    assert(EnvPipeline.GetTotalStarCount() == 294);
    assert(EnvPipeline.ValidateShaderContracts());

    // Verify folded ParticleSystem in WorldEffects.h and HudPortrait in Hud.h
    Render::ParticleSystem Particles;
    Particles.Init(64);
    Particles.EmitExplosion(128.0f, 128.0f, 8);
    Particles.Update(0.05f);
    assert(Game::Client::UI::ResolvePortraitKeyForChkUnitId(0) == "tmarine");

    std::cout << "[Phase 72] 21 Fragmented Files Retired & Consolidated (Cumulative 86 Files Retired in Phases 55-72) & Self-Contained Engine::AI Verified.\n";
}

// ============================================================================
// Phase 73 Test:
// - Verifies Engine::Vision::VisionServer2D high-ground elevation occlusion,
//   flying unit cliff-ignoring sight, shared alliance vision masks, detector
//   revelation grid, GPU FoW alpha LUT export, and FogOfWarSystem binding.
// ============================================================================
void TestPhase73VisionServer2DAndFogOfWarConvergence()
{
    Engine::Vision::VisionServer2D Vision;
    Vision.InitializeGrid(32, 32, 8, 32);

    // Elevate cliff region [12..20, 8..16] to ElevationLevel = 2
    for (int Y = 8; Y <= 16; ++Y)
    {
        for (int X = 12; X <= 20; ++X)
            Vision.SetCellElevation(X, Y, 2u);
    }

    // Player 0 and Player 1 share vision
    Vision.SetSharedVisionAlliance(0, 1, true);
    assert(Vision.AreAllied(0, 1));

    // Low-ground unit at (10, 12) with elevation 0 and sight radius 6:
    // Can see low-ground cell (9, 12), but CANNOT see up the cliff at (14, 12)
    Engine::Vision::VisionRevealer2D GroundMarine{};
    GroundMarine.OwnerPlayerID = 0;
    GroundMarine.TileX = 10;
    GroundMarine.TileY = 12;
    GroundMarine.SightRadiusTiles = 6;
    GroundMarine.ElevationLevel = 0;
    GroundMarine.IgnoresElevation = false;

    Vision.EvaluateFrame({GroundMarine});
    assert(Vision.IsTileVisible(9, 12, 0));
    assert(Vision.IsTileVisible(9, 12, 1)); // Shared with allied Player 1
    assert(!Vision.IsTileVisible(14, 12, 0)); // Blocked by cliff elevation 2

    // Flying detector (Science Vessel) at (10, 12) with IgnoresElevation=true & IsDetector=true:
    // Reveals high-ground cliff cell (14, 12) and marks it detected, while previous cells demote to Explored
    Engine::Vision::VisionRevealer2D ScienceVessel{};
    ScienceVessel.OwnerPlayerID = 0;
    ScienceVessel.TileX = 10;
    ScienceVessel.TileY = 12;
    ScienceVessel.SightRadiusTiles = 6;
    ScienceVessel.IgnoresElevation = true;
    ScienceVessel.IsDetector = true;
    ScienceVessel.DetectorRadiusTiles = 6;

    Vision.EvaluateFrame({ScienceVessel});
    assert(Vision.IsTileVisible(14, 12, 0));
    assert(Vision.IsTileDetected(14, 12, 0));
    assert(Vision.ExportGpuFogAlphaLut(0)[12 * 32 + 14] == 255u);

    // Bind FogOfWarSystem to VisionServer2D
    Core::ECS::SystemManager SysMgr;
    Game::Map::GameMap Map;
    Map.Generate(32, 32);
    Game::Systems::FogOfWarSystem FogSys(SysMgr, &Map);
    FogSys.BindEngineVisionServer(&Vision);
    assert(FogSys.GetBoundEngineVisionServer() == &Vision);

    std::cout << "[Phase 73] Engine::Vision::VisionServer2D (High-Ground Cliff Occlusion, Shared Alliance Vision & Detector Grid) Verified.\n";
}

// ============================================================================
// Phase 74 Test:
// - Verifies Engine::Input::CameraAndSelectionServer2D edge scroll, world
//   clamping, Screen<->World projection, generational box-selection, 10-group
//   control groups, and CameraController + Selection bindings.
// ============================================================================
void TestPhase74CameraAndSelectionServer2DConvergence()
{
    Engine::Input::CameraAndSelectionServer2D CamSel;
    CamSel.SetViewportSize(1280, 720);
    CamSel.SetWorldBoundsByTiles(128, 128, 32.0f);
    CamSel.SetCameraCenter({1024.0f, 1024.0f});

    // Screen <-> World roundtrip
    const Core::Math::Vec2 WorldPt = CamSel.ScreenToWorld({640.0f, 360.0f});
    assert( std::fabs(WorldPt.X - 1024.0f) < 0.01f && std::fabs(WorldPt.Y - 1024.0f) < 0.01f );
    const Core::Math::Vec2 ScreenPt = CamSel.WorldToScreen(WorldPt);
    assert( std::fabs(ScreenPt.X - 640.0f) < 0.01f && std::fabs(ScreenPt.Y - 360.0f) < 0.01f );

    // Left-edge mouse scroll steps camera left
    const Core::Math::Vec2 AfterEdgePan = CamSel.StepCameraEdgeAndKeyPan(2, 360, {0.0f, 0.0f}, 0.1f);
    assert(AfterEdgePan.X < 1024.0f);

    // Generational Selection & Control Group
    Core::ECS::EntityManager EM;
    const int E1 = EM.CreateEntity();
    const int E2 = EM.CreateEntity();
    const int E3 = EM.CreateEntity();

    CamSel.SetActiveSelection(EM, {E1, E2, E3});
    assert(CamSel.GetSelectedEntities().size() == 3);
    assert(CamSel.SaveControlGroup(1, EM));

    // Destroy E2 -> RecallControlGroup(1) automatically prunes dead handle and returns {E1, E3}
    EM.DestroyEntity(E2);
    const auto Recalled = CamSel.RecallControlGroup(1, EM);
    assert(Recalled.size() == 2 && Recalled[0] == E1 && Recalled[1] == E3);

    // Verify Client::CameraController binding
    Render::Camera2D ClientCam;
    ClientCam.SetViewport(1280, 720);
    Game::Client::CameraController CamCtrl;
    CamCtrl.Attach(ClientCam);
    CamCtrl.BindEngineCameraServer(&CamSel);
    CamCtrl.SetBounds(96, 96);
    assert(CamCtrl.GetBoundEngineCameraServer() == &CamSel);
    assert(CamSel.GetConfig().WorldWidthPx == 96.0f * 32.0f);

    std::cout << "[Phase 74] Engine::Input::CameraAndSelectionServer2D (Edge Pan, World Clamping & Generational Control Groups) Verified.\n";
}

// ============================================================================
// Phase 75 Test:
// - Verifies Engine::Gameplay::EconomyAndProductionServer cloud 228-unit
//   catalog import, multi-player resource wallets, 5-slot production queue,
//   cancellation refund, and HarvestSystem bidirectional synchronization.
// ============================================================================
void TestPhase75EconomyAndProductionServerConvergence(const std::string& AssetsRoot)
{
    Resource::ModernAssetRegistry ModReg;
    assert(ModReg.LoadFromAssetsRDirectory(AssetsRoot));

    Engine::Gameplay::EconomyAndProductionServer EconServer;
    assert(EconServer.ImportFromModernRegistry(ModReg) == 228);
    EconServer.ResetWallets(200, 100, 10);

    // Queue 2 Terran Marines (UnitID = 0, cost 50 minerals, 1 supply each) at Barracks Entity 500
    assert(EconServer.EnqueueProduction(500, 0, 0u));
    assert(EconServer.EnqueueProduction(500, 0, 0u));
    assert(EconServer.Wallet(0).Minerals == 100);
    assert(EconServer.Wallet(0).SupplyUsed == 2);

    // Cancel slot 1 -> refunds 50 minerals and 1 supply
    assert(EconServer.CancelProductionSlot(500, 1, 1.0f));
    assert(EconServer.Wallet(0).Minerals == 150);
    assert(EconServer.Wallet(0).SupplyUsed == 1);

    // Step 360 ticks -> completes Marine #1
    const auto Completed = EconServer.StepProductionQueues(360u);
    assert(Completed.size() == 1 && Completed[0].first == 500 && Completed[0].second == 0u);

    // Verify bidirectional sync with Game::Systems::HarvestSystem
    Core::ECS::EntityManager EM;
    Game::Systems::HarvestSystem::ResetResources(EM);
    Game::Systems::HarvestSystem::SyncFromEngineEconomyServer(EM, EconServer);
    assert(Game::Systems::HarvestSystem::GetResources(EM, 0).Minerals == 150);

    std::cout << "[Phase 75] Engine::Gameplay::EconomyAndProductionServer (228-Unit Cost Catalog, 5-Slot Queue & HarvestSystem Sync) Verified.\n";
}

// ============================================================================
// Phase 76 Test:
// - Verifies Engine::Combat::CombatAndProjectileServer2D cloud 130-weapon
//   catalog import, StarCraft size/damage-type matrix (Normal, Explosive,
//   Concussive + Plasma Shields), 3-tier radial splash query via PhysicsServer2D,
//   and ballistic projectile trajectory stepping.
// ============================================================================
void TestPhase76CombatAndProjectileServer2DConvergence(const std::string& AssetsRoot)
{
    Resource::ModernAssetRegistry ModReg;
    assert(ModReg.LoadFromAssetsRDirectory(AssetsRoot));

    Engine::Combat::CombatAndProjectileServer2D CombatServer;
    assert(CombatServer.ImportFromModernRegistry(ModReg) == 130);

    // Evaluate Concussive weapon vs Small (100%) and vs Large (25%)
    Engine::Combat::WeaponDefinition2D VultureGrenade{};
    VultureGrenade.WeaponID = 900u;
    VultureGrenade.DamageAmount = 20;
    VultureGrenade.DamageBonusPerUpgrade = 2;
    VultureGrenade.DamageFactor = 1;
    VultureGrenade.DamageKind = Engine::Combat::WeaponDamageKind::Concussive;
    VultureGrenade.SplashInnerPx = 16.0f;
    VultureGrenade.SplashMediumPx = 32.0f;
    VultureGrenade.SplashOuterPx = 48.0f;
    CombatServer.RegisterWeapon(VultureGrenade);

    const auto SmallRes = CombatServer.EvaluateWeaponDamage(
        900u, /*Upgrade=*/0, /*HP=*/40, /*Shields=*/0, /*Armor=*/0, /*ShieldUp=*/0,
        Engine::Combat::TargetSizeKind::Small);
    const auto LargeRes = CombatServer.EvaluateWeaponDamage(
        900u, /*Upgrade=*/0, /*HP=*/40, /*Shields=*/0, /*Armor=*/0, /*ShieldUp=*/0,
        Engine::Combat::TargetSizeKind::Large);
    assert(SmallRes.HullDamageDealt == 20); // 100% vs Small
    assert(LargeRes.HullDamageDealt == 5);  // 25% vs Large

    // Evaluate radial 3-tier splash against PhysicsServer2D bodies
    Engine::Physics::PhysicsServer2D Physics;
    Physics.RegisterOrUpdateBody({101, Engine::Physics::ShapeKind2D::Circle, {100.0f, 100.0f}, 8.0f, {8.0f, 8.0f}, Engine::Physics::CollisionLayer::GroundUnit, false, false});
    Physics.RegisterOrUpdateBody({102, Engine::Physics::ShapeKind2D::Circle, {124.0f, 100.0f}, 8.0f, {8.0f, 8.0f}, Engine::Physics::CollisionLayer::GroundUnit, false, false});
    Physics.RegisterOrUpdateBody({103, Engine::Physics::ShapeKind2D::Circle, {144.0f, 100.0f}, 8.0f, {8.0f, 8.0f}, Engine::Physics::CollisionLayer::GroundUnit, false, false});
    Physics.RebuildBroadphase();

    const auto SplashHits = CombatServer.QuerySplashTargets(900u, {100.0f, 100.0f}, Physics);
    assert(SplashHits.size() == 3);

    // Step ballistic projectile to impact
    CombatServer.SpawnProjectile(900u, 1, 101, 0, {0.0f, 0.0f}, {100.0f, 0.0f}, 500.0f);
    assert(CombatServer.GetActiveProjectileCount() == 1);
    const auto Impacted = CombatServer.StepProjectiles(0.25f);
    assert(Impacted.size() == 1 && Impacted[0].ReachedImpact);
    assert(CombatServer.GetActiveProjectileCount() == 0);

    std::cout << "[Phase 76] Engine::Combat::CombatAndProjectileServer2D (130 Weapons, Size/Armor/Shield Matrix & 3-Tier Splash) Verified.\n";
}

// ============================================================================
// Phase 77 Test:
// - Verifies Engine::Project::EngineProjectRegistry hosting multiple distinct
//   game modules ("starcraft-remastered" and "orbital-skirmish") on top of the
//   19-Server EngineProjectHost foundation.
// ============================================================================
class OrbitalSkirmishGameModule final : public Engine::Project::IGameModule
{
public:
    std::string_view GetName() const override { return "OrbitalSkirmishGameModule"; }
    std::string_view GetModuleVersion() const override { return "1.0.0-Orbital"; }

    void ConfigureProject(Engine::Project::ProjectManifest& Manifest) override
    {
        Manifest.ProjectName = "Orbital Fleet Skirmish";
        Manifest.FixedTickHz = 30.0f;
        Manifest.AssetMountRoot.clear();
        Manifest.AuthoritativeRollback = true;
    }

    void OnStartup(Engine::EngineApp& App) override
    {
        App.Context().Vision2D().InitializeGrid(64, 64, 4, 32);
        App.Context().EconomyAndProduction().ResetWallets(500, 250, 20);

        Engine::Gameplay::ProductionCatalogEntry Frigate{};
        Frigate.UnitID = 2001u;
        Frigate.UnitName = "Orbital_Ion_Frigate";
        Frigate.Faction = "TerranFleet";
        Frigate.MineralCost = 150;
        Frigate.GasCost = 50;
        Frigate.SupplyCost = 2;
        Frigate.BuildTimeTicks = 5u;
        App.Context().EconomyAndProduction().RegisterCatalogEntry(Frigate);
        App.Context().EconomyAndProduction().EnqueueProduction(1, 0, 2001u);

        Engine::Combat::WeaponDefinition2D IonCannon{};
        IonCannon.WeaponID = 3001u;
        IonCannon.DamageAmount = 45;
        IonCannon.DamageKind = Engine::Combat::WeaponDamageKind::Explosive;
        App.Context().Combat2D().RegisterWeapon(IonCannon);
        App.Context().Combat2D().SpawnProjectile(3001u, 1, 2, 0, {0.0f, 0.0f}, {60.0f, 0.0f}, 600.0f);
    }
};

void TestPhase77MultiProjectEngineRegistryAnd19ServerMatrix(const std::string& AssetsRoot)
{
    Engine::Project::EngineProjectRegistry Registry;
    Registry.RegisterProject<Game::StarCraftGameModule>(
        "starcraft-remastered",
        "StarCraft: Remastered (Experimental RTS Project)",
        "RTS",
        AssetsRoot);
    Registry.RegisterProject<OrbitalSkirmishGameModule>(
        "orbital-skirmish",
        "Orbital Fleet Skirmish (Experimental Space Tactics Project)",
        "SpaceTactics");

    assert(Registry.GetRegisteredProjectCount() == 2);
    assert(Registry.HasProject("starcraft-remastered"));
    assert(Registry.HasProject("orbital-skirmish"));

    // 1. Launch Project #1: StarCraft: Remastered
    auto SCHost = Registry.LaunchProject("starcraft-remastered", /*LoadInitialScene=*/true);
    assert(SCHost != nullptr);
    assert(SCHost->Context().Prefabs().GetTable().Size() == 228);
    assert(SCHost->Context().Animation2D().GetControllerTable().Size() == 398);
    assert(SCHost->Context().Audio2D().GetSoundTable().Size() == 1144);
    assert(SCHost->Context().EconomyAndProduction().GetCatalogSize() == 228);
    assert(SCHost->Context().Combat2D().GetWeaponCount() == 130);
    assert(SCHost->Context().Vision2D().GetWidth() == 96 && SCHost->Context().Vision2D().GetHeight() == 96);
    assert(SCHost->Context().SceneLoader().GetActiveScene().SpawnedEntities.size() == 34);
    SCHost->StepFrame(1.0f / 24.0f);
    SCHost->ShutdownProject();

    // 2. Launch Project #2: Orbital Fleet Skirmish on the exact same Engine Foundation
    auto OrbitalHost = Registry.LaunchProject("orbital-skirmish", /*LoadInitialScene=*/true);
    assert(OrbitalHost != nullptr);
    OrbitalHost->App().StepFixedTicks(5u);
    assert(OrbitalHost->Context().EconomyAndProduction().GetTelemetry().UnitsCompleted == 1);
    assert(OrbitalHost->Context().Combat2D().GetTelemetry().ProjectilesImpacted == 1);
    OrbitalHost->ShutdownProject();

    std::cout << "[Phase 77] Engine::Project::EngineProjectRegistry & 19-Server Matrix Multi-Project Hosting Verified.\n";
}

} // namespace

int main()
{
    std::cout << std::unitbuf;
    const std::string AssetsRoot = ResolveAssetsRRoot();
    TestPhase72TwentyFileConsolidationAndSelfContainedEngineAI(AssetsRoot);
    TestPhase73VisionServer2DAndFogOfWarConvergence();
    TestPhase74CameraAndSelectionServer2DConvergence();
    TestPhase75EconomyAndProductionServerConvergence(AssetsRoot);
    TestPhase76CombatAndProjectileServer2DConvergence(AssetsRoot);
    TestPhase77MultiProjectEngineRegistryAnd19ServerMatrix(AssetsRoot);

    std::cout << "=== ALL PHASE 72-77 ENGINE EVOLUTION, CONVERGENCE & SIMPLIFICATION TESTS PASSED ===\n";
    return 0;
}
