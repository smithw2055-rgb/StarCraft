#include "UI/Hud.h"

#include <cstddef>

#include "UI/HudLayout.h"

namespace Game::Client::UI
{

bool Hud::CheckMinimapClick(int SX, int SY,
                            int VW, int VH,
                            int MW, int MH,
                            float& OutWX, float& OutWY) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    const auto& MinimapRect = Layout.MinimapRect;
    if (!MinimapRect.Contains(SX, SY))
        return false;

    const float U = (float)(SX - MinimapRect.X) / (float)MinimapRect.W;
    const float V = (float)(SY - MinimapRect.Y) / (float)MinimapRect.H;
    OutWX = U * (float)(MW * 32);
    OutWY = V * (float)(MH * 32);
    return true;
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
