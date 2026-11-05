#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

#include "Client/ClientRuntime.h"
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "Core/Platform/Platform.h"
#include "Core/Utils/InputDebug.h"
#include "Main/GameClientViewRuntime.h"
#include "Main/GameCommandDispatchRuntime.h"
#include "Main/GameEntitySpawnRuntime.h"
#include "Main/GameMenuPresentation.h"
#include "Main/GameSession.h"
#include "Main/GuiRuntime.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void LogGameplayInputSnapshot(const char* Stage,
                                     int ViewportWidth,
                                     int ViewportHeight,
                                     int MouseX,
                                     int MouseY,
                                     bool InGui,
                                     bool HasPendingTarget,
                                     bool LeftPressed,
                                     bool RightReleased)
{
    if (!Core::Utils::InputDebugEnabled())
        return;
    if (!LeftPressed && !RightReleased)
        return;

    std::ostringstream Stream;
    Stream << "stage=" << Stage
           << " mouse=" << MouseX << "," << MouseY
           << " viewport=" << ViewportWidth << "x" << ViewportHeight
           << " framebuffer=" << Core::Platform::GetFramebufferWidth() << "x" << Core::Platform::GetFramebufferHeight()
           << " inGui=" << (InGui ? 1 : 0)
           << " pendingTarget=" << (HasPendingTarget ? 1 : 0)
           << " leftPressed=" << (LeftPressed ? 1 : 0)
           << " rightReleased=" << (RightReleased ? 1 : 0);
    Core::Utils::InputDebugLog("InputGameplay", Stream.str());
}

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
            const auto& Defs = Hud.GetButtonDefs();
            if (Defs[8].Kind == UI::ActionKind::Back && Defs[8].Enabled)
            {
                Hud.OnButtonInvoked(8);
            }
            else
            {
                Menu.ToggleOpenAndPauseWhenOpened();
            }
        }
    }

    const ScreenRectI HudMenuRect = ResolveHudMenuButtonRect(ViewportWidth, ViewportHeight);
    if (Core::Platform::IsMouseInGameContentArea() &&
        Core::Platform::IsMouseButtonPressed(0) &&
        HudMenuRect.Contains(MouseX, MouseY))
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
    if (!Core::Platform::IsMouseInGameContentArea())
        BlockSelection = true;
    if (Core::Platform::IsMouseButtonDown(0))
    {
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
    }
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

// Shift is the SC1 waypoint-queue modifier. Kept in the presentation layer so
// the Game simulation stays input-agnostic.
inline bool IsShiftHeldForCommandQueue()
{
    return Core::Platform::IsShiftDown();
}

template <typename RecordGameActionT>
inline bool ExecuteSmartClickAtWorldPosition(
    ClientRuntime& Client,
    GameSession& Session,
    const Core::Math::Vec2& Target,
    RecordGameActionT&& RecordGameAction,
    bool Queued = false)
{
    if (!Session.IsEcsInitialized())
        return false;

    auto& EM = Session.Entities();
    auto EcsIds = Client.CollectSelectedAliveIds(EM);
    if (EcsIds.empty())
        return false;

    auto ClickTarget = Client.ResolveClickTargetContext(EM, EcsIds, Target);
    ExecuteSmartClickCommand(EM, EcsIds, ClickTarget, Target, Queued);
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
    const bool RightReleased = Core::Platform::IsMouseButtonReleased(1);
    const bool InGameContent = Core::Platform::IsMouseInGameContentArea();
    static bool sIsDraggingMinimap = false;

    if (!Core::Platform::IsMouseButtonDown(0))
    {
        sIsDraggingMinimap = false;
    }
    else if (sIsDraggingMinimap && !InGui && InGameContent)
    {
        float DragWX = 0.0f, DragWY = 0.0f;
        if (Hud.CheckMinimapClick(
                MouseX,
                MouseY,
                ViewportWidth,
                ViewportHeight,
                Session.GetMapData().Width,
                Session.GetMapData().Height,
                DragWX,
                DragWY))
        {
            Client.CenterCamera(DragWX, DragWY);
            Client.CancelPointerGesture();
        }
    }

    if (Core::Platform::IsMouseButtonPressed(0) && !InGui && InGameContent)
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
            if (Core::Utils::InputDebugEnabled())
            {
                std::ostringstream Stream;
                Stream << "button=left branch=minimap mouse=" << MouseX << "," << MouseY
                       << " world=" << WX << "," << WY;
                Core::Utils::InputDebugLog("InputGameplayClick", Stream.str());
            }
            sIsDraggingMinimap = true;
            Client.CenterCamera(WX, WY);
            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }

        const int CmdIdx = Hud.CheckCommandPanelClick(MouseX, MouseY, ViewportWidth, ViewportHeight);
        const int QueueSlotIdx = Hud.CheckQueueSlotClick(MouseX, MouseY);
        if (CmdIdx >= 0 && Session.IsEcsInitialized())
        {
            if (Core::Utils::InputDebugEnabled())
            {
                std::ostringstream Stream;
                Stream << "button=left branch=command_panel mouse=" << MouseX << "," << MouseY
                       << " cmd=" << CmdIdx;
                Core::Utils::InputDebugLog("InputGameplayClick", Stream.str());
            }
            auto& EM = Session.Entities();
            auto EcsIds = Client.CollectSelectedAliveIds(EM);
            ExecuteHudCommandIndex(EM, EcsIds, Hud, CmdIdx);
            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }
        else if (QueueSlotIdx >= 0 && Session.IsEcsInitialized())
        {
            if (Core::Utils::InputDebugEnabled())
            {
                std::ostringstream Stream;
                Stream << "button=left branch=queue_slot mouse=" << MouseX << "," << MouseY
                       << " slot=" << QueueSlotIdx;
                Core::Utils::InputDebugLog("InputGameplayClick", Stream.str());
            }
            auto& EM = Session.Entities();
            auto EcsIds = Client.CollectSelectedAliveIds(EM);
            if (!EcsIds.empty())
            {
                if (Game::Systems::ProductionSystem::CancelTrain(EM, EcsIds[0], QueueSlotIdx))
                {
                    Hud.UpdateContext(EM, EcsIds);
                }
            }
            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }
        else if (const int MinimapBtnIdx = Hud.CheckMinimapButtonClick(MouseX, MouseY, ViewportWidth, ViewportHeight); MinimapBtnIdx >= 0)
        {
            if (MinimapBtnIdx == 0)
                const_cast<UI::Hud&>(Hud).ToggleMinimapTerrain();
            else if (MinimapBtnIdx == 1)
                const_cast<UI::Hud&>(Hud).ToggleMinimapAllianceColors();
            else if (MinimapBtnIdx == 2)
                const_cast<UI::Hud&>(Hud).ToggleMinimapBeacons();

            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }
        else if (int HitEntityId = -1; (Hud.CheckMultiSelectSlotClick(MouseX, MouseY, HitEntityId) >= 0) && Session.IsEcsInitialized() && HitEntityId >= 0)
        {
            const bool ShiftHeld = Core::Platform::IsShiftDown();
            if (ShiftHeld)
            {
                auto Current = Client.GetSelectedIds();
                std::vector<int> Filtered;
                for (int Id : Current)
                {
                    if (Id != HitEntityId)
                        Filtered.push_back(Id);
                }
                Client.SetSelectedIdsForDemo(Filtered);
            }
            else
            {
                Client.SetSelectedIdsForDemo({ HitEntityId });
            }

            auto& EM = Session.Entities();
            auto EcsIds = Client.CollectSelectedAliveIds(EM);
            const_cast<UI::Hud&>(Hud).UpdateContext(EM, EcsIds);
            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }
        else if (Hud.IsPointInHudBand(MouseX, MouseY, ViewportWidth, ViewportHeight))
        {
            if (Core::Utils::InputDebugEnabled())
            {
                std::ostringstream Stream;
                Stream << "button=left branch=hud_band mouse=" << MouseX << "," << MouseY;
                Core::Utils::InputDebugLog("InputGameplayClick", Stream.str());
            }
            Client.CancelPointerGesture();
            ConsumedLeftClick = true;
        }

        if (!ConsumedLeftClick && Session.IsEcsInitialized() && Hud.HasPendingTarget())
        {
            Core::Math::Vec2 Target = Client.ScreenToWorld((float)MouseX, (float)MouseY);
            if (Core::Utils::InputDebugEnabled())
            {
                std::ostringstream Stream;
                Stream << "button=left branch=pending_target mouse=" << MouseX << "," << MouseY
                       << " world=" << Target.X << "," << Target.Y;
                Core::Utils::InputDebugLog("InputGameplayClick", Stream.str());
            }
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
            if (Core::Utils::InputDebugEnabled())
            {
                std::ostringstream Stream;
                Stream << "button=left branch=attack_move mouse=" << MouseX << "," << MouseY
                       << " world=" << Target.X << "," << Target.Y;
                Core::Utils::InputDebugLog("InputGameplayClick", Stream.str());
            }
            auto& EM = Session.Entities();
            auto EcsIds = Client.CollectSelectedAliveIds(EM);
            ExecuteAttackMoveCommand(EM, EcsIds, Target, IsShiftHeldForCommandQueue());
        }
    }

    if (Session.IsEcsInitialized() && RightReleased && !InGui && InGameContent)
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
            if (Core::Utils::InputDebugEnabled())
            {
                std::ostringstream Stream;
                Stream << "button=right branch=minimap mouse=" << MouseX << "," << MouseY
                       << " world=" << MinimapWX << "," << MinimapWY;
                Core::Utils::InputDebugLog("InputGameplayClick", Stream.str());
            }
            ExecuteSmartClickAtWorldPosition(
                Client,
                Session,
                {MinimapWX, MinimapWY},
                RecordGameAction,
                IsShiftHeldForCommandQueue());
        }
        else if (InHudBand)
        {
            const int QueueSlotIdx = Hud.CheckQueueSlotClick(MouseX, MouseY);
            if (QueueSlotIdx >= 0 && Session.IsEcsInitialized())
            {
                auto& EM = Session.Entities();
                auto EcsIds = Client.CollectSelectedAliveIds(EM);
                if (!EcsIds.empty())
                {
                    if (Game::Systems::ProductionSystem::CancelTrain(EM, EcsIds[0], QueueSlotIdx))
                    {
                        Hud.UpdateContext(EM, EcsIds);
                    }
                }
            }
            Client.CancelPointerGesture();
        }
        else
        {
            Core::Math::Vec2 Target = Client.ScreenToWorld((float)MouseX, (float)MouseY);
            if (Core::Utils::InputDebugEnabled())
            {
                std::ostringstream Stream;
                Stream << "button=right branch=world mouse=" << MouseX << "," << MouseY
                       << " world=" << Target.X << "," << Target.Y;
                Core::Utils::InputDebugLog("InputGameplayClick", Stream.str());
            }
            ExecuteSmartClickAtWorldPosition(
                Client,
                Session,
                Target,
                RecordGameAction,
                IsShiftHeldForCommandQueue());
        }
    }
}

inline void HandleClientKeyboardCommands(
    ClientRuntime& Client,
    GameSession& Session,
    UI::Hud& Hud)
{
    if (!Session.IsEcsInitialized())
        return;

    Client.HandleControlGroupHotkeys(Session.Entities());

    auto& EM = Session.Entities();
    auto EcsIds = Client.CollectSelectedAliveIds(EM);
    if (!EcsIds.empty())
    {
        bool HotkeyDispatched = false;
        for (int Key = 'A'; Key <= 'Z'; ++Key)
        {
            if (Core::Platform::IsKeyPressed(Key))
            {
                if (TryDispatchCommandHotkey(EM, EcsIds, Hud, Key))
                {
                    HotkeyDispatched = true;
                    break;
                }
            }
        }

        if (!HotkeyDispatched)
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

            ExecuteKeyboardCommandShortcut(EM, EcsIds, Hud, Shortcut);
        }
    }

    if (Core::Platform::IsKeyPressed(Core::Platform::KeyCode::F6))
        GameEntitySpawnRuntime::SpawnTerranAuditFormationOnce(Session, Client.GetCameraPosition());
}

template <typename RestartGameT, typename ReturnToMainMenuT>
inline bool HandleMatchGameOverInputGate(
    GameSession& Session,
    int ViewportWidth,
    int ViewportHeight,
    int MouseX,
    int MouseY,
    RestartGameT&& RestartGame,
    ReturnToMainMenuT&& ReturnToMainMenu)
{
    if (!Session.IsMatchOver())
        return false;

    const float PanelW = 440.0f;
    const float PanelH = 220.0f;
    const float PanelX = ((float)ViewportWidth - PanelW) * 0.5f;
    const float PanelY = ((float)ViewportHeight - PanelH) * 0.5f;
    const float ButtonW = 180.0f;
    const float ButtonH = 34.0f;
    const float ButtonY = PanelY + PanelH - 52.0f;
    const float RestartX = PanelX + 24.0f;
    const float MenuX = PanelX + PanelW - ButtonW - 24.0f;

    const bool LeftPressed = Core::Platform::IsMouseButtonPressed(0);
    const float FX = (float)MouseX;
    const float FY = (float)MouseY;
    const bool RestartClicked = LeftPressed &&
        FX >= RestartX && FX < RestartX + ButtonW &&
        FY >= ButtonY && FY < ButtonY + ButtonH;
    const bool MenuClicked = LeftPressed &&
        FX >= MenuX && FX < MenuX + ButtonW &&
        FY >= ButtonY && FY < ButtonY + ButtonH;

    if (Core::Platform::IsKeyPressed(Core::Platform::KeyCode::R) || RestartClicked)
    {
        RestartGame();
        return true;
    }
    if (Core::Platform::IsKeyPressed(Core::Platform::KeyCode::Escape) ||
        Core::Platform::IsKeyPressed(Core::Platform::KeyCode::Q) ||
        MenuClicked)
    {
        ReturnToMainMenu();
        return true;
    }
    return true;
}

template <typename SaveGameT,
          typename LoadGameT,
          typename UpdateUiT,
          typename RecordGameActionT,
          typename RestartGameT,
          typename ReturnToMainMenuT>
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
    RecordGameActionT&& RecordGameAction,
    RestartGameT&& RestartGame,
    ReturnToMainMenuT&& ReturnToMainMenu)
{
    const int ViewportWidth = Core::Platform::GetWindowWidth();
    const int ViewportHeight = Core::Platform::GetWindowHeight();
    const int MouseX = (int)Core::Platform::GetMouseX();
    const int MouseY = (int)Core::Platform::GetMouseY();
    const bool LeftPressed = Core::Platform::IsMouseButtonPressed(0);
    const bool RightReleased = Core::Platform::IsMouseButtonReleased(1);

    if (HandleMatchGameOverInputGate(
            Session,
            ViewportWidth,
            ViewportHeight,
            MouseX,
            MouseY,
            std::forward<RestartGameT>(RestartGame),
            std::forward<ReturnToMainMenuT>(ReturnToMainMenu)))
    {
        return true;
    }

    const bool InGui = UpdateClientInputGates(
        Gui,
        Menu,
        Hud,
        ViewportWidth,
        ViewportHeight,
        MouseX,
        MouseY);
    LogGameplayInputSnapshot("pre_menu", ViewportWidth, ViewportHeight, MouseX, MouseY, InGui, Hud.HasPendingTarget(), LeftPressed, RightReleased);

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
    HandleClientKeyboardCommands(Client, Session, Hud);

    return false;
}

} // namespace Game::Client::GameLoop
