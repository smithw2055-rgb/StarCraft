#include "UI/Hud.h"

#include <cstddef>
#include <sstream>

#include "Core/Utils/InputDebug.h"
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
    const bool Hit = MinimapScreenToWorld(Layout.MinimapRect, MW, MH, SX, SY, OutWX, OutWY);
    if (Core::Utils::InputDebugEnabled())
    {
        std::ostringstream Stream;
        Stream << "screen=" << SX << "," << SY
               << " viewport=" << VW << "x" << VH
               << " map=" << MW << "x" << MH
               << " rect=" << Layout.MinimapRect.X << "," << Layout.MinimapRect.Y
               << "," << Layout.MinimapRect.W << "," << Layout.MinimapRect.H
               << " hit=" << (Hit ? 1 : 0);
        if (Hit)
            Stream << " world=" << OutWX << "," << OutWY;
        Core::Utils::InputDebugLog("InputHudMinimap", Stream.str());
    }
    return Hit;
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

int Hud::CheckQueueSlotClick(int SX, int SY) const
{
    for (const auto& Hit : mState.InfoPanel.QueueSlotHits)
    {
        if (SX >= Hit.X && SX < Hit.X + Hit.W &&
            SY >= Hit.Y && SY < Hit.Y + Hit.H)
        {
            return Hit.QueueIndex;
        }
    }
    return -1;
}

int Hud::CheckMultiSelectSlotClick(int SX, int SY, int& OutEntityId) const
{
    for (const auto& Hit : mState.InfoPanel.MultiSelectSlotHits)
    {
        if (SX >= Hit.X && SX < Hit.X + Hit.W &&
            SY >= Hit.Y && SY < Hit.Y + Hit.H)
        {
            OutEntityId = Hit.EntityId;
            return Hit.SlotIndex;
        }
    }
    OutEntityId = -1;
    return -1;
}

int Hud::CheckMinimapButtonClick(int SX, int SY, int VW, int VH) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    for (int I = 0; I < 3; ++I)
    {
        if (Layout.MinimapButtonRects[I].Contains(SX, SY))
            return I;
    }
    return -1;
}

void Hud::ToggleMinimapTerrain()
{
    mState.Minimap.ShowTerrain = !mState.Minimap.ShowTerrain;
}

void Hud::ToggleMinimapAllianceColors()
{
    mState.Minimap.UseAllianceColors = !mState.Minimap.UseAllianceColors;
}

void Hud::ToggleMinimapBeacons()
{
    mState.Minimap.ShowBeacons = !mState.Minimap.ShowBeacons;
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
