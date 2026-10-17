#pragma once

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameCampaignOverlay.h"
#include "Main/GameHudPresentation.h"
#include "Main/GameMenuPresentation.h"
#include "Main/GameMenuRuntime.h"
#include "Main/GameSession.h"
#include "Render/Draw2D.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void RenderGameplayUiFrame(
    ClientRuntime& Client,
    GameSession& Session,
    CampaignRuntime& Campaign,
    GameMenuRuntime& Menu,
    UI::Hud& Hud,
    bool DrawNativeHud)
{
    const int ViewportWidth = Core::Platform::GetWindowWidth();
    const int ViewportHeight = Core::Platform::GetWindowHeight();

    Render::g_Draw2D.BeginFrameScreen();
    if (DrawNativeHud)
        RenderNativeHudFrame(Hud, Session, Client, ViewportWidth, ViewportHeight);

    RenderHudMenuButton(ViewportWidth, ViewportHeight, Menu.IsOpen());

    Client.RenderSelectionDragOverlay();

    RenderActiveCampaignOverlay(Campaign, Hud, Client, Session, ViewportWidth, ViewportHeight);

    const int MouseX = (int)Core::Platform::GetMouseX();
    const int MouseY = (int)Core::Platform::GetMouseY();

    RenderGameMenuPresentationLayer(
        ViewportWidth,
        ViewportHeight,
        MouseX,
        MouseY,
        Menu,
        Campaign.IsScenarioPaused());

    Core::ECS::EntityManager* EcsEM = Session.IsEcsInitialized() ? &Session.Entities() : nullptr;
    Client.RenderCursor(MouseX, MouseY, Hud, EcsEM, Session.IsEcsInitialized(), Render::g_Draw2D);

    Render::g_Draw2D.EndFrame();
}

} // namespace Game::Client::GameLoop
