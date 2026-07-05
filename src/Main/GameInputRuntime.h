#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Main/GameClientViewRuntime.h"
#include "Main/GameCommandDispatchRuntime.h"
#include "Main/GameEntitySpawnRuntime.h"
#include "Main/GameMenuPresentation.h"
#include "Main/GameSession.h"
#include "Main/GuiRuntime.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline bool UpdateClientInputGates(
    GuiRuntime& Gui,
    GameMenuRuntime& Menu,
    UI::Hud& Hud,
    int ViewportWidth,
    int ViewportHeight,
    int MouseX,
    int MouseY)
{
#if ENABLE_GUI_UI
    if (Core::Platform::IsKeyPressed(Core::Platform::KeyCode::F9) && Gui.MainGui.IsActive())
        Gui.UseGui = !Gui.UseGui;

    const bool InGui = Gui.UseGui && Gui.MainGui.IsPointInHud((float)MouseX, (float)MouseY);
#else
    (void)Gui;
    const bool InGui = false;
#endif

    if (Core::Platform::IsKeyPressed(Core::Platform::KeyCode::Escape))
    {
        if (Hud.HasPendingTarget())
        {
            Hud.CancelPendingTarget();
        }
        else
        {
            Menu.ToggleOpenAndPauseWhenOpened();
        }
    }

    const ScreenRectI HudMenuRect = ResolveHudMenuButtonRect(ViewportWidth, ViewportHeight);
    if (Core::Platform::IsMouseButtonPressed(0) && HudMenuRect.Contains(MouseX, MouseY))
    {
        Menu.OpenAndPause();
        Hud.CancelPendingTarget();
    }

    return InGui;
}

inline void UpdateClientSelectionFromUiState(
    ClientRuntime& Client,
    GameSession& Session,
    UI::Hud& Hud,
    int ViewportWidth,
    int ViewportHeight,
    int MouseX,
    int MouseY,
    bool InGui)
{
    bool BlockSelection = false;
    if (Core::Platform::IsMouseButtonPressed(0))
    {
        if (InGui)
            BlockSelection = true;

        float DummyWX, DummyWY;
        if (Hud.CheckMinimapClick(
                MouseX,
                MouseY,
                ViewportWidth,
                ViewportHeight,
                Session.GetMapData().Width,
                Session.GetMapData().Height,
                DummyWX,
                DummyWY))
        {
            BlockSelection = true;
        }
        else if (Hud.CheckCommandPanelClick(MouseX, MouseY, ViewportWidth, ViewportHeight) >= 0)
        {
            BlockSelection = true;
        }
        else if (Hud.IsPointInHudBand(MouseX, MouseY, ViewportWidth, ViewportHeight))
        {
            BlockSelection = true;
        }
        else if (Hud.HasPendingTarget())
        {
            BlockSelection = true;
        }
        else if (Session.IsEcsInitialized() && Core::Platform::IsKeyDown(65))
        {
            BlockSelection = true;
        }
    }

    Client.UpdateSelection(Session.Entities(), BlockSelection);
}

inline bool ConsumeGuiHudCommandClick(
    GuiRuntime& Gui,
    ClientRuntime& Client,
    GameSession& Session,
    UI::Hud& Hud)
{
#if ENABLE_GUI_UI
    if (!Gui.UseGui || !Gui.MainGui.IsActive())
        return false;

    const int CmdIdx = Gui.MainGui.ConsumeClickedCommand();
    if (CmdIdx < 0)
        return false;

    if (Session.IsEcsInitialized())
    {
        auto& EM = Session.Entities();
        auto EcsIds = Client.CollectSelectedAliveIds(EM);
        ExecuteHudCommandIndex(EM, EcsIds, Hud, CmdIdx);
    }
    Client.CancelPointerGesture();
    return true;
#else
    (void)Gui;
    (void)Client;
    (void)Session;
    (void)Hud;
    return false;
#endif
}

inline bool ConsumeGuiHudMinimapClick(
    GuiRuntime& Gui,
    ClientRuntime& Client,
    GameSession& Session,
    int MouseX,
    int MouseY)
{
#if ENABLE_GUI_UI
    if (!Gui.UseGui || !Gui.MainGui.IsActive() || !Core::Platform::IsMouseButtonPressed(0))
        return false;

    float WX = 0.0f;
    float WY = 0.0f;
    if (!Gui.MainGui.CheckMinimapClick(
            static_cast<float>(MouseX),
            static_cast<float>(MouseY),
            Session.GetMapData().Width,
            Session.GetMapData().Height,
            WX,
            WY))
    {
        return false;
    }

    Client.CenterCamera(WX, WY);
    Client.CancelPointerGesture();
    return true;
#else
    (void)Gui;
    (void)Client;
    (void)Session;
    (void)MouseX;
    (void)MouseY;
    return false;
#endif
}

template <typename RecordGameActionT>
inline bool ExecuteSmartClickAtWorldPosition(
    ClientRuntime& Client,
    GameSession& Session,
    const Core::Math::Vec2& Target,
    RecordGameActionT&& RecordGameAction)
{
    if (!Session.IsEcsInitialized())
        return false;

    auto& EM = Session.Entities();
    auto EcsIds = Client.CollectSelectedAliveIds(EM);
    if (EcsIds.empty())
        return false;

    auto ClickTarget = Client.ResolveClickTargetContext(EM, EcsIds, Target);
    ExecuteSmartClickCommand(EM, EcsIds, ClickTarget, Target);
    RecordGameAction(0, 1, (uint16_t)(Target.X), (uint16_t)(Target.Y));
    Client.CancelPointerGesture();
    return true;
}

template <typename RecordGameActionT>
inline bool ConsumeGuiHudMinimapSmartClick(
    GuiRuntime& Gui,
    ClientRuntime& Client,
    GameSession& Session,
    int MouseX,
    int MouseY,
    RecordGameActionT&& RecordGameAction)
{
#if ENABLE_GUI_UI
    if (!Gui.UseGui || !Gui.MainGui.IsActive() || !Core::Platform::IsMouseButtonReleased(1))
        return false;

    float WX = 0.0f;
    float WY = 0.0f;
    if (!Gui.MainGui.CheckMinimapClick(
            static_cast<float>(MouseX),
            static_cast<float>(MouseY),
            Session.GetMapData().Width,
            Session.GetMapData().Height,
            WX,
            WY))
    {
        return false;
    }

    return ExecuteSmartClickAtWorldPosition(
        Client,
        Session,
        {WX, WY},
        RecordGameAction);
#else
    (void)Gui;
    (void)Client;
    (void)Session;
    (void)MouseX;
    (void)MouseY;
    (void)RecordGameAction;
    return false;
#endif
}

template <typename RecordGameActionT>
inline void HandleClientWorldClickCommands(
    ClientRuntime& Client,
    GameSession& Session,
    UI::Hud& Hud,
    int ViewportWidth,
    int ViewportHeight,
    int MouseX,
    int MouseY,
    bool InGui,
    RecordGameActionT&& RecordGameAction)
{
    if (Core::Platform::IsMouseButtonPressed(0) && !InGui)
    {
        bool ConsumedLeftClick = false;
        float WX, WY;

        if (Hud.CheckMinimapClick(
                MouseX,
                MouseY,
                ViewportWidth,
                ViewportHeight,
                Session.GetMapData().Width,
                Session.GetMapData().Height,
                WX,
                WY))
        {
            Client.CenterCamera(WX, WY);
            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }

        const int CmdIdx = Hud.CheckCommandPanelClick(MouseX, MouseY, ViewportWidth, ViewportHeight);
        if (CmdIdx >= 0 && Session.IsEcsInitialized())
        {
            auto& EM = Session.Entities();
            auto EcsIds = Client.CollectSelectedAliveIds(EM);
            ExecuteHudCommandIndex(EM, EcsIds, Hud, CmdIdx);
            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }
        else if (Hud.IsPointInHudBand(MouseX, MouseY, ViewportWidth, ViewportHeight))
        {
            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }

        if (!ConsumedLeftClick && Session.IsEcsInitialized() && Hud.HasPendingTarget())
        {
            Core::Math::Vec2 Target = Client.ScreenToWorld((float)MouseX, (float)MouseY);
            auto& EM = Session.Entities();
            auto EcsIds = Client.CollectSelectedAliveIds(EM);
            bool TargetCommandAccepted = false;
            if (!EcsIds.empty())
            {
                auto ClickTarget = Client.ResolveClickTargetContext(EM, EcsIds, Target);
                TargetCommandAccepted = ExecutePendingTargetCommand(
                    EM,
                    EcsIds,
                    Hud.GetPendingTargetMode(),
                    Hud.GetPendingBuildDefID(),
                    ClickTarget,
                    Target);
            }
            if (TargetCommandAccepted)
                Hud.CancelPendingTarget();
            Client.CancelPointerGesture();
        }
        else if (!ConsumedLeftClick && Session.IsEcsInitialized() && Core::Platform::IsKeyDown(65))
        {
            Core::Math::Vec2 Target = Client.ScreenToWorld((float)MouseX, (float)MouseY);
            auto& EM = Session.Entities();
            auto EcsIds = Client.CollectSelectedAliveIds(EM);
            ExecuteAttackMoveCommand(EM, EcsIds, Target);
        }
    }

    if (Session.IsEcsInitialized() && Core::Platform::IsMouseButtonReleased(1) && !InGui)
    {
        const bool InHudBand = Hud.IsPointInHudBand(MouseX, MouseY, ViewportWidth, ViewportHeight);
        float MinimapWX = 0.0f;
        float MinimapWY = 0.0f;
        if (Hud.HasPendingTarget())
        {
            Hud.CancelPendingTarget();
            Client.CancelPointerGesture();
        }
        else if (Hud.CheckMinimapClick(
                     MouseX,
                     MouseY,
                     ViewportWidth,
                     ViewportHeight,
                     Session.GetMapData().Width,
                     Session.GetMapData().Height,
                     MinimapWX,
                     MinimapWY))
        {
            ExecuteSmartClickAtWorldPosition(
                Client,
                Session,
                {MinimapWX, MinimapWY},
                RecordGameAction);
        }
        else if (InHudBand)
        {
            Client.CancelPointerGesture();
        }
        else
        {
            Core::Math::Vec2 Target = Client.ScreenToWorld((float)MouseX, (float)MouseY);
            ExecuteSmartClickAtWorldPosition(
                Client,
                Session,
                Target,
                RecordGameAction);
        }
    }
}

inline void HandleClientKeyboardCommands(
    ClientRuntime& Client,
    GameSession& Session)
{
    if (!Session.IsEcsInitialized())
        return;

    Client.HandleControlGroupHotkeys(Session.Entities());

    auto& EM = Session.Entities();
    auto EcsIds = Client.CollectSelectedAliveIds(EM);
    if (!EcsIds.empty())
    {
        KeyboardCommandShortcut Shortcut = KeyboardCommandShortcut::None;
        if (Core::Platform::IsKeyPressed(83))
            Shortcut = KeyboardCommandShortcut::Stop;
        else if (Core::Platform::IsKeyPressed(72))
            Shortcut = KeyboardCommandShortcut::Hold;
        else if (Core::Platform::IsKeyPressed(80))
            Shortcut = KeyboardCommandShortcut::Patrol;
        else if (Core::Platform::IsKeyPressed(70))
            Shortcut = KeyboardCommandShortcut::Follow;

        ExecuteKeyboardCommandShortcut(EM, EcsIds, Shortcut);
    }

    if (Core::Platform::IsKeyPressed(Core::Platform::KeyCode::F6))
        GameEntitySpawnRuntime::SpawnTerranAuditFormationOnce(Session, Client.GetCameraPosition());
}

template <typename SaveGameT,
          typename LoadGameT,
          typename UpdateUiT,
          typename RecordGameActionT>
inline bool UpdateActiveGameplayInputRuntime(
    ClientRuntime& Client,
    GameSession& Session,
    GuiRuntime& Gui,
    GameMenuRuntime& Menu,
    UI::Hud& Hud,
    float Dt,
    const std::string& QuickSavePath,
    SaveGameT&& SaveGame,
    LoadGameT&& LoadGame,
    UpdateUiT&& UpdateUi,
    RecordGameActionT&& RecordGameAction)
{
    const int ViewportWidth = Core::Platform::GetWindowWidth();
    const int ViewportHeight = Core::Platform::GetWindowHeight();
    const int MouseX = (int)Core::Platform::GetMouseX();
    const int MouseY = (int)Core::Platform::GetMouseY();

    const bool InGui = UpdateClientInputGates(
        Gui,
        Menu,
        Hud,
        ViewportWidth,
        ViewportHeight,
        MouseX,
        MouseY);

    if (HandleGameMenuPauseUpdateGate(
            Menu,
            MouseX,
            MouseY,
            ViewportWidth,
            ViewportHeight,
            QuickSavePath,
            std::forward<SaveGameT>(SaveGame),
            std::forward<LoadGameT>(LoadGame),
            std::forward<UpdateUiT>(UpdateUi)))
    {
        return true;
    }

    UpdateClientCameraForCurrentViewport(Client, Hud, Dt);
    UpdateClientSelectionFromUiState(
        Client,
        Session,
        Hud,
        ViewportWidth,
        ViewportHeight,
        MouseX,
        MouseY,
        InGui);
    ConsumeGuiHudMinimapClick(Gui, Client, Session, MouseX, MouseY);
    ConsumeGuiHudCommandClick(Gui, Client, Session, Hud);
    ConsumeGuiHudMinimapSmartClick(
        Gui,
        Client,
        Session,
        MouseX,
        MouseY,
        RecordGameAction);
    HandleClientWorldClickCommands(
        Client,
        Session,
        Hud,
        ViewportWidth,
        ViewportHeight,
        MouseX,
        MouseY,
        InGui,
        std::forward<RecordGameActionT>(RecordGameAction));
    HandleClientKeyboardCommands(Client, Session);

    return false;
}

} // namespace Game::Client::GameLoop
