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

struct NavigationServerTelemetry
{
    uint64_t ClearanceRebuilds = 0;
    uint64_t AStarQueriesExecuted = 0;
    uint64_t FlowFieldsGenerated = 0;
    uint64_t AvoidanceStepsComputed = 0;
};

// ============================================================================
// NavigationServer2D (Phase 40):
// - General-purpose 2D RTS navigation server inspired by Godot NavigationServer2D
//   and classic RTS hierarchical/clearance pathfinding.
// - Provides:
//   1. True Clearance Field computation (supporting 1x1, 2x2, 3x3, 4x4 footprints).
//   2. Deterministic A* pathfinding with footprint clearance filtering.
//   3. Goal-based Dijkstra Flow Field generation for large crowd steering.
//   4. Deterministic radial separation / local avoidance for unit steering.
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
        RebuildClearanceField();
    }

    int32_t GetWidth() const noexcept { return mWidth; }
    int32_t GetHeight() const noexcept { return mHeight; }
    float GetCellSize() const noexcept { return mCellSize; }

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

    uint8_t GetCellClearance(int32_t X, int32_t Y)
    {
        if (mClearanceDirty)
            RebuildClearanceField();
        if (!InBounds(X, Y))
            return 0u;
        return mClearance[CellIndex(X, Y)];
    }

    // Dynamic programming true-clearance field computation in O(W * H):
    // mClearance[y * W + x] = largest square footprint (capped at 16) that fits
    // with its top-left corner at (x, y).
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

    bool CanFitFootprint(int32_t X, int32_t Y, uint8_t FootprintSize)
    {
        if (mClearanceDirty)
            RebuildClearanceField();
        if (!InBounds(X, Y))
            return false;
        return mClearance[CellIndex(X, Y)] >= std::max<uint8_t>(1u, FootprintSize);
    }

    // Deterministic A* Pathfinding with Footprint Clearance
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
                if (FScore != Other.FScore)
                    return FScore > Other.FScore;
                return Index > Other.Index; // Deterministic tie-break
            }
        };

        auto OctileHeuristic = [](int32_t X0, int32_t Y0, int32_t X1, int32_t Y1) -> uint32_t {
            const uint32_t DX = static_cast<uint32_t>(std::abs(X1 - X0));
            const uint32_t DY = static_cast<uint32_t>(std::abs(Y1 - Y0));
            const uint32_t MinD = std::min(DX, DY);
            const uint32_t MaxD = std::max(DX, DY);
            return 14u * MinD + 10u * (MaxD - MinD);
        };

        std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<OpenEntry>> OpenSet;
        const size_t StartIdx = CellIndex(Start.X, Start.Y);
        const size_t GoalIdx = CellIndex(Goal.X, Goal.Y);

        GScore[StartIdx] = 0;
        OpenSet.push({OctileHeuristic(Start.X, Start.Y, Goal.X, Goal.Y), static_cast<int32_t>(StartIdx)});

        static constexpr int32_t kDirX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static constexpr int32_t kDirY[8] = {0, 0, 1, -1, 1, -1, 1, -1};
        static constexpr uint32_t kStepCost[8] = {10, 10, 10, 10, 14, 14, 14, 14};

        uint32_t Expanded = 0;
        while (!OpenSet.empty() && Expanded < MaxExpandedNodes)
        {
            const OpenEntry Current = OpenSet.top();
            OpenSet.pop();

            const size_t CurIdx = static_cast<size_t>(Current.Index);
            if (Closed[CurIdx])
                continue;
            Closed[CurIdx] = 1u;
            ++Expanded;

            if (CurIdx == GoalIdx)
            {
                std::vector<GridCoord2D> Path;
                int32_t Trace = static_cast<int32_t>(GoalIdx);
                while (Trace >= 0)
                {
                    const int32_t TX = Trace % mWidth;
                    const int32_t TY = Trace / mWidth;
                    Path.push_back({TX, TY});
                    Trace = CameFrom[static_cast<size_t>(Trace)];
                }
                std::reverse(Path.begin(), Path.end());
                return Path;
            }

            const int32_t CX = static_cast<int32_t>(CurIdx) % mWidth;
            const int32_t CY = static_cast<int32_t>(CurIdx) / mWidth;

            for (int Dir = 0; Dir < 8; ++Dir)
            {
                const int32_t NX = CX + kDirX[Dir];
                const int32_t NY = CY + kDirY[Dir];
                if (!CanFitFootprint(NX, NY, RequiredClearance))
                    continue;

                // Prevent corner cutting on diagonal moves
                if (Dir >= 4)
                {
                    if (!CanFitFootprint(CX + kDirX[Dir], CY, RequiredClearance) ||
                        !CanFitFootprint(CX, CY + kDirY[Dir], RequiredClearance))
                    {
                        continue;
                    }
                }

                const size_t NextIdx = CellIndex(NX, NY);
                if (Closed[NextIdx])
                    continue;

                const uint32_t TentativeG = GScore[CurIdx] + kStepCost[Dir];
                if (TentativeG < GScore[NextIdx])
                {
                    GScore[NextIdx] = TentativeG;
                    CameFrom[NextIdx] = static_cast<int32_t>(CurIdx);
                    const uint32_t F = TentativeG + OctileHeuristic(NX, NY, Goal.X, Goal.Y);
                    OpenSet.push({F, static_cast<int32_t>(NextIdx)});
                }
            }
        }
        return {};
    }

    // Builds a Dijkstra Flow Field toward Goal for mass RTS crowd movement
    bool BuildFlowField(GridCoord2D Goal, uint8_t FootprintSize, FlowField2D& OutField)
    {
        if (mClearanceDirty)
            RebuildClearanceField();
        ++mTelemetry.FlowFieldsGenerated;

        const uint8_t RequiredClearance = std::max<uint8_t>(1u, FootprintSize);
        if (!CanFitFootprint(Goal.X, Goal.Y, RequiredClearance))
            return false;

        const size_t Total = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
        OutField.Width = mWidth;
        OutField.Height = mHeight;
        OutField.Goal = Goal;
        OutField.FootprintClearance = RequiredClearance;
        OutField.IntegrationCost.assign(Total, FlowField2D::kUnreachableCost);
        OutField.FlowVectors.assign(Total, {0.0f, 0.0f});

        struct QueueNode
        {
            uint32_t Cost = 0;
            int32_t Index = 0;
            bool operator>(const QueueNode& Other) const noexcept
            {
                if (Cost != Other.Cost)
                    return Cost > Other.Cost;
                return Index > Other.Index;
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

            const size_t CurIdx = static_cast<size_t>(Top.Index);
            if (Top.Cost > OutField.IntegrationCost[CurIdx])
                continue;

            const int32_t CX = static_cast<int32_t>(CurIdx) % mWidth;
            const int32_t CY = static_cast<int32_t>(CurIdx) / mWidth;

            for (int Dir = 0; Dir < 8; ++Dir)
            {
                const int32_t NX = CX + kDirX[Dir];
                const int32_t NY = CY + kDirY[Dir];
                if (!CanFitFootprint(NX, NY, RequiredClearance))
                    continue;

                if (Dir >= 4)
                {
                    if (!CanFitFootprint(CX + kDirX[Dir], CY, RequiredClearance) ||
                        !CanFitFootprint(CX, CY + kDirY[Dir], RequiredClearance))
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

        // Compute normalized gradient vectors pointing toward lowest integration cost neighbor
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
                    if (Dir >= 4)
                    {
                        if (!CanFitFootprint(X + kDirX[Dir], Y, RequiredClearance) ||
                            !CanFitFootprint(X, Y + kDirY[Dir], RequiredClearance))
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
                    // Deterministic tie-breaker when centers coincide
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
    std::vector<uint8_t> mBlocked;
    std::vector<uint8_t> mClearance;
    NavigationServerTelemetry mTelemetry{};
};

} // namespace Engine::Navigation
