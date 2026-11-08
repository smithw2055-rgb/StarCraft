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
#include "GameMenuRuntime.h"
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
#include "Main/GameInteractionSmokeRuntime.h"
#include "Main/GameRuntimeState.h"
#include "Main/GameScenes.h"

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
