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
          typename RecordGameActionT>
inline void UpdateGameRuntimeFrame(
    GameRuntimeState& Runtime,
    float Dt,
    SaveGameT&& SaveGame,
    LoadGameT&& LoadGame,
    RefreshGuiT&& RefreshGui,
    RecordGameActionT&& RecordGameAction)
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
            }))
    {
        return;
    }

    float SimulationScale = RunInteractionDemoIfRequested(
        Runtime.InteractionDemo,
        Runtime.Session.Game,
        Runtime.Client.Runtime,
        Runtime.Client.Hud,
        Dt);
    if (const char* ObserverSpeed = std::getenv("STARCRAFT_AI_OBSERVER_SPEED"))
    {
        const float RequestedScale = std::strtof(ObserverSpeed, nullptr);
        if (RequestedScale >= 1.0f && RequestedScale <= 16.0f)
            SimulationScale = RequestedScale;
        static bool SpeedLogged = false;
        if (!SpeedLogged)
        {
            SpeedLogged = true;
            Core::Utils::Logger::Info(
                "AiObserverMatch", "simulation_speed=" +
                std::to_string(SimulationScale) + "x");
        }
    }
    const float SimulationDt = Dt * SimulationScale;

    RunInteractionSmokeIfRequested(
        Runtime.InteractionSmoke,
        Runtime.Session.Game,
        Runtime.Session.Campaign,
        Runtime.Client.Runtime,
        Runtime.App.Gui,
        Runtime.Client.Hud);

    // Fast observer playback must advance the complete simulation in bounded
    // steps. Passing Dt*16 once made bullets jump across several IScript
    // frames while AnimationSystem emitted every intermediate effect at the
    // final position (most visibly, clustered Lurker spines and missed line
    // damage). Keep rendering at the host frame rate, but advance every game
    // system in at most one classic 24 Hz simulation frame per substep.
    constexpr float kMaximumSimulationStep = 1.0f / 24.0f;
    float RemainingSimulationTime = SimulationDt;
    while (RemainingSimulationTime > 0.000001f)
    {
        const float Step = RemainingSimulationTime > kMaximumSimulationStep
            ? kMaximumSimulationStep
            : RemainingSimulationTime;
        UpdateCampaignAndSimulationRuntime(
            Runtime.Session.Campaign,
            Runtime.Session.Game,
            Runtime.App.Audio,
            Runtime.Client.Runtime,
            Runtime.Client.Hud,
            Step);
        RemainingSimulationTime -= Step;
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

    RenderGameplayWorldFrame(Runtime.Client.Runtime, Runtime.Session.Game);
    RenderGameplayUiFrame(
        Runtime.Client.Runtime,
        Runtime.Session.Game,
        Runtime.Session.Campaign,
        Runtime.App.Menu,
        Runtime.Client.Hud,
        !(Runtime.App.Gui.UseGui && Runtime.App.Gui.MainGui.IsActive()));
    RenderGuiPresentationLayer(Runtime.App.Gui);
    Runtime.App.Runtime.UpdateFrameStats();
}

inline void HandleGameRuntimeFrameEvent(GameRuntimeState& Runtime, const void* Event)
{
    Core::Platform::HandleEvent(Event);
    HandleGuiPresentationEventForState(Runtime.App.Gui, Runtime.App.Runtime.State, Event);
}

} // namespace Game::Client::GameLoop
