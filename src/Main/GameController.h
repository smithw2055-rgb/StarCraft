#pragma once

#include <cstdint>
#include <string>

#include "Main/GameRuntimeState.h"

namespace Game::Client::GameLoop
{

class GameController
{
public:
    GameController() = default;

    bool Initialize(int Width, int Height);
    void Update(float Dt, int Width, int Height);
    void Render(int Width, int Height);
    void OnEvent(const void* Event);
    void Shutdown();
    void StartGame();
    void SetMapPathOverride(std::string MapPath);
    void SetCampaignRuntimeEnabled(bool Enabled);
    void SetDirectStartEnabled(bool Enabled);
    void SetInteractionDemoEnabled(bool Enabled);
    void SetInitialCameraOverride(float WorldX, float WorldY);
    void SetInitialGuiHudEnabled(bool Enabled);

    // Public accessors for platform callbacks to reach game state
    bool IsInitialized() const { return mRuntime.App.Runtime.Initialized; }
    int GetViewportWidth() const { return mRuntime.Client.Runtime.GetViewportWidth(); }
    int GetViewportHeight() const { return mRuntime.Client.Runtime.GetViewportHeight(); }

    // Networking
    bool HostGame(const std::string& BindIP, int Port, int MaxClients = 7);
    bool JoinGame(const std::string& HostIP, int Port);
    void StartNetworkGame();

    // Replay
    bool SaveReplay(const std::string& FilePath);
    bool LoadReplay(const std::string& FilePath);
    void RecordGameAction(uint8_t PlayerID, uint8_t ActionType,
                          uint16_t P1 = 0, uint16_t P2 = 0,
                          uint16_t P3 = 0, uint8_t P4 = 0);

    // Save/Load game state
    bool SaveGame(const std::string& FilePath);
    bool LoadGame(const std::string& FilePath);

private:
    void InitGameSystems();

    GameRuntimeState mRuntime;
    bool mHasInitialCameraOverride = false;
    float mInitialCameraX = 0.0f;
    float mInitialCameraY = 0.0f;
    bool mInitialGuiHudEnabled = false;
    bool mDirectStartEnabled = false;

    // Glue menu
};

} // namespace Game::Client::GameLoop
