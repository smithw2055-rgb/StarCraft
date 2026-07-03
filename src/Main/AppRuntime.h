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
    void ResolveMapPath();
    void UpdateFrameStats();

    bool Initialized = false;
    int FrameCount = 0;
    float FpsTimer = 0.0f;
    int FpsFrames = 0;
    int FpsValue = 0;
    std::string InstallPath;
    std::string MapPath;
    std::string MapPathOverride;
    std::string AssetRoot;
    GameState State = GameState::MainMenu;
};

} // namespace Game::Client::GameLoop
