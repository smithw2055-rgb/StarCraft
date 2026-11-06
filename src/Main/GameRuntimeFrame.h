#pragma once

#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"
#include "Main/GameLoop/GameSaveState.h"
#include "Main/GameSaveRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameInputRuntime.h"
#include "Main/GameInteractionSmokeRuntime.h"
#include "Main/GameRuntimeState.h"
#include "Main/GameScenes.h"
#include "Main/GameSimulationRuntime.h"
#include "Main/GameUiPresentation.h"
#include "Main/GameWorldPresentation.h"
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
