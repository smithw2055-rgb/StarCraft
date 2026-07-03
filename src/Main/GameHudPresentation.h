#pragma once

#include "Client/ClientRuntime.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void RenderNativeHudFrame(UI::Hud& Hud,
                                 GameSession& Session,
                                 Game::Client::ClientRuntime& Client,
                                 int ViewportWidth,
                                 int ViewportHeight)
{
    Hud.UpdateContext(Session.Entities(), Client.GetSelectedIds());
    Hud.Draw(Session.Entities(),
             Client.GetSelectedIds(),
             Session.GetMapData().Width,
             Session.GetMapData().Height,
             ViewportWidth,
             ViewportHeight);
}

} // namespace Game::Client::GameLoop
