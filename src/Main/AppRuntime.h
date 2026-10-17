#pragma once

#include <string>

namespace Game::Client::GameLoop
{

enum class GameState
{
    MainMenu,
    InGame
};

struct AppRuntime
{
    bool DetectGameInstall();
    void SetMapPathOverride(std::string MapPath);
    void SetCampaignRuntimeEnabled(bool Enabled);
    void ResolveMapPath();
    void UpdateFrameStats();

    bool Initialized = false;
    int FrameCount = 0;
    float FpsTimer = 0.0f;
    int FpsFrames = 0;
    int FpsValue = 0;
    float MaxFrameTime = 0.0f;
    std::string InstallPath;
    std::string MapPath;
    std::string MapPathOverride;
    std::string AssetRoot;
    bool CampaignRuntimeEnabled = true;
    GameState State = GameState::MainMenu;
};

} // namespace Game::Client::GameLoop
