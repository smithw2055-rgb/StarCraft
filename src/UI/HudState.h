#pragma once

#include <cstdint>
#include <vector>

#include "Core/Math/Vec2.h"
#include "UI/HudCommandPanelState.h"
#include "UI/HudInfoPanelState.h"

namespace Game::Client::UI
{

struct HudResourceState
{
    int MineralCount = 50;
    int GasCount = 0;
    int SupplyUsed = 0;
    int SupplyMax = 200;
};

struct HudSelectionState
{
    int SelectedCount = 0;
    int LastHovered = -1;
    int SelectedEntityId = -1;
    int SelectedChkUnitId = -1;
    int SelectedTerranDefId = -1;
    std::vector<int> LastSelectedIds;
};

struct HudMinimapPing
{
    Core::Math::Vec2 Position;
    float TimeRemaining = 0.0f;
};

struct HudMinimapDot
{
    Core::Math::Vec2 Position;
    uint32_t Color = 0xFFFFFFFF;
    uint8_t Width = 2;
    uint8_t Height = 2;
};

struct HudMinimapState
{
    bool ShowTerrain = true;
    bool UseAllianceColors = false;
    bool ShowBeacons = true;

    float CameraX = 0.0f;
    float CameraY = 0.0f;
    float CameraZoom = 1.0f;
    int ViewportWidth = 0;
    int ViewportHeight = 0;
    int MapWidth = 0;
    int MapHeight = 0;
    mutable std::vector<int> TileColors;
    std::vector<HudMinimapDot> UnitDots;
    mutable std::vector<bool> FogVisible;
    mutable std::vector<bool> FogExplored;
    std::vector<HudMinimapPing> CampaignPings;

    float CamVisibleX = 0.0f;
    float CamVisibleY = 0.0f;
    float CamVisibleW = 0.0f;
    float CamVisibleH = 0.0f;
};

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
