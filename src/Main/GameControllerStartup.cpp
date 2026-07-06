#include "Main/GameController.h"

#include <cstdint>

#include "Core/Utils/InputDebug.h"
#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Main/GameEcsSessionBootstrap.h"
#include "Main/GameMapBootstrap.h"
#include "Main/GameResourceBootstrap.h"
#include "Main/GameRuntimeBootstrap.h"
#include "Main/GameStartRuntime.h"
#include "UI/HudLayout.h"

namespace Game::Client::GameLoop
{
namespace
{
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
    int Logged = 0;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                           Game::Components::UnitComponent,
                                           Game::Components::PlayerComponent>())
    {
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        if (!Player || !Unit || !Transform || Player->PlayerID != 0)
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
}

void GameController::StartGame()
{
    StartGameFromMainMenu(
        mRuntime.App.Runtime,
        mRuntime.App.Menu,
        mRuntime.Session.Campaign,
        mRuntime.Session.Game,
            [this]() { InitGameSystems(); });
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

void GameController::InitGameSystems()
{
    Core::Utils::Logger::Info("GameBootstrap", "InitGameSystems begin");
    Core::Utils::Logger::Info(
        "GameBootstrap",
        "controller=" + std::to_string((uintptr_t)this) +
            " runtime=" + std::to_string((uintptr_t)&mRuntime) +
            " client_runtime=" + std::to_string((uintptr_t)&mRuntime.Client.Runtime));
    if (!mRuntime.App.Runtime.DetectGameInstall())
        return;
    Core::Utils::Logger::Info("GameBootstrap", "Game install detected");

    if (!InitializeClientResourcesAndSystemAssetResolvers(mRuntime.Client.Runtime, mRuntime.App.Runtime.InstallPath))
        return;
    Core::Utils::Logger::Info("GameBootstrap", "Client resources initialized");

    if (!ResolveAndLoadStartupMap(
        mRuntime.App.Runtime,
        [this]() { return LoadRealMapIntoSession(mRuntime.App.Runtime, mRuntime.Session.Game, mRuntime.Client.Runtime); }))
        return;
    Core::Utils::Logger::Info("GameBootstrap", "Startup map loaded");

    InitializeGameplayRuntime(
        mRuntime.App.Runtime,
        mRuntime.Client.Hud,
        mRuntime.Client.Runtime,
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
    Core::Utils::Logger::Info("GameBootstrap", "Gameplay runtime initialized");
    LogMouseDebugBootstrapSnapshot(mRuntime.Session.Game, mRuntime.Client.Runtime);

    if (mHasInitialCameraOverride)
        mRuntime.Client.Runtime.CenterCamera(mInitialCameraX, mInitialCameraY);
}

} // namespace Game::Client::GameLoop
