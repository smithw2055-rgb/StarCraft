#include "Game/Pathfinding/AStar.h"
#include "Game/Map/Map.h"
#include "Core/Utils/Logger.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <queue>

namespace Game::Pathfinding
{

AStar::SearchConfig AStar::sConfig;

int AStar::Heuristic(int DX, int DY)
{
    const int AbsDX = std::abs(DX);
    const int AbsDY = std::abs(DY);
    if (!sConfig.AllowDiagonal)
        return sConfig.HeuristicWeight * (AbsDX + AbsDY);

    const int Diagonal = std::min(AbsDX, AbsDY);
    const int Straight = std::max(AbsDX, AbsDY) - Diagonal;
    const int DiagonalCost = (sConfig.HeuristicWeight * 14) / 10;
    return DiagonalCost * Diagonal + sConfig.HeuristicWeight * Straight;
}

static bool IsWalkableWithClearance(const Map::GameMap& Map,
                                    int X,
                                    int Y,
                                    int ClearanceRadiusPixels,
                                    const std::function<bool(int, int)>& ExtraBlockedTile)
{
    if (!Map.IsInBounds(X, Y) || !Map.IsWalkable(X, Y) ||
        (ExtraBlockedTile && ExtraBlockedTile(X, Y)))
    {
        return false;
    }
    if (ClearanceRadiusPixels <= 0)
        return true;

    const Core::Math::Vec2 Center = Map.TileToWorld(X, Y);
    const float R = (float)ClearanceRadiusPixels;
    const Core::Math::Vec2 Samples[] = {
        {Center.X - R, Center.Y},
        {Center.X + R, Center.Y},
        {Center.X, Center.Y - R},
        {Center.X, Center.Y + R},
        {Center.X - R, Center.Y - R},
        {Center.X + R, Center.Y - R},
        {Center.X - R, Center.Y + R},
        {Center.X + R, Center.Y + R}
    };
    for (const auto& Sample : Samples)
    {
        int SX = 0;
        int SY = 0;
        Map.WorldToTile(Sample, SX, SY);
        if (!Map.IsInBounds(SX, SY) || !Map.IsWalkable(SX, SY) ||
            (ExtraBlockedTile && ExtraBlockedTile(SX, SY)) ||
            !Map.CanTraverseGround(X, Y, SX, SY))
        {
            return false;
        }
    }
    return true;
}

bool AStar::IsWalkable(const Map::GameMap& Map,
                       int X,
                       int Y,
                       int ClearanceRadiusPixels,
                       const std::function<bool(int, int)>& ExtraBlockedTile)
{
    return IsWalkableWithClearance(Map, X, Y, ClearanceRadiusPixels, ExtraBlockedTile);
}

static bool CanStepDiagonal(const Map::GameMap& Map,
                            int FromX,
                            int FromY,
                            int StepX,
                            int StepY,
                            int ClearanceRadiusPixels,
                            const std::function<bool(int, int)>& ExtraBlockedTile)
{
    if (!Map.CanTraverseGround(FromX, FromY,
                               FromX + StepX, FromY + StepY))
    {
        return false;
    }
    if (StepX == 0 || StepY == 0)
        return true;
    if (AStar::GetConfig().CutCorners)
        return true;
    return Map.CanTraverseGround(FromX, FromY, FromX + StepX, FromY) &&
           Map.CanTraverseGround(FromX, FromY, FromX, FromY + StepY) &&
           IsWalkableWithClearance(Map, FromX + StepX, FromY,
                                   ClearanceRadiusPixels, ExtraBlockedTile) &&
           IsWalkableWithClearance(Map, FromX, FromY + StepY,
                                   ClearanceRadiusPixels, ExtraBlockedTile);
}

static bool HasLineOfSight(const Map::GameMap& Map,
                           int X0, int Y0, int X1, int Y1,
                           int ClearanceRadiusPixels,
                           const std::function<bool(int, int)>& ExtraBlockedTile)
{
    if (Map.GetGroundHeight(X0, Y0) != Map.GetGroundHeight(X1, Y1))
        return false;

    int DX = std::abs(X1 - X0);
    int DY = std::abs(Y1 - Y0);
    int SX = (X0 < X1) ? 1 : -1;
    int SY = (Y0 < Y1) ? 1 : -1;
    int Err = DX - DY;
    int CX = X0, CY = Y0;
    while (CX != X1 || CY != Y1)
    {
        const int PreviousX = CX;
        const int PreviousY = CY;
        int E2 = Err * 2;
        if (E2 > -DY) { Err -= DY; CX += SX; }
        if (E2 < DX) { Err += DX; CY += SY; }

        if (!Map.CanTraverseGround(PreviousX, PreviousY, CX, CY) ||
            Map.GetGroundHeight(CX, CY) != Map.GetGroundHeight(X0, Y0) ||
            (CX != PreviousX && CY != PreviousY &&
             !CanStepDiagonal(Map, PreviousX, PreviousY, CX - PreviousX, CY - PreviousY,
                              ClearanceRadiusPixels, ExtraBlockedTile)) ||
            ((CX != X1 || CY != Y1) &&
             !IsWalkableWithClearance(
                 Map, CX, CY, ClearanceRadiusPixels, ExtraBlockedTile)))
        {
            return false;
        }
    }
    return true;
}

// Fast local spiral search around an unwalkable destination (e.g. clicked inside a
// building, mineral patch, cliff, or water). Avoids running a full-map BFS before A*.
static bool ResolveNearbyPassableGoalTile(
    const Map::GameMap& Map,
    int StartX,
    int StartY,
    int DesiredEndX,
    int DesiredEndY,
    int ClearanceRadiusPixels,
    const std::function<bool(int, int)>& ExtraBlockedTile,
    int& OutEndX,
    int& OutEndY)
{
    const int DesiredHeight = Map.GetGroundHeight(DesiredEndX, DesiredEndY);
    int BestSameHeightX = -1, BestSameHeightY = -1;
    int BestSameHeightDistSq = std::numeric_limits<int>::max();
    int BestSameHeightStartDistSq = std::numeric_limits<int>::max();

    int BestAnyHeightX = -1, BestAnyHeightY = -1;
    int BestAnyHeightDistSq = std::numeric_limits<int>::max();
    int BestAnyHeightStartDistSq = std::numeric_limits<int>::max();

    constexpr int kMaxSearchRadius = 24;
    for (int Radius = 1; Radius <= kMaxSearchRadius; ++Radius)
    {
        for (int DY = -Radius; DY <= Radius; ++DY)
        {
            for (int DX = -Radius; DX <= Radius; ++DX)
            {
                if (std::abs(DX) != Radius && std::abs(DY) != Radius)
                    continue;

                const int CX = DesiredEndX + DX;
                const int CY = DesiredEndY + DY;
                if (!Map.IsInBounds(CX, CY))
                    continue;
                if (!IsWalkableWithClearance(Map, CX, CY, ClearanceRadiusPixels, ExtraBlockedTile))
                    continue;

                const int DistSq = DX * DX + DY * DY;
                const int ToStartX = CX - StartX;
                const int ToStartY = CY - StartY;
                const int StartDistSq = ToStartX * ToStartX + ToStartY * ToStartY;

                if (Map.GetGroundHeight(CX, CY) == DesiredHeight)
                {
                    if (DistSq < BestSameHeightDistSq ||
                        (DistSq == BestSameHeightDistSq && StartDistSq < BestSameHeightStartDistSq))
                    {
                        BestSameHeightDistSq = DistSq;
                        BestSameHeightStartDistSq = StartDistSq;
                        BestSameHeightX = CX;
                        BestSameHeightY = CY;
                    }
                }

                if (DistSq < BestAnyHeightDistSq ||
                    (DistSq == BestAnyHeightDistSq && StartDistSq < BestAnyHeightStartDistSq))
                {
                    BestAnyHeightDistSq = DistSq;
                    BestAnyHeightStartDistSq = StartDistSq;
                    BestAnyHeightX = CX;
                    BestAnyHeightY = CY;
                }
            }
        }

        if (BestSameHeightX >= 0)
        {
            OutEndX = BestSameHeightX;
            OutEndY = BestSameHeightY;
            return true;
        }
    }

    if (BestAnyHeightX >= 0)
    {
        OutEndX = BestAnyHeightX;
        OutEndY = BestAnyHeightY;
        return true;
    }
    return false;
}

PathResult AStar::FindPath(const Map::GameMap& Map,
                           Core::Math::Vec2 Start, Core::Math::Vec2 End,
                           int MaxNodes,
                           int ClearanceRadiusPixels)
{
    return FindPath(Map, Start, End, MaxNodes, ClearanceRadiusPixels, {});
}

PathResult AStar::FindPath(const Map::GameMap& Map,
                           Core::Math::Vec2 Start, Core::Math::Vec2 End,
                           int MaxNodes,
                           int ClearanceRadiusPixels,
                           const std::function<bool(int, int)>& ExtraBlockedTile)
{
    int SX, SY, EX, EY;
    Map.WorldToTile(Start, SX, SY);
    Map.WorldToTile(End, EX, EY);
    const bool ExactEndWalkable =
        IsWalkable(Map, EX, EY, ClearanceRadiusPixels, ExtraBlockedTile);
    PathResult Result = FindPathTile(
        Map, SX, SY, EX, EY, MaxNodes, ClearanceRadiusPixels, ExtraBlockedTile);
    if (!Result.Waypoints.empty())
    {
        int FirstX = 0;
        int FirstY = 0;
        Map.WorldToTile(Result.Waypoints.front(), FirstX, FirstY);
        if (FirstX == SX && FirstY == SY)
            Result.Waypoints.erase(Result.Waypoints.begin());
    }
    bool ReachedExactEndTile = SX == EX && SY == EY;
    if (!Result.Waypoints.empty())
    {
        int LastX = 0;
        int LastY = 0;
        Map.WorldToTile(Result.Waypoints.back(), LastX, LastY);
        ReachedExactEndTile = LastX == EX && LastY == EY;
    }

    if (ExactEndWalkable && ReachedExactEndTile)
    {
        if (!Result.Waypoints.empty())
            Result.Waypoints.back() = End;
        else if (Result.Found)
            Result.Waypoints.push_back(End);
    }
    return Result;
}

PathResult AStar::FindPathTile(const Map::GameMap& Map,
                               int StartX, int StartY,
                               int EndX, int EndY,
                               int MaxNodes,
                               int ClearanceRadiusPixels)
{
    return FindPathTile(
        Map, StartX, StartY, EndX, EndY, MaxNodes, ClearanceRadiusPixels, {});
}

PathResult AStar::FindPathTile(const Map::GameMap& Map,
                               int StartX, int StartY,
                               int EndX, int EndY,
                               int MaxNodes,
                               int ClearanceRadiusPixels,
                               const std::function<bool(int, int)>& ExtraBlockedTile)
{
    PathResult Result;
    if (!Map.IsInBounds(StartX, StartY))
        return Result;

    const int Width = Map.GetWidth();
    const int Height = Map.GetHeight();
    if (Width <= 0 || Height <= 0)
        return Result;

    EndX = std::clamp(EndX, 0, Width - 1);
    EndY = std::clamp(EndY, 0, Height - 1);
    const int OriginalEndX = EndX;
    const int OriginalEndY = EndY;

    if (!IsWalkableWithClearance(Map, EndX, EndY, ClearanceRadiusPixels, ExtraBlockedTile))
    {
        int ResolvedEndX = EndX;
        int ResolvedEndY = EndY;
        if (ResolveNearbyPassableGoalTile(
                Map, StartX, StartY, EndX, EndY,
                ClearanceRadiusPixels, ExtraBlockedTile,
                ResolvedEndX, ResolvedEndY))
        {
            EndX = ResolvedEndX;
            EndY = ResolvedEndY;
        }
    }

    const int MapNodeCount = Width * Height;
    const int NodeBudget = MaxNodes > 0
        ? std::min(std::max(MaxNodes, 512), MapNodeCount)
        : MapNodeCount;

    thread_local std::vector<PathNode> sNodes;
    thread_local std::vector<std::pair<int, int>> sOpen;
    thread_local std::vector<uint32_t> sClosedGen;
    thread_local std::vector<std::pair<uint32_t, int>> sNodeMap;
    thread_local uint32_t sPathGen = 0;

    ++sPathGen;
    if (sPathGen == 0)
    {
        std::fill(sClosedGen.begin(), sClosedGen.end(), 0);
        for (auto& Entry : sNodeMap)
            Entry = {0, -1};
        sPathGen = 1;
    }

    if ((int)sClosedGen.size() < MapNodeCount)
        sClosedGen.resize(MapNodeCount, 0);
    if ((int)sNodeMap.size() < MapNodeCount)
        sNodeMap.resize(MapNodeCount, {0, -1});

    sNodes.clear();
    sNodes.reserve(std::min(NodeBudget + 64, MapNodeCount));
    const int InitialH = Heuristic(StartX - EndX, StartY - EndY);
    sNodes.push_back({StartX, StartY, 0, InitialH, -1});

    sOpen.clear();
    sOpen.reserve(std::min(NodeBudget + 64, MapNodeCount));
    sOpen.push_back({sNodes[0].F(), 0});

    sNodeMap[StartY * Width + StartX] = {sPathGen, 0};

    int BestPartialNodeIdx = 0;
    int BestPartialDistSq =
        (StartX - OriginalEndX) * (StartX - OriginalEndX) +
        (StartY - OriginalEndY) * (StartY - OriginalEndY);

    const int DX[] = {1, -1, 0, 0, 1, -1, 1, -1};
    const int DY[] = {0, 0, 1, -1, 1, -1, -1, 1};
    const int DirCount = sConfig.AllowDiagonal ? 8 : 4;
    const int DiagStart = DirCount - 4;
    auto Cmp = [](const std::pair<int, int>& A, const std::pair<int, int>& B)
    {
        return A.first > B.first;
    };

    auto BuildSmoothedWaypoints = [&](int GoalNodeIdx)
    {
        std::vector<Core::Math::Vec2> Raw;
        int P = GoalNodeIdx;
        while (P >= 0)
        {
            Raw.push_back({
                sNodes[(size_t)P].X * 32.0f + 16.0f,
                sNodes[(size_t)P].Y * 32.0f + 16.0f
            });
            P = sNodes[(size_t)P].ParentIndex;
        }
        std::reverse(Raw.begin(), Raw.end());

        if (Raw.size() > 2)
        {
            Result.Waypoints.push_back(Raw[0]);
            size_t Anchor = 0;
            while (Anchor + 1 < Raw.size())
            {
                size_t Furthest = Raw.size() - 1;
                int AX = 0, AY = 0;
                Map.WorldToTile(Raw[Anchor], AX, AY);
                while (Furthest > Anchor + 1)
                {
                    int FX = 0, FY = 0;
                    Map.WorldToTile(Raw[Furthest], FX, FY);
                    if (HasLineOfSight(Map, AX, AY, FX, FY,
                                       ClearanceRadiusPixels, ExtraBlockedTile))
                    {
                        break;
                    }
                    --Furthest;
                }
                Result.Waypoints.push_back(Raw[Furthest]);
                Anchor = Furthest;
            }
        }
        else
        {
            Result.Waypoints = std::move(Raw);
        }
        Result.Found = (Result.Status == PathStatus::Exact);
    };

    while (!sOpen.empty())
    {
        std::pop_heap(sOpen.begin(), sOpen.end(), Cmp);
        const int CurIdx = sOpen.back().second;
        sOpen.pop_back();

        const int CurX = sNodes[(size_t)CurIdx].X;
        const int CurY = sNodes[(size_t)CurIdx].Y;
        const int CurG = sNodes[(size_t)CurIdx].G;

        const int CI = CurY * Width + CurX;
        if (sClosedGen[(size_t)CI] == sPathGen)
            continue;
        sClosedGen[(size_t)CI] = sPathGen;
        Result.NodesSearched++;

        const int DistSqToOriginal =
            (CurX - OriginalEndX) * (CurX - OriginalEndX) +
            (CurY - OriginalEndY) * (CurY - OriginalEndY);
        if (DistSqToOriginal < BestPartialDistSq)
        {
            BestPartialDistSq = DistSqToOriginal;
            BestPartialNodeIdx = CurIdx;
        }

        if (CurX == EndX && CurY == EndY)
        {
            Result.Status = PathStatus::Exact;
            Result.ResolvedDestination = {
                (float)(EndX * 32 + 16),
                (float)(EndY * 32 + 16)
            };
            BuildSmoothedWaypoints(CurIdx);
            return Result;
        }

        if (Result.NodesSearched >= NodeBudget || (int)sNodes.size() >= NodeBudget)
            break;

        for (int D = 0; D < DirCount; ++D)
        {
            const int NX = CurX + DX[D];
            const int NY = CurY + DY[D];

            if (!IsWalkable(Map, NX, NY, ClearanceRadiusPixels, ExtraBlockedTile))
                continue;
            if (!Map.CanTraverseGround(CurX, CurY, NX, NY))
                continue;

            if (D >= DiagStart &&
                !CanStepDiagonal(Map, CurX, CurY, DX[D], DY[D],
                                 ClearanceRadiusPixels, ExtraBlockedTile))
            {
                continue;
            }

            const int NI = NY * Width + NX;
            if (sClosedGen[(size_t)NI] == sPathGen)
                continue;

            const int StepCost = (D < DiagStart) ? 10 : 14;
            const int NG = CurG + StepCost;

            auto& Entry = sNodeMap[(size_t)NI];
            if (Entry.first != sPathGen)
            {
                Entry.first = sPathGen;
                Entry.second = (int)sNodes.size();
                sNodes.push_back({NX, NY, NG, Heuristic(NX - EndX, NY - EndY), CurIdx});
                sOpen.push_back({sNodes.back().F(), Entry.second});
                std::push_heap(sOpen.begin(), sOpen.end(), Cmp);
            }
            else
            {
                const int NodeIdx = Entry.second;
                if (NG < sNodes[(size_t)NodeIdx].G)
                {
                    sNodes[(size_t)NodeIdx].G = NG;
                    sNodes[(size_t)NodeIdx].ParentIndex = CurIdx;
                    sOpen.push_back({sNodes[(size_t)NodeIdx].F(), NodeIdx});
                    std::push_heap(sOpen.begin(), sOpen.end(), Cmp);
                }
            }
        }
    }

    // If the target was on an unreachable island/cliff or budget was reached,
    // advance the unit to the closest reachable tile (cliff edge / shore) toward the destination.
    if (BestPartialNodeIdx > 0 || (StartX == OriginalEndX && StartY == OriginalEndY))
    {
        Result.Status = PathStatus::Partial;
        const auto& BestNode = sNodes[(size_t)BestPartialNodeIdx];
        Result.ResolvedDestination = {
            (float)(BestNode.X * 32 + 16),
            (float)(BestNode.Y * 32 + 16)
        };
        BuildSmoothedWaypoints(BestPartialNodeIdx);
    }

    return Result;
}

std::vector<Core::Math::Vec2> AStar::ReconstructPath(const std::vector<PathNode>& Nodes, int GoalIndex)
{
    std::vector<Core::Math::Vec2> Path;
    int Current = GoalIndex;
    while (Current >= 0 && Current < (int)Nodes.size())
    {
        Path.push_back({
            Nodes[(size_t)Current].X * 32.0f + 16.0f,
            Nodes[(size_t)Current].Y * 32.0f + 16.0f
        });
        Current = Nodes[(size_t)Current].ParentIndex;
    }
    std::reverse(Path.begin(), Path.end());
    return Path;
}

} // namespace Game::Pathfinding
