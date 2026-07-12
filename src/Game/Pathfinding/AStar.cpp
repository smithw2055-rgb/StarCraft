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
            (ExtraBlockedTile && ExtraBlockedTile(SX, SY)))
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

static bool HasLineOfSight(const Map::GameMap& Map,
                           int X0, int Y0, int X1, int Y1,
                           int ClearanceRadiusPixels,
                           const std::function<bool(int, int)>& ExtraBlockedTile)
{
    int DX = std::abs(X1 - X0);
    int DY = std::abs(Y1 - Y0);
    int SX = (X0 < X1) ? 1 : -1;
    int SY = (Y0 < Y1) ? 1 : -1;
    int Err = DX - DY;
    int CX = X0, CY = Y0;
    while (CX != X1 || CY != Y1)
    {
        int E2 = Err * 2;
        if (E2 > -DY) { Err -= DY; CX += SX; }
        if (E2 < DX) { Err += DX; CY += SY; }
        if ((CX != X1 || CY != Y1) &&
            !IsWalkableWithClearance(Map, CX, CY, ClearanceRadiusPixels, ExtraBlockedTile))
        {
            return false;
        }
    }
    return true;
}

static bool CanStepDiagonal(const Map::GameMap& Map,
                            int FromX,
                            int FromY,
                            int StepX,
                            int StepY,
                            int ClearanceRadiusPixels,
                            const std::function<bool(int, int)>& ExtraBlockedTile)
{
    if (StepX == 0 || StepY == 0)
        return true;
    if (AStar::GetConfig().CutCorners)
        return true;
    return IsWalkableWithClearance(Map, FromX + StepX, FromY,
                                   ClearanceRadiusPixels, ExtraBlockedTile) &&
           IsWalkableWithClearance(Map, FromX, FromY + StepY,
                                   ClearanceRadiusPixels, ExtraBlockedTile);
}

static bool ResolveReachableGoalTile(const Map::GameMap& Map,
                                     int StartX,
                                     int StartY,
                                     int DesiredEndX,
                                     int DesiredEndY,
                                     int ClearanceRadiusPixels,
                                     const std::function<bool(int, int)>& ExtraBlockedTile,
                                     int& OutEndX,
                                     int& OutEndY)
{
    const int Width = Map.GetWidth();
    const int Height = Map.GetHeight();
    if (Width <= 0 || Height <= 0)
        return false;

    if (!IsWalkableWithClearance(Map, StartX, StartY, ClearanceRadiusPixels, ExtraBlockedTile))
        return false;

    const int DirX[] = {1, -1, 0, 0, 1, -1, 1, -1};
    const int DirY[] = {0, 0, 1, -1, 1, -1, -1, 1};
    const int DirCount = AStar::GetConfig().AllowDiagonal ? 8 : 4;

    std::vector<uint8_t> Visited((size_t)Width * (size_t)Height, 0);
    std::queue<std::pair<int, int>> Open;
    Open.push({StartX, StartY});
    Visited[(size_t)StartY * (size_t)Width + (size_t)StartX] = 1;

    int BestX = StartX;
    int BestY = StartY;
    int BestDistSq = std::numeric_limits<int>::max();
    int BestStartDistSq = 0;

    auto Consider = [&](int X, int Y)
    {
        const int ToEndX = X - DesiredEndX;
        const int ToEndY = Y - DesiredEndY;
        const int DistSq = ToEndX * ToEndX + ToEndY * ToEndY;
        const int FromStartX = X - StartX;
        const int FromStartY = Y - StartY;
        const int StartDistSq = FromStartX * FromStartX + FromStartY * FromStartY;
        if (DistSq < BestDistSq ||
            (DistSq == BestDistSq && StartDistSq < BestStartDistSq))
        {
            BestDistSq = DistSq;
            BestStartDistSq = StartDistSq;
            BestX = X;
            BestY = Y;
        }
    };

    while (!Open.empty())
    {
        auto [CurX, CurY] = Open.front();
        Open.pop();
        Consider(CurX, CurY);
        if (CurX == DesiredEndX && CurY == DesiredEndY)
        {
            OutEndX = CurX;
            OutEndY = CurY;
            return true;
        }

        for (int D = 0; D < DirCount; ++D)
        {
            const int NX = CurX + DirX[D];
            const int NY = CurY + DirY[D];
            if (!Map.IsInBounds(NX, NY))
                continue;
            const size_t NI = (size_t)NY * (size_t)Width + (size_t)NX;
            if (Visited[NI])
                continue;
            if (!IsWalkableWithClearance(Map, NX, NY, ClearanceRadiusPixels, ExtraBlockedTile))
                continue;
            if (!CanStepDiagonal(Map, CurX, CurY, DirX[D], DirY[D],
                                 ClearanceRadiusPixels, ExtraBlockedTile))
                continue;

            Visited[NI] = 1;
            Open.push({NX, NY});
        }
    }

    OutEndX = BestX;
    OutEndY = BestY;
    return BestDistSq != std::numeric_limits<int>::max();
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
    (void)MaxNodes;

    if (!Map.IsInBounds(StartX, StartY))
        return Result;

    int Width = Map.GetWidth();
    int Height = Map.GetHeight();
    if (Width <= 0 || Height <= 0)
        return Result;

    EndX = std::clamp(EndX, 0, Width - 1);
    EndY = std::clamp(EndY, 0, Height - 1);

    int ResolvedEndX = EndX;
    int ResolvedEndY = EndY;
    if (!ResolveReachableGoalTile(Map, StartX, StartY, EndX, EndY,
                                  ClearanceRadiusPixels, ExtraBlockedTile,
                                  ResolvedEndX, ResolvedEndY))
    {
        return Result;
    }
    EndX = ResolvedEndX;
    EndY = ResolvedEndY;

    std::vector<PathNode> Nodes;
    const int MapNodeCount = Width * Height;
    const int NodeBudget = MapNodeCount;
    Nodes.reserve(NodeBudget);
    Nodes.push_back({StartX, StartY, 0, Heuristic(StartX - EndX, StartY - EndY), -1});

    // Open set: pairs of (F-cost, NodeIndex)
    std::vector<std::pair<int, int>> Open;
    Open.reserve(NodeBudget);
    Open.push_back({Nodes[0].F(), 0});

    std::vector<bool> Closed(Width * Height, false);
    std::vector<int> NodeMap(Width * Height, -1);
    NodeMap[StartY * Width + StartX] = 0;

    const int DX[] = {1, -1, 0, 0, 1, -1, 1, -1};
    const int DY[] = {0, 0, 1, -1, 1, -1, -1, 1};
    int DirCount = sConfig.AllowDiagonal ? 8 : 4;
    int DiagStart = DirCount - 4;
    auto Cmp = [](const std::pair<int,int>& A, const std::pair<int,int>& B) {
        return A.first > B.first;
    };

    while (!Open.empty())
    {
        std::pop_heap(Open.begin(), Open.end(), Cmp);
        int CurIdx = Open.back().second;
        Open.pop_back();

        PathNode& Cur = Nodes[CurIdx];

        int CI = Cur.Y * Width + Cur.X;
        if (Closed[CI]) continue;
        Closed[CI] = true;
        Result.NodesSearched++;

        if (Cur.X == EndX && Cur.Y == EndY)
        {
            // Reconstruct with smoothing
            std::vector<Core::Math::Vec2> Raw;
            int P = CurIdx;
            while (P >= 0)
            {
                Raw.push_back({
                    Nodes[P].X * 32.0f + 16.0f,
                    Nodes[P].Y * 32.0f + 16.0f
                });
                P = Nodes[P].ParentIndex;
            }
            std::reverse(Raw.begin(), Raw.end());

            // Smooth path: skip waypoints with direct line of sight
            if (Raw.size() > 2)
            {
                Result.Waypoints.push_back(Raw[0]);
                size_t Anchor = 0;
                while (Anchor + 1 < Raw.size())
                {
                    size_t Furthest = Raw.size() - 1;
                    int AX = 0;
                    int AY = 0;
                    Map.WorldToTile(Raw[Anchor], AX, AY);
                    while (Furthest > Anchor + 1)
                    {
                        int FX = 0;
                        int FY = 0;
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

            Result.Found = true;
            return Result;
        }

        if ((int)Nodes.size() >= NodeBudget) continue;

        for (int D = 0; D < DirCount; D++)
        {
            int NX = Cur.X + DX[D];
            int NY = Cur.Y + DY[D];

            if (!IsWalkable(Map, NX, NY, ClearanceRadiusPixels, ExtraBlockedTile)) continue;

            if (D >= DiagStart &&
                !CanStepDiagonal(Map, Cur.X, Cur.Y, DX[D], DY[D],
                                 ClearanceRadiusPixels, ExtraBlockedTile))
                continue;

            int NI = NY * Width + NX;
            if (Closed[NI]) continue;

            int StepCost = (D < DiagStart) ? 10 : 14;
            int NG = Cur.G + StepCost;

            int& NodeIdx = NodeMap[NI];
            if (NodeIdx == -1)
            {
                NodeIdx = (int)Nodes.size();
                Nodes.push_back({NX, NY, NG, Heuristic(NX - EndX, NY - EndY), CurIdx});
                Open.push_back({Nodes.back().F(), NodeIdx});
                std::push_heap(Open.begin(), Open.end(), Cmp);
            }
            else if (NG < Nodes[(size_t)NodeIdx].G)
            {
                Nodes[(size_t)NodeIdx].G = NG;
                Nodes[(size_t)NodeIdx].ParentIndex = CurIdx;
                Open.push_back({Nodes[(size_t)NodeIdx].F(), NodeIdx});
                std::push_heap(Open.begin(), Open.end(), Cmp);
            }
        }
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
