#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SoAComponentStream.h"
#include "Core/ECS/SystemGroup.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/NavigationWorldResource.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/CommandSystem.h"
#include "Render/ModernRenderPipeline.h"
#include "Render/RenderServer2D.h"
#include "Resource/ModernAssetRegistry.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

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

struct SharedWorldConfigResource
{
    int TickRateHz = 24;
    uint32_t DeterministicSeed = 0xC0FFEEu;
};

struct SparseBeaconComponent
{
    int BeaconCode = 0;
};

struct UnpopulatedTagComponent
{
    int Unused = 0;
};

struct DeferredSpawnedTag
{
    int CreatedInPhase = -1;
};

void TestUnifiedWorldResourceAndO1SlotLookup()
{
    Core::ECS::EntityManager EM;

    auto& SetRef = EM.SetResource<SharedWorldConfigResource>(SharedWorldConfigResource{30, 0x12345678u});
    Require(EM.HasResource<SharedWorldConfigResource>(), "EM.HasResource reports resource present");
    Require(EM.Context().HasResource<SharedWorldConfigResource>(), "EM.Context().HasResource reports same resource present");

    auto* CtxPtr = EM.Context().GetResource<SharedWorldConfigResource>();
    Require(CtxPtr == &SetRef, "EM.GetResource and EM.Context().GetResource resolve to the exact same O(1) slot instance");
    Require(CtxPtr->TickRateHz == 30 && CtxPtr->DeterministicSeed == 0x12345678u, "Unified resource values match");
    Require(EM.Context().GetResourceCount() == 1u, "WorldContext tracks exact active resource count");

    EM.RemoveResource<SharedWorldConfigResource>();
    Require(!EM.HasResource<SharedWorldConfigResource>() && !EM.Context().HasResource<SharedWorldConfigResource>(),
            "Removing resource clears unified WorldContext slot");
    Require(EM.Context().GetResourceCount() == 0u, "WorldContext resource count is 0 after removal");
}

void TestPivotQueryAccelerationAndZeroAllocScratchBuffers()
{
    Core::ECS::EntityManager EM;
    EM.ResetQueryTelemetry();

    for (int i = 0; i < 2000; ++i)
    {
        const Core::ECS::EntityID Id = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Id).Position = {
            static_cast<float>(i * 4), static_cast<float>(i * 2)
        };
        if (i % 500 == 0)
        {
            EM.AddComponent<SparseBeaconComponent>(Id).BeaconCode = i + 100;
        }
    }

    auto EmptyResult = EM.GetEntitiesWith<Game::Components::TransformComponent, UnpopulatedTagComponent>();
    Require(EmptyResult.empty(), "Intersection with unpopulated component is empty");
    Require(EM.GetQueryTelemetry().EarlyOutEmptyPoolCount == 1u,
            "Unpopulated component pool triggers O(1) EarlyOutEmptyPoolCount");

    auto Beacons = EM.GetEntitiesWith<Game::Components::TransformComponent, SparseBeaconComponent>();
    Require(Beacons.size() == 4u, "Pivot-accelerated GetEntitiesWith finds all 4 sparse entities");
    Require(EM.GetQueryTelemetry().PivotAcceleratedQueries >= 1u,
            "GetEntitiesWith recorded PivotAcceleratedQueries");

    for (int Pass = 0; Pass < 5; ++Pass)
    {
        int Visited = 0;
        EM.ForEachComponent<Game::Components::TransformComponent, SparseBeaconComponent>(
            [&](Core::ECS::EntityID Id,
                Game::Components::TransformComponent& Trans,
                SparseBeaconComponent& Beacon)
            {
                Require(EM.IsAlive(Id), "Visited entity is alive");
                Require(Beacon.BeaconCode >= 100, "BeaconCode is valid");
                Trans.Position.X += 1.0f;
                ++Visited;

                if (Visited == 1)
                {
                    int NestedCount = 0;
                    EM.ForEachEntityWith<SparseBeaconComponent>([&](Core::ECS::EntityID) {
                        ++NestedCount;
                    });
                    Require(NestedCount == 4, "Nested ForEachEntityWith succeeds without corrupting outer scratch buffer");
                }
            });
        Require(Visited == 4, "ForEachComponent visited all 4 sparse entities");
    }

    Require(EM.GetQueryTelemetry().ScratchBufferReuses >= 8u,
            "ForEachComponent & ForEachEntityWith reused pre-allocated scratch buffers without heap reallocation");
}

class PhaseBarrierProducerSystem final : public Core::ECS::ISystem
{
public:
    explicit PhaseBarrierProducerSystem(Core::ECS::SystemManager& SysMgr)
        : mSysMgr(SysMgr) {}

    std::string_view GetName() const override { return "PhaseBarrierProducerSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::FixedSimulation; }

    void Update(float) override
    {
        auto& EM = mSysMgr.GetEntityManager();
        EM.Commands().CreateEntity([](Core::ECS::EntityManager& World) {
            const Core::ECS::EntityID E = World.CreateEntity();
            World.AddComponent<DeferredSpawnedTag>(E).CreatedInPhase =
                static_cast<int>(Core::ECS::SystemPhase::FixedSimulation);
        });
    }

private:
    Core::ECS::SystemManager& mSysMgr;
};

class PhaseBarrierConsumerSystem final : public Core::ECS::ISystem
{
public:
    explicit PhaseBarrierConsumerSystem(Core::ECS::SystemManager& SysMgr)
        : mSysMgr(SysMgr) {}

    std::string_view GetName() const override { return "PhaseBarrierConsumerSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::TransformPropagation; }

    void Update(float) override
    {
        auto& EM = mSysMgr.GetEntityManager();
        ObservedDeferredCount = EM.GetComponentCount<DeferredSpawnedTag>();
    }

    size_t ObservedDeferredCount = 0;

private:
    Core::ECS::SystemManager& mSysMgr;
};

class PresentationCounterSystem final : public Core::ECS::ISystem
{
public:
    std::string_view GetName() const override { return "PresentationCounterSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::Presentation; }

    void Update(float) override { ++PresentationTicks; }

    int PresentationTicks = 0;
};

void TestSystemManagerPhaseBarriersAndPlayerLoopSplit()
{
    Core::ECS::SystemManager SM;
    SM.RegisterSystem(std::make_unique<PhaseBarrierProducerSystem>(SM));
    SM.RegisterSystem(std::make_unique<PhaseBarrierConsumerSystem>(SM));
    SM.RegisterSystem(std::make_unique<PresentationCounterSystem>());

    auto* Consumer = SM.FindSystem<PhaseBarrierConsumerSystem>();
    auto* PresSys = SM.FindSystem<PresentationCounterSystem>();
    Require(Consumer != nullptr && PresSys != nullptr, "FindSystem<T> resolves registered systems by type");

    SM.UpdateSimulationStep(1.0f / 24.0f);
    Require(Consumer->ObservedDeferredCount == 1u,
            "EntityCommandBuffer barrier automatically flushed between FixedSimulation and TransformPropagation");
    Require(PresSys->PresentationTicks == 0,
            "UpdateSimulationStep does not tick Presentation phase systems");

    SM.UpdatePresentationFrame(1.0f / 60.0f);
    Require(PresSys->PresentationTicks == 1,
            "UpdatePresentationFrame ticks Presentation phase systems");
    Require(SM.GetCommandBarrierFlushCount() >= 2u,
            "SystemManager recorded automatic command barrier flushes");
}

void TestCsrFinalizedSpatialIndexAndBoundedEventBus()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();
    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));

    for (int i = 0; i < 64; ++i)
    {
        const Core::ECS::EntityID E = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(E).Position = {
            static_cast<float>((i % 8) * 48), static_cast<float>((i / 8) * 48)
        };
        auto& U = EM.AddComponent<Game::Components::UnitComponent>(E);
        U.HitPoints = 45;
        EM.AddComponent<Game::Components::PlayerComponent>(E).PlayerID = (i % 2);
    }

    SM.UpdateSimulationStep(1.0f / 24.0f);

    const auto* SpatialRes = EM.GetResource<Game::Pathfinding::SpatialIndexResource>();
    Require(SpatialRes != nullptr, "SpatialIndexResource accessible via unified EM.GetResource");
    Require(SpatialRes->SpatialHash.IsCSRFinalized(),
            "WorldSpatialIndexSystem finalizes SpatialHash CSR array");
    Require(SpatialRes->AllUnitsHash.IsCSRFinalized(),
            "WorldSpatialIndexSystem finalizes AllUnitsHash CSR array");

    const Core::ECS::EntityID Enemy = SpatialRes->FindNearestEnemy(EM, {0.0f, 0.0f}, 128.0f, 0);
    Require(Enemy != Core::ECS::InvalidEntity &&
                EM.GetComponent<Game::Components::PlayerComponent>(Enemy)->PlayerID == 1,
            "FindNearestEnemy resolves nearest enemy over CSR spatial grid");

    struct HighFrequencyEvent { int Frame = 0; };
    for (int i = 0; i < 10000; ++i)
    {
        EM.Events().Publish(HighFrequencyEvent{i});
    }
    Require(EM.Events().Read<HighFrequencyEvent>().size() <=
                Core::ECS::EventChannel<HighFrequencyEvent>::kDefaultMaxBufferedEvents,
            "EventBus enforces bounded queue cap when events are not manually drained");
}

void TestModernRenderPipelineNodePoolingAndO1SocketLookup()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();
    Render::ModernRenderPipelineSystem RenderSys(SM, nullptr);

    for (int i = 0; i < 40; ++i)
    {
        const Core::ECS::EntityID E = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(E).Position = {
            static_cast<float>(i * 16), static_cast<float>(i * 12)
        };
        auto& R = EM.AddComponent<Game::Components::RenderComponent>(E);
        R.Visible = true;
        R.ImageID = 189;
        R.SubunitImageID = (i % 2 == 0) ? 261u : UINT32_MAX;
    }

    const size_t CmdsFrame1 = RenderSys.ExtractFromECSWorld(EM);
    Require(CmdsFrame1 == 60u, "Frame 1 extracted 40 body + 20 turret render commands");
    Require(RenderSys.GetPoolStats().NodesAllocatedLastFrame > 0u,
            "Frame 1 allocated initial SceneNode2D instances");

    for (int Frame = 2; Frame <= 10; ++Frame)
    {
        const size_t Cmds = RenderSys.ExtractFromECSWorld(EM);
        Require(Cmds == 60u, "Steady-state frame extracted identical command count");
        Require(RenderSys.GetPoolStats().NodesAllocatedLastFrame == 0u,
                "Steady-state frame performed ZERO new SceneNode2D heap allocations");
        Require(RenderSys.GetPoolStats().NodesReusedLastFrame == 100u,
                "Steady-state frame reused all 100 SceneNode2D nodes (40 roots + 40 bodies + 20 turrets) from pool");
    }

    Resource::ModernSocket2DTrack Track;
    Track.SocketAssetName = "TestSocket";
    for (int32_t Frame = 0; Frame < 10; ++Frame)
    {
        for (int32_t Dir = 0; Dir < 17; ++Dir)
        {
            Resource::ModernSocketMountEntry Entry;
            Entry.FrameSet = Frame;
            Entry.DirectionIndex = Dir;
            Entry.PixelOffsetSD[0] = static_cast<int16_t>(Frame * 10 + Dir);
            Entry.PixelOffsetSD[1] = static_cast<int16_t>(-(Frame * 5 + Dir));
            Entry.PixelOffsetHD4x[0] = static_cast<int16_t>(Entry.PixelOffsetSD[0] * 4);
            Entry.PixelOffsetHD4x[1] = static_cast<int16_t>(Entry.PixelOffsetSD[1] * 4);
            Track.FrameSocketTable.push_back(Entry);
        }
    }
    Track.RebuildLookupIndex();

    int16_t SDX = 0, SDY = 0, HDX = 0, HDY = 0;
    Require(Track.SampleOffset(7, 13, SDX, SDY, HDX, HDY), "O(1) socket track lookup found frame 7 dir 13");
    Require(SDX == 83 && SDY == -48 && HDX == 332 && HDY == -192,
            "O(1) socket track lookup returned exact SD and HD4x offsets");
}

class MockSubStageSystem final : public Core::ECS::ISystem
{
public:
    MockSubStageSystem(std::string Name, Core::ECS::EntityManager& EM, std::vector<std::string>& Log, bool SpawnDeferred)
        : mName(std::move(Name)), mEM(EM), mLog(Log), mSpawnDeferred(SpawnDeferred) {}

    std::string_view GetName() const override { return mName; }

    void Update(float) override
    {
        mLog.push_back(mName);
        if (mSpawnDeferred)
        {
            mEM.Commands().CreateEntity([](Core::ECS::EntityManager& W) {
                const Core::ECS::EntityID Id = W.CreateEntity();
                W.AddComponent<DeferredSpawnedTag>(Id).CreatedInPhase = 77;
            });
        }
        ObservedTagCount = mEM.GetComponentCount<DeferredSpawnedTag>();
    }

    size_t ObservedTagCount = 0;

private:
    std::string mName;
    Core::ECS::EntityManager& mEM;
    std::vector<std::string>& mLog;
    bool mSpawnDeferred = false;
};

void TestPhase7HierarchicalSystemGroup()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    std::vector<std::string> StageLog;
    auto LocomotionGroup = std::make_unique<Core::ECS::SystemGroup>(
        "LocomotionPipelineGroup",
        Core::ECS::SystemPhase::FixedSimulation,
        &EM);

    auto* Stage1 = LocomotionGroup->AddSubSystem<MockSubStageSystem>(
        /*FlushCommandBarrierAfter=*/true, "PathRequestStage", EM, StageLog, true);
    auto* Stage2 = LocomotionGroup->AddSubSystem<MockSubStageSystem>(
        /*FlushCommandBarrierAfter=*/false, "SteeringStage", EM, StageLog, false);
    auto* Stage3 = LocomotionGroup->AddSubSystem<MockSubStageSystem>(
        /*FlushCommandBarrierAfter=*/false, "KinematicsStage", EM, StageLog, false);

    auto* GroupPtr = LocomotionGroup.get();
    SM.RegisterSystem(std::move(LocomotionGroup));

    Require(Stage1 != nullptr && Stage2 != nullptr && Stage3 != nullptr, "Added 3 sub-systems to SystemGroup");
    SM.UpdateSimulationStep(1.0f / 24.0f);

    Require(StageLog.size() == 3 &&
                StageLog[0] == "PathRequestStage" &&
                StageLog[1] == "SteeringStage" &&
                StageLog[2] == "KinematicsStage",
            "SystemGroup executed sub-systems in deterministic order");
    Require(Stage2->ObservedTagCount == 1u,
            "SystemGroup flushed sub-stage EntityCommandBuffer barrier between Stage1 and Stage2");
    Require(GroupPtr->GetSubBarrierFlushCount() == 1u, "SystemGroup recorded sub-barrier flush");
    Require(GroupPtr->GetSubSystemProfiles().size() == 3u, "SystemGroup recorded per-sub-system profile telemetry");

    // Disable SteeringStage at runtime and verify it is skipped
    StageLog.clear();
    Require(GroupPtr->SetSubSystemEnabled("SteeringStage", false), "Disabled SteeringStage");
    SM.UpdateSimulationStep(1.0f / 24.0f);
    Require(StageLog.size() == 2 && StageLog[0] == "PathRequestStage" && StageLog[1] == "KinematicsStage",
            "Disabled sub-system was skipped cleanly by SystemGroup");
}

void TestPhase8NavigationWorldResourceMultiWorldIsolation()
{
    Core::ECS::EntityManager WorldA;
    Core::ECS::EntityManager WorldB;

    Game::Map::GameMap MapA;
    MapA.Generate(32, 32);
    Game::Map::GameMap MapB;
    MapB.Generate(64, 64);

    // Block middle of MapA while keeping MapB completely open
    for (int y = 0; y < 32; ++y)
    {
        Game::Map::Tile T = MapA.GetTile(16, y);
        T.Walkable = false;
        MapA.SetTile(16, y, T);
    }

    Game::SimulationContext::Get(WorldA).Map = &MapA;
    Game::SimulationContext::Get(WorldB).Map = &MapB;

    auto& NavA = Game::Pathfinding::NavigationWorldResource::GetOrSync(WorldA);
    auto& NavB = Game::Pathfinding::NavigationWorldResource::GetOrSync(WorldB);

    Require(NavA.BoundMap == &MapA && NavB.BoundMap == &MapB,
            "NavigationWorldResource binds each World to its own GameMap via SimulationContext");
    Require(NavA.RegionsBuilt && NavB.RegionsBuilt,
            "NavigationWorldResource built isolated RegionPathfinder instances for WorldA and WorldB");

    Game::Pathfinding::NavigationPathQuery Query;
    Query.Start = {4.0f * 32.0f + 16.0f, 16.0f * 32.0f + 16.0f};
    Query.Target = {24.0f * 32.0f + 16.0f, 16.0f * 32.0f + 16.0f};
    Query.MaximumExpandedCells = 4096;

    auto ResA = NavA.NavService.QueryPath(Query);
    auto ResB = NavB.NavService.QueryPath(Query);

    Require(ResA.Status != Game::Pathfinding::PathStatus::Exact,
            "WorldA path across full vertical wall is blocked/partial in MapA");
    Require(ResB.Status == Game::Pathfinding::PathStatus::Exact && ResB.HasUsablePath(),
            "WorldB path across open terrain succeeds with Exact path in MapB");
}

void TestPhase9SoAKinematicsStreamAndRenderServerRID()
{
    Core::ECS::EntityManager EM;
    for (int i = 0; i < 512; ++i)
    {
        const Core::ECS::EntityID Id = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Id).Position = {
            static_cast<float>(i * 2), static_cast<float>(i * 3)
        };
        EM.AddComponent<Game::Components::PathfindingComponent>(Id).CurrentVelocity = {24.0f, -12.0f};
        EM.AddComponent<Game::Components::UnitComponent>(Id).HitPoints = 40;
    }

    Core::ECS::KinematicsSoAStream SoA;
    Require(SoA.ExtractFromECS(EM) == 512u, "KinematicsSoAStream extracted 512 active units into contiguous SoA arrays");

    SoA.IntegrateBatch(1.0f, -10000.0f, -10000.0f, 10000.0f, 10000.0f, 128u);

    std::vector<float> InterpX;
    std::vector<float> InterpY;
    SoA.ComputeInterpolatedPositions(0.5f, InterpX, InterpY);
    Require(std::fabs(InterpX[0] - 12.0f) < 1e-4f && std::fabs(InterpY[0] - (-6.0f)) < 1e-4f,
            "KinematicsSoAStream computed exact half-step interpolated positions (12, -6)");

    SoA.WriteBackToECS(EM);
    const auto* T0 = EM.GetComponent<Game::Components::TransformComponent>(SoA.GetEntityIds()[0]);
    Require(T0 != nullptr && T0->HasPrevPosition &&
                std::fabs(T0->PrevPosition.X - 0.0f) < 1e-4f &&
                std::fabs(T0->Position.X - 24.0f) < 1e-4f &&
                std::fabs(T0->Position.Y - (-12.0f)) < 1e-4f,
            "KinematicsSoAStream wrote integrated positions and PrevPosition back to ECS TransformComponent");

    // Verify Godot 4 style RenderServer2D CanvasItemRID retained handle table
    Render::RenderServer2D Server;
    Render::GpuTextureHandle ValidTex;
    ValidTex.Image.id = 42;
    ValidTex.View.id = 420;

    Render::CanvasItemRID Rid1 = Server.CanvasItemCreate();
    Render::CanvasItemRID Rid2 = Server.CanvasItemCreate();
    Require(Rid1.IsValid() && Rid2.IsValid() && Server.GetActiveCanvasItemCount() == 2u,
            "Created 2 generational CanvasItemRID handles in RenderServer2D");

    Server.CanvasItemSetTransform(Rid1, 100.0f, 120.0f, 0);
    Server.CanvasItemSetSprite(Rid1, ValidTex, 32.0f, 32.0f);
    Server.CanvasItemSetTransform(Rid2, 5000.0f, 5000.0f, 0); // Outside camera frustum
    Server.CanvasItemSetSprite(Rid2, ValidTex, 32.0f, 32.0f);

    Server.BeginWorldFrame(100.0f, 120.0f, 800.0f, 600.0f, 1.0f, 64.0f);
    Require(Server.ExtractRetainedCanvasItemsToWorldQueue() == 2u,
            "Extracted 2 retained CanvasItems to WorldQueue");
    const auto Stats = Server.GetWorldQueue().ComputeStats();
    Require(Stats.RecordedCommands == 1u && Stats.CulledCommands == 1u,
            "Retained CanvasItemRID obeys RenderServer2D camera frustum culling");

    // Free Rid1 and verify generational ABA protection when slot is recycled
    Require(Server.CanvasItemFree(Rid1), "Freed Rid1");
    Require(!Server.IsCanvasItemValid(Rid1), "Stale Rid1 is rejected after free");
    Render::CanvasItemRID Rid1Recycled = Server.CanvasItemCreate();
    Require(Rid1Recycled.Index == Rid1.Index && Rid1Recycled.Generation == Rid1.Generation + 1,
            "Recycled CanvasItemRID slot incremented generation counter");
    Require(!Server.IsCanvasItemValid(Rid1) && Server.IsCanvasItemValid(Rid1Recycled),
            "Generational CanvasItemRID prevents ABA dangling references");
}

} // namespace

int main()
{
    TestUnifiedWorldResourceAndO1SlotLookup();
    TestPivotQueryAccelerationAndZeroAllocScratchBuffers();
    TestSystemManagerPhaseBarriersAndPlayerLoopSplit();
    TestCsrFinalizedSpatialIndexAndBoundedEventBus();
    TestModernRenderPipelineNodePoolingAndO1SocketLookup();
    TestPhase7HierarchicalSystemGroup();
    TestPhase8NavigationWorldResourceMultiWorldIsolation();
    TestPhase9SoAKinematicsStreamAndRenderServerRID();

    std::cout << "[Phase6To9EngineArchitectureRefactorTest] All Godot/Unity structural refactorings (Phases 6-9) passed.\n";
    return 0;
}
