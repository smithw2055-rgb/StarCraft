#pragma once

#include <functional>
#include <string>

#if ENABLE_GUI_UI
#include "UI/GlueGuiHost.h"
#include "UI/MainGui.h"
#endif

namespace Game::Client { struct ClientRuntime; }
namespace Game::Client::UI { class Hud; }

namespace Game::Client::GameLoop
{

struct GameSession;
enum class GameState;

struct GuiRuntime
{
    bool UseGui = false;
    bool GuiInitialized = false;
    bool GlueGuiInitialized = false;
    int AppliedWidth = 0;
    int AppliedHeight = 0;
#if ENABLE_GUI_UI
    UI::MainGui MainGui;
    UI::GlueGuiHost GlueGui;
#endif
};

void InitializeGuiRuntime(
    GuiRuntime& Gui,
    std::string& AssetRoot,
    int Width,
    int Height,
    std::function<void(const std::string& Action)> GlueActionCallback,
    std::function<void(const std::string& AudioPath)> GlueAudioCallback = nullptr,
    bool EnableGlueMenu = true);

void ShutdownGuiRuntime(GuiRuntime& Gui);

void UpdateGlueMenuPresentationLayer(GuiRuntime& Gui);
bool UpdateGuiPresentationForState(GuiRuntime& Gui, GameState State);
void RenderGlueMenuPresentationLayer(GuiRuntime& Gui);
bool HandleGlueMenuPresentationEvent(GuiRuntime& Gui, const void* Event);
void HandleGuiPresentationEventForState(GuiRuntime& Gui, GameState State, const void* Event);

void UpdateGuiPresentationLayer(
    GuiRuntime& Gui,
    UI::Hud& Hud,
    GameSession& Session,
    ClientRuntime& Client);
void RenderGuiPresentationLayer(GuiRuntime& Gui);
void HandleGuiHudPresentationEvent(GuiRuntime& Gui, const void* Event);

} // namespace Game::Client::GameLoop
