#include "UI/Hud.h"
#include "UI/HudLayout.h"

#include "Core/ECS/EntityManager.h"
namespace Game::Client::UI
{


void Hud::Draw(Core::ECS::EntityManager& Entities,
               const std::vector<int>& SelectedIds,
               int MapWidthTiles, int MapHeightTiles,
               int VW, int VH)
{
    const auto& UIAssets = mUIAssets;
    const auto AggIcons = UIAssets.GetHudAggIcons();
    const auto Layout = ResolveHudLayout(VW, VH);

    const bool HasModularConsole = DrawConsoleChrome(Layout, VW);

    DrawCommandPanel(Layout, VW, VH);

    DrawResourceBar(VW, AggIcons);

    mState.Selection.SelectedCount = (int)SelectedIds.size();

    DrawInfoPanel(Entities, Layout, HasModularConsole, AggIcons);

    DrawMinimapOverlay(Entities, Layout, MapWidthTiles, MapHeightTiles);
}

} // namespace Game::Client::UI






