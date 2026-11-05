#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Game::Pathfinding
{

class NavigationSpatialHash
{
public:
    static constexpr int CellSize = 64;
    static constexpr int GridOffset = 32;
    static constexpr int GridDim = 256;

    NavigationSpatialHash()
    {
        mCellBucketIndex.fill(-1);
    }

    void Clear()
    {
        for (const uint32_t GridIdx : mUsedCells)
            mCellBucketIndex[GridIdx] = -1;
        mUsedCells.clear();
        mUsedBucketCount = 0;
        mFallbackBuckets.clear();
    }

    bool Empty() const
    {
        return mUsedBucketCount == 0 && mFallbackBuckets.empty();
    }

    void Insert(int EntityID, const Core::Math::Vec2& Position)
    {
        const int CellX = ToCell(Position.X);
        const int CellY = ToCell(Position.Y);
        const int GridX = CellX + GridOffset;
        const int GridY = CellY + GridOffset;
        if ((unsigned)GridX < (unsigned)GridDim &&
            (unsigned)GridY < (unsigned)GridDim)
        {
            const uint32_t GridIdx =
                (uint32_t)(GridY * GridDim + GridX);
            int32_t BucketIdx = mCellBucketIndex[GridIdx];
            if (BucketIdx < 0)
            {
                BucketIdx = (int32_t)mUsedBucketCount++;
                if ((size_t)BucketIdx >= mBucketPool.size())
                    mBucketPool.emplace_back();
                else
                    mBucketPool[(size_t)BucketIdx].clear();
                mCellBucketIndex[GridIdx] = BucketIdx;
                mUsedCells.push_back(GridIdx);
            }
            mBucketPool[(size_t)BucketIdx].push_back(EntityID);
            return;
        }
        mFallbackBuckets[CellKey(CellX, CellY)].push_back(EntityID);
    }

    void QueryInto(float MinimumX, float MinimumY,
                   float MaximumX, float MaximumY,
                   std::vector<int>& Result) const
    {
        Result.clear();
        if (Empty())
            return;

        const int CellMinimumX = ToCell(MinimumX);
        const int CellMinimumY = ToCell(MinimumY);
        const int CellMaximumX = ToCell(MaximumX);
        const int CellMaximumY = ToCell(MaximumY);
        const bool HasFallback = !mFallbackBuckets.empty();
        for (int Y = CellMinimumY; Y <= CellMaximumY; ++Y)
        {
            const int GridY = Y + GridOffset;
            const bool ValidY = (unsigned)GridY < (unsigned)GridDim;
            for (int X = CellMinimumX; X <= CellMaximumX; ++X)
            {
                const int GridX = X + GridOffset;
                if (ValidY && (unsigned)GridX < (unsigned)GridDim)
                {
                    const int32_t BucketIdx =
                        mCellBucketIndex[(size_t)(GridY * GridDim + GridX)];
                    if (BucketIdx >= 0)
                    {
                        const auto& Bucket = mBucketPool[(size_t)BucketIdx];
                        Result.insert(
                            Result.end(), Bucket.begin(), Bucket.end());
                    }
                }
                else if (HasFallback)
                {
                    const auto It = mFallbackBuckets.find(CellKey(X, Y));
                    if (It != mFallbackBuckets.end())
                    {
                        Result.insert(
                            Result.end(), It->second.begin(), It->second.end());
                    }
                }
            }
        }
    }

private:
    static uint64_t CellKey(int X, int Y)
    {
        return (uint64_t)(uint32_t)X |
               ((uint64_t)(uint32_t)Y << 32);
    }

    static int ToCell(float Coordinate)
    {
        return (int)std::floor(Coordinate / (float)CellSize);
    }

    std::array<int32_t, GridDim * GridDim> mCellBucketIndex;
    std::vector<uint32_t> mUsedCells;
    std::vector<std::vector<int>> mBucketPool;
    size_t mUsedBucketCount = 0;
    std::unordered_map<uint64_t, std::vector<int>> mFallbackBuckets;
};

} // namespace Game::Pathfinding

