#include "Main/AppRuntime.h"

#include <cstdlib>
#include <iostream>
#include <utility>

#include "Client/MapLoader.h"
#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"
#include "Resource/GameDetector.h"

namespace Game::Client::GameLoop
{

bool AppRuntime::DetectGameInstall()
{
    Resource::GameInstallInfo Info = Resource::GameDetector::Detect();
    if (!Info.IsValid())
    {
        Core::Utils::Logger::Error("GameMain", "No StarCraft install found.");
        return false;
    }

    InstallPath = Info.InstallPath;
    Core::Utils::Logger::Info("GameMain", "Install: " + InstallPath +
        (Info.Type == Resource::ArchiveType::Casc ? " (CASC)" : " (MPQ)"));
    return true;
}

void AppRuntime::SetMapPathOverride(std::string MapPath)
{
    MapPathOverride = std::move(MapPath);
}

void AppRuntime::ResolveMapPath()
{
    if (!MapPathOverride.empty())
    {
        MapPath = MapPathOverride;
        Core::Utils::Logger::Info("GameMain", "Using map override: " + MapPath);
    }
    else if (const char* EnvMap = std::getenv("STARCRAFT_MAP_PATH"))
    {
        MapPath = EnvMap;
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
