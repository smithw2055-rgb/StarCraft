#pragma once

#include <cstddef>
#include <cstdio>
#include <string>

#include "AppRuntime.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Core/Utils/Logger.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/GameLoop/GameSaveState.h"
#include "GameMenuRuntime.h"
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
