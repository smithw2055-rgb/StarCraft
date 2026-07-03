#pragma once

#include <string>
#include <utility>

#include "Client/MapLoader.h"
#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"
#include "Main/AppRuntime.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop
{

template <typename TryLoadMapT, typename ConfigureSyntheticMapT>
void ResolveAndLoadStartupMap(AppRuntime& App,
                              TryLoadMapT&& TryLoadMap,
                              ConfigureSyntheticMapT&& ConfigureSyntheticMap)
{
    App.ResolveMapPath();
    if (App.MapPath.empty())
    {
        Core::Utils::Logger::Warning("GameMain", "No default map found; using synthetic skirmish map");
        ConfigureSyntheticMap();
        return;
    }

    if (!TryLoadMap())
    {
        Core::Utils::Logger::Warning("GameMain", "Falling back to synthetic skirmish map");
        ConfigureSyntheticMap();
    }
}

inline void InitializeClientWorldViewFromSession(ClientRuntime& Client, GameSession& Session)
{
    const auto Camera = Session.GetInitialCameraPosition();
    const std::string TilesetName = Session.GetTilesetNameOrDefault();
    Client.InitializeWorldView(
        Session.GetMapData(),
        TilesetName,
        Camera.first,
        Camera.second,
        Core::Platform::GetWindowWidth(),
        Core::Platform::GetWindowHeight());
    Session.MarkMapLoaded();
}

inline bool LoadRealMapIntoSession(AppRuntime& App, GameSession& Session, ClientRuntime& Client)
{
    Game::Client::MapLoader::Result Result = Game::Client::MapLoader::Load(App.MapPath);
    if (!Result.Loaded)
    {
        Core::Utils::Logger::Warning("GameMain", "Map load failed: " + Result.ErrorMessage);
        return false;
    }

    Session.ApplyLoadedMap(Result);

    if (!Result.RawChkData.empty())
    {
        if (Session.ParseLoadedScriptData(Result.RawChkData))
        {
            Core::Utils::Logger::Info("GameController",
                "Parsed real map CHK script: map=" + App.MapPath +
                " chk=" + Result.ChkPath +
                " strings=" + std::to_string(Session.GetScriptData().Strings.size()) +
                " locations=" + std::to_string(Session.GetScriptData().Locations.size()) +
                " triggers=" + std::to_string(Session.GetScriptData().Triggers.size()));
        }
        else
        {
            Core::Utils::Logger::Warning("GameController", "Failed to parse real map CHK script data");
        }
    }

    InitializeClientWorldViewFromSession(Client, Session);
    return true;
}

inline void ConfigureSyntheticSkirmishMapIntoSession(GameSession& Session, ClientRuntime& Client)
{
    Session.ConfigureSyntheticSkirmishMap();
    InitializeClientWorldViewFromSession(Client, Session);
}

} // namespace Game::Client::GameLoop
