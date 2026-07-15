#pragma once

namespace Game::ProtossPower
{

inline constexpr int PylonRadiusTiles = 6;
inline constexpr float TileSize = 32.0f;
inline constexpr float PylonRadiusWorld =
    (float)PylonRadiusTiles * TileSize;

inline bool ContainsTileOffset(int Dx, int Dy)
{
    return Dx * Dx + Dy * Dy <= PylonRadiusTiles * PylonRadiusTiles;
}

} // namespace Game::ProtossPower
