#include "Main/GameController.h"

#include "Main/GameEcsSessionBootstrap.h"
#include "Main/GameMapBootstrap.h"
#include "Main/GameResourceBootstrap.h"
#include "Main/GameRuntimeBootstrap.h"
#include "Main/GameStartRuntime.h"

namespace Game::Client::GameLoop
{

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
    if (!mRuntime.App.Runtime.DetectGameInstall())
        return;

    if (!InitializeClientResourcesAndSystemAssetResolvers(mRuntime.Client.Runtime, mRuntime.App.Runtime.InstallPath))
        return;

    ResolveAndLoadStartupMap(
        mRuntime.App.Runtime,
        [this]() { return LoadRealMapIntoSession(mRuntime.App.Runtime, mRuntime.Session.Game, mRuntime.Client.Runtime); },
        [this]() { ConfigureSyntheticSkirmishMapIntoSession(mRuntime.Session.Game, mRuntime.Client.Runtime); });

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
                mRuntime.App.Runtime.MapPath);
        });

    if (mHasInitialCameraOverride)
        mRuntime.Client.Runtime.CenterCamera(mInitialCameraX, mInitialCameraY);
}

} // namespace Game::Client::GameLoop
