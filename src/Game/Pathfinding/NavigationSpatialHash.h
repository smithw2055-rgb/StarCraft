#pragma once

#include <algorithm>
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

    void Clear()
    {
        mBuckets.clear();
    }

    bool Empty() const
    {
        return mBuckets.empty();
    }

    void Insert(int EntityID, const Core::Math::Vec2& Position)
    {
        mBuckets[CellKey(
            ToCell(Position.X), ToCell(Position.Y))].push_back(EntityID);
    }

    void QueryInto(float MinimumX, float MinimumY,
                   float MaximumX, float MaximumY,
                   std::vector<int>& Result) const
    {
        Result.clear();
        const int CellMinimumX = ToCell(MinimumX);
        const int CellMinimumY = ToCell(MinimumY);
        const int CellMaximumX = ToCell(MaximumX);
        const int CellMaximumY = ToCell(MaximumY);
        for (int Y = CellMinimumY; Y <= CellMaximumY; ++Y)
        {
            for (int X = CellMinimumX; X <= CellMaximumX; ++X)
            {
                const auto It = mBuckets.find(CellKey(X, Y));
                if (It == mBuckets.end())
                    continue;
                Result.insert(
                    Result.end(), It->second.begin(), It->second.end());
            }
        }
        // Each entity is inserted into exactly one bucket, buckets receive
        // entities in ascending ID order, and queries visit cells in fixed
        // Y/X order.  The result is therefore already unique and
        // deterministic; globally sorting every per-unit query dominated
        // dense-formation movement time.
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

    std::unordered_map<uint64_t, std::vector<int>> mBuckets;
};

} // namespace Game::Pathfinding
