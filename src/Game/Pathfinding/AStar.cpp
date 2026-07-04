#include "Game/Pathfinding/AStar.h"
#include "Game/Map/Map.h"
#include "Core/Utils/Logger.h"

#include <cmath>
#include <algorithm>
#include <limits>

namespace Game::Pathfinding
{

AStar::SearchConfig AStar::sConfig;

int AStar::Heuristic(int DX, int DY)
{
    return sConfig.HeuristicWeight * (std::abs(DX) + std::abs(DY));
}

static bool IsWalkableWithClearance(const Map::GameMap& Map,
                                    int X,
                                    int Y,
                                    int ClearanceRadiusPixels)
{
    if (!Map.IsInBounds(X, Y) || !Map.IsWalkable(X, Y))
        return false;
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
        if (!Map.IsInBounds(SX, SY) || !Map.IsWalkable(SX, SY))
            return false;
    }
    return true;
}

bool AStar::IsWalkable(const Map::GameMap& Map, int X, int Y, int ClearanceRadiusPixels)
{
    return IsWalkableWithClearance(Map, X, Y, ClearanceRadiusPixels);
}

static bool HasLineOfSight(const Map::GameMap& Map,
                           int X0, int Y0, int X1, int Y1,
                           int ClearanceRadiusPixels)
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
            !IsWalkableWithClearance(Map, CX, CY, ClearanceRadiusPixels))
            return false;
    }
    return true;
}

PathResult AStar::FindPath(const Map::GameMap& Map,
                           Core::Math::Vec2 Start, Core::Math::Vec2 End,
                           int MaxNodes,
                           int ClearanceRadiusPixels)
{
    int SX, SY, EX, EY;
    Map.WorldToTile(Start, SX, SY);
    Map.WorldToTile(End, EX, EY);
    const bool ExactEndWalkable = IsWalkable(Map, EX, EY, ClearanceRadiusPixels);
    PathResult Result = FindPathTile(Map, SX, SY, EX, EY, MaxNodes, ClearanceRadiusPixels);
    if (!Result.Waypoints.empty())
    {
        int FirstX = 0;
        int FirstY = 0;
        Map.WorldToTile(Result.Waypoints.front(), FirstX, FirstY);
        if (FirstX == SX && FirstY == SY)
            Result.Waypoints.erase(Result.Waypoints.begin());
    }
    if (ExactEndWalkable)
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
    PathResult Result;

    if (!IsWalkable(Map, StartX, StartY, ClearanceRadiusPixels))
        return Result;

    if (!IsWalkable(Map, EndX, EndY, ClearanceRadiusPixels))
    {
        int BestDist = std::numeric_limits<int>::max();
        int BestX = EndX, BestY = EndY;
        for (int DY = -5; DY <= 5; DY++)
            for (int DX = -5; DX <= 5; DX++)
            {
                int NX = EndX + DX, NY = EndY + DY;
                if (IsWalkable(Map, NX, NY, ClearanceRadiusPixels))
                {
                    int D = DX * DX + DY * DY;
                    if (D < BestDist) { BestDist = D; BestX = NX; BestY = NY; }
                }
            }
        EndX = BestX; EndY = BestY;
        if (!IsWalkable(Map, EndX, EndY, ClearanceRadiusPixels)) return Result;
    }

    int Width = Map.GetWidth();
    int Height = Map.GetHeight();

    std::vector<PathNode> Nodes;
    Nodes.reserve(MaxNodes);
    Nodes.push_back({StartX, StartY, 0, Heuristic(StartX - EndX, StartY - EndY), -1});

    // Open set: pairs of (F-cost, NodeIndex)
    std::vector<std::pair<int, int>> Open;
    Open.reserve(MaxNodes);
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

    int BestGoalIdx = 0;
    int BestGoalDist = Heuristic(StartX - EndX, StartY - EndY);

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

        // Track closest node to target for fallback path
        int CurDist = Heuristic(Cur.X - EndX, Cur.Y - EndY);
        if (CurDist < BestGoalDist)
        {
            BestGoalDist = CurDist;
            BestGoalIdx = CurIdx;
        }

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
                for (size_t I = 1; I < Raw.size(); I++)
                {
                    auto& Prev = Result.Waypoints.back();
                    auto& Next = Raw[I];
                    int PX, PY, NX, NY;
                    Map.WorldToTile(Prev, PX, PY);
                    Map.WorldToTile(Next, NX, NY);
                    if (!HasLineOfSight(Map, PX, PY, NX, NY, ClearanceRadiusPixels))
                        Result.Waypoints.push_back(Raw[I - 1]);
                }
                Result.Waypoints.push_back(Raw.back());
            }
            else
            {
                Result.Waypoints = std::move(Raw);
            }

            Result.Found = true;
            return Result;
        }

        if ((int)Nodes.size() >= MaxNodes) continue;

        for (int D = 0; D < DirCount; D++)
        {
            int NX = Cur.X + DX[D];
            int NY = Cur.Y + DY[D];

            if (!IsWalkable(Map, NX, NY, ClearanceRadiusPixels)) continue;

            if (D >= DiagStart)
            {
                if (!IsWalkable(Map, Cur.X + DX[D], Cur.Y, ClearanceRadiusPixels) ||
                    !IsWalkable(Map, Cur.X, Cur.Y + DY[D], ClearanceRadiusPixels))
                    continue;
            }

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

    // Fallback: use best partial path (closest reachable node to target)
    auto BuildPath = [&](int FromIdx) {
        std::vector<Core::Math::Vec2> Raw;
        int P = FromIdx;
        while (P >= 0)
        {
            Raw.push_back({
                Nodes[P].X * 32.0f + 16.0f,
                Nodes[P].Y * 32.0f + 16.0f
            });
            P = Nodes[P].ParentIndex;
        }
        std::reverse(Raw.begin(), Raw.end());
        return Raw;
    };

    if (BestGoalIdx >= 0)
    {
        auto Raw = BuildPath(BestGoalIdx);

        // Smooth
        if (Raw.size() > 2)
        {
            Result.Waypoints.push_back(Raw[0]);
            for (size_t I = 1; I < Raw.size(); I++)
            {
                int PX, PY, NX, NY;
                Map.WorldToTile(Result.Waypoints.back(), PX, PY);
                Map.WorldToTile(Raw[I], NX, NY);
                if (!HasLineOfSight(Map, PX, PY, NX, NY, ClearanceRadiusPixels))
                    Result.Waypoints.push_back(Raw[I - 1]);
            }
            Result.Waypoints.push_back(Raw.back());
        }
        else
        {
            Result.Waypoints = std::move(Raw);
        }
        Result.Found = true;
    }

    return Result;
}

std::vector<Core::Math::Vec2> AStar::ReconstructPath(const std::vector<PathNode>& Nodes, int GoalIndex)
{
    (void)Nodes;
    (void)GoalIndex;
    return {};
}

} // namespace Game::Pathfinding
