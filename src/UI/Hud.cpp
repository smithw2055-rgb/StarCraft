#include "UI/Hud.h"
#include "UI/HudLayout.h"

#include <cstdio>

#include "Core/ECS/EntityManager.h"
#include "Render/Color.h"
#include "Render/Draw2D.h"

namespace Game::Client::UI
{


void Hud::Draw(Core::ECS::EntityManager& Entities,
               const std::vector<int>& SelectedIds,
               int MapWidthTiles, int MapHeightTiles,
               int VW, int VH)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    const auto AggIcons = UIAssets.GetHudAggIcons();
    const auto Layout = ResolveHudLayout(VW, VH);

    const bool HasModularConsole = DrawConsoleChrome(Layout, VW);

    DrawCommandPanel(Layout, VW, VH);

    DrawResourceBar(VW, AggIcons);

    char Buf[64];

    mState.Selection.SelectedCount = (int)SelectedIds.size();
    std::snprintf(Buf, sizeof(Buf), "Sel: %d", mState.Selection.SelectedCount);
    D.DrawText(Buf, 8.0f, 20.0f, 1.5f, Render::Color(180, 200, 220, 255));

    DrawInfoPanel(Entities, Layout, HasModularConsole, AggIcons);

    DrawMinimapOverlay(Entities, Layout, MapWidthTiles, MapHeightTiles);
}

} // namespace Game::Client::UI






