#pragma once

#include <functional>
#include <string>

namespace Game::Client::GameLoop
{

struct GuiRuntime;

void InitializeGuiRuntime(
    GuiRuntime& Gui,
    std::string& AssetRoot,
    int Width,
    int Height,
    std::function<void(const std::string& Action)> GlueActionCallback,
    bool EnableGlueMenu = true);

void ShutdownGuiRuntime(GuiRuntime& Gui);

} // namespace Game::Client::GameLoop
