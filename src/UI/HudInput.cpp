#include "UI/Hud.h"

#include <cstddef>

#include "UI/HudLayout.h"
#include "UI/HudMinimapGeometry.h"

namespace Game::Client::UI
{

bool Hud::CheckMinimapClick(int SX, int SY,
                            int VW, int VH,
                            int MW, int MH,
                            float& OutWX, float& OutWY) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    return MinimapScreenToWorld(Layout.MinimapRect, MW, MH, SX, SY, OutWX, OutWY);
}

int Hud::CheckCommandPanelClick(int SX, int SY, int VW, int VH) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    for (std::size_t Index = 0; Index < Layout.CommandRects.size(); ++Index)
    {
        if (Layout.CommandRects[Index].Contains(SX, SY))
            return (int)Index;
    }
    return -1;
}

bool Hud::IsPointInHudBand(int, int SY, int VW, int VH) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    return SY >= Layout.HudTop || SY < kResourceBarHeight;
}

int Hud::GetHudHeight(int VW, int VH) const
{
    return ResolveHudLayout(VW, VH).HudHeight;
}

} // namespace Game::Client::UI
