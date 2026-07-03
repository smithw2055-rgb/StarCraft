#pragma once

#include <utility>

#include "Client/ClientRuntime.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/AppRuntime.h"
#include "Main/GameHudBootstrap.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

template <typename InitializeEcsT>
void InitializeGameplayRuntime(AppRuntime& App,
                               UI::Hud& Hud,
                               Game::Client::ClientRuntime& Client,
                               InitializeEcsT&& InitializeEcs)
{
    InitializeHudRuntime(Hud, Client);
    InitializeEcs();
    SyncHudFromPlayerResources(Hud);
    App.Initialized = true;
}

} // namespace Game::Client::GameLoop
