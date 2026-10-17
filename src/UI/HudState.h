#pragma once

#include "UI/HudCommandPanelState.h"
#include "UI/HudInfoPanelState.h"
#include "UI/HudMinimapState.h"
#include "UI/HudResourceState.h"
#include "UI/HudSelectionState.h"

namespace Game::Client::UI
{

struct HudState
{
    HudResourceState Resources;
    HudSelectionState Selection;
    HudMinimapState Minimap;
    int ActiveCommand = -1;
    float MatchElapsedSeconds = 0.0f;
    HudCommandPanelState CommandPanel;
    HudInfoPanelState InfoPanel;
};

} // namespace Game::Client::UI
