#pragma once

#include <algorithm>
#include <cmath>

namespace Game::Client
{

struct BuildingFootprintTiles
{
    float W = 3.0f;
    float H = 2.0f;
};

struct BuildingFootprintSpec
{
    float CenterX = 0.0f;
    float CenterY = 0.0f;
    float RadiusX = 24.0f;
    float RadiusY = 8.0f;
};

inline BuildingFootprintSpec SelectionEllipseFromVisualBounds(float X,
                                                              float Y,
                                                              float FrameHalfW,
                                                              float FrameHalfH,
                                                              float FallbackRadius)
{
    float Radius = FallbackRadius;
    if (FrameHalfW > 0.0f && FrameHalfH > 0.0f)
        Radius = std::sqrt(FrameHalfW * FrameHalfW + FrameHalfH * FrameHalfH);

    Radius = std::max(8.0f, Radius);
    return {
        X,
        Y,
        Radius,
        std::max(4.0f, Radius * 0.5f)
    };
}

inline BuildingFootprintSpec SelectionEllipseFromVisualRadius(float X,
                                                              float Y,
                                                              float VisualRadius,
                                                              float FallbackRadius)
{
    float Radius = VisualRadius > 0.0f ? VisualRadius : FallbackRadius;
    Radius = std::max(8.0f, Radius);
    return {
        X,
        Y,
        Radius,
        std::max(4.0f, Radius * 0.5f)
    };
}

inline BuildingFootprintSpec SelectionEllipseFromDatDimensions(float X,
                                                               float Y,
                                                               int DimensionWidth,
                                                               int DimensionHeight,
                                                               float FallbackRadius)
{
    if (DimensionWidth > 0 && DimensionHeight > 0)
    {
        return {
            X,
            Y,
            std::max(8.0f, (float)DimensionWidth * 0.5f),
            std::max(4.0f, (float)DimensionHeight * 0.5f)
        };
    }

    const float Radius = std::max(8.0f, FallbackRadius);
    return {
        X,
        Y,
        Radius,
        std::max(4.0f, Radius * 0.5f)
    };
}

inline BuildingFootprintSpec SelectionEllipseFromDatBounds(float X,
                                                           float Y,
                                                           int Left,
                                                           int Up,
                                                           int Right,
                                                           int Down,
                                                           float FallbackRadius)
{
    const int Width = Left + Right + 1;
    const int Height = Up + Down + 1;
    if (Width > 0 && Height > 0)
    {
        return {
            X + ((float)Right + 1.0f - (float)Left) * 0.5f,
            Y + ((float)Down + 1.0f - (float)Up) * 0.5f,
            std::max(8.0f, (float)Width * 0.5f),
            std::max(4.0f, (float)Height * 0.5f)
        };
    }

    return SelectionEllipseFromDatDimensions(X, Y, Width, Height, FallbackRadius);
}

inline int SelectionCirclePixelSize(int SelectionCircle)
{
    static constexpr int kSizes[] = {22, 32, 48, 62, 72, 94, 110, 122, 146, 224};
    if (SelectionCircle < 0 || SelectionCircle >= (int)(sizeof(kSizes) / sizeof(kSizes[0])))
        return 0;
    return kSizes[SelectionCircle];
}

inline BuildingFootprintSpec SelectionEllipseFromSpriteCircle(float X,
                                                              float Y,
                                                              int SelectionCircle,
                                                              int VerticalOffset,
                                                              float FallbackRadius)
{
    const int PixelSize = SelectionCirclePixelSize(SelectionCircle);
    if (PixelSize > 0)
    {
        const float RadiusX = (float)PixelSize * 0.5f;
        return {
            X,
            Y + (float)VerticalOffset,
            std::max(8.0f, RadiusX),
            std::max(4.0f, RadiusX * 0.5f)
        };
    }

    const float Radius = std::max(8.0f, FallbackRadius);
    return {
        X,
        Y + (float)VerticalOffset,
        Radius,
        std::max(4.0f, Radius * 0.5f)
    };
}

inline BuildingFootprintTiles BuildingFootprintSize(int ChkUnitID)
{
    switch (ChkUnitID)
    {
        case 106: // Terran Command Center
        case 111: // Terran Barracks
        case 113: // Terran Factory
        case 114: // Terran Starport
        case 116: // Terran Science Facility
        case 131: // Zerg Hatchery
        case 132: // Zerg Lair
        case 133: // Zerg Hive
        case 154: // Protoss Nexus
        case 160: // Protoss Gateway
        case 167: // Protoss Stargate
            return {4.0f, 3.0f};
        case 109: // Terran Supply Depot
        case 112: // Terran Academy
        case 122: // Terran Engineering Bay
        case 123: // Terran Armory
        case 125: // Terran Bunker
        case 134: // Zerg Nydus Canal
        case 135: // Zerg Hydralisk Den
        case 136: // Zerg Defiler Mound
        case 138: // Zerg Queen's Nest
        case 139: // Zerg Evolution Chamber
        case 140: // Zerg Ultralisk Cavern
        case 142: // Zerg Spawning Pool
        case 155: // Protoss Robotics Facility
        case 159: // Protoss Observatory
        case 163: // Protoss Citadel of Adun
        case 164: // Protoss Cybernetics Core
        case 165: // Protoss Templar Archives
        case 166: // Protoss Forge
        case 169: // Protoss Fleet Beacon
        case 170: // Protoss Arbiter Tribunal
        case 171: // Protoss Robotics Support Bay
            return {3.0f, 2.0f};
        case 110: // Terran Refinery
        case 149: // Zerg Extractor
        case 157: // Protoss Assimilator
            return {4.0f, 2.0f};
        case 107: // Terran Comsat Station
        case 108: // Terran Nuclear Silo
        case 115: // Terran Control Tower
        case 117: // Terran Covert Ops
        case 118: // Terran Physics Lab
        case 120: // Terran Machine Shop
        case 124: // Terran Missile Turret
        case 137: // Zerg Greater Spire
        case 141: // Zerg Spire
        case 156: // Protoss Pylon
        case 162: // Protoss Photon Cannon
        case 172: // Protoss Shield Battery
            return {2.0f, 2.0f};
        default:
            return {3.0f, 2.0f};
    }
}

inline BuildingFootprintSpec BuildingFootprintForSelection(float X,
                                                           float Y,
                                                           int ChkUnitID,
                                                           float FrameHalfW = 0.0f,
                                                           float FrameHalfH = 0.0f)
{
    const BuildingFootprintTiles Tiles = BuildingFootprintSize(ChkUnitID);
    const float FallbackHalfW = Tiles.W * 16.0f;
    const float FallbackHalfH = Tiles.H * 16.0f;
    const float FallbackRadius =
        std::sqrt(FallbackHalfW * FallbackHalfW + FallbackHalfH * FallbackHalfH);
    return SelectionEllipseFromVisualBounds(X, Y, FrameHalfW, FrameHalfH, FallbackRadius);
}

} // namespace Game::Client
