#pragma once

#include <vector>

namespace Game::Client::UI
{

struct HudMinimapState
{
    float CameraX = 0.0f;
    float CameraY = 0.0f;
    float CameraZoom = 1.0f;
    int ViewportWidth = 0;
    int ViewportHeight = 0;
    int MapWidth = 0;
    int MapHeight = 0;
    mutable std::vector<int> TileColors;
    mutable std::vector<float> UnitDots;
    mutable std::vector<bool> FogVisible;
    mutable std::vector<bool> FogExplored;
};

} // namespace Game::Client::UI
