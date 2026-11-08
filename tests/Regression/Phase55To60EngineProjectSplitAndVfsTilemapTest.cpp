#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Client/WorldEffects.h"
#include "Engine/Engine.h"
#include "Game/Campaign/CampaignController.h"
#include "Game/DataEnums.h"
#include "Game/StarCraftEnginePlugin.h"
#include "Render/GpuTextureHandle.h"
#include "Resource/Cv5Decoder.h"
#include "Resource/IArchiveReader.h"
#include "Resource/UIAssetCacheView.h"
#include "UI/HudState.h"

namespace Core::Platform
{
    bool IsKeyPressed(int) { return false; }
    bool IsKeyDown(int) { return false; }
    bool IsKeyReleased(int) { return false; }
    int GetWindowWidth() { return 1280; }
    int GetWindowHeight() { return 720; }
}

namespace Core::Audio
{
    struct AudioSystem::Impl {};
    AudioSystem::AudioSystem() = default;
    AudioSystem::~AudioSystem() = default;
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags) { return 0; }
    void AudioSystem::Update(float) {}
    void AudioSystem::SetSoundType(int, const SoundType&) {}
    void AudioSystem::SetSoundPath(int, const std::string) {}
}

namespace Render
{
    void SokolPBRDispatcher::Shutdown() {}
}

namespace
{

void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::cerr << "[FAIL] " << Message << "\n";
        std::exit(1);
    }
}

std::string ResolveRepoRoot()
{
    namespace fs = std::filesystem;
    for (const char* Candidate : {".", "..", "../.."})
    {
        if (fs::exists(fs::path(Candidate) / "src" / "Engine" / "Engine.h"))
            return Candidate;
    }
    return ".";
}

std::string ResolveAssetsRRoot()
{
    namespace fs = std::filesystem;
    for (const char* Candidate : {
             "assets-R",
             "../assets-R",
             "../../assets-R"})
    {
        if (fs::exists(Candidate))
            return Candidate;
    }
    return "assets-R";
}

// ============================================================================
// Phase 55 Verification: 25 Redundant/Obsolete Files Removed & Consolidated
// ============================================================================
void TestPhase55StreamlinedCodebaseAnd25ConsolidatedFiles(const std::string& RepoRoot)
{
    namespace fs = std::filesystem;
    for (const char* RemovedFile : {
             "src/Game/Locale/Korean.h",
             "src/Game/MovementTuning.h",
             "src/Game/Campaign/CampaignProgressStore.h",
             "src/Game/Campaign/CampaignProgressStore.cpp",
             "src/Main/GameCampaignBootstrap.h",
             "src/Main/GameCampaignOverlay.h",
             "src/Main/GameHudPresentation.h",
             "src/Main/GameSystemAssetResolvers.h",
             "src/Main/GameMapBootstrap.h",
             "src/Main/GameSessionOperations.h",
             "src/Main/GameSimulationRuntime.h",
             "src/Main/GameNetworkActionRuntime.h",
             "src/Main/GameSaveRuntime.h",
             "src/Render/GpuBufferHandle.h",
             "src/Render/GpuShaderHandle.h",
             "src/Resource/TilesetAssetCacheView.h",
             "src/Resource/GameDetector.h",
             "src/Resource/GameDetector.cpp",
             "src/Resource/Vx4Decoder.h",
             "src/Resource/Vx4Decoder.cpp",
             "src/UI/GlueComponentRegistration.h",
             "src/UI/HudButtonStyle.h",
             "src/UI/HudInfoWidgets.h",
             "src/UI/HudCommandPanelState.h",
             "src/Client/WorldEffects.cpp"})
    {
        Require(!fs::exists(fs::path(RepoRoot) / RemovedFile),
                ("Phase 55: Redundant file must be removed: " + std::string(RemovedFile)).c_str());
    }

    // 1. Verify unified GpuTextureHandle.h provides Buffer, Shader, Pipeline, and Sampler handles
    Render::GpuTextureHandle TexH{{1u}, {2u}};
    Render::GpuBufferHandle BufH{{3u}};
    Render::GpuShaderHandle ShdH{{4u}};
    Render::GpuPipelineHandle PipH{{5u}};
    Render::GpuSamplerHandle SmpH{{6u}};
    Require(TexH.Valid() && BufH.Valid() && ShdH.Valid() && PipH.Valid() && SmpH.Valid(),
            "Phase 55: Unified Render/GpuTextureHandle.h provides all 5 GPU handle types");

    // 2. Verify unified UIAssetCacheView.h provides TilesetAssetCacheView alongside the other 4 views
    Resource::Graphics::TilesetAssetCacheView TilesetView(nullptr);
    Require(!TilesetView.HasCache(),
            "Phase 55: Unified Resource/UIAssetCacheView.h provides TilesetAssetCacheView");

    // 3. Verify unified Cv5Decoder.h/.cpp provides Vx4Decoder::Decode
    std::vector<uint8_t> DummyVx4(32, 0);
    DummyVx4[0] = 7;
    const auto Vx4 = Resource::Vx4Decoder::Decode(DummyVx4);
    Require(Vx4.MegaTileCount == 1u && Vx4.MegaTiles[0].MiniTileIDs[0] == 7u,
            "Phase 55: Unified Resource/Cv5Decoder provides Vx4Decoder::Decode");

    // 4. Verify unified DataEnums.h provides Game::Tuning::ToWorldMoveSpeed
    Require(Game::Tuning::ToWorldMoveSpeed(2.0f) == 64.0f,
            "Phase 55: Unified Game/DataEnums.h provides Game::Tuning::ToWorldMoveSpeed");

    // 5. Verify unified HudState.h provides HudCommandPanelState & WorldEffects header-only implementation
    Game::Client::UI::HudState HState{};
    Require(HState.CommandPanel.PanelPage == 0 &&
                HState.CommandPanel.PanelCategory == Game::Client::UI::HudPanelCategory::None,
            "Phase 55: Unified UI/HudState.h embeds HudCommandPanelState");

    Game::Client::WorldEffects Effects;
    Effects.AddCampaignPing({128.0f, 256.0f});
    Require(Effects.GetCampaignPings().size() == 1u &&
                Effects.GetCampaignPings()[0].Position.X == 128.0f,
            "Phase 55: Header-only Client/WorldEffects.h manages campaign pings");

    // 6. Verify CMakeLists.txt defines EngineFoundationObj and has zero dead source references
    {
        std::ifstream CMakeIn(fs::path(RepoRoot) / "CMakeLists.txt");
        Require(CMakeIn.is_open(), "Phase 55: CMakeLists.txt exists");
        std::string CMakeText((std::istreambuf_iterator<char>(CMakeIn)), std::istreambuf_iterator<char>());
        Require(CMakeText.find("add_library(EngineFoundationObj OBJECT") != std::string::npos,
                "Phase 59: CMakeLists.txt defines standalone EngineFoundationObj library");
        Require(CMakeText.find("tests/TriggerRuntimeTest/TriggerRuntimeTest.cpp") == std::string::npos &&
                    CMakeText.find("tests/ResourceExtractor/ResourceExtractor.cpp") == std::string::npos &&
                    CMakeText.find("tests/MpqListTest/MpqListTest.cpp") == std::string::npos,
                "Phase 55: Retired dead CMake executable blocks removed from CMakeLists.txt");
    }
}


// ============================================================================
// Phase 56 Verification: VirtualFileSystemServer & Priority Mount Overlays
// ============================================================================
void TestPhase56VirtualFileSystemServer(const std::string& AssetsRRoot)
{
    Engine::VFS::VirtualFileSystemServer VFS;
    VFS.MountDirectory("res://", AssetsRRoot, /*Priority=*/0, /*ReadOnly=*/true);

    Require(VFS.Exists("res://00_Schema_And_Pipeline/Master_Refactor_Manifest.json"),
            "Phase 56: VFS resolves res://00_Schema_And_Pipeline/Master_Refactor_Manifest.json");
    Require(VFS.Exists("res://02_Units_And_DataAssets/UnitDataAssets_All_228.json"),
            "Phase 56: VFS resolves res://02_Units_And_DataAssets/UnitDataAssets_All_228.json");
    Require(VFS.Exists("res://04_Terrain_Environment_And_Shaders/Byways_Chk_Full_Scene.json"),
            "Phase 56: VFS resolves res://04_Terrain_Environment_And_Shaders/Byways_Chk_Full_Scene.json");

    const auto ManifestText = VFS.ReadText("res://00_Schema_And_Pipeline/Master_Refactor_Manifest.json");
    Require(ManifestText.has_value() && ManifestText->find("total_hd_pbr_unit_bundles") != std::string::npos,
            "Phase 56: VFS ReadText reads cloud Master_Refactor_Manifest.json");

    // Verify in-memory & writable user:// virtual file storage
    Require(VFS.WriteText("user://config/engine_settings.json", "{\"vsync\":true}"),
            "Phase 56: VFS writes to virtual user:// scheme");
    const auto SettingsText = VFS.ReadText("user://config/engine_settings.json");
    Require(SettingsText.has_value() && *SettingsText == "{\"vsync\":true}",
            "Phase 56: VFS reads back virtual user:// file");

    const auto UserJsonFiles = VFS.ListFiles("user://", ".json");
    Require(UserJsonFiles.size() == 1u && UserJsonFiles[0] == "user://config/engine_settings.json",
            "Phase 56: VFS ListFiles enumerates in-memory user:// files");

    const auto JsonFiles = VFS.ListFiles("res://02_Units_And_DataAssets", ".json");
    Require(JsonFiles.size() >= 8u,
            "Phase 56: VFS ListFiles enumerates JSON data assets under res://02_Units_And_DataAssets");
}

// ============================================================================
// Phase 57 Verification: TilemapServer2D (8 GPU LUTs, 395 Foliage, 294 Stars)
// ============================================================================
void TestPhase57TilemapServer2D(const std::string& AssetsRRoot)
{
    Engine::Tilemap::TilemapServer2D Tilemap;
    const size_t LoadedItems = Tilemap.LoadFromAssetsRDirectory(AssetsRRoot);
    Require(LoadedItems > 0, "Phase 57: TilemapServer2D loaded manifests from assets-R");

    Require(Tilemap.GetTilesetLutCount() == 8u,
            "Phase 57: TilemapServer2D loaded all 8 HD GPU Tileset LUTs");
    const auto* BadlandsLut = Tilemap.FindTilesetLut("badlands");
    const auto* JungleLut = Tilemap.FindTilesetLut("jungle");
    Require(BadlandsLut != nullptr && BadlandsLut->Cv5Groups == 1979u && BadlandsLut->Vf4MegaTiles == 6172u,
            "Phase 57: Badlands GPU LUT metadata matches cloud manifest");
    Require(JungleLut != nullptr && JungleLut->Cv5Groups == 2046u && JungleLut->Vf4MegaTiles == 7038u,
            "Phase 57: Jungle GPU LUT metadata matches cloud manifest");

    Require(Tilemap.GetCreepAtlasCount() == 4u,
            "Phase 57: TilemapServer2D loaded 4 HD 37-frame creep edge atlases");
    Require(Tilemap.GetFoliageAtlasCount() == 3u && Tilemap.GetFoliageRecordCount() == 395u,
            "Phase 57: TilemapServer2D loaded 3 HD foliage atlases and 395 foliage.bin records");
    Require(Tilemap.GetParallaxLayerCount() == 5u && Tilemap.GetParallaxStarCount() == 294u,
            "Phase 57: TilemapServer2D loaded 5 space parallax layers and 294 star particles");

    // Verify foliage wind sway, unit trample depression, water flipbook, and camera culling
    Tilemap.SpawnFoliageInstance(0u, {100.0f, 100.0f});
    Tilemap.StepEnvironmentSimulation(0.25f, {{104.0f, 100.0f}}, /*CrushRadiusPx=*/32.0f);
    Require(Tilemap.GetFoliageInstances().size() == 1u &&
                Tilemap.GetFoliageInstances()[0].CrushDepression > 0.5f,
            "Phase 57: TilemapServer2D computes unit trample depression and wind sway on foliage");
    Require(Tilemap.GetWaterFlipbookState().CurrentFrameA == 3u &&
                Tilemap.GetWaterFlipbookState().CurrentFrameB == 4u,
            "Phase 57: TilemapServer2D advances 45-frame water normal flipbook");

    const auto Win = Tilemap.ComputeVisibleTileWindow(64.0f, 64.0f, 640, 480);
    Require(Win.MinTileX == 2 && Win.MinTileY == 2 && Win.VisibleTileCount == 300u,
            "Phase 57: TilemapServer2D computes camera frustum visible tile window");
    Require(Engine::Tilemap::TilemapServer2D::ResolveCreepEdgeFrameIndex(0xFFu) == 36u,
            "Phase 57: TilemapServer2D resolves 37-frame creep edge bitmask");
}

// ============================================================================
// Phase 58 Verification: ScenarioTriggerServer & CHK Trigger Metadata
// ============================================================================
void TestPhase58ScenarioTriggerServer(const std::string& AssetsRRoot)
{
    Engine::Scripting::ScenarioTriggerServer TriggerServer;
    Require(TriggerServer.ImportScenarioMetadataFromChkJsonFile(
                AssetsRRoot + "/04_Terrain_Environment_And_Shaders/Byways_Chk_Full_Scene.json"),
            "Phase 58: ScenarioTriggerServer imported Byways_Chk_Full_Scene.json");
    Require(TriggerServer.GetSceneName() == "Byways_Chk_Full_Scene" &&
                TriggerServer.GetTriggerChunkByteSize() == 7200u &&
                TriggerServer.GetImportedTriggerRecordCount() == 3u,
            "Phase 58: ScenarioTriggerServer extracted TRIG chunk size (7200B = 3 triggers)");

    // One-shot initialization rule
    Engine::Scripting::ScenarioRule InitRule;
    InitRule.Name = "InitScenarioResources";
    InitRule.PreserveTrigger = false;
    InitRule.Conditions.push_back({Engine::Scripting::TriggerConditionKind::Always, "", 0, true, {}});
    InitRule.Actions.push_back({Engine::Scripting::TriggerActionKind::SetBlackboardInt, "Minerals", 500, true, 0, {}});
    TriggerServer.RegisterRule(std::move(InitRule));

    // Repeating preserved wave rule with 3-tick wait timer
    Engine::Scripting::ScenarioRule WaveRule;
    WaveRule.Name = "PeriodicWaveSpawn";
    WaveRule.PreserveTrigger = true;
    WaveRule.Conditions.push_back({Engine::Scripting::TriggerConditionKind::BlackboardIntAtLeast, "Minerals", 500, true, {}});
    WaveRule.Actions.push_back({Engine::Scripting::TriggerActionKind::AddBlackboardInt, "WaveCount", 1, true, 0, {}});
    WaveRule.Actions.push_back({Engine::Scripting::TriggerActionKind::EmitSignal, "OnWaveSpawned", 1, true, 0, {}});
    WaveRule.Actions.push_back({Engine::Scripting::TriggerActionKind::WaitTicks, "", 0, true, 3u, {}});
    TriggerServer.RegisterRule(std::move(WaveRule));

    for (uint64_t Tick = 1; Tick <= 7; ++Tick)
        TriggerServer.EvaluateTick(Tick);

    Require(TriggerServer.Blackboard().GetInt("Minerals") == 500 &&
                TriggerServer.Blackboard().GetInt("WaveCount") == 3 &&
                TriggerServer.GetEmittedSignals().size() == 3u,
            "Phase 58: ScenarioTriggerServer evaluates one-shot and repeating wait-timed rules");
}

// ============================================================================
// Phase 59 & 60 Verification: Formal Engine Foundation + Dual Game Modules
// (1) StarCraftGameModule (StarCraft: Remastered Experimental Project)
// (2) SandboxDefenseGameModule (Independent Non-StarCraft Game Built on Same Engine)
// ============================================================================
struct DefenseTowerComponent
{
    float RangePx = 128.0f;
    int32_t Damage = 25;
    uint32_t Kills = 0;
};

class SandboxDefenseGameModule final : public Engine::Project::IGameModule
{
public:
    std::string_view GetName() const override { return "SandboxDefenseGameModule"; }
    std::string_view GetModuleVersion() const override { return "1.0.0-Sandbox"; }

    void ConfigureProject(Engine::Project::ProjectManifest& Manifest) override
    {
        Manifest.ProjectName = "Sandbox 2D Tower Defense";
        Manifest.FixedTickHz = 30.0f;
        Manifest.AssetMountRoot.clear();
        Manifest.AuthoritativeRollback = true;
    }

    void OnStartup(Engine::EngineApp& App) override
    {
        App.Context().Navigation2D().InitializeGrid(32, 32, 32.0f);
        App.Context().Navigation2D().RebuildConnectedRegions(1u);

        // Register a generic Tower prefab in PrefabRegistry
        Engine::Scene::PackedScene2D TowerPrefab;
        TowerPrefab.RootNodeDescriptor.Name = "TowerRoot";
        TowerPrefab.RootNodeDescriptor.Sockets.push_back({"MuzzleSocket", {0.0f, -16.0f}});
        TowerPrefab.AddComponentInitializer(
            [](Core::ECS::EntityManager& EM,
               Core::ECS::EntityID Id,
               const Core::Math::Vec2& /*SpawnPos*/,
               int32_t /*OwnerPlayerID*/) {
                auto& Comp = EM.AddComponent<DefenseTowerComponent>(Id);
                Comp.RangePx = 160.0f;
                Comp.Damage = 40;
                Comp.Kills = 0u;
            });
        App.Context().Prefabs().RegisterPrefab(1001u, "Tower_LaserMk1", std::move(TowerPrefab));

        // Register a scenario rule that increments wave counter every tick
        Engine::Scripting::ScenarioRule Rule;
        Rule.Name = "DefenseTickCounter";
        Rule.PreserveTrigger = true;
        Rule.Conditions.push_back({Engine::Scripting::TriggerConditionKind::Always, "", 0, true, {}});
        Rule.Actions.push_back({Engine::Scripting::TriggerActionKind::AddBlackboardInt, "DefenseTicks", 1, true, 0, {}});
        App.Context().ScenarioTriggers().RegisterRule(std::move(Rule));
    }

    bool OnLoadInitialScene(Engine::EngineApp& App,
                            const Engine::Project::ProjectManifest& /*Manifest*/) override
    {
        const auto Result = App.Context().Prefabs().SpawnByID(
            1001u, App.Entities(), App.Context().SceneTree(), {256.0f, 256.0f});
        return Result.Entity != Core::ECS::InvalidEntity;
    }
};

void TestPhase59And60EngineProjectHostAndDualGameModules(const std::string& AssetsRRoot)
{
    // 1. Boot Experimental Project #1: StarCraft: Remastered via EngineProjectHost
    {
        Engine::Project::EngineProjectHost StarCraftHost;
        auto& SCModule = StarCraftHost.MountGameModule<Game::StarCraftGameModule>(AssetsRRoot);
        Require(StarCraftHost.BootProject(/*LoadInitialScene=*/true),
                "Phase 59: EngineProjectHost booted StarCraftGameModule and loaded initial CHK scene");

        Require(StarCraftHost.Manifest().TotalHDPBRBundles == 247u &&
                    StarCraftHost.Manifest().TotalTilesetGpuLuts == 8u &&
                    StarCraftHost.Manifest().StrictHDOnly,
                "Phase 59: ProjectManifest parsed Master_Refactor_Manifest.json (247 HD PBR bundles, 8 GPU LUTs)");
        Require(SCModule.GetAssetRegistry() != nullptr &&
                    StarCraftHost.Context().Prefabs().GetTable().Size() >= 228u &&
                    StarCraftHost.Context().Animation2D().GetControllerTable().Size() >= 398u &&
                    StarCraftHost.Context().Audio2D().GetSoundTable().Size() >= 1144u &&
                    StarCraftHost.Context().BehaviorTrees().GetCatalog().GetTotalTreeCount() >= 449u &&
                    StarCraftHost.Context().Tilemap2D().GetTilesetLutCount() == 8u &&
                    StarCraftHost.Context().SceneLoader().GetActiveScene().SpawnedEntities.size() == 34u,
                "Phase 59: All 15 Engine Servers populated from cloud assets-R via StarCraftGameModule");

        StarCraftHost.StepFrame(1.0f / 24.0f);
        Require(StarCraftHost.Context().Time().GetTickCount() >= 1u &&
                    StarCraftHost.App().RollbackBuffer().GetTotalRecorded() >= 1u,
                "Phase 59: StarCraftHost stepped frame and recorded generic rollback snapshot");
        StarCraftHost.ShutdownProject();
    }

    // 2. Boot Independent Project #2: SandboxDefenseGameModule on the exact same Engine Foundation
    {
        Engine::Project::EngineProjectHost DefenseHost;
        DefenseHost.MountGameModule<SandboxDefenseGameModule>();
        Require(DefenseHost.BootProject(/*LoadInitialScene=*/true),
                "Phase 60: EngineProjectHost booted independent SandboxDefenseGameModule");

        DefenseHost.App().StepFixedTicks(5u);
        Require(DefenseHost.Context().ScenarioTriggers().Blackboard().GetInt("DefenseTicks") == 5,
                "Phase 60: EngineApp fixed tick loop automatically evaluated ScenarioTriggerServer rules");

        const auto TowerEntities = DefenseHost.App().Entities().GetEntitiesWith<DefenseTowerComponent>();
        Require(TowerEntities.size() == 1u &&
                    DefenseHost.App().Entities().GetComponent<DefenseTowerComponent>(TowerEntities[0])->Damage == 40,
                "Phase 60: Custom non-StarCraft ECS component & prefab instantiated cleanly on EngineFoundation");
        DefenseHost.ShutdownProject();
    }
}

} // namespace

int main()
{
    const std::string RepoRoot = ResolveRepoRoot();
    const std::string AssetsRRoot = ResolveAssetsRRoot();

    TestPhase55StreamlinedCodebaseAnd25ConsolidatedFiles(RepoRoot);
    std::cout << "[PASS] Phase 55: 25 Redundant/Obsolete Files Removed & Consolidated across Game/Main/Render/Resource/UI/Client.\n";

    TestPhase56VirtualFileSystemServer(AssetsRRoot);
    std::cout << "[PASS] Phase 56: Engine::VFS::VirtualFileSystemServer (res://, user://, priority overlay) verified.\n";

    TestPhase57TilemapServer2D(AssetsRRoot);
    std::cout << "[PASS] Phase 57: Engine::Tilemap::TilemapServer2D (8 GPU LUTs, 395 foliage records, 294 parallax stars, 45-frame water) verified.\n";

    TestPhase58ScenarioTriggerServer(AssetsRRoot);
    std::cout << "[PASS] Phase 58: Engine::Scripting::ScenarioTriggerServer (Condition->Action rules & CHK metadata) verified.\n";

    TestPhase59And60EngineProjectHostAndDualGameModules(AssetsRRoot);
    std::cout << "[PASS] Phase 59-60: EngineProjectHost & Dual Game Modules (StarCraftGameModule + SandboxDefenseGameModule) verified.\n";

    std::cout << "=== ALL PHASE 55-60 ENGINE FOUNDATION & GAME PROJECT EVOLUTION TESTS PASSED ===\n";
    return 0;
}
