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

template <typename TryLoadMapT>
bool ResolveAndLoadStartupMap(AppRuntime& App,
                              TryLoadMapT&& TryLoadMap)
{
    App.ResolveMapPath();
    if (App.MapPath.empty())
    {
        Core::Utils::Logger::Error("GameMain", "No default map found; startup map load aborted");
        return false;
    }

    if (!TryLoadMap())
    {
        Core::Utils::Logger::Error("GameMain", "Startup map load aborted");
        return false;
    }

    return true;
}

inline void InitializeClientWorldViewFromSession(ClientRuntime& Client, GameSession& Session)
{
    Core::Utils::Logger::Info(
        "GameBootstrap",
        "InitializeClientWorldViewFromSession viewport=" +
            std::to_string(Core::Platform::GetWindowWidth()) + "x" +
            std::to_string(Core::Platform::GetWindowHeight()));
    Client.SetLocalPlayerId(Session.GetLocalPlayerID());
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
    Core::Utils::Logger::Info("GameBootstrap", "LoadRealMapIntoSession path=" + App.MapPath);
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
