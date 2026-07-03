#include "GameController.h"

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

} // namespace Game::Client::GameLoop













