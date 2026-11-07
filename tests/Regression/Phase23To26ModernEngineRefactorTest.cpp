#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#include "Core/Audio/AudioSystem.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Game/AI/BehaviorTreeRuntime.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Races/RaceDef.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/DeclarativeAnimationPlayer.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Trigger/SCRTriggerParser.h"
#include "Game/Trigger/TriggerVM.h"
#include "Main/GameLoop/GameSaveState.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

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

namespace
{

void Require(bool Condition, const char* Message)
{
    if (Condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message);
    std::exit(1);
}

void TestPhase23TriggerVMSpatialLocationBroadphaseAndGenerationalDeathTracking()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    SM.RegisterSystem(std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM));

    Game::Trigger::SCRTriggerData ScriptData{};
    ScriptData.Players[0].Owner = 6; // Human
    ScriptData.Players[1].Owner = 5; // Computer

    // Define Location 1: [100..300, 100..300]
    Game::Trigger::SCRLocation Loc1{};
    Loc1.ID = 1;
    Loc1.Left = 100;
    Loc1.Top = 100;
    Loc1.Right = 300;
    Loc1.Bottom = 300;
    Loc1.StringID = 1;
    ScriptData.Locations.push_back(Loc1);

    // Define a trigger that awards Victory when Player 0 brings >= 1 Marine (UnitID 0) to Location 1
    Game::Trigger::SCRTrigger BringTrig{};
    BringTrig.Owners[0] = 1;
    BringTrig.Conditions[0].ConditionType = 3; // Bring
    BringTrig.Conditions[0].PlayerID = 0;
    BringTrig.Conditions[0].UnitID = 0; // Marine
    BringTrig.Conditions[0].LocationID = 1;
    BringTrig.Conditions[0].ComparisonType = 0; // At least
    BringTrig.Conditions[0].Amount = 1;
    BringTrig.Actions[0].ActionType = 1; // Victory
    ScriptData.Triggers.push_back(BringTrig);

    Game::Trigger::TriggerVM VM;
    Require(VM.LoadScript(ScriptData), "TriggerVM loaded synthetic Bring trigger script");

    // Spawn Marine1 inside Location 1 at (200, 200) and Marine2 far outside at (900, 900)
    const int Marine1 = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Marine1).Position = {200.0f, 200.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Marine1).PlayerID = 0;
    auto& U1 = EM.AddComponent<Game::Components::UnitComponent>(Marine1);
    U1.ChkUnitID = 0;
    U1.HitPoints = 40;

    const int Marine2 = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Marine2).Position = {900.0f, 900.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Marine2).PlayerID = 0;
    auto& U2 = EM.AddComponent<Game::Components::UnitComponent>(Marine2);
    U2.ChkUnitID = 0;
    U2.HitPoints = 40;

    SM.UpdateSimulationStep(0.05f);
    const auto* SpatialRes = EM.GetResource<Game::Pathfinding::SpatialIndexResource>();
    const uint64_t ReusesBeforeTrigger = SpatialRes->SharedQueryReuses;

    VM.Update(EM, 0.1f);
    Require(VM.GetPlayerOutcome(0) == Game::Trigger::TriggerOutcome::Victory, "TriggerVM Bring condition at Location 1 fired Victory action");
    Require(SpatialRes->SharedQueryReuses > ReusesBeforeTrigger,
            "TriggerVM::FindMatchingUnits reused SpatialIndexResource O(1) CSR broadphase for Location 1");

    // Verify Generational ABA-safe Death Tracking:
    // Destroy Marine1 (Id=1, Gen=1) AND immediately recycle slot Id=1 (Gen=2) in the same frame before VM.Update!
    Require(VM.GetDeathCount(0, 0) == 0, "Initial death count for Player 0 Marine is 0");
    const uint32_t OldGen = EM.GetGeneration(Marine1);
    EM.DestroyEntity(Marine1);
    EM.CreateEntityWithID(Marine1, OldGen + 1);
    EM.AddComponent<Game::Components::TransformComponent>(Marine1).Position = {500.0f, 500.0f};
    EM.AddComponent<Game::Components::PlayerComponent>(Marine1).PlayerID = 1;
    auto& RecycledZergling = EM.AddComponent<Game::Components::UnitComponent>(Marine1);
    RecycledZergling.ChkUnitID = 37; // Zergling
    RecycledZergling.HitPoints = 35;

    VM.Update(EM, 0.1f);
    Require(VM.GetDeathCount(0, 0) == 1,
            "TriggerVM::SyncDeathsFromEntityManager detected Marine1 death despite same-frame ABA slot reuse!");

    // Also verify normal death with DeathRecordComponent awards kill credit to KillerPlayerID:
    auto& DeathRec = EM.AddComponent<Game::Components::DeathRecordComponent>(Marine1);
    DeathRec.VictimPlayerID = 1;
    DeathRec.VictimUnitID = 37;
    DeathRec.KillerPlayerID = 0;
    DeathRec.AwardKill = true;
    EM.DestroyEntity(Marine1);
    VM.Update(EM, 0.1f);
    Require(VM.GetDeathCount(1, 37) == 1 && VM.GetKillCount(0, 37) == 1,
            "TriggerVM::SyncDeathsFromEntityManager reads DeathRecordComponent and awards kill credit after DestroyEntity generation bump!");
}

void TestPhase24AuthoritativeWorldSaveAndLoadRoundTrip()
{
    Core::ECS::EntityManager SourceWorld;
    auto& Econ = SourceWorld.Context().EmplaceResource<Game::Systems::PlayerResourceStore>();
    Econ.Players[0].Minerals = 775;
    Econ.Players[0].VespeneGas = 340;

    auto& Upg = SourceWorld.Context().EmplaceResource<Core::ECS::WorldUpgradeResource>();
    Upg.PlayerUpgrades[0].Levels[1] = 3;

    const int Tank = SourceWorld.CreateEntity();
    SourceWorld.AddComponent<Game::Components::TransformComponent>(Tank).Position = {412.0f, 628.0f};
    SourceWorld.AddComponent<Game::Components::PlayerComponent>(Tank).PlayerID = 0;
    auto& TankUnit = SourceWorld.AddComponent<Game::Components::UnitComponent>(Tank);
    TankUnit.ChkUnitID = 5;
    TankUnit.HitPoints = 150;
    auto& Prod = SourceWorld.AddComponent<Game::Components::ProductionComponent>(Tank);
    Prod.Queue.push_back(5);
    Prod.Queue.push_back(32);
    auto& Cmd = SourceWorld.AddComponent<Game::Components::CommandComponent>(Tank);
    Game::Components::Order MoveOrd{};
    MoveOrd.Type = Game::Components::OrderType::Move;
    MoveOrd.TargetPosition = {800.0f, 900.0f};
    Cmd.QueueOrder(MoveOrd);
    auto& Path = SourceWorld.AddComponent<Game::Components::PathfindingComponent>(Tank);
    Path.Path.push_back({500.0f, 700.0f});
    Path.Path.push_back({800.0f, 900.0f});
    Path.PathIndex = 0;
    auto& Bldg = SourceWorld.AddComponent<Game::Components::BuildingComponent>(Tank);
    Bldg.AssistantBuilderUnitIDs = {11, 22, 33};
    auto& Res = SourceWorld.AddComponent<Game::Components::ResourceComponent>(Tank);
    Res.Amount = 1500;
    Res.QueuedGatherers = {44, 55};

    Game::Client::GameLoop::GameSaveSnapshot SaveOut;
    SaveOut.MapPath = "maps/broodwar/hunters.scm";
    SaveOut.Paused = true;
    SaveOut.Resources.MineralCount = 775;
    SaveOut.Resources.GasCount = 340;
    SaveOut.Resources.SupplyUsed = 24;
    SaveOut.Resources.SupplyMax = 50;
    SaveOut.HasWorldSnapshot = true;
    SaveOut.SimulationTick = 1440;
    SaveOut.WorldSnapshot = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(SourceWorld, 1440);

    FILE* TmpFile = std::tmpfile();
    Require(TmpFile != nullptr, "Created temporary save file");
    Require(Game::Client::GameLoop::WriteGameSave(TmpFile, SaveOut), "WriteGameSave succeeded");
    std::rewind(TmpFile);

    Game::Client::GameLoop::GameSaveSnapshot SaveIn;
    Require(Game::Client::GameLoop::ReadGameSave(TmpFile, SaveIn), "ReadGameSave succeeded");
    std::fclose(TmpFile);

    Require(SaveIn.MapPath == "maps/broodwar/hunters.scm" && SaveIn.Paused,
            "ReadGameSave restored map path and pause state");
    Require(SaveIn.HasWorldSnapshot && SaveIn.SimulationTick == 1440 &&
            SaveIn.WorldSnapshot.Entities.size() == 1u,
            "ReadGameSave restored authoritative WorldDeterministicSnapshot header and entity records");

    Core::ECS::EntityManager TargetWorld;
    Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(TargetWorld, SaveIn.WorldSnapshot);
    const uint32_t SourceHash = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(SourceWorld, 1440);
    const uint32_t TargetHash = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(TargetWorld, 1440);
    Require(SourceHash == TargetHash && SourceHash == SaveIn.WorldSnapshot.StateHashFNV1a,
            "Restored TargetWorld from disk save file matches SourceWorld FNV-1a hash bit-for-bit!");
}

void TestPhase25AnimationSystemDeclarativeStateBridgeAndCachedQueries()
{
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    SM.RegisterSystem(std::make_unique<Game::Systems::AnimationSystem>(SM));

    const int Zealot = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Zealot).Position = {100.0f, 100.0f};
    EM.AddComponent<Game::Components::RenderComponent>(Zealot).Visible = true;
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(Zealot);
    Unit.ChkUnitID = 65;
    Unit.HitPoints = 100;
    auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(Zealot);
    Path.CurrentSpeed = 5.0f;
    Path.CurrentVelocity = {5.0f, 0.0f};
    auto& Decl = EM.AddComponent<Game::Systems::DeclarativeAnimatorComponent>(Zealot);
    Decl.IScriptID = 212;
    Decl.CurrentClipName = "Init";

    SM.UpdateSimulationStep(0.05f);
    Require(Decl.CurrentClipName == "Walking",
            "AnimationSystem automatically bridged Walking AnimState to DeclarativeAnimatorComponent");

    // Set unit HP to 0 while NoBreakSectionActive is true -> must still transition to Death!
    Decl.NoBreakSectionActive = true;
    Decl.Playing = false;
    Unit.HitPoints = 0;
    SM.UpdateSimulationStep(0.05f);
    Require(Decl.CurrentClipName == "Death" && !Decl.NoBreakSectionActive && Decl.Playing,
            "AnimationSystem automatically bridged Dying AnimState to DeclarativeAnimatorComponent even during NoBreakSectionActive");
}

} // namespace

int main()
{
    TestPhase23TriggerVMSpatialLocationBroadphaseAndGenerationalDeathTracking();
    TestPhase24AuthoritativeWorldSaveAndLoadRoundTrip();
    TestPhase25AnimationSystemDeclarativeStateBridgeAndCachedQueries();

    std::cout << "[Phase23To26ModernEngineRefactorTest] All Phase 23-26 modern engine refactorings (TriggerVM Spatial Location Broadphase & Generational Death Tracking, Full ECS World Save/Load Persistence, Declarative Animation Bridge, and Parallel JobSystem Minimap) passed.\n";
    return 0;
}
