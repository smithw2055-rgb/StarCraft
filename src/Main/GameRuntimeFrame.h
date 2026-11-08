#pragma once

#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"
#include "Main/GameLoop/GameSaveState.h"

// === Consolidated from src/Main/GameLoop/HudResourceSync.h ===
#include "Game/Systems/HarvestSystem.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void ResetPlayerResourceState()
{
    for (int PlayerId = 0; PlayerId < 8; ++PlayerId)
    {
        auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerId);
        Resources.Minerals = 500;
        Resources.VespeneGas = 0;
        Resources.SupplyUsed = 0;
        Resources.SupplyMax = 0;
    }
}

inline UI::HudResourceState CaptureHudResourceState(const UI::Hud& Hud)
{
    return Hud.GetResourceState();
}

inline void RestorePlayerResourcesFromHudState(UI::Hud& Hud, const UI::HudResourceState& State, int PlayerId = 0)
{
    auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerId);
    Resources.Minerals = State.MineralCount;
    Resources.VespeneGas = State.GasCount;
    Resources.SupplyUsed = State.SupplyUsed;
    Resources.SupplyMax = State.SupplyMax;
    Hud.SetResourceState(State);
}

inline void SyncHudFromPlayerResources(UI::Hud& Hud, int PlayerId = 0)
{
    auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerId);
    UI::HudResourceState HudResources;
    HudResources.MineralCount = Resources.Minerals;
    HudResources.GasCount = Resources.VespeneGas;
    HudResources.SupplyUsed = Resources.SupplyUsed;
    HudResources.SupplyMax = Resources.SupplyMax;
    Hud.SetResourceState(HudResources);
}

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameMatchOutcomeRuntime.h ===
#include "Main/GameSession.h"
#include "Game/Components.h"
#include "Game/Systems/FogOfWarSystem.h"

#include <array>
#include <cstdint>
#include <string>

namespace Game::Client::GameLoop
{

inline void UpdateMeleeMatchOutcome(GameSession& Session, float Dt)
{
    if (!Session.IsEcsInitialized() ||
        !Session.IsMeleeVictoryCheckEnabled() ||
        Session.IsMatchOver())
    {
        return;
    }

    Session.AdvanceMatchElapsed(Dt);

    auto& EM = Session.Entities();
    std::array<int, 8> BuildingCounts{};
    std::array<int, 8> UnitCounts{};

    for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                      Game::Components::PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Eid);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
        if (!Unit || !Player || Unit->HitPoints <= 0 ||
            Player->PlayerID < 0 || Player->PlayerID >= 8)
        {
            continue;
        }
        ++UnitCounts[(size_t)Player->PlayerID];
        if (Game::Systems::IsMeleeStructureEntity(EM, Eid))
            ++BuildingCounts[(size_t)Player->PlayerID];
    }

    for (int P = 0; P < 8; ++P)
    {
        if (UnitCounts[(size_t)P] > 0 || BuildingCounts[(size_t)P] > 0)
            Session.ParticipatingPlayers[(size_t)P] = true;
    }

    if (Session.GetMatchElapsedSeconds() < 5.0f)
        return;

    const int LocalPlayerID = Session.GetLocalPlayerID();
    const auto* Fog = Session.GetFogSystem();

    if (!Session.ParticipatingPlayers[(size_t)LocalPlayerID])
        return;

    int ActiveOpponents = 0;
    int EliminatedOpponents = 0;

    for (int P = 0; P < 8; ++P)
    {
        if (P == LocalPlayerID || !Session.ParticipatingPlayers[(size_t)P])
            continue;
        if (Fog && Fog->AreAllied(LocalPlayerID, P))
            continue;

        ++ActiveOpponents;
        if (BuildingCounts[(size_t)P] == 0)
            ++EliminatedOpponents;
    }

    const bool LocalEliminated = BuildingCounts[(size_t)LocalPlayerID] == 0;

    if (LocalEliminated)
    {
        if (ActiveOpponents > 0 && EliminatedOpponents == ActiveOpponents)
        {
            Session.SetMatchOutcome(MatchOutcome::Draw);
            Core::Utils::Logger::Info(
                "MatchOutcome", "Melee match draw at simulated_seconds=" +
                std::to_string((int)Session.GetMatchElapsedSeconds()));
        }
        else
        {
            Session.SetMatchOutcome(MatchOutcome::Defeat);
            Core::Utils::Logger::Info(
                "MatchOutcome", "Melee match defeat: local player " +
                std::to_string(LocalPlayerID) + " eliminated at simulated_seconds=" +
                std::to_string((int)Session.GetMatchElapsedSeconds()));
        }
    }
    else if (ActiveOpponents > 0 && EliminatedOpponents == ActiveOpponents)
    {
        Session.SetMatchOutcome(MatchOutcome::Victory);
        Core::Utils::Logger::Info(
            "MatchOutcome", "Melee match victory: local player " +
            std::to_string(LocalPlayerID) + " victorious at simulated_seconds=" +
            std::to_string((int)Session.GetMatchElapsedSeconds()));
    }
}

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameSaveRuntime.h ===
#include <cstddef>
#include <cstdio>
#include <string>

#include "AppRuntime.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Core/Utils/Logger.h"
#include "Main/GameLoop/GameSaveState.h"
#include "Main/GameMenuPresentation.h"
#include "GameSession.h"

namespace Game::Client::GameLoop
{

inline GameSaveSnapshot CaptureGameSaveSnapshot(const AppRuntime& App,
                                                const GameMenuRuntime& Menu,
                                                const UI::Hud& Hud)
{
    GameSaveSnapshot Snapshot;
    Snapshot.MapPath = App.MapPath;
    Snapshot.Paused = Menu.IsPaused();
    Snapshot.Resources = CaptureHudResourceState(Hud);
    return Snapshot;
}

inline GameSaveSnapshot CaptureAuthoritativeGameSaveSnapshot(
    const AppRuntime& App,
    const GameMenuRuntime& Menu,
    const UI::Hud& Hud,
    const GameSession& Session)
{
    GameSaveSnapshot Snapshot = CaptureGameSaveSnapshot(App, Menu, Hud);
    if (Session.IsEcsInitialized())
    {
        Snapshot.HasWorldSnapshot = true;
        Snapshot.SimulationTick = Session.SimulationTick;
        Snapshot.WorldSnapshot = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(
            Session.EcsSysMgr.GetEntityManager(),
            static_cast<uint32_t>(Session.SimulationTick));
    }
    return Snapshot;
}

inline void ApplyGameSaveSnapshot(const GameSaveSnapshot& Snapshot,
                                  GameMenuRuntime& Menu,
                                  UI::Hud& Hud)
{
    RestorePlayerResourcesFromHudState(Hud, Snapshot.Resources);
    Menu.RestorePaused(Snapshot.Paused);
}

inline void ApplyAuthoritativeGameSaveSnapshot(
    const GameSaveSnapshot& Snapshot,
    GameMenuRuntime& Menu,
    UI::Hud& Hud,
    GameSession& Session)
{
    if (Snapshot.HasWorldSnapshot && Session.IsEcsInitialized())
    {
        Session.Entities().ActivateWorld();
        Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(
            Session.Entities(), Snapshot.WorldSnapshot);
        Session.SimulationTick = Snapshot.SimulationTick;
        Session.KinematicsStream.ExtractFromECS(Session.Entities());
        if (Snapshot.WorldSnapshot.HasEconomyResource)
        {
            SyncHudFromPlayerResources(Hud);
            Menu.RestorePaused(Snapshot.Paused);
            return;
        }
    }
    ApplyGameSaveSnapshot(Snapshot, Menu, Hud);
}

inline bool SaveGameSnapshotToFile(const std::string& FilePath, const GameSaveSnapshot& Snapshot)
{
    FILE* File = fopen(FilePath.c_str(), "wb");
    if (!File)
        return false;

    const bool Written = WriteGameSave(File, Snapshot);
    fclose(File);
    return Written;
}

inline bool LoadGameSnapshotFromFile(const std::string& FilePath, GameSaveSnapshot& Snapshot)
{
    FILE* File = fopen(FilePath.c_str(), "rb");
    if (!File)
        return false;

    const bool Loaded = ReadGameSave(File, Snapshot);
    fclose(File);
    return Loaded;
}

inline bool SaveCurrentGameToFile(const std::string& FilePath,
                                  const AppRuntime& App,
                                  const GameMenuRuntime& Menu,
                                  const UI::Hud& Hud)
{
    const GameSaveSnapshot Snapshot = CaptureGameSaveSnapshot(App, Menu, Hud);
    if (!SaveGameSnapshotToFile(FilePath, Snapshot))
        return false;

    Core::Utils::Logger::Info("GameController", "Game saved to " + FilePath);
    return true;
}

inline bool SaveCurrentGameToFile(const std::string& FilePath,
                                  const AppRuntime& App,
                                  const GameMenuRuntime& Menu,
                                  const UI::Hud& Hud,
                                  const GameSession& Session)
{
    const GameSaveSnapshot Snapshot = CaptureAuthoritativeGameSaveSnapshot(App, Menu, Hud, Session);
    if (!SaveGameSnapshotToFile(FilePath, Snapshot))
        return false;

    Core::Utils::Logger::Info("GameController", "Game saved to " + FilePath);
    return true;
}

inline bool LoadCurrentGameFromFile(const std::string& FilePath,
                                    const AppRuntime& App,
                                    GameMenuRuntime& Menu,
                                    UI::Hud& Hud)
{
    GameSaveSnapshot Snapshot = CaptureGameSaveSnapshot(App, Menu, Hud);
    if (!LoadGameSnapshotFromFile(FilePath, Snapshot))
        return false;

    ApplyGameSaveSnapshot(Snapshot, Menu, Hud);
    Core::Utils::Logger::Info("GameController", "Game loaded from " + FilePath);
    return true;
}

inline bool LoadCurrentGameFromFile(const std::string& FilePath,
                                    const AppRuntime& App,
                                    GameMenuRuntime& Menu,
                                    UI::Hud& Hud,
                                    GameSession& Session)
{
    GameSaveSnapshot Snapshot = CaptureGameSaveSnapshot(App, Menu, Hud);
    if (!LoadGameSnapshotFromFile(FilePath, Snapshot))
        return false;

    ApplyAuthoritativeGameSaveSnapshot(Snapshot, Menu, Hud, Session);
    Core::Utils::Logger::Info("GameController", "Game loaded from " + FilePath);
    return true;
}

inline std::string GetDefaultQuickSavePath()
{
    return "artifacts\\quicksave.scsave";
}

inline bool SaveSessionReplayToFile(GameSession& Session, const std::string& FilePath)
{
    size_t BytesWritten = 0;
    if (!Session.SaveReplayToFile(FilePath, &BytesWritten))
        return false;

    Core::Utils::Logger::Info("GameController",
        "Replay saved to " + FilePath + " (" + std::to_string(BytesWritten) + " bytes)");
    return true;
}

inline bool LoadSessionReplayFromFile(GameSession& Session, const std::string& FilePath)
{
    std::string GameName;
    if (!Session.LoadReplayFromFile(FilePath, &GameName))
    {
        Core::Utils::Logger::Warning("GameController", "Failed to load replay: " + FilePath);
        return false;
    }

    Core::Utils::Logger::Info("GameController",
        "Loaded replay: " + GameName +
        " (" + std::to_string(Session.GetLastReplayActionCount()) + " actions)");
    return true;
}

} // namespace Game::Client::GameLoop

#include "Main/GameGuiPresentation.h"
// === Consolidated from src/Main/GameInputRuntime.h ===


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
#include "Core/Input/InputActionMap.h"
#include "Core/Platform/Platform.h"
#include "Core/Utils/InputDebug.h"
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
#include "Main/GameCommandDispatchRuntime.h"
#include "Main/GameRuntimeState.h"
#include "Main/GameMenuPresentation.h"
#include "Main/GameSession.h"
#include "Main/GameGuiPresentation.h"
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
    if (Core::Input::InputActionMap::Instance().IsActionTriggered("System.ToggleGui") && Gui.MainGui.IsActive())
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
        else if (Session.IsEcsInitialized() && Core::Input::InputActionMap::Instance().IsActionTriggered("Command.AttackMoveModifier"))
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
        else if (!ConsumedLeftClick && Session.IsEcsInitialized() && Core::Input::InputActionMap::Instance().IsActionTriggered("Command.AttackMoveModifier"))
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
            const auto& ActionMap = Core::Input::InputActionMap::Instance();
            KeyboardCommandShortcut Shortcut = KeyboardCommandShortcut::None;
            if (ActionMap.IsActionTriggered("Command.Stop"))
                Shortcut = KeyboardCommandShortcut::Stop;
            else if (ActionMap.IsActionTriggered("Command.HoldPosition"))
                Shortcut = KeyboardCommandShortcut::Hold;
            else if (ActionMap.IsActionTriggered("Command.Patrol"))
                Shortcut = KeyboardCommandShortcut::Patrol;
            else if (ActionMap.IsActionTriggered("Command.Follow"))
                Shortcut = KeyboardCommandShortcut::Follow;

            ExecuteKeyboardCommandShortcut(EM, EcsIds, Hud, Shortcut);
        }
    }

    if (Core::Input::InputActionMap::Instance().IsActionTriggered("Debug.SpawnAuditFormation"))
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

#include "Main/GameRuntimeState.h"
// === Consolidated from src/Main/GameScenes.h ===
#include <memory>
#include <string_view>

#include "Core/Scene/SceneManager.h"
#include "Main/AppRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameRuntimeState.h"
// === Consolidated from src/Main/GameSimulationRuntime.h ===
#include "Client/ClientRuntime.h"
// === Consolidated from src/Main/GameNetworkActionRuntime.h ===
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Game/Components.h"
#include "Game/Networking/LockstepSync.h"
#include "Game/Systems/CommandSystem.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop
{

inline bool HostSessionNetworkGame(GameSession& Session,
                                   const std::string& BindIP,
                                   int Port,
                                   int MaxClients)
{
    return Session.HostNetworkGame(BindIP, Port, MaxClients);
}

inline bool JoinSessionNetworkGame(GameSession& Session,
                                   const std::string& HostIP,
                                   int Port)
{
    return Session.JoinNetworkGame(HostIP, Port);
}

inline void StartSessionNetworkGame(GameSession& Session)
{
    Session.StartNetworkGameIfEnabled();
}

inline void RecordSessionLocalFrameAction(GameSession& Session,
                                          uint64_t FrameCount,
                                          uint8_t PlayerID,
                                          uint8_t ActionType,
                                          uint16_t P1,
                                          uint16_t P2,
                                          uint16_t P3,
                                          uint8_t P4)
{
    Session.RecordLocalFrameAction((uint32_t)FrameCount, PlayerID, ActionType, P1, P2, P3, P4);
}

inline float ReadNetworkActionFloat(const std::vector<uint8_t>& Data, size_t Offset)
{
    float Value = 0.0f;
    if (Offset + sizeof(Value) <= Data.size())
        std::memcpy(&Value, Data.data() + Offset, sizeof(Value));
    return Value;
}

inline void ExecuteNetworkAction(GameSession& Session, const Game::Networking::SyncAction& Action)
{
    if (Action.Data.empty())
        return;

    auto& EM = Session.Entities();
    const uint8_t CmdType = Action.Data[0];

    auto CollectControllableUnits = [&]() -> std::vector<int> {
        std::vector<int> Units;
        EM.ForEachEntityWith<Game::Components::PlayerComponent, Game::Components::UnitComponent>(
            [&](Core::ECS::EntityID Id) {
                const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Id);
                const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Id);
                if (Player && Player->PlayerID == Action.PlayerID && Unit && Unit->HitPoints > 0)
                {
                    if (!EM.GetComponent<Game::Components::BuildingComponent>(Id) &&
                        !EM.GetComponent<Game::Components::ResourceComponent>(Id) &&
                        EM.GetComponent<Game::Components::CommandComponent>(Id))
                    {
                        Units.push_back((int)Id);
                    }
                }
            });
        return Units;
    };

    switch (CmdType)
    {
    case 0:
    case 1:
    {
        float X = 0.0f;
        float Y = 0.0f;
        std::vector<int> Units;

        if (CmdType == 0 && Action.Data.size() >= 9)
        {
            X = ReadNetworkActionFloat(Action.Data, 1);
            Y = ReadNetworkActionFloat(Action.Data, 5);
            if (Action.Data.size() > 9)
            {
                for (size_t i = 9; i + sizeof(int) <= Action.Data.size(); i += sizeof(int))
                {
                    int Id = 0;
                    std::memcpy(&Id, Action.Data.data() + i, sizeof(Id));
                    if (EM.IsAlive(Id))
                        Units.push_back(Id);
                }
            }
        }
        else if (Action.Data.size() >= 5)
        {
            const uint16_t P1 = (uint16_t)Action.Data[1] | ((uint16_t)Action.Data[2] << 8);
            const uint16_t P2 = (uint16_t)Action.Data[3] | ((uint16_t)Action.Data[4] << 8);
            X = (float)P1;
            Y = (float)P2;
        }

        if (Units.empty())
            Units = CollectControllableUnits();

        if (!Units.empty())
            Game::Systems::CommandSystem::IssueMove(EM, Units, {X, Y});
        break;
    }
    default:
        break;
    }
}

} // namespace Game::Client::GameLoop

#include "Main/GameSession.h"
#include "UI/Hud.h"


namespace Game::Client::GameLoop
{

inline void UpdateGameplaySimulationRuntime(
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    UI::Hud& Hud,
    float Dt)
{
    if (!Session.IsEcsInitialized())
        return;

    Session.ProcessNetworkFrame([&Session](const Game::Networking::SyncAction& Action) {
        ExecuteNetworkAction(Session, Action);
    });

    // Staged Dual-Phase Execution (Unity PlayerLoop / Godot SceneTree physics vs process split):
    // 1. Advance deterministic simulation phases (Input -> NetworkLockstep -> FixedSimulation -> TransformPropagation)
    //    and populate Session.KinematicsStream SoA buffers + optional RollbackStateManager ring buffer.
    Session.UpdateSimulationStep(Dt);

    // 2. Advance presentation & render extraction phases (Presentation -> RenderExtract)
    Session.UpdatePresentationFrame(Dt);

    Audio.UpdateForClientView(Client, Dt);

    if (Session.IsReplayRecording())
    {
        // Record dummy action per frame to keep the replay stream alive.
        // Real actions are recorded via RecordGameAction().
    }

    Session.Bwapi().OnFrame();

    SyncHudFromPlayerResources(Hud, Session.GetLocalPlayerID());

    Client.PruneSelection(Session.Entities());

    Audio.UpdateClientWorldEffects(Client, Session.Entities(), Dt);
}

inline void UpdateCampaignAndSimulationRuntime(
    CampaignRuntime& Campaign,
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    UI::Hud& Hud,
    float Dt)
{
    if (Campaign.IsActive())
    {
        Campaign.Controller().Update(Session.Entities(), Dt);
        Campaign.RecordVictoryProgressIfNeeded();

        const auto State = Campaign.Controller().GetState();
        if (State == Game::Campaign::CampaignState::Victory)
            Session.SetMatchOutcome(MatchOutcome::Victory);
        else if (State == Game::Campaign::CampaignState::Defeat)
            Session.SetMatchOutcome(MatchOutcome::Defeat);
        else if (State == Game::Campaign::CampaignState::Draw)
            Session.SetMatchOutcome(MatchOutcome::Draw);
    }
    else
    {
        UpdateMeleeMatchOutcome(Session, Dt);
    }

    // Scenario-triggered pauses freeze the simulation but keep
    // CampaignController ticking so Wait/Unpause/transmission triggers can resume.
    if (!Campaign.IsScenarioPaused())
        UpdateGameplaySimulationRuntime(Session, Audio, Client, Hud, Dt);
}

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameUiPresentation.h ===


#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
// === Consolidated from src/Main/GameCampaignOverlay.h ===
#include "Client/ClientRuntime.h"
#include "Main/CampaignPresentation.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void RenderActiveCampaignOverlay(CampaignRuntime& Campaign,
                                        UI::Hud& Hud,
                                        Game::Client::ClientRuntime& Client,
                                        int ViewportWidth,
                                        int ViewportHeight)
{
    if (!Campaign.IsActive())
        return;

    RenderCampaignOverlay(
        Campaign.Controller(),
        Hud,
        Client.GetDataAssets(),
        Client.GetArchiveSource(),
        Client.GetUIAssets(),
        ViewportWidth,
        ViewportHeight);
}

inline bool IsAiObserverMatchActive()
{
    const char* Value = std::getenv("STARCRAFT_AI_OBSERVER_MATCH");
    return Value && Value[0] != '\0' && Value[0] != '0';
}

inline void RenderActiveCampaignOverlay(CampaignRuntime& Campaign,
                                        UI::Hud& Hud,
                                        Game::Client::ClientRuntime& Client,
                                        GameSession& Session,
                                        int ViewportWidth,
                                        int ViewportHeight)
{
    if (IsAiObserverMatchActive())
    {
        if (Session.IsMatchOver())
        {
            RenderMatchGameOverOverlay(
                Session.GetMatchOutcome(),
                ViewportWidth,
                ViewportHeight,
                Session.GetMatchElapsedSeconds());
        }
        return;
    }

    if (Campaign.IsActive())
    {
        RenderCampaignOverlay(
            Campaign.Controller(),
            Hud,
            Client.GetDataAssets(),
            Client.GetArchiveSource(),
            Client.GetUIAssets(),
            ViewportWidth,
            ViewportHeight);
    }
    else if (Session.IsMatchOver())
    {
        RenderMatchGameOverOverlay(
            Session.GetMatchOutcome(),
            ViewportWidth,
            ViewportHeight,
            Session.GetMatchElapsedSeconds());
    }
}

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameHudPresentation.h ===
#include "Client/ClientRuntime.h"
#include "Client/WorldEffects.h"
#include "Game/Components.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Main/GameLoop/CursorManager.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline uint32_t PackHudColor(Core::Color Color)
{
    return (static_cast<uint32_t>(Color.A) << 24) |
        (static_cast<uint32_t>(Color.R) << 16) |
        (static_cast<uint32_t>(Color.G) << 8) |
        static_cast<uint32_t>(Color.B);
}

inline Core::Color MinimapRelationshipColor(
    const Game::Components::PlayerComponent* Player,
    int LocalPlayerID,
    const Game::Systems::FogOfWarSystem* Fog)
{
    if (!Player || Player->PlayerID < 0 || Player->PlayerID >= 8)
        return Core::Color(190, 190, 150, 255);
    if (Player->PlayerID == LocalPlayerID)
        return Core::Color(80, 200, 255, 255);
    if (Player->IsRescuePassive || (Fog && Fog->AreAllied(LocalPlayerID, Player->PlayerID)))
        return Core::Color(80, 220, 120, 255);
    return Core::Color(235, 64, 64, 255);
}

inline void SyncMinimapDotsToHud(UI::Hud& Hud,
                                 Core::ECS::EntityManager& Entities,
                                 int LocalPlayerID,
                                 const Game::Systems::FogOfWarSystem* Fog)
{
    std::vector<UI::HudMinimapDot> Dots;
    Entities.ForEachEntityWith<Game::Components::TransformComponent,
                               Game::Components::RenderComponent,
                               Game::Components::UnitComponent>([&](int EntityId)
    {
        auto* Transform = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Render = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
        if (!Transform || !Render || !Unit || !Render->Visible || Unit->HitPoints <= 0)
            return;

        const auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityId);
        const int PlayerID = Player ? Player->PlayerID : 11;
        const bool IsMineral = (Unit->ChkUnitID >= 176 && Unit->ChkUnitID <= 178);
        const bool IsGeyser = (Unit->ChkUnitID == 188);
        const bool IsResource = IsMineral || IsGeyser;
        const auto* Build = Entities.GetComponent<Game::Components::BuildingComponent>(EntityId);
        const bool IsBuilding = (Build != nullptr && Build->IsBuilding) || IsBuildingChkId(Unit->ChkUnitID);

        if (Fog)
        {
            const int TileX = (int)(Transform->Position.X / 32.0f);
            const int TileY = (int)(Transform->Position.Y / 32.0f);

            if (IsResource)
            {
                if (!Fog->IsExplored(TileX, TileY, LocalPlayerID))
                    return;
            }
            else if (PlayerID == LocalPlayerID || Fog->AreAllied(LocalPlayerID, PlayerID))
            {
                // Self or Allied units always visible
            }
            else
            {
                // Enemy
                if (IsBuilding)
                {
                    if (!Fog->IsExplored(TileX, TileY, LocalPlayerID))
                        return;
                }
                else
                {
                    // Enemy mobile unit: visible only in active sight
                    if (!Fog->IsVisible(TileX, TileY, LocalPlayerID))
                        return;
                }
            }
        }

        Core::Color DotColor;
        uint8_t DotW = 2;
        uint8_t DotH = 2;

        if (IsMineral)
        {
            DotColor = Core::Color(0, 228, 252, 255); // Cyan
            DotW = 2;
            DotH = 2;
        }
        else if (IsGeyser)
        {
            DotColor = Core::Color(0, 228, 0, 255); // Green
            DotW = 3;
            DotH = 3;
        }
        else
        {
            if (Hud.GetMinimapUseAllianceColors())
            {
                if (PlayerID == LocalPlayerID || (Fog && Fog->AreAllied(LocalPlayerID, PlayerID)))
                    DotColor = Core::Color(80, 220, 120, 255);
                else
                    DotColor = Core::Color(235, 64, 64, 255);
            }
            else
            {
                DotColor = MinimapRelationshipColor(Player, LocalPlayerID, Fog);
            }

            if (IsBuilding)
            {
                // Town halls (Command Center, Nexus, Hatchery/Lair/Hive) get 5x5 footprint
                const bool IsTownHall = (Unit->ChkUnitID == 106 || Unit->ChkUnitID == 154 ||
                                         Unit->ChkUnitID == 131 || Unit->ChkUnitID == 132 || Unit->ChkUnitID == 133);
                DotW = IsTownHall ? 5 : 4;
                DotH = IsTownHall ? 5 : 4;
            }
        }

        Dots.push_back({Transform->Position, PackHudColor(DotColor), DotW, DotH});
    });
    Hud.SetMinimapDots(Dots);
}

inline void SyncCampaignPingsToHud(UI::Hud& Hud,
                                   Game::Client::ClientRuntime& Client,
                                   int MapWidthTiles,
                                   int MapHeightTiles)
{
    Hud.SetMinimapMapSize(MapWidthTiles, MapHeightTiles);

    std::vector<UI::HudMinimapPing> CampaignPings;
    const auto& ClientPings = Client.GetCampaignPings();
    CampaignPings.reserve(ClientPings.size());
    for (const auto& Ping : ClientPings)
        CampaignPings.push_back({Ping.Position, Ping.TimeRemaining});
    Hud.SetCampaignPings(CampaignPings);
}

inline void RenderNativeHudFrame(UI::Hud& Hud,
                                 GameSession& Session,
                                 Game::Client::ClientRuntime& Client,
                                 int ViewportWidth,
                                 int ViewportHeight)
{
    float CamX = 0.0f;
    float CamY = 0.0f;
    float CamW = 0.0f;
    float CamH = 0.0f;
    Client.GetVisibleWorldRect(CamX, CamY, CamW, CamH);

    Hud.SetMatchElapsedSeconds(Session.GetMatchElapsedSeconds());
    Hud.SetMinimapViewportRect(CamX, CamY, CamW, CamH);
    Hud.UpdateMinimapTerrain(
        &Session.GetEcsMap(),
        &Session.GetMapData(),
        Session.GetFogSystem(),
        Session.GetLocalPlayerID());

    Hud.UpdateContext(Session.Entities(), Client.GetSelectedIds());
    SyncCampaignPingsToHud(Hud, Client, Session.GetMapData().Width, Session.GetMapData().Height);
    SyncMinimapDotsToHud(
        Hud, Session.Entities(), Session.GetLocalPlayerID(), Session.GetFogSystem());
    Hud.Draw(Session.Entities(),
             Client.GetSelectedIds(),
             Session.GetMapData().Width,
             Session.GetMapData().Height,
             ViewportWidth,
             ViewportHeight);
}

} // namespace Game::Client::GameLoop

#include "Main/GameMenuPresentation.h"
#include "Main/GameSession.h"
#include "Render/Draw2D.h"
#include "UI/Hud.h"

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


namespace Game::Client::GameLoop
{

inline Core::Scene::SceneId ToSceneId(GameState State)
{
    return State == GameState::InGame
        ? Core::Scene::SceneId::Gameplay
        : Core::Scene::SceneId::MainMenu;
}

inline GameState ToGameState(Core::Scene::SceneId Id)
{
    return Id == Core::Scene::SceneId::Gameplay
        ? GameState::InGame
        : GameState::MainMenu;
}

class GlueMenuScene final : public Core::Scene::IScene
{
public:
    explicit GlueMenuScene(GameRuntimeState& Runtime)
        : mRuntime(Runtime)
    {
    }

    Core::Scene::SceneId GetId() const override { return Core::Scene::SceneId::MainMenu; }
    std::string_view GetName() const override { return "GlueMenuScene"; }

    bool OnPreUpdate(float /*DeltaSeconds*/) override
    {
        UpdateGuiPresentationForState(mRuntime.App.Gui, GameState::MainMenu);
        return true; // Main menu does not advance gameplay 24Hz ticks
    }

    void OnRender(float /*InterpolationAlpha*/) override
    {
        RenderGlueMenuPresentationLayer(mRuntime.App.Gui);
    }

    void OnEvent(const void* Event) override
    {
        HandleGuiPresentationEventForState(mRuntime.App.Gui, GameState::MainMenu, Event);
    }

private:
    GameRuntimeState& mRuntime;
};

class GameplayScene final : public Core::Scene::IScene
{
public:
    explicit GameplayScene(GameRuntimeState& Runtime)
        : mRuntime(Runtime)
    {
    }

    Core::Scene::SceneId GetId() const override { return Core::Scene::SceneId::Gameplay; }
    std::string_view GetName() const override { return "GameplayScene"; }

    void OnEnter() override
    {
        mRuntime.Session.Game.Entities().ActivateWorld();
    }

    void OnFixedUpdate(float FixedDeltaSeconds) override
    {
        mRuntime.Session.Game.SaveTransformTickState();
        UpdateCampaignAndSimulationRuntime(
            mRuntime.Session.Campaign,
            mRuntime.Session.Game,
            mRuntime.App.Audio,
            mRuntime.Client.Runtime,
            mRuntime.Client.Hud,
            FixedDeltaSeconds);
    }

    void OnPostUpdate(float /*DeltaSeconds*/, int FixedTicksExecuted) override
    {
        if (FixedTicksExecuted > 0 && mRuntime.Session.Game.IsMapLoaded())
        {
            mRuntime.Session.Game.SyncCreepPresentation();
        }
    }

    void OnRender(float InterpolationAlpha) override
    {
        RenderGameplayWorldFrame(mRuntime.Client.Runtime, mRuntime.Session.Game, InterpolationAlpha);
        RenderGameplayUiFrame(
            mRuntime.Client.Runtime,
            mRuntime.Session.Game,
            mRuntime.Session.Campaign,
            mRuntime.App.Menu,
            mRuntime.Client.Hud,
#if ENABLE_GUI_UI
            !(mRuntime.App.Gui.UseGui && mRuntime.App.Gui.MainGui.IsActive())
#else
            true
#endif
        );
        RenderGuiPresentationLayer(mRuntime.App.Gui);
        mRuntime.App.Runtime.UpdateFrameStats();
    }

    void OnEvent(const void* Event) override
    {
        HandleGuiPresentationEventForState(mRuntime.App.Gui, GameState::InGame, Event);
    }

private:
    GameRuntimeState& mRuntime;
};

inline Core::Scene::SceneManager& EnsureRuntimeScenesRegistered(GameRuntimeState& Runtime)
{
    auto& Scenes = Runtime.App.Runtime.Scenes;
    if (!Scenes.HasScene(Core::Scene::SceneId::MainMenu))
    {
        Scenes.ConfigureFixedStep(1.0 / 24.0, 2);
        Scenes.RegisterScene(std::make_unique<GlueMenuScene>(Runtime));
        Scenes.RegisterScene(std::make_unique<GameplayScene>(Runtime));
    }

    const Core::Scene::SceneId DesiredScene = ToSceneId(Runtime.App.Runtime.State);
    if (Scenes.GetActiveSceneId() != DesiredScene)
    {
        Scenes.SwitchToImmediate(DesiredScene);
    }
    return Scenes;
}

} // namespace Game::Client::GameLoop


#include "Render/Draw2D.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

namespace Game::Client::GameLoop
{

template <typename SaveGameT,
          typename LoadGameT,
          typename RefreshGuiT,
          typename RecordGameActionT,
          typename RestartGameT,
          typename ReturnToMainMenuT>
inline void UpdateGameRuntimeFrame(
    GameRuntimeState& Runtime,
    float Dt,
    SaveGameT&& SaveGame,
    LoadGameT&& LoadGame,
    RefreshGuiT&& RefreshGui,
    RecordGameActionT&& RecordGameAction,
    RestartGameT&& RestartGame,
    ReturnToMainMenuT&& ReturnToMainMenu)
{
    Core::Platform::SetDeltaTime(Dt);
    Core::Platform::ProcessInput();

    if (Runtime.InteractionDemo.Enabled && !Runtime.InteractionDemo.FrameProbeLogged)
    {
        Runtime.InteractionDemo.FrameProbeLogged = true;
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "frame probe initialized=" +
                std::to_string(Runtime.App.Runtime.Initialized ? 1 : 0) +
                " state=" +
                std::to_string(Runtime.App.Runtime.State == GameState::InGame ? 1 : 0) +
                " ecs=" +
                std::to_string(Runtime.Session.Game.IsEcsInitialized() ? 1 : 0) +
                " map=" +
                std::to_string(Runtime.Session.Game.IsMapLoaded() ? 1 : 0) +
                " menuOpen=" +
                std::to_string(Runtime.App.Menu.IsOpen() ? 1 : 0) +
                " menuPaused=" +
                std::to_string(Runtime.App.Menu.IsPaused() ? 1 : 0));
    }

    if (!Runtime.App.Runtime.Initialized)
        return;

    auto& Scenes = EnsureRuntimeScenesRegistered(Runtime);

    if (UpdateGuiPresentationForState(Runtime.App.Gui, Runtime.App.Runtime.State))
        return;

    if (UpdateActiveGameplayInputRuntime(
            Runtime.Client.Runtime,
            Runtime.Session.Game,
            Runtime.App.Gui,
            Runtime.App.Menu,
            Runtime.Client.Hud,
            Dt,
            GetDefaultQuickSavePath(),
            std::forward<SaveGameT>(SaveGame),
            std::forward<LoadGameT>(LoadGame),
            [&RefreshGui]() { RefreshGui(); },
            [&RecordGameAction](uint8_t PlayerID, uint8_t ActionType, uint16_t P1, uint16_t P2) {
                RecordGameAction(PlayerID, ActionType, P1, P2);
            },
            std::forward<RestartGameT>(RestartGame),
            std::forward<ReturnToMainMenuT>(ReturnToMainMenu)))
    {
        return;
    }

    float SimulationScale = RunInteractionDemoIfRequested(
        Runtime.InteractionDemo,
        Runtime.Session.Game,
        Runtime.Client.Runtime,
        Runtime.Client.Hud,
        Dt);
    bool HasObserverSpeedOverride = false;
    if (const char* ObserverSpeed = std::getenv("STARCRAFT_AI_OBSERVER_SPEED"))
    {
        const float RequestedScale = std::strtof(ObserverSpeed, nullptr);
        if (RequestedScale >= 1.0f && RequestedScale <= 16.0f)
        {
            SimulationScale = RequestedScale;
            HasObserverSpeedOverride = true;
        }
        static bool SpeedLogged = false;
        if (!SpeedLogged)
        {
            SpeedLogged = true;
            Core::Utils::Logger::Info(
                "AiObserverMatch", "simulation_speed=" +
                std::to_string(SimulationScale) + "x");
        }
    }

    const float SimulationInputDt = HasObserverSpeedOverride
        ? std::min(Dt, 1.0f / 15.0f)
        : Dt;
    const float SimulationDt = SimulationInputDt * SimulationScale;

    RunInteractionSmokeIfRequested(
        Runtime.InteractionSmoke,
        Runtime.Session.Game,
        Runtime.Session.Campaign,
        Runtime.Client.Runtime,
        Runtime.App.Gui,
        Runtime.Client.Hud);

    const int kMaximumSimulationTicksPerHostFrame = HasObserverSpeedOverride
        ? std::min(32, (int)std::ceil(SimulationScale * 2.0f))
        : 2;

    // Synchronize SceneManager fixed-step state with GameSession accumulator/tick counters
    // and step the active GameplayScene deterministically at 24Hz.
    Scenes.SetAccumulatorSeconds(Runtime.Session.Game.SimulationAccumulatorSeconds);
    Scenes.SetFixedTickCount(Runtime.Session.Game.SimulationTick);
    Scenes.StepFrame(SimulationDt, 1.0f, kMaximumSimulationTicksPerHostFrame);
    Runtime.Session.Game.SimulationAccumulatorSeconds = Scenes.GetAccumulatorSeconds();
    Runtime.Session.Game.SimulationTick = Scenes.GetFixedTickCount();

    RefreshGui();
}

inline void RenderGameRuntimeFrame(GameRuntimeState& Runtime)
{
    if (!Runtime.App.Runtime.Initialized)
    {
        Render::BeginFrame();
        Render::EndFrame();
        return;
    }

    auto& Scenes = EnsureRuntimeScenesRegistered(Runtime);
    Scenes.SetAccumulatorSeconds(Runtime.Session.Game.SimulationAccumulatorSeconds);
    Scenes.RenderActiveScene();
}

inline void HandleGameRuntimeFrameEvent(GameRuntimeState& Runtime, const void* Event)
{
    Core::Platform::HandleEvent(Event);
    auto& Scenes = EnsureRuntimeScenesRegistered(Runtime);
    Scenes.DispatchEvent(Event);
}

} // namespace Game::Client::GameLoop

