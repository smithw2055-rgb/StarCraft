#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "Client/MapLoader.h"
#include "Core/Platform/Platform.h"
#include "Core/Scene/SceneManager.h"
#include "Core/Utils/Logger.h"

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
    Core::Scene::SceneManager Scenes;
};

namespace Detail
{
    inline constexpr const char* kDevInstallPath = "C:\\Projects\\StarCraft";

    inline std::string ResolveMapSelectionPath(const std::string& Path, const std::string& InstallPath)
    {
        if (Path.empty())
            return {};

        std::filesystem::path Candidate(Path);
        if (Candidate.is_absolute())
            return Candidate.lexically_normal().string();

        std::error_code Ec;
        if (std::filesystem::exists(Candidate, Ec))
            return Candidate.lexically_normal().string();

        if (!InstallPath.empty())
        {
            std::filesystem::path InstallCandidate =
                (std::filesystem::path(InstallPath) / Candidate).lexically_normal();
            Ec.clear();
            if (std::filesystem::exists(InstallCandidate, Ec))
                return InstallCandidate.string();
        }

        return Path;
    }

    inline bool IsCampaignMapPath(const std::string& Path)
    {
        std::string Normalized = Path;
        std::replace(Normalized.begin(), Normalized.end(), '\\', '/');
        std::transform(Normalized.begin(), Normalized.end(), Normalized.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        return Normalized.find("/campaign/") != std::string::npos;
    }
} // namespace Detail

inline bool AppRuntime::DetectGameInstall()
{
    std::error_code Ec;

    auto TryRoot = [&](const std::string& Root) -> bool
    {
        if (Root.empty())
            return false;

        const std::filesystem::path RootPath(Root);
        const std::filesystem::path AssetsPath = RootPath / "assets";
        if (std::filesystem::is_directory(AssetsPath, Ec))
        {
            InstallPath = RootPath.lexically_normal().string();
            AssetRoot = AssetsPath.lexically_normal().string();
            return true;
        }

        if (std::filesystem::is_directory(RootPath, Ec) &&
            std::filesystem::is_directory(RootPath / "ui", Ec))
        {
            AssetRoot = RootPath.lexically_normal().string();
            InstallPath = RootPath.parent_path().lexically_normal().string();
            return true;
        }
        return false;
    };

    if (const char* EnvAssetRoot = std::getenv("STARCRAFT_ASSET_ROOT"))
    {
        if (TryRoot(EnvAssetRoot))
        {
            Core::Utils::Logger::Info("GameMain", "Using STARCRAFT_ASSET_ROOT: " + AssetRoot);
            return true;
        }
    }

    if (const char* EnvInstall = std::getenv("STARCRAFT_INSTALL_PATH"))
    {
        if (TryRoot(EnvInstall))
        {
            Core::Utils::Logger::Info("GameMain", "Using STARCRAFT_INSTALL_PATH: " + AssetRoot);
            return true;
        }
    }

    const std::filesystem::path Current = std::filesystem::current_path(Ec);
    if (!Ec && TryRoot(Current.string()))
    {
        Core::Utils::Logger::Info("GameMain", "Using working directory assets: " + AssetRoot);
        return true;
    }

    if (TryRoot(Detail::kDevInstallPath))
    {
        Core::Utils::Logger::Info("GameMain", "Using hardcoded dev assets: " + AssetRoot);
        return true;
    }

    Core::Utils::Logger::Error("GameMain",
        "Game assets directory not found. Set STARCRAFT_ASSET_ROOT or STARCRAFT_INSTALL_PATH.");
    return false;
}

inline void AppRuntime::SetMapPathOverride(std::string Path)
{
    MapPathOverride = std::move(Path);
}

inline void AppRuntime::SetCampaignRuntimeEnabled(bool Enabled)
{
    CampaignRuntimeEnabled = Enabled;
}

inline void AppRuntime::ResolveMapPath()
{
    if (!MapPathOverride.empty())
    {
        MapPath = Detail::ResolveMapSelectionPath(MapPathOverride, InstallPath);
        if (!Detail::IsCampaignMapPath(MapPath))
            CampaignRuntimeEnabled = false;
        Core::Utils::Logger::Info("GameMain", "Using map override: " + MapPath);
    }
    else if (const char* EnvMap = std::getenv("STARCRAFT_MAP_PATH"))
    {
        MapPath = Detail::ResolveMapSelectionPath(EnvMap, InstallPath);
        Core::Utils::Logger::Info("GameMain", "Using STARCRAFT_MAP_PATH: " + MapPath);
    }
    else
    {
        MapPath = Game::Client::MapLoader::FindDefaultMap(InstallPath);
    }
}

inline void AppRuntime::UpdateFrameStats()
{
    FrameCount++;
    const float Dt = Core::Platform::GetDeltaTime();
    FpsTimer += Dt;
    FpsFrames++;
    if (Dt > MaxFrameTime)
        MaxFrameTime = Dt;

    if (FpsTimer >= 1.0f)
    {
        FpsValue = FpsFrames;
        if (const char* LogFps = std::getenv("STARCRAFT_LOG_FPS"))
        {
            if (LogFps[0] == '1')
            {
                std::cout << "fps=" << FpsValue
                          << " max_ms=" << (int)(MaxFrameTime * 1000.0f)
                          << " win=" << Core::Platform::GetWindowWidth() << "x" << Core::Platform::GetWindowHeight()
                          << " client=" << Core::Platform::GetClientWidth() << "x" << Core::Platform::GetClientHeight()
                          << " frame=" << FrameCount
                          << " mouse=" << Core::Platform::GetMouseX() << "," << Core::Platform::GetMouseY()
                          << std::endl;
            }
        }
        FpsTimer = 0.0f;
        FpsFrames = 0;
        MaxFrameTime = 0.0f;
    }
}

} // namespace Game::Client::GameLoop
