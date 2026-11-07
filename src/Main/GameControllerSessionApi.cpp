#include "Main/GameController.h"

// Keep this translation unit tied to the complete GameController runtime layout.

#include "Main/GameSessionOperations.h"

namespace Game::Client::GameLoop
{

bool GameController::HostGame(const std::string& BindIP, int Port, int MaxClients)
{
    return GameSessionOperations::HostGame(mRuntime.Session.Game, BindIP, Port, MaxClients);
}

bool GameController::JoinGame(const std::string& HostIP, int Port)
{
    return GameSessionOperations::JoinGame(mRuntime.Session.Game, HostIP, Port);
}

void GameController::StartNetworkGame()
{
    GameSessionOperations::StartNetworkGame(mRuntime.Session.Game);
}

void GameController::RecordGameAction(uint8_t PlayerID, uint8_t ActionType,
                                       uint16_t P1, uint16_t P2,
                                       uint16_t P3, uint8_t P4)
{
    GameSessionOperations::RecordGameAction(
        mRuntime.Session.Game,
        mRuntime.App.Runtime.FrameCount,
        PlayerID,
        ActionType,
        P1,
        P2,
        P3,
        P4);
}

bool GameController::SaveGame(const std::string& FilePath)
{
    return GameSessionOperations::SaveGame(
        FilePath,
        mRuntime.App.Runtime,
        mRuntime.App.Menu,
        mRuntime.Client.Hud,
        mRuntime.Session.Game);
}

bool GameController::LoadGame(const std::string& FilePath)
{
    return GameSessionOperations::LoadGame(
        FilePath,
        mRuntime.App.Runtime,
        mRuntime.App.Menu,
        mRuntime.Client.Hud,
        mRuntime.Session.Game);
}

bool GameController::SaveReplay(const std::string& FilePath)
{
    return GameSessionOperations::SaveReplay(mRuntime.Session.Game, FilePath);
}

bool GameController::LoadReplay(const std::string& FilePath)
{
    return GameSessionOperations::LoadReplay(mRuntime.Session.Game, FilePath);
}

} // namespace Game::Client::GameLoop
