#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#include "Core/Audio/AudioSystem.h"
#include "Engine/Engine.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/StarCraftEnginePlugin.h"
#include "Game/Systems/SystemAssetBridge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace Core::Platform
{
    bool IsKeyPressed(int) { return false; }
    bool IsKeyDown(int) { return false; }
    bool IsKeyReleased(int) { return false; }
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
    if (Condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message);
    std::exit(1);
}

// Custom game-agnostic component (non-trivial, holds std::string & std::vector)
// to prove GenericWorldSnapshot works for arbitrary non-StarCraft components.
struct CustomScriptingMetadataComponent
{
    std::string ScriptTag;
    std::vector<int> WaypointTags;
};

struct CustomWorldRuleResource
{
    int GravityScale = 100;
    float WindSpeed = 3.5f;
};

void TestPhase31GenericTypeErasedWorldSnapshotAndRollback()
{
    Core::ECS::EntityManager EM;
    Engine::ECS::GenericRollbackBuffer<16> Rollback;
    Rollback.RegisterWorldResource<CustomWorldRuleResource>();
    Rollback.RegisterWorldResource<Game::Systems::PlayerResourceStore>();

    EM.SetResource<CustomWorldRuleResource>(CustomWorldRuleResource{120, 7.25f});
    Game::Systems::HarvestSystem::ResetResources(EM);
    Game::Systems::HarvestSystem::GetResources(EM, 0).Minerals = 650;
    Game::Systems::HarvestSystem::GetResources(EM, 0).VespeneGas = 300;

    const auto HandleA = EM.CreateEntityHandle();
    auto& TransA = EM.AddComponent<Game::Components::TransformComponent>(HandleA.Id);
    TransA.Position = {256.0f, 512.0f};
    auto& UnitA = EM.AddComponent<Game::Components::UnitComponent>(HandleA.Id);
    UnitA.ChkUnitID = 0; // Marine
    UnitA.HitPoints = 40;
    UnitA.MaxHitPoints = 40;
    auto& MetaA = EM.AddComponent<CustomScriptingMetadataComponent>(HandleA.Id);
    MetaA.ScriptTag = "AlphaSquadLeader";
    MetaA.WaypointTags = {10, 20, 30};

    // Spawn and immediately destroy a transient entity BEFORE Tick 5 to verify
    // post-rollback allocator determinism and ABA generational handle protection.
    const auto TransientBeforeSnap = EM.CreateEntityHandle();
    EM.DestroyEntity(TransientBeforeSnap.Id);
    Require(!EM.IsAlive(TransientBeforeSnap), "Phase 31: Destroyed transient handle must be dead before snapshot");

    // Record snapshot at Tick 5
    Rollback.RecordFrame(EM, 5u);
    const auto* Snap5 = Rollback.FindSnapshot(5u);
    Require(Snap5 != nullptr, "Phase 31: Snapshot for tick 5 must exist");
    Require(Snap5->GetEntityCount() == 1, "Phase 31: Snapshot must capture 1 entity");
    Require(Snap5->GetCapturedPoolCount() >= 3, "Phase 31: Snapshot must capture both binary and cloned component pools");

    // Verify restoring Snap5 into a brand-new EntityManager (uninitialized pools)
    {
        Core::ECS::EntityManager FreshEM;
        Snap5->Restore(FreshEM);
        Require(FreshEM.IsAlive(HandleA), "Phase 31: Snapshot restored into fresh EM must preserve Entity A handle");
        const auto* FreshMeta = FreshEM.GetComponent<CustomScriptingMetadataComponent>(HandleA);
        Require(FreshMeta && FreshMeta->ScriptTag == "AlphaSquadLeader",
                "Phase 31: Snapshot restored into fresh EM must auto-create missing component pools");
    }

    // Mutate world at Tick 6: damage A, change non-trivial component, spawn B, mutate resources
    TransA.Position = {999.0f, 888.0f};
    UnitA.HitPoints = 5;
    MetaA.ScriptTag = "MutatedCorruptedTag";
    MetaA.WaypointTags = {99};
    EM.GetResource<CustomWorldRuleResource>()->GravityScale = 999;
    Game::Systems::HarvestSystem::GetResources(EM, 0).Minerals = 10;

    const auto HandleB = EM.CreateEntityHandle();
    EM.AddComponent<Game::Components::TransformComponent>(HandleB.Id).Position = {100.0f, 100.0f};

    // Roll back to Tick 5
    Require(Rollback.RollbackToTick(EM, 5u), "Phase 31: RollbackToTick(5) must succeed");
    Require(EM.IsAlive(HandleA), "Phase 31: Entity A handle generation must remain valid after rollback");
    Require(!EM.IsAlive(HandleB.Id), "Phase 31: Entity B spawned after tick 5 must be removed on rollback");
    Require(!EM.IsAlive(TransientBeforeSnap),
            "Phase 31: Transient entity destroyed prior to snapshot must remain invalid after rollback (no ABA regression)");

    // Replay spawning at Tick 6 after rollback: must produce the exact same EntityID as HandleB.Id!
    const auto HandleBReplay = EM.CreateEntityHandle();
    Require(HandleBReplay.Id == HandleB.Id,
            "Phase 31: Entity spawned after rollback must receive identical deterministic EntityID as pre-rollback timeline");
    EM.DestroyEntity(HandleBReplay.Id);

    const auto* RestoredTransA = EM.GetComponent<Game::Components::TransformComponent>(HandleA);
    const auto* RestoredUnitA = EM.GetComponent<Game::Components::UnitComponent>(HandleA);
    const auto* RestoredMetaA = EM.GetComponent<CustomScriptingMetadataComponent>(HandleA);
    Require(RestoredTransA && std::fabs(RestoredTransA->Position.X - 256.0f) < 1e-4f &&
            std::fabs(RestoredTransA->Position.Y - 512.0f) < 1e-4f,
            "Phase 31: Binary TransformComponent must be restored identically");
    Require(RestoredUnitA && RestoredUnitA->HitPoints == 40,
            "Phase 31: Binary UnitComponent HitPoints must be restored identically");
    Require(RestoredMetaA && RestoredMetaA->ScriptTag == "AlphaSquadLeader" &&
            RestoredMetaA->WaypointTags.size() == 3 && RestoredMetaA->WaypointTags[2] == 30,
            "Phase 31: Non-trivial cloned CustomScriptingMetadataComponent must be restored identically");
    Require(EM.GetResource<CustomWorldRuleResource>()->GravityScale == 120,
            "Phase 31: CustomWorldRuleResource must be restored via resource hook");
    Require(Game::Systems::HarvestSystem::GetResources(EM, 0).Minerals == 650,
            "Phase 31: PlayerResourceStore minerals must be restored via resource hook");
}

void TestPhase32EngineTimeAccumulatorAndPooledSceneTree2D()
{
    // 1. TimeStepAccumulator deterministic fixed-step & interpolation
    Engine::TimeStepAccumulator Clock(1.0f / 24.0f, 4);
    const int Steps1 = Clock.AdvanceFrame(2.5f / 24.0f);
    Require(Steps1 == 2, "Phase 32: 2.5 fixed steps must execute 2 ticks");
    Require(Clock.GetTickCount() == 2, "Phase 32: TickCount must equal 2");
    Require(std::fabs(Clock.GetInterpolationAlpha() - 0.5f) < 1e-3f,
            "Phase 32: Remainder 0.5 tick must yield InterpolationAlpha = 0.5");

    // Spiral-of-death clamp test (1.0 second lag spike clamped to MaxSubSteps = 4)
    const int StepsClamped = Clock.AdvanceFrame(1.0f);
    Require(StepsClamped == 4, "Phase 32: Lag spike must clamp to MaxSubStepsPerFrame = 4");

    // 2. SceneTree2D hierarchy, 2D Sockets, dirty transform propagation & zero-alloc pooling
    Engine::SceneTree2D Tree;
    auto* UnitRoot = Tree.SpawnChild(Tree.GetRoot(), "SiegeTankRoot", 1u);
    UnitRoot->SetLocalPosition({400.0f, 300.0f});
    UnitRoot->SetLocalZIndex(10);
    UnitRoot->DefineSocket("TurretMount", {6.0f, -8.0f});

    auto* TurretNode = Tree.SpawnChild(*UnitRoot, "SiegeTurret", 2u);
    TurretNode->BindToParentSocket("TurretMount");
    TurretNode->SetLocalPosition({2.0f, 3.0f});
    TurretNode->SetLocalZIndex(2, true);

    const uint32_t Recomputed1 = Tree.PropagateAllTransforms();
    Require(Recomputed1 >= 2, "Phase 32: First transform propagation must compute dirty nodes");
    const auto TurretWorld = TurretNode->GetWorldPosition();
    Require(std::fabs(TurretWorld.X - 408.0f) < 1e-4f && std::fabs(TurretWorld.Y - 295.0f) < 1e-4f,
            "Phase 32: Turret world position must combine Parent(400,300) + Socket(6,-8) + Local(2,3) = (408,295)");
    Require(TurretNode->GetWorldZIndex() == 12, "Phase 32: Relative ZIndex must combine 10 + 2 = 12");

    // Second propagation without mutation must do 0 recomputations
    const uint32_t Recomputed2 = Tree.PropagateAllTransforms();
    Require(Recomputed2 == 0, "Phase 32: Unmodified SceneTree2D must recompute 0 transforms");

    // Recycle subtree and re-acquire to verify object pool reuse
    const uint64_t AllocsBefore = Tree.GetPoolAllocations();
    Tree.ClearAndRecycleToPool();
    Require(Tree.GetPooledNodeCount() == 2, "Phase 32: Recycled nodes must return to pool");
    auto* ReusedNode = Tree.SpawnChild(Tree.GetRoot(), "ReusedUnit", 3u);
    Require(ReusedNode != nullptr && Tree.GetPoolAllocations() == AllocsBefore && Tree.GetPoolReuses() >= 1,
            "Phase 32: Spawning from warm SceneTree2D pool must reuse pooled Node2D with 0 new allocations");
}

void TestPhase33DataAssetTableAndUnifiedAudioEventRouter()
{
    // 1. Generic DataAssetTable<T>
    struct SampleProjectileSpec
    {
        uint32_t Id = 0;
        float Speed = 0.0f;
    };
    Engine::DataAssetTable<SampleProjectileSpec> Table;
    auto Handle1 = Table.Register(42u, "GaussRifleBullet", SampleProjectileSpec{42u, 600.0f});
    Require(Handle1.IsValid(), "Phase 33: Registered DataAssetTable handle must be valid");
    Require(Table.FindById(42u) && std::fabs(Table.FindById(42u)->Speed - 600.0f) < 1e-4f,
            "Phase 33: FindById must resolve registered asset");
    Require(Table.FindByName("GaussRifleBullet") != nullptr,
            "Phase 33: FindByName must resolve registered asset");

    // Hot-reload asset 42 -> bumps generation and invalidates old Handle1
    auto Handle2 = Table.Register(42u, "GaussRifleBullet", SampleProjectileSpec{42u, 750.0f});
    Require(Table.Resolve(Handle1) == nullptr, "Phase 33: Stale handle generation must resolve to nullptr after hot-reload");
    Require(Table.Resolve(Handle2) != nullptr && std::fabs(Table.Resolve(Handle2)->Speed - 750.0f) < 1e-4f,
            "Phase 33: New handle generation must resolve updated asset");

    auto Handle3 = Table.BumpGeneration(42u);
    Require(Handle3.IsValid() && Table.Resolve(Handle2) == nullptr && Table.Resolve(Handle3) != nullptr,
            "Phase 33: BumpGeneration must invalidate prior handle generation and return valid bumped handle");

    // 2. Unified ModernAudioEventRouterSystem (AnimAudioNotifyEvent + SoundRequestEvent)
    Core::ECS::SystemManager SM;
    Core::Audio::ModernAudioEventRouterSystem AudioRouter(SM);
    AudioRouter.GetBusMixer().SetCoalesceRadius(64.0f);

    // Emit SoundRequestEvent (legacy simulation sound event)
    SM.GetEventBus().Emit(Game::Events::SoundRequestEvent{15, 200.0f, 300.0f, 20});
    Require(AudioRouter.GetDispatchedHistory().size() == 1 &&
            AudioRouter.GetDispatchedHistory()[0].SoundID == 15u,
            "Phase 33: ModernAudioEventRouterSystem must bridge SoundRequestEvent into AudioBusMixer");

    // Emit duplicate AnimAudioNotifyEvent within 64px coalesce radius on same frame -> coalesced!
    Game::Events::AnimAudioNotifyEvent DupEv{};
    DupEv.SoundID = 15u;
    DupEv.WorldPos = {210.0f, 305.0f};
    SM.GetEventBus().Emit(DupEv);
    Require(AudioRouter.GetDispatchedHistory().size() == 1 &&
            AudioRouter.GetBusMixer().GetTelemetry().CoalescedDuplicateCount == 1,
            "Phase 33: Same-frame SoundRequestEvent and AnimAudioNotifyEvent within coalesce radius must deduplicate");
}

void TestPhase34CloudModernAssetRegistryBridgeAndMultiWorldIsolation()
{
    Resource::ModernAssetRegistry Registry;
    if (Registry.LoadFromAssetsRDirectory("assets-R"))
    {
        Game::Systems::SystemAssetBridge::BindModernAssetRegistry(Registry);
        const int SyncedCount = Game::StarCraftEnginePlugin::SyncUnitPrototypesFromModernAssets(Registry);
        Require(SyncedCount > 0, "Phase 34: SyncUnitPrototypesFromModernAssets must sync unit defs from cloud assets-R");

        // Verify BuildingSystem & ProductionSystem use cloud ModernAssetRegistry resolvers
        Core::ECS::SystemManager SM;
        auto& EM = SM.GetEntityManager();
        Game::Map::GameMap Map;
        Map.Generate(128, 128);
        auto& SimCtx = Game::SimulationContext::Get(EM);
        SimCtx.Map = &Map;
        SimCtx.MutableMap = &Map;
        Game::Systems::HarvestSystem::ResetResources(EM);
        Game::Systems::HarvestSystem::GetResources(EM, 0).Minerals = 2000;
        Game::Systems::HarvestSystem::GetResources(EM, 0).VespeneGas = 2000;
        Game::Systems::HarvestSystem::GetResources(EM, 0).SupplyMax = 200;

        // Spawn Siege Tank (Tank Mode, ChkUnitID = 5) via ProductionSystem::DebugSpawnUnit
        const int TankDefID = Game::Races::GetUnitDefIDByChkUnitID(5);
        Require(TankDefID >= 0, "Phase 34: Siege Tank DefID must exist");
        const int TankEntity = Game::Systems::ProductionSystem::DebugSpawnUnit(EM, TankDefID, {512.0f, 512.0f}, 0);
        Require(TankEntity > 0, "Phase 34: DebugSpawnUnit for Siege Tank must succeed");
        const auto* TankRender = EM.GetComponent<Game::Components::RenderComponent>(TankEntity);
        Require(TankRender != nullptr && TankRender->ImageID == 250u && TankRender->SubunitImageID == 251u,
                "Phase 34: Siege Tank spawned with SystemAssetBridge must resolve HD ImageID=250 and Turret SubunitImageID=251 from cloud assets-R");
    }

    // Verify Multi-World BulletSystem ActiveHaloRockets isolation
    Core::ECS::SystemManager WorldA;
    Core::ECS::SystemManager WorldB;
    auto& EMA = WorldA.GetEntityManager();
    auto& EMB = WorldB.GetEntityManager();
    Game::Systems::BulletSystem BulletSysA(WorldA);
    Game::Systems::BulletSystem BulletSysB(WorldB);

    Game::Systems::BulletSystem::SetVisualResolver(
        [](int /*ChkUnitID*/, bool /*PreferAir*/) -> std::optional<Game::Systems::BulletSystem::VisualSpec>
        {
            Game::Systems::BulletSystem::VisualSpec V{};
            V.ImageID = 965u; // kValkyrieHaloRocketImageID
            return V;
        });

    auto SpawnValkyriePair = [](Core::ECS::EntityManager& EM) -> std::pair<int, int>
    {
        const int Src = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Src).Position = {100.0f, 100.0f};
        EM.AddComponent<Game::Components::UnitComponent>(Src).ChkUnitID = 58; // Valkyrie
        EM.AddComponent<Game::Components::CombatComponent>(Src).AirDamage = 6;
        EM.AddComponent<Game::Components::PlayerComponent>(Src).PlayerID = 0;

        const int Tgt = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Tgt).Position = {200.0f, 100.0f};
        auto& TgtUnit = EM.AddComponent<Game::Components::UnitComponent>(Tgt);
        TgtUnit.ChkUnitID = 43; // Mutalisk
        TgtUnit.HitPoints = 120;
        TgtUnit.MaxHitPoints = 120;
        EM.AddComponent<Game::Components::CombatComponent>(Tgt).MyElevation = Game::Components::Elevation::Air;
        EM.AddComponent<Game::Components::PlayerComponent>(Tgt).PlayerID = 1;
        return {Src, Tgt};
    };

    auto [SrcA, TgtA] = SpawnValkyriePair(EMA);
    auto [SrcB, TgtB] = SpawnValkyriePair(EMB);

    // Exhaust 80 Halo Rockets in WorldA
    int SpawnedInA = 0;
    for (int i = 0; i < 85; ++i)
    {
        const int Bid = BulletSysA.SpawnBullet(
            EMA,
            *EMA.GetComponent<Game::Components::CombatComponent>(SrcA),
            *EMA.GetComponent<Game::Components::TransformComponent>(SrcA),
            *EMA.GetComponent<Game::Components::PlayerComponent>(SrcA),
            SrcA, 6, 0, 192.0f, TgtA, {200.0f, 100.0f}, 1, false);
        if (Bid > 0)
            ++SpawnedInA;
    }
    Require(SpawnedInA == 80, "Phase 35: WorldA must cap Halo Rockets at 80");

    // WorldB must still be able to spawn Halo Rockets independently!
    const int BulletInB = BulletSysB.SpawnBullet(
        EMB,
        *EMB.GetComponent<Game::Components::CombatComponent>(SrcB),
        *EMB.GetComponent<Game::Components::TransformComponent>(SrcB),
        *EMB.GetComponent<Game::Components::PlayerComponent>(SrcB),
        SrcB, 6, 0, 192.0f, TgtB, {200.0f, 100.0f}, 1, false);
    Require(BulletInB > 0, "Phase 35: WorldB Halo Rocket cap must be isolated from WorldA via WorldContext");
}

void TestPhase36FullStackEngineAppWithStarCraftPlugin()
{
    Engine::EngineApp App(1.0f / 24.0f, 4);
    App.SetAutoRecordRollbackSnapshots(true);

    auto& ScPlugin = App.AddPlugin<Game::StarCraftEnginePlugin>("assets-R");
    App.Initialize();

    Require(App.IsInitialized(), "Phase 36: EngineApp must initialize cleanly");
    Require(App.GetPluginCount() == 1 && App.FindPlugin("StarCraftEnginePlugin") == &ScPlugin,
            "Phase 36: StarCraftEnginePlugin must be registered and discoverable on EngineApp");
    Require(ScPlugin.GetSpatialGroup() != nullptr &&
            ScPlugin.GetEconomyGroup() != nullptr &&
            ScPlugin.GetCombatGroup() != nullptr &&
            ScPlugin.GetRenderPipeline() != nullptr,
            "Phase 36: StarCraftEnginePlugin must install all hierarchical SystemGroups and ModernRenderPipelineSystem");

    auto& EM = App.Entities();
    Game::Systems::HarvestSystem::GetResources(EM, 0).Minerals = 500;

    // Spawn a Marine in EngineApp's primary world
    const int MarineDefID = Game::Races::GetUnitDefIDByChkUnitID(0);
    const int MarineId = Game::Systems::ProductionSystem::DebugSpawnUnit(EM, MarineDefID, {320.0f, 320.0f}, 0);
    Require(MarineId > 0, "Phase 36: Marine must spawn in EngineApp primary world");

    // Step 2 fixed ticks (records rollback snapshots for ticks 1 and 2)
    App.StepFixedTicks(2u);
    Require(App.Context().Time().GetTickCount() == 2u, "Phase 36: EngineApp must advance 2 fixed ticks");
    Require(App.RollbackBuffer().GetRecordedCount() == 2u, "Phase 36: EngineApp must auto-record 2 rollback snapshots");

    // Mutate Marine position and minerals, then step a 2-substep frame (2.0f / 24.0f)
    // to verify that multi-substep StepFrame records distinct snapshots for Tick 3 and Tick 4.
    EM.GetComponent<Game::Components::TransformComponent>(MarineId)->Position = {700.0f, 700.0f};
    Game::Systems::HarvestSystem::GetResources(EM, 0).Minerals = 50;
    const int SubStepsExecuted = App.StepFrame(2.0f / 24.0f);
    Require(SubStepsExecuted == 2, "Phase 36: StepFrame(2/24) must execute 2 fixed sub-steps");
    Require(App.Context().Time().GetTickCount() == 4u, "Phase 36: TickCount must reach 4 after 2 sub-steps");
    Require(App.RollbackBuffer().FindSnapshot(3u) != nullptr &&
            App.RollbackBuffer().FindSnapshot(4u) != nullptr,
            "Phase 36: Multi-substep StepFrame must record distinct snapshots for both Tick 3 and Tick 4");

    Require(ScPlugin.GetRenderPipeline()->GetRenderQueue().GetCommands().size() >= 1,
            "Phase 36: EngineApp StepFrame must run RenderExtract and populate RenderQueue2D");

    // Roll back EngineApp primary world to Tick 1
    Require(App.RollbackBuffer().RollbackToTick(EM, 1u),
            "Phase 36: EngineApp GenericRollbackBuffer must restore Tick 1");
    Require(std::fabs(EM.GetComponent<Game::Components::TransformComponent>(MarineId)->Position.X - 320.0f) < 1e-3f,
            "Phase 36: Marine position must roll back to (320, 320)");
    Require(Game::Systems::HarvestSystem::GetResources(EM, 0).Minerals == 500,
            "Phase 36: Player minerals must roll back to 500");

    // Verify Auxiliary World creation on EngineApp
    auto& AuxWorld = App.CreateOrGetAuxiliaryWorld("AISimulationSandbox");
    const int AuxEntity = AuxWorld.GetEntityManager().CreateEntity();
    Require(AuxEntity > 0 && App.GetAuxiliaryWorldCount() == 1,
            "Phase 36: EngineApp must support isolated auxiliary ECS worlds");

    App.Shutdown();
    Require(!App.IsInitialized(), "Phase 36: EngineApp must shut down cleanly");
}

} // namespace

int main()
{
    TestPhase31GenericTypeErasedWorldSnapshotAndRollback();
    std::cout << "[PASS] Phase 31: Game-Agnostic Type-Erased GenericWorldSnapshot & Rollback verified.\n";

    TestPhase32EngineTimeAccumulatorAndPooledSceneTree2D();
    std::cout << "[PASS] Phase 32: Engine::TimeStepAccumulator & Pooled SceneTree2D with 2D Sockets verified.\n";

    TestPhase33DataAssetTableAndUnifiedAudioEventRouter();
    std::cout << "[PASS] Phase 33: Engine::DataAssetTable<T> & Unified ModernAudioEventRouterSystem verified.\n";

    TestPhase34CloudModernAssetRegistryBridgeAndMultiWorldIsolation();
    std::cout << "[PASS] Phase 34 & 35: Cloud ModernAssetRegistry SystemAssetBridge & Multi-World Halo Rocket Isolation verified.\n";

    TestPhase36FullStackEngineAppWithStarCraftPlugin();
    std::cout << "[PASS] Phase 36: Full-Stack Engine::EngineApp + Game::StarCraftEnginePlugin Lifecycle & Rollback verified.\n";

    std::cout << "ALL PHASE 31-36 ENGINE FOUNDATION & SIMPLIFICATION TESTS PASSED!\n";
    return 0;
}
