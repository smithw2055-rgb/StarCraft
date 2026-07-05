#pragma once

#include "Client/ClientRuntime.h"
#include "Main/GuiRuntime.h"
#include "Main/AppRuntime.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameAudioRuntime.h"
#include "Main/GameInteractionSmokeRuntime.h"
#include "Main/GameMenuRuntime.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

struct GameAppRuntimeState
{
    AppRuntime Runtime;
    GameAudioRuntime Audio;
    GameMenuRuntime Menu;
    GuiRuntime Gui;
};

struct GameClientRuntimeState
{
    ClientRuntime Runtime;
    UI::Hud Hud;
};

struct GameSessionRuntimeState
{
    GameSession Game;
    CampaignRuntime Campaign;
};

struct GameRuntimeState
{
    GameAppRuntimeState App;
    GameClientRuntimeState Client;
    GameSessionRuntimeState Session;
    GameInteractionSmokeState InteractionSmoke;
    GameInteractionDemoState InteractionDemo;
};

} // namespace Game::Client::GameLoop
