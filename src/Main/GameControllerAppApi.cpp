#include "GameController.h"

// Keep this translation unit tied to the complete GameController runtime layout.

#include <utility>
  
namespace Game::Client::GameLoop
{

// ================================================================
// Public API
// ================================================================

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

} // namespace Game::Client::GameLoop













