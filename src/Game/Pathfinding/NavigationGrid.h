#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Game::Pathfinding
{

inline constexpr int NavigationCellSize = 8;
inline constexpr int NavigationCellsPerBuildTile = 4;

enum class PathStatus : uint8_t
{
    Exact,
    Partial,
    Unreachable
};

enum class PathEndReason : uint8_t
{
    None,
    GoalBlocked,
    Disconnected,
    NodeBudget
};

struct NavigationFootprint
{
    int Left = 0;
    int Up = 0;
    int Right = 0;
    int Down = 0;

    int Signature() const
    {
        const int CellLeft = (std::max(0, Left) + NavigationCellSize - 1) /
            NavigationCellSize;
        const int CellUp = (std::max(0, Up) + NavigationCellSize - 1) /
            NavigationCellSize;
        const int CellRight = (std::max(0, Right) + NavigationCellSize - 1) /
            NavigationCellSize;
        const int CellDown = (std::max(0, Down) + NavigationCellSize - 1) /
            NavigationCellSize;
        return CellLeft | (CellUp << 6) | (CellRight << 12) |
            (CellDown << 18);
    }
};

struct NavigationObstacle
{
    int OwnerEntityID = -1;
    Core::Math::Vec2 Position;
    int Left = 0;
    int Up = 0;
    int Right = 0;
    int Down = 0;

    bool operator==(const NavigationObstacle& Other) const
    {
        return OwnerEntityID == Other.OwnerEntityID &&
            Position.X == Other.Position.X &&
            Position.Y == Other.Position.Y &&
            Left == Other.Left && Up == Other.Up &&
            Right == Other.Right && Down == Other.Down;
    }
};

class NavigationGrid
{
public:
    void Initialize(int BuildWidth, int BuildHeight)
    {
        mWidth = std::max(0, BuildWidth) * NavigationCellsPerBuildTile;
        mHeight = std::max(0, BuildHeight) * NavigationCellsPerBuildTile;
        const size_t CellCount = (size_t)mWidth * (size_t)mHeight;
        mWalkable.assign(CellCount, 1);
        mHeightLevel.assign(CellCount, 0);
        mStaticOwner.assign(CellCount, -1);
        ++mVersion;
    }

    int GetWidth() const { return mWidth; }
    int GetHeight() const { return mHeight; }
    uint32_t GetVersion() const { return mVersion; }

    bool IsInBounds(int X, int Y) const
    {
        return X >= 0 && X < mWidth && Y >= 0 && Y < mHeight;
    }

    void SetBaseCell(int X, int Y, bool Walkable, uint8_t HeightLevel)
    {
        if (!IsInBounds(X, Y))
            return;
        const size_t Index = CellIndex(X, Y);
        mWalkable[Index] = Walkable ? 1 : 0;
        mHeightLevel[Index] = std::min<uint8_t>(2, HeightLevel);
    }

    void CommitBaseChanges()
    {
        ++mVersion;
    }

    bool IsBaseWalkable(int X, int Y) const
    {
        return IsInBounds(X, Y) && mWalkable[CellIndex(X, Y)] != 0;
    }

    int GetHeightLevel(int X, int Y) const
    {
        return IsInBounds(X, Y) ? (int)mHeightLevel[CellIndex(X, Y)] : -1;
    }

    int GetStaticOwner(int X, int Y) const
    {
        return IsInBounds(X, Y) ? mStaticOwner[CellIndex(X, Y)] : -2;
    }

    void SyncStaticObstacles(std::vector<NavigationObstacle> Obstacles)
    {
        std::sort(Obstacles.begin(), Obstacles.end(),
            [](const NavigationObstacle& A, const NavigationObstacle& B)
            {
                return A.OwnerEntityID < B.OwnerEntityID;
            });
        if (Obstacles == mObstacles)
            return;

        mObstacles = std::move(Obstacles);
        std::fill(mStaticOwner.begin(), mStaticOwner.end(), -1);
        for (const NavigationObstacle& Obstacle : mObstacles)
        {
            const int MinimumX = WorldPixelToCell(
                (int)std::floor(Obstacle.Position.X) - Obstacle.Left);
            const int MaximumX = WorldPixelToCell(
                (int)std::ceil(Obstacle.Position.X) + Obstacle.Right);
            const int MinimumY = WorldPixelToCell(
                (int)std::floor(Obstacle.Position.Y) - Obstacle.Up);
            const int MaximumY = WorldPixelToCell(
                (int)std::ceil(Obstacle.Position.Y) + Obstacle.Down);

            for (int Y = std::max(0, MinimumY);
                 Y <= std::min(mHeight - 1, MaximumY); ++Y)
            {
                for (int X = std::max(0, MinimumX);
                     X <= std::min(mWidth - 1, MaximumX); ++X)
                {
                    int& Owner = mStaticOwner[CellIndex(X, Y)];
                    if (Owner == -1)
                        Owner = Obstacle.OwnerEntityID;
                    else if (Owner != Obstacle.OwnerEntityID)
                        Owner = -2;
                }
            }
        }
        ++mVersion;
    }

    void WorldToCell(const Core::Math::Vec2& Position,
                     int& OutX, int& OutY) const
    {
        OutX = WorldPixelToCell((int)std::floor(Position.X));
        OutY = WorldPixelToCell((int)std::floor(Position.Y));
    }

    Core::Math::Vec2 CellToWorld(int X, int Y) const
    {
        return {
            (float)(X * NavigationCellSize + NavigationCellSize / 2),
            (float)(Y * NavigationCellSize + NavigationCellSize / 2)
        };
    }

    bool CanOccupy(int CenterX, int CenterY,
                   const NavigationFootprint& Footprint,
                   int IgnoredOwnerEntityID = -1,
                   int AdditionalIgnoredOwnerEntityID = -1) const
    {
        if (!IsInBounds(CenterX, CenterY))
            return false;

        const size_t CenterIndex = CellIndex(CenterX, CenterY);
        if (mWalkable[CenterIndex] == 0)
            return false;

        const int CenterOwner = mStaticOwner[CenterIndex];
        if (CenterOwner != -1 &&
            CenterOwner != IgnoredOwnerEntityID &&
            CenterOwner != AdditionalIgnoredOwnerEntityID)
        {
            return false;
        }

        const int CenterPixelX =
            CenterX * NavigationCellSize + NavigationCellSize / 2;
        const int CenterPixelY =
            CenterY * NavigationCellSize + NavigationCellSize / 2;
        const int MinimumX = WorldPixelToCell(
            CenterPixelX - std::max(0, Footprint.Left));
        const int MaximumX = WorldPixelToCell(
            CenterPixelX + std::max(0, Footprint.Right));
        const int MinimumY = WorldPixelToCell(
            CenterPixelY - std::max(0, Footprint.Up));
        const int MaximumY = WorldPixelToCell(
            CenterPixelY + std::max(0, Footprint.Down));

        // Fast path for 1x1 cell footprint (most common infantry/workers)
        if (MinimumX == MaximumX && MinimumY == MaximumY)
            return true;

        for (int Y = MinimumY; Y <= MaximumY; ++Y)
        {
            for (int X = MinimumX; X <= MaximumX; ++X)
            {
                if (X == CenterX && Y == CenterY)
                    continue;

                if (!IsInBounds(X, Y) ||
                    mWalkable[CellIndex(X, Y)] == 0)
                {
                    return false;
                }
                const int Owner = mStaticOwner[CellIndex(X, Y)];
                if (Owner != -1 &&
                    Owner != IgnoredOwnerEntityID &&
                    Owner != AdditionalIgnoredOwnerEntityID)
                    return false;
            }
        }
        return true;
    }

    bool CanOccupyWorld(const Core::Math::Vec2& Position,
                        const NavigationFootprint& Footprint,
                        int IgnoredOwnerEntityID = -1,
                        int AdditionalIgnoredOwnerEntityID = -1) const
    {
        int CenterX = 0;
        int CenterY = 0;
        WorldToCell(Position, CenterX, CenterY);
        if (!IsInBounds(CenterX, CenterY))
            return false;

        const int MinimumX = WorldPixelToCell(
            (int)std::floor(Position.X) - std::max(0, Footprint.Left));
        const int MaximumX = WorldPixelToCell(
            (int)std::ceil(Position.X) + std::max(0, Footprint.Right));
        const int MinimumY = WorldPixelToCell(
            (int)std::floor(Position.Y) - std::max(0, Footprint.Up));
        const int MaximumY = WorldPixelToCell(
            (int)std::ceil(Position.Y) + std::max(0, Footprint.Down));
        for (int Y = MinimumY; Y <= MaximumY; ++Y)
        {
            for (int X = MinimumX; X <= MaximumX; ++X)
            {
                if (!IsInBounds(X, Y) ||
                    mWalkable[CellIndex(X, Y)] == 0)
                {
                    return false;
                }
                const int Owner = mStaticOwner[CellIndex(X, Y)];
                if (Owner != -1 &&
                    Owner != IgnoredOwnerEntityID &&
                    Owner != AdditionalIgnoredOwnerEntityID)
                    return false;
            }
        }
        return true;
    }

    bool CanTraverseWorld(const Core::Math::Vec2& From,
                          const Core::Math::Vec2& To,
                          const NavigationFootprint& Footprint,
                          int IgnoredOwnerEntityID = -1,
                          int AdditionalIgnoredOwnerEntityID = -1) const
    {
        int FromX = 0, FromY = 0;
        int ToX = 0, ToY = 0;
        WorldToCell(From, FromX, FromY);
        WorldToCell(To, ToX, ToY);
        if (IsInBounds(FromX, FromY) && IsInBounds(ToX, ToY))
        {
            const int FromHeight = (int)mHeightLevel[CellIndex(FromX, FromY)];
            const int ToHeight = (int)mHeightLevel[CellIndex(ToX, ToY)];
            if (std::abs(FromHeight - ToHeight) > 1)
                return false;
        }
        return CanOccupyWorld(
                   To, Footprint, IgnoredOwnerEntityID,
                   AdditionalIgnoredOwnerEntityID);
    }

    bool CanTraverse(int FromX, int FromY, int ToX, int ToY,
                     const NavigationFootprint& Footprint,
                     int IgnoredOwnerEntityID = -1,
                     int AdditionalIgnoredOwnerEntityID = -1) const
    {
        if (IsInBounds(FromX, FromY) && IsInBounds(ToX, ToY))
        {
            const int FromHeight = (int)mHeightLevel[CellIndex(FromX, FromY)];
            const int ToHeight = (int)mHeightLevel[CellIndex(ToX, ToY)];
            if (std::abs(FromHeight - ToHeight) > 1)
                return false;
        }
        return CanOccupy(
            ToX, ToY, Footprint, IgnoredOwnerEntityID,
            AdditionalIgnoredOwnerEntityID);
    }

    static int WorldPixelToCell(int Pixel)
    {
        if (Pixel >= 0)
            return Pixel / NavigationCellSize;
        return -((-Pixel + NavigationCellSize - 1) / NavigationCellSize);
    }

private:

    size_t CellIndex(int X, int Y) const
    {
        return (size_t)Y * (size_t)mWidth + (size_t)X;
    }

    int mWidth = 0;
    int mHeight = 0;
    uint32_t mVersion = 0;
    std::vector<uint8_t> mWalkable;
    std::vector<uint8_t> mHeightLevel;
    std::vector<int> mStaticOwner;
    std::vector<NavigationObstacle> mObstacles;
};

} // namespace Game::Pathfinding
