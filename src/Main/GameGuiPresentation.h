#pragma once

namespace Game::Client { struct ClientRuntime; }
namespace Game::Client::UI { class Hud; }

namespace Game::Client::GameLoop
{

struct GuiRuntime;
struct GameSession;
enum class GameState;

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
