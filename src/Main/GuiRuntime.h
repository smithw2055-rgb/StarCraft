#pragma once

#if ENABLE_GUI_UI
#include "UI/GlueGuiHost.h"
#include "UI/MainGui.h"
#endif

namespace Game::Client::GameLoop
{

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

} // namespace Game::Client::GameLoop
