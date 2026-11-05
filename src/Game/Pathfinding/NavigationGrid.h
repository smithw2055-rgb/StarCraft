#pragma once

#include <algorithm>
#include <array>
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

    int CellLeft() const
    {
        return (std::max(0, Left) + NavigationCellSize / 2 - 1) >> 3;
    }

    int CellUp() const
    {
        return (std::max(0, Up) + NavigationCellSize / 2 - 1) >> 3;
    }

    int CellRight() const
    {
        return (std::max(0, Right) + NavigationCellSize / 2) >> 3;
    }

    int CellDown() const
    {
        return (std::max(0, Down) + NavigationCellSize / 2) >> 3;
    }

    int MinCellRadius() const
    {
        return std::min({CellLeft(), CellUp(), CellRight(), CellDown()});
    }

    int MaxCellRadius() const
    {
        return std::max({CellLeft(), CellUp(), CellRight(), CellDown()});
    }

    int RequiredSymmetricRadiusCells() const
    {
        const int MaxExtentPx = std::max({0, Left, Up, Right, Down});
        return (MaxExtentPx + NavigationCellSize / 2) / NavigationCellSize;
    }

    int Signature() const
    {
        const int CL = (std::max(0, Left) + NavigationCellSize - 1) /
            NavigationCellSize;
        const int CU = (std::max(0, Up) + NavigationCellSize - 1) /
            NavigationCellSize;
        const int CR = (std::max(0, Right) + NavigationCellSize - 1) /
            NavigationCellSize;
        const int CD = (std::max(0, Down) + NavigationCellSize - 1) /
            NavigationCellSize;
        return CL | (CU << 6) | (CR << 12) | (CD << 18);
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
    static constexpr int MaxCachedClearanceRadius = 3;

    void Initialize(int BuildWidth, int BuildHeight)
    {
        mWidth = std::max(0, BuildWidth) * NavigationCellsPerBuildTile;
        mHeight = std::max(0, BuildHeight) * NavigationCellsPerBuildTile;
        const size_t CellCount = (size_t)mWidth * (size_t)mHeight;
        mWalkable.assign(CellCount, 1);
        mHeightLevel.assign(CellCount, 0);
        mIsRamp.assign(CellCount, 0);
        mStaticOwner.assign(CellCount, -1);
        mClearance.assign(CellCount, 0);
        mPassableMask.assign(CellCount, 0);
        mObstacles.clear();
        mHasExplicitRamps = false;
        ClearTemporaryObstacle();
        ++mTerrainVersion;
        ++mVersion;
        RebuildClearanceField();
    }

    int GetWidth() const { return mWidth; }
    int GetHeight() const { return mHeight; }
    uint32_t GetVersion() const { return mVersion; }

    bool IsInBounds(int X, int Y) const
    {
        return X >= 0 && X < mWidth && Y >= 0 && Y < mHeight;
    }

    void SetBaseCell(int X, int Y, bool Walkable, uint8_t HeightLevel, bool IsRamp = false)
    {
        if (!IsInBounds(X, Y))
            return;
        const size_t Index = CellIndex(X, Y);
        mWalkable[Index] = Walkable ? 1 : 0;
        mHeightLevel[Index] = std::min<uint8_t>(2, HeightLevel);
        mIsRamp[Index] = IsRamp ? 1 : 0;
        if (IsRamp)
            mHasExplicitRamps = true;
        ++mTerrainVersion;
    }

    void SetRamp(int X, int Y, bool IsRamp)
    {
        if (!IsInBounds(X, Y))
            return;
        mIsRamp[CellIndex(X, Y)] = IsRamp ? 1 : 0;
        if (IsRamp)
            mHasExplicitRamps = true;
        ++mTerrainVersion;
    }

    bool IsRamp(int X, int Y) const
    {
        if (!IsInBounds(X, Y))
            return false;
        return mIsRamp[CellIndex(X, Y)] != 0;
    }

    bool HasExplicitRamps() const
    {
        return mHasExplicitRamps;
    }

    void CommitBaseChanges()
    {
        ++mVersion;
        RebuildClearanceField();
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
        return IsInBounds(X, Y) ? EffectiveOwnerAt(X, Y) : -2;
    }

    void SetTemporaryObstacle(const NavigationObstacle& Obstacle)
    {
        mHasTempObstacle = true;
        mTempOwnerID = Obstacle.OwnerEntityID;
        mTempMinX = std::max(0, WorldPixelToCell(
            (int)std::floor(Obstacle.Position.X) - Obstacle.Left));
        mTempMaxX = std::min(mWidth - 1, WorldPixelToCell(
            (int)std::ceil(Obstacle.Position.X) + Obstacle.Right));
        mTempMinY = std::max(0, WorldPixelToCell(
            (int)std::floor(Obstacle.Position.Y) - Obstacle.Up));
        mTempMaxY = std::min(mHeight - 1, WorldPixelToCell(
            (int)std::ceil(Obstacle.Position.Y) + Obstacle.Down));
    }

    void ClearTemporaryObstacle()
    {
        mHasTempObstacle = false;
        mTempOwnerID = -1;
        mTempMinX = 0;
        mTempMaxX = -1;
        mTempMinY = 0;
        mTempMaxY = -1;
    }

    void EnsureTerrainComponents() const
    {
        if (mBuiltTerrainComponentVersion == mTerrainVersion &&
            mTerrainComponent.size() == (size_t)mWidth * (size_t)mHeight)
        {
            return;
        }
        RebuildTerrainComponents();
    }

    int GetTerrainComponent(int X, int Y) const
    {
        if (!IsInBounds(X, Y))
            return 0;
        EnsureTerrainComponents();
        return mTerrainComponent[CellIndex(X, Y)];
    }

    int GetEffectiveTerrainComponent(int X, int Y) const
    {
        if (!IsInBounds(X, Y))
            return 0;
        EnsureTerrainComponents();
        const int Direct = mTerrainComponent[CellIndex(X, Y)];
        if (Direct != 0)
            return Direct;
        for (int R = 1; R <= 2; ++R)
        {
            for (int DY = -R; DY <= R; ++DY)
            {
                for (int DX = -R; DX <= R; ++DX)
                {
                    const int NX = X + DX;
                    const int NY = Y + DY;
                    if (!IsInBounds(NX, NY))
                        continue;
                    const int Neighbor = mTerrainComponent[CellIndex(NX, NY)];
                    if (Neighbor != 0)
                        return Neighbor;
                }
            }
        }
        return 0;
    }

    bool AreWorldPositionsTerrainConnected(
        const Core::Math::Vec2& A,
        const Core::Math::Vec2& B) const
    {
        int AX = 0, AY = 0, BX = 0, BY = 0;
        WorldToCell(A, AX, AY);
        WorldToCell(B, BX, BY);
        AX = std::clamp(AX, 0, std::max(0, mWidth - 1));
        AY = std::clamp(AY, 0, std::max(0, mHeight - 1));
        BX = std::clamp(BX, 0, std::max(0, mWidth - 1));
        BY = std::clamp(BY, 0, std::max(0, mHeight - 1));
        const int CompA = GetEffectiveTerrainComponent(AX, AY);
        const int CompB = GetEffectiveTerrainComponent(BX, BY);
        return CompA == 0 || CompB == 0 || CompA == CompB;
    }

    void EnsureClearanceComponents(int Radius) const
    {
        const int ClampedR = std::clamp(Radius, 0, MaxCachedClearanceRadius);
        const size_t Count = (size_t)mWidth * (size_t)mHeight;
        if (mBuiltClearanceComponentVersion[(size_t)ClampedR] == mVersion &&
            mClearanceComponent[(size_t)ClampedR].size() == Count)
        {
            return;
        }
        RebuildClearanceComponents(ClampedR);
    }

    int GetClearanceComponent(int X, int Y, int Radius) const
    {
        if (!IsInBounds(X, Y))
            return 0;
        const int ClampedR = std::clamp(Radius, 0, MaxCachedClearanceRadius);
        EnsureClearanceComponents(ClampedR);
        return mClearanceComponent[(size_t)ClampedR][CellIndex(X, Y)];
    }

    const std::vector<NavigationObstacle>& GetObstacles() const
    {
        return mObstacles;
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
        RebuildClearanceField();
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

        const int LeftCells = Footprint.CellLeft();
        const int RightCells = Footprint.CellRight();
        const int UpCells = Footprint.CellUp();
        const int DownCells = Footprint.CellDown();
        const int MinimumX = CenterX - LeftCells;
        const int MaximumX = CenterX + RightCells;
        const int MinimumY = CenterY - UpCells;
        const int MaximumY = CenterY + DownCells;

        const bool OverlapsTempObstacle =
            mHasTempObstacle &&
            MaximumX >= mTempMinX && MinimumX <= mTempMaxX &&
            MaximumY >= mTempMinY && MinimumY <= mTempMaxY;

        const int MaxRadius = std::max({LeftCells, RightCells, UpCells, DownCells});
        if (!OverlapsTempObstacle && !mPassableMask.empty())
        {
            // When the static footprint of radius MaxRadius is completely unblocked
            // and height-valid, CanOccupy is guaranteed true even if an owner is ignored.
            if (MaxRadius <= MaxCachedClearanceRadius &&
                (mPassableMask[CenterIndex] & (uint8_t)(1u << MaxRadius)) != 0)
            {
                return true;
            }
            if (MaxRadius > MaxCachedClearanceRadius &&
                !mClearance.empty() &&
                (int)mClearance[CenterIndex] > MaxRadius)
            {
                return true;
            }
            // When no owners are ignored, if even the inscribed radius MinRadius fails
            // static passability, the footprint cannot occupy this cell.
            if (IgnoredOwnerEntityID < 0 && AdditionalIgnoredOwnerEntityID < 0)
            {
                const int MinRadius = std::min({LeftCells, RightCells, UpCells, DownCells});
                if (MinRadius <= MaxCachedClearanceRadius &&
                    (mPassableMask[CenterIndex] & (uint8_t)(1u << MinRadius)) == 0)
                {
                    return false;
                }
            }
        }

        const int CenterOwner = EffectiveOwnerAt(CenterX, CenterY, CenterIndex);
        if (CenterOwner != -1 &&
            CenterOwner != IgnoredOwnerEntityID &&
            CenterOwner != AdditionalIgnoredOwnerEntityID)
        {
            return false;
        }

        if (MinimumX == MaximumX && MinimumY == MaximumY)
            return true;

        for (int Y = MinimumY; Y <= MaximumY; ++Y)
        {
            for (int X = MinimumX; X <= MaximumX; ++X)
            {
                if (X == CenterX && Y == CenterY)
                    continue;

                if (!IsInBounds(X, Y))
                    return false;
                const size_t Idx = CellIndex(X, Y);
                if (mWalkable[Idx] == 0)
                    return false;
                const int Owner = EffectiveOwnerAt(X, Y, Idx);
                if (Owner != -1 &&
                    Owner != IgnoredOwnerEntityID &&
                    Owner != AdditionalIgnoredOwnerEntityID)
                    return false;

                if (!FootprintHeightAllowed(CenterX, CenterY, X, Y))
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
                if (!IsInBounds(X, Y))
                    return false;
                const size_t Idx = CellIndex(X, Y);
                if (mWalkable[Idx] == 0)
                    return false;
                const int Owner = EffectiveOwnerAt(X, Y, Idx);
                if (Owner != -1 &&
                    Owner != IgnoredOwnerEntityID &&
                    Owner != AdditionalIgnoredOwnerEntityID)
                    return false;

                if (!FootprintHeightAllowed(CenterX, CenterY, X, Y))
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
        if (!HeightStepAllowed(FromX, FromY, ToX, ToY))
            return false;
        if (AdditionalIgnoredOwnerEntityID >= 0)
        {
            const int FromCenterOwner = GetStaticOwner(FromX, FromY);
            const int ToCenterOwner = GetStaticOwner(ToX, ToY);
            if (FromCenterOwner != AdditionalIgnoredOwnerEntityID &&
                ToCenterOwner == AdditionalIgnoredOwnerEntityID)
            {
                return false;
            }
            const int FromOverlap = CountStaticOwnerFootprintOverlap(
                FromX, FromY, Footprint, AdditionalIgnoredOwnerEntityID);
            const int ToOverlap = CountStaticOwnerFootprintOverlap(
                ToX, ToY, Footprint, AdditionalIgnoredOwnerEntityID);
            if (ToOverlap > FromOverlap)
                return false;
        }
        return CanOccupy(
                   ToX, ToY, Footprint, IgnoredOwnerEntityID,
                   AdditionalIgnoredOwnerEntityID) ||
               CanOccupyWorld(
                   To, Footprint, IgnoredOwnerEntityID,
                   AdditionalIgnoredOwnerEntityID);
    }

    int CountStaticOwnerFootprintOverlap(int CenterX,
                                         int CenterY,
                                         const NavigationFootprint& Footprint,
                                         int OwnerEntityID) const
    {
        if (OwnerEntityID < 0 || !IsInBounds(CenterX, CenterY))
            return 0;

        const int LeftCells = Footprint.CellLeft();
        const int RightCells = Footprint.CellRight();
        const int UpCells = Footprint.CellUp();
        const int DownCells = Footprint.CellDown();
        const int MinimumX = CenterX - LeftCells;
        const int MaximumX = CenterX + RightCells;
        const int MinimumY = CenterY - UpCells;
        const int MaximumY = CenterY + DownCells;

        const bool OverlapsTempObstacle =
            mHasTempObstacle &&
            MaximumX >= mTempMinX && MinimumX <= mTempMaxX &&
            MaximumY >= mTempMinY && MinimumY <= mTempMaxY;
        const int MaxRadius = std::max({LeftCells, RightCells, UpCells, DownCells});
        const size_t CenterIndex = CellIndex(CenterX, CenterY);
        if (!OverlapsTempObstacle && !mPassableMask.empty() &&
            MaxRadius <= MaxCachedClearanceRadius &&
            (mPassableMask[CenterIndex] & (uint8_t)(1u << MaxRadius)) != 0)
        {
            return 0;
        }

        int OverlapCount = 0;
        for (int Y = MinimumY; Y <= MaximumY; ++Y)
        {
            for (int X = MinimumX; X <= MaximumX; ++X)
            {
                if (IsInBounds(X, Y) &&
                    EffectiveOwnerAt(X, Y, CellIndex(X, Y)) == OwnerEntityID)
                {
                    ++OverlapCount;
                }
            }
        }
        return OverlapCount;
    }

    bool CanTraverse(int FromX, int FromY, int ToX, int ToY,
                     const NavigationFootprint& Footprint,
                     int IgnoredOwnerEntityID = -1,
                     int AdditionalIgnoredOwnerEntityID = -1) const
    {
        if (!IsInBounds(ToX, ToY) || !HeightStepAllowed(FromX, FromY, ToX, ToY))
            return false;

        const int LeftCells = Footprint.CellLeft();
        const int RightCells = Footprint.CellRight();
        const int UpCells = Footprint.CellUp();
        const int DownCells = Footprint.CellDown();
        const int MaxRadius = std::max({LeftCells, RightCells, UpCells, DownCells});
        const bool ToOverlapsTemp =
            mHasTempObstacle &&
            (ToX + RightCells) >= mTempMinX && (ToX - LeftCells) <= mTempMaxX &&
            (ToY + DownCells) >= mTempMinY && (ToY - UpCells) <= mTempMaxY;
        const size_t ToIndex = CellIndex(ToX, ToY);
        if (!ToOverlapsTemp && !mPassableMask.empty() &&
            MaxRadius <= MaxCachedClearanceRadius &&
            (mPassableMask[ToIndex] & (uint8_t)(1u << MaxRadius)) != 0)
        {
            return true;
        }

        if (AdditionalIgnoredOwnerEntityID >= 0)
        {
            const int FromCenterOwner = GetStaticOwner(FromX, FromY);
            const int ToCenterOwner = GetStaticOwner(ToX, ToY);
            if (FromCenterOwner != AdditionalIgnoredOwnerEntityID &&
                ToCenterOwner == AdditionalIgnoredOwnerEntityID)
            {
                return false;
            }
            const int FromOverlap = CountStaticOwnerFootprintOverlap(
                FromX, FromY, Footprint, AdditionalIgnoredOwnerEntityID);
            const int ToOverlap = CountStaticOwnerFootprintOverlap(
                ToX, ToY, Footprint, AdditionalIgnoredOwnerEntityID);
            if (ToOverlap > FromOverlap)
                return false;
        }
        return CanOccupy(
            ToX, ToY, Footprint, IgnoredOwnerEntityID,
            AdditionalIgnoredOwnerEntityID);
    }

    bool HasLineOfSightCells(int X0, int Y0, int X1, int Y1,
                             const NavigationFootprint& Footprint,
                             int IgnoredOwnerEntityID = -1,
                             int AdditionalIgnoredOwnerEntityID = -1) const
    {
        if (!IsInBounds(X0, Y0) || !IsInBounds(X1, Y1))
            return false;
        int DX = std::abs(X1 - X0);
        int DY = std::abs(Y1 - Y0);
        int SX = (X0 < X1) ? 1 : -1;
        int SY = (Y0 < Y1) ? 1 : -1;
        int Err = DX - DY;
        int CX = X0, CY = Y0;
        while (CX != X1 || CY != Y1)
        {
            const int PrevX = CX;
            const int PrevY = CY;
            int E2 = Err * 2;
            if (E2 > -DY) { Err -= DY; CX += SX; }
            if (E2 < DX) { Err += DX; CY += SY; }
            if (!CanTraverse(PrevX, PrevY, CX, CY, Footprint,
                             IgnoredOwnerEntityID, AdditionalIgnoredOwnerEntityID))
            {
                return false;
            }
            if (CX != PrevX && CY != PrevY)
            {
                if (!CanTraverse(PrevX, PrevY, CX, PrevY, Footprint,
                                 IgnoredOwnerEntityID, AdditionalIgnoredOwnerEntityID) ||
                    !CanTraverse(PrevX, PrevY, PrevX, CY, Footprint,
                                 IgnoredOwnerEntityID, AdditionalIgnoredOwnerEntityID))
                {
                    return false;
                }
            }
        }
        return true;
    }

    bool HasLineOfSightWorld(const Core::Math::Vec2& From,
                             const Core::Math::Vec2& To,
                             const NavigationFootprint& Footprint,
                             int IgnoredOwnerEntityID = -1,
                             int AdditionalIgnoredOwnerEntityID = -1) const
    {
        int X0 = 0, Y0 = 0, X1 = 0, Y1 = 0;
        WorldToCell(From, X0, Y0);
        WorldToCell(To, X1, Y1);
        return HasLineOfSightCells(
            X0, Y0, X1, Y1, Footprint,
            IgnoredOwnerEntityID, AdditionalIgnoredOwnerEntityID);
    }

    static int WorldPixelToCell(int Pixel)
    {
        if (Pixel >= 0)
            return Pixel / NavigationCellSize;
        return -((-Pixel + NavigationCellSize - 1) / NavigationCellSize);
    }

private:
    int EffectiveOwnerAt(int X, int Y, size_t Index) const
    {
        int Owner = mStaticOwner[Index];
        if (mHasTempObstacle &&
            X >= mTempMinX && X <= mTempMaxX &&
            Y >= mTempMinY && Y <= mTempMaxY)
        {
            if (Owner == -1)
                Owner = mTempOwnerID;
            else if (Owner != mTempOwnerID)
                Owner = -2;
        }
        return Owner;
    }

    int EffectiveOwnerAt(int X, int Y) const
    {
        return EffectiveOwnerAt(X, Y, CellIndex(X, Y));
    }

    bool HeightStepAllowed(int FromX, int FromY, int ToX, int ToY) const
    {
        if (!IsInBounds(FromX, FromY) || !IsInBounds(ToX, ToY))
            return true;
        const int FromHeight = (int)mHeightLevel[CellIndex(FromX, FromY)];
        const int ToHeight = (int)mHeightLevel[CellIndex(ToX, ToY)];
        if (FromHeight == ToHeight)
            return true;
        if (std::abs(FromHeight - ToHeight) > 1)
            return false;
        if (!mHasExplicitRamps)
            return true;
        return IsRamp(FromX, FromY) || IsRamp(ToX, ToY);
    }

    bool FootprintHeightAllowed(int CenterX, int CenterY, int X, int Y) const
    {
        if (HeightStepAllowed(CenterX, CenterY, X, Y))
            return true;
        if (!IsInBounds(CenterX, CenterY) || !IsInBounds(X, Y))
            return true;
        const int FromHeight = (int)mHeightLevel[CellIndex(CenterX, CenterY)];
        const int ToHeight = (int)mHeightLevel[CellIndex(X, Y)];
        if (std::abs(FromHeight - ToHeight) > 1)
            return false;

        int CurX = CenterX;
        int CurY = CenterY;
        while (CurX != X || CurY != Y)
        {
            const int NextX = CurX + (X > CurX ? 1 : (X < CurX ? -1 : 0));
            const int NextY = CurY + (Y > CurY ? 1 : (Y < CurY ? -1 : 0));
            if (!HeightStepAllowed(CurX, CurY, NextX, NextY))
                return false;
            CurX = NextX;
            CurY = NextY;
        }
        return true;
    }

    void RebuildTerrainComponents() const
    {
        const size_t Count = (size_t)mWidth * (size_t)mHeight;
        mTerrainComponent.assign(Count, 0);
        mBuiltTerrainComponentVersion = mTerrainVersion;
        if (Count == 0 || mWidth <= 0 || mHeight <= 0)
            return;

        std::vector<int> Queue;
        Queue.reserve(Count / 2);
        int NextCompID = 1;
        static constexpr int DX[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
        static constexpr int DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };

        for (int Y = 0; Y < mHeight; ++Y)
        {
            for (int X = 0; X < mWidth; ++X)
            {
                const size_t StartIdx = CellIndex(X, Y);
                if (mTerrainComponent[StartIdx] != 0 || mWalkable[StartIdx] == 0)
                    continue;

                const int CompID = NextCompID++;
                mTerrainComponent[StartIdx] = CompID;
                Queue.clear();
                Queue.push_back((int)StartIdx);
                for (size_t Head = 0; Head < Queue.size(); ++Head)
                {
                    const int CurIdx = Queue[Head];
                    const int CX = CurIdx % mWidth;
                    const int CY = CurIdx / mWidth;
                    for (int Dir = 0; Dir < 8; ++Dir)
                    {
                        const int NX = CX + DX[Dir];
                        const int NY = CY + DY[Dir];
                        if (!IsInBounds(NX, NY))
                            continue;
                        const size_t NIdx = CellIndex(NX, NY);
                        if (mTerrainComponent[NIdx] != 0 || mWalkable[NIdx] == 0)
                            continue;
                        if (!HeightStepAllowed(CX, CY, NX, NY))
                            continue;
                        if (Dir >= 4)
                        {
                            const int CardX = CX + DX[Dir];
                            const int CardY = CY + DY[Dir];
                            if (mWalkable[CellIndex(CardX, CY)] == 0 ||
                                !HeightStepAllowed(CX, CY, CardX, CY) ||
                                mWalkable[CellIndex(CX, CardY)] == 0 ||
                                !HeightStepAllowed(CX, CY, CX, CardY))
                            {
                                continue;
                            }
                        }
                        mTerrainComponent[NIdx] = CompID;
                        Queue.push_back((int)NIdx);
                    }
                }
            }
        }
    }

    void RebuildClearanceComponents(int Radius) const
    {
        const int ClampedR = std::clamp(Radius, 0, MaxCachedClearanceRadius);
        const size_t Count = (size_t)mWidth * (size_t)mHeight;
        auto& Comp = mClearanceComponent[(size_t)ClampedR];
        Comp.assign(Count, 0);
        mBuiltClearanceComponentVersion[(size_t)ClampedR] = mVersion;
        if (Count == 0 || mWidth <= 0 || mHeight <= 0 || mPassableMask.size() != Count)
            return;

        const uint8_t Bit = (uint8_t)(1u << ClampedR);
        std::vector<int> Queue;
        Queue.reserve(Count / 2);
        int NextCompID = 1;
        static constexpr int DX[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
        static constexpr int DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };

        for (int Y = 0; Y < mHeight; ++Y)
        {
            for (int X = 0; X < mWidth; ++X)
            {
                const size_t StartIdx = CellIndex(X, Y);
                if (Comp[StartIdx] != 0 || (mPassableMask[StartIdx] & Bit) == 0)
                    continue;

                const int CompID = NextCompID++;
                Comp[StartIdx] = CompID;
                Queue.clear();
                Queue.push_back((int)StartIdx);
                for (size_t Head = 0; Head < Queue.size(); ++Head)
                {
                    const int CurIdx = Queue[Head];
                    const int CX = CurIdx % mWidth;
                    const int CY = CurIdx / mWidth;
                    for (int Dir = 0; Dir < 8; ++Dir)
                    {
                        const int NX = CX + DX[Dir];
                        const int NY = CY + DY[Dir];
                        if (!IsInBounds(NX, NY))
                            continue;
                        const size_t NIdx = CellIndex(NX, NY);
                        if (Comp[NIdx] != 0 || (mPassableMask[NIdx] & Bit) == 0)
                            continue;
                        if (!HeightStepAllowed(CX, CY, NX, NY))
                            continue;
                        if (Dir >= 4)
                        {
                            const int CardX = CX + DX[Dir];
                            const int CardY = CY + DY[Dir];
                            if ((mPassableMask[CellIndex(CardX, CY)] & Bit) == 0 ||
                                !HeightStepAllowed(CX, CY, CardX, CY) ||
                                (mPassableMask[CellIndex(CX, CardY)] & Bit) == 0 ||
                                !HeightStepAllowed(CX, CY, CX, CardY))
                            {
                                continue;
                            }
                        }
                        Comp[NIdx] = CompID;
                        Queue.push_back((int)NIdx);
                    }
                }
            }
        }
    }

    void RebuildClearanceField()
    {
        const size_t Count = (size_t)mWidth * (size_t)mHeight;
        if (mClearance.size() != Count)
            mClearance.assign(Count, 0);
        if (mPassableMask.size() != Count)
            mPassableMask.assign(Count, 0);
        if (mWidth <= 0 || mHeight <= 0)
            return;

        constexpr uint8_t kMaxClearance = 16;
        // Pass 1: Forward raster (top-left to bottom-right)
        for (int Y = 0; Y < mHeight; ++Y)
        {
            for (int X = 0; X < mWidth; ++X)
            {
                const size_t Idx = CellIndex(X, Y);
                if (mWalkable[Idx] == 0 || mStaticOwner[Idx] != -1)
                {
                    mClearance[Idx] = 0;
                    continue;
                }
                uint8_t Dist = kMaxClearance;
                if (X > 0 && HeightStepAllowed(X, Y, X - 1, Y)) Dist = std::min(Dist, mClearance[CellIndex(X - 1, Y)]);
                else Dist = 0;
                if (Y > 0 && HeightStepAllowed(X, Y, X, Y - 1)) Dist = std::min(Dist, mClearance[CellIndex(X, Y - 1)]);
                else Dist = 0;
                if (X > 0 && Y > 0 && HeightStepAllowed(X, Y, X - 1, Y - 1)) Dist = std::min(Dist, mClearance[CellIndex(X - 1, Y - 1)]);
                else Dist = 0;
                if (X + 1 < mWidth && Y > 0 && HeightStepAllowed(X, Y, X + 1, Y - 1)) Dist = std::min(Dist, mClearance[CellIndex(X + 1, Y - 1)]);
                else Dist = 0;

                mClearance[Idx] = std::min<uint8_t>(kMaxClearance, (uint8_t)(Dist + 1));
            }
        }
        // Pass 2: Backward raster (bottom-right to top-left)
        for (int Y = mHeight - 1; Y >= 0; --Y)
        {
            for (int X = mWidth - 1; X >= 0; --X)
            {
                const size_t Idx = CellIndex(X, Y);
                if (mClearance[Idx] == 0)
                    continue;

                uint8_t Dist = mClearance[Idx];
                if (X + 1 < mWidth && HeightStepAllowed(X, Y, X + 1, Y)) Dist = std::min(Dist, (uint8_t)(mClearance[CellIndex(X + 1, Y)] + 1));
                else Dist = std::min<uint8_t>(Dist, 1);
                if (Y + 1 < mHeight && HeightStepAllowed(X, Y, X, Y + 1)) Dist = std::min(Dist, (uint8_t)(mClearance[CellIndex(X, Y + 1)] + 1));
                else Dist = std::min<uint8_t>(Dist, 1);
                if (X + 1 < mWidth && Y + 1 < mHeight && HeightStepAllowed(X, Y, X + 1, Y + 1)) Dist = std::min(Dist, (uint8_t)(mClearance[CellIndex(X + 1, Y + 1)] + 1));
                else Dist = std::min<uint8_t>(Dist, 1);
                if (X > 0 && Y + 1 < mHeight && HeightStepAllowed(X, Y, X - 1, Y + 1)) Dist = std::min(Dist, (uint8_t)(mClearance[CellIndex(X - 1, Y + 1)] + 1));
                else Dist = std::min<uint8_t>(Dist, 1);

                mClearance[Idx] = Dist;
            }
        }

        // Pass 3: Populate exact per-radius static passability bitmask (R = 0..3)
        for (int Y = 0; Y < mHeight; ++Y)
        {
            for (int X = 0; X < mWidth; ++X)
            {
                const size_t Idx = CellIndex(X, Y);
                const uint8_t Clear = mClearance[Idx];
                if (Clear == 0)
                {
                    mPassableMask[Idx] = 0;
                    continue;
                }
                if (Clear > MaxCachedClearanceRadius)
                {
                    mPassableMask[Idx] = 0x0Fu;
                    continue;
                }
                uint8_t Mask = 1u;
                const int GuaranteedR = (int)Clear - 1;
                for (int R = 1; R <= GuaranteedR && R <= MaxCachedClearanceRadius; ++R)
                    Mask |= (uint8_t)(1u << R);
                for (int R = std::max(1, GuaranteedR + 1); R <= MaxCachedClearanceRadius; ++R)
                {
                    if (X - R < 0 || X + R >= mWidth || Y - R < 0 || Y + R >= mHeight)
                        break;
                    bool RingOk = true;
                    for (int DY = -R; DY <= R && RingOk; ++DY)
                    {
                        for (int DX = -R; DX <= R; ++DX)
                        {
                            if (std::abs(DX) != R && std::abs(DY) != R)
                                continue;
                            const int NX = X + DX;
                            const int NY = Y + DY;
                            const size_t NIdx = CellIndex(NX, NY);
                            if (mWalkable[NIdx] == 0 || mStaticOwner[NIdx] != -1 ||
                                !FootprintHeightAllowed(X, Y, NX, NY))
                            {
                                RingOk = false;
                                break;
                            }
                        }
                    }
                    if (!RingOk)
                        break;
                    Mask |= (uint8_t)(1u << R);
                }
                mPassableMask[Idx] = Mask;
            }
        }
    }

    size_t CellIndex(int X, int Y) const
    {
        return (size_t)Y * (size_t)mWidth + (size_t)X;
    }

    int mWidth = 0;
    int mHeight = 0;
    uint32_t mVersion = 0;
    uint32_t mTerrainVersion = 1;
    mutable uint32_t mBuiltTerrainComponentVersion = 0;
    mutable std::array<uint32_t, MaxCachedClearanceRadius + 1> mBuiltClearanceComponentVersion{};
    std::vector<uint8_t> mWalkable;
    std::vector<uint8_t> mHeightLevel;
    std::vector<uint8_t> mIsRamp;
    bool mHasExplicitRamps = false;
    std::vector<int> mStaticOwner;
    std::vector<uint8_t> mClearance;
    std::vector<uint8_t> mPassableMask;
    mutable std::vector<int> mTerrainComponent;
    mutable std::array<std::vector<int>, MaxCachedClearanceRadius + 1> mClearanceComponent;
    std::vector<NavigationObstacle> mObstacles;
    bool mHasTempObstacle = false;
    int mTempOwnerID = -1;
    int mTempMinX = 0;
    int mTempMaxX = -1;
    int mTempMinY = 0;
    int mTempMaxY = -1;
};

} // namespace Game::Pathfinding
