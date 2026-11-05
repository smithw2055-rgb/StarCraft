#pragma once

#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"
#include "Main/GameLoop/GameSaveState.h"
#include "Main/GameSaveRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameInputRuntime.h"
#include "Main/GameInteractionSmokeRuntime.h"
#include "Main/GameRuntimeState.h"
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
    // Observer acceleration is a tick-production control, not wall-clock
    // catch-up.  Feeding a slow simulation frame back through a 16x scale
    // creates an ever-growing accumulator ("spiral of death").  Normal
    // gameplay retains the complete host delta; accelerated observation caps
    // only its synthetic input to one 15 Hz host frame.  This still permits
    // accelerated observation while bounding any single-frame catch-up batch.
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

    // Authoritative simulation advances only in complete classic 24 Hz ticks.
    // Host-frame residual time remains in the session accumulator so stable,
    // alternating and accelerated render rates produce the same tick states.
    constexpr double kSimulationTickSeconds = 1.0 / 24.0;
    const int kMaximumSimulationTicksPerHostFrame = HasObserverSpeedOverride
        ? std::min(32, (int)std::ceil(SimulationScale * 2.0f))
        : 2;
    Runtime.Session.Game.SimulationAccumulatorSeconds +=
        (double)SimulationDt;
    int SimulationTicksThisHostFrame = 0;
    while (Runtime.Session.Game.SimulationAccumulatorSeconds + 1.0e-12 >=
               kSimulationTickSeconds &&
           SimulationTicksThisHostFrame <
               kMaximumSimulationTicksPerHostFrame)
    {
        Runtime.Session.Game.SaveTransformTickState();
        UpdateCampaignAndSimulationRuntime(
            Runtime.Session.Campaign,
            Runtime.Session.Game,
            Runtime.App.Audio,
            Runtime.Client.Runtime,
            Runtime.Client.Hud,
            (float)kSimulationTickSeconds);
        Runtime.Session.Game.SimulationAccumulatorSeconds -=
            kSimulationTickSeconds;
        ++Runtime.Session.Game.SimulationTick;
        ++SimulationTicksThisHostFrame;
    }

    if (SimulationTicksThisHostFrame > 0 && Runtime.Session.Game.IsMapLoaded())
    {
        Runtime.Session.Game.SyncCreepPresentation();
    }
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

    if (Runtime.App.Runtime.State == GameState::MainMenu)
    {
        RenderGlueMenuPresentationLayer(Runtime.App.Gui);
        return;
    }

    constexpr double kSimulationTickSeconds = 1.0 / 24.0;
    const float InterpAlpha = std::clamp(
        static_cast<float>(Runtime.Session.Game.SimulationAccumulatorSeconds / kSimulationTickSeconds),
        0.0f, 1.0f);
    RenderGameplayWorldFrame(Runtime.Client.Runtime, Runtime.Session.Game, InterpAlpha);
    RenderGameplayUiFrame(
        Runtime.Client.Runtime,
        Runtime.Session.Game,
        Runtime.Session.Campaign,
        Runtime.App.Menu,
        Runtime.Client.Hud,
#if ENABLE_GUI_UI
        !(Runtime.App.Gui.UseGui && Runtime.App.Gui.MainGui.IsActive())
#else
        true
#endif
    );
    RenderGuiPresentationLayer(Runtime.App.Gui);
    Runtime.App.Runtime.UpdateFrameStats();
}

inline void HandleGameRuntimeFrameEvent(GameRuntimeState& Runtime, const void* Event)
{
    Core::Platform::HandleEvent(Event);
    HandleGuiPresentationEventForState(Runtime.App.Gui, Runtime.App.Runtime.State, Event);
}

} // namespace Game::Client::GameLoop
