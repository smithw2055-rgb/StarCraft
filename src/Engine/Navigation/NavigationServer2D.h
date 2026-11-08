#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
#include <utility>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Engine::Navigation
{

struct GridCoord2D
{
    int32_t X = 0;
    int32_t Y = 0;

    constexpr bool operator==(const GridCoord2D& Other) const noexcept
    {
        return X == Other.X && Y == Other.Y;
    }
    constexpr bool operator!=(const GridCoord2D& Other) const noexcept
    {
        return !(*this == Other);
    }
};

struct FlowField2D
{
    int32_t Width = 0;
    int32_t Height = 0;
    GridCoord2D Goal{0, 0};
    uint8_t FootprintClearance = 1;
    std::vector<uint32_t> IntegrationCost;
    std::vector<Core::Math::Vec2> FlowVectors;

    static constexpr uint32_t kUnreachableCost = std::numeric_limits<uint32_t>::max();

    bool IsReachable(int32_t X, int32_t Y) const noexcept
    {
        if (X < 0 || Y < 0 || X >= Width || Y >= Height)
            return false;
        return IntegrationCost[static_cast<size_t>(Y) * static_cast<size_t>(Width) + static_cast<size_t>(X)] != kUnreachableCost;
    }

    Core::Math::Vec2 SampleFlow(int32_t X, int32_t Y) const noexcept
    {
        if (X < 0 || Y < 0 || X >= Width || Y >= Height)
            return {0.0f, 0.0f};
        return FlowVectors[static_cast<size_t>(Y) * static_cast<size_t>(Width) + static_cast<size_t>(X)];
    }
};

struct AvoidanceAgent2D
{
    int32_t EntityID = -1;
    Core::Math::Vec2 Position{0.0f, 0.0f};
    Core::Math::Vec2 Velocity{0.0f, 0.0f};
    float Radius = 12.0f;
};

// ============================================================================
// NavigationAgent2D (Phase 62 — Godot 4 NavigationAgent2D Equivalent):
// Stateful per-entity navigation agent that automatically queries
// NavigationServer2D for O(1) island connectivity, footprint-aware A* paths,
// waypoint progression, and local avoidance steering velocity.
// ============================================================================
struct NavigationAgent2D
{
    int32_t EntityID = -1;
    uint8_t FootprintClearance = 1;
    float Radius = 12.0f;
    float WaypointReachThresholdPx = 10.0f;
    Core::Math::Vec2 TargetWorldPosition{0.0f, 0.0f};
    std::vector<GridCoord2D> Waypoints;
    size_t CurrentWaypointIndex = 0;
    bool HasActiveTarget = false;
    bool PathNeedsRebuild = false;
    bool TargetReachable = false;
    bool ReachedTarget = true;
    Core::Math::Vec2 DesiredVelocity{0.0f, 0.0f};

    void SetTargetPosition(Core::Math::Vec2 NewTargetWorldPos) noexcept
    {
        TargetWorldPosition = NewTargetWorldPos;
        HasActiveTarget = true;
        PathNeedsRebuild = true;
        ReachedTarget = false;
    }

    void ClearTarget() noexcept
    {
        Waypoints.clear();
        CurrentWaypointIndex = 0;
        HasActiveTarget = false;
        PathNeedsRebuild = false;
        TargetReachable = false;
        ReachedTarget = true;
        DesiredVelocity = {0.0f, 0.0f};
    }
};

struct NavigationServerTelemetry
{
    uint64_t ClearanceRebuilds = 0;
    uint64_t RegionRebuilds = 0;
    uint64_t AStarQueriesExecuted = 0;
    uint64_t FlowFieldsGenerated = 0;
    uint64_t AvoidanceStepsComputed = 0;
    uint64_t AgentStepsEvaluated = 0;
};

// ============================================================================
// NavigationServer2D (Phase 40, Phase 47 & Phase 62 Unified Navigation Server):
// - General-purpose 2D RTS navigation server inspired by Godot NavigationServer2D
//   and classic RTS hierarchical/clearance pathfinding.
// - Converges NavigationGrid (elevation levels, ramps) & RegionPathfinder
//   (O(1) connected-component island reachability) into a single engine server:
//   1. True Clearance Field computation (supporting 1x1, 2x2, 3x3, 4x4 footprints).
//   2. Per-cell Elevation Levels, Ramp transitions, and O(W*H) Flood-Fill
//      Connected Component Region labeling (AreCellsConnected in O(1)).
//   3. Deterministic A* pathfinding with footprint clearance & cliff/ramp rules.
//   4. Goal-based Dijkstra Flow Field generation for large crowd steering.
//   5. Deterministic radial separation / local avoidance & NavigationAgent2D stepping.
// ============================================================================
class NavigationServer2D
{
public:
    void InitializeGrid(int32_t Width, int32_t Height, float CellSizePixels = 32.0f)
    {
        mWidth = std::max<int32_t>(1, Width);
        mHeight = std::max<int32_t>(1, Height);
        mCellSize = std::max(1.0f, CellSizePixels);
        const size_t Total = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
        mBlocked.assign(Total, 0u);
        mClearance.assign(Total, 1u);
        mElevation.assign(Total, 0u);
        mRamp.assign(Total, 0u);
        mRegionIDs.assign(Total, -1);
        mRegionCount = 0;
        RebuildClearanceField();
    }

    int32_t GetWidth() const noexcept { return mWidth; }
    int32_t GetHeight() const noexcept { return mHeight; }
    float GetCellSize() const noexcept { return mCellSize; }

    GridCoord2D WorldToCell(Core::Math::Vec2 WorldPos) const noexcept
    {
        const int32_t CX = std::clamp(
            static_cast<int32_t>(WorldPos.X / mCellSize), 0, std::max(0, mWidth - 1));
        const int32_t CY = std::clamp(
            static_cast<int32_t>(WorldPos.Y / mCellSize), 0, std::max(0, mHeight - 1));
        return {CX, CY};
    }

    Core::Math::Vec2 CellToWorldCenter(GridCoord2D Cell) const noexcept
    {
        return {
            (static_cast<float>(Cell.X) + 0.5f) * mCellSize,
            (static_cast<float>(Cell.Y) + 0.5f) * mCellSize};
    }

    bool InBounds(int32_t X, int32_t Y) const noexcept
    {
        return X >= 0 && Y >= 0 && X < mWidth && Y < mHeight;
    }

    void SetCellBlocked(int32_t X, int32_t Y, bool Blocked)
    {
        if (!InBounds(X, Y))
            return;
        mBlocked[CellIndex(X, Y)] = Blocked ? 1u : 0u;
        mClearanceDirty = true;
    }

    bool IsCellBlocked(int32_t X, int32_t Y) const noexcept
    {
        if (!InBounds(X, Y))
            return true;
        return mBlocked[CellIndex(X, Y)] != 0u;
    }

    void SetCellElevation(int32_t X, int32_t Y, uint8_t ElevationLevel, bool IsRamp = false)
    {
        if (!InBounds(X, Y))
            return;
        const size_t Idx = CellIndex(X, Y);
        mElevation[Idx] = ElevationLevel;
        mRamp[Idx] = IsRamp ? 1u : 0u;
    }

    uint8_t GetCellElevation(int32_t X, int32_t Y) const noexcept
    {
        if (!InBounds(X, Y))
            return 0u;
        return mElevation[CellIndex(X, Y)];
    }

    bool IsCellRamp(int32_t X, int32_t Y) const noexcept
    {
        if (!InBounds(X, Y))
            return false;
        return mRamp[CellIndex(X, Y)] != 0u;
    }

    // Checks whether a ground unit can step directly between two adjacent cells
    // considering elevation cliff differences unless bridged by a ramp cell.
    bool CanTraverseElevation(int32_t FromX, int32_t FromY, int32_t ToX, int32_t ToY) const noexcept
    {
        if (!InBounds(FromX, FromY) || !InBounds(ToX, ToY))
            return false;
        const size_t IdxA = CellIndex(FromX, FromY);
        const size_t IdxB = CellIndex(ToX, ToY);
        if (mElevation[IdxA] == mElevation[IdxB])
            return true;
        const int32_t Diff = std::abs(static_cast<int32_t>(mElevation[IdxA]) - static_cast<int32_t>(mElevation[IdxB]));
        if (Diff > 1)
            return false;
        return (mRamp[IdxA] != 0u) || (mRamp[IdxB] != 0u);
    }

    uint8_t GetCellClearance(int32_t X, int32_t Y)
    {
        if (mClearanceDirty)
            RebuildClearanceField();
        if (!InBounds(X, Y))
            return 0u;
        return mClearance[CellIndex(X, Y)];
    }

    // Dynamic programming true-clearance field computation in O(W * H)
    void RebuildClearanceField()
    {
        if (mWidth <= 0 || mHeight <= 0)
            return;

        for (int32_t Y = mHeight - 1; Y >= 0; --Y)
        {
            for (int32_t X = mWidth - 1; X >= 0; --X)
            {
                const size_t Idx = CellIndex(X, Y);
                if (mBlocked[Idx] != 0u)
                {
                    mClearance[Idx] = 0u;
                    continue;
                }
                if (X == mWidth - 1 || Y == mHeight - 1)
                {
                    mClearance[Idx] = 1u;
                }
                else
                {
                    const uint8_t Right = mClearance[CellIndex(X + 1, Y)];
                    const uint8_t Down = mClearance[CellIndex(X, Y + 1)];
                    const uint8_t Diag = mClearance[CellIndex(X + 1, Y + 1)];
                    const uint8_t MinNeighbor = std::min({Right, Down, Diag});
                    mClearance[Idx] = static_cast<uint8_t>(std::min<int>(16, static_cast<int>(MinNeighbor) + 1));
                }
            }
        }
        mClearanceDirty = false;
        ++mTelemetry.ClearanceRebuilds;
    }

    // Rebuilds connected walkable regions via flood-fill in O(W * H)
    uint32_t RebuildConnectedRegions(uint8_t FootprintSize = 1)
    {
        if (mClearanceDirty)
            RebuildClearanceField();

        const uint8_t ReqClearance = std::max<uint8_t>(1u, FootprintSize);
        const size_t Total = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
        mRegionIDs.assign(Total, -1);
        mRegionCount = 0;

        static constexpr int32_t kDirX[4] = {1, -1, 0, 0};
        static constexpr int32_t kDirY[4] = {0, 0, 1, -1};

        std::queue<GridCoord2D> Queue;
        for (int32_t Y = 0; Y < mHeight; ++Y)
        {
            for (int32_t X = 0; X < mWidth; ++X)
            {
                const size_t StartIdx = CellIndex(X, Y);
                if (mRegionIDs[StartIdx] != -1 || mClearance[StartIdx] < ReqClearance)
                    continue;

                const int32_t NewRegion = static_cast<int32_t>(mRegionCount++);
                mRegionIDs[StartIdx] = NewRegion;
                Queue.push({X, Y});

                while (!Queue.empty())
                {
                    const GridCoord2D Cur = Queue.front();
                    Queue.pop();

                    for (int D = 0; D < 4; ++D)
                    {
                        const int32_t NX = Cur.X + kDirX[D];
                        const int32_t NY = Cur.Y + kDirY[D];
                        if (!InBounds(NX, NY))
                            continue;
                        const size_t NIdx = CellIndex(NX, NY);
                        if (mRegionIDs[NIdx] != -1 || mClearance[NIdx] < ReqClearance)
                            continue;
                        if (!CanTraverseElevation(Cur.X, Cur.Y, NX, NY))
                            continue;

                        mRegionIDs[NIdx] = NewRegion;
                        Queue.push({NX, NY});
                    }
                }
            }
        }
        ++mTelemetry.RegionRebuilds;
        return mRegionCount;
    }

    int32_t GetCellRegionID(int32_t X, int32_t Y) const noexcept
    {
        if (!InBounds(X, Y))
            return -1;
        return mRegionIDs[CellIndex(X, Y)];
    }

    bool AreCellsConnected(GridCoord2D A, GridCoord2D B) const noexcept
    {
        const int32_t RegA = GetCellRegionID(A.X, A.Y);
        const int32_t RegB = GetCellRegionID(B.X, B.Y);
        return RegA >= 0 && RegA == RegB;
    }

    uint32_t GetConnectedRegionCount() const noexcept { return mRegionCount; }

    bool CanFitFootprint(int32_t X, int32_t Y, uint8_t FootprintSize)
    {
        if (mClearanceDirty)
            RebuildClearanceField();
        if (!InBounds(X, Y))
            return false;
        return mClearance[CellIndex(X, Y)] >= std::max<uint8_t>(1u, FootprintSize);
    }

    // Deterministic A* Pathfinding with Footprint Clearance & Elevation/Ramp rules
    std::vector<GridCoord2D> FindPath(GridCoord2D Start,
                                      GridCoord2D Goal,
                                      uint8_t FootprintSize = 1,
                                      uint32_t MaxExpandedNodes = 8192)
    {
        if (mClearanceDirty)
            RebuildClearanceField();
        ++mTelemetry.AStarQueriesExecuted;

        const uint8_t RequiredClearance = std::max<uint8_t>(1u, FootprintSize);
        if (!CanFitFootprint(Start.X, Start.Y, RequiredClearance) ||
            !CanFitFootprint(Goal.X, Goal.Y, RequiredClearance))
        {
            return {};
        }

        if (Start == Goal)
            return {Start};

        const size_t TotalCells = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
        std::vector<uint32_t> GScore(TotalCells, std::numeric_limits<uint32_t>::max());
        std::vector<int32_t> CameFrom(TotalCells, -1);
        std::vector<uint8_t> Closed(TotalCells, 0u);

        struct OpenEntry
        {
            uint32_t FScore = 0;
            int32_t Index = 0;
            bool operator>(const OpenEntry& Other) const noexcept
            {
                return FScore != Other.FScore ? (FScore > Other.FScore) : (Index > Other.Index);
            }
        };

        auto OctileHeuristic = [](GridCoord2D A, GridCoord2D B) -> uint32_t {
            const uint32_t DX = static_cast<uint32_t>(std::abs(A.X - B.X));
            const uint32_t DY = static_cast<uint32_t>(std::abs(A.Y - B.Y));
            const uint32_t MinD = std::min(DX, DY);
            const uint32_t MaxD = std::max(DX, DY);
            return 14u * MinD + 10u * (MaxD - MinD);
        };

        std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> OpenSet;
        const int32_t StartIdx = static_cast<int32_t>(CellIndex(Start.X, Start.Y));
        const int32_t GoalIdx = static_cast<int32_t>(CellIndex(Goal.X, Goal.Y));

        GScore[static_cast<size_t>(StartIdx)] = 0;
        OpenSet.push({OctileHeuristic(Start, Goal), StartIdx});

        static constexpr int32_t kDirX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static constexpr int32_t kDirY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        static constexpr uint32_t kStepCost[8] = {10, 10, 10, 10, 14, 14, 14, 14};

        uint32_t Expanded = 0;
        bool Found = false;

        while (!OpenSet.empty() && Expanded < MaxExpandedNodes)
        {
            const OpenEntry Current = OpenSet.top();
            OpenSet.pop();

            if (Current.Index == GoalIdx)
            {
                Found = true;
                break;
            }
            if (Closed[static_cast<size_t>(Current.Index)] != 0u)
                continue;
            Closed[static_cast<size_t>(Current.Index)] = 1u;
            ++Expanded;

            const int32_t CX = Current.Index % mWidth;
            const int32_t CY = Current.Index / mWidth;
            const uint32_t CurG = GScore[static_cast<size_t>(Current.Index)];

            for (int Dir = 0; Dir < 8; ++Dir)
            {
                const int32_t NX = CX + kDirX[Dir];
                const int32_t NY = CY + kDirY[Dir];
                if (!CanFitFootprint(NX, NY, RequiredClearance))
                    continue;
                if (!CanTraverseElevation(CX, CY, NX, NY))
                    continue;

                // Prevent diagonal corner cutting across blocked cells or cliffs
                if (Dir >= 4)
                {
                    if (!CanFitFootprint(CX + kDirX[Dir], CY, RequiredClearance) ||
                        !CanFitFootprint(CX, CY + kDirY[Dir], RequiredClearance) ||
                        !CanTraverseElevation(CX, CY, CX + kDirX[Dir], CY) ||
                        !CanTraverseElevation(CX, CY, CX, CY + kDirY[Dir]) ||
                        !CanTraverseElevation(CX + kDirX[Dir], CY, NX, NY) ||
                        !CanTraverseElevation(CX, CY + kDirY[Dir], NX, NY))
                    {
                        continue;
                    }
                }

                const int32_t NeighborIdx = static_cast<int32_t>(CellIndex(NX, NY));
                if (Closed[static_cast<size_t>(NeighborIdx)] != 0u)
                    continue;

                const uint32_t TentativeG = CurG + kStepCost[Dir];
                if (TentativeG < GScore[static_cast<size_t>(NeighborIdx)])
                {
                    CameFrom[static_cast<size_t>(NeighborIdx)] = Current.Index;
                    GScore[static_cast<size_t>(NeighborIdx)] = TentativeG;
                    const uint32_t F = TentativeG + OctileHeuristic({NX, NY}, Goal);
                    OpenSet.push({F, NeighborIdx});
                }
            }
        }

        if (!Found)
            return {};

        std::vector<GridCoord2D> Path;
        int32_t Cur = GoalIdx;
        while (Cur != -1)
        {
            Path.push_back({Cur % mWidth, Cur / mWidth});
            Cur = CameFrom[static_cast<size_t>(Cur)];
        }
        std::reverse(Path.begin(), Path.end());
        return Path;
    }

    // Dijkstra Goal-based FlowField2D generator for crowd steering
    bool BuildFlowField(GridCoord2D Goal, uint8_t FootprintSize, FlowField2D& OutField)
    {
        if (mClearanceDirty)
            RebuildClearanceField();
        ++mTelemetry.FlowFieldsGenerated;

        const uint8_t RequiredClearance = std::max<uint8_t>(1u, FootprintSize);
        const size_t Total = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);

        OutField.Width = mWidth;
        OutField.Height = mHeight;
        OutField.Goal = Goal;
        OutField.FootprintClearance = RequiredClearance;
        OutField.IntegrationCost.assign(Total, FlowField2D::kUnreachableCost);
        OutField.FlowVectors.assign(Total, {0.0f, 0.0f});

        if (!CanFitFootprint(Goal.X, Goal.Y, RequiredClearance))
            return false;

        struct QueueNode
        {
            uint32_t Cost = 0;
            int32_t Index = 0;
            bool operator>(const QueueNode& Other) const noexcept
            {
                return Cost != Other.Cost ? (Cost > Other.Cost) : (Index > Other.Index);
            }
        };

        std::priority_queue<QueueNode, std::vector<QueueNode>, std::greater<QueueNode>> Frontier;
        const size_t GoalIdx = CellIndex(Goal.X, Goal.Y);
        OutField.IntegrationCost[GoalIdx] = 0;
        Frontier.push({0u, static_cast<int32_t>(GoalIdx)});

        static constexpr int32_t kDirX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static constexpr int32_t kDirY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        static constexpr uint32_t kStepCost[8] = {10, 10, 10, 10, 14, 14, 14, 14};

        while (!Frontier.empty())
        {
            const QueueNode Top = Frontier.top();
            Frontier.pop();

            if (Top.Cost > OutField.IntegrationCost[static_cast<size_t>(Top.Index)])
                continue;

            const int32_t CX = Top.Index % mWidth;
            const int32_t CY = Top.Index / mWidth;

            for (int Dir = 0; Dir < 8; ++Dir)
            {
                const int32_t NX = CX + kDirX[Dir];
                const int32_t NY = CY + kDirY[Dir];
                if (!CanFitFootprint(NX, NY, RequiredClearance))
                    continue;
                if (!CanTraverseElevation(CX, CY, NX, NY))
                    continue;
                if (Dir >= 4)
                {
                    if (!CanFitFootprint(CX + kDirX[Dir], CY, RequiredClearance) ||
                        !CanFitFootprint(CX, CY + kDirY[Dir], RequiredClearance) ||
                        !CanTraverseElevation(CX, CY, CX + kDirX[Dir], CY) ||
                        !CanTraverseElevation(CX, CY, CX, CY + kDirY[Dir]) ||
                        !CanTraverseElevation(CX + kDirX[Dir], CY, NX, NY) ||
                        !CanTraverseElevation(CX, CY + kDirY[Dir], NX, NY))
                    {
                        continue;
                    }
                }

                const size_t NextIdx = CellIndex(NX, NY);
                const uint32_t NewCost = Top.Cost + kStepCost[Dir];
                if (NewCost < OutField.IntegrationCost[NextIdx])
                {
                    OutField.IntegrationCost[NextIdx] = NewCost;
                    Frontier.push({NewCost, static_cast<int32_t>(NextIdx)});
                }
            }
        }

        for (int32_t Y = 0; Y < mHeight; ++Y)
        {
            for (int32_t X = 0; X < mWidth; ++X)
            {
                const size_t Idx = CellIndex(X, Y);
                const uint32_t BaseCost = OutField.IntegrationCost[Idx];
                if (BaseCost == 0 || BaseCost == FlowField2D::kUnreachableCost)
                    continue;

                uint32_t BestCost = BaseCost;
                int32_t BestDX = 0;
                int32_t BestDY = 0;

                for (int Dir = 0; Dir < 8; ++Dir)
                {
                    const int32_t NX = X + kDirX[Dir];
                    const int32_t NY = Y + kDirY[Dir];
                    if (!InBounds(NX, NY))
                        continue;
                    if (!CanTraverseElevation(X, Y, NX, NY))
                        continue;
                    if (Dir >= 4)
                    {
                        if (!CanFitFootprint(X + kDirX[Dir], Y, RequiredClearance) ||
                            !CanFitFootprint(X, Y + kDirY[Dir], RequiredClearance) ||
                            !CanTraverseElevation(X, Y, X + kDirX[Dir], Y) ||
                            !CanTraverseElevation(X, Y, X, Y + kDirY[Dir]) ||
                            !CanTraverseElevation(X + kDirX[Dir], Y, NX, NY) ||
                            !CanTraverseElevation(X, Y + kDirY[Dir], NX, NY))
                        {
                            continue;
                        }
                    }
                    const uint32_t NeighborCost = OutField.IntegrationCost[CellIndex(NX, NY)];
                    if (NeighborCost < BestCost)
                    {
                        BestCost = NeighborCost;
                        BestDX = kDirX[Dir];
                        BestDY = kDirY[Dir];
                    }
                }

                if (BestDX != 0 || BestDY != 0)
                {
                    const float Len = std::sqrt(static_cast<float>(BestDX * BestDX + BestDY * BestDY));
                    OutField.FlowVectors[Idx] = {
                        static_cast<float>(BestDX) / Len,
                        static_cast<float>(BestDY) / Len};
                }
            }
        }
        return true;
    }

    // Deterministic radial separation / local avoidance velocity
    Core::Math::Vec2 ComputeAvoidanceVelocity(
        const AvoidanceAgent2D& Self,
        const std::vector<AvoidanceAgent2D>& Neighbors,
        Core::Math::Vec2 PreferredVelocity,
        float MaxSpeed,
        float SeparationWeight = 1.5f)
    {
        ++mTelemetry.AvoidanceStepsComputed;

        Core::Math::Vec2 Repulsion{0.0f, 0.0f};
        for (const auto& Other : Neighbors)
        {
            if (Other.EntityID == Self.EntityID)
                continue;

            const float DX = Self.Position.X - Other.Position.X;
            const float DY = Self.Position.Y - Other.Position.Y;
            const float CombinedRadius = Self.Radius + Other.Radius;
            const float DistSq = DX * DX + DY * DY;

            if (DistSq < CombinedRadius * CombinedRadius)
            {
                if (DistSq > 1e-4f)
                {
                    const float Dist = std::sqrt(DistSq);
                    const float OverlapRatio = (CombinedRadius - Dist) / CombinedRadius;
                    Repulsion.X += (DX / Dist) * OverlapRatio * MaxSpeed;
                    Repulsion.Y += (DY / Dist) * OverlapRatio * MaxSpeed;
                }
                else
                {
                    const float Dir = (Self.EntityID < Other.EntityID) ? 1.0f : -1.0f;
                    Repulsion.X += Dir * MaxSpeed;
                }
            }
        }

        Core::Math::Vec2 Combined{
            PreferredVelocity.X + Repulsion.X * SeparationWeight,
            PreferredVelocity.Y + Repulsion.Y * SeparationWeight};

        const float SpeedSq = Combined.X * Combined.X + Combined.Y * Combined.Y;
        if (SpeedSq > MaxSpeed * MaxSpeed && SpeedSq > 1e-6f)
        {
            const float Scale = MaxSpeed / std::sqrt(SpeedSq);
            Combined.X *= Scale;
            Combined.Y *= Scale;
        }
        return Combined;
    }

    // Phase 62 NavigationAgent2D Stepper:
    // Advances a stateful NavigationAgent2D from CurrentWorldPos toward its TargetWorldPosition,
    // computing its A* path on demand (after O(1) region reachability check when regions are built),
    // advancing through waypoints, applying local avoidance, and returning the new world position.
    Core::Math::Vec2 StepNavigationAgent(
        NavigationAgent2D& Agent,
        Core::Math::Vec2 CurrentWorldPos,
        float MaxSpeedPxPerSec,
        float DeltaSeconds,
        const std::vector<AvoidanceAgent2D>& Neighbors = {})
    {
        ++mTelemetry.AgentStepsEvaluated;
        if (!Agent.HasActiveTarget || Agent.ReachedTarget || DeltaSeconds <= 0.0f)
        {
            Agent.DesiredVelocity = {0.0f, 0.0f};
            return CurrentWorldPos;
        }

        const GridCoord2D StartCell = WorldToCell(CurrentWorldPos);
        const GridCoord2D GoalCell = WorldToCell(Agent.TargetWorldPosition);

        if (Agent.PathNeedsRebuild)
        {
            Agent.PathNeedsRebuild = false;
            Agent.CurrentWaypointIndex = 0;

            if (mRegionCount > 0 && Agent.FootprintClearance == 1 &&
                !AreCellsConnected(StartCell, GoalCell))
            {
                Agent.Waypoints.clear();
                Agent.TargetReachable = false;
                Agent.DesiredVelocity = {0.0f, 0.0f};
                return CurrentWorldPos;
            }

            Agent.Waypoints = FindPath(StartCell, GoalCell, Agent.FootprintClearance);
            Agent.TargetReachable = !Agent.Waypoints.empty();
            if (!Agent.TargetReachable)
            {
                Agent.DesiredVelocity = {0.0f, 0.0f};
                return CurrentWorldPos;
            }
            if (Agent.Waypoints.size() > 1 && Agent.Waypoints[0] == StartCell)
                Agent.CurrentWaypointIndex = 1;
        }

        if (Agent.Waypoints.empty() || Agent.CurrentWaypointIndex >= Agent.Waypoints.size())
        {
            Agent.ReachedTarget = true;
            Agent.HasActiveTarget = false;
            Agent.DesiredVelocity = {0.0f, 0.0f};
            return CurrentWorldPos;
        }

        Core::Math::Vec2 WaypointPos = (Agent.CurrentWaypointIndex + 1 == Agent.Waypoints.size())
                                           ? Agent.TargetWorldPosition
                                           : CellToWorldCenter(Agent.Waypoints[Agent.CurrentWaypointIndex]);

        float DX = WaypointPos.X - CurrentWorldPos.X;
        float DY = WaypointPos.Y - CurrentWorldPos.Y;
        float Dist = std::sqrt(DX * DX + DY * DY);

        while (Dist <= Agent.WaypointReachThresholdPx &&
               Agent.CurrentWaypointIndex + 1 < Agent.Waypoints.size())
        {
            ++Agent.CurrentWaypointIndex;
            WaypointPos = (Agent.CurrentWaypointIndex + 1 == Agent.Waypoints.size())
                              ? Agent.TargetWorldPosition
                              : CellToWorldCenter(Agent.Waypoints[Agent.CurrentWaypointIndex]);
            DX = WaypointPos.X - CurrentWorldPos.X;
            DY = WaypointPos.Y - CurrentWorldPos.Y;
            Dist = std::sqrt(DX * DX + DY * DY);
        }

        if (Dist <= Agent.WaypointReachThresholdPx &&
            Agent.CurrentWaypointIndex + 1 >= Agent.Waypoints.size())
        {
            Agent.ReachedTarget = true;
            Agent.HasActiveTarget = false;
            Agent.DesiredVelocity = {0.0f, 0.0f};
            return Agent.TargetWorldPosition;
        }

        Core::Math::Vec2 PreferredVel{0.0f, 0.0f};
        if (Dist > 1e-4f)
        {
            PreferredVel.X = (DX / Dist) * MaxSpeedPxPerSec;
            PreferredVel.Y = (DY / Dist) * MaxSpeedPxPerSec;
        }

        if (!Neighbors.empty())
        {
            AvoidanceAgent2D Self{Agent.EntityID, CurrentWorldPos, PreferredVel, Agent.Radius};
            Agent.DesiredVelocity = ComputeAvoidanceVelocity(Self, Neighbors, PreferredVel, MaxSpeedPxPerSec);
        }
        else
        {
            Agent.DesiredVelocity = PreferredVel;
        }

        return {
            CurrentWorldPos.X + Agent.DesiredVelocity.X * DeltaSeconds,
            CurrentWorldPos.Y + Agent.DesiredVelocity.Y * DeltaSeconds};
    }

    const NavigationServerTelemetry& GetTelemetry() const noexcept { return mTelemetry; }

private:
    size_t CellIndex(int32_t X, int32_t Y) const noexcept
    {
        return static_cast<size_t>(Y) * static_cast<size_t>(mWidth) + static_cast<size_t>(X);
    }

    int32_t mWidth = 0;
    int32_t mHeight = 0;
    float mCellSize = 32.0f;
    bool mClearanceDirty = true;
    uint32_t mRegionCount = 0;
    std::vector<uint8_t> mBlocked;
    std::vector<uint8_t> mClearance;
    std::vector<uint8_t> mElevation;
    std::vector<uint8_t> mRamp;
    std::vector<int32_t> mRegionIDs;
    NavigationServerTelemetry mTelemetry{};
};

} // namespace Engine::Navigation
