#pragma once

#include <vector>

namespace Game::Client::UI
{

struct HudSelectionState
{
    int SelectedCount = 0;
    int LastHovered = -1;
    int SelectedEntityId = -1;
    int SelectedChkUnitId = -1;
    int SelectedTerranDefId = -1;
    std::vector<int> LastSelectedIds;
};

} // namespace Game::Client::UI
