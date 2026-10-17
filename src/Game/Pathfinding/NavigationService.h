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
};

struct NavigationPathResult
{
    std::vector<Core::Math::Vec2> Waypoints;
    PathStatus Status = PathStatus::Unreachable;
    PathEndReason EndReason = PathEndReason::Disconnected;
    Core::Math::Vec2 ResolvedDestination;
    int ExpandedCells = 0;
    uint32_t NavigationVersion = 0;

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
    void ResetStats() { mStats.Reset(); }

    NavigationPathResult QueryPath(NavigationPathQuery Query)
    {
        const auto BeginTime = std::chrono::steady_clock::now();
        ++mStats.QueryCount;

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

        const bool OriginalTargetPassable = mGrid.CanOccupy(
            TargetX, TargetY, Query.Footprint, Query.IgnoredOwnerEntityID,
            Query.AdditionalIgnoredOwnerEntityID);

        int EffectiveTargetX = TargetX;
        int EffectiveTargetY = TargetY;
        bool EffectiveTargetPassable = OriginalTargetPassable;

        if (!OriginalTargetPassable)
        {
            int ProjectedX = -1;
            int ProjectedY = -1;
            if (FindNearestPassableCell(
                    TargetX, TargetY, StartX, StartY, Query, 64,
                    ProjectedX, ProjectedY))
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

        if (StartX == EffectiveTargetX && StartY == EffectiveTargetY && !OriginalTargetPassable)
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
        if (OriginalTargetPassable &&
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
            Result.Status = OriginalTargetPassable ? PathStatus::Exact : PathStatus::Partial;
            Result.EndReason = OriginalTargetPassable ? PathEndReason::None : PathEndReason::GoalBlocked;
            Result.ResolvedDestination = mGrid.CellToWorld(EffectiveTargetX, EffectiveTargetY);
            Result.Waypoints.push_back(Result.ResolvedDestination);
            FinishStats(Result, BeginTime);
            return Result;
        }

        const int Width = mGrid.GetWidth();
        const int StartIndex = StartY * Width + StartX;
        const int NodeBudget = std::max(1, Query.MaximumExpandedCells);

        std::vector<uint8_t> AllowedClusters;
        const std::vector<int> Corridor = GetClusterCorridor(
            StartX, StartY, EffectiveTargetX, EffectiveTargetY, Query);
        if (!Corridor.empty())
        {
            BuildAllowedClusterMask(Corridor, AllowedClusters);
        }

        AStarSearchState State;
        if (!AllowedClusters.empty())
        {
            State = ExecuteAStar(
                StartX, StartY, EffectiveTargetX, EffectiveTargetY,
                Query, AllowedClusters, NodeBudget,
                EffectiveTargetPassable, Result);
        }

        if (!State.ReachedTarget && !State.BudgetReached && EffectiveTargetPassable)
        {
            // Coarse cluster corridor had an internal impassable barrier (e.g. cliff /
            // ramp disconnect). Fall back to unconstrained search across
            // the full navigation grid to guarantee finding the genuine passable route.
            AllowedClusters.clear();
            const auto FallbackState = ExecuteAStar(
                StartX, StartY, EffectiveTargetX, EffectiveTargetY,
                Query, AllowedClusters, NodeBudget,
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
                (OriginalTargetPassable ? PathEndReason::Disconnected :
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
            ? (OriginalTargetPassable ? PathStatus::Exact : PathStatus::Partial)
            : (State.BudgetReached ? PathStatus::Partial : PathStatus::Unreachable);
        Result.EndReason = State.ReachedTarget
            ? (OriginalTargetPassable ? PathEndReason::None : PathEndReason::GoalBlocked)
            : (State.BudgetReached ? PathEndReason::NodeBudget :
                (OriginalTargetPassable ? PathEndReason::Disconnected :
                    PathEndReason::GoalBlocked));
        Result.ResolvedDestination = Result.Waypoints.back();
        if (OriginalTargetPassable && State.ReachedTarget &&
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

        bool operator<(const CorridorKey& Other) const
        {
            return std::tie(Version, StartCluster, TargetCluster, FootprintSignature) <
                std::tie(Other.Version, Other.StartCluster, Other.TargetCluster, Other.FootprintSignature);
        }
    };

    bool FindNearestPassableCell(
        int CenterX, int CenterY, int StartX, int StartY,
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
        int BestX = -1;
        int BestY = -1;
        int BestHeightDiff = std::numeric_limits<int>::max();
        int BestDistSq = std::numeric_limits<int>::max();
        int BestStartDistSq = std::numeric_limits<int>::max();

        const int EffectiveMaxRadius = std::max(MaxRadius, 64);
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
                        const int CellHeight = mGrid.GetHeightLevel(X, Y);
                        const int HeightDiff = (TargetHeight >= 0 && CellHeight >= 0)
                            ? std::abs(CellHeight - TargetHeight) : 0;
                        const int DistSq = DX * DX + DY * DY;
                        const int StartDistSq = (X - StartX) * (X - StartX) + (Y - StartY) * (Y - StartY);

                        const auto CandidateKey = std::make_tuple(HeightDiff, DistSq, StartDistSq);
                        const auto BestKey = std::make_tuple(BestHeightDiff, BestDistSq, BestStartDistSq);
                        if (CandidateKey < BestKey)
                        {
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
            if (FoundAtRadius && BestHeightDiff == 0)
            {
                OutX = BestX;
                OutY = BestY;
                return true;
            }
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
            const int StartY = AY * ClusterCellSize;
            const int EndY = std::min(
                mGrid.GetHeight(), (AY + 1) * ClusterCellSize);
            for (int Y = StartY; Y < EndY; ++Y)
            {
                if (mGrid.CanTraverse(LeftCellX, Y, RightCellX, Y,
                        Query.Footprint, -1, -1) &&
                    mGrid.CanTraverse(RightCellX, Y, LeftCellX, Y,
                        Query.Footprint, -1, -1))
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
            const int StartX = AX * ClusterCellSize;
            const int EndX = std::min(
                mGrid.GetWidth(), (AX + 1) * ClusterCellSize);
            for (int X = StartX; X < EndX; ++X)
            {
                if (mGrid.CanTraverse(X, TopCellY, X, BottomCellY,
                        Query.Footprint, -1, -1) &&
                    mGrid.CanTraverse(X, BottomCellY, X, TopCellY,
                        Query.Footprint, -1, -1))
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
        const int StartCluster = ClusterIndexForCell(StartX, StartY);
        const int TargetCluster = ClusterIndexForCell(TargetX, TargetY);
        const CorridorKey Key{
            mGrid.GetVersion(), StartCluster, TargetCluster,
            Query.Footprint.Signature()
        };
        const auto Cached = mCorridorCache.find(Key);
        if (Cached != mCorridorCache.end())
        {
            ++mStats.CorridorCacheHits;
            return Cached->second;
        }
        if (StartCluster == TargetCluster)
        {
            std::vector<int> Result{StartCluster};
            mCorridorCache[Key] = Result;
            return Result;
        }

        const int ClusterW = ClusterWidth();
        const int ClusterH = ClusterHeight();
        const int ClusterCount = ClusterW * ClusterH;
        const int Infinity = std::numeric_limits<int>::max();
        std::vector<int> G((size_t)ClusterCount, Infinity);
        std::vector<int> Parent((size_t)ClusterCount, -1);
        std::vector<uint8_t> Closed((size_t)ClusterCount, 0);
        std::vector<int> Open;
        G[StartCluster] = 0;
        Open.push_back(StartCluster);

        static constexpr int CDX[4] = { 1, 0, -1, 0 };
        static constexpr int CDY[4] = { 0, 1, 0, -1 };
        while (!Open.empty())
        {
            int BestOpen = 0;
            for (int Index = 1; Index < (int)Open.size(); ++Index)
            {
                const int Candidate = Open[Index];
                const int CurrentBest = Open[BestOpen];
                const int CandidateX = Candidate % ClusterW;
                const int CandidateY = Candidate / ClusterW;
                const int BestX = CurrentBest % ClusterW;
                const int BestY = CurrentBest / ClusterW;
                const int TargetClusterX = TargetCluster % ClusterW;
                const int TargetClusterY = TargetCluster / ClusterW;
                const auto CandidateKey = std::make_tuple(
                    G[Candidate] +
                        10 * (std::abs(CandidateX - TargetClusterX) +
                              std::abs(CandidateY - TargetClusterY)),
                    CandidateY, CandidateX);
                const auto BestKey = std::make_tuple(
                    G[CurrentBest] +
                        10 * (std::abs(BestX - TargetClusterX) +
                              std::abs(BestY - TargetClusterY)),
                    BestY, BestX);
                if (CandidateKey < BestKey)
                    BestOpen = Index;
            }

            const int Current = Open[BestOpen];
            Open.erase(Open.begin() + BestOpen);
            if (Closed[Current])
                continue;
            Closed[Current] = 1;
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
                if (Closed[Next] || !ClustersConnect(Current, Next, Query))
                    continue;
                const int NextG = G[Current] + 10;
                if (NextG >= G[Next])
                    continue;
                G[Next] = NextG;
                Parent[Next] = Current;
                Open.push_back(Next);
            }
        }

        std::vector<int> Result;
        if (Parent[TargetCluster] >= 0)
        {
            for (int Cluster = TargetCluster; Cluster >= 0;
                 Cluster = Parent[Cluster])
            {
                Result.push_back(Cluster);
                if (Cluster == StartCluster)
                    break;
            }
            std::reverse(Result.begin(), Result.end());
        }
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
    };

    AStarSearchState ExecuteAStar(
        int StartX, int StartY, int TargetX, int TargetY,
        const NavigationPathQuery& Query,
        const std::vector<uint8_t>& AllowedClusters,
        int NodeBudget,
        bool TargetPassable,
        NavigationPathResult& Result)
    {
        AStarSearchState State;
        EnsureWorkspace();
        AdvanceGeneration();

        const int Width = mGrid.GetWidth();
        const int StartIndex = StartY * Width + StartX;
        const int TargetIndex = TargetY * Width + TargetX;

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

        std::priority_queue<OpenEntry,
            std::vector<OpenEntry>, OpenEntryGreater> Open;
        TouchNode(StartIndex);
        mGCost[StartIndex] = 0;
        mParent[StartIndex] = -1;
        const int StartH = Octile(StartX - TargetX, StartY - TargetY);
        Open.push({StartIndex, 0, StartH, StartX, StartY});

        State.BestIndex = StartIndex;
        State.BestH = StartH;
        State.BestG = 0;

        static constexpr int DX[8] = { 1, 0, -1, 0, 1, -1, -1, 1 };
        static constexpr int DY[8] = { 0, 1, 0, -1, 1, 1, -1, -1 };

        int ExpandedThisCall = 0;
        while (!Open.empty())
        {
            if (ExpandedThisCall >= NodeBudget)
            {
                State.BudgetReached = true;
                break;
            }

            const OpenEntry Current = Open.top();
            Open.pop();
            if (!IsTouched(Current.Index) || mClosed[Current.Index] ||
                mGCost[Current.Index] != Current.G)
            {
                continue;
            }

            mClosed[Current.Index] = 1;
            ++ExpandedThisCall;
            ++Result.ExpandedCells;
            const int CurrentX = Current.Index % Width;
            const int CurrentY = Current.Index / Width;

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

            for (int Direction = 0; Direction < 8; ++Direction)
            {
                const int NextX = CurrentX + DX[Direction];
                const int NextY = CurrentY + DY[Direction];
                if (!mGrid.IsInBounds(NextX, NextY) ||
                    !IsClusterAllowed(NextX, NextY, AllowedClusters))
                {
                    continue;
                }
                if (!mGrid.CanTraverse(CurrentX, CurrentY, NextX, NextY,
                        Query.Footprint, Query.IgnoredOwnerEntityID,
                        Query.AdditionalIgnoredOwnerEntityID))
                {
                    continue;
                }
                if (Direction >= 4 &&
                    (!mGrid.CanTraverse(CurrentX, CurrentY,
                        CurrentX + DX[Direction], CurrentY,
                        Query.Footprint, Query.IgnoredOwnerEntityID,
                        Query.AdditionalIgnoredOwnerEntityID) ||
                     !mGrid.CanTraverse(CurrentX, CurrentY,
                        CurrentX, CurrentY + DY[Direction],
                        Query.Footprint, Query.IgnoredOwnerEntityID,
                        Query.AdditionalIgnoredOwnerEntityID)))
                {
                    continue;
                }

                const int NextIndex = NextY * Width + NextX;
                TouchNode(NextIndex);
                if (mClosed[NextIndex])
                    continue;
                const int NextG = Current.G + (Direction < 4 ? 10 : 14);
                if (NextG >= mGCost[NextIndex])
                    continue;

                mGCost[NextIndex] = NextG;
                mParent[NextIndex] = Current.Index;
                const int H = Octile(NextX - TargetX, NextY - TargetY);
                Open.push({NextIndex, NextG, H, NextX, NextY});
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

            if (!mGrid.CanTraverse(PreviousX, PreviousY, X, Y,
                    Query.Footprint, Query.IgnoredOwnerEntityID,
                        Query.AdditionalIgnoredOwnerEntityID))
            {
                return false;
            }
            if (X != PreviousX && Y != PreviousY &&
                (!mGrid.CanTraverse(PreviousX, PreviousY, X, PreviousY,
                    Query.Footprint, Query.IgnoredOwnerEntityID,
                        Query.AdditionalIgnoredOwnerEntityID) ||
                 !mGrid.CanTraverse(PreviousX, PreviousY, PreviousX, Y,
                    Query.Footprint, Query.IgnoredOwnerEntityID,
                        Query.AdditionalIgnoredOwnerEntityID)))
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
    uint32_t mBaseGridVersion = 0;
    NavigationPerformanceStats mStats;
    std::map<CorridorKey, std::vector<int>> mCorridorCache;
    uint32_t mGeneration = 1;
    std::vector<uint32_t> mStamp;
    std::vector<int> mGCost;
    std::vector<int> mParent;
    std::vector<uint8_t> mClosed;
};

} // namespace Game::Pathfinding
