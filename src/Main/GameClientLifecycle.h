#pragma once

#include <iostream>

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Render/Draw2D.h"
#include "Render/Renderer.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline bool InitializeClientShellRuntime()
{
    Core::Platform::Initialize();
    if (!Render::Initialize())
    {
        std::cerr << "Renderer init failed" << std::endl;
        return false;
    }
    if (!Render::g_Draw2D.Init())
    {
        std::cerr << "Draw2D init failed" << std::endl;
        return false;
    }
    return true;
}

inline void ShutdownClientShellRuntime(ClientRuntime& Client, UI::Hud& Hud)
{
    Hud.Shutdown();
    Render::g_Draw2D.Shutdown();
    Client.ShutdownResources();
    Render::Shutdown();
    Core::Platform::Shutdown();
}

} // namespace Game::Client::GameLoop
