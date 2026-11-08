#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#include "Core/Audio/AudioSystem.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "Engine/Engine.h"
#include "Game/StarCraftEnginePlugin.h"
#include "Main/GameRuntimeFrame.h"
#include "Main/GameLoop/GameSaveState.h"


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
// Phase 37 Test: WorldSerializer Binary + CRC32 & Streamlined GameSaveState
// ============================================================================
void TestPhase37WorldSerializerAndStreamlinedSaveState()
{
    Core::ECS::EntityManager SourceWorld;

    auto& Econ = SourceWorld.Context().EmplaceResource<Game::Systems::PlayerResourceStore>();
    Econ.Players[0].Minerals = 1250;
    Econ.Players[0].VespeneGas = 600;

    auto& Upg = SourceWorld.Context().EmplaceResource<Core::ECS::WorldUpgradeResource>();
    Upg.PlayerUpgrades[0].Levels[2] = 3;

    const Core::ECS::EntityID Carrier = SourceWorld.CreateEntity();
    SourceWorld.AddComponent<Game::Components::TransformComponent>(Carrier).Position = {512.0f, 768.0f};
    SourceWorld.AddComponent<Game::Components::PlayerComponent>(Carrier).PlayerID = 1;
    auto& Unit = SourceWorld.AddComponent<Game::Components::UnitComponent>(Carrier);
    Unit.ChkUnitID = 72;
    Unit.HitPoints = 300;
    Unit.ShieldPoints = 150;

    auto& Cmd = SourceWorld.AddComponent<Game::Components::CommandComponent>(Carrier);
    Game::Components::Order AttackOrder{};
    AttackOrder.Type = Game::Components::OrderType::AttackMove;
    AttackOrder.TargetPosition = {1024.0f, 1024.0f};
    Cmd.QueueOrder(AttackOrder);

    auto& Path = SourceWorld.AddComponent<Game::Components::PathfindingComponent>(Carrier);
    Path.Path.push_back({600.0f, 800.0f});
    Path.Path.push_back({1024.0f, 1024.0f});
    Path.HasFixedPosition = true;
    Path.FixedPosition = Core::Math::FixedVec2(512.0f, 768.0f);

    // 1. Direct WorldSerializer binary + CRC32 roundtrip
    std::vector<uint8_t> BinaryStream;
    Require(Engine::ECS::WorldSerializer::CaptureAndSerialize(SourceWorld, 900, BinaryStream),
            "Phase 37: WorldSerializer::CaptureAndSerialize succeeded");
    Require(BinaryStream.size() > sizeof(Engine::ECS::WorldSerializer::BinaryHeader),
            "Phase 37: Serialized world stream has header + payload");

    // Verify CRC32 tamper detection
    std::vector<uint8_t> CorruptedStream = BinaryStream;
    CorruptedStream.back() ^= 0xFFu;
    Core::ECS::EntityManager CorruptTarget;
    Require(!Engine::ECS::WorldSerializer::DeserializeAndRestore(CorruptTarget, CorruptedStream),
            "Phase 37: WorldSerializer rejects corrupted CRC32 payload");

    // Restore valid binary stream into a brand-new EntityManager
    Core::ECS::EntityManager DirectTarget;
    uint32_t RestoredTick = 0;
    Require(Engine::ECS::WorldSerializer::DeserializeAndRestore(DirectTarget, BinaryStream, &RestoredTick),
            "Phase 37: WorldSerializer::DeserializeAndRestore succeeded on fresh EntityManager");
    Require(RestoredTick == 900 && DirectTarget.GetEntityCount() == 1u,
            "Phase 37: Restored tick and entity count match");

    const auto* RestoredCmd = DirectTarget.GetComponent<Game::Components::CommandComponent>(Carrier);
    const auto* RestoredPath = DirectTarget.GetComponent<Game::Components::PathfindingComponent>(Carrier);
    Require(RestoredCmd != nullptr && RestoredCmd->Orders.size() == 1u &&
                RestoredCmd->Orders[0].TargetPosition.X == 1024.0f,
            "Phase 37: CustomComponentBinarySerializer restored CommandComponent orders");
    Require(RestoredPath != nullptr && RestoredPath->Path.size() == 2u,
            "Phase 37: CustomComponentBinarySerializer restored PathfindingComponent waypoints");

    // 2. Verify streamlined GameSaveState roundtrip
    Game::Client::GameLoop::GameSaveSnapshot SaveOut{};
    SaveOut.MapPath = "maps/broodwar/lost_temple.scm";
    SaveOut.Paused = false;
    SaveOut.HasWorldSnapshot = true;
    SaveOut.SimulationTick = 900;
    SaveOut.WorldSnapshot = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(SourceWorld, 900);

    FILE* Tmp = std::tmpfile();
    Require(Tmp != nullptr && Game::Client::GameLoop::WriteGameSave(Tmp, SaveOut),
            "Phase 37: Streamlined WriteGameSave succeeded");
    std::rewind(Tmp);

    Game::Client::GameLoop::GameSaveSnapshot SaveIn{};
    Require(Game::Client::GameLoop::ReadGameSave(Tmp, SaveIn),
            "Phase 37: Streamlined ReadGameSave succeeded");
    std::fclose(Tmp);

    Core::ECS::EntityManager DiskTargetWorld;
    Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(DiskTargetWorld, SaveIn.WorldSnapshot);
    const uint32_t HashSrc = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(SourceWorld, 900);
    const uint32_t HashDst = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(DiskTargetWorld, 900);
    Require(HashSrc == HashDst,
            "Phase 37: Streamlined GameSaveState restored identical FNV-1a world state hash");
}

// ============================================================================
// Phase 38 Test: Unified SceneNode2D (Engine::Node2D) & RenderGraph2D
// ============================================================================
void TestPhase38UnifiedNode2DAndRenderGraph2D()
{
    // Verify Render::SceneNode2D inherits from Engine::Node2D
    Render::SceneNode2D TankRoot("SiegeTank_Root", Render::SceneNodeRole::RootUnit);
    Engine::Node2D& BaseRef = TankRoot;
    BaseRef.SetLocalPosition({320.0f, 480.0f});
    TankRoot.SetDirection32(20); // Flipped direction (> 16)
    Require(TankRoot.GetDirection17() == 12 && TankRoot.IsFlippedHorizontally(),
            "Phase 38: Engine::Node2D 32-direction symmetry & horizontal flip computed accurately");

    Engine::SceneTree2D SceneTree;
    Engine::Node2D* UnitNode = SceneTree.SpawnChild(SceneTree.GetRoot(), "Marine_Sprite", 1);
    UnitNode->SetLocalPosition({200.0f, 300.0f});
    UnitNode->SetImageID(189);
    UnitNode->SetElevationSortLayer(4);
    UnitNode->DefineSocket("Muzzle", {10.0f, -4.0f});

    Engine::Node2D* FlashNode = SceneTree.SpawnChild(*UnitNode, "MuzzleFlash", 7);
    FlashNode->BindToParentSocket("Muzzle");
    FlashNode->SetLocalPosition({2.0f, 1.0f});
    FlashNode->SetImageID(210);
    FlashNode->SetElevationSortLayer(4);
    FlashNode->SetZIndexOffset(3);

    Render::RenderServer2D RenderServer;
    Engine::Render::RenderGraph2D RenderGraph;

    bool CustomTerrainExecuted = false;
    RenderGraph.SetPassCallback(
        Engine::Render::RenderPassStage::TerrainPass,
        [&](Engine::Render::RenderGraphPassContext&) {
            CustomTerrainExecuted = true;
        });

    auto FrameTelemetry = RenderGraph.ExecuteFrame(RenderServer, SceneTree, nullptr, 0.5f);
    Require(CustomTerrainExecuted && FrameTelemetry.ActivePassesExecuted == 6u,
            "Phase 38: RenderGraph2D executed all 6 declarative render passes");
    Require(FrameTelemetry.TotalCommandsQueued == 2u,
            "Phase 38: RenderGraph2D extracted parent & socket-mounted child Engine::Node2D sprites");
    Require( std::fabs(FlashNode->GetWorldPosition().X - 212.0f) < 1e-4f &&
                 std::fabs(FlashNode->GetWorldPosition().Y - 297.0f) < 1e-4f,
            "Phase 38: Socket-bound child Node2D world position resolved during RenderGraph2D frame");

    // Disable FogOfWarPass and verify 5 passes run
    RenderGraph.SetPassEnabled(Engine::Render::RenderPassStage::FogOfWarPass, false);
    FrameTelemetry = RenderGraph.ExecuteFrame(RenderServer, SceneTree, nullptr, 0.75f);
    Require(FrameTelemetry.ActivePassesExecuted == 5u,
            "Phase 38: Disabling FogOfWarPass reduces active pass count to 5");
}

// ============================================================================
// Phase 39 Test: Engine::Input::CommandRouter & Inlined Runtime Helpers
// ============================================================================
void TestPhase39CommandRouterAndStreamlinedMainHelpers()
{
    struct TestRTSCommand
    {
        uint8_t CommandType = 0;
        float TargetX = 0.0f;
        float TargetY = 0.0f;
    };

    Core::Input::InputActionMap ActionMap = Core::Input::InputActionMap::CreateDefaultRTSMap();
    Engine::Input::CommandRouter<TestRTSCommand> Router;
    Router.SetLocalPlayerID(2);
    Router.SetInputLatencyTicks(2);

    Router.BindActionTranslator("Command.AttackMoveModifier", [](uint8_t /*PlayerID*/, TestRTSCommand& Out) {
        Out.CommandType = 10;
        Out.TargetX = 640.0f;
        Out.TargetY = 480.0f;
        return true;
    });

    const size_t Emitted = Router.TriggerActionByKey(
        ActionMap, "Command.AttackMoveModifier", 'A', /*CurrentTick=*/50);
    Require(Emitted == 1u && Router.GetPendingCount() == 1u,
            "Phase 39: CommandRouter translated KeyCode::A ('AttackMove') into pending command");

    auto EarlyDrain = Router.DrainCommandsForTick(51);
    Require(EarlyDrain.empty(), "Phase 39: Command held until TargetTick = 50 + 2 = 52");

    auto ReadyDrain = Router.DrainCommandsForTick(52);
    Require(ReadyDrain.size() == 1u && ReadyDrain[0].PlayerID == 2 &&
                ReadyDrain[0].Payload.CommandType == 10 &&
                ReadyDrain[0].Payload.TargetX == 640.0f,
            "Phase 39: CommandRouter drained command at exact deterministic target tick");

    // Verify inlined EcsSync.h helper
    Core::ECS::EntityManager EM;
    const Core::ECS::EntityID E1 = EM.CreateEntity();
    const Core::ECS::EntityID E2 = EM.CreateEntity();
    EM.DestroyEntity(E2);
    auto AliveSelected = Game::Client::GameLoop::CollectSelectedAliveEcsIds(EM, std::vector<int>{E1, E2});
    Require(AliveSelected.size() == 1u && AliveSelected[0] == E1,
            "Phase 39: Inlined CollectSelectedAliveEcsIds filters destroyed entities");
}

// ============================================================================
// Phase 40 Test: NavigationServer2D (Clearance, A*, FlowField, Avoidance) &
//                BehaviorTreeServer (Cloud 293 Behavior Trees)
// ============================================================================
void TestPhase40NavigationServerAndBehaviorTreeServer()
{
    Engine::Navigation::NavigationServer2D Nav;
    Nav.InitializeGrid(16, 16, 32.0f);

    // Build a vertical wall at X = 8 with:
    // - A 1-cell narrow gap at Y = 3 (only 1x1 unit can pass)
    // - A 3-cell wide gap at Y = 10..12 (both 1x1 and 2x2 units can pass)
    for (int32_t Y = 0; Y < 16; ++Y)
    {
        if (Y == 3 || (Y >= 10 && Y <= 12))
            continue;
        Nav.SetCellBlocked(8, Y, true);
    }
    Nav.RebuildClearanceField();

    Require(Nav.CanFitFootprint(8, 3, 1) && !Nav.CanFitFootprint(8, 3, 2),
            "Phase 40: Clearance field allows 1x1 at narrow gap (8,3) but blocks 2x2 footprint");
    Require(Nav.CanFitFootprint(8, 10, 2),
            "Phase 40: Clearance field allows 2x2 footprint at wide gap (8,10)");

    auto SmallPath = Nav.FindPath({2, 3}, {13, 3}, /*FootprintSize=*/1);
    auto LargePath = Nav.FindPath({2, 3}, {13, 3}, /*FootprintSize=*/2);
    Require(!SmallPath.empty() && !LargePath.empty(),
            "Phase 40: Both 1x1 and 2x2 units found valid paths across the wall");
    Require(LargePath.size() > SmallPath.size(),
            "Phase 40: 2x2 unit detours through the wide southern gap (Y=10..12)");

    Engine::Navigation::FlowField2D Flow;
    Require(Nav.BuildFlowField({14, 10}, /*FootprintSize=*/1, Flow),
            "Phase 40: Built Dijkstra FlowField toward goal (14,10)");
    Require(Flow.IsReachable(2, 10),
            "Phase 40: Start cell (2,10) is reachable in FlowField");
    const Core::Math::Vec2 FlowVec = Flow.SampleFlow(2, 10);
    Require(FlowVec.X > 0.5f,
            "Phase 40: FlowField gradient at (2,10) points east toward (14,10)");

    // Local Avoidance separation check
    Engine::Navigation::AvoidanceAgent2D AgentA{1, {100.0f, 100.0f}, {0.0f, 0.0f}, 16.0f};
    Engine::Navigation::AvoidanceAgent2D AgentB{2, {108.0f, 100.0f}, {0.0f, 0.0f}, 16.0f};
    const Core::Math::Vec2 SteerVel = Nav.ComputeAvoidanceVelocity(
        AgentA, {AgentA, AgentB}, {10.0f, 0.0f}, 100.0f);
    Require(SteerVel.X < 10.0f,
            "Phase 40: Local avoidance pushes overlapping AgentA away from AgentB");

    // Cloud BehaviorTreeServer check
    Engine::AI::BehaviorTreeServer BTServer;
    const size_t LoadedTrees = BTServer.LoadFromAssetsRDirectory("assets-R");
    Require(LoadedTrees >= 200u,
            "Phase 40: BehaviorTreeServer imported cloud AIScript/BWScript behavior trees from assets-R");

    Engine::AI::Blackboard Board;
    const auto Status = BTServer.TickTreeByName("Ter3", Board);
    Require(Status == Engine::AI::BTStatus::Success && Board.GetInt("ExecutedNodeCount") > 0,
            "Phase 40: BehaviorTreeServer executed cloud 'Ter3' AI behavior tree against Blackboard");
}

// ============================================================================
// Phase 41 Test: PackedScene2D & PrefabRegistry (228 Cloud Unit Prefabs)
// ============================================================================
void TestPhase41PackedScene2DAndCloudPrefabRegistry()
{
    Resource::ModernAssetRegistry CloudRegistry;
    Require(CloudRegistry.LoadFromAssetsRDirectory("assets-R"),
            "Phase 41: Loaded cloud assets-R ModernAssetRegistry");

    Engine::Scene::PrefabRegistry Prefabs;
    const size_t BuiltPrefabs = Prefabs.BuildUnitPrefabsFromModernRegistry(CloudRegistry);
    Require(BuiltPrefabs == 228u,
            "Phase 41: PrefabRegistry auto-generated all 228 unit/building PackedScene2D prefabs");

    Core::ECS::EntityManager EM;
    Engine::SceneTree2D SceneTree;

    // Spawn UnitID 5 (Terran Siege Tank Tank Mode, which has a Turret attachment)
    auto TankInstance = Prefabs.SpawnByID(5u, EM, SceneTree, {400.0f, 500.0f}, /*OwnerPlayerID=*/0);
    Require(TankInstance.Entity != Core::ECS::InvalidEntity && TankInstance.RootSceneNode != nullptr,
            "Phase 41: Instantiated Siege Tank PackedScene2D prefab");

    SceneTree.PropagateAllTransforms();

    const auto* UnitComp = EM.GetComponent<Game::Components::UnitComponent>(TankInstance.Entity);
    Require(UnitComp != nullptr && UnitComp->ChkUnitID == 5 && UnitComp->MaxHitPoints == 150,
            "Phase 41: Spawned Siege Tank ECS entity initialized with cloud MaxHP=150");

    Engine::Node2D* BodyNode = TankInstance.RootSceneNode->FindChild("MainBody");
    Engine::Node2D* TurretNode = TankInstance.RootSceneNode->FindChild("TurretSubunit");
    Require(BodyNode != nullptr && TurretNode != nullptr,
            "Phase 41: Siege Tank SceneTree2D hierarchy contains MainBody and TurretSubunit child nodes");
    Require( std::fabs(TurretNode->GetWorldPosition().X - 400.0f) < 1e-4f &&
                 std::fabs(TurretNode->GetWorldPosition().Y - 496.0f) < 1e-4f,
            "Phase 41: TurretSubunit bound to 'TurretSocket' resolved world offset (400, 496)");
}

// ============================================================================
// Phase 42 Test: DeterministicSessionServer (Rollback/Replay) & EngineTelemetryServer
// ============================================================================
void TestPhase42DeterministicSessionServerAndTelemetry()
{
    struct VelocityCommand
    {
        float DeltaX = 0.0f;
        float DeltaY = 0.0f;
    };

    auto StepSim = [](Core::ECS::EntityManager& EM,
                      uint32_t /*Tick*/,
                      const std::vector<Engine::Net::DeterministicSessionServer<VelocityCommand>::FrameCommandEntry>& Cmds) {
        for (Core::ECS::EntityID Id : EM.GetActiveEntities())
        {
            if (auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(Id))
            {
                Trans->Position.X += 1.0f;
                for (const auto& C : Cmds)
                {
                    Trans->Position.X += C.Command.DeltaX;
                    Trans->Position.Y += C.Command.DeltaY;
                }
            }
        }
    };

    // Authority world with command scheduled on time at Tick 2
    Core::ECS::EntityManager AuthWorld;
    const Core::ECS::EntityID AuthUnit = AuthWorld.CreateEntity();
    AuthWorld.AddComponent<Game::Components::TransformComponent>(AuthUnit).Position = {10.0f, 20.0f};

    Engine::Net::DeterministicSessionServer<VelocityCommand> AuthSession;
    AuthSession.BindSimulationStepCallback(StepSim);
    AuthSession.ScheduleCommand(2u, /*PlayerID=*/1, VelocityCommand{50.0f, -10.0f});

    for (uint32_t T = 0; T < 6; ++T)
        AuthSession.AdvanceTick(AuthWorld);

    // Client world that initially misses Player 1's command at Tick 2, advances to Tick 6,
    // and then receives the late command for Tick 2 and rolls back + resimulates!
    Core::ECS::EntityManager ClientWorld;
    const Core::ECS::EntityID ClientUnit = ClientWorld.CreateEntity();
    ClientWorld.AddComponent<Game::Components::TransformComponent>(ClientUnit).Position = {10.0f, 20.0f};

    Engine::Net::DeterministicSessionServer<VelocityCommand> ClientSession;
    ClientSession.BindSimulationStepCallback(StepSim);
    for (uint32_t T = 0; T < 6; ++T)
        ClientSession.AdvanceTick(ClientWorld);

    // Before rollback, Tick 5 hash mismatches authority
    Require(!ClientSession.VerifyRemoteTickHash(5u, AuthSession.GetRecordedTickHash(5u)),
            "Phase 42: Desync detected before late command rollback");

    // Inject late command at Tick 2 and resimulate Ticks 2..5
    Require(ClientSession.InjectLateCommandAndResimulate(
                ClientWorld, 2u, /*PlayerID=*/1, VelocityCommand{50.0f, -10.0f}),
            "Phase 42: Late command rollback & resimulation succeeded");

    Require(ClientSession.VerifyRemoteTickHash(5u, AuthSession.GetRecordedTickHash(5u)),
            "Phase 42: Post-rollback state hash matches authority bit-for-bit at Tick 5");

    // Verify Replay binary export and import into a 3rd ReplayWorld
    std::vector<uint8_t> ReplayBytes;
    Require(AuthSession.ExportReplayBinary(ReplayBytes) && !ReplayBytes.empty(),
            "Phase 42: ExportReplayBinary succeeded");

    Core::ECS::EntityManager ReplayWorld;
    const Core::ECS::EntityID ReplayUnit = ReplayWorld.CreateEntity();
    ReplayWorld.AddComponent<Game::Components::TransformComponent>(ReplayUnit).Position = {10.0f, 20.0f};

    Engine::Net::DeterministicSessionServer<VelocityCommand> ReplaySession;
    ReplaySession.BindSimulationStepCallback(StepSim);
    uint32_t TotalReplayTicks = 0;
    Require(ReplaySession.ImportReplayBinary(ReplayBytes, &TotalReplayTicks) && TotalReplayTicks == 6u,
            "Phase 42: ImportReplayBinary restored 6 ticks of commands");
    for (uint32_t T = 0; T < TotalReplayTicks; ++T)
        ReplaySession.AdvanceTick(ReplayWorld);

    Require(ReplaySession.GetRecordedTickHash(5u) == AuthSession.GetRecordedTickHash(5u),
            "Phase 42: Deterministic replay playback reproduced exact authority state hash");

    // Verify EngineTelemetryServer inspection and full EngineApp + StarCraftEnginePlugin integration
    Engine::EngineApp App;
    App.AddPlugin<Game::StarCraftEnginePlugin>("assets-R");
    App.Initialize();
    App.StepFixedTicks(2);

    App.Context().Telemetry().RecordSystemSample("CombatSystem", 145.0);
    App.Context().Telemetry().RecordSystemSample("CombatSystem", 180.0);
    const auto* Prof = App.Context().Telemetry().FindSystemProfile("CombatSystem");
    Require(Prof != nullptr && Prof->InvocationCount == 2u && Prof->MaxMicroseconds == 180.0,
            "Phase 42: EngineTelemetryServer recorded system profile metrics");

    Require(App.Context().Prefabs().GetTable().Size() == 228u &&
                App.Context().BehaviorTrees().GetTreeTable().Size() >= 200u,
            "Phase 42: StarCraftEnginePlugin auto-populated PrefabRegistry (228) and BehaviorTreeServer (>=200) on EngineApp startup");

    auto SpawnedMarine = App.Context().Prefabs().SpawnByID(
        0u, App.Entities(), App.Context().SceneTree(), {256.0f, 256.0f}, 0);
    Require(SpawnedMarine.Entity != Core::ECS::InvalidEntity,
            "Phase 42: Spawned Terran Marine via EngineContext::Prefabs()");

    auto MemReport = App.Context().Telemetry().InspectWorld(App.Entities());
    Require(MemReport.LivingEntities >= 1u && MemReport.ActiveComponentPools >= 4u &&
                MemReport.TotalDenseComponentBytes > 0u,
            "Phase 42: EngineTelemetryServer::InspectWorld reported live ECS pool memory usage");

    App.Shutdown();
}

} // namespace

int main()
{
    TestPhase37WorldSerializerAndStreamlinedSaveState();
    std::cout << "[PASS] Phase 37: WorldSerializer (CRC32 + CustomComponentBinarySerializer) & Streamlined GameSaveState verified.\n";

    TestPhase38UnifiedNode2DAndRenderGraph2D();
    std::cout << "[PASS] Phase 38: Unified SceneNode2D (Engine::Node2D) & Multi-Pass RenderGraph2D verified.\n";

    TestPhase39CommandRouterAndStreamlinedMainHelpers();
    std::cout << "[PASS] Phase 39: Engine::Input::CommandRouter & Streamlined Runtime Helpers verified.\n";

    TestPhase40NavigationServerAndBehaviorTreeServer();
    std::cout << "[PASS] Phase 40: Engine::Navigation::NavigationServer2D & Cloud BehaviorTreeServer verified.\n";

    TestPhase41PackedScene2DAndCloudPrefabRegistry();
    std::cout << "[PASS] Phase 41: Engine::Scene::PackedScene2D & Cloud 228-Unit PrefabRegistry verified.\n";

    TestPhase42DeterministicSessionServerAndTelemetry();
    std::cout << "[PASS] Phase 42: Engine::Net::DeterministicSessionServer, Replay, & EngineTelemetryServer verified.\n";

    std::cout << "[Phase37To42EngineEvolutionTest] ALL PHASE 37-42 ENGINE EVOLUTION & SIMPLIFICATION TESTS PASSED.\n";
    return 0;
}
