#include "Game/Pathfinding/NavigationService.h"

#include <cstdio>
#include <cstdlib>

namespace
{
void Require(bool Condition, const char* Message)
{
    if (Condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message);
    std::exit(1);
}

Core::Math::Vec2 CellWorld(int X, int Y)
{
    return {
        (float)(X * Game::Pathfinding::NavigationCellSize + 4),
        (float)(Y * Game::Pathfinding::NavigationCellSize + 4)
    };
}
}

int main()
{
    using namespace Game::Pathfinding;

    NavigationGrid Grid;
    Grid.Initialize(8, 8);
    for (int Y = 0; Y < Grid.GetHeight(); ++Y)
    {
        if (Y == 15)
            continue;
        Grid.SetBaseCell(12, Y, false, 0);
    }
    Grid.CommitBaseChanges();

    NavigationService Service;
    Service.SetBaseGrid(Grid);
    NavigationPathQuery ExactQuery;
    ExactQuery.Start = CellWorld(4, 4);
    ExactQuery.Target = CellWorld(25, 25);
    const auto Exact = Service.QueryPath(ExactQuery);
    Require(Exact.Status == PathStatus::Exact &&
            Exact.EndReason == PathEndReason::None &&
            Exact.ResolvedDestination.DistanceTo(ExactQuery.Target) < 0.1f,
        "portal corridor returns an exact route through an 8-pixel gap");
    Require(Exact.ExpandedCells > 0 && Exact.ExpandedCells <= 8192,
        "exact route reports its real bounded expansion count");

    NavigationPathQuery BudgetQuery = ExactQuery;
    BudgetQuery.MaximumExpandedCells = 2;
    const auto Budget = Service.QueryPath(BudgetQuery);
    Require(Budget.Status == PathStatus::Partial &&
            Budget.EndReason == PathEndReason::NodeBudget &&
            Budget.ResolvedDestination.DistanceTo(BudgetQuery.Start) > 1.0f,
        "node budget returns a partial route with actual forward progress");

    NavigationObstacle GoalObstacle;
    GoalObstacle.OwnerEntityID = 99;
    GoalObstacle.Position = ExactQuery.Target;
    GoalObstacle.Left = 3;
    GoalObstacle.Up = 3;
    GoalObstacle.Right = 3;
    GoalObstacle.Down = 3;
    const uint32_t BeforeObstacleVersion = Service.GetVersion();
    Service.SyncStaticObstacles({GoalObstacle});
    Require(Service.GetVersion() > BeforeObstacleVersion,
        "static occupancy changes advance the navigation version");

    const auto BlockedGoal = Service.QueryPath(ExactQuery);
    Require(BlockedGoal.Status == PathStatus::Partial &&
            BlockedGoal.EndReason == PathEndReason::GoalBlocked,
        "blocked goal returns a useful partial route and reason");

    NavigationPathQuery InteractionQuery = ExactQuery;
    InteractionQuery.IgnoredOwnerEntityID = 99;
    const auto IgnoredGoal = Service.QueryPath(InteractionQuery);
    Require(IgnoredGoal.Status == PathStatus::Exact,
        "interaction target owner can be ignored without ignoring terrain");

    NavigationGrid NarrowGrid;
    NarrowGrid.Initialize(4, 4);
    for (int Y = 0; Y < NarrowGrid.GetHeight(); ++Y)
    {
        for (int X = 0; X < NarrowGrid.GetWidth(); ++X)
            NarrowGrid.SetBaseCell(X, Y, Y == 8, 0);
    }
    NarrowGrid.CommitBaseChanges();
    NavigationService NarrowService;
    NarrowService.SetBaseGrid(NarrowGrid);
    NavigationPathQuery SmallQuery;
    SmallQuery.Start = CellWorld(1, 8);
    SmallQuery.Target = CellWorld(14, 8);
    Require(NarrowService.QueryPath(SmallQuery).Status == PathStatus::Exact,
        "point footprint traverses a single-minitile corridor");
    SmallQuery.Footprint = {8, 8, 8, 8};
    Require(NarrowService.QueryPath(SmallQuery).Status ==
            PathStatus::Unreachable,
        "units.dat footprint rejects a corridor narrower than the unit");

    NavigationGrid DisconnectedGrid;
    DisconnectedGrid.Initialize(2, 2);
    for (int Y = 0; Y < DisconnectedGrid.GetHeight(); ++Y)
    {
        for (int X = 0; X < DisconnectedGrid.GetWidth(); ++X)
            DisconnectedGrid.SetBaseCell(X, Y, false, 0);
    }
    DisconnectedGrid.SetBaseCell(1, 1, true, 0);
    DisconnectedGrid.SetBaseCell(6, 6, true, 0);
    DisconnectedGrid.CommitBaseChanges();
    NavigationService DisconnectedService;
    DisconnectedService.SetBaseGrid(DisconnectedGrid);
    NavigationPathQuery DisconnectedQuery;
    DisconnectedQuery.Start = CellWorld(1, 1);
    DisconnectedQuery.Target = CellWorld(6, 6);
    const auto Disconnected =
        DisconnectedService.QueryPath(DisconnectedQuery);
    Require(Disconnected.Status == PathStatus::Unreachable &&
            Disconnected.EndReason == PathEndReason::Disconnected,
        "a unit that cannot leave its start is unreachable");

    // Test: unit leaving a building/depot
    NavigationGrid DepotGrid;
    DepotGrid.Initialize(64, 64);
    NavigationObstacle NexusObs;
    NexusObs.OwnerEntityID = 25;
    NexusObs.Position = {336.0f, 240.0f};
    NexusObs.Left = 63;
    NexusObs.Right = 64;
    NexusObs.Up = 47;
    NexusObs.Down = 48;
    NavigationService DepotService;
    DepotService.SetBaseGrid(DepotGrid);
    DepotService.SyncStaticObstacles({NexusObs});

    NavigationPathQuery EscapeQuery;
    EscapeQuery.Start = {280.0f, 205.0f};
    EscapeQuery.Target = {486.0f, 120.0f};
    EscapeQuery.IgnoredOwnerEntityID = 1;
    EscapeQuery.AdditionalIgnoredOwnerEntityID = -1;
    EscapeQuery.Footprint = {15, 15, 16, 16};
    const auto EscapeResult = DepotService.QueryPath(EscapeQuery);
    std::printf("EscapeResult: status=%d reason=%d expanded=%d waypoints=%zu\n",
        (int)EscapeResult.Status, (int)EscapeResult.EndReason,
        EscapeResult.ExpandedCells, EscapeResult.Waypoints.size());

    // Test: Canyon cliff with distant ramp detour (reproducing user scenario)
    NavigationGrid CliffGrid;
    CliffGrid.Initialize(16, 16); // 64x64 cells
    for (int Y = 0; Y < 64; ++Y)
    {
        for (int X = 0; X < 64; ++X)
        {
            uint8_t H = 2; // High ground default
            if (Y >= 20 && Y <= 40)
            {
                if (X >= 46 && X <= 50)
                {
                    // Ramp with transition heights
                    H = (Y <= 22 || Y >= 38) ? 1 : 0;
                }
                else
                {
                    H = 0; // Low ground canyon
                }
            }
            CliffGrid.SetBaseCell(X, Y, true, H);
        }
    }
    CliffGrid.CommitBaseChanges();
    NavigationService CliffService;
    CliffService.SetBaseGrid(CliffGrid);

    NavigationPathQuery CliffQuery;
    CliffQuery.Start = CellWorld(10, 10);  // High ground (Height 2)
    CliffQuery.Target = CellWorld(10, 30); // Canyon floor (Height 0), Euclidean distance = 20 cells
    CliffQuery.MaximumExpandedCells = 8192;

    const auto CliffResult = CliffService.QueryPath(CliffQuery);
    Require(CliffResult.Status == PathStatus::Exact,
        "path across cliff must successfully detour through distant ramp");
    Require(CliffResult.ResolvedDestination.DistanceTo(CliffQuery.Target) < 0.1f,
        "path reaches genuine destination across canyon");

    bool DetouredToRamp = false;
    for (const auto& Pt : CliffResult.Waypoints)
    {
        int WX = 0, WY = 0;
        CliffGrid.WorldToCell(Pt, WX, WY);
        if (WX >= 45)
            DetouredToRamp = true;
    }
    Require(DetouredToRamp, "waypoints must detour via ramp at X >= 45");

    std::printf("NavigationPathfindingTest passed\n");
    return 0;
}
