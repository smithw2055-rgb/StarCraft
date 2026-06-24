#pragma once

#include <vector>
#include <cstdint>
#include <functional>

#include "Core/Math/Vec2.h"

namespace Game::Map { class GameMap; }

namespace Game::Pathfinding
{

struct PathNode
{
    int X = 0, Y = 0;
    int G = 0;
    int H = 0;
    int ParentIndex = -1;

    int F() const { return G + H; }
};

struct PathResult
{
    std::vector<Core::Math::Vec2> Waypoints;
    bool Found = false;
    int NodesSearched = 0;
};

class AStar
{
public:
    static PathResult FindPath(const Map::GameMap& Map,
                               Core::Math::Vec2 Start, Core::Math::Vec2 End,
                               int MaxNodes = 1000);

    static PathResult FindPathTile(const Map::GameMap& Map,
                                   int StartX, int StartY,
                                   int EndX, int EndY,
                                   int MaxNodes = 1000);

    struct SearchConfig
    {
        bool AllowDiagonal = true;
        bool CutCorners = false;
        int HeuristicWeight = 10;
    };

    static void SetConfig(const SearchConfig& Config) { sConfig = Config; }

private:
    static int Heuristic(int DX, int DY);
    static bool IsWalkable(const Map::GameMap& Map, int X, int Y);
    static std::vector<Core::Math::Vec2> ReconstructPath(const std::vector<PathNode>& Nodes, int GoalIndex);

    static SearchConfig sConfig;
};

} // namespace Game::Pathfinding
