#pragma once

#include "Client/ClientRuntime.h"
#include "Main/GameSession.h"
#include "Render/Draw2D.h"

namespace Game::Client::GameLoop
{

inline void RenderGameplayWorldFrame(ClientRuntime& Client, GameSession& Session)
{
    Client.BeginWorldFrame();

    if (Session.IsMapLoaded())
    {
        Session.SyncCreepPresentation();
        Client.RenderWorld(Session.GetMapData(), Session.Entities(), Session.IsMapLoaded());
        Client.RenderWorldEffects(Render::g_Draw2D);
    }

    Client.EndWorldFrame();
}

} // namespace Game::Client::GameLoop
