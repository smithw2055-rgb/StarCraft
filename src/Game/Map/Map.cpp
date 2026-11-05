#include "Game/Map/Map.h"
#include "Game/Map/ChkParser.h"
#include "Core/Utils/Logger.h"

#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <cmath>

namespace Game::Map
{
namespace
{
bool ShouldLogMapTileSamples()
{
    const char* Value = std::getenv("STARCRAFT_DEBUG_MAP_TILES");
    return Value && Value[0] == '1';
}
}

GameMap::GameMap()
{
    mTiles.resize(DefaultMapWidth * DefaultMapHeight);
    mNavigationGrid.Initialize(DefaultMapWidth, DefaultMapHeight);
}

void GameMap::Generate(int Width, int Height)
{
    mWidth = Width;
    mHeight = Height;
    mTiles.resize(Width * Height);
    mNavigationGrid.Initialize(Width, Height);

    for (int Y = 0; Y < Height; ++Y)
    {
        for (int X = 0; X < Width; ++X)
        {
            Tile& T = mTiles[Y * Width + X];
            T.Terrain = TerrainType::Grass;
            T.Walkable = true;
            T.Buildable = true;
            T.Resource = ResourceType::None;
            T.ResourceAmount = 0;
            T.MegaTileID = 0;
        }
    }

    // Place start locations and resource clusters
    mStartLocations.clear();

    for (int P = 0; P < 2; P++)
    {
        int BaseX = (P == 0) ? (Width / 4) : (3 * Width / 4);
        int BaseY = (P == 0) ? (Height / 4) : (3 * Height / 4);

        mStartLocations.push_back({BaseX, BaseY, P});

        // Mineral patch cluster (8 patches in a C-shape)
        int MineralPositions[8][2] = {
            {-2, -2}, {-1, -3}, {1, -3}, {2, -2},
            {3, -1}, {3, 1}, {2, 2}, {-2, 2}
        };
        for (int M = 0; M < 8; M++)
        {
            int MX = BaseX + MineralPositions[M][0];
            int MY = BaseY + MineralPositions[M][1];
            if (IsInBounds(MX, MY))
            {
                Tile& T = mTiles[MY * Width + MX];
                T.Resource = ResourceType::Minerals;
                T.ResourceAmount = 1500;
                T.Buildable = false;
            }
        }

        // Vespene geyser
        int GX = BaseX + 5;
        int GY = BaseY + 0;
        if (IsInBounds(GX, GY))
        {
            Tile& T = mTiles[GY * Width + GX];
            T.Resource = ResourceType::VespeneGas;
            T.ResourceAmount = 5000;
            T.Buildable = false;
        }
    }

    for (int Y = 0; Y < Height; ++Y)
    {
        for (int X = 0; X < Width; ++X)
            UpdateNavigationBuildTile(X, Y);
    }
    mNavigationGrid.CommitBaseChanges();
}

void GameMap::SetTile(int X, int Y, const Tile& NewTile)
{
    assert(IsInBounds(X, Y));
    const Tile& OldTile = mTiles[Y * mWidth + X];
    const bool NavChanged =
        OldTile.Walkable != NewTile.Walkable ||
        OldTile.GroundHeight != NewTile.GroundHeight ||
        OldTile.SubtileWalkable != NewTile.SubtileWalkable ||
        OldTile.SubtileGroundHeights != NewTile.SubtileGroundHeights ||
        OldTile.SubtileRamp != NewTile.SubtileRamp;
    mTiles[Y * mWidth + X] = NewTile;
    if (NavChanged)
    {
        UpdateNavigationBuildTile(X, Y);
        mNavigationGrid.CommitBaseChanges();
    }
}

void GameMap::UpdateNavigationBuildTile(int X, int Y)
{
    if (!IsInBounds(X, Y))
        return;

    const Tile& T = mTiles[Y * mWidth + X];
    for (int SubY = 0; SubY < 4; ++SubY)
    {
        for (int SubX = 0; SubX < 4; ++SubX)
        {
            const size_t SubtileIndex =
                (size_t)SubY * 4 + (size_t)SubX;
            const int CellX = X * Game::Pathfinding::NavigationCellsPerBuildTile + SubX;
            const int CellY = Y * Game::Pathfinding::NavigationCellsPerBuildTile + SubY;
            const bool PreservedRamp = T.SubtileRamp[SubtileIndex] || mNavigationGrid.IsRamp(CellX, CellY);
            const bool CellWalkable = T.Walkable && T.SubtileWalkable[SubtileIndex];
            mNavigationGrid.SetBaseCell(
                CellX, CellY,
                CellWalkable, T.SubtileGroundHeights[SubtileIndex],
                PreservedRamp);
        }
    }
}

const Tile& GameMap::GetTile(int X, int Y) const
{
    assert(IsInBounds(X, Y));
    return mTiles[Y * mWidth + X];
}

bool GameMap::IsWalkable(int X, int Y) const
{
    if (!IsInBounds(X, Y)) return false;
    return mTiles[Y * mWidth + X].Walkable;
}

int GameMap::GetGroundHeight(int X, int Y) const
{
    if (!IsInBounds(X, Y))
        return 0;
    return (int)mTiles[Y * mWidth + X].GroundHeight;
}

int GameMap::GetGroundHeightAtWorld(const Core::Math::Vec2& WorldPos) const
{
    int TileX = 0;
    int TileY = 0;
    WorldToTile(WorldPos, TileX, TileY);
    if (!IsInBounds(TileX, TileY))
        return 0;

    const int PixelX = std::max(0, (int)std::floor(WorldPos.X));
    const int PixelY = std::max(0, (int)std::floor(WorldPos.Y));
    const int SubX = std::clamp((PixelX % 32) / 8, 0, 3);
    const int SubY = std::clamp((PixelY % 32) / 8, 0, 3);
    return (int)mTiles[TileY * mWidth + TileX]
        .SubtileGroundHeights[(size_t)SubY * 4 + (size_t)SubX];
}

bool GameMap::CanTraverseGround(int FromX, int FromY, int ToX, int ToY) const
{
    if (!IsInBounds(FromX, FromY) || !IsInBounds(ToX, ToY))
        return false;
    const int H1 = GetGroundHeight(FromX, FromY);
    const int H2 = GetGroundHeight(ToX, ToY);
    if (H1 == H2)
        return true;
    if (std::abs(H1 - H2) > 1)
        return false;
    if (!mNavigationGrid.HasExplicitRamps())
        return IsWalkable(FromX, FromY) && IsWalkable(ToX, ToY);

    for (int SubY = 0; SubY < 4; ++SubY)
    {
        for (int SubX = 0; SubX < 4; ++SubX)
        {
            if (mNavigationGrid.IsRamp(FromX * 4 + SubX, FromY * 4 + SubY) ||
                mNavigationGrid.IsRamp(ToX * 4 + SubX, ToY * 4 + SubY))
            {
                return true;
            }
        }
    }
    return false;
}

bool GameMap::CanTraverseGround(const Core::Math::Vec2& From,
                                const Core::Math::Vec2& To) const
{
    int FX = 0, FY = 0, TX = 0, TY = 0;
    WorldToTile(From, FX, FY);
    WorldToTile(To, TX, TY);
    return CanTraverseGround(FX, FY, TX, TY);
}

bool GameMap::IsBuildable(int X, int Y) const
{
    if (!IsInBounds(X, Y)) return false;
    return mTiles[Y * mWidth + X].Buildable;
}

bool GameMap::IsInBounds(int X, int Y) const
{
    return X >= 0 && X < mWidth && Y >= 0 && Y < mHeight;
}

Core::Math::Vec2 GameMap::TileToWorld(int X, int Y) const
{
    return {
        static_cast<float>(X) * TileSize + TileSize * 0.5f,
        static_cast<float>(Y) * TileSize + TileSize * 0.5f
    };
}

void GameMap::WorldToTile(const Core::Math::Vec2& WorldPos, int& OutX, int& OutY) const
{
    OutX = static_cast<int>(WorldPos.X / TileSize);
    OutY = static_cast<int>(WorldPos.Y / TileSize);
}

uint16_t GameMap::GetMegaTile(int X, int Y) const
{
    if (!IsInBounds(X, Y)) return 0;
    return mTiles[Y * mWidth + X].MegaTileID;
}

void GameMap::SetMegaTile(int X, int Y, uint16_t MegaTileID)
{
    if (!IsInBounds(X, Y)) return;
    mTiles[Y * mWidth + X].MegaTileID = MegaTileID;
}

static TerrainType MegaTileToTerrain(uint16_t MT)
{
    // Simple heuristic: use the low-byte range to estimate terrain type
    // Proper mapping requires tileset-specific megatile tables
    uint8_t Low = MT & 0xFF;

    if (Low < 4)   return TerrainType::Floor;
    if (Low < 8)   return TerrainType::Grass;
    if (Low < 12)  return TerrainType::Dirt;
    if (Low < 16)  return TerrainType::Rock;
    if (Low < 20)  return TerrainType::Mud;
    if (Low < 24)  return TerrainType::Water;
    if (Low < 28)  return TerrainType::Lava;
    if (Low < 32)  return TerrainType::Ash;
    if (Low < 40)  return TerrainType::HighGrass;
    if (Low < 48)  return TerrainType::HighRock;
    return TerrainType::Floor;
}

void GameMap::ApplyTilesetData(const std::vector<uint16_t>* Cv5Data,
                                const std::vector<uint16_t>* Vf4Data)
{
    if (!Cv5Data || !Vf4Data) return;

    int WalkableTiles = 0, UnwalkableTiles = 0;
    int GroundHeightTiles[3] = {0, 0, 0};

    for (int Y = 0; Y < mHeight; Y++)
    {
        for (int X = 0; X < mWidth; X++)
        {
            Tile& T = mTiles[Y * mWidth + X];
            uint16_t MT = T.MegaTileID;
            uint16_t Group = (MT >> 4) & 0x7FF;
            uint16_t Sub = MT & 0xF;
            size_t Cv5Off = Group * 16 + Sub;

            if (Cv5Off >= Cv5Data->size())
            {
                T.Walkable = T.Buildable = false;
                UpdateNavigationBuildTile(X, Y);
                UnwalkableTiles++;
                continue;
            }
            uint16_t MegaIdx = (*Cv5Data)[Cv5Off];
            size_t Vf4EntryCount = Vf4Data->size() / 16;
            if (MegaIdx >= Vf4EntryCount)
            {
                T.Walkable = T.Buildable = false;
                UpdateNavigationBuildTile(X, Y);
                UnwalkableTiles++;
                continue;
            }

            int WalkableCount = 0;
            int MiddleCount = 0;
            int HighCount = 0;
            for (int MI = 0; MI < 16; ++MI)
            {
                size_t Vf4Off = (size_t)MegaIdx * 16 + MI;
                if (Vf4Off >= Vf4Data->size()) continue;
                const uint16_t Flags = (*Vf4Data)[Vf4Off];
                if ((Flags & 0x0001) != 0)
                {
                    ++WalkableCount;
                }
                if ((Flags & 0x0002) != 0) ++MiddleCount;
                if ((Flags & 0x0004) != 0) ++HighCount;
            }

            for (int MI = 0; MI < 16; MI++)
            {
                size_t Vf4Off = (size_t)MegaIdx * 16 + MI;
                if (Vf4Off >= Vf4Data->size())
                    continue;

                const uint16_t Flags = (*Vf4Data)[Vf4Off];
                T.SubtileGroundHeights[(size_t)MI] =
                    (Flags & 0x0004) != 0 ? 2 :
                    (Flags & 0x0002) != 0 ? 1 : 0;
                const bool SubWalkable = (Flags & 0x0001) != 0;
                const bool SubIsRamp = (Flags & 0x0010) != 0;
                T.SubtileWalkable[(size_t)MI] = SubWalkable;
                T.SubtileRamp[(size_t)MI] = SubIsRamp;
                mNavigationGrid.SetBaseCell(
                    X * Game::Pathfinding::NavigationCellsPerBuildTile +
                        (MI % 4),
                    Y * Game::Pathfinding::NavigationCellsPerBuildTile +
                        (MI / 4),
                    SubWalkable,
                    T.SubtileGroundHeights[(size_t)MI],
                    SubIsRamp);
            }

            T.Terrain = MegaTileToTerrain(MT);
            T.Walkable = (WalkableCount >= 8);
            T.Buildable = (WalkableCount > 12);
            if (!T.Walkable &&
                T.Terrain != TerrainType::Water &&
                T.Terrain != TerrainType::Lava &&
                T.Terrain != TerrainType::HighRock)
            {
                T.Terrain = TerrainType::Rock;
            }
            // Match the reference megatile aggregation: ramps/middle ground
            // remain level 1 while true high ground remains level 2.
            T.GroundHeight = HighCount >= 12 ? 2 :
                (HighCount < 12 && MiddleCount + HighCount >= 12 ? 1 : 0);
            ++GroundHeightTiles[std::min(2, (int)T.GroundHeight)];
            T.HasCreep = false;
            T.Resource = ResourceType::None;
            T.ResourceAmount = 0;
            if (T.Walkable) WalkableTiles++;
            else UnwalkableTiles++;
        }
    }
    mNavigationGrid.CommitBaseChanges();
    Core::Utils::Logger::Info("GameMap",
        "ApplyTilesetData: " + std::to_string(WalkableTiles) +
        " walkable, " + std::to_string(UnwalkableTiles) +
        " unwalkable tiles, ground heights=" +
        std::to_string(GroundHeightTiles[0]) + "/" +
        std::to_string(GroundHeightTiles[1]) + "/" +
        std::to_string(GroundHeightTiles[2]));

    if (!ShouldLogMapTileSamples())
        return;

    for (int Y = 0; Y < mHeight && Y < 5; Y++)
    {
        for (int X = 0; X < mWidth && X < 5; X++)
        {
            int Idx = Y * mWidth + X;
            Core::Utils::Logger::Debug("GameMap",
                "  Tile(" + std::to_string(X) + "," + std::to_string(Y) +
                ") MT=" + std::to_string(mTiles[Idx].MegaTileID) +
                " walk=" + (mTiles[Idx].Walkable ? "1" : "0") +
                " build=" + (mTiles[Idx].Buildable ? "1" : "0"));
        }
    }
}

void GameMap::LoadFromChk(const ChkParser& Parser)
{
    mWidth = Parser.GetMapWidth();
    mHeight = Parser.GetMapHeight();
    mTiles.resize(mWidth * mHeight);
    mNavigationGrid.Initialize(mWidth, mHeight);
    mStartLocations.clear();

    // ── Terrain from MTXM ──────────────────────────────────
    auto& MegaTiles = Parser.GetMegaTiles();
    size_t TileCount = (size_t)mWidth * mHeight;
    for (size_t I = 0; I < TileCount && I < MegaTiles.size(); I++)
    {
        int X = (int)(I % mWidth);
        int Y = (int)(I / mWidth);
        uint16_t MT = MegaTiles[I];

        Tile T;
        T.MegaTileID = MT;
        T.Terrain = MegaTileToTerrain(MT);
        T.Walkable = (T.Terrain != TerrainType::Water && T.Terrain != TerrainType::Lava
                      && T.Terrain != TerrainType::Rock && T.Terrain != TerrainType::HighRock);
        T.Buildable = T.Walkable;
        T.SubtileWalkable.fill(T.Walkable);
        T.SubtileRamp.fill(false);
        T.Resource = ResourceType::None;
        T.ResourceAmount = 0;

        mTiles[Y * mWidth + X] = T;
    }

    // ── Resources and start locations from UNIT ────────────
    auto* Units = Parser.GetUnits();
    size_t UnitCount = Parser.GetUnitCount();

    for (size_t I = 0; I < UnitCount; I++)
    {
        auto& U = Units[I];

        // Convert pixel coords to tile coords
        int TileX = U.X / 32;
        int TileY = U.Y / 32;

        if (U.UnitID == 214 || U.UnitID == 115) // Start Location (214 = StarCraft standard, 115 = legacy)
        {
            StartLocationInfo SL;
            SL.TileX = TileX;
            SL.TileY = TileY;
            SL.PlayerIndex = U.Owner;
            mStartLocations.push_back(SL);
        }
        else if ((U.UnitID >= 176 && U.UnitID <= 178) || (U.UnitID >= 100 && U.UnitID <= 102)) // Mineral Patch (176..178 = StarCraft standard, 100..102 = legacy)
        {
            if (IsInBounds(TileX, TileY))
            {
                auto& T = mTiles[TileY * mWidth + TileX];
                T.Resource = ResourceType::Minerals;
                T.ResourceAmount = U.ResourceAmount > 0 ? (int)U.ResourceAmount : 1500;
                T.Buildable = false;
            }
        }
        else if (U.UnitID == 188 || U.UnitID == 103) // Vespene Geyser (188 = StarCraft standard, 103 = legacy)
        {
            if (IsInBounds(TileX, TileY))
            {
                auto& T = mTiles[TileY * mWidth + TileX];
                T.Resource = ResourceType::VespeneGas;
                T.ResourceAmount = U.ResourceAmount > 0 ? (int)U.ResourceAmount : 5000;
                T.Buildable = false;
            }
        }
    }

    for (int Y = 0; Y < mHeight; ++Y)
    {
        for (int X = 0; X < mWidth; ++X)
            UpdateNavigationBuildTile(X, Y);
    }
    mNavigationGrid.CommitBaseChanges();
}

} // namespace Game::Map
