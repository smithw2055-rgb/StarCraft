#pragma once

#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"
#include "Main/GameLoop/GameSaveState.h"
// === Consolidated from src/Main/GameSaveRuntime.h ===
#include <cstddef>
#include <cstdio>
#include <string>

#include "AppRuntime.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Core/Utils/Logger.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/GameLoop/GameSaveState.h"
#include "Main/GameMenuPresentation.h"
#include "GameSession.h"

namespace Game::Client::GameLoop
{

inline GameSaveSnapshot CaptureGameSaveSnapshot(const AppRuntime& App,
                                                const GameMenuRuntime& Menu,
                                                const UI::Hud& Hud)
{
    GameSaveSnapshot Snapshot;
    Snapshot.MapPath = App.MapPath;
    Snapshot.Paused = Menu.IsPaused();
    Snapshot.Resources = CaptureHudResourceState(Hud);
    return Snapshot;
}

inline GameSaveSnapshot CaptureAuthoritativeGameSaveSnapshot(
    const AppRuntime& App,
    const GameMenuRuntime& Menu,
    const UI::Hud& Hud,
    const GameSession& Session)
{
    GameSaveSnapshot Snapshot = CaptureGameSaveSnapshot(App, Menu, Hud);
    if (Session.IsEcsInitialized())
    {
        Snapshot.HasWorldSnapshot = true;
        Snapshot.SimulationTick = Session.SimulationTick;
        Snapshot.WorldSnapshot = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(
            Session.EcsSysMgr.GetEntityManager(),
            static_cast<uint32_t>(Session.SimulationTick));
    }
    return Snapshot;
}

inline void ApplyGameSaveSnapshot(const GameSaveSnapshot& Snapshot,
                                  GameMenuRuntime& Menu,
                                  UI::Hud& Hud)
{
    RestorePlayerResourcesFromHudState(Hud, Snapshot.Resources);
    Menu.RestorePaused(Snapshot.Paused);
}

inline void ApplyAuthoritativeGameSaveSnapshot(
    const GameSaveSnapshot& Snapshot,
    GameMenuRuntime& Menu,
    UI::Hud& Hud,
    GameSession& Session)
{
    if (Snapshot.HasWorldSnapshot && Session.IsEcsInitialized())
    {
        Session.Entities().ActivateWorld();
        Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(
            Session.Entities(), Snapshot.WorldSnapshot);
        Session.SimulationTick = Snapshot.SimulationTick;
        Session.KinematicsStream.ExtractFromECS(Session.Entities());
        if (Snapshot.WorldSnapshot.HasEconomyResource)
        {
            SyncHudFromPlayerResources(Hud);
            Menu.RestorePaused(Snapshot.Paused);
            return;
        }
    }
    ApplyGameSaveSnapshot(Snapshot, Menu, Hud);
}

inline bool SaveGameSnapshotToFile(const std::string& FilePath, const GameSaveSnapshot& Snapshot)
{
    FILE* File = fopen(FilePath.c_str(), "wb");
    if (!File)
        return false;

    const bool Written = WriteGameSave(File, Snapshot);
    fclose(File);
    return Written;
}

inline bool LoadGameSnapshotFromFile(const std::string& FilePath, GameSaveSnapshot& Snapshot)
{
    FILE* File = fopen(FilePath.c_str(), "rb");
    if (!File)
        return false;

    const bool Loaded = ReadGameSave(File, Snapshot);
    fclose(File);
    return Loaded;
}

inline bool SaveCurrentGameToFile(const std::string& FilePath,
                                  const AppRuntime& App,
                                  const GameMenuRuntime& Menu,
                                  const UI::Hud& Hud)
{
    const GameSaveSnapshot Snapshot = CaptureGameSaveSnapshot(App, Menu, Hud);
    if (!SaveGameSnapshotToFile(FilePath, Snapshot))
        return false;

    Core::Utils::Logger::Info("GameController", "Game saved to " + FilePath);
    return true;
}

inline bool SaveCurrentGameToFile(const std::string& FilePath,
                                  const AppRuntime& App,
                                  const GameMenuRuntime& Menu,
                                  const UI::Hud& Hud,
                                  const GameSession& Session)
{
    const GameSaveSnapshot Snapshot = CaptureAuthoritativeGameSaveSnapshot(App, Menu, Hud, Session);
    if (!SaveGameSnapshotToFile(FilePath, Snapshot))
        return false;

    Core::Utils::Logger::Info("GameController", "Game saved to " + FilePath);
    return true;
}

inline bool LoadCurrentGameFromFile(const std::string& FilePath,
                                    const AppRuntime& App,
                                    GameMenuRuntime& Menu,
                                    UI::Hud& Hud)
{
    GameSaveSnapshot Snapshot = CaptureGameSaveSnapshot(App, Menu, Hud);
    if (!LoadGameSnapshotFromFile(FilePath, Snapshot))
        return false;

    ApplyGameSaveSnapshot(Snapshot, Menu, Hud);
    Core::Utils::Logger::Info("GameController", "Game loaded from " + FilePath);
    return true;
}

inline bool LoadCurrentGameFromFile(const std::string& FilePath,
                                    const AppRuntime& App,
                                    GameMenuRuntime& Menu,
                                    UI::Hud& Hud,
                                    GameSession& Session)
{
    GameSaveSnapshot Snapshot = CaptureGameSaveSnapshot(App, Menu, Hud);
    if (!LoadGameSnapshotFromFile(FilePath, Snapshot))
        return false;

    ApplyAuthoritativeGameSaveSnapshot(Snapshot, Menu, Hud, Session);
    Core::Utils::Logger::Info("GameController", "Game loaded from " + FilePath);
    return true;
}

inline std::string GetDefaultQuickSavePath()
{
    return "artifacts\\quicksave.scsave";
}

inline bool SaveSessionReplayToFile(GameSession& Session, const std::string& FilePath)
{
    size_t BytesWritten = 0;
    if (!Session.SaveReplayToFile(FilePath, &BytesWritten))
        return false;

    Core::Utils::Logger::Info("GameController",
        "Replay saved to " + FilePath + " (" + std::to_string(BytesWritten) + " bytes)");
    return true;
}

inline bool LoadSessionReplayFromFile(GameSession& Session, const std::string& FilePath)
{
    std::string GameName;
    if (!Session.LoadReplayFromFile(FilePath, &GameName))
    {
        Core::Utils::Logger::Warning("GameController", "Failed to load replay: " + FilePath);
        return false;
    }

    Core::Utils::Logger::Info("GameController",
        "Loaded replay: " + GameName +
        " (" + std::to_string(Session.GetLastReplayActionCount()) + " actions)");
    return true;
}

} // namespace Game::Client::GameLoop

#include "Main/GameGuiPresentation.h"
#include "Main/GameInputRuntime.h"
#include "Main/GameRuntimeState.h"
// === Consolidated from src/Main/GameScenes.h ===
#include <memory>
#include <string_view>

#include "Core/Scene/SceneManager.h"
#include "Main/AppRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameRuntimeState.h"
// === Consolidated from src/Main/GameSimulationRuntime.h ===
#include "Client/ClientRuntime.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/CampaignRuntime.h"
// === Consolidated from src/Main/GameNetworkActionRuntime.h ===
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Game/Components.h"
#include "Game/Networking/LockstepSync.h"
#include "Game/Systems/CommandSystem.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop
{

inline bool HostSessionNetworkGame(GameSession& Session,
                                   const std::string& BindIP,
                                   int Port,
                                   int MaxClients)
{
    return Session.HostNetworkGame(BindIP, Port, MaxClients);
}

inline bool JoinSessionNetworkGame(GameSession& Session,
                                   const std::string& HostIP,
                                   int Port)
{
    return Session.JoinNetworkGame(HostIP, Port);
}

inline void StartSessionNetworkGame(GameSession& Session)
{
    Session.StartNetworkGameIfEnabled();
}

inline void RecordSessionLocalFrameAction(GameSession& Session,
                                          uint64_t FrameCount,
                                          uint8_t PlayerID,
                                          uint8_t ActionType,
                                          uint16_t P1,
                                          uint16_t P2,
                                          uint16_t P3,
                                          uint8_t P4)
{
    Session.RecordLocalFrameAction((uint32_t)FrameCount, PlayerID, ActionType, P1, P2, P3, P4);
}

inline float ReadNetworkActionFloat(const std::vector<uint8_t>& Data, size_t Offset)
{
    float Value = 0.0f;
    if (Offset + sizeof(Value) <= Data.size())
        std::memcpy(&Value, Data.data() + Offset, sizeof(Value));
    return Value;
}

inline void ExecuteNetworkAction(GameSession& Session, const Game::Networking::SyncAction& Action)
{
    if (Action.Data.empty())
        return;

    auto& EM = Session.Entities();
    const uint8_t CmdType = Action.Data[0];

    auto CollectControllableUnits = [&]() -> std::vector<int> {
        std::vector<int> Units;
        EM.ForEachEntityWith<Game::Components::PlayerComponent, Game::Components::UnitComponent>(
            [&](Core::ECS::EntityID Id) {
                const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Id);
                const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Id);
                if (Player && Player->PlayerID == Action.PlayerID && Unit && Unit->HitPoints > 0)
                {
                    if (!EM.GetComponent<Game::Components::BuildingComponent>(Id) &&
                        !EM.GetComponent<Game::Components::ResourceComponent>(Id) &&
                        EM.GetComponent<Game::Components::CommandComponent>(Id))
                    {
                        Units.push_back((int)Id);
                    }
                }
            });
        return Units;
    };

    switch (CmdType)
    {
    case 0:
    case 1:
    {
        float X = 0.0f;
        float Y = 0.0f;
        std::vector<int> Units;

        if (CmdType == 0 && Action.Data.size() >= 9)
        {
            X = ReadNetworkActionFloat(Action.Data, 1);
            Y = ReadNetworkActionFloat(Action.Data, 5);
            if (Action.Data.size() > 9)
            {
                for (size_t i = 9; i + sizeof(int) <= Action.Data.size(); i += sizeof(int))
                {
                    int Id = 0;
                    std::memcpy(&Id, Action.Data.data() + i, sizeof(Id));
                    if (EM.IsAlive(Id))
                        Units.push_back(Id);
                }
            }
        }
        else if (Action.Data.size() >= 5)
        {
            const uint16_t P1 = (uint16_t)Action.Data[1] | ((uint16_t)Action.Data[2] << 8);
            const uint16_t P2 = (uint16_t)Action.Data[3] | ((uint16_t)Action.Data[4] << 8);
            X = (float)P1;
            Y = (float)P2;
        }

        if (Units.empty())
            Units = CollectControllableUnits();

        if (!Units.empty())
            Game::Systems::CommandSystem::IssueMove(EM, Units, {X, Y});
        break;
    }
    default:
        break;
    }
}

} // namespace Game::Client::GameLoop

#include "Main/GameSession.h"
#include "UI/Hud.h"

#include "Main/GameMatchOutcomeRuntime.h"

namespace Game::Client::GameLoop
{

inline void UpdateGameplaySimulationRuntime(
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    UI::Hud& Hud,
    float Dt)
{
    if (!Session.IsEcsInitialized())
        return;

    Session.ProcessNetworkFrame([&Session](const Game::Networking::SyncAction& Action) {
        ExecuteNetworkAction(Session, Action);
    });

    // Staged Dual-Phase Execution (Unity PlayerLoop / Godot SceneTree physics vs process split):
    // 1. Advance deterministic simulation phases (Input -> NetworkLockstep -> FixedSimulation -> TransformPropagation)
    //    and populate Session.KinematicsStream SoA buffers + optional RollbackStateManager ring buffer.
    Session.UpdateSimulationStep(Dt);

    // 2. Advance presentation & render extraction phases (Presentation -> RenderExtract)
    Session.UpdatePresentationFrame(Dt);

    Audio.UpdateForClientView(Client, Dt);

    if (Session.IsReplayRecording())
    {
        // Record dummy action per frame to keep the replay stream alive.
        // Real actions are recorded via RecordGameAction().
    }

    Session.Bwapi().OnFrame();

    SyncHudFromPlayerResources(Hud, Session.GetLocalPlayerID());

    Client.PruneSelection(Session.Entities());

    Audio.UpdateClientWorldEffects(Client, Session.Entities(), Dt);
}

inline void UpdateCampaignAndSimulationRuntime(
    CampaignRuntime& Campaign,
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    UI::Hud& Hud,
    float Dt)
{
    if (Campaign.IsActive())
    {
        Campaign.Controller().Update(Session.Entities(), Dt);
        Campaign.RecordVictoryProgressIfNeeded();

        const auto State = Campaign.Controller().GetState();
        if (State == Game::Campaign::CampaignState::Victory)
            Session.SetMatchOutcome(MatchOutcome::Victory);
        else if (State == Game::Campaign::CampaignState::Defeat)
            Session.SetMatchOutcome(MatchOutcome::Defeat);
        else if (State == Game::Campaign::CampaignState::Draw)
            Session.SetMatchOutcome(MatchOutcome::Draw);
    }
    else
    {
        UpdateMeleeMatchOutcome(Session, Dt);
    }

    // Scenario-triggered pauses freeze the simulation but keep
    // CampaignController ticking so Wait/Unpause/transmission triggers can resume.
    if (!Campaign.IsScenarioPaused())
        UpdateGameplaySimulationRuntime(Session, Audio, Client, Hud, Dt);
}

} // namespace Game::Client::GameLoop

#include "Main/GameUiPresentation.h"

namespace Game::Client::GameLoop
{

inline Core::Scene::SceneId ToSceneId(GameState State)
{
    return State == GameState::InGame
        ? Core::Scene::SceneId::Gameplay
        : Core::Scene::SceneId::MainMenu;
}

inline GameState ToGameState(Core::Scene::SceneId Id)
{
    return Id == Core::Scene::SceneId::Gameplay
        ? GameState::InGame
        : GameState::MainMenu;
}

class GlueMenuScene final : public Core::Scene::IScene
{
public:
    explicit GlueMenuScene(GameRuntimeState& Runtime)
        : mRuntime(Runtime)
    {
    }

    Core::Scene::SceneId GetId() const override { return Core::Scene::SceneId::MainMenu; }
    std::string_view GetName() const override { return "GlueMenuScene"; }

    bool OnPreUpdate(float /*DeltaSeconds*/) override
    {
        UpdateGuiPresentationForState(mRuntime.App.Gui, GameState::MainMenu);
        return true; // Main menu does not advance gameplay 24Hz ticks
    }

    void OnRender(float /*InterpolationAlpha*/) override
    {
        RenderGlueMenuPresentationLayer(mRuntime.App.Gui);
    }

    void OnEvent(const void* Event) override
    {
        HandleGuiPresentationEventForState(mRuntime.App.Gui, GameState::MainMenu, Event);
    }

private:
    GameRuntimeState& mRuntime;
};

class GameplayScene final : public Core::Scene::IScene
{
public:
    explicit GameplayScene(GameRuntimeState& Runtime)
        : mRuntime(Runtime)
    {
    }

    Core::Scene::SceneId GetId() const override { return Core::Scene::SceneId::Gameplay; }
    std::string_view GetName() const override { return "GameplayScene"; }

    void OnEnter() override
    {
        mRuntime.Session.Game.Entities().ActivateWorld();
    }

    void OnFixedUpdate(float FixedDeltaSeconds) override
    {
        mRuntime.Session.Game.SaveTransformTickState();
        UpdateCampaignAndSimulationRuntime(
            mRuntime.Session.Campaign,
            mRuntime.Session.Game,
            mRuntime.App.Audio,
            mRuntime.Client.Runtime,
            mRuntime.Client.Hud,
            FixedDeltaSeconds);
    }

    void OnPostUpdate(float /*DeltaSeconds*/, int FixedTicksExecuted) override
    {
        if (FixedTicksExecuted > 0 && mRuntime.Session.Game.IsMapLoaded())
        {
            mRuntime.Session.Game.SyncCreepPresentation();
        }
    }

    void OnRender(float InterpolationAlpha) override
    {
        RenderGameplayWorldFrame(mRuntime.Client.Runtime, mRuntime.Session.Game, InterpolationAlpha);
        RenderGameplayUiFrame(
            mRuntime.Client.Runtime,
            mRuntime.Session.Game,
            mRuntime.Session.Campaign,
            mRuntime.App.Menu,
            mRuntime.Client.Hud,
#if ENABLE_GUI_UI
            !(mRuntime.App.Gui.UseGui && mRuntime.App.Gui.MainGui.IsActive())
#else
            true
#endif
        );
        RenderGuiPresentationLayer(mRuntime.App.Gui);
        mRuntime.App.Runtime.UpdateFrameStats();
    }

    void OnEvent(const void* Event) override
    {
        HandleGuiPresentationEventForState(mRuntime.App.Gui, GameState::InGame, Event);
    }

private:
    GameRuntimeState& mRuntime;
};

inline Core::Scene::SceneManager& EnsureRuntimeScenesRegistered(GameRuntimeState& Runtime)
{
    auto& Scenes = Runtime.App.Runtime.Scenes;
    if (!Scenes.HasScene(Core::Scene::SceneId::MainMenu))
    {
        Scenes.ConfigureFixedStep(1.0 / 24.0, 2);
        Scenes.RegisterScene(std::make_unique<GlueMenuScene>(Runtime));
        Scenes.RegisterScene(std::make_unique<GameplayScene>(Runtime));
    }

    const Core::Scene::SceneId DesiredScene = ToSceneId(Runtime.App.Runtime.State);
    if (Scenes.GetActiveSceneId() != DesiredScene)
    {
        Scenes.SwitchToImmediate(DesiredScene);
    }
    return Scenes;
}

} // namespace Game::Client::GameLoop


#include "Main/GameUiPresentation.h"
#include "Render/Renderer.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

namespace Game::Client::GameLoop
{

template <typename SaveGameT,
          typename LoadGameT,
          typename RefreshGuiT,
          typename RecordGameActionT,
          typename RestartGameT,
          typename ReturnToMainMenuT>
inline void UpdateGameRuntimeFrame(
    GameRuntimeState& Runtime,
    float Dt,
    SaveGameT&& SaveGame,
    LoadGameT&& LoadGame,
    RefreshGuiT&& RefreshGui,
    RecordGameActionT&& RecordGameAction,
    RestartGameT&& RestartGame,
    ReturnToMainMenuT&& ReturnToMainMenu)
{
    Core::Platform::SetDeltaTime(Dt);
    Core::Platform::ProcessInput();

    if (Runtime.InteractionDemo.Enabled && !Runtime.InteractionDemo.FrameProbeLogged)
    {
        Runtime.InteractionDemo.FrameProbeLogged = true;
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "frame probe initialized=" +
                std::to_string(Runtime.App.Runtime.Initialized ? 1 : 0) +
                " state=" +
                std::to_string(Runtime.App.Runtime.State == GameState::InGame ? 1 : 0) +
                " ecs=" +
                std::to_string(Runtime.Session.Game.IsEcsInitialized() ? 1 : 0) +
                " map=" +
                std::to_string(Runtime.Session.Game.IsMapLoaded() ? 1 : 0) +
                " menuOpen=" +
                std::to_string(Runtime.App.Menu.IsOpen() ? 1 : 0) +
                " menuPaused=" +
                std::to_string(Runtime.App.Menu.IsPaused() ? 1 : 0));
    }

    if (!Runtime.App.Runtime.Initialized)
        return;

    auto& Scenes = EnsureRuntimeScenesRegistered(Runtime);

    if (UpdateGuiPresentationForState(Runtime.App.Gui, Runtime.App.Runtime.State))
        return;

    if (UpdateActiveGameplayInputRuntime(
            Runtime.Client.Runtime,
            Runtime.Session.Game,
            Runtime.App.Gui,
            Runtime.App.Menu,
            Runtime.Client.Hud,
            Dt,
            GetDefaultQuickSavePath(),
            std::forward<SaveGameT>(SaveGame),
            std::forward<LoadGameT>(LoadGame),
            [&RefreshGui]() { RefreshGui(); },
            [&RecordGameAction](uint8_t PlayerID, uint8_t ActionType, uint16_t P1, uint16_t P2) {
                RecordGameAction(PlayerID, ActionType, P1, P2);
            },
            std::forward<RestartGameT>(RestartGame),
            std::forward<ReturnToMainMenuT>(ReturnToMainMenu)))
    {
        return;
    }

    float SimulationScale = RunInteractionDemoIfRequested(
        Runtime.InteractionDemo,
        Runtime.Session.Game,
        Runtime.Client.Runtime,
        Runtime.Client.Hud,
        Dt);
    bool HasObserverSpeedOverride = false;
    if (const char* ObserverSpeed = std::getenv("STARCRAFT_AI_OBSERVER_SPEED"))
    {
        const float RequestedScale = std::strtof(ObserverSpeed, nullptr);
        if (RequestedScale >= 1.0f && RequestedScale <= 16.0f)
        {
            SimulationScale = RequestedScale;
            HasObserverSpeedOverride = true;
        }
        static bool SpeedLogged = false;
        if (!SpeedLogged)
        {
            SpeedLogged = true;
            Core::Utils::Logger::Info(
                "AiObserverMatch", "simulation_speed=" +
                std::to_string(SimulationScale) + "x");
        }
    }

    const float SimulationInputDt = HasObserverSpeedOverride
        ? std::min(Dt, 1.0f / 15.0f)
        : Dt;
    const float SimulationDt = SimulationInputDt * SimulationScale;

    RunInteractionSmokeIfRequested(
        Runtime.InteractionSmoke,
        Runtime.Session.Game,
        Runtime.Session.Campaign,
        Runtime.Client.Runtime,
        Runtime.App.Gui,
        Runtime.Client.Hud);

    const int kMaximumSimulationTicksPerHostFrame = HasObserverSpeedOverride
        ? std::min(32, (int)std::ceil(SimulationScale * 2.0f))
        : 2;

    // Synchronize SceneManager fixed-step state with GameSession accumulator/tick counters
    // and step the active GameplayScene deterministically at 24Hz.
    Scenes.SetAccumulatorSeconds(Runtime.Session.Game.SimulationAccumulatorSeconds);
    Scenes.SetFixedTickCount(Runtime.Session.Game.SimulationTick);
    Scenes.StepFrame(SimulationDt, 1.0f, kMaximumSimulationTicksPerHostFrame);
    Runtime.Session.Game.SimulationAccumulatorSeconds = Scenes.GetAccumulatorSeconds();
    Runtime.Session.Game.SimulationTick = Scenes.GetFixedTickCount();

    RefreshGui();
}

inline void RenderGameRuntimeFrame(GameRuntimeState& Runtime)
{
    if (!Runtime.App.Runtime.Initialized)
    {
        Render::BeginFrame();
        Render::EndFrame();
        return;
    }

    auto& Scenes = EnsureRuntimeScenesRegistered(Runtime);
    Scenes.SetAccumulatorSeconds(Runtime.Session.Game.SimulationAccumulatorSeconds);
    Scenes.RenderActiveScene();
}

inline void HandleGameRuntimeFrameEvent(GameRuntimeState& Runtime, const void* Event)
{
    Core::Platform::HandleEvent(Event);
    auto& Scenes = EnsureRuntimeScenesRegistered(Runtime);
    Scenes.DispatchEvent(Event);
}

} // namespace Game::Client::GameLoop
