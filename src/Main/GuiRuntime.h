#pragma once

#if ENABLE_GUI_UI
#include "UI/GlueGuiHost.h"
#include "UI/MainGui.h"
#endif

namespace Game::Client::GameLoop
{

struct GuiRuntime
{
#if ENABLE_GUI_UI
    UI::MainGui MainGui;
    UI::GlueGuiHost GlueGui;
    bool UseGui = false;
    bool GuiInitialized = false;
    bool GuiD3D11MediaInitialized = false;
    bool GlueGuiInitialized = false;
#endif
};

} // namespace Game::Client::GameLoop
