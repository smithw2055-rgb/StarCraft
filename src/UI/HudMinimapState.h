#pragma once

#include "Core/Math/Vec2.h"

#include <vector>

namespace Game::Client::UI
{

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

} // namespace Game::Client::UI
