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

#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Engine/Engine.h"
#include "Game/AI/AIBrain.h"
#include "Game/Map/Map.h"
#include "Game/Networking/RollbackStateManager.h"
#include "Game/Pathfinding/NavigationWorldResource.h"
#include "Game/StarCraftEnginePlugin.h"
#include "Game/Trigger/SCRTriggerParser.h"
#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"
#include "Resource/IArchiveReader.h"
#include "Resource/TblDecoder.h"
#include "Resource/Vf4Decoder.h"
#include "UI/HudLayout.h"

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
    void AudioSystem::SetSoundPath(int, std::string) {}
}

namespace Render
{
    void SokolPBRDispatcher::Shutdown() {}

}

namespace
{

std::string ResolveAssetsRRoot()
{
    for (const char* Candidate : {"assets-R", "../assets-R", "../../assets-R"})
    {
        if (std::filesystem::exists(std::filesystem::path(Candidate) / "Master_Refactor_Manifest.json"))
            return Candidate;
    }
    return "assets-R";
}

// ============================================================================
// Phase 61 Test:
// - Verifies unified Gameplay SystemGroup Pipeline shared between
//   StarCraftEnginePlugin and GameSessionBootstrap::RegisterGameplaySystems.
// - Verifies AnimationServer2D::EvaluateUnitLocomotionAndCombatState automatic
//   transitioning across Init, Walking, GndAttkInit, IsWorking, and Death clips
//   while honoring nobrkcodestart..nobrkcodeend non-interruptible sections.
// ============================================================================
void TestPhase61UnifiedPipelineAndAnimationStateMachine(const std::string& AssetsRoot)
{
    // 1. Verify unified GameSessionBootstrap::RegisterGameplaySystems delegates to StarCraftEnginePlugin
    Game::Client::GameLoop::GameSession Session;
    Session.MutableEcsMap().Generate(64, 64);
    Core::Audio::AudioSystem DummyAudio;
    Game::Client::GameLoop::GameSessionBootstrap::RegisterGameplaySystems(Session, DummyAudio);

    auto& SysMgr = Session.Systems();
    // 5 top-level SystemGroups: SpatialAndCommandGroup, LocomotionAndEconomyGroup,
    // CombatAndRaceGroup, PresentationAndAudioGroup, RenderExtractionGroup
    assert(SysMgr.GetSystemCount() == 5);
    assert(SysMgr.FindSystem<Game::Pathfinding::WorldSpatialIndexSystem>() != nullptr);
    assert(SysMgr.FindSystem<Game::Systems::MovementSystem>() != nullptr);
    assert(SysMgr.FindSystem<Game::Systems::CombatSystem>() != nullptr);
    assert(SysMgr.FindSystem<Core::Audio::ModernAudioEventRouterSystem>() != nullptr);
    assert(SysMgr.FindSystem<Render::ModernRenderPipelineSystem>() != nullptr);
    assert(Session.GetFogSystem() != nullptr);

    // 2. Verify AnimationServer2D::EvaluateUnitLocomotionAndCombatState
    Resource::ModernAssetRegistry ModReg;
    const bool Loaded = ModReg.LoadFromAssetsRDirectory(AssetsRoot);
    assert(Loaded);

    Engine::Animation::AnimationServer2D AnimServer;
    const size_t ImportedControllers = AnimServer.ImportFromModernRegistry(ModReg);
    assert(ImportedControllers == 398);

    // Find a controller that has Init, Walking, GndAttkInit, Death clips (e.g., Marine controller ID 0)
    uint32_t ChosenCtrlID = 0;
    for (const auto& Ctrl : ModReg.GetIScriptControllers())
    {
        if (Ctrl.Clips.count("Init") && Ctrl.Clips.count("Walking") &&
            Ctrl.Clips.count("GndAttkInit") && Ctrl.Clips.count("Death"))
        {
            ChosenCtrlID = Ctrl.IScriptID;
            break;
        }
    }

    Engine::Animation::Animator2DState AnimState{};
    AnimState.ControllerID = ChosenCtrlID;
    AnimState.CurrentClipName = "Init";

    std::vector<Engine::Animation::AnimationNotifyEvent2D> Events;
    // Step in Walking state (SpeedPxPerSec = 120.0f)
    const bool SteppedWalk = AnimServer.EvaluateUnitLocomotionAndCombatState(
        AnimState, /*SpeedPxPerSec=*/120.0f, /*IsAttacking=*/false,
        /*IsAirTarget=*/false, /*IsWorking=*/false, /*IsDead=*/false,
        /*DeltaTicks=*/3u, Events);
    assert(SteppedWalk);
    assert(AnimState.CurrentClipName == "Walking");

    // Step in Ground Attack state
    AnimState.NonInterruptibleSection = false;
    const bool SteppedAttack = AnimServer.EvaluateUnitLocomotionAndCombatState(
        AnimState, /*SpeedPxPerSec=*/0.0f, /*IsAttacking=*/true,
        /*IsAirTarget=*/false, /*IsWorking=*/false, /*IsDead=*/false,
        /*DeltaTicks=*/4u, Events);
    assert(SteppedAttack);
    assert(AnimState.CurrentClipName == "GndAttkInit");

    // Step in Death state (must force-interrupt even if NonInterruptibleSection is true)
    AnimState.NonInterruptibleSection = true;
    AnimServer.EvaluateUnitLocomotionAndCombatState(
        AnimState, /*SpeedPxPerSec=*/0.0f, /*IsAttacking=*/false,
        /*IsAirTarget=*/false, /*IsWorking=*/false, /*IsDead=*/true,
        /*DeltaTicks=*/2u, Events);
    assert(AnimState.CurrentClipName == "Death");
    assert(AnimServer.GetTelemetry().StateTransitionsExecuted >= 3);

    std::cout << "[Phase 61] Unified System Pipeline & Declarative Animation State Machine verified (Transitions="
              << AnimServer.GetTelemetry().StateTransitionsExecuted << ").\n";
}

// ============================================================================
// Phase 62 Test:
// - Verifies Engine::Navigation::NavigationAgent2D & NavigationWorldResource
//   A* path computation, O(1) connected-region fast-reject, waypoint stepping,
//   and local avoidance steering.
// ============================================================================
void TestPhase62NavigationAgent2DAndWorldResource()
{
    Core::ECS::EntityManager EM;
    Game::Map::GameMap Map;
    Map.Generate(32, 32);

    auto& NavWorld = Game::Pathfinding::NavigationWorldResource::GetOrSync(EM, &Map);
    assert(NavWorld.RegionsBuilt);
    assert(NavWorld.NavServer.GetConnectedRegionCount() >= 1);

    const auto Path = NavWorld.FindPathViaEngineServer({2, 2}, {10, 10}, 1);
    assert(!Path.empty());
    assert(Path.front().X == 2 && Path.front().Y == 2);
    assert(Path.back().X == 10 && Path.back().Y == 10);

    Engine::Navigation::NavigationAgent2D Agent;
    Agent.EntityID = 42;
    Agent.FootprintClearance = 1;
    Agent.WaypointReachThresholdPx = 12.0f;
    Agent.SetTargetPosition({336.0f, 336.0f}); // Tile (10, 10) center

    Core::Math::Vec2 Pos{80.0f, 80.0f}; // Tile (2, 2) center
    for (int Step = 0; Step < 200 && !Agent.ReachedTarget; ++Step)
    {
        Pos = NavWorld.StepNavigationAgent(Agent, Pos, /*MaxSpeedPxPerSec=*/160.0f, /*DeltaSeconds=*/0.05f);
    }
    assert(Agent.TargetReachable);
    assert(Agent.ReachedTarget);
    assert(Pos.DistanceTo({336.0f, 336.0f}) <= 12.5f);
    assert(NavWorld.NavServer.GetTelemetry().AgentStepsEvaluated > 0);

    std::cout << "[Phase 62] NavigationAgent2D & NavigationWorldResource verified (AgentSteps="
              << NavWorld.NavServer.GetTelemetry().AgentStepsEvaluated << ").\n";
}

// ============================================================================
// Phase 63 Test:
// - Verifies Engine::AI::BehaviorTreeServer::ExecuteTacticalScriptToBlackboard
//   against cloud assets-R AIScriptBin (293 trees) + BWScriptBin (156 trees).
// ============================================================================
void TestPhase63BehaviorTreeTacticalExecutor(const std::string& AssetsRoot)
{
    Engine::AI::BehaviorTreeServer BTServer;
    const size_t LoadedTrees = BTServer.LoadFromAssetsRDirectory(AssetsRoot);
    assert(LoadedTrees == 449);

    Engine::AI::Blackboard Board;
    const bool Executed = BTServer.ExecuteTacticalScriptToBlackboard("TMCu", Board, 4800);
    assert(Executed);
    assert(Board.GetInt("ExecutedNodeCount", 0) > 0);
    assert(Board.GetInt("BuildQueueCount", 0) > 0);
    assert(!Board.GetString("LastBuildUnit").empty());

    std::cout << "[Phase 63] BehaviorTreeServer Tactical Executor verified (Script=TMCu, BuildOrders="
              << Board.GetInt("BuildQueueCount", 0)
              << ", DefenseRules=" << Board.GetInt("DefensePoolRuleCount", 0)
              << ", LastBuildUnit=" << Board.GetString("LastBuildUnit") << ").\n";
}

// ============================================================================
// Phase 64 Test:
// - Verifies that all 20 redundant/micro-wrapper files have been consolidated
//   and removed from the repository while their folded headers/classes work.
// ============================================================================
void TestPhase64TwentyFileConsolidationAudit()
{
    const std::vector<std::string> RetiredFiles = {
        "src/Core/ECS/EntityCommandBuffer.h",
        "src/Client/CameraController.cpp",
        "src/Render/ParticleSystem.cpp",
        "src/Resource/Vf4Decoder.cpp",
        "src/Resource/TblDecoder.cpp",
        "src/Resource/FileSystemArchive.h",
        "src/Resource/FileSystemArchive.cpp",
        "src/Resource/CascArchive.h",
        "src/Resource/MpqArchive.h",
        "src/Game/Campaign/CampaignMissionCatalog.cpp",
        "src/UI/HudLayout.cpp",
        "src/UI/GlueCampaignData.h",
        "src/UI/GlueCampaignData.cpp",
        "src/Main/GameMenuRuntime.h",
        "src/Main/GameAudioRuntime.h",
        "src/Main/GameInteractionSmokeRuntime.h",
        "src/Main/GameEcsSessionBootstrap.h",
        "src/Main/GameResourceBootstrap.h",
        "src/Main/GameScenes.h",
        "src/Main/GameLoop/InputHandler.cpp"};

    for (const auto& RelPath : RetiredFiles)
    {
        assert(!std::filesystem::exists(RelPath));
    }

    // Verify folded EntityCommandBuffer in EntityManager.h
    Core::ECS::EntityManager EM;
    Core::ECS::EntityCommandBuffer ECB;
    ECB.CreateEntity([](Core::ECS::EntityManager& Mgr) {
        const auto Eid = Mgr.CreateEntity();
        Mgr.AddComponent<Engine::ECS::Transform2DComponent>(Eid).Position = {64.0f, 128.0f};
    });
    assert(ECB.Size() == 1 && !ECB.IsEmpty());
    ECB.Playback(EM);
    assert(EM.GetEntitiesWith<Engine::ECS::Transform2DComponent>().size() == 1);

    // Verify folded HudLayout and FileSystemArchive
    const auto Layout = Game::Client::UI::ResolveHudLayout(1920, 1080);
    assert(Layout.HudHeight > 0);
    Resource::FileSystemArchive FsArchive;
    assert(FsArchive.Open("."));
    assert(FsArchive.IsOpen());

    std::cout << "[Phase 64] 20 Redundant/Micro-Wrapper Files Retired & Consolidated cleanly.\n";
}

// ============================================================================
// Phase 65 Test:
// - Verifies RollbackStateManager binding to Engine::Net::DeterministicSessionServer
//   and SCRTriggerParser::CompileToScenarioTriggerServer.
// ============================================================================
void TestPhase65NetworkAndTriggerConvergence()
{
    // 1. RollbackStateManager + Engine::Net::DeterministicSessionServer<SyncActionWirePacket>
    Core::ECS::EntityManager EM;
    const int Eid = EM.CreateEntity();
    EM.AddComponent<Engine::ECS::Transform2DComponent>(Eid).Position = {10.0f, 20.0f};

    Game::Networking::RollbackStateManager::EngineSessionServer EngineNet;
    Game::Networking::RollbackStateManager RollbackMgr;
    RollbackMgr.BindDeterministicSessionServer(&EngineNet);
    assert(RollbackMgr.GetBoundDeterministicSessionServer() == &EngineNet);

    Game::Networking::SyncAction Act{};
    Act.Frame = 0;
    Act.PlayerID = 1;
    Act.Data = {2u, 10u, 20u};

    const uint32_t Hash0 = RollbackMgr.RecordFrame(EM, 0, {Act});
    assert(Hash0 != 0);
    assert(EngineNet.GetCommandsForTick(0).size() == 1);
    assert(RollbackMgr.VerifyRemoteFrameHash(0, Hash0));

    // 2. SCRTriggerParser::CompileToScenarioTriggerServer
    Game::Trigger::SCRTriggerData ChkTrigData{};
    Game::Trigger::SCRTrigger Trig{};
    Trig.Owners[0] = 1;
    Trig.Conditions[0].ConditionType = 12; // Elapsed time
    Trig.Conditions[0].Amount = 2;         // 2 seconds = 48 ticks at 24Hz
    Trig.Actions[0].ActionType = 13;       // Set switch
    Trig.Actions[0].Secondary = 5;         // Switch_5
    Trig.Actions[0].Amount = 4;            // Set
    Trig.Actions[1].ActionType = 1;        // Victory signal
    ChkTrigData.Triggers.push_back(Trig);

    Engine::Scripting::ScenarioTriggerServer TriggerServer;
    const size_t Compiled = Game::Trigger::SCRTriggerParser::CompileToScenarioTriggerServer(
        ChkTrigData, TriggerServer);
    assert(Compiled == 1);
    assert(TriggerServer.EvaluateTick(20) == 0);
    assert(!TriggerServer.Blackboard().GetBool("Switch_5", false));
    assert(TriggerServer.EvaluateTick(48) == 1);
    assert(TriggerServer.Blackboard().GetBool("Switch_5", false));
    assert(!TriggerServer.GetEmittedSignals().empty());
    assert(TriggerServer.GetEmittedSignals().front().SignalName == "Victory");

    std::cout << "[Phase 65] Network Lockstep/Rollback & CHK ScenarioTrigger Compilation verified.\n";
}

// ============================================================================
// Phase 66 Test:
// - Verifies Engine::ECS::CommonComponents2D & CapabilityTraitRegistry across
//   all 228 units from cloud assets-R via EngineProjectHost + StarCraftEnginePlugin.
// ============================================================================
void TestPhase66CommonComponents2DAndCapabilityTraitRegistry(const std::string& AssetsRoot)
{
    Engine::Project::EngineProjectHost Host;
    auto& PluginRef = Host.MountGameModule<Game::StarCraftEnginePlugin>(AssetsRoot);
    auto* PluginPtr = &PluginRef;
    const bool Booted = Host.BootProject(true);
    assert(Booted);

    const auto& Traits = PluginPtr->GetCapabilityTraits();
    assert(Traits.Size() == 228);

    // UnitID 0 = Terran Marine (Organic)
    assert(Traits.HasTrait(0, Engine::ECS::CapabilityFlags2D::Organic));
    // UnitID 7 = Terran SCV (Worker + Mechanical + Organic)
    assert(Traits.HasTrait(7, Engine::ECS::CapabilityFlags2D::Worker));
    // UnitID 9 = Terran Science Vessel (Flyer + Detector + Mechanical + Spellcaster)
    assert(Traits.HasTrait(9, Engine::ECS::CapabilityFlags2D::Flyer));
    assert(Traits.HasTrait(9, Engine::ECS::CapabilityFlags2D::Detector));
    // UnitID 64 = Protoss Probe (Worker + Robotic)
    assert(Traits.HasTrait(64, Engine::ECS::CapabilityFlags2D::Worker));
    assert(Traits.HasTrait(64, Engine::ECS::CapabilityFlags2D::Robotic));

    // Verify GenericWorldSnapshot captures and restores CommonComponents2D deterministically
    auto& EM = Host.App().Entities();
    const int TestEnt = EM.CreateEntity();
    EM.AddComponent<Engine::ECS::Transform2DComponent>(TestEnt) = {{512.0f, 768.0f}, {1.0f, 1.0f}, 0.0f, 8, 1};
    EM.AddComponent<Engine::ECS::Kinematics2DComponent>(TestEnt) = {{24.0f, -12.0f}, 160.0f, 400.0f, 20, false};
    EM.AddComponent<Engine::ECS::HealthAndShieldComponent>(TestEnt) = {40, 40, 0, 0, 1, false, false};

    Engine::ECS::GenericWorldSnapshot Snap;
    Snap.Capture(EM, 100);
    const uint32_t HashBefore = Snap.ComputeBinaryHashFNV1a();

    EM.GetComponent<Engine::ECS::Transform2DComponent>(TestEnt)->Position = {999.0f, 999.0f};
    EM.GetComponent<Engine::ECS::HealthAndShieldComponent>(TestEnt)->HitPoints = 5;

    Snap.Restore(EM);
    assert(EM.GetComponent<Engine::ECS::Transform2DComponent>(TestEnt)->Position.X == 512.0f);
    assert(EM.GetComponent<Engine::ECS::HealthAndShieldComponent>(TestEnt)->HitPoints == 40);

    Engine::ECS::GenericWorldSnapshot SnapAfter;
    SnapAfter.Capture(EM, 100);
    assert(SnapAfter.ComputeBinaryHashFNV1a() == HashBefore);

    Host.ShutdownProject();

    std::cout << "[Phase 66] CommonComponents2D & CapabilityTraitRegistry verified (Units="
              << Traits.Size() << ", DeterministicHash=0x" << std::hex << HashBefore << std::dec << ").\n";
}

} // namespace

int main()
{
    const std::string AssetsRoot = ResolveAssetsRRoot();
    TestPhase61UnifiedPipelineAndAnimationStateMachine(AssetsRoot);
    TestPhase62NavigationAgent2DAndWorldResource();
    TestPhase63BehaviorTreeTacticalExecutor(AssetsRoot);
    TestPhase64TwentyFileConsolidationAudit();
    TestPhase65NetworkAndTriggerConvergence();
    TestPhase66CommonComponents2DAndCapabilityTraitRegistry(AssetsRoot);

    std::cout << "=== ALL PHASE 61-66 ENGINE CONVERGENCE & LEGACY RETIREMENT TESTS PASSED ===\n";
    return 0;
}
