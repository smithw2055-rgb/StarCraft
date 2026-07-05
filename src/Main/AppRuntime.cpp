#include "Main/AppRuntime.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "Client/MapLoader.h"
#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"

namespace Game::Client::GameLoop
{

namespace
{
    constexpr const char* kDevInstallPath = "C:\\Projects\\StarCraft";
    constexpr const char* kDevAssetRoot = "C:\\Projects\\StarCraft\\assets";

    std::string ResolveMapSelectionPath(const std::string& Path, const std::string& InstallPath)
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

    bool IsCampaignMapPath(const std::string& Path)
    {
        std::string Normalized = Path;
        std::replace(Normalized.begin(), Normalized.end(), '\\', '/');
        std::transform(Normalized.begin(), Normalized.end(), Normalized.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        return Normalized.find("/campaign/") != std::string::npos;
    }
}

bool AppRuntime::DetectGameInstall()
{
    std::error_code Ec;
    if (!std::filesystem::is_directory(kDevAssetRoot, Ec))
    {
        Core::Utils::Logger::Error("GameMain",
            std::string("Hardcoded dev assets directory not found: ") + kDevAssetRoot);
        return false;
    }

    InstallPath = kDevInstallPath;
    AssetRoot = kDevAssetRoot;
    Core::Utils::Logger::Info("GameMain", "Using hardcoded dev assets: " + AssetRoot);
    return true;
}

void AppRuntime::SetMapPathOverride(std::string Path)
{
    MapPathOverride = std::move(Path);
}

void AppRuntime::SetCampaignRuntimeEnabled(bool Enabled)
{
    CampaignRuntimeEnabled = Enabled;
}

void AppRuntime::ResolveMapPath()
{
    if (!MapPathOverride.empty())
    {
        MapPath = ResolveMapSelectionPath(MapPathOverride, InstallPath);
        if (!IsCampaignMapPath(MapPath))
            CampaignRuntimeEnabled = false;
        Core::Utils::Logger::Info("GameMain", "Using map override: " + MapPath);
    }
    else if (const char* EnvMap = std::getenv("STARCRAFT_MAP_PATH"))
    {
        MapPath = ResolveMapSelectionPath(EnvMap, InstallPath);
        Core::Utils::Logger::Info("GameMain", "Using STARCRAFT_MAP_PATH: " + MapPath);
    }
    else
    {
        MapPath = Game::Client::MapLoader::FindDefaultMap(InstallPath);
    }
}

void AppRuntime::UpdateFrameStats()
{
    FrameCount++;
    FpsTimer += Core::Platform::GetDeltaTime();
    FpsFrames++;
    if (FpsTimer >= 1.0f)
    {
        FpsValue = FpsFrames;
        if (const char* LogFps = std::getenv("STARCRAFT_LOG_FPS"))
        {
            if (LogFps[0] == '1')
            {
                std::cout << "fps=" << FpsValue
                          << " win=" << Core::Platform::GetWindowWidth() << "x" << Core::Platform::GetWindowHeight()
                          << " frame=" << FrameCount
                          << " mouse=" << Core::Platform::GetMouseX() << "," << Core::Platform::GetMouseY()
                          << std::endl;
            }
        }
        FpsTimer = 0.0f;
        FpsFrames = 0;
    }
}

} // namespace Game::Client::GameLoop
