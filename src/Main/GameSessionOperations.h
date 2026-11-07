#pragma once

#include <cstdint>
#include <string>

#include "Main/AppRuntime.h"
#include "Main/GameMenuRuntime.h"
#include "Main/GameNetworkActionRuntime.h"
#include "Main/GameSaveRuntime.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop::GameSessionOperations
{

inline bool HostGame(GameSession& Session,
                     const std::string& BindIP,
                     int Port,
                     int MaxClients)
{
    return HostSessionNetworkGame(Session, BindIP, Port, MaxClients);
}

inline bool JoinGame(GameSession& Session,
                     const std::string& HostIP,
                     int Port)
{
    return JoinSessionNetworkGame(Session, HostIP, Port);
}

inline void StartNetworkGame(GameSession& Session)
{
    StartSessionNetworkGame(Session);
}

inline void RecordGameAction(GameSession& Session,
                             uint64_t FrameCount,
                             uint8_t PlayerID,
                             uint8_t ActionType,
                             uint16_t P1,
                             uint16_t P2,
                             uint16_t P3,
                             uint8_t P4)
{
    RecordSessionLocalFrameAction(Session, FrameCount, PlayerID, ActionType, P1, P2, P3, P4);
}

inline bool SaveGame(const std::string& FilePath,
                     const AppRuntime& App,
                     const GameMenuRuntime& Menu,
                     const UI::Hud& Hud)
{
    return SaveCurrentGameToFile(FilePath, App, Menu, Hud);
}

inline bool SaveGame(const std::string& FilePath,
                     const AppRuntime& App,
                     const GameMenuRuntime& Menu,
                     const UI::Hud& Hud,
                     const GameSession& Session)
{
    return SaveCurrentGameToFile(FilePath, App, Menu, Hud, Session);
}

inline bool LoadGame(const std::string& FilePath,
                     const AppRuntime& App,
                     GameMenuRuntime& Menu,
                     UI::Hud& Hud)
{
    return LoadCurrentGameFromFile(FilePath, App, Menu, Hud);
}

inline bool LoadGame(const std::string& FilePath,
                     const AppRuntime& App,
                     GameMenuRuntime& Menu,
                     UI::Hud& Hud,
                     GameSession& Session)
{
    return LoadCurrentGameFromFile(FilePath, App, Menu, Hud, Session);
}

inline bool SaveReplay(GameSession& Session, const std::string& FilePath)
{
    return SaveSessionReplayToFile(Session, FilePath);
}

inline bool LoadReplay(GameSession& Session, const std::string& FilePath)
{
    return LoadSessionReplayFromFile(Session, FilePath);
}

} // namespace Game::Client::GameLoop::GameSessionOperations
