#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

#include "Engine/Navigation/NavigationServer2D.h"
#include "Game/Pathfinding/NavigationGrid.h"

namespace Game::Pathfinding
{

struct NavigationPathQuery
{
    Core::Math::Vec2 Start;
    Core::Math::Vec2 Target;
    NavigationFootprint Footprint;
    int IgnoredOwnerEntityID = -1;
    int AdditionalIgnoredOwnerEntityID = -1;
    int MaximumExpandedCells = 8192;
    bool HasAvoidedObstacle = false;
    NavigationObstacle AvoidedObstacle;
};

struct NavigationPathResult
{
    std::vector<Core::Math::Vec2> Waypoints;
    PathStatus Status = PathStatus::Unreachable;
    PathEndReason EndReason = PathEndReason::Disconnected;
    Core::Math::Vec2 ResolvedDestination;
    int ExpandedCells = 0;
    uint32_t NavigationVersion = 0;
    int AdditionalIgnoredOwnerEntityID = -1;

    bool HasUsablePath() const
    {
        return Status != PathStatus::Unreachable && !Waypoints.empty();
    }
};

struct NavigationPerformanceStats
{
    uint64_t QueryCount = 0;
    uint64_t CorridorCacheHits = 0;
    uint64_t ExpandedCells = 0;
    uint64_t PartialPaths = 0;
    uint64_t FailedPaths = 0;
    uint64_t QueryMicroseconds = 0;

    void Reset()
    {
        *this = {};
    }
};

class NavigationService
{
public:
    static constexpr int ClusterCellSize = 16;

    void SetBaseGrid(const NavigationGrid& Grid)
    {
        if (mBaseGridVersion == Grid.GetVersion() &&
            mGrid.GetWidth() == Grid.GetWidth() &&
            mGrid.GetHeight() == Grid.GetHeight())
        {
            return;
        }
        Grid.EnsureTerrainComponents();
        mBaseGridVersion = Grid.GetVersion();
        mGrid = Grid;
        mCorridorCache.clear();
        ResetWorkspace();
    }

    const NavigationGrid& GetGrid() const { return mGrid; }
    uint32_t GetVersion() const { return mGrid.GetVersion(); }

    void SyncStaticObstacles(std::vector<NavigationObstacle> Obstacles)
    {
        const uint32_t OldVersion = mGrid.GetVersion();
        mGrid.SyncStaticObstacles(std::move(Obstacles));
        if (OldVersion != mGrid.GetVersion())
            mCorridorCache.clear();
    }

    const NavigationPerformanceStats& GetStats() const { return mStats; }
    void BindEngineNavigationServer(Engine::Navigation::NavigationServer2D* Server) noexcept { mBoundEngineNav = Server; }
    Engine::Navigation::NavigationServer2D* GetBoundEngineNavigationServer() const noexcept { return mBoundEngineNav; }
    void ResetStats() { mStats.Reset(); }

    NavigationPathResult QueryPath(NavigationPathQuery Query)
    {
        const auto BeginTime = std::chrono::steady_clock::now();
        ++mStats.QueryCount;

        struct TempObstacleGuard
        {
            NavigationGrid& Grid;
            bool Active = false;
            TempObstacleGuard(NavigationGrid& InGrid, const NavigationPathQuery& InQuery)
                : Grid(InGrid), Active(InQuery.HasAvoidedObstacle)
            {
                if (Active)
                    Grid.SetTemporaryObstacle(InQuery.AvoidedObstacle);
            }
            ~TempObstacleGuard()
            {
                if (Active)
                    Grid.ClearTemporaryObstacle();
            }
        } TempGuard(mGrid, Query);

        NavigationPathResult Result;
        Result.NavigationVersion = mGrid.GetVersion();
        Result.ResolvedDestination = Query.Start;

        if (mGrid.GetWidth() <= 0 || mGrid.GetHeight() <= 0)
        {
            FinishStats(Result, BeginTime);
            return Result;
        }

        int StartX = 0;
        int StartY = 0;
        int TargetX = 0;
        int TargetY = 0;
        mGrid.WorldToCell(Query.Start, StartX, StartY);
        mGrid.WorldToCell(Query.Target, TargetX, TargetY);
        StartX = std::clamp(StartX, 0, mGrid.GetWidth() - 1);
        StartY = std::clamp(StartY, 0, mGrid.GetHeight() - 1);
        TargetX = std::clamp(TargetX, 0, mGrid.GetWidth() - 1);
        TargetY = std::clamp(TargetY, 0, mGrid.GetHeight() - 1);

        if (Query.AdditionalIgnoredOwnerEntityID < 0)
        {
            int StartOwner = mGrid.GetStaticOwner(StartX, StartY);
            if (StartOwner < 0)
            {
                const auto StartWorld = mGrid.CellToWorld(StartX, StartY);
                const int CenterPixelX = (int)StartWorld.X;
                const int CenterPixelY = (int)StartWorld.Y;
                const int MinX = mGrid.WorldPixelToCell(CenterPixelX - std::max(0, Query.Footprint.Left));
                const int MaxX = mGrid.WorldPixelToCell(CenterPixelX + std::max(0, Query.Footprint.Right));
                const int MinY = mGrid.WorldPixelToCell(CenterPixelY - std::max(0, Query.Footprint.Up));
                const int MaxY = mGrid.WorldPixelToCell(CenterPixelY + std::max(0, Query.Footprint.Down));
                for (int Y = MinY; Y <= MaxY && StartOwner < 0; ++Y)
                {
                    for (int X = MinX; X <= MaxX && StartOwner < 0; ++X)
                    {
                        const int Owner = mGrid.GetStaticOwner(X, Y);
                        if (Owner >= 0 && Owner != Query.IgnoredOwnerEntityID)
                            StartOwner = Owner;
                    }
                }
            }
            if (StartOwner >= 0 && StartOwner != Query.IgnoredOwnerEntityID)
            {
                Query.AdditionalIgnoredOwnerEntityID = StartOwner;
            }
        }
        Result.AdditionalIgnoredOwnerEntityID = Query.AdditionalIgnoredOwnerEntityID;

        const int FootprintR = std::clamp(
            Query.Footprint.MinCellRadius(), 0,
            NavigationGrid::MaxCachedClearanceRadius);
        const bool UseClearanceComp =
            !Query.HasAvoidedObstacle &&
            Query.Footprint.MaxCellRadius() <= NavigationGrid::MaxCachedClearanceRadius;
        const int StartTerrainComp = mGrid.GetEffectiveTerrainComponent(StartX, StartY);
        const ReachableClearanceSet StartClearanceComps = UseClearanceComp
            ? CollectReachableClearanceComponents(StartX, StartY, FootprintR, Query)
            : ReachableClearanceSet{};
        int StartClearanceComp = StartClearanceComps.Primary();

        const bool OriginalTargetPassable = mGrid.CanOccupy(
            TargetX, TargetY, Query.Footprint, Query.IgnoredOwnerEntityID,
            Query.AdditionalIgnoredOwnerEntityID);

        int EffectiveTargetX = TargetX;
        int EffectiveTargetY = TargetY;
        bool EffectiveTargetPassable = OriginalTargetPassable;
        bool GoalIsExactTarget = OriginalTargetPassable;

        if (!OriginalTargetPassable)
        {
            int ProjectedX = -1;
            int ProjectedY = -1;
            if (FindNearestPassableCell(
                    TargetX, TargetY, StartX, StartY,
                    StartTerrainComp, StartClearanceComps, FootprintR, UseClearanceComp,
                    Query, 64, ProjectedX, ProjectedY))
            {
                EffectiveTargetX = ProjectedX;
                EffectiveTargetY = ProjectedY;
                EffectiveTargetPassable = true;
            }
        }

        if (StartX == TargetX && StartY == TargetY)
        {
            Result.Status = OriginalTargetPassable ? PathStatus::Exact : PathStatus::Partial;
            Result.EndReason = OriginalTargetPassable ? PathEndReason::None :
                PathEndReason::GoalBlocked;
            Result.ResolvedDestination = OriginalTargetPassable ? Query.Target :
                mGrid.CellToWorld(StartX, StartY);
            Result.Waypoints.push_back(Result.ResolvedDestination);
            FinishStats(Result, BeginTime);
            return Result;
        }

        const bool StartHasLocalEscape = HasLocalEscape(
            StartX, StartY, EffectiveTargetX, EffectiveTargetY, Query, 32, 6);

        if (EffectiveTargetPassable && UseClearanceComp && !StartClearanceComps.Empty())
        {
            const int TargetTerrainComp =
                mGrid.GetEffectiveTerrainComponent(EffectiveTargetX, EffectiveTargetY);
            const bool SameTerrain =
                (StartTerrainComp == 0 || TargetTerrainComp == 0 ||
                 StartTerrainComp == TargetTerrainComp);
            const ReachableClearanceSet EffectiveTargetClearanceComps =
                CollectReachableClearanceComponents(
                    EffectiveTargetX, EffectiveTargetY, FootprintR, Query);
            const int SharedClearanceComp =
                StartClearanceComps.FirstSharedWith(EffectiveTargetClearanceComps);
            if (SharedClearanceComp > 0)
            {
                StartClearanceComp = SharedClearanceComp;
            }
            else if (SameTerrain)
            {
                int ProjectedX = -1;
                int ProjectedY = -1;
                if (StartHasLocalEscape &&
                    ProjectToStartClearanceComponent(
                        TargetX, TargetY, StartX, StartY,
                        StartClearanceComps, FootprintR, Query,
                        ProjectedX, ProjectedY))
                {
                    EffectiveTargetX = ProjectedX;
                    EffectiveTargetY = ProjectedY;
                    EffectiveTargetPassable = true;
                    GoalIsExactTarget = false;
                    const int ProjectedComp =
                        mGrid.GetClearanceComponent(EffectiveTargetX, EffectiveTargetY, FootprintR);
                    if (ProjectedComp > 0)
                        StartClearanceComp = ProjectedComp;
                }
                else
                {
                    Result.Status = PathStatus::Unreachable;
                    Result.EndReason = OriginalTargetPassable
                        ? PathEndReason::Disconnected
                        : PathEndReason::GoalBlocked;
                    FinishStats(Result, BeginTime);
                    return Result;
                }
            }
        }

        if (StartX == EffectiveTargetX && StartY == EffectiveTargetY && !GoalIsExactTarget)
        {
            Result.Status = PathStatus::Partial;
            Result.EndReason = PathEndReason::GoalBlocked;
            Result.ResolvedDestination = mGrid.CellToWorld(StartX, StartY);
            Result.Waypoints.push_back(Result.ResolvedDestination);
            FinishStats(Result, BeginTime);
            return Result;
        }

        // Most harvest, return and construction paths are short and
        // unobstructed. Running the portal corridor and fine A* for a segment
        // already proven safe by footprint-aware supercover LOS is pure work.
        if (GoalIsExactTarget &&
            HasLineOfSight(StartX, StartY, TargetX, TargetY, Query))
        {
            Result.Status = PathStatus::Exact;
            Result.EndReason = PathEndReason::None;
            Result.ResolvedDestination = Query.Target;
            Result.Waypoints.push_back(Query.Target);
            FinishStats(Result, BeginTime);
            return Result;
        }

        if (EffectiveTargetPassable &&
            HasLineOfSight(StartX, StartY, EffectiveTargetX, EffectiveTargetY, Query))
        {
            Result.Status = GoalIsExactTarget ? PathStatus::Exact : PathStatus::Partial;
            Result.EndReason = GoalIsExactTarget ? PathEndReason::None : PathEndReason::GoalBlocked;
            Result.ResolvedDestination = mGrid.CellToWorld(EffectiveTargetX, EffectiveTargetY);
            Result.Waypoints.push_back(Result.ResolvedDestination);
            FinishStats(Result, BeginTime);
            return Result;
        }

        const int Width = mGrid.GetWidth();
        const int StartIndex = StartY * Width + StartX;
        const bool TargetHasLocalEscape = !EffectiveTargetPassable ||
            (UseClearanceComp && StartClearanceComp > 0 &&
             mGrid.GetClearanceComponent(EffectiveTargetX, EffectiveTargetY, FootprintR) == StartClearanceComp) ||
            HasLocalEscape(EffectiveTargetX, EffectiveTargetY, StartX, StartY, Query, 32, 6);
        const int NodeBudget = StartHasLocalEscape
            ? (TargetHasLocalEscape
                ? std::max(1, Query.MaximumExpandedCells)
                : std::min(std::max(1, Query.MaximumExpandedCells), 512))
            : std::min(std::max(1, Query.MaximumExpandedCells), 64);

        const std::vector<int> Corridor = GetClusterCorridor(
            StartX, StartY, EffectiveTargetX, EffectiveTargetY, Query);
        if (!Corridor.empty())
        {
            BuildAllowedClusterMask(Corridor, mAllowedClusters);
        }
        else
        {
            mAllowedClusters.clear();
        }

        AStarSearchState State;
        if (!mAllowedClusters.empty())
        {
            State = ExecuteAStar(
                StartX, StartY, EffectiveTargetX, EffectiveTargetY,
                Query, mAllowedClusters, NodeBudget,
                EffectiveTargetPassable, Result);
        }

        const int EffectiveTargetTerrainComp =
            mGrid.GetEffectiveTerrainComponent(EffectiveTargetX, EffectiveTargetY);
        const bool SameEffectiveTerrain =
            (StartTerrainComp == 0 || EffectiveTargetTerrainComp == 0 ||
             StartTerrainComp == EffectiveTargetTerrainComp);
        if (!State.ReachedTarget &&
            ((State.PrunedByClusterMask && !Corridor.empty()) ||
             (Corridor.empty() && SameEffectiveTerrain)) &&
            EffectiveTargetPassable &&
            StartHasLocalEscape && TargetHasLocalEscape)
        {
            // Coarse cluster corridor had an internal impassable barrier (e.g. cliff /
            // ramp disconnect). Fall back to unconstrained search across
            // the navigation grid with a bounded budget to find the genuine passable route.
            mAllowedClusters.clear();
            const int FallbackBudget = std::min(NodeBudget, 8192);
            const auto FallbackState = ExecuteAStar(
                StartX, StartY, EffectiveTargetX, EffectiveTargetY,
                Query, mAllowedClusters, FallbackBudget,
                EffectiveTargetPassable, Result);
            if (FallbackState.ReachedTarget ||
                FallbackState.BestH < State.BestH ||
                (State.BestIndex == StartIndex && FallbackState.BestIndex != StartIndex))
            {
                State = FallbackState;
            }
        }

        if (State.BestIndex == StartIndex || State.BestIndex < 0)
        {
            Result.Status = PathStatus::Unreachable;
            Result.EndReason = State.BudgetReached ? PathEndReason::NodeBudget :
                (GoalIsExactTarget ? PathEndReason::Disconnected :
                    PathEndReason::GoalBlocked);
            FinishStats(Result, BeginTime);
            return Result;
        }

        std::vector<std::pair<int, int>> Cells;
        for (int Index = State.BestIndex; Index >= 0; Index = mParent[Index])
            Cells.push_back({Index % Width, Index / Width});
        std::reverse(Cells.begin(), Cells.end());
        const std::vector<std::pair<int, int>> Smoothed =
            SmoothCells(Cells, Query);

        for (size_t Index = 1; Index < Smoothed.size(); ++Index)
        {
            Result.Waypoints.push_back(
                mGrid.CellToWorld(Smoothed[Index].first, Smoothed[Index].second));
        }
        if (Result.Waypoints.empty())
            Result.Waypoints.push_back(mGrid.CellToWorld(
                State.BestIndex % Width, State.BestIndex / Width));

        Result.Status = State.ReachedTarget
            ? (GoalIsExactTarget ? PathStatus::Exact : PathStatus::Partial)
            : ((State.BestIndex != StartIndex && State.BestIndex >= 0)
                ? PathStatus::Partial
                : PathStatus::Unreachable);
        Result.EndReason = State.ReachedTarget
            ? (GoalIsExactTarget ? PathEndReason::None : PathEndReason::GoalBlocked)
            : (State.BudgetReached ? PathEndReason::NodeBudget :
                (GoalIsExactTarget ? PathEndReason::Disconnected :
                    PathEndReason::GoalBlocked));
        Result.ResolvedDestination = Result.Waypoints.back();
        if (GoalIsExactTarget && State.ReachedTarget &&
            mGrid.CanOccupyWorld(Query.Target, Query.Footprint,
                Query.IgnoredOwnerEntityID,
                Query.AdditionalIgnoredOwnerEntityID))
        {
            Result.Waypoints.back() = Query.Target;
            Result.ResolvedDestination = Query.Target;
        }

        FinishStats(Result, BeginTime);
        return Result;
    }

private:
    struct CorridorKey
    {
        uint32_t Version = 0;
        int StartCluster = -1;
        int TargetCluster = -1;
        int FootprintSignature = 0;
        int IgnoredOwnerEntityID = -1;
        int AdditionalIgnoredOwnerEntityID = -1;

        bool operator<(const CorridorKey& Other) const
        {
            return std::tie(Version, StartCluster, TargetCluster, FootprintSignature,
                            IgnoredOwnerEntityID, AdditionalIgnoredOwnerEntityID) <
                std::tie(Other.Version, Other.StartCluster, Other.TargetCluster, Other.FootprintSignature,
                         Other.IgnoredOwnerEntityID, Other.AdditionalIgnoredOwnerEntityID);
        }
    };

    struct ReachableClearanceSet
    {
        std::array<int, 8> Comps{};
        int Count = 0;

        bool Empty() const
        {
            return Count <= 0;
        }

        int Primary() const
        {
            return Count > 0 ? Comps[0] : 0;
        }

        bool Contains(int Comp) const
        {
            if (Comp <= 0)
                return false;
            for (int Index = 0; Index < Count; ++Index)
            {
                if (Comps[Index] == Comp)
                    return true;
            }
            return false;
        }

        void Add(int Comp)
        {
            if (Comp <= 0 || Contains(Comp) || Count >= (int)Comps.size())
                return;
            Comps[Count++] = Comp;
        }

        int FirstSharedWith(const ReachableClearanceSet& Other) const
        {
            for (int Index = 0; Index < Count; ++Index)
            {
                if (Other.Contains(Comps[Index]))
                    return Comps[Index];
            }
            return 0;
        }
    };

    ReachableClearanceSet CollectReachableClearanceComponents(
        int StartCellX, int StartCellY,
        int FootprintR,
        const NavigationPathQuery& Query,
        int StopOnComp = 0) const
    {
        ReachableClearanceSet Result;
        if (!mGrid.IsInBounds(StartCellX, StartCellY))
            return Result;
        const int Direct = mGrid.GetClearanceComponent(StartCellX, StartCellY, FootprintR);
        if (Direct > 0)
        {
            Result.Add(Direct);
            return Result;
        }

        if (!mGrid.CanOccupy(StartCellX, StartCellY, Query.Footprint,
                             Query.IgnoredOwnerEntityID,
                             Query.AdditionalIgnoredOwnerEntityID))
        {
            return Result;
        }

        constexpr int WindowRadius = 18;
        constexpr int WindowSide = WindowRadius * 2 + 1;
        uint8_t Visited[WindowSide * WindowSide]{};
        std::pair<int8_t, int8_t> Queue[WindowSide * WindowSide];
        int Head = 0;
        int Tail = 0;

        Visited[WindowRadius * WindowSide + WindowRadius] = 1;
        Queue[Tail++] = {static_cast<int8_t>(0), static_cast<int8_t>(0)};

        const bool StartOverlapsIgnored =
            (Query.AdditionalIgnoredOwnerEntityID >= 0) &&
            !mGrid.CanOccupy(StartCellX, StartCellY, Query.Footprint,
                             Query.IgnoredOwnerEntityID, -1);
        const int AdditionalIgnored = StartOverlapsIgnored
            ? Query.AdditionalIgnoredOwnerEntityID
            : -1;

        static constexpr int DX[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
        static constexpr int DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };

        while (Head < Tail)
        {
            const auto [RelX, RelY] = Queue[Head++];
            const int CurX = StartCellX + (int)RelX;
            const int CurY = StartCellY + (int)RelY;

            bool CardinalPassable[4] = {false, false, false, false};
            for (int Dir = 0; Dir < 8; ++Dir)
            {
                const int NextRelX = (int)RelX + DX[Dir];
                const int NextRelY = (int)RelY + DY[Dir];
                if (std::abs(NextRelX) > WindowRadius || std::abs(NextRelY) > WindowRadius)
                    continue;
                const int NextX = StartCellX + NextRelX;
                const int NextY = StartCellY + NextRelY;
                if (!mGrid.IsInBounds(NextX, NextY))
                    continue;

                if (Dir >= 4)
                {
                    const bool CardOk =
                        (Dir == 4 && CardinalPassable[0] && CardinalPassable[1]) ||
                        (Dir == 5 && CardinalPassable[2] && CardinalPassable[1]) ||
                        (Dir == 6 && CardinalPassable[2] && CardinalPassable[3]) ||
                        (Dir == 7 && CardinalPassable[0] && CardinalPassable[3]);
                    if (!CardOk)
                        continue;
                }

                if (!mGrid.CanTraverse(CurX, CurY, NextX, NextY,
                        Query.Footprint, Query.IgnoredOwnerEntityID, AdditionalIgnored))
                {
                    continue;
                }
                if (Dir < 4)
                    CardinalPassable[Dir] = true;

                const int Comp = mGrid.GetClearanceComponent(NextX, NextY, FootprintR);
                if (Comp > 0)
                {
                    Result.Add(Comp);
                    if (StopOnComp > 0 && Comp == StopOnComp)
                        return Result;
                    continue;
                }

                const int VisitedIdx =
                    (NextRelY + WindowRadius) * WindowSide + (NextRelX + WindowRadius);
                if (Visited[VisitedIdx])
                    continue;
                Visited[VisitedIdx] = 1;
                Queue[Tail++] = {(int8_t)NextRelX, (int8_t)NextRelY};
            }
        }
        return Result;
    }

    bool ProjectToStartClearanceComponent(
        int TargetX, int TargetY,
        int StartX, int StartY,
        const ReachableClearanceSet& StartClearanceComps,
        int FootprintR,
        const NavigationPathQuery& Query,
        int& OutX, int& OutY) const
    {
        if (StartClearanceComps.Empty())
            return false;

        int AnchorX = StartX;
        int AnchorY = StartY;
        {
            int X = TargetX;
            int Y = TargetY;
            const int DX = std::abs(StartX - TargetX);
            const int DY = std::abs(StartY - TargetY);
            const int SX = (TargetX < StartX) ? 1 : -1;
            const int SY = (TargetY < StartY) ? 1 : -1;
            int Err = DX - DY;
            while (true)
            {
                if (StartClearanceComps.Contains(
                        mGrid.GetClearanceComponent(X, Y, FootprintR)))
                {
                    AnchorX = X;
                    AnchorY = Y;
                    break;
                }
                if (X == StartX && Y == StartY)
                    break;
                const int E2 = Err * 2;
                if (E2 > -DY) { Err -= DY; X += SX; }
                if (E2 < DX) { Err += DX; Y += SY; }
            }
        }

        const bool NeedsExactOccupyCheck =
            Query.Footprint.MaxCellRadius() > FootprintR;
        int BestX = -1;
        int BestY = -1;
        int BestDistSq = std::numeric_limits<int>::max();
        int BestStartDistSq = std::numeric_limits<int>::max();

        auto SearchWindow = [&](int CenterX, int CenterY, int Radius)
        {
            const int MinY = std::max(0, CenterY - Radius);
            const int MaxY = std::min(mGrid.GetHeight() - 1, CenterY + Radius);
            const int MinX = std::max(0, CenterX - Radius);
            const int MaxX = std::min(mGrid.GetWidth() - 1, CenterX + Radius);
            for (int Y = MinY; Y <= MaxY; ++Y)
            {
                for (int X = MinX; X <= MaxX; ++X)
                {
                    if (!StartClearanceComps.Contains(
                            mGrid.GetClearanceComponent(X, Y, FootprintR)))
                    {
                        continue;
                    }
                    if (NeedsExactOccupyCheck &&
                        !mGrid.CanOccupy(X, Y, Query.Footprint,
                            Query.IgnoredOwnerEntityID,
                            Query.AdditionalIgnoredOwnerEntityID))
                    {
                        continue;
                    }
                    const int DX = X - TargetX;
                    const int DY = Y - TargetY;
                    const int DistSq = DX * DX + DY * DY;
                    const int SDX = X - StartX;
                    const int SDY = Y - StartY;
                    const int StartDistSq = SDX * SDX + SDY * SDY;
                    if (std::make_tuple(DistSq, StartDistSq, Y, X) <
                        std::make_tuple(BestDistSq, BestStartDistSq, BestY, BestX))
                    {
                        BestDistSq = DistSq;
                        BestStartDistSq = StartDistSq;
                        BestX = X;
                        BestY = Y;
                    }
                }
            }
        };

        SearchWindow(AnchorX, AnchorY, 12);
        if (std::max(std::abs(TargetX - AnchorX), std::abs(TargetY - AnchorY)) <= 24)
            SearchWindow(TargetX, TargetY, 16);

        if (BestX >= 0)
        {
            OutX = BestX;
            OutY = BestY;
            return true;
        }
        return false;
    }

    bool HasLocalEscape(
        int StartCellX, int StartCellY,
        int GoalCellX, int GoalCellY,
        const NavigationPathQuery& Query,
        int MaxVisitedCells = 16,
        int RequiredChebyshevRadius = 4) const
    {
        if (!mGrid.IsInBounds(StartCellX, StartCellY))
            return false;
        if (std::max(std::abs(StartCellX - GoalCellX),
                     std::abs(StartCellY - GoalCellY)) <= 2)
        {
            return true;
        }

        constexpr int WindowRadius = 8;
        constexpr int WindowSide = WindowRadius * 2 + 1;
        uint8_t Visited[WindowSide * WindowSide]{};
        std::pair<int8_t, int8_t> Queue[WindowSide * WindowSide];
        int Head = 0;
        int Tail = 0;

        Visited[WindowRadius * WindowSide + WindowRadius] = 1;
        Queue[Tail++] = {static_cast<int8_t>(0), static_cast<int8_t>(0)};

        const bool StartOverlapsIgnored =
            (Query.AdditionalIgnoredOwnerEntityID >= 0) &&
            !mGrid.CanOccupy(StartCellX, StartCellY, Query.Footprint,
                             Query.IgnoredOwnerEntityID, -1);
        const int AdditionalIgnored = StartOverlapsIgnored
            ? Query.AdditionalIgnoredOwnerEntityID
            : -1;

        static constexpr int DX[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
        static constexpr int DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };

        while (Head < Tail)
        {
            const auto [RelX, RelY] = Queue[Head++];
            const int CurX = StartCellX + (int)RelX;
            const int CurY = StartCellY + (int)RelY;

            if (std::max(std::abs(CurX - GoalCellX), std::abs(CurY - GoalCellY)) <= 2)
                return true;
            if (std::max(std::abs((int)RelX), std::abs((int)RelY)) >= RequiredChebyshevRadius)
                return true;
            if (Tail >= MaxVisitedCells)
                return true;

            bool CardinalPassable[4] = {false, false, false, false};
            for (int Dir = 0; Dir < 8; ++Dir)
            {
                const int NextRelX = (int)RelX + DX[Dir];
                const int NextRelY = (int)RelY + DY[Dir];
                if (std::abs(NextRelX) > WindowRadius || std::abs(NextRelY) > WindowRadius)
                    continue;
                const int NextX = StartCellX + NextRelX;
                const int NextY = StartCellY + NextRelY;
                if (!mGrid.IsInBounds(NextX, NextY))
                    continue;

                if (Dir >= 4)
                {
                    const bool CardOk =
                        (Dir == 4 && CardinalPassable[0] && CardinalPassable[1]) ||
                        (Dir == 5 && CardinalPassable[2] && CardinalPassable[1]) ||
                        (Dir == 6 && CardinalPassable[2] && CardinalPassable[3]) ||
                        (Dir == 7 && CardinalPassable[0] && CardinalPassable[3]);
                    if (!CardOk)
                        continue;
                }

                if (!mGrid.CanTraverse(CurX, CurY, NextX, NextY,
                        Query.Footprint, Query.IgnoredOwnerEntityID, AdditionalIgnored))
                {
                    continue;
                }
                if (Dir < 4)
                    CardinalPassable[Dir] = true;

                const int VisitedIdx =
                    (NextRelY + WindowRadius) * WindowSide + (NextRelX + WindowRadius);
                if (Visited[VisitedIdx])
                    continue;

                Visited[VisitedIdx] = 1;
                Queue[Tail++] = {(int8_t)NextRelX, (int8_t)NextRelY};
                if (Tail >= MaxVisitedCells ||
                    std::max(std::abs(NextRelX), std::abs(NextRelY)) >= RequiredChebyshevRadius ||
                    std::max(std::abs(NextX - GoalCellX), std::abs(NextY - GoalCellY)) <= 2)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool FindNearestPassableCell(
        int CenterX, int CenterY, int StartX, int StartY,
        int StartTerrainComp, const ReachableClearanceSet& StartClearanceComps,
        int FootprintR, bool UseClearanceComp,
        const NavigationPathQuery& Query,
        int MaxRadius,
        int& OutX, int& OutY) const
    {
        if (mGrid.CanOccupy(CenterX, CenterY, Query.Footprint,
                            Query.IgnoredOwnerEntityID,
                            Query.AdditionalIgnoredOwnerEntityID))
        {
            OutX = CenterX;
            OutY = CenterY;
            return true;
        }

        const int TargetHeight = mGrid.GetHeightLevel(CenterX, CenterY);
        const int StartHeight = mGrid.IsInBounds(StartX, StartY) ? mGrid.GetHeightLevel(StartX, StartY) : -1;
        const int PreferredHeight = (StartHeight >= 0) ? StartHeight : TargetHeight;
        const int TargetTerrainComp = mGrid.GetEffectiveTerrainComponent(CenterX, CenterY);
        const bool TargetOnDisconnectedTerrain =
            (StartTerrainComp > 0 && TargetTerrainComp > 0 &&
             StartTerrainComp != TargetTerrainComp);
        int BestX = -1;
        int BestY = -1;
        int BestCompMismatch = std::numeric_limits<int>::max();
        int BestPocketPenalty = std::numeric_limits<int>::max();
        int BestHeightDiff = std::numeric_limits<int>::max();
        int BestDistSq = std::numeric_limits<int>::max();
        int BestStartDistSq = std::numeric_limits<int>::max();

        const int EffectiveMaxRadius = TargetOnDisconnectedTerrain
            ? std::min(MaxRadius, 8)
            : std::min(std::max(MaxRadius, 16), 24);
        for (int R = 1; R <= EffectiveMaxRadius; ++R)
        {
            bool FoundAtRadius = false;
            for (int DY = -R; DY <= R; ++DY)
            {
                for (int DX = -R; DX <= R; ++DX)
                {
                    if (std::abs(DX) != R && std::abs(DY) != R)
                        continue;

                    const int X = CenterX + DX;
                    const int Y = CenterY + DY;
                    if (!mGrid.IsInBounds(X, Y))
                        continue;

                    if (mGrid.CanOccupy(X, Y, Query.Footprint,
                                        Query.IgnoredOwnerEntityID,
                                        Query.AdditionalIgnoredOwnerEntityID))
                    {
                        const int CellTerrainComp = mGrid.GetEffectiveTerrainComponent(X, Y);
                        const int TerrainMismatch =
                            (StartTerrainComp > 0 && CellTerrainComp > 0 &&
                             StartTerrainComp != CellTerrainComp) ? 1 : 0;

                        int ClearanceMismatch = 0;
                        bool ProvenConnected = false;
                        if (UseClearanceComp && !StartClearanceComps.Empty())
                        {
                            const int CellClearanceComp =
                                mGrid.GetClearanceComponent(X, Y, FootprintR);
                            if (CellClearanceComp > 0)
                            {
                                if (StartClearanceComps.Contains(CellClearanceComp))
                                {
                                    ClearanceMismatch = 0;
                                    ProvenConnected = true;
                                }
                                else
                                {
                                    ClearanceMismatch = 1;
                                }
                            }
                            else
                            {
                                const ReachableClearanceSet CellComps =
                                    CollectReachableClearanceComponents(
                                        X, Y, FootprintR, Query,
                                        StartClearanceComps.Primary());
                                if (StartClearanceComps.FirstSharedWith(CellComps) > 0)
                                {
                                    ClearanceMismatch = 0;
                                    ProvenConnected = true;
                                }
                                else
                                {
                                    ClearanceMismatch = 1;
                                }
                            }
                        }

                        const int CompMismatch = TerrainMismatch * 2 + ClearanceMismatch;
                        if (CompMismatch > BestCompMismatch)
                            continue;

                        const int CellHeight = mGrid.GetHeightLevel(X, Y);
                        const int HeightDiff = (PreferredHeight >= 0 && CellHeight >= 0)
                            ? std::abs(CellHeight - PreferredHeight) : 0;
                        const int DistSq = DX * DX + DY * DY;
                        const int StartDistSq = (X - StartX) * (X - StartX) + (Y - StartY) * (Y - StartY);

                        const auto OptimisticKey = std::make_tuple(
                            CompMismatch, 0, HeightDiff, DistSq, StartDistSq);
                        const auto BestKey = std::make_tuple(
                            BestCompMismatch, BestPocketPenalty, BestHeightDiff, BestDistSq, BestStartDistSq);
                        if (OptimisticKey >= BestKey)
                            continue;

                        const int PocketPenalty = ProvenConnected
                            ? 0
                            : (HasLocalEscape(X, Y, StartX, StartY, Query, 16, 4) ? 0 : 1);
                        const auto CandidateKey = std::make_tuple(
                            CompMismatch, PocketPenalty, HeightDiff, DistSq, StartDistSq);
                        if (CandidateKey < BestKey)
                        {
                            BestCompMismatch = CompMismatch;
                            BestPocketPenalty = PocketPenalty;
                            BestHeightDiff = HeightDiff;
                            BestDistSq = DistSq;
                            BestStartDistSq = StartDistSq;
                            BestX = X;
                            BestY = Y;
                            FoundAtRadius = true;
                        }
                    }
                }
            }
            if (FoundAtRadius &&
                BestCompMismatch == 0 &&
                BestPocketPenalty == 0 &&
                BestHeightDiff == 0)
            {
                OutX = BestX;
                OutY = BestY;
                return true;
            }
            if (TargetOnDisconnectedTerrain && BestX >= 0)
                break;
        }
        if (BestX >= 0)
        {
            OutX = BestX;
            OutY = BestY;
            return true;
        }
        return false;
    }

    int ClusterWidth() const
    {
        return (mGrid.GetWidth() + ClusterCellSize - 1) / ClusterCellSize;
    }

    int ClusterHeight() const
    {
        return (mGrid.GetHeight() + ClusterCellSize - 1) / ClusterCellSize;
    }

    int ClusterIndexForCell(int X, int Y) const
    {
        return (Y / ClusterCellSize) * ClusterWidth() +
            (X / ClusterCellSize);
    }

    bool ClustersConnect(int A, int B,
                         const NavigationPathQuery& Query) const
    {
        const int ClusterW = ClusterWidth();
        const int AX = A % ClusterW;
        const int AY = A / ClusterW;
        const int BX = B % ClusterW;
        const int BY = B / ClusterW;
        const int DeltaX = BX - AX;
        const int DeltaY = BY - AY;
        if (std::abs(DeltaX) + std::abs(DeltaY) != 1)
            return false;

        if (DeltaX != 0)
        {
            const int LeftClusterX = std::min(AX, BX);
            const int LeftCellX = (LeftClusterX + 1) * ClusterCellSize - 1;
            const int RightCellX = LeftCellX + 1;
            const int FromX = (DeltaX > 0) ? LeftCellX : RightCellX;
            const int ToX = (DeltaX > 0) ? RightCellX : LeftCellX;
            const int StartY = AY * ClusterCellSize;
            const int EndY = std::min(
                mGrid.GetHeight(), (AY + 1) * ClusterCellSize);
            for (int Y = StartY; Y < EndY; ++Y)
            {
                const int AdditionalIgnored =
                    (Query.AdditionalIgnoredOwnerEntityID >= 0 &&
                     !mGrid.CanOccupy(FromX, Y, Query.Footprint,
                                      Query.IgnoredOwnerEntityID, -1))
                    ? Query.AdditionalIgnoredOwnerEntityID
                    : -1;
                if (mGrid.CanTraverse(FromX, Y, ToX, Y,
                        Query.Footprint, Query.IgnoredOwnerEntityID,
                        AdditionalIgnored))
                {
                    return true;
                }
            }
        }
        else
        {
            const int TopClusterY = std::min(AY, BY);
            const int TopCellY = (TopClusterY + 1) * ClusterCellSize - 1;
            const int BottomCellY = TopCellY + 1;
            const int FromY = (DeltaY > 0) ? TopCellY : BottomCellY;
            const int ToY = (DeltaY > 0) ? BottomCellY : TopCellY;
            const int StartX = AX * ClusterCellSize;
            const int EndX = std::min(
                mGrid.GetWidth(), (AX + 1) * ClusterCellSize);
            for (int X = StartX; X < EndX; ++X)
            {
                const int AdditionalIgnored =
                    (Query.AdditionalIgnoredOwnerEntityID >= 0 &&
                     !mGrid.CanOccupy(X, FromY, Query.Footprint,
                                      Query.IgnoredOwnerEntityID, -1))
                    ? Query.AdditionalIgnoredOwnerEntityID
                    : -1;
                if (mGrid.CanTraverse(X, FromY, X, ToY,
                        Query.Footprint, Query.IgnoredOwnerEntityID,
                        AdditionalIgnored))
                {
                    return true;
                }
            }
        }
        return false;
    }

    std::vector<int> GetClusterCorridor(
        int StartX, int StartY, int TargetX, int TargetY,
        const NavigationPathQuery& Query)
    {
        const int StartComp = mGrid.GetEffectiveTerrainComponent(StartX, StartY);
        const int TargetComp = mGrid.GetEffectiveTerrainComponent(TargetX, TargetY);
        if (StartComp > 0 && TargetComp > 0 && StartComp != TargetComp)
        {
            return {};
        }

        const int StartCluster = ClusterIndexForCell(StartX, StartY);
        const int TargetCluster = ClusterIndexForCell(TargetX, TargetY);
        const CorridorKey Key{
            mGrid.GetVersion(), StartCluster, TargetCluster,
            Query.Footprint.Signature(),
            Query.IgnoredOwnerEntityID,
            Query.AdditionalIgnoredOwnerEntityID
        };
        if (!Query.HasAvoidedObstacle)
        {
            const auto Cached = mCorridorCache.find(Key);
            if (Cached != mCorridorCache.end())
            {
                ++mStats.CorridorCacheHits;
                return Cached->second;
            }
        }
        if (StartCluster == TargetCluster)
        {
            std::vector<int> Result{StartCluster};
            if (!Query.HasAvoidedObstacle)
                mCorridorCache[Key] = Result;
            return Result;
        }

        const int ClusterW = ClusterWidth();
        const int ClusterH = ClusterHeight();
        const int ClusterCount = ClusterW * ClusterH;
        const int Infinity = std::numeric_limits<int>::max();
        mClusterG.assign((size_t)ClusterCount, Infinity);
        mClusterParent.assign((size_t)ClusterCount, -1);
        mClusterClosed.assign((size_t)ClusterCount, 0);
        mClusterOpen.clear();
        mClusterG[StartCluster] = 0;
        mClusterOpen.push_back(StartCluster);

        static constexpr int CDX[4] = { 1, 0, -1, 0 };
        static constexpr int CDY[4] = { 0, 1, 0, -1 };
        while (!mClusterOpen.empty())
        {
            int BestOpen = 0;
            for (int Index = 1; Index < (int)mClusterOpen.size(); ++Index)
            {
                const int Candidate = mClusterOpen[Index];
                const int CurrentBest = mClusterOpen[BestOpen];
                const int CandidateX = Candidate % ClusterW;
                const int CandidateY = Candidate / ClusterW;
                const int BestX = CurrentBest % ClusterW;
                const int BestY = CurrentBest / ClusterW;
                const int TargetClusterX = TargetCluster % ClusterW;
                const int TargetClusterY = TargetCluster / ClusterW;
                const auto CandidateKey = std::make_tuple(
                    mClusterG[Candidate] +
                        10 * (std::abs(CandidateX - TargetClusterX) +
                              std::abs(CandidateY - TargetClusterY)),
                    CandidateY, CandidateX);
                const auto BestKey = std::make_tuple(
                    mClusterG[CurrentBest] +
                        10 * (std::abs(BestX - TargetClusterX) +
                              std::abs(BestY - TargetClusterY)),
                    BestY, BestX);
                if (CandidateKey < BestKey)
                    BestOpen = Index;
            }

            const int Current = mClusterOpen[BestOpen];
            mClusterOpen.erase(mClusterOpen.begin() + BestOpen);
            if (mClusterClosed[Current])
                continue;
            mClusterClosed[Current] = 1;
            if (Current == TargetCluster)
                break;

            const int CurrentX = Current % ClusterW;
            const int CurrentY = Current / ClusterW;
            for (int Direction = 0; Direction < 4; ++Direction)
            {
                const int NextX = CurrentX + CDX[Direction];
                const int NextY = CurrentY + CDY[Direction];
                if (NextX < 0 || NextX >= ClusterW ||
                    NextY < 0 || NextY >= ClusterH)
                {
                    continue;
                }
                const int Next = NextY * ClusterW + NextX;
                if (mClusterClosed[Next] || !ClustersConnect(Current, Next, Query))
                    continue;
                const int NextG = mClusterG[Current] + 10;
                if (NextG >= mClusterG[Next])
                    continue;
                mClusterG[Next] = NextG;
                mClusterParent[Next] = Current;
                mClusterOpen.push_back(Next);
            }
        }

        std::vector<int> Result;
        if (mClusterParent[TargetCluster] >= 0)
        {
            for (int Cluster = TargetCluster; Cluster >= 0;
                 Cluster = mClusterParent[Cluster])
            {
                Result.push_back(Cluster);
                if (Cluster == StartCluster)
                    break;
            }
            std::reverse(Result.begin(), Result.end());
        }
        if (!Query.HasAvoidedObstacle)
            mCorridorCache[Key] = Result;
        return Result;
    }

    void BuildAllowedClusterMask(const std::vector<int>& Corridor,
                                 std::vector<uint8_t>& OutMask) const
    {
        const int ClusterW = ClusterWidth();
        const int ClusterH = ClusterHeight();
        OutMask.assign((size_t)ClusterW * (size_t)ClusterH, 0);
        for (const int Cluster : Corridor)
        {
            if (Cluster < 0 || Cluster >= (int)OutMask.size())
                continue;
            const int CX = Cluster % ClusterW;
            const int CY = Cluster / ClusterW;
            for (int DY = -1; DY <= 1; ++DY)
            {
                for (int DX = -1; DX <= 1; ++DX)
                {
                    const int NX = CX + DX;
                    const int NY = CY + DY;
                    if (NX >= 0 && NX < ClusterW && NY >= 0 && NY < ClusterH)
                        OutMask[(size_t)(NY * ClusterW + NX)] = 1;
                }
            }
        }
    }

    bool IsClusterAllowed(int CellX, int CellY,
                          const std::vector<uint8_t>& Mask) const
    {
        if (Mask.empty())
            return true;
        const int Cluster = ClusterIndexForCell(CellX, CellY);
        return Cluster >= 0 && Cluster < (int)Mask.size() &&
            Mask[(size_t)Cluster] != 0;
    }

    struct AStarSearchState
    {
        int BestIndex = -1;
        int BestH = std::numeric_limits<int>::max();
        int BestG = 0;
        bool ReachedTarget = false;
        bool BudgetReached = false;
        bool PrunedByClusterMask = false;
    };

    struct OpenEntry
    {
        int Index = -1;
        int G = 0;
        int H = 0;
        int X = 0;
        int Y = 0;
    };

    struct OpenEntryGreater
    {
        bool operator()(const OpenEntry& A, const OpenEntry& B) const
        {
            return std::make_tuple(A.G + A.H, A.H, A.Y, A.X) >
                std::make_tuple(B.G + B.H, B.H, B.Y, B.X);
        }
    };

    AStarSearchState ExecuteAStar(
        int StartX, int StartY, int TargetX, int TargetY,
        const NavigationPathQuery& Query,
        const std::vector<uint8_t>& AllowedClusters,
        int NodeBudget,
        bool /*TargetPassable*/,
        NavigationPathResult& Result)
    {
        AStarSearchState State;
        EnsureWorkspace();
        AdvanceGeneration();

        const int Width = mGrid.GetWidth();
        const int StartIndex = StartY * Width + StartX;
        const int TargetIndex = TargetY * Width + TargetX;

        OpenEntryGreater HeapComp;
        mOpenHeap.clear();
        TouchNode(StartIndex);
        mGCost[StartIndex] = 0;
        mParent[StartIndex] = -1;
        const int StartH = Octile(StartX - TargetX, StartY - TargetY);
        mOpenHeap.push_back({StartIndex, 0, StartH, StartX, StartY});

        State.BestIndex = StartIndex;
        State.BestH = StartH;
        State.BestG = 0;

        static constexpr int DX[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
        static constexpr int DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };

        int ExpandedThisCall = 0;
        while (!mOpenHeap.empty())
        {
            if (ExpandedThisCall >= NodeBudget)
            {
                State.BudgetReached = true;
                break;
            }

            std::pop_heap(mOpenHeap.begin(), mOpenHeap.end(), HeapComp);
            const OpenEntry Current = mOpenHeap.back();
            mOpenHeap.pop_back();
            if (!IsTouched(Current.Index) || mClosed[Current.Index] ||
                mGCost[Current.Index] != Current.G)
            {
                continue;
            }

            mClosed[Current.Index] = 1;
            ++ExpandedThisCall;
            ++Result.ExpandedCells;
            const int CurrentX = Current.X;
            const int CurrentY = Current.Y;

            if (Current.H < State.BestH ||
                (Current.H == State.BestH &&
                 std::make_tuple(Current.G, CurrentY, CurrentX) <
                 std::make_tuple(State.BestG, State.BestIndex / Width, State.BestIndex % Width)))
            {
                State.BestIndex = Current.Index;
                State.BestH = Current.H;
                State.BestG = Current.G;
            }

            if (Current.Index == TargetIndex)
            {
                State.ReachedTarget = true;
                State.BestIndex = Current.Index;
                break;
            }

            const bool CurrentlyOverlapsIgnored =
                (Query.AdditionalIgnoredOwnerEntityID >= 0) &&
                !mGrid.CanOccupy(CurrentX, CurrentY, Query.Footprint, Query.IgnoredOwnerEntityID, -1);
            const int AdditionalIgnored = CurrentlyOverlapsIgnored
                ? Query.AdditionalIgnoredOwnerEntityID
                : -1;

            bool CardinalPassable[4] = {false, false, false, false};
            for (int Direction = 0; Direction < 8; ++Direction)
            {
                const int NextX = CurrentX + DX[Direction];
                const int NextY = CurrentY + DY[Direction];
                if (!mGrid.IsInBounds(NextX, NextY))
                    continue;

                if (Direction < 4)
                {
                    if (!mGrid.CanTraverse(CurrentX, CurrentY, NextX, NextY,
                            Query.Footprint, Query.IgnoredOwnerEntityID,
                            AdditionalIgnored))
                    {
                        continue;
                    }
                    CardinalPassable[Direction] = true;
                    if (!IsClusterAllowed(NextX, NextY, AllowedClusters))
                    {
                        State.PrunedByClusterMask = true;
                        continue;
                    }
                }
                else
                {
                    const bool CardOk =
                        (Direction == 4 && CardinalPassable[0] && CardinalPassable[1]) ||
                        (Direction == 5 && CardinalPassable[2] && CardinalPassable[1]) ||
                        (Direction == 6 && CardinalPassable[2] && CardinalPassable[3]) ||
                        (Direction == 7 && CardinalPassable[0] && CardinalPassable[3]);
                    if (!CardOk)
                        continue;
                    if (!IsClusterAllowed(NextX, NextY, AllowedClusters))
                    {
                        State.PrunedByClusterMask = true;
                        continue;
                    }
                }

                const int NextIndex = NextY * Width + NextX;
                TouchNode(NextIndex);
                if (mClosed[NextIndex])
                    continue;
                const int NextG = Current.G + (Direction < 4 ? 10 : 14);
                if (NextG >= mGCost[NextIndex])
                    continue;

                if (Direction >= 4 &&
                    !mGrid.CanTraverse(CurrentX, CurrentY, NextX, NextY,
                        Query.Footprint, Query.IgnoredOwnerEntityID,
                        AdditionalIgnored))
                {
                    continue;
                }

                mGCost[NextIndex] = NextG;
                mParent[NextIndex] = Current.Index;
                const int H = Octile(NextX - TargetX, NextY - TargetY);
                mOpenHeap.push_back({NextIndex, NextG, H, NextX, NextY});
                std::push_heap(mOpenHeap.begin(), mOpenHeap.end(), HeapComp);
            }
        }

        return State;
    }

    bool HasLineOfSight(int StartX, int StartY, int EndX, int EndY,
                        const NavigationPathQuery& Query) const
    {
        int X = StartX;
        int Y = StartY;
        const int DeltaX = std::abs(EndX - StartX);
        const int DeltaY = std::abs(EndY - StartY);
        const int StepX = StartX < EndX ? 1 : -1;
        const int StepY = StartY < EndY ? 1 : -1;
        int Error = DeltaX - DeltaY;
        bool StillOverlapsAdditionalIgnored =
            (Query.AdditionalIgnoredOwnerEntityID >= 0) &&
            !mGrid.CanOccupy(StartX, StartY, Query.Footprint, Query.IgnoredOwnerEntityID, -1);

        while (X != EndX || Y != EndY)
        {
            const int PreviousX = X;
            const int PreviousY = Y;
            const int DoubleError = Error * 2;
            if (DoubleError > -DeltaY)
            {
                Error -= DeltaY;
                X += StepX;
            }
            if (DoubleError < DeltaX)
            {
                Error += DeltaX;
                Y += StepY;
            }

            if (StillOverlapsAdditionalIgnored &&
                mGrid.CanOccupy(PreviousX, PreviousY, Query.Footprint, Query.IgnoredOwnerEntityID, -1))
            {
                StillOverlapsAdditionalIgnored = false;
            }
            if (!mGrid.CanTraverse(PreviousX, PreviousY, X, Y,
                    Query.Footprint, Query.IgnoredOwnerEntityID,
                    StillOverlapsAdditionalIgnored ? Query.AdditionalIgnoredOwnerEntityID : -1))
            {
                return false;
            }
            if (X != PreviousX && Y != PreviousY &&
                (!mGrid.CanTraverse(PreviousX, PreviousY, X, PreviousY,
                    Query.Footprint, Query.IgnoredOwnerEntityID,
                    StillOverlapsAdditionalIgnored ? Query.AdditionalIgnoredOwnerEntityID : -1) ||
                 !mGrid.CanTraverse(PreviousX, PreviousY, PreviousX, Y,
                    Query.Footprint, Query.IgnoredOwnerEntityID,
                    StillOverlapsAdditionalIgnored ? Query.AdditionalIgnoredOwnerEntityID : -1)))
            {
                return false;
            }
        }
        return true;
    }

    std::vector<std::pair<int, int>> SmoothCells(
        const std::vector<std::pair<int, int>>& Cells,
        const NavigationPathQuery& Query) const
    {
        if (Cells.size() <= 2)
            return Cells;

        std::vector<std::pair<int, int>> Result;
        Result.push_back(Cells.front());
        size_t Anchor = 0;
        constexpr size_t MaximumSmoothingLookahead = 64;
        while (Anchor + 1 < Cells.size())
        {
            // Reverse-testing every remaining cell makes string pulling
            // cubic on long, winding real-map routes.  A fixed 64-minitile
            // lookahead still removes local zig-zags and long straight runs
            // while bounding supercover LOS work per emitted segment.
            size_t Furthest = std::min(
                Cells.size() - 1,
                Anchor + MaximumSmoothingLookahead);
            while (Furthest > Anchor + 1 &&
                   !HasLineOfSight(
                       Cells[Anchor].first, Cells[Anchor].second,
                       Cells[Furthest].first, Cells[Furthest].second,
                       Query))
            {
                --Furthest;
            }
            Result.push_back(Cells[Furthest]);
            Anchor = Furthest;
        }
        return Result;
    }

    static int Octile(int DeltaX, int DeltaY)
    {
        DeltaX = std::abs(DeltaX);
        DeltaY = std::abs(DeltaY);
        const int Diagonal = std::min(DeltaX, DeltaY);
        return Diagonal * 14 + (std::max(DeltaX, DeltaY) - Diagonal) * 10;
    }

    void ResetWorkspace()
    {
        mGeneration = 1;
        mStamp.clear();
        mGCost.clear();
        mParent.clear();
        mClosed.clear();
        mOpenHeap.clear();
        mAllowedClusters.clear();
    }

    void EnsureWorkspace()
    {
        const size_t Count =
            (size_t)mGrid.GetWidth() * (size_t)mGrid.GetHeight();
        if (mStamp.size() == Count)
            return;
        mStamp.assign(Count, 0);
        mGCost.resize(Count);
        mParent.resize(Count);
        mClosed.resize(Count);
        mOpenHeap.reserve(4096);
        mGeneration = 1;
    }

    void AdvanceGeneration()
    {
        ++mGeneration;
        if (mGeneration == 0)
        {
            std::fill(mStamp.begin(), mStamp.end(), 0);
            mGeneration = 1;
        }
    }

    bool IsTouched(int Index) const
    {
        return mStamp[(size_t)Index] == mGeneration;
    }

    void TouchNode(int Index)
    {
        if (IsTouched(Index))
            return;
        mStamp[(size_t)Index] = mGeneration;
        mGCost[(size_t)Index] = std::numeric_limits<int>::max();
        mParent[(size_t)Index] = -1;
        mClosed[(size_t)Index] = 0;
    }

    void FinishStats(
        const NavigationPathResult& Result,
        const std::chrono::steady_clock::time_point& BeginTime)
    {
        mStats.ExpandedCells += (uint64_t)Result.ExpandedCells;
        if (Result.Status == PathStatus::Partial)
            ++mStats.PartialPaths;
        else if (Result.Status == PathStatus::Unreachable)
            ++mStats.FailedPaths;
        const auto Elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - BeginTime);
        mStats.QueryMicroseconds += (uint64_t)std::max<int64_t>(
            0, Elapsed.count());
    }

    NavigationGrid mGrid;
    Engine::Navigation::NavigationServer2D* mBoundEngineNav = nullptr;
    uint32_t mBaseGridVersion = 0;
    NavigationPerformanceStats mStats;
    std::map<CorridorKey, std::vector<int>> mCorridorCache;
    uint32_t mGeneration = 1;
    std::vector<uint32_t> mStamp;
    std::vector<int> mGCost;
    std::vector<int> mParent;
    std::vector<uint8_t> mClosed;
    std::vector<OpenEntry> mOpenHeap;
    std::vector<uint8_t> mAllowedClusters;
    std::vector<int> mClusterG;
    std::vector<int> mClusterParent;
    std::vector<uint8_t> mClusterClosed;
    std::vector<int> mClusterOpen;
};

} // namespace Game::Pathfinding
