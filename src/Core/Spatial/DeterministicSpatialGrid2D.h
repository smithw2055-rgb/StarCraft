#pragma once

#include "Core/Math/Vec2.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Core::Spatial
{

// ============================================================================
// DeterministicSpatialGrid2D: Flat arena + CSR (Compressed Sparse Row) spatial hash
// - Replaces vector-of-vectors pointer chasing with flat contiguous arrays
// - Preserves exact insertion order per cell for 100% RTS lockstep determinism
// - Supports both dynamic incremental queries (via intrusive flat linked list)
//   and finalized CSR contiguous slice queries (FinalizeCSR())
// ============================================================================
template <int TCellSize = 64, int TGridOffset = 32, int TGridDim = 256>
class DeterministicSpatialGrid2D
{
public:
    static constexpr int CellSize = TCellSize;
    static constexpr int GridOffset = TGridOffset;
    static constexpr int GridDim = TGridDim;

    DeterministicSpatialGrid2D()
    {
        mCellSlotIndex.fill(-1);
        mActiveCells.reserve(512);
        mUsedGridIndices.reserve(512);
        mNodes.reserve(1024);
        mCsrEntries.reserve(1024);
    }

    void Clear()
    {
        for (const uint32_t GridIdx : mUsedGridIndices)
        {
            mCellSlotIndex[GridIdx] = -1;
        }
        mUsedGridIndices.clear();
        mActiveCells.clear();
        mNodes.clear();
        mCsrEntries.clear();
        mFallbackBuckets.clear();
        mCsrBuilt = false;
    }

    bool Empty() const
    {
        return mNodes.empty() && mFallbackBuckets.empty();
    }

    void Insert(int EntityID, const Core::Math::Vec2& Position)
    {
        mCsrBuilt = false;
        const int CellX = ToCell(Position.X);
        const int CellY = ToCell(Position.Y);
        const int GridX = CellX + GridOffset;
        const int GridY = CellY + GridOffset;

        if (static_cast<unsigned>(GridX) < static_cast<unsigned>(GridDim) &&
            static_cast<unsigned>(GridY) < static_cast<unsigned>(GridDim))
        {
            const uint32_t GridIdx = static_cast<uint32_t>(GridY * GridDim + GridX);
            const int32_t NodeIdx = static_cast<int32_t>(mNodes.size());
            mNodes.push_back({EntityID, -1});

            int32_t SlotIdx = mCellSlotIndex[GridIdx];
            if (SlotIdx < 0)
            {
                SlotIdx = static_cast<int32_t>(mActiveCells.size());
                mCellSlotIndex[GridIdx] = SlotIdx;
                mActiveCells.push_back({NodeIdx, NodeIdx, 0u, 1u});
                mUsedGridIndices.push_back(GridIdx);
            }
            else
            {
                ActiveCell& Cell = mActiveCells[static_cast<size_t>(SlotIdx)];
                mNodes[static_cast<size_t>(Cell.Tail)].Next = NodeIdx;
                Cell.Tail = NodeIdx;
                ++Cell.Count;
            }
            return;
        }

        mFallbackBuckets[CellKey(CellX, CellY)].push_back(EntityID);
    }

    // Compacts all cell linked lists into a single contiguous CSR array (mCsrEntries)
    // so subsequent QueryInto calls copy contiguous cache lines per cell.
    void FinalizeCSR()
    {
        if (mCsrBuilt)
            return;

        mCsrEntries.resize(mNodes.size());
        uint32_t RunningOffset = 0;
        for (ActiveCell& Cell : mActiveCells)
        {
            Cell.CsrOffset = RunningOffset;
            uint32_t WriteCursor = RunningOffset;
            int32_t Cur = Cell.Head;
            while (Cur >= 0)
            {
                const Node& N = mNodes[static_cast<size_t>(Cur)];
                mCsrEntries[WriteCursor++] = N.EntityID;
                Cur = N.Next;
            }
            RunningOffset = WriteCursor;
        }
        mCsrBuilt = true;
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
            const bool ValidY = static_cast<unsigned>(GridY) < static_cast<unsigned>(GridDim);
            for (int X = CellMinimumX; X <= CellMaximumX; ++X)
            {
                const int GridX = X + GridOffset;
                if (ValidY && static_cast<unsigned>(GridX) < static_cast<unsigned>(GridDim))
                {
                    const int32_t SlotIdx = mCellSlotIndex[static_cast<size_t>(GridY * GridDim + GridX)];
                    if (SlotIdx < 0)
                        continue;

                    const ActiveCell& Cell = mActiveCells[static_cast<size_t>(SlotIdx)];
                    if (mCsrBuilt)
                    {
                        const int* BasePtr = mCsrEntries.data() + Cell.CsrOffset;
                        Result.insert(Result.end(), BasePtr, BasePtr + Cell.Count);
                    }
                    else
                    {
                        int32_t Cur = Cell.Head;
                        while (Cur >= 0)
                        {
                            const Node& N = mNodes[static_cast<size_t>(Cur)];
                            Result.push_back(N.EntityID);
                            Cur = N.Next;
                        }
                    }
                }
                else if (HasFallback)
                {
                    const auto It = mFallbackBuckets.find(CellKey(X, Y));
                    if (It != mFallbackBuckets.end())
                    {
                        Result.insert(Result.end(), It->second.begin(), It->second.end());
                    }
                }
            }
        }
    }

    size_t GetActiveCellCount() const { return mActiveCells.size(); }
    size_t GetInsertedEntityCount() const { return mNodes.size(); }
    bool IsCSRFinalized() const { return mCsrBuilt; }

private:
    struct Node
    {
        int EntityID = -1;
        int32_t Next = -1;
    };

    struct ActiveCell
    {
        int32_t Head = -1;
        int32_t Tail = -1;
        uint32_t CsrOffset = 0;
        uint32_t Count = 0;
    };

    static uint64_t CellKey(int X, int Y)
    {
        return static_cast<uint64_t>(static_cast<uint32_t>(X)) |
               (static_cast<uint64_t>(static_cast<uint32_t>(Y)) << 32);
    }

    static int ToCell(float Coordinate)
    {
        return static_cast<int>(std::floor(Coordinate / static_cast<float>(CellSize)));
    }

    std::array<int32_t, GridDim * GridDim> mCellSlotIndex;
    std::vector<uint32_t> mUsedGridIndices;
    std::vector<ActiveCell> mActiveCells;
    std::vector<Node> mNodes;
    std::vector<int> mCsrEntries;
    std::unordered_map<uint64_t, std::vector<int>> mFallbackBuckets;
    bool mCsrBuilt = false;
};

} // namespace Core::Spatial
