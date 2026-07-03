#pragma once

#include "Client/ClientRuntime.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void InitializeHudRuntime(UI::Hud& Hud, Game::Client::ClientRuntime& Client)
{
    Hud.Init(Client.GetUIAssets(), Client.GetSpriteAssets());
    ResetPlayerResourceState();
}

} // namespace Game::Client::GameLoop
