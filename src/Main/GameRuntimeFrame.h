#pragma once

#include "Core/Platform/Platform.h"
#include "Main/GameLoop/GameSaveState.h"
#include "Main/GameSaveRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameInputRuntime.h"
#include "Main/GameRuntimeState.h"
#include "Main/GameSimulationRuntime.h"
#include "Main/GameUiPresentation.h"
#include "Main/GameWorldPresentation.h"
#include "Render/Renderer.h"

#include <cstdint>
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

    UpdateCampaignAndSimulationRuntime(
        Runtime.Session.Campaign,
        Runtime.Session.Game,
        Runtime.App.Audio,
        Runtime.Client.Runtime,
        Runtime.Client.Hud,
        Dt);

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
        Runtime.Client.Hud);
    RenderGuiPresentationLayer(Runtime.App.Gui);
    Runtime.App.Runtime.UpdateFrameStats();
}

inline void HandleGameRuntimeFrameEvent(GameRuntimeState& Runtime, const void* Event)
{
    Core::Platform::HandleEvent(Event);
    HandleGuiPresentationEventForState(Runtime.App.Gui, Runtime.App.Runtime.State, Event);
}

} // namespace Game::Client::GameLoop
