#include "Main/GameController.h"
#include "Game/Campaign/CampaignMissionCatalog.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Render/Draw2D.h"
#include "Render/Renderer.h"

#include "Core/Platform/Platform.h"
#include "Core/Utils/InputDebug.h"
#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Main/GameEcsSessionBootstrap.h"
// === Consolidated from src/Main/GameMapBootstrap.h ===
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

#include "Main/GameResourceBootstrap.h"
#include "Main/GameRuntimeFrame.h"
// === Consolidated from src/Main/GameSessionOperations.h ===
#include <cstdint>
#include <string>

#include "Main/AppRuntime.h"
#include "Main/GameMenuRuntime.h"


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

#include "UI/HudLayout.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace Game::Client::GameLoop
{

namespace
{
    bool InitializeClientShellRuntime()
    {
        Core::Platform::Initialize();
        if (!Render::Initialize())
        {
            std::cerr << "Renderer init failed" << std::endl;
            return false;
        }
        if (!Render::g_Draw2D.Init())
        {
            std::cerr << "Draw2D init failed" << std::endl;
            return false;
        }
        return true;
    }

    void ShutdownClientShellRuntime(ClientRuntime& Client, UI::Hud& Hud)
    {
        Hud.Shutdown();
        Render::g_Draw2D.Shutdown();
        Client.ShutdownResources();
        Render::Shutdown();
        Core::Platform::Shutdown();
    }

    template <typename QuitT, typename SetMapPathT, typename StartGameT>
    void HandleGlueMenuAction(
        const std::string& Action,
        CampaignRuntime& Campaign,
        QuitT&& Quit,
        SetMapPathT&& SetMapPath,
        StartGameT&& StartGame)
    {
        if (Action == "exit")
        {
            Quit();
        }
        else if (Game::Campaign::IsCampaignMissionAction(Action))
        {
            const Game::Campaign::CampaignMissionRef Mission = Game::Campaign::ResolveCampaignMission(Action);
            if (Mission.Valid)
            {
                Campaign.SetProgressTarget(Mission.Expansion, Mission.Race, Mission.Mission);
                SetMapPath(Mission.Path);
                StartGame();
            }
        }
        else if (Action == "start_original" || Action == "start_expansion" ||
                 Action == "start_custom_game")
        {
            std::cout << "StarCraft: menu action '" << Action
                      << "' has no campaign map path; using default map resolver" << std::endl;
            Campaign.ClearProgressTarget();
            StartGame();
        }
    }

    template <typename InitGameSystemsT>
    void StartGameFromMainMenu(AppRuntime& App,
                               GameMenuRuntime& Menu,
                               CampaignRuntime& Campaign,
                               GameSession& Session,
                               InitGameSystemsT&& InitGameSystems)
    {
        if (App.State != GameState::MainMenu)
            return;

        std::cout << "StarCraft: Starting game..." << std::endl;
        Menu.CloseAndResume();
        Campaign.SetScenarioPaused(false);

        InitGameSystems();
        if (!Session.IsEcsInitialized())
        {
            std::cerr << "Game init failed!" << std::endl;
            return;
        }

        App.State = GameState::InGame;
        Render::ResetStateCache();
        std::cout << "StarCraft: Game started" << std::endl;
    }

    template <typename InitializeEcsT>
    void InitializeGameplayRuntime(AppRuntime& App,
                                   UI::Hud& Hud,
                                   Game::Client::ClientRuntime& Client,
                                   int LocalPlayerID,
                                   InitializeEcsT&& InitializeEcs)
    {
        Hud.Init(Client.GetUIAssets(), Client.GetSpriteAssets());
        ResetPlayerResourceState();
        InitializeEcs();
        SyncHudFromPlayerResources(Hud, LocalPlayerID);
        App.Initialized = true;
    }

    bool ShouldDevStartInGame(const AppRuntime& App, bool DirectStartEnabled)
    {
        if (DirectStartEnabled)
            return true;
        if (!App.MapPathOverride.empty())
            return true;
        const char* DevStart = std::getenv("STARCRAFT_DEV_START_INGAME");
        return DevStart && std::string(DevStart) == "1";
    }

    void LogControllerViewportState(const char* Stage)
    {
        if (!Core::Utils::InputDebugEnabled())
            return;

        std::ostringstream Stream;
        Stream << "stage=" << Stage
               << " logical=" << Core::Platform::GetWindowWidth() << "x" << Core::Platform::GetWindowHeight()
               << " client=" << Core::Platform::GetClientWidth() << "x" << Core::Platform::GetClientHeight()
               << " framebuffer=" << Core::Platform::GetFramebufferWidth() << "x" << Core::Platform::GetFramebufferHeight();
        Core::Utils::InputDebugLog("InputControllerState", Stream.str());
    }

    void LogMouseDebugBootstrapSnapshot(GameSession& Session, ClientRuntime& Client)
    {
        if (!Core::Utils::InputDebugEnabled() || !Session.IsEcsInitialized())
            return;

        const auto Camera = Client.GetCameraPosition();
        Core::Utils::Logger::Info(
            "InputBootstrap",
            "viewport=" + std::to_string(Client.GetViewportWidth()) + "x" +
                std::to_string(Client.GetViewportHeight()) +
                " camera=" + std::to_string((int)Camera.X) + "," +
                std::to_string((int)Camera.Y));

        const auto Layout = UI::ResolveHudLayout(Client.GetViewportWidth(), Client.GetViewportHeight());
        Core::Utils::Logger::Info(
            "InputBootstrap",
            "minimap_rect=" + std::to_string(Layout.MinimapRect.X) + "," +
                std::to_string(Layout.MinimapRect.Y) + "," +
                std::to_string(Layout.MinimapRect.W) + "," +
                std::to_string(Layout.MinimapRect.H));

        auto& EM = Session.Entities();
        const int LocalPlayerID = Session.GetLocalPlayerID();
        int Logged = 0;
        for (int EntityID : EM.GetEntitiesWithCached<Game::Components::TransformComponent,
                                                     Game::Components::UnitComponent,
                                                     Game::Components::PlayerComponent>())
        {
            auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            if (!Player || !Unit || !Transform || Player->PlayerID != LocalPlayerID)
                continue;

            const auto Screen = Client.WorldToScreen(Transform->Position.X, Transform->Position.Y);
            const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
            const auto Def = DefID >= 0 ? Game::Races::GetUnitDefByID(DefID) : Game::Races::UnitDef();
            const std::string Name = Def.Name.empty() ? ("CHK_" + std::to_string(Unit->ChkUnitID)) : Def.Name;
            Core::Utils::Logger::Info(
                "InputBootstrap",
                "unit id=" + std::to_string(EntityID) +
                    " name=" + Name +
                    " world=" + std::to_string((int)Transform->Position.X) + "," +
                    std::to_string((int)Transform->Position.Y) +
                    " screen=" + std::to_string((int)Screen.X) + "," +
                    std::to_string((int)Screen.Y));
            if (++Logged >= 12)
                break;
        }
    }
} // namespace

// ============================================================================
// Lifecycle & Frame Dispatch
// ============================================================================

bool GameController::Initialize(int Width, int Height)
{
    (void)Width;
    (void)Height;
    std::cout << "StarCraft: init" << std::endl;
    std::cout << "GameController.Initialize this=" << (uintptr_t)this
              << " runtime=" << (uintptr_t)&mRuntime
              << " client_runtime=" << (uintptr_t)&mRuntime.Client.Runtime
              << std::endl;

    if (!InitializeClientShellRuntime())
        return false;

    LogControllerViewportState("post_shell_init");

    mRuntime.Client.Runtime.InitializeWorldEffects(800);
    mRuntime.App.Audio.Initialize();

    LogControllerViewportState("pre_gui_init");
    InitializeGuiRuntime(
        mRuntime.App.Gui,
        mRuntime.App.Runtime.AssetRoot,
        Core::Platform::GetWindowWidth(),
        Core::Platform::GetWindowHeight(),
        [this](const std::string& Action) {
            HandleGlueMenuAction(
                Action,
                mRuntime.Session.Campaign,
                []() { Core::Platform::RequestQuit(); },
                [this](const std::string& MapPath) { SetMapPathOverride(MapPath); },
                [this]() { StartGame(); });
        },
        [this](const std::string& AudioPath) {
            mRuntime.App.Audio.PlayFile(AudioPath, 0.0f, 0.0f, 35, Core::Audio::SoundFlags::PerSound);
        },
        !mDirectStartEnabled);
#if ENABLE_GUI_UI
    if (mInitialGuiHudEnabled && mRuntime.App.Gui.MainGui.IsActive())
        mRuntime.App.Gui.UseGui = true;
#endif

    LogControllerViewportState("post_gui_init");

    mRuntime.App.Runtime.Initialized = true;
    mRuntime.App.Runtime.State = GameState::MainMenu;
    if (ShouldDevStartInGame(mRuntime.App.Runtime, mDirectStartEnabled))
    {
        LogControllerViewportState("pre_start_game");
        if (mDirectStartEnabled)
            std::cout << "StarCraft: direct test map startup entering game" << std::endl;
        StartGame();
    }
    return true;
}

void GameController::Update(float Dt, int Width, int Height)
{
    (void)Width;
    (void)Height;
    UpdateGameRuntimeFrame(
        mRuntime,
        Dt,
        [this](const std::string& Path) { return SaveGame(Path); },
        [this](const std::string& Path) { return LoadGame(Path); },
        [this]() { UpdateGuiPresentationLayer(mRuntime.App.Gui, mRuntime.Client.Hud, mRuntime.Session.Game, mRuntime.Client.Runtime); },
        [this](uint8_t PlayerID, uint8_t ActionType, uint16_t P1, uint16_t P2) {
            RecordGameAction(PlayerID, ActionType, P1, P2);
        },
        [this]() { RestartGame(); },
        [this]() { ReturnToMainMenu(); });
}

void GameController::Render(int Width, int Height)
{
    (void)Width;
    (void)Height;
    RenderGameRuntimeFrame(mRuntime);
}

void GameController::OnEvent(const void* Event)
{
    HandleGameRuntimeFrameEvent(mRuntime, Event);
}

void GameController::Shutdown()
{
    std::cout << "StarCraft: cleanup" << std::endl;
    ShutdownGuiRuntime(mRuntime.App.Gui);
    ShutdownClientShellRuntime(mRuntime.Client.Runtime, mRuntime.Client.Hud);
}

// ============================================================================
// Startup & Session Transitions
// ============================================================================

void GameController::StartGame()
{
    StartGameFromMainMenu(
        mRuntime.App.Runtime,
        mRuntime.App.Menu,
        mRuntime.Session.Campaign,
        mRuntime.Session.Game,
        [this]() { InitGameSystems(); });
}

void GameController::RestartGame()
{
    mRuntime.App.Runtime.State = GameState::MainMenu;
    mRuntime.Client.Runtime.ResetSessionState();
    mRuntime.Session.Game.ResetMatchOutcome();
    if (mRuntime.Session.Campaign.IsActive())
        mRuntime.Session.Campaign.Controller().Reset();
    StartGame();
}

void GameController::ReturnToMainMenu()
{
    mRuntime.App.Runtime.State = GameState::MainMenu;
    mRuntime.Client.Runtime.ResetSessionState();
    mRuntime.Session.Game.ResetMatchOutcome();
    mRuntime.Session.Campaign.Deactivate();
    mRuntime.App.Menu.CloseAndResume();
}

void GameController::InitGameSystems()
{
    Core::Utils::Logger::Info("GameBootstrap", "InitGameSystems begin");
    if (!mRuntime.App.Runtime.DetectGameInstall())
        return;

    if (!InitializeClientResourcesAndSystemAssetResolvers(mRuntime.Client.Runtime, mRuntime.App.Runtime.InstallPath))
        return;
    mRuntime.App.Audio.ConfigureGameSounds(mRuntime.Client.Runtime);

    if (!ResolveAndLoadStartupMap(
        mRuntime.App.Runtime,
        [this]() { return LoadRealMapIntoSession(mRuntime.App.Runtime, mRuntime.Session.Game, mRuntime.Client.Runtime); }))
        return;

    InitializeGameplayRuntime(
        mRuntime.App.Runtime,
        mRuntime.Client.Hud,
        mRuntime.Client.Runtime,
        mRuntime.Session.Game.GetLocalPlayerID(),
        [this]() {
            InitializeLoadedMapGameplayRuntime(
                mRuntime.Session.Game,
                mRuntime.Client.Runtime,
                mRuntime.App.Audio,
                mRuntime.Client.Hud,
                mRuntime.Session.Campaign,
                mRuntime.App.Runtime.MapPath,
                mRuntime.App.Runtime.CampaignRuntimeEnabled,
                mHasInitialCameraOverride);
        });
    LogMouseDebugBootstrapSnapshot(mRuntime.Session.Game, mRuntime.Client.Runtime);

    if (mHasInitialCameraOverride)
        mRuntime.Client.Runtime.CenterCamera(mInitialCameraX, mInitialCameraY);
}

// ============================================================================
// Configuration & Session Operations API
// ============================================================================

void GameController::SetMapPathOverride(std::string MapPath)
{
    mRuntime.App.Runtime.SetMapPathOverride(std::move(MapPath));
}

void GameController::SetCampaignRuntimeEnabled(bool Enabled)
{
    mRuntime.App.Runtime.SetCampaignRuntimeEnabled(Enabled);
}

void GameController::SetDirectStartEnabled(bool Enabled)
{
    mDirectStartEnabled = Enabled;
}

void GameController::SetInteractionDemoEnabled(bool Enabled)
{
    mRuntime.InteractionDemo.Enabled = Enabled;
}

void GameController::SetInitialCameraOverride(float WorldX, float WorldY)
{
    mHasInitialCameraOverride = true;
    mInitialCameraX = WorldX;
    mInitialCameraY = WorldY;
}

void GameController::SetInitialGuiHudEnabled(bool Enabled)
{
    mInitialGuiHudEnabled = Enabled;
}

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
