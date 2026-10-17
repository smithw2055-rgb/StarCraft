#pragma once

#include "Client/ClientRuntime.h"
#include "Main/GameSession.h"
#include "Render/Draw2D.h"

namespace Game::Client::GameLoop
{

inline void RenderGameplayWorldFrame(ClientRuntime& Client, GameSession& Session, float InterpAlpha = 1.0f)
{
    Client.BeginWorldFrame();

    if (Session.IsMapLoaded())
    {
        Client.RenderWorld(Session.GetMapData(), Session.Entities(), Session.IsMapLoaded(), InterpAlpha);
        Client.RenderWorldEffects(Render::g_Draw2D);
    }

    Client.EndWorldFrame();
}

} // namespace Game::Client::GameLoop
