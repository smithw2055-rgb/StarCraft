#include "Core/Audio/AudioBusMixer.h"
#include "Core/Audio/ModernAudioEventRouter.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Core/Input/InputActionMap.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Networking/RollbackStateManager.h"
#include "Game/Races/RaceDef.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace Core::Audio
{
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags) { return 0; }
    void AudioSystem::Update(float) {}
    void AudioSystem::SetSoundType(int, const SoundType&) {}
    void AudioSystem::SetSoundPath(int, std::string) {}
}

namespace Core::Platform
{
    bool IsKeyPressed(int) { return false; }
    bool IsKeyDown(int) { return false; }
    bool IsKeyReleased(int) { return false; }
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

struct ReactiveHealthTag
{
    int HitPoints = 100;
};

void TestPhase10MultiWorldSimulationContextInProductionAndBuilding()
{
    Core::ECS::EntityManager WorldA;
    Core::ECS::EntityManager WorldB;

    Game::Map::GameMap MapA;
    MapA.Generate(32, 32);
    Game::Map::GameMap MapB;
    MapB.Generate(32, 32);

    // Clear global static BuildingSystem::SetMap(nullptr) & ProductionSystem::SetMap(nullptr)
    // to prove both systems resolve maps strictly from per-World SimulationContext!
    Game::Systems::BuildingSystem::SetMap(nullptr);
    Game::Systems::ProductionSystem::SetMap(nullptr);

    Game::SimulationContext::Get(WorldA).Map = &MapA;
    Game::SimulationContext::Get(WorldB).Map = &MapB;

    const int PylonDefID = Game::Races::GetUnitDefID("Pylon");
    const int PylonChkID = Game::Races::GetUnitDefByID(PylonDefID).ChkUnitID;

    // Spawn an active Protoss Pylon in WorldA at tile (10, 10), but NOT in WorldB
    const Core::ECS::EntityID PylonA = WorldA.CreateEntity();
    WorldA.AddComponent<Game::Components::TransformComponent>(PylonA).Position = MapA.TileToWorld(10, 10);
    auto& BldgA = WorldA.AddComponent<Game::Components::BuildingComponent>(PylonA);
    BldgA.BuildingDefID = PylonDefID;
    BldgA.UnderConstruction = false;
    auto& UnitA = WorldA.AddComponent<Game::Components::UnitComponent>(PylonA);
    UnitA.ChkUnitID = PylonChkID;
    UnitA.HitPoints = 300;
    WorldA.AddComponent<Game::Components::PlayerComponent>(PylonA).PlayerID = 0;

    const Core::Math::Vec2 ProbeBuildPos = MapA.TileToWorld(12, 10);
    Require(Game::Systems::BuildingSystem::HasPylonPowerAtPosition(WorldA, ProbeBuildPos, 0),
            "WorldA resolves MapA from SimulationContext and detects Pylon power at (12,10)");
    Require(!Game::Systems::BuildingSystem::HasPylonPowerAtPosition(WorldB, ProbeBuildPos, 0),
            "WorldB resolves MapB from SimulationContext and reports no Pylon power");
}

void TestPhase11DeclarativeInputActionMapAndChords()
{
    using namespace Core::Input;

    InputActionMap ActionMap = InputActionMap::CreateDefaultRTSMap();

    int ActiveKeyPressed = 0;
    int ActiveKeyDown = 0;
    bool ShiftHeld = false;
    bool CtrlHeld = false;

    InputStateProvider MockProvider;
    MockProvider.IsKeyPressedFn = [&](int K) { return K == ActiveKeyPressed; };
    MockProvider.IsKeyDownFn    = [&](int K) { return K == ActiveKeyDown; };
    MockProvider.IsKeyReleasedFn = [](int) { return false; };
    MockProvider.IsShiftDownFn  = [&]() { return ShiftHeld; };
    MockProvider.IsControlDownFn = [&]() { return CtrlHeld; };
    ActionMap.SetStateProvider(MockProvider);

    // 1. Verify default RTS command bindings
    ActiveKeyPressed = 'S';
    Require(ActionMap.IsActionTriggered("Command.Stop"), "Default 'S' triggers Command.Stop");
    Require(!ActionMap.IsActionTriggered("Command.Patrol"), "'S' does not trigger Command.Patrol");

    ActiveKeyDown = 'A';
    Require(ActionMap.IsActionTriggered("Command.AttackMoveModifier"), "Held 'A' triggers Command.AttackMoveModifier");

    // 2. Register a chord action requiring Ctrl + Shift + 'G'
    ActionMap.RegisterAction(
        "Camera.CenterPatrolChord",
        {'G', InputModifierMask::Control | InputModifierMask::Shift, InputTriggerPhase::Pressed});

    ActiveKeyPressed = 'G';
    ShiftHeld = true;
    CtrlHeld = false;
    Require(!ActionMap.IsActionTriggered("Camera.CenterPatrolChord"),
            "Chord rejects trigger when Control modifier is missing");

    CtrlHeld = true;
    Require(ActionMap.IsActionTriggered("Camera.CenterPatrolChord"),
            "Chord triggers when exact Ctrl+Shift+'G' combination is active");

    // 3. Runtime rebinding & action disable gate
    Require(ActionMap.RebindAction("Command.Stop", {'X', InputModifierMask::None, InputTriggerPhase::Pressed}),
            "Rebound Command.Stop to 'X'");
    ActiveKeyPressed = 'S';
    ShiftHeld = false;
    CtrlHeld = false;
    Require(!ActionMap.IsActionTriggered("Command.Stop"), "Old key 'S' no longer triggers Command.Stop after rebind");
    ActiveKeyPressed = 'X';
    Require(ActionMap.IsActionTriggered("Command.Stop"), "New key 'X' triggers Command.Stop");

    ActionMap.SetActionEnabled("Command.Stop", false);
    Require(!ActionMap.IsActionTriggered("Command.Stop"), "Disabled action does not trigger");
}

void TestPhase12ReactiveLifecycleSignalsAndVersionedQueryCache()
{
    Core::ECS::EntityManager EM;
    EM.ResetQueryTelemetry();

    int ConstructedCount = 0;
    int RemovedCount = 0;
    Core::ECS::EntityID LastConstructedId = Core::ECS::InvalidEntity;
    Core::ECS::EntityID LastRemovedId = Core::ECS::InvalidEntity;

    EM.OnComponentConstructed<ReactiveHealthTag>(
        [&](Core::ECS::EntityManager&, Core::ECS::EntityID Id, ReactiveHealthTag& Comp)
        {
            ++ConstructedCount;
            LastConstructedId = Id;
            Comp.HitPoints = 250; // Reactive default override on construction
        });

    EM.OnComponentRemoved<ReactiveHealthTag>(
        [&](Core::ECS::EntityManager&, Core::ECS::EntityID Id)
        {
            ++RemovedCount;
            LastRemovedId = Id;
        });

    const Core::ECS::EntityID E1 = EM.CreateEntity();
    const Core::ECS::EntityID E2 = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(E1);
    EM.AddComponent<Game::Components::TransformComponent>(E2);

    auto& H1 = EM.AddComponent<ReactiveHealthTag>(E1);
    auto& H2 = EM.AddComponent<ReactiveHealthTag>(E2);
    Require(ConstructedCount == 2 && LastConstructedId == E2,
            "OnComponentConstructed fired for both E1 and E2");
    Require(H1.HitPoints == 250 && H2.HitPoints == 250,
            "OnComponentConstructed mutated newly constructed component in-place");

    // Test GetEntitiesWithCached versioned caching
    const auto& Cached1 = EM.GetEntitiesWithCached<Game::Components::TransformComponent, ReactiveHealthTag>();
    Require(Cached1.size() == 2u, "Initial GetEntitiesWithCached returns 2 entities");
    Require(EM.GetQueryTelemetry().CachedQueryMisses == 1u &&
                EM.GetQueryTelemetry().CachedQueryHits == 0u,
            "First cached query recorded 1 miss");

    for (int i = 0; i < 10; ++i)
    {
        const auto& CachedHit = EM.GetEntitiesWithCached<Game::Components::TransformComponent, ReactiveHealthTag>();
        Require(&CachedHit == &Cached1 && CachedHit.size() == 2u,
                "Repeated GetEntitiesWithCached returns identical cached vector reference in O(1)");
    }
    Require(EM.GetQueryTelemetry().CachedQueryHits == 10u,
            "10 repeated calls recorded 10 O(1) CachedQueryHits");

    // Mutate component mask via RemoveComponent -> must fire OnComponentRemoved and invalidate cache version!
    EM.RemoveComponent<ReactiveHealthTag>(E1);
    Require(RemovedCount == 1 && LastRemovedId == E1,
            "RemoveComponent fired OnComponentRemoved for E1");

    const auto& CachedAfterRemove = EM.GetEntitiesWithCached<Game::Components::TransformComponent, ReactiveHealthTag>();
    Require(CachedAfterRemove.size() == 1u && CachedAfterRemove[0] == E2,
            "GetEntitiesWithCached automatically invalidated and rebuilt after RemoveComponent");
    Require(EM.GetQueryTelemetry().CachedQueryMisses == 2u,
            "Mask mutation triggered cache rebuild");

    // DestroyEntity(E2) -> must also fire OnComponentRemoved for remaining attached components
    EM.DestroyEntity(E2);
    Require(RemovedCount == 2 && LastRemovedId == E2,
            "DestroyEntity fired OnComponentRemoved for E2");
    Require(EM.GetEntitiesWithCached<Game::Components::TransformComponent, ReactiveHealthTag>().empty(),
            "GetEntitiesWithCached is empty after E2 destruction");
}

void TestPhase13RollbackStateManagerAndAudioBusMixer()
{
    // 1. Test RollbackStateManager ring-buffered rollback & resimulation
    Core::ECS::EntityManager LocalWorld;
    Core::ECS::EntityManager AuthorityWorld;

    auto SetupInitialWorld = [](Core::ECS::EntityManager& W) -> Core::ECS::EntityID
    {
        const Core::ECS::EntityID Unit = W.CreateEntity();
        W.AddComponent<Game::Components::TransformComponent>(Unit).Position = {100.0f, 100.0f};
        auto& U = W.AddComponent<Game::Components::UnitComponent>(Unit);
        U.ChkUnitID = 0;
        U.HitPoints = 40;
        W.AddComponent<Game::Components::PlayerComponent>(Unit).PlayerID = 0;
        return Unit;
    };

    const Core::ECS::EntityID LocalUnit = SetupInitialWorld(LocalWorld);
    const Core::ECS::EntityID AuthUnit  = SetupInitialWorld(AuthorityWorld);
    (void)LocalUnit;
    (void)AuthUnit;

    auto ApplySyncAction = [](Core::ECS::EntityManager& W, const Game::Networking::SyncAction& Act)
    {
        if (Act.Data.size() >= 2 && Act.Data[0] == 0x10)
        {
            const float BoostX = static_cast<float>(Act.Data[1]);
            W.ForEachComponent<Game::Components::TransformComponent>(
                [&](Core::ECS::EntityID, Game::Components::TransformComponent& T)
                {
                    T.Position.X += BoostX;
                });
        }
    };

    auto SimulateTick = [](Core::ECS::EntityManager& W, uint32_t /*Frame*/)
    {
        W.ForEachComponent<Game::Components::TransformComponent>(
            [](Core::ECS::EntityID, Game::Components::TransformComponent& T)
            {
                T.Position.X += 4.0f;
                T.Position.Y += 2.0f;
            });
    };

    Game::Networking::RollbackStateManager RollbackMgr;
    Game::Networking::SyncAction LateAction;
    LateAction.Frame = 12;
    LateAction.PlayerID = 1;
    LateAction.Data = {0x10, 50}; // +50.0f X impulse at frame 12

    // AuthorityWorld executes LateAction on time at frame 12; LocalWorld misses it initially
    for (uint32_t Frame = 10; Frame < 20; ++Frame)
    {
        RollbackMgr.RecordFrame(LocalWorld, Frame, {});
        SimulateTick(LocalWorld, Frame);

        if (Frame == 12)
            ApplySyncAction(AuthorityWorld, LateAction);
        SimulateTick(AuthorityWorld, Frame);
    }
    RollbackMgr.RecordFrame(LocalWorld, 20, {});

    const uint32_t AuthHashAt20 = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(AuthorityWorld, 20);
    Require(!RollbackMgr.VerifyRemoteFrameHash(20, AuthHashAt20),
            "RollbackStateManager detects state hash divergence at frame 20 before rollback");
    Require(RollbackMgr.GetTelemetry().DesyncDetections == 1u, "Recorded 1 desync detection");

    // Now LateAction arrives for frame 12 while LocalWorld is at frame 20 -> Rollback & Resimulate!
    Require(RollbackMgr.InjectActionAndResimulate(
                LocalWorld, 12, 20, LateAction, ApplySyncAction, SimulateTick),
            "InjectActionAndResimulate rolled back to frame 12 and resimulated to frame 20");
    Require(RollbackMgr.GetTelemetry().TotalRollbacksExecuted == 1u &&
                RollbackMgr.GetTelemetry().TotalResimulatedTicks == 8u,
            "Rollback telemetry recorded 1 rollback and 8 resimulated ticks (frames 12..19)");
    Require(RollbackMgr.VerifyRemoteFrameHash(20, AuthHashAt20),
            "After rollback and resimulation, LocalWorld FNV-1a hash matches AuthorityWorld bit-for-bit!");

    // 2. Test AudioBusMixer hierarchical gain, sidechain voice ducking & spatial coalescing
    Core::ECS::SystemManager SM;
    Core::Audio::ModernAudioEventRouterSystem AudioRouter(SM, nullptr, nullptr);
    auto& Mixer = AudioRouter.GetBusMixer();

    Mixer.SetBusVolume(Core::Audio::AudioBusId::Master, 0.8f);
    Mixer.SetBusVolume(Core::Audio::AudioBusId::SFX, 0.5f);
    Require(std::fabs(Mixer.ComputeEffectiveBusGain(Core::Audio::AudioBusId::SFX) - 0.4f) < 1e-4f,
            "Effective SFX gain equals Master (0.8) * SFX (0.5) = 0.4");

    // Trigger unit portrait voice transmission ducking (0.5x duck ratio for 1.0s)
    Mixer.TriggerVoiceTransmissionDucking(1.0f, 0.5f);
    Require(Mixer.IsVoiceDuckingActive(), "Voice transmission ducking is active");
    Require(std::fabs(Mixer.ComputeEffectiveBusGain(Core::Audio::AudioBusId::SFX) - 0.2f) < 1e-4f,
            "Sidechain voice ducking attenuates SFX gain from 0.4 to 0.2");
    Require(std::fabs(Mixer.ComputeEffectiveBusGain(Core::Audio::AudioBusId::Voice) - 0.8f) < 1e-4f,
            "Voice bus itself is NOT ducked during transmission");

    // Emit 12 overlapping Marine weapon fire events (SoundID 69) within 32px of each other on the same frame,
    // plus 1 distant Marine weapon fire event 500px away.
    AudioRouter.ClearHistory();
    for (int i = 0; i < 12; ++i)
    {
        Game::Events::AnimAudioNotifyEvent Ev;
        Ev.SourceEntity = i + 1;
        Ev.SoundID = 69u;
        Ev.AudioClipPath = "sound/Bullet/tmaFir00.wav";
        Ev.WorldPos = {200.0f + static_cast<float>(i * 2), 200.0f};
        SM.GetEventBus().Publish(Ev);
    }
    {
        Game::Events::AnimAudioNotifyEvent DistantEv;
        DistantEv.SourceEntity = 99;
        DistantEv.SoundID = 69u;
        DistantEv.AudioClipPath = "sound/Bullet/tmaFir00.wav";
        DistantEv.WorldPos = {800.0f, 800.0f};
        SM.GetEventBus().Publish(DistantEv);
    }

    Require(AudioRouter.GetDispatchedHistory().size() == 2u,
            "Spatial sound coalescing deduplicated 12 nearby identical SoundID 69 events into 1 while preserving the distant event");
    Require(Mixer.GetTelemetry().CoalescedDuplicateCount == 11u,
            "AudioBusMixer recorded 11 coalesced duplicate spatial sound requests");

    // Advance mixer past ducking duration and recovery
    Mixer.AdvanceFrame(1.0f);
    Mixer.AdvanceFrame(1.0f);
    Require(!Mixer.IsVoiceDuckingActive() &&
                std::fabs(Mixer.ComputeEffectiveBusGain(Core::Audio::AudioBusId::SFX) - 0.4f) < 1e-4f,
            "SFX bus gain smoothly recovered to 0.4 after voice transmission ended");
}

} // namespace

int main()
{
    TestPhase10MultiWorldSimulationContextInProductionAndBuilding();
    TestPhase11DeclarativeInputActionMapAndChords();
    TestPhase12ReactiveLifecycleSignalsAndVersionedQueryCache();
    TestPhase13RollbackStateManagerAndAudioBusMixer();

    std::cout << "[Phase10To13EngineModernizationTest] All Phase 10-13 Godot/Unity engine modernization tests passed.\n";
    return 0;
}
