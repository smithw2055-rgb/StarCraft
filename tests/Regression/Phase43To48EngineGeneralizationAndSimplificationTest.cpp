#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#include "Core/Audio/AudioSystem.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Engine/Engine.h"
#include "Game/Networking/RollbackStateManager.h"
#include "Game/StarCraftEnginePlugin.h"
#include "Main/GameRuntimeFrame.h"
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

// ============================================================================
// Phase 43–45 Test: Codebase Simplification, Dead File Removal & Rollback Adapter
// ============================================================================
void TestPhase43To45StreamlinedCodebaseAndRollbackAdapter()
{
    // 1. Verify all 35 redundant / dead / fragmented files have been removed
    const std::vector<const char*> RemovedFiles = {
        "src/Client/ProjectileSystem.h",
        "src/Client/ProjectileSystem.cpp",
        "src/Client/SelectionOverlayPresentation.h",
        "src/Client/SelectionOverlayPresentation.cpp",
        "src/UI/GuiHost.h",
        "src/UI/GuiHost.cpp",
        "src/UI/HudResourceState.h",
        "src/UI/HudSelectionState.h",
        "src/UI/HudMinimapState.h",
        "src/UI/HudConsole.cpp",
        "src/UI/HudInput.cpp",
        "src/UI/HudCommandPanelRender.cpp",
        "src/UI/HudCommandButtons.cpp",
        "src/UI/HudCommandActions.cpp",
        "src/Main/CampaignText.h",
        "src/Main/CampaignPortrait.h",
        "src/Main/CampaignAudio.h",
        "src/Main/GuiRuntime.h",
        "src/Main/GameGuiBootstrap.h",
        "src/Main/GameClientViewRuntime.h",
        "src/Main/GameWorldPresentation.h",
        "src/Main/GameHudBootstrap.h",
        "src/Main/GameRuntimeBootstrap.h",
        "src/Main/GameClientLifecycle.h",
        "src/Main/GameStartRuntime.h",
        "src/Main/GameGlueMenuActionRuntime.h",
        "src/Resource/PcxRegion.h",
        "src/Resource/TilesetAssetTypes.h",
        "src/Resource/ArchiveSourceTypes.h",
        "src/Resource/DataAssetTypes.h",
        "src/Resource/SpriteAssetTypes.h",
        "src/Resource/PcxToGpu.h",
        "src/Resource/PcxToGpu.cpp",
        "src/Resource/UIAssetImageHelpers.h",
        "src/Resource/UIAssetImageHelpers.cpp"
    };

    for (const char* RelPath : RemovedFiles)
    {
        Require(!std::filesystem::exists(RelPath),
                "Phase 43-45: Redundant/dead file removed from repository");
    }

    // 2. Verify consolidated UI::HudState (HudResourceState + HudSelectionState + HudMinimapState)
    Game::Client::UI::HudState ConsolidatedHud{};
    ConsolidatedHud.Resources.MineralCount = 800;
    ConsolidatedHud.Resources.GasCount = 350;
    ConsolidatedHud.Selection.SelectedCount = 12;
    ConsolidatedHud.Minimap.ShowTerrain = true;
    Require(ConsolidatedHud.Resources.MineralCount == 800 &&
                ConsolidatedHud.Selection.SelectedCount == 12 &&
                ConsolidatedHud.Minimap.ShowTerrain,
            "Phase 43: Consolidated HudState header exposes Resource, Selection, and Minimap states");

    // 3. Verify streamlined RollbackStateManager delegating to GenericWorldSnapshot
    Core::ECS::EntityManager World;
    const Core::ECS::EntityID UnitE = World.CreateEntity();
    World.AddComponent<Game::Components::TransformComponent>(UnitE).Position = {100.0f, 200.0f};

    Game::Networking::RollbackStateManager RollbackMgr;
    const uint32_t HashT0 = RollbackMgr.RecordFrame(World, 0);
    World.GetComponent<Game::Components::TransformComponent>(UnitE)->Position.X = 150.0f;
    RollbackMgr.RecordFrame(World, 1);

    Game::Networking::SyncAction Injected{};
    Injected.PlayerID = 1;
    Injected.Frame = 0;
    Injected.Data = {50};

    Require(RollbackMgr.InjectActionAndResimulate(
                World,
                /*TargetFrame=*/0,
                /*CurrentFrame=*/1,
                Injected,
                [UnitE](Core::ECS::EntityManager& EM, const Game::Networking::SyncAction& Act) {
                    EM.GetComponent<Game::Components::TransformComponent>(UnitE)->Position.Y +=
                        static_cast<float>(Act.Data.empty() ? 0 : Act.Data[0]);
                },
                [UnitE](Core::ECS::EntityManager& EM, uint32_t) {
                    EM.GetComponent<Game::Components::TransformComponent>(UnitE)->Position.X += 50.0f;
                }),
            "Phase 45: Streamlined RollbackStateManager rolled back and resimulated via GenericWorldSnapshot");

    const auto* FinalTrans = World.GetComponent<Game::Components::TransformComponent>(UnitE);
    Require(HashT0 != 0 && FinalTrans != nullptr &&
                std::fabs(FinalTrans->Position.X - 150.0f) < 1e-4f &&
                std::fabs(FinalTrans->Position.Y - 250.0f) < 1e-4f,
            "Phase 45: Post-rollback entity transform matches expected resimulated state");
}

// ============================================================================
// Phase 46 Test: AnimationServer2D (398 Controllers + 2D Sockets) &
//                AudioServer2D (1,144 SFX + Spatial Coalescing)
// ============================================================================
void TestPhase46AnimationServer2DAndAudioServer2D()
{
    Resource::ModernAssetRegistry CloudRegistry;
    Require(CloudRegistry.LoadFromAssetsRDirectory("assets-R"),
            "Phase 46: Loaded cloud assets-R into ModernAssetRegistry");

    // 1. AnimationServer2D
    Engine::Animation::AnimationServer2D AnimServer;
    const size_t ImportedCtrls = AnimServer.ImportFromModernRegistry(CloudRegistry);
    Require(ImportedCtrls == 398u &&
                AnimServer.GetTelemetry().ClipsRegistered == 1364u &&
                AnimServer.GetSocketTrackTable().Size() == 2u,
            "Phase 46: AnimationServer2D imported all 398 controllers (1,364 clips) and 2 Socket tracks");

    // Step Critter Ragnasaur (IScriptID 198) Death clip and verify frame & audio notify tracks
    Engine::Animation::Animator2DState AnimState{};
    AnimState.ControllerID = 198u;
    Require(AnimServer.RequestStateTransition(AnimState, "Death", true),
            "Phase 46: Transitioned IScript 198 to 'Death' clip");

    std::vector<Engine::Animation::AnimationNotifyEvent2D> Notifies;
    Require(AnimServer.StepAnimator(AnimState, 8u, Notifies),
            "Phase 46: Stepped 'Death' clip across 8 ticks");
    Require(AnimState.Finished && AnimState.CurrentFrameSet == 160 && !Notifies.empty(),
            "Phase 46: Non-looping 'Death' clip reached final FrameSet=160 and emitted notify events");

    bool SawDeathSound = false;
    for (const auto& Ev : Notifies)
    {
        if (Ev.Kind == Engine::Animation::NotifyKind2D::AudioTrigger && Ev.SoundID == 53u)
            SawDeathSound = true;
    }
    Require(SawDeathSound,
            "Phase 46: AnimationServer2D emitted AudioTrigger notify for SoundID=53 (LCrDth00.wav)");

    // Sample 17-way mirrored 2D Socket track (SCV repair spark) at direction 4 vs mirrored 28 (32 - 4)
    Core::Math::Vec2 OffsetEast{}, OffsetWest{};
    Require(AnimServer.SampleSocketOffsetHD("Socket_Terran_SCV_RepairAndSpark", 0, 4u, OffsetEast) &&
                AnimServer.SampleSocketOffsetHD("Socket_Terran_SCV_RepairAndSpark", 0, 28u, OffsetWest),
            "Phase 46: Sampled SCV 2D Socket track at direction 4 and mirrored direction 28");
    Require(std::fabs(OffsetEast.X - 44.0f) < 1e-4f &&
                std::fabs(OffsetWest.X - (-44.0f)) < 1e-4f &&
                std::fabs(OffsetEast.Y - OffsetWest.Y) < 1e-4f,
            "Phase 46: 32-direction horizontal mirror accurately flips Socket X offset (+44 vs -44)");

    // 2. AudioServer2D
    Engine::Audio::AudioServer2D AudioServer;
    const size_t ImportedSounds = AudioServer.ImportFromModernRegistry(CloudRegistry);
    Require(ImportedSounds == 1144u,
            "Phase 46: AudioServer2D imported all 1,144 sound assets from cloud Sound_Catalog_1144_SFX.json");

    const auto* ExploSnd = AudioServer.FindSoundByID(7u);
    Require(ExploSnd != nullptr && ExploSnd->WavPath == "sound/Misc/ExploLrg.wav",
            "Phase 46: Resolved SoundID=7 ('sound/Misc/ExploLrg.wav') in AudioServer2D");

    // Enqueue 12 simultaneous Marine gunfire sounds within 128px coalesce radius
    AudioServer.SetCoalesceRadiusPx(128.0f);
    for (int i = 0; i < 12; ++i)
    {
        AudioServer.EnqueueSpatialSound(
            7u,
            {500.0f + static_cast<float>(i * 4), 500.0f},
            Core::Audio::AudioBusId::SFX,
            1.0f);
    }
    Require(AudioServer.GetPendingRequestCount() == 1u &&
                AudioServer.GetTelemetry().TotalRequestsCoalesced == 11u,
            "Phase 46: AudioServer2D coalesced 12 nearby same-tick sounds into 1 voice");

    const size_t VoicesDispatched = AudioServer.FlushFrame({400.0f, 500.0f}, 1600.0f, 1.0f / 60.0f);
    Require(VoicesDispatched == 1u,
            "Phase 46: AudioServer2D flushed coalesced spatial voice to AudioBusMixer");
}

// ============================================================================
// Phase 47 Test: PhysicsServer2D (Overlap, Raycast2D, Trigger, Separation) &
//                NavigationServer2D (Elevation, Ramps, Connected Regions)
// ============================================================================
void TestPhase47PhysicsServer2DAndNavigationElevationRegions()
{
    Engine::Physics::PhysicsServer2D Physics;

    // Register two overlapping ground units, one static building AABB, and one trigger zone
    Physics.RegisterOrUpdateBody({
        /*Entity=*/10,
        Engine::Physics::ShapeKind2D::Circle,
        /*Position=*/{100.0f, 100.0f},
        /*Radius=*/16.0f,
        /*HalfExtents=*/{16.0f, 16.0f},
        Engine::Physics::CollisionLayer::GroundUnit,
        /*IsStatic=*/false,
        /*IsTrigger=*/false
    });
    Physics.RegisterOrUpdateBody({
        /*Entity=*/11,
        Engine::Physics::ShapeKind2D::Circle,
        /*Position=*/{110.0f, 100.0f},
        /*Radius=*/16.0f,
        /*HalfExtents=*/{16.0f, 16.0f},
        Engine::Physics::CollisionLayer::GroundUnit,
        /*IsStatic=*/false,
        /*IsTrigger=*/false
    });
    Physics.RegisterOrUpdateBody({
        /*Entity=*/20,
        Engine::Physics::ShapeKind2D::AABB,
        /*Position=*/{250.0f, 100.0f},
        /*Radius=*/32.0f,
        /*HalfExtents=*/{30.0f, 30.0f},
        Engine::Physics::CollisionLayer::Building,
        /*IsStatic=*/true,
        /*IsTrigger=*/false
    });
    Physics.RegisterOrUpdateBody({
        /*Entity=*/99,
        Engine::Physics::ShapeKind2D::AABB,
        /*Position=*/{105.0f, 100.0f},
        /*Radius=*/32.0f,
        /*HalfExtents=*/{25.0f, 25.0f},
        Engine::Physics::CollisionLayer::GroundUnit,
        /*IsStatic=*/true,
        /*IsTrigger=*/true
    });

    std::vector<Core::ECS::EntityID> CircleHits;
    Physics.OverlapCircle({105.0f, 100.0f}, 20.0f, Engine::Physics::CollisionLayer::GroundUnit, CircleHits);
    Require(CircleHits.size() >= 2u && CircleHits[0] == 10 && CircleHits[1] == 11,
            "Phase 47: PhysicsServer2D::OverlapCircle returned deterministic sorted ground unit hits");

    // Raycast from (0, 100) east (+1, 0) toward Building AABB at [220..280] while ignoring ground units
    Engine::Physics::RaycastHit2D RayHit{};
    Require(Physics.Raycast2D(
                {0.0f, 100.0f},
                {1.0f, 0.0f},
                500.0f,
                Engine::Physics::CollisionLayer::Building,
                RayHit),
            "Phase 47: PhysicsServer2D::Raycast2D hit Building AABB");
    Require(RayHit.Entity == 20 && std::fabs(RayHit.Distance - 220.0f) < 1e-3f &&
                std::fabs(RayHit.Normal.X - (-1.0f)) < 1e-3f,
            "Phase 47: Raycast2D computed exact entry distance (220.0) and normal (-1, 0)");

    // Trigger overlap evaluation
    std::vector<Engine::Physics::TriggerOverlapPair2D> TriggerPairs;
    Physics.EvaluateTriggerOverlaps(TriggerPairs);
    Require(TriggerPairs.size() == 2u,
            "Phase 47: Trigger entity 99 detected both overlapping ground units (10 and 11)");

    // Deterministic separation velocity between overlapping entities 10 and 11
    const Core::Math::Vec2 Sep10 = Physics.SolveDeterministicSeparation(10, 64.0f);
    const Core::Math::Vec2 Sep11 = Physics.SolveDeterministicSeparation(11, 64.0f);
    Require(Sep10.X < 0.0f && Sep11.X > 0.0f,
            "Phase 47: SolveDeterministicSeparation pushes overlapping units 10 and 11 apart");

    // NavigationServer2D Elevation, Ramp & O(1) Connected Region check
    Engine::Navigation::NavigationServer2D Nav;
    Nav.InitializeGrid(12, 12, 32.0f);
    // Right half (X >= 6) is high ground (Elevation = 1)
    for (int32_t Y = 0; Y < 12; ++Y)
    {
        for (int32_t X = 6; X < 12; ++X)
            Nav.SetCellElevation(X, Y, 1u, false);
    }

    const uint32_t RegionsBeforeRamp = Nav.RebuildConnectedRegions(1u);
    Require(RegionsBeforeRamp == 2u && !Nav.AreCellsConnected({2, 5}, {9, 5}),
            "Phase 47: Cliff separates low ground (0) and high ground (1) into 2 disconnected regions");
    Require(Nav.FindPath({2, 5}, {9, 5}, 1u).empty(),
            "Phase 47: A* rejects cliff crossing when no ramp is present");

    // Carve a ramp at (5, 5) and (6, 5)
    Nav.SetCellElevation(5, 5, 0u, /*IsRamp=*/true);
    Nav.SetCellElevation(6, 5, 1u, /*IsRamp=*/true);
    const uint32_t RegionsAfterRamp = Nav.RebuildConnectedRegions(1u);
    Require(RegionsAfterRamp == 1u && Nav.AreCellsConnected({2, 5}, {9, 5}),
            "Phase 47: Adding ramp merges low and high ground into 1 connected region");
    Require(!Nav.FindPath({2, 5}, {9, 5}, 1u).empty(),
            "Phase 47: A* routes from low ground to high ground through the ramp");
}

// ============================================================================
// Phase 48 Test: UIServer2D (ThemeDB, 3x3 Command Grid, HD UI Atlases),
//                SceneLoaderServer (Byways_Chk_Full_Scene + 8 LUTs + Foliage),
//                NetworkTransportServer (Loopback Peer Rollback Sync), & EngineApp
// ============================================================================
void TestPhase48UIServer2DSceneLoaderAndNetworkTransport()
{
    // 1. UIServer2D
    Engine::UI::UIServer2D UIServer;
    Require(UIServer.Theme().GetStyleCount() >= 3u &&
                UIServer.Theme().FindStyleBox("ProtossConsole") != nullptr,
            "Phase 48: UIServer2D ThemeDB initialized default faction styles");

    const size_t ImportedUIAtlases = UIServer.ImportHDUIAtlasesFromManifest(
        "assets-R/04_Terrain_Environment_And_Shaders/Terrain_CHK_And_HD_Effects_Manifest.json");
    Require(ImportedUIAtlases == 12u,
            "Phase 48: UIServer2D imported all 12 HD DDS GRP UI & Effect Atlases from cloud manifest");

    const auto* CmdIconsAtlas = UIServer.GetUIAtlasTable().FindByName(
        "T_UI_Command_Icons_HD_Atlas_390Frames.png");
    Require(CmdIconsAtlas != nullptr && CmdIconsAtlas->FrameCount == 390u,
            "Phase 48: Verified T_UI_Command_Icons_HD_Atlas_390Frames.png (390 command icons) in UIServer2D");

    UIServer.BuildCommandGrid3x3(
        {960.0f, 520.0f},
        48.0f,
        6.0f,
        {{"Command.Move", uint16_t(228)}, {"Command.Stop", uint16_t(229)}, {"Command.AttackMove", uint16_t(230)}, {"Command.StimPack", uint16_t(235)}});

    std::string ClickedAction;
    // Slot 3 (row 1, col 0) is at X = 960..1008, Y = 574..622
    Require(UIServer.RoutePointerClick({975.0f, 590.0f}, ClickedAction) &&
                ClickedAction == "Command.StimPack",
            "Phase 48: UIServer2D routed pointer click on 3x3 Command Card slot 3 to 'Command.StimPack'");

    Render::RenderQueue2D UIRenderQueue;
    const size_t ExtractedUICmds = UIServer.ExtractToRenderQueue(UIRenderQueue);
    Require(ExtractedUICmds == 10u, // 1 grid container + 9 command buttons
            "Phase 48: UIServer2D extracted 10 UICanvas render commands into RenderQueue2D");

    // 2. SceneLoaderServer + Full EngineApp + StarCraftEnginePlugin
    Engine::EngineApp App;
    App.AddPlugin<Game::StarCraftEnginePlugin>("assets-R");
    App.Initialize();

    Require(App.Context().Animation2D().GetControllerTable().Size() == 398u &&
                App.Context().Audio2D().GetSoundTable().Size() == 1144u &&
                App.Context().UI2D().GetUIAtlasTable().Size() == 12u,
            "Phase 48: StarCraftEnginePlugin auto-populated AnimationServer2D (398), AudioServer2D (1144), and UIServer2D (12)");

    Require(App.Context().SceneLoader().LoadSceneFromAssetsR(
                "assets-R",
                App.Entities(),
                App.Context().SceneTree(),
                App.Context().Prefabs(),
                &App.Context().Navigation2D(),
                &App.Context().Physics2D()),
            "Phase 48: SceneLoaderServer loaded cloud Byways_Chk_Full_Scene.json into EngineApp");

    const auto& ActiveScene = App.Context().SceneLoader().GetActiveScene();
    Require(ActiveScene.SceneName == "Byways_Chk_Full_Scene" &&
                ActiveScene.WidthTiles == 96 && ActiveScene.HeightTiles == 96 &&
                ActiveScene.PlacedUnits.size() == 34u &&
                ActiveScene.SpawnedEntities.size() == 34u &&
                ActiveScene.TilesetGpuLuts.size() == 8u &&
                ActiveScene.FoliageAtlases.size() == 3u &&
                ActiveScene.FoliageRecordCount == 395u,
            "Phase 48: SceneLoaderServer verified 96x96 Byways map, 34 spawned units, 8 GPU Tileset LUTs, and 395 Foliage records");

    Require(App.Context().Navigation2D().GetWidth() == 96 &&
                App.Context().Physics2D().GetBodyCount() == 34u,
            "Phase 48: SceneLoaderServer synced NavigationServer2D (96x96) and PhysicsServer2D (34 bodies)");

    App.StepFrame(1.0f / 24.0f);

    // 3. NetworkTransportServer (LoopbackNetworkHub + DeterministicSessionServer)
    struct PeerMoveCmd
    {
        float DX = 0.0f;
        float DY = 0.0f;
    };

    auto SimStep = [](Core::ECS::EntityManager& EM,
                      uint32_t,
                      const std::vector<Engine::Net::DeterministicSessionServer<PeerMoveCmd>::FrameCommandEntry>& Cmds) {
        for (Core::ECS::EntityID Id : EM.GetActiveEntities())
        {
            if (auto* T = EM.GetComponent<Game::Components::TransformComponent>(Id))
            {
                T->Position.X += 2.0f;
                for (const auto& C : Cmds)
                {
                    T->Position.X += C.Command.DX;
                    T->Position.Y += C.Command.DY;
                }
            }
        }
    };

    auto Peer0Transport = App.Context().NetTransportHub().CreatePeerEndpoint(0);
    auto Peer1Transport = App.Context().NetTransportHub().CreatePeerEndpoint(1);

    Core::ECS::EntityManager World0, World1;
    World0.AddComponent<Game::Components::TransformComponent>(World0.CreateEntity()).Position = {0.0f, 0.0f};
    World1.AddComponent<Game::Components::TransformComponent>(World1.CreateEntity()).Position = {0.0f, 0.0f};

    Engine::Net::DeterministicSessionServer<PeerMoveCmd> Session0, Session1;
    Session0.BindSimulationStepCallback(SimStep);
    Session1.BindSimulationStepCallback(SimStep);

    // Peer 0 schedules a command at Tick 1 and broadcasts it to Peer 1 over LoopbackNetworkHub
    const PeerMoveCmd CmdP0{25.0f, -15.0f};
    Session0.ScheduleCommand(1u, 0, CmdP0);
    Peer0Transport->Broadcast(Engine::Net::LoopbackNetworkHub::MakeCommandPacket(0, 1u, CmdP0));

    // Both worlds advance to Tick 4 before Peer 1 polls its transport (simulating network lag)
    for (uint32_t T = 0; T < 4; ++T)
    {
        Session0.AdvanceTick(World0);
        Session1.AdvanceTick(World1);
    }

    // Peer 0 broadcasts its authoritative Tick 3 state hash
    Peer0Transport->SendToPeer(
        1,
        Engine::Net::LoopbackNetworkHub::MakeHashVerifyPacket(0, 3u, Session0.GetRecordedTickHash(3u)));

    // Now Peer 1 pumps its incoming transport: it receives the late Tick 1 command (auto-rolling back
    // and resimulating Ticks 1..3) and then verifies Peer 0's Tick 3 hash!
    bool HashVerifiedOk = false;
    const size_t PacketsProcessed = Engine::Net::LoopbackNetworkHub::PumpTransportIntoSession(
        *Peer1Transport, Session1, World1, &HashVerifiedOk);
    Require(PacketsProcessed == 2u && HashVerifiedOk &&
                Session1.GetRecordedTickHash(3u) == Session0.GetRecordedTickHash(3u),
            "Phase 48: NetworkTransportServer delivered late command & verified post-rollback state hash across peers");

    App.Shutdown();
}

} // namespace

int main()
{
    TestPhase43To45StreamlinedCodebaseAndRollbackAdapter();
    std::cout << "[PASS] Phase 43-45: 35 Redundant/Dead Files Removed, HudState Consolidated & RollbackStateManager Streamlined.\n";

    TestPhase46AnimationServer2DAndAudioServer2D();
    std::cout << "[PASS] Phase 46: Engine::Animation::AnimationServer2D (398 Controllers + 2D Sockets) & AudioServer2D (1,144 SFX + Coalescing) verified.\n";

    TestPhase47PhysicsServer2DAndNavigationElevationRegions();
    std::cout << "[PASS] Phase 47: Engine::Physics::PhysicsServer2D (Overlap/Raycast2D/Separation) & NavigationServer2D (Elevation/Ramps/Regions) verified.\n";

    TestPhase48UIServer2DSceneLoaderAndNetworkTransport();
    std::cout << "[PASS] Phase 48: Engine::UI::UIServer2D, SceneLoaderServer (Byways 96x96 + 8 LUTs + 395 Foliage), & NetworkTransportServer verified.\n";

    std::cout << "[Phase43To48EngineGeneralizationAndSimplificationTest] ALL PHASE 43-48 ENGINE GENERALIZATION & SIMPLIFICATION TESTS PASSED.\n";
    return 0;
}
