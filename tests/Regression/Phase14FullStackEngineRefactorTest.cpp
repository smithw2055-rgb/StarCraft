#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#include "Core/Audio/AudioSystem.h"
#include "Core/Audio/ModernAudioEventRouter.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SmallBuffer.h"
#include "Core/ECS/SoAComponentStream.h"
#include "Core/ECS/SystemGroup.h"
#include "Core/ECS/SystemManager.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Game/Components.h"
#include "Game/Systems/DeclarativeAnimationPlayer.h"
#include "Game/Map/Map.h"
#include "Game/Networking/RollbackStateManager.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Races/RaceDef.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"
#include "Render/ModernRenderPipeline.h"
#include "Resource/AssetServer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

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

void Require(bool Condition, const char* Message)
{
    if (Condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message);
    std::exit(1);
}

void TestTask1And2SmallBufferAndGenerationalEntityHandles()
{
    // 1. Verify inline SmallBuffer trivial copyability & vector interop
    static_assert(std::is_trivially_copyable_v<Core::ECS::SmallBuffer<int, 16>>,
                  "SmallBuffer<int, 16> must be trivially copyable");
    static_assert(std::is_trivially_copyable_v<Game::Components::ProductionComponent>,
                  "ProductionComponent must be trivially copyable with SmallBuffer");
    static_assert(std::is_trivially_copyable_v<Game::Components::ShieldImpactComponent>,
                  "ShieldImpactComponent must be trivially copyable with SmallBuffer");
    static_assert(std::is_trivially_copyable_v<Game::Components::LoadedUnitsComponent>,
                  "LoadedUnitsComponent must be trivially copyable with SmallBuffer");

    Core::ECS::SmallBuffer<int, 5> ProdQueue{10, 20, 30};
    Require(ProdQueue.size() == 3u && ProdQueue[1] == 20, "SmallBuffer initializer_list & indexing works");
    ProdQueue.erase(ProdQueue.begin() + 1);
    Require(ProdQueue.size() == 2u && ProdQueue[0] == 10 && ProdQueue[1] == 30, "SmallBuffer erase shifts elements inline");

    std::vector<int> AsVec = ProdQueue;
    Require(AsVec.size() == 2u && AsVec[1] == 30, "SmallBuffer converts to std::vector transparently");

    // 2. Verify Generational EntityHandle ABA slot-recycling safety
    Core::ECS::EntityManager EM;
    const Core::ECS::EntityHandle AttackerHandle = EM.CreateEntityHandle();
    const Core::ECS::EntityHandle EnemyHandle    = EM.CreateEntityHandle();

    EM.AddComponent<Game::Components::UnitComponent>(AttackerHandle.Id).HitPoints = 40;
    EM.AddComponent<Game::Components::UnitComponent>(EnemyHandle.Id).HitPoints = 60;

    auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(AttackerHandle.Id);
    Combat.BindTargetHandle(EM, EnemyHandle.Id);
    Require(Combat.ResolveValidTargetId(EM) == EnemyHandle.Id, "TargetHandle resolves while EnemyHandle is alive");

    auto& TransportLoaded = EM.AddComponent<Game::Components::LoadedUnitsComponent>(AttackerHandle.Id);
    TransportLoaded.AddPassengerHandle(EnemyHandle);
    Require(TransportLoaded.PassengerIDs.size() == 1u, "LoadedUnitsComponent recorded passenger handle");

    // Destroy EnemyHandle (Id=2, Gen=1) and immediately spawn a new entity in the recycled slot (Id=2, Gen=2)
    const Core::ECS::EntityID OldSlotId = EnemyHandle.Id;
    Require(EM.DestroyEntity(EnemyHandle), "Destroyed EnemyHandle");
    EM.CreateEntityWithID(OldSlotId, EM.GetGeneration(OldSlotId));
    const Core::ECS::EntityHandle RecycledFriendlyHandle = EM.GetEntityHandle(OldSlotId);
    Require(RecycledFriendlyHandle.Id == OldSlotId && RecycledFriendlyHandle.Generation > EnemyHandle.Generation,
            "Recycled entity reuses slot ID with incremented generation");
    EM.AddComponent<Game::Components::UnitComponent>(RecycledFriendlyHandle.Id).HitPoints = 100;

    // Stale EnemyHandle must return nullptr on TryGetComponent and auto-clear on CombatComponent!
    Require(EM.TryGetComponent<Game::Components::UnitComponent>(EnemyHandle) == nullptr,
            "TryGetComponent rejects stale EntityHandle after ABA slot reuse");
    Require(EM.TryGetComponent<Game::Components::UnitComponent>(RecycledFriendlyHandle) != nullptr,
            "TryGetComponent resolves new EntityHandle on recycled slot");
    Require(Combat.ResolveValidTargetId(EM) == -1,
            "CombatComponent::ResolveValidTargetId detects ABA slot recycling and clears stale target");

    TransportLoaded.PruneInvalidPassengers(EM);
    Require(TransportLoaded.PassengerIDs.empty(),
            "LoadedUnitsComponent::PruneInvalidPassengers removes recycled ABA entity handle");
}

void TestTask3FullComponentWorldSnapshotAndRollback()
{
    Core::ECS::EntityManager EM;

    auto& Econ = EM.Context().EmplaceResource<Game::Systems::PlayerResourceStore>();
    Econ.Players[0].Minerals = 500;
    Econ.Players[0].VespeneGas = 250;

    auto& Upg = EM.Context().EmplaceResource<Core::ECS::WorldUpgradeResource>();
    Upg.PlayerUpgrades[0].Levels[0] = 2;

    const Core::ECS::EntityID Worker = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Worker).Position = {128.0f, 256.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Worker).PlayerID = 0;
    EM.AddComponent<Game::Components::UnitComponent>(Worker).HitPoints = 60;

    auto& Cmd = EM.AddComponent<Game::Components::CommandComponent>(Worker);
    Game::Components::Order GatherOrder;
    GatherOrder.Type = Game::Components::OrderType::Gather;
    GatherOrder.TargetPosition = {64.0f, 64.0f};
    GatherOrder.TargetEntityID = 99;
    Cmd.QueueOrder(GatherOrder);

    auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(Worker);
    Path.HasFixedPosition = true;
    Path.FixedPosition = Core::Math::FixedVec2(128.0f, 256.0f);
    Path.FixedVelocity = Core::Math::FixedVec2(4.0f, -2.0f);
    Path.Path.push_back({100.0f, 200.0f});

    auto& Harv = EM.AddComponent<Game::Components::HarvestComponent>(Worker);
    Harv.State = Game::Components::HarvestState::MovingToDepot;
    Harv.CarryingType = Game::Components::ResourceType::Minerals;
    Harv.CarryingAmount = 8;

    auto& Bldg = EM.AddComponent<Game::Components::BuildingComponent>(Worker);
    Bldg.BuildingDefID = 12;
    Bldg.BuildProgress = 34.5f;
    Bldg.UnderConstruction = true;

    auto& Prod = EM.AddComponent<Game::Components::ProductionComponent>(Worker);
    Prod.CurrentTrainingUnitDefID = 3;
    Prod.TrainingProgress = 12.0f;
    Prod.Queue.push_back(3);
    Prod.Queue.push_back(5);

    auto& Status = EM.AddComponent<Game::Components::StatusEffectComponent>(Worker);
    Status.AddEffect(Game::Components::StatusEffect::Stim, 8.5f, Worker);

    auto& Burrow = EM.AddComponent<Game::Components::BurrowComponent>(Worker);
    Burrow.CanBurrow = true;
    Burrow.IsBurrowed = true;
    Burrow.Phase = Game::Components::BurrowPhase::Burrowed;

    auto& Siege = EM.AddComponent<Game::Components::SiegeTransitionComponent>(Worker);
    Siege.Active = true;
    Siege.TargetSieged = true;
    Siege.Stage = 1;
    Siege.Elapsed = 0.75f;

    // Capture full deterministic snapshot at frame 42
    const auto Snap42 = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(EM, 42);
    Require(Snap42.StateHashFNV1a != 0, "Captured non-zero FNV-1a hash for full-component world");

    // Mutate only queued Order target position (same Orders.size()) to verify deep order hashing
    Cmd.Orders[0].TargetPosition = {999.0f, 999.0f};
    Require(Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(EM, 42) != Snap42.StateHashFNV1a,
            "Mutating queued Order element inside CommandComponent changes FNV-1a world hash");
    Cmd.Orders[0].TargetPosition = {64.0f, 64.0f};

    // Mutate deep gameplay components (Harvest, Production Queue, Building Progress, Economy, FixedPosition, SiegeTransition)
    Econ.Players[0].Minerals = 50;
    Harv.CarryingAmount = 0;
    Harv.State = Game::Components::HarvestState::Idle;
    Prod.Queue.clear();
    Bldg.BuildProgress = 99.0f;
    Path.FixedPosition = Core::Math::FixedVec2(512.0f, 512.0f);
    Status.RemoveEffect(Game::Components::StatusEffect::Stim);
    Burrow.IsBurrowed = false;
    Siege.Active = false;

    const uint32_t MutatedHash = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(EM, 42);
    Require(MutatedHash != Snap42.StateHashFNV1a,
            "Mutating Harvest/Production/Building/Pathfinding/Status/Economy changes world hash");

    // Restore snapshot and verify 100% of components and hash are restored bit-for-bit
    Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(EM, Snap42);
    const uint32_t RestoredHash = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(EM, 42);
    Require(RestoredHash == Snap42.StateHashFNV1a,
            "Restoring full-component snapshot reproduces identical FNV-1a hash");

    const auto* RestoredHarv = EM.GetComponent<Game::Components::HarvestComponent>(Worker);
    const auto* RestoredProd = EM.GetComponent<Game::Components::ProductionComponent>(Worker);
    const auto* RestoredBldg = EM.GetComponent<Game::Components::BuildingComponent>(Worker);
    const auto* RestoredPath = EM.GetComponent<Game::Components::PathfindingComponent>(Worker);
    const auto* RestoredStatus = EM.GetComponent<Game::Components::StatusEffectComponent>(Worker);
    const auto* RestoredBurrow = EM.GetComponent<Game::Components::BurrowComponent>(Worker);
    const auto* RestoredEcon = EM.Context().GetResource<Game::Systems::PlayerResourceStore>();

    Require(RestoredHarv && RestoredHarv->CarryingAmount == 8 &&
            RestoredHarv->State == Game::Components::HarvestState::MovingToDepot,
            "HarvestComponent restored accurately");
    Require(RestoredProd && RestoredProd->Queue.size() == 2u && RestoredProd->Queue[1] == 5,
            "ProductionComponent SmallBuffer queue restored accurately");
    Require(RestoredBldg && std::fabs(RestoredBldg->BuildProgress - 34.5f) < 1e-4f && RestoredBldg->UnderConstruction,
            "BuildingComponent restored accurately");
    Require(RestoredPath && RestoredPath->HasFixedPosition &&
            RestoredPath->FixedPosition == Core::Math::FixedVec2(128.0f, 256.0f),
            "PathfindingComponent FixedPosition restored accurately");
    Require(RestoredStatus && RestoredStatus->HasEffect(Game::Components::StatusEffect::Stim),
            "StatusEffectComponent restored accurately");
    Require(RestoredBurrow && RestoredBurrow->IsBurrowed,
            "BurrowComponent restored accurately");
    Require(RestoredEcon && RestoredEcon->Players[0].Minerals == 500,
            "PlayerResourceStore restored accurately");
}

void TestTask4LiveGameSessionSystemGroupAndPipelineWiring()
{
    Game::Client::GameLoop::GameSession Session;
    Core::Audio::AudioSystem DummyAudio;

    Session.ConfigureSyntheticSkirmishMap();
    Session.MarkMapLoaded();
    struct DummyTileset { bool Loaded = false; std::vector<uint16_t> Cv5; std::vector<uint16_t> Vf4; };
    Session.BuildEcsMapFromLoadedMap<DummyTileset>(nullptr);
    Game::Client::GameLoop::GameSessionBootstrap::RegisterGameplaySystems(Session, DummyAudio);
    Session.MarkEcsInitialized();
    Session.RollbackRecordingEnabled = true;

    auto& SysMgr = Session.Systems();
    // Verify top-level SystemGroups are registered
    Require(SysMgr.GetSystemCount() == 5u,
            "GameSessionBootstrap registers 5 hierarchical SystemGroups across FixedSimulation, Presentation, and RenderExtract");

    // Verify recursive FindSystem<T> finds nested subsystems inside SystemGroups
    Require(SysMgr.FindSystem<Game::Pathfinding::WorldSpatialIndexSystem>() != nullptr,
            "Recursive FindSystem locates WorldSpatialIndexSystem inside SpatialAndCommandGroup");
    Require(SysMgr.FindSystem<Game::Systems::MovementSystem>() != nullptr,
            "Recursive FindSystem locates MovementSystem inside LocomotionAndEconomyGroup");
    Require(SysMgr.FindSystem<Game::Systems::CombatSystem>() != nullptr,
            "Recursive FindSystem locates CombatSystem inside CombatAndRaceGroup");
    Require(SysMgr.FindSystem<Core::Audio::ModernAudioEventRouterSystem>() != nullptr,
            "Recursive FindSystem locates ModernAudioEventRouterSystem inside PresentationAndAudioGroup");
    Require(SysMgr.FindSystem<Render::ModernRenderPipelineSystem>() != nullptr,
            "Recursive FindSystem locates ModernRenderPipelineSystem inside RenderExtractionGroup");

    // Spawn a test unit in the live GameSession
    const Core::ECS::EntityID Marine = Session.Entities().CreateEntity();
    auto& Trans = Session.Entities().AddComponent<Game::Components::TransformComponent>(Marine);
    Trans.Position = {256.0f, 256.0f};
    Session.Entities().AddComponent<Game::Components::PlayerComponent>(Marine).PlayerID = 0;
    auto& Unit = Session.Entities().AddComponent<Game::Components::UnitComponent>(Marine);
    Unit.ChkUnitID = 0;
    Unit.HitPoints = 40;
    Unit.Speed = 4.0f;
    auto& RenderComp = Session.Entities().AddComponent<Game::Components::RenderComponent>(Marine);
    RenderComp.ImageID = 0;
    RenderComp.Visible = true;

    // Step simulation & presentation phases
    Session.SaveTransformTickState();
    Session.UpdateSimulationStep(1.0f / 24.0f);
    Session.UpdatePresentationFrame(1.0f / 24.0f);

    Require(Session.KinematicsStream.Size() == 1u,
            "GameSession::UpdateSimulationStep automatically extracts active units into KinematicsSoAStream");
    Require(Session.RollbackMgr.GetTelemetry().TotalSnapshotsRecorded == 1u,
            "GameSession::UpdateSimulationStep records frame snapshot in RollbackStateManager when enabled");
    Require(Session.NetSync.ComputeStateHash() != 0u,
            "GameSession::NetSync state hash callback is wired to WorldSnapshotSerializer::ComputeWorldHashFNV1a");

    auto* RenderPipe = SysMgr.FindSystem<Render::ModernRenderPipelineSystem>();
    Require(RenderPipe && RenderPipe->GetSceneRootCount() == 1u,
            "GameSession::UpdatePresentationFrame runs ModernRenderPipelineSystem in RenderExtract phase");

    // Verify dirty-tracked SyncCreepPresentation
    Game::Map::Tile CreepTile = Session.GetEcsMap().GetTile(10, 10);
    CreepTile.HasCreep = true;
    Session.MutableEcsMap().SetTile(10, 10, CreepTile);
    Require(Session.SyncCreepPresentation() == 1u,
            "SyncCreepPresentation reports 1 dirty tile when creep expands");
    Require(Session.SyncCreepPresentation() == 0u,
            "SyncCreepPresentation reports 0 changed tiles on subsequent unchanged frame");
}

void TestTask5UnifiedAssetServerAndBudgetedUploads()
{
    auto& Server = Resource::AssetServer::Instance();
    Server.ResetTelemetry();

    struct MockShaderAsset
    {
        std::string Name;
        uint32_t ProgramId = 0;
    };

    auto H1 = Server.RegisterOrAcquire<MockShaderAsset>("shaders/hd_pbr_sprite.glsl", {"hd_pbr_sprite", 101u});
    auto H2 = Server.RegisterOrAcquire<MockShaderAsset>("shaders/hd_pbr_sprite.glsl", {"hd_pbr_sprite", 101u});
    Require(H1 == H2 && Server.GetTelemetry().CacheHits == 1u,
            "AssetServer deduplicates identical URI registrations and increments reference count");

    const auto* Resolved = Server.Resolve(H1);
    Require(Resolved && Resolved->ProgramId == 101u, "AssetServer resolves valid AssetHandle");

    // Release twice to reach RefCount == 0 and recycle slot
    Require(Server.Release(H1), "First release decrements RefCount");
    Require(Server.Resolve(H1) != nullptr, "Asset remains alive while RefCount > 0");
    Require(Server.Release(H2), "Second release unloads asset and recycles slot");
    Require(Server.Resolve(H1) == nullptr, "Stale AssetHandle returns nullptr after unload");

    // Re-register into recycled slot -> generation must increment!
    auto H3 = Server.RegisterOrAcquire<MockShaderAsset>("shaders/sdf_selection.glsl", {"sdf_selection", 202u});
    Require(H3.Index == H1.Index && H3.Generation > H1.Generation,
            "Recycled AssetServer slot increments generation to prevent ABA handle bugs");
    Require(Server.Resolve(H1) == nullptr && Server.Resolve(H3)->ProgramId == 202u,
            "Old handle rejected while new handle resolves cleanly");

    // Test parallel decode + budgeted main-thread GPU upload flushing
    int UploadedCount = 0;
    std::vector<uint32_t> TextureSizes = {4 * 1024 * 1024, 4 * 1024 * 1024, 4 * 1024 * 1024};
    Server.DispatchAsyncBatchDecode(TextureSizes, [&](uint32_t Bytes) {
        Resource::PendingGpuUploadTask Task;
        Task.LogicalUri = "unit/hd_sheet.dds";
        Task.ByteSize = Bytes;
        Task.UploadCallback = [&UploadedCount]() { ++UploadedCount; };
        return Task;
    });

    Require(Server.GetPendingGpuUploadCount() == 3u, "Queued 3 decoded textures for main-thread GPU upload");
    // Flush with 8 MB budget -> should upload exactly 2 of the 4 MB textures!
    const size_t FlushedFrame1 = Server.FlushPendingGpuUploads(8 * 1024 * 1024);
    Require(FlushedFrame1 == 2u && UploadedCount == 2 && Server.GetPendingGpuUploadCount() == 1u,
            "FlushPendingGpuUploads respects 8MB per-frame upload budget");

    const size_t FlushedFrame2 = Server.FlushPendingGpuUploads(8 * 1024 * 1024);
    Require(FlushedFrame2 == 1u && UploadedCount == 3 && Server.GetPendingGpuUploadCount() == 0u,
            "Remaining upload completes on next frame");
}

} // namespace

int main()
{
    TestTask1And2SmallBufferAndGenerationalEntityHandles();
    TestTask3FullComponentWorldSnapshotAndRollback();
    TestTask4LiveGameSessionSystemGroupAndPipelineWiring();
    TestTask5UnifiedAssetServerAndBudgetedUploads();

    std::cout << "[Phase14FullStackEngineRefactorTest] All Tasks 1-5 (SmallBuffer, EntityHandle ABA safety, 17-Component Snapshot Rollback, Live GameSession SystemGroups, and AssetServer) passed.\n";
    return 0;
}
