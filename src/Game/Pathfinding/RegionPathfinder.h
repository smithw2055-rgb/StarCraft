#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Core/Math/Vec2.h"
#include "Core/Math/FixedPoint.h"

namespace Game::Pathfinding
{

struct RegionNode
{
    int RegionID = -1;
    int ParentRegionID = -1;
    int G = 0;
    int H = 0;
    int F() const { return G + H; }
};

struct Region
{
    int ID = -1;
    int MinX = 0, MinY = 0;
    int MaxX = 0, MaxY = 0;
    int CenterX = 0, CenterY = 0;
    int TileCount = 0;
    int ConnectivityGroup = -1;
    std::vector<int> NeighborRegions;
    std::vector<int> BorderTiles;
};

struct ContourPoint
{
    int X = 0, Y = 0;
    int NextIndex = -1;
};

class RegionPathfinder
{
public:
    static constexpr int kMaxRegions = 4096;
    static constexpr int kMaxMapWidth = 256;
    static constexpr int kMaxMapHeight = 256;
    static constexpr int kMaxShortPath = 4096;

    struct MapContext
    {
        int MapWidth = 0;
        int MapHeight = 0;
        bool Walkable[kMaxMapWidth][kMaxMapHeight] = {};
        int TileRegion[kMaxMapWidth][kMaxMapHeight] = {};  // region ID per tile, -1 = unwalkable
    };

    void BuildRegions(const MapContext& Map)
    {
        mMap = Map;
        mRegions.clear();
        mRegions.reserve(kMaxRegions);

        std::vector<int> visited(mMap.MapWidth * mMap.MapHeight, 0);
        int regionID = 0;

        for (int y = 0; y < mMap.MapHeight; ++y)
        {
            for (int x = 0; x < mMap.MapWidth; ++x)
            {
                if (!mMap.Walkable[x][y] || visited[y * mMap.MapWidth + x])
                    continue;

                Region region;
                region.ID = regionID++;
                FloodFillRegion(x, y, region, visited);
                mRegions.push_back(region);
            }
        }

        BuildConnectivity();
    }

    struct LongPathResult
    {
        std::vector<int> RegionSequence;
        bool Found = false;
    };

    LongPathResult FindRegionPath(int StartTileX, int StartTileY, int EndTileX, int EndTileY)
    {
        LongPathResult result;

        int startRegion = GetRegionAt(StartTileX, StartTileY);
        int endRegion = GetRegionAt(EndTileX, EndTileY);

        if (startRegion < 0 || endRegion < 0)
            return result;

        if (startRegion == endRegion)
        {
            result.RegionSequence = {startRegion};
            result.Found = true;
            return result;
        }

        int groupStart = GetConnectivityGroup(startRegion);
        int groupEnd = GetConnectivityGroup(endRegion);

        if (groupStart != groupEnd || groupStart < 0)
            return result;

        result = AStarRegionSearch(startRegion, endRegion);
        return result;
    }

    struct ShortPathResult
    {
        std::vector<Core::Math::Vec2> Waypoints;
        bool Found = false;
    };

    ShortPathResult FindShortPath(int StartX, int StartY, int EndX, int EndY)
    {
        ShortPathResult result;

        struct TileNode
        {
            int X = 0, Y = 0, G = 0, H = 0, ParentIdx = -1;
            int F() const { return G + H; }
        };

        auto IsWalkableTile = [&](int X, int Y) -> bool {
            if (X < 0 || X >= (int)mMap.MapWidth) return false;
            if (Y < 0 || Y >= (int)mMap.MapHeight) return false;
            return mMap.Walkable[X][Y];
        };

        if (!IsWalkableTile(EndX, EndY))
            return result;

        std::vector<TileNode> nodes;
        nodes.reserve(kMaxShortPath);
        nodes.push_back({StartX, StartY, 0, Manhattan(StartX, StartY, EndX, EndY), -1});

        std::vector<bool> closed(mMap.MapWidth * mMap.MapHeight, false);
        int closedIdx = StartY * mMap.MapWidth + StartX;
        if (closedIdx < (int)closed.size()) closed[closedIdx] = true;

        while (!nodes.empty() && (int)nodes.size() < kMaxShortPath)
        {
            int best = 0;
            for (int i = 1; i < (int)nodes.size(); ++i)
                if (nodes[i].F() < nodes[best].F()) best = i;

            TileNode current = nodes[best];

            if (current.X == EndX && current.Y == EndY)
            {
                result.Found = true;
                std::vector<Core::Math::Vec2> rev;
                const TileNode* node = &nodes[best];
                while (node)
                {
                    rev.push_back({(float)node->X * 32.0f + 16.0f, (float)node->Y * 32.0f + 16.0f});
                    if (node->ParentIdx < 0 || node->ParentIdx >= (int)nodes.size()) break;
                    node = &nodes[(size_t)node->ParentIdx];
                }
                std::reverse(rev.begin(), rev.end());
                result.Waypoints = rev;
                return result;
            }

            // Move current node to end (logically remove)
            size_t curIdx = (size_t)best;
            nodes.erase(nodes.begin() + best);

            const int dx[] = {0, 1, 0, -1, 1, 1, -1, -1};
            const int dy[] = {-1, 0, 1, 0, -1, 1, 1, -1};
            for (int d = 0; d < 8; ++d)
            {
                int nx = current.X + dx[d];
                int ny = current.Y + dy[d];
                if (!IsWalkableTile(nx, ny)) continue;
                int cidx = ny * mMap.MapWidth + nx;
                if (cidx >= 0 && cidx < (int)closed.size() && closed[cidx]) continue;
                if (d >= 4)
                {
                    if (!IsWalkableTile(current.X + dx[d], current.Y)) continue;
                    if (!IsWalkableTile(current.X, current.Y + dy[d])) continue;
                }

                int g = current.G + ((d >= 4) ? 14 : 10);
                if (cidx >= 0 && cidx < (int)closed.size()) closed[cidx] = true;
                nodes.push_back({nx, ny, g, Manhattan(nx, ny, EndX, EndY), (int)curIdx});
            }
        }

        return result;
    }

    int GetRegionAt(int TileX, int TileY) const
    {
        if (TileX < 0 || TileX >= mMap.MapWidth || TileY < 0 || TileY >= mMap.MapHeight)
            return -1;
        return mMap.TileRegion[TileX][TileY];
    }

    const Region* GetRegion(int RegionID) const
    {
        if (RegionID < 0 || RegionID >= (int)mRegions.size()) return nullptr;
        return &mRegions[RegionID];
    }

    int GetConnectivityGroup(int RegionID) const
    {
        const Region* r = GetRegion(RegionID);
        return r ? r->ConnectivityGroup : -1;
    }

    bool IsWalkable(int X, int Y) const
    {
        if (X < 0 || X >= mMap.MapWidth || Y < 0 || Y >= mMap.MapHeight)
            return false;
        return mMap.Walkable[X][Y];
    }

    void BuildConnectivity()
    {
        for (auto& region : mRegions)
        {
            for (int y = region.MinY; y <= region.MaxY; ++y)
            {
                for (int x = region.MinX; x <= region.MaxX; ++x)
                {
                    if (!mMap.Walkable[x][y]) continue;
                    if (mMap.TileRegion[x][y] != region.ID) continue;

                    const int dx[] = {1, 0, -1, 0};
                    const int dy[] = {0, 1, 0, -1};
                    for (int d = 0; d < 4; ++d)
                    {
                        int nx = x + dx[d];
                        int ny = y + dy[d];
                        if (nx < 0 || nx >= mMap.MapWidth || ny < 0 || ny >= mMap.MapHeight) continue;
                        int neighbor = mMap.TileRegion[nx][ny];
                        if (neighbor >= 0 && neighbor != region.ID)
                        {
                            bool exists = false;
                            for (int n : region.NeighborRegions)
                                if (n == neighbor) { exists = true; break; }
                            if (!exists) region.NeighborRegions.push_back(neighbor);
                        }
                    }
                }
            }
        }

        std::vector<bool> regionVisited(mRegions.size(), false);
        for (size_t i = 0; i < mRegions.size(); ++i)
        {
            if (regionVisited[i]) continue;
            FloodConnectivity((int)i, (int)i, regionVisited);
        }
    }

private:
    void FloodFillRegion(int StartX, int StartY, Region& OutRegion, std::vector<int>& Visited)
    {
        std::vector<std::pair<int, int>> stack;
        stack.push_back({StartX, StartY});

        OutRegion.MinX = OutRegion.MaxX = StartX;
        OutRegion.MinY = OutRegion.MaxY = StartY;

        int mapW = mMap.MapWidth;

        while (!stack.empty())
        {
            auto [x, y] = stack.back();
            stack.pop_back();

            if (x < 0 || x >= mMap.MapWidth || y < 0 || y >= mMap.MapHeight) continue;
            if (!mMap.Walkable[x][y]) continue;
            if (Visited[y * mapW + x]) continue;

            Visited[y * mapW + x] = 1;
            mMap.TileRegion[x][y] = OutRegion.ID;
            OutRegion.TileCount++;

            if (x < OutRegion.MinX) OutRegion.MinX = x;
            if (x > OutRegion.MaxX) OutRegion.MaxX = x;
            if (y < OutRegion.MinY) OutRegion.MinY = y;
            if (y > OutRegion.MaxY) OutRegion.MaxY = y;

            stack.push_back({x + 1, y});
            stack.push_back({x - 1, y});
            stack.push_back({x, y + 1});
            stack.push_back({x, y - 1});
        }

        OutRegion.CenterX = (OutRegion.MinX + OutRegion.MaxX) / 2;
        OutRegion.CenterY = (OutRegion.MinY + OutRegion.MaxY) / 2;
    }

    void FloodConnectivity(int RegionID, int GroupID, std::vector<bool>& Visited)
    {
        if (Visited[RegionID]) return;
        Visited[RegionID] = true;
        mRegions[RegionID].ConnectivityGroup = GroupID;

        for (int neighbor : mRegions[RegionID].NeighborRegions)
            FloodConnectivity(neighbor, GroupID, Visited);
    }

    LongPathResult AStarRegionSearch(int StartRegion, int EndRegion)
    {
        LongPathResult result;
        std::vector<RegionNode> openList;
        std::unordered_map<int, int> closed;

        openList.push_back({StartRegion, -1, 0, RegionHeuristic(StartRegion, EndRegion)});

        while (!openList.empty())
        {
            int best = 0;
            for (int i = 1; i < (int)openList.size(); ++i)
                if (openList[i].F() < openList[best].F()) best = i;

            RegionNode current = openList[best];
            openList.erase(openList.begin() + best);

            if (current.RegionID == EndRegion)
            {
                result.Found = true;
                result.RegionSequence.push_back(current.RegionID);
                int parent = current.ParentRegionID;
                for (int i = (int)openList.size() - 1; i >= 0; --i)
                {
                    if (openList[i].RegionID == parent)
                    {
                        result.RegionSequence.insert(result.RegionSequence.begin(), openList[i].RegionID);
                        parent = openList[i].ParentRegionID;
                    }
                }
                return result;
            }

            if (closed[current.RegionID]) continue;
            closed[current.RegionID] = 1;

            const Region* region = GetRegion(current.RegionID);
            if (!region) continue;

            for (int neighbor : region->NeighborRegions)
            {
                if (closed[neighbor]) continue;
                int g = current.G + 1;
                openList.push_back({neighbor, current.RegionID, g, RegionHeuristic(neighbor, EndRegion)});
            }
        }

        return result;
    }

    int RegionHeuristic(int A, int B) const
    {
        const Region* ra = GetRegion(A);
        const Region* rb = GetRegion(B);
        if (!ra || !rb) return 999999;
        return std::abs(ra->CenterX - rb->CenterX) + std::abs(ra->CenterY - rb->CenterY);
    }

    int Manhattan(int Ax, int Ay, int Bx, int By) const
    {
        return std::abs(Ax - Bx) * 10 + std::abs(Ay - By) * 10;
    }

    MapContext mMap;
    std::vector<Region> mRegions;
};

} // namespace Game::Pathfinding
