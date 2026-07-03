#pragma once

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void UpdateClientCameraForCurrentViewport(ClientRuntime& Client, const UI::Hud& Hud, float Dt)
{
    const int ViewportWidth = Core::Platform::GetWindowWidth();
    const int ViewportHeight = Core::Platform::GetWindowHeight();
    Client.UpdateCameraForViewport(
        ViewportWidth,
        ViewportHeight,
        (float)Hud.GetResourceBarHeight(),
        (float)Hud.GetHudHeight(ViewportWidth, ViewportHeight),
        Dt);
}

} // namespace Game::Client::GameLoop
