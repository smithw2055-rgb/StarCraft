#include "Main/GameController.h"

#include "Main/GameRuntimeFrame.h"

namespace Game::Client::GameLoop
{

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
        });
}

void GameController::Render(int Width, int Height)
{
    (void)Width; (void)Height;
    RenderGameRuntimeFrame(mRuntime);
}

void GameController::OnEvent(const void* Event)
{
    HandleGameRuntimeFrameEvent(mRuntime, Event);
}

} // namespace Game::Client::GameLoop
