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

    // Test: Large base interior (>400 cells) sealed by player building walls
    // with a 1-minitile gap that passes 1x1 units (Clearance=0) but blocks
    // 2x2 / large units (Clearance >= 1).
    NavigationGrid WalledBaseGrid;
    WalledBaseGrid.Initialize(32, 32); // 128x128 cells
    std::vector<NavigationObstacle> WallObstacles;
    // Top wall: X in [40, 80], Y in [40, 41], leaving 1-cell gap at X = 60, Y in [40, 41]
    {
        NavigationObstacle TopLeftWall;
        TopLeftWall.OwnerEntityID = 201;
        TopLeftWall.Position = CellWorld(49, 40);
        TopLeftWall.Left = 9 * NavigationCellSize + 3;
        TopLeftWall.Right = 10 * NavigationCellSize + 3;
        TopLeftWall.Up = 3;
        TopLeftWall.Down = NavigationCellSize + 3;
        WallObstacles.push_back(TopLeftWall);

        NavigationObstacle TopRightWall;
        TopRightWall.OwnerEntityID = 202;
        TopRightWall.Position = CellWorld(70, 40);
        TopRightWall.Left = 9 * NavigationCellSize + 3;
        TopRightWall.Right = 10 * NavigationCellSize + 3;
        TopRightWall.Up = 3;
        TopRightWall.Down = NavigationCellSize + 3;
        WallObstacles.push_back(TopRightWall);

        NavigationObstacle BottomWall;
        BottomWall.OwnerEntityID = 203;
        BottomWall.Position = CellWorld(60, 80);
        BottomWall.Left = 20 * NavigationCellSize + 3;
        BottomWall.Right = 20 * NavigationCellSize + 3;
        BottomWall.Up = 3;
        BottomWall.Down = NavigationCellSize + 3;
        WallObstacles.push_back(BottomWall);

        NavigationObstacle LeftWall;
        LeftWall.OwnerEntityID = 204;
        LeftWall.Position = CellWorld(40, 60);
        LeftWall.Left = 3;
        LeftWall.Right = NavigationCellSize + 3;
        LeftWall.Up = 20 * NavigationCellSize + 3;
        LeftWall.Down = 20 * NavigationCellSize + 3;
        WallObstacles.push_back(LeftWall);

        NavigationObstacle RightWall;
        RightWall.OwnerEntityID = 205;
        RightWall.Position = CellWorld(79, 60);
        RightWall.Left = 3;
        RightWall.Right = NavigationCellSize + 3;
        RightWall.Up = 20 * NavigationCellSize + 3;
        RightWall.Down = 20 * NavigationCellSize + 3;
        WallObstacles.push_back(RightWall);
    }

    NavigationService WalledService;
    WalledService.SetBaseGrid(WalledBaseGrid);
    WalledService.SyncStaticObstacles(WallObstacles);

    NavigationPathQuery SmallWallQuery;
    SmallWallQuery.Start = CellWorld(60, 20);
    SmallWallQuery.Target = CellWorld(60, 60); // Open cell inside >1200-cell walled courtyard
    SmallWallQuery.Footprint = {0, 0, 0, 0};
    const auto SmallWallResult = WalledService.QueryPath(SmallWallQuery);
    Require(SmallWallResult.Status == PathStatus::Exact &&
            SmallWallResult.ResolvedDestination.DistanceTo(SmallWallQuery.Target) < 0.1f,
        "1x1 unit (Clearance=0) paths through 1-minitile gap into walled base");

    NavigationPathQuery LargeWallQuery = SmallWallQuery;
    LargeWallQuery.Footprint = {15, 15, 16, 16}; // Dragoon / Siege Tank / Ultralisk
    const auto LargeWallResult = WalledService.QueryPath(LargeWallQuery);
    Require(LargeWallResult.Status == PathStatus::Partial &&
            LargeWallResult.EndReason == PathEndReason::GoalBlocked,
        "large unit blocked by 1-minitile wall gap receives Partial path outside wall");
    Require(LargeWallResult.ExpandedCells <= 256,
        "per-clearance component projection avoids multi-thousand-cell A* expansion on walled base");
    Require(LargeWallResult.ResolvedDestination.Y < CellWorld(60, 40).Y,
        "projected partial destination stops outside the top wall");

    NavigationPathQuery AtWallQuery = LargeWallQuery;
    AtWallQuery.Start = LargeWallResult.ResolvedDestination;
    const auto AtWallResult = WalledService.QueryPath(AtWallQuery);
    Require(AtWallResult.Status == PathStatus::Partial &&
            AtWallResult.ExpandedCells == 0,
        "large unit already at projected wall boundary resolves Partial in 0 expanded cells");

    // Test: SCV returning cargo to a delivery point 24px inside an ignored
    // Command Center must resolve its reachable clearance component to the
    // outer map component and produce an Exact path into the delivery zone,
    // rather than projecting the destination outside the Command Center.
    NavigationPathQuery DepotDeliveryQuery;
    DepotDeliveryQuery.Start = {180.0f, 240.0f};
    DepotDeliveryQuery.Target = {296.0f, 240.0f}; // 24px inside NexusObs (left edge = 273)
    DepotDeliveryQuery.IgnoredOwnerEntityID = 25;
    DepotDeliveryQuery.Footprint = {11, 11, 11, 11}; // SCV footprint
    const auto DepotDeliveryResult = DepotService.QueryPath(DepotDeliveryQuery);
    Require(DepotDeliveryResult.Status == PathStatus::Exact &&
            DepotDeliveryResult.EndReason == PathEndReason::None &&
            DepotDeliveryResult.ResolvedDestination.DistanceTo(DepotDeliveryQuery.Target) < 0.1f,
        "SCV return-to-depot query reaches delivery target inside ignored Command Center");

    // Test: Command Center bordering a sealed pocket between adjacent buildings
    // on its East side while open to the mineral line on its West side, and
    // vice versa for deep interior delivery points (>12 cells from the open side).
    {
        NavigationGrid PocketGrid;
        PocketGrid.Initialize(32, 32);
        std::vector<NavigationObstacle> PocketObstacles;

        NavigationObstacle CcObs;
        CcObs.OwnerEntityID = 300;
        CcObs.Position = {384.0f, 384.0f};
        CcObs.Left = 64;
        CcObs.Up = 48;
        CcObs.Right = 63;
        CcObs.Down = 47;
        PocketObstacles.push_back(CcObs);

        // Create a small enclosed pocket (Comp B) immediately East of the Command Center:
        // Command Center right edge is at X=447 (cell 55). Pocket spans X=[448..495] (cells 56..61), Y=[336..431] (cells 42..53).
        NavigationObstacle PocketTop;
        PocketTop.OwnerEntityID = 310;
        PocketTop.Position = {472.0f, 328.0f};
        PocketTop.Left = 32;
        PocketTop.Right = 31;
        PocketTop.Up = 8;
        PocketTop.Down = 7;
        PocketObstacles.push_back(PocketTop);

        NavigationObstacle PocketBottom;
        PocketBottom.OwnerEntityID = 311;
        PocketBottom.Position = {472.0f, 440.0f};
        PocketBottom.Left = 32;
        PocketBottom.Right = 31;
        PocketBottom.Up = 8;
        PocketBottom.Down = 7;
        PocketObstacles.push_back(PocketBottom);

        NavigationObstacle PocketRight;
        PocketRight.OwnerEntityID = 312;
        PocketRight.Position = {504.0f, 384.0f};
        PocketRight.Left = 8;
        PocketRight.Right = 7;
        PocketRight.Up = 64;
        PocketRight.Down = 63;
        PocketObstacles.push_back(PocketRight);

        NavigationObstacle MineralObs;
        MineralObs.OwnerEntityID = 301;
        MineralObs.Position = {160.0f, 384.0f};
        MineralObs.Left = 32;
        MineralObs.Up = 16;
        MineralObs.Right = 31;
        MineralObs.Down = 15;
        PocketObstacles.push_back(MineralObs);

        NavigationService PocketService;
        PocketService.SetBaseGrid(PocketGrid);
        PocketService.SyncStaticObstacles(PocketObstacles);

        // 1) SCV inside the East half of the Command Center (closer to the sealed East pocket
        // than to the open West side) departing to the West mineral at (204, 384).
        NavigationPathQuery DepartPocketQuery;
        DepartPocketQuery.Start = {408.0f, 384.0f};
        DepartPocketQuery.Target = {204.0f, 384.0f};
        DepartPocketQuery.IgnoredOwnerEntityID = 301;
        DepartPocketQuery.AdditionalIgnoredOwnerEntityID = 300;
        DepartPocketQuery.Footprint = {11, 11, 11, 11};
        const auto DepartPocketResult = PocketService.QueryPath(DepartPocketQuery);
        Require(DepartPocketResult.Status == PathStatus::Exact &&
                DepartPocketResult.ResolvedDestination.DistanceTo(DepartPocketQuery.Target) < 0.1f,
            "SCV departing Command Center adjacent to sealed building pocket resolves open mineral component instead of pocket");

        // 2) SCV on the open West side returning to a delivery target on the East side inside
        // the Command Center (424, 384), which is >12 cells from the West clearance component
        // and immediately adjacent to the sealed East pocket.
        NavigationPathQuery DeepDeliveryQuery;
        DeepDeliveryQuery.Start = {240.0f, 384.0f};
        DeepDeliveryQuery.Target = {424.0f, 384.0f};
        DeepDeliveryQuery.IgnoredOwnerEntityID = 300;
        DeepDeliveryQuery.Footprint = {11, 11, 11, 11};
        const auto DeepDeliveryResult = PocketService.QueryPath(DeepDeliveryQuery);
        Require(DeepDeliveryResult.Status == PathStatus::Exact &&
                DeepDeliveryResult.ResolvedDestination.DistanceTo(DeepDeliveryQuery.Target) < 0.1f,
            "SCV delivery target near sealed side of Command Center resolves outer clearance component across full building width");
    }

    std::printf("NavigationPathfindingTest passed\n");
    return 0;
}
