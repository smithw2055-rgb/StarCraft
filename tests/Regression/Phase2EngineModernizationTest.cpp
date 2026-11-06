#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Core/Scene/SceneManager.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Races/UnitPrototypeRegistry.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/UpgradeSystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace Core::Audio
{
int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags)
{
    return -1;
}
} // namespace Core::Audio

namespace
{

void Require(bool Condition, const char* Message)
{
    if (Condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message);
    std::exit(1);
}

class MockScene final : public Core::Scene::IScene
{
public:
    MockScene(Core::Scene::SceneId Id, std::string_view Name)
        : mId(Id), mName(Name)
    {
    }

    Core::Scene::SceneId GetId() const override { return mId; }
    std::string_view GetName() const override { return mName; }

    void OnEnter() override { ++EnteredCount; Active = true; }
    void OnExit() override { ++ExitedCount; Active = false; }

    bool OnPreUpdate(float Dt) override
    {
        LastPreUpdateDt = Dt;
        return BlockFixedStep;
    }

    void OnFixedUpdate(float FixedDt) override
    {
        ++FixedTickCount;
        LastFixedDt = FixedDt;
    }

    void OnPostUpdate(float Dt, int FixedSteps) override
    {
        LastPostUpdateDt = Dt;
        LastFixedStepsInFrame = FixedSteps;
    }

    void OnRender(float Alpha) override
    {
        ++RenderCount;
        LastRenderAlpha = Alpha;
    }

    void OnEvent(const void* Ev) override
    {
        LastEventPtr = Ev;
    }

    Core::Scene::SceneId mId;
    std::string_view mName;
    bool Active = false;
    bool BlockFixedStep = false;
    int EnteredCount = 0;
    int ExitedCount = 0;
    int FixedTickCount = 0;
    int RenderCount = 0;
    int LastFixedStepsInFrame = 0;
    float LastPreUpdateDt = 0.0f;
    float LastFixedDt = 0.0f;
    float LastPostUpdateDt = 0.0f;
    float LastRenderAlpha = 0.0f;
    const void* LastEventPtr = nullptr;
};

void TestSceneManagerLifecycleAndFixedStep()
{
    Core::Scene::SceneManager Scenes;
    Scenes.ConfigureFixedStep(1.0 / 24.0, 4);

    auto MenuPtr = std::make_unique<MockScene>(Core::Scene::SceneId::MainMenu, "GlueMenuScene");
    auto GamePtr = std::make_unique<MockScene>(Core::Scene::SceneId::Gameplay, "GameplayScene");
    MockScene* Menu = MenuPtr.get();
    MockScene* Game = GamePtr.get();

    Scenes.RegisterScene(std::move(MenuPtr));
    Scenes.RegisterScene(std::move(GamePtr));

    Core::Scene::SceneId LastFrom = Core::Scene::SceneId::None;
    Core::Scene::SceneId LastTo = Core::Scene::SceneId::None;
    Scenes.SetTransitionCallback([&](Core::Scene::SceneId From, Core::Scene::SceneId To)
    {
        LastFrom = From;
        LastTo = To;
    });

    Require(Scenes.SwitchToImmediate(Core::Scene::SceneId::MainMenu), "switch to MainMenu succeeds");
    Require(Menu->Active && Menu->EnteredCount == 1, "MainMenu OnEnter called");

    // Request deferred switch to Gameplay
    Scenes.RequestSwitchTo(Core::Scene::SceneId::Gameplay);
    Require(Scenes.GetActiveSceneId() == Core::Scene::SceneId::MainMenu,
            "deferred switch waits until next StepFrame");

    // Step 1/48s (half of a 24Hz tick): transitions to Gameplay, 0 fixed ticks, alpha = 0.5
    const int TicksHalf = Scenes.StepFrame(1.0f / 48.0f);
    Require(Scenes.GetActiveSceneId() == Core::Scene::SceneId::Gameplay,
            "StepFrame applies pending transition to Gameplay");
    Require(!Menu->Active && Menu->ExitedCount == 1, "MainMenu OnExit called");
    Require(Game->Active && Game->EnteredCount == 1, "Gameplay OnEnter called");
    Require(LastFrom == Core::Scene::SceneId::MainMenu &&
                LastTo == Core::Scene::SceneId::Gameplay,
            "transition callback recorded MainMenu -> Gameplay");
    Require(TicksHalf == 0 && Game->FixedTickCount == 0,
            "half tick does not trigger OnFixedUpdate yet");

    Scenes.RenderActiveScene();
    Require(Game->RenderCount == 1 && std::fabs(Game->LastRenderAlpha - 0.5f) < 0.001f,
            "interpolation alpha is 0.5 at half tick");

    // Step another 1/48s: reaches 1/24s -> 1 fixed tick executed, alpha = 0.0
    const int TicksFull = Scenes.StepFrame(1.0f / 48.0f);
    Require(TicksFull == 1 && Game->FixedTickCount == 1,
            "second half tick triggers exactly one 24Hz OnFixedUpdate");
    Require(std::fabs(Game->LastFixedDt - (1.0f / 24.0f)) < 0.0001f,
            "OnFixedUpdate receives exact 1/24s step");

    // Test Overlay stack (e.g. Pause modal blocking simulation ticks while rendering on top)
    auto PauseOverlayPtr = std::make_unique<MockScene>(Core::Scene::SceneId::Custom, "PauseOverlay");
    MockScene* PauseOverlay = PauseOverlayPtr.get();
    PauseOverlay->BlockFixedStep = true;
    Scenes.PushOverlay(std::move(PauseOverlayPtr));
    Require(Scenes.HasOverlay() && PauseOverlay->Active, "overlay pushed and entered");

    const int TicksWhilePaused = Scenes.StepFrame(1.0f / 12.0f);
    Require(TicksWhilePaused == 0 && Game->FixedTickCount == 1,
            "modal overlay blocking pre-update pauses underlying Gameplay fixed ticks");

    int DummyEvent = 42;
    Scenes.DispatchEvent(&DummyEvent);
    Require(PauseOverlay->LastEventPtr == &DummyEvent && Game->LastEventPtr == nullptr,
            "overlay intercepts input events before underlying scene");

    Scenes.RenderActiveScene();
    Require(Game->RenderCount == 2 && PauseOverlay->RenderCount == 1,
            "RenderActiveScene renders base scene followed by overlay stack");

    Require(Scenes.PopOverlay() && !Scenes.HasOverlay(), "PopOverlay exits and removes overlay");
}

void TestUnitPrototypeRegistryAndDataDrivenSync()
{
    auto& Registry = Game::Races::UnitPrototypeRegistry::Instance();
    Registry.RebuildFromDefaults();

    // Verify semantic flags replace hardcoded magic numbers
    Require(Game::Races::IsWorkerChkUnitID(7) &&
                Game::Races::IsWorkerChkUnitID(41) &&
                Game::Races::IsWorkerChkUnitID(64) &&
                !Game::Races::IsWorkerChkUnitID(0),
            "semantic worker classification matches SCV/Drone/Probe");
    Require(Game::Races::IsTownHallChkUnitID(106) &&
                Game::Races::IsTownHallChkUnitID(131) &&
                Game::Races::IsTownHallChkUnitID(132) &&
                Game::Races::IsTownHallChkUnitID(133) &&
                Game::Races::IsTownHallChkUnitID(154),
            "semantic town hall classification matches CC/Hatchery/Lair/Hive/Nexus");
    Require(Game::Races::IsLarvaChkUnitID(35) && !Game::Races::IsLarvaChkUnitID(36),
            "semantic larva classification matches Zerg Larva");
    Require(Game::Races::IsOverlordChkUnitID(42),
            "semantic overlord classification matches Zerg Overlord");
    Require(Game::Races::IsTransportChkUnitID(11) &&
                Game::Races::IsTransportChkUnitID(69) &&
                Game::Races::IsTransportChkUnitID(42),
            "semantic transport classification matches Dropship/Shuttle/Overlord");
    Require(Game::Races::IsBunkerChkUnitID(125),
            "semantic bunker classification matches Terran Bunker");
    Require(Registry.IsMineralField(176) && Registry.IsMineralField(177) &&
                Registry.IsMineralField(178) && Registry.IsVespeneGeyser(188),
            "semantic resource classification matches mineral fields and vespene geyser");

    // Verify runtime .dat synchronization and Mod override propagation through GetUnitDefByID
    const int MarineDefID = Game::Races::GetUnitDefID("Marine");
    Require(MarineDefID >= 0, "Marine DefID resolved");
    const auto OriginalMarine = Game::Races::GetUnitDefByID(MarineDefID);
    Require(OriginalMarine.HitPoints == 40 && OriginalMarine.GroundDamage == 6,
            "baseline Marine stats match canonical Brood War values");

    Game::Races::UnitDatSyncEntry MarinePatch;
    MarinePatch.ChkUnitID = 0; // Terran Marine
    MarinePatch.HitPoints = 55;
    MarinePatch.GroundDamage = 9;
    Require(Registry.SyncFromDatEntries({MarinePatch}) == 1,
            "SyncFromDatEntries updates Marine prototype");

    const auto PatchedMarine = Game::Races::GetUnitDefByID(MarineDefID);
    Require(PatchedMarine.HitPoints == 55 && PatchedMarine.GroundDamage == 9,
            "GetUnitDefByID reflects data-driven prototype sync immediately");

    // Restore canonical defaults so downstream tests see standard values
    Registry.RebuildFromDefaults();
    Require(Game::Races::GetUnitDefByID(MarineDefID).HitPoints == 40,
            "RebuildFromDefaults restores canonical Marine stats");
}

void TestMultiWorldSimulationContextAndBuildingOrderIsolation()
{
    Core::ECS::SystemManager WorldA;
    Core::ECS::SystemManager WorldB;
    auto& EMA = WorldA.GetEntityManager();
    auto& EMB = WorldB.GetEntityManager();

    Game::Map::GameMap MapA;
    MapA.Generate(32, 32);
    Game::Map::GameMap MapB;
    MapB.Generate(32, 32);
    for (int Y = 4; Y <= 12; ++Y)
    {
        for (int X = 4; X <= 12; ++X)
        {
            Game::Map::Tile T = MapA.GetTile(X, Y);
            T.Walkable = false;
            MapA.SetTile(X, Y, T);
        }
    }

    Game::SimulationContext::Get(EMA).Map = &MapA;
    Game::SimulationContext::Get(EMB).Map = &MapB;

    auto SpawnDropshipWithPassenger = [](Core::ECS::EntityManager& EM, Core::Math::Vec2 Pos)
    {
        const int Dropship = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Dropship).Position = Pos;
        auto& DU = EM.AddComponent<Game::Components::UnitComponent>(Dropship);
        DU.ChkUnitID = 11; // Dropship
        DU.HitPoints = 150;
        EM.AddComponent<Game::Components::PlayerComponent>(Dropship).PlayerID = 0;

        const int Passenger = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Passenger).Position = Pos;
        auto& PU = EM.AddComponent<Game::Components::UnitComponent>(Passenger);
        PU.ChkUnitID = 0; // Marine
        PU.HitPoints = 40;
        EM.AddComponent<Game::Components::PlayerComponent>(Passenger).PlayerID = 0;

        Require(Game::Systems::TransportSystem::LoadUnit(EM, Dropship, Passenger),
                "loaded Marine into Dropship");
        return Dropship;
    };

    const Core::Math::Vec2 TestPos = MapA.TileToWorld(8, 8);
    const int DropshipA = SpawnDropshipWithPassenger(EMA, TestPos);
    const int DropshipB = SpawnDropshipWithPassenger(EMB, TestPos);

    Require(!Game::Systems::TransportSystem::CanUnload(EMA, DropshipA, 0),
            "WorldA rejects unload over impassable terrain in MapA via SimulationContext");
    Require(Game::Systems::TransportSystem::CanUnload(EMB, DropshipB, 0),
            "WorldB permits unload over walkable terrain in MapB via SimulationContext");

    // Verify BuildingOrderStore isolation between WorldA and WorldB
    Game::Systems::BuildingSystem::ResetPendingBuildOrders(EMA);
    Game::Systems::BuildingSystem::ResetPendingBuildOrders(EMB);

    const int BarracksDefID = Game::Races::GetUnitDefID("Barracks");
    Game::Systems::BuildOrder OrderA;
    OrderA.Active = true;
    OrderA.BuilderID = 10;
    OrderA.PlayerID = 0;
    OrderA.BuildingDefID = BarracksDefID;
    OrderA.Position = {256.0f, 256.0f};
    EMA.GetResource<Game::Systems::BuildingOrderStore>()->PendingBuildOrders.push_back(OrderA);

    Require(Game::Systems::BuildingSystem::CountPendingBuilds(EMA, 0, BarracksDefID) == 1,
            "WorldA has 1 pending Barracks build order");
    Require(Game::Systems::BuildingSystem::CountPendingBuilds(EMB, 0, BarracksDefID) == 0,
            "WorldB has 0 pending Barracks build orders (isolated BuildingOrderStore)");
    Require(Game::Systems::BuildingSystem::IsBuilderCommitted(EMA, 10),
            "Builder 10 is committed in WorldA");
    Require(!Game::Systems::BuildingSystem::IsBuilderCommitted(EMB, 10),
            "Builder 10 is not committed in WorldB");
}

} // namespace

int main()
{
    TestSceneManagerLifecycleAndFixedStep();
    TestUnitPrototypeRegistryAndDataDrivenSync();
    TestMultiWorldSimulationContextAndBuildingOrderIsolation();

    std::printf("Phase2EngineModernizationTest passed\n");
    return 0;
}
