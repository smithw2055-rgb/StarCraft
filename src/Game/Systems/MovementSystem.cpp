#include "Game/Systems/MovementSystem.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/MovementTuning.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/AStar.h"
#include "Game/Pathfinding/NavigationSpatialHash.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/Math/Vec2.h"
#include "Core/Utils/Logger.h"

#include <unordered_map>
#include <map>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <functional>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
static const Game::Pathfinding::NavigationSpatialHash*
sActiveSpatialHash = nullptr;
static const Game::Pathfinding::NavigationSpatialHash*
sActiveStaticSpatialHash = nullptr;
static MovementNavigationStats* sActiveNavigationStats = nullptr;
static const std::vector<Core::ECS::EntityID>*
sLastPredictiveQuery = nullptr;
static Core::ECS::EntityID
sLastPredictiveQueryEntityID = -1;

static bool IsStaticGroundBlocker(Core::ECS::EntityManager& EM, Core::ECS::EntityID ID);

struct StallDiagnosticTelemetry
{
uint64_t TotalStallEvents = 0;
uint64_t CliffElevationStalls = 0;
uint64_t WaterCoastStalls = 0;
uint64_t StaticBlockerStalls = 0;
uint64_t DynamicUnitStalls = 0;
uint64_t OtherStalls = 0;

struct HotspotKey
{
int TileX;
int TileY;
bool operator<(const HotspotKey& O) const
{
if (TileX != O.TileX) return TileX < O.TileX;
return TileY < O.TileY;
}
};
struct HotspotInfo
{
uint64_t Count = 0;
std::string PrimaryReason;
int Height = 0;
int ChkUnitID = -1;
};
std::map<HotspotKey, HotspotInfo> Hotspots;
float LastReportTime = 0.0f;
};

static bool PathingDebugEnabled()
{
const char* Value = std::getenv("STARCRAFT_DEBUG_PATHING");
return Value && Value[0] != '\0' && Value[0] != '0';
}

static int DebugMovementEntity()
{
static const int Value = []
{
const char* Raw = std::getenv("STARCRAFT_DEBUG_ENTITY");
return Raw ? std::atoi(Raw) : -1;
}();
return Value;
}

static StallDiagnosticTelemetry sDiagnosticTelemetry;

static void MaybePrintStallTelemetryReport(float CurrentTime, bool Force = false)
{
if (!PathingDebugEnabled() && !Force)
return;
if (!Force && CurrentTime - sDiagnosticTelemetry.LastReportTime < 5.0f)
return;
sDiagnosticTelemetry.LastReportTime = CurrentTime;

if (sDiagnosticTelemetry.TotalStallEvents == 0)
return;

std::ostringstream Oss;
Oss << "\n================ [PATHING STALL TELEMETRY REPORT] ================\n";
Oss << "Total Stall Events: " << sDiagnosticTelemetry.TotalStallEvents << "\n";
const float Total = (float)sDiagnosticTelemetry.TotalStallEvents;
Oss << " - Cliff/Elevation Edge  : " << sDiagnosticTelemetry.CliffElevationStalls
<< " (" << std::fixed << std::setprecision(1) << (sDiagnosticTelemetry.CliffElevationStalls *
100.0f / Total) << "%)\n";
Oss << " - Water/Coast/Obstacle  : " << sDiagnosticTelemetry.WaterCoastStalls
<< " (" << (sDiagnosticTelemetry.WaterCoastStalls * 100.0f / Total) << "%)\n";
Oss << " - Dynamic Unit Crowding : " << sDiagnosticTelemetry.DynamicUnitStalls
<< " (" << (sDiagnosticTelemetry.DynamicUnitStalls * 100.0f / Total) << "%)\n";
Oss << " - Static Building Blocker: " << sDiagnosticTelemetry.StaticBlockerStalls
<< " (" << (sDiagnosticTelemetry.StaticBlockerStalls * 100.0f / Total) << "%)\n";
Oss << " - Other/Transient       : " << sDiagnosticTelemetry.OtherStalls
<< " (" << (sDiagnosticTelemetry.OtherStalls * 100.0f / Total) << "%)\n";
Oss << "Top Stall Hotspots:\n";

std::vector<std::pair<StallDiagnosticTelemetry::HotspotKey, StallDiagnosticTelemetry::HotspotInfo>> SortedHotspots(
sDiagnosticTelemetry.Hotspots.begin(), sDiagnosticTelemetry.Hotspots.end());
std::sort(SortedHotspots.begin(), SortedHotspots.end(), [](const auto& A, const auto& B) {
return A.second.Count > B.second.Count;
});
size_t Shown = 0;
for (const auto& Entry : SortedHotspots)
{
if (++Shown > 5) break;
Oss << "   #" << Shown << " Tile(" << Entry.first.TileX << ", " << Entry.first.TileY << ")"
<< " Height=" << Entry.second.Height
<< " Reason=" << Entry.second.PrimaryReason
<< " ChkUnit=" << Entry.second.ChkUnitID
<< " Stalls=" << Entry.second.Count << "\n";
}
Oss << "==================================================================";
Core::Utils::Logger::Info("PathingTelemetry", Oss.str());
}

static const std::vector<Core::ECS::EntityID>& QueryNearbyEntities(
Core::ECS::EntityManager& EM,
float MinimumX, float MinimumY, float MaximumX, float MaximumY)
{
static thread_local std::vector<Core::ECS::EntityID> Result;
if (sActiveSpatialHash)
{
sActiveSpatialHash->QueryInto(
MinimumX, MinimumY, MaximumX, MaximumY, Result);
if (sActiveNavigationStats)
sActiveNavigationStats->NeighborPairs += Result.size();
return Result;
}
auto& SpatialRes = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
SpatialRes.AllUnitsHash.QueryInto(MinimumX, MinimumY, MaximumX, MaximumY, Result);
std::sort(Result.begin(), Result.end());
if (sActiveNavigationStats)
sActiveNavigationStats->NeighborPairs += Result.size();
return Result;
}

static const std::vector<Core::ECS::EntityID>& QueryNearbyStaticEntities(
Core::ECS::EntityManager& EM,
float MinimumX, float MinimumY, float MaximumX, float MaximumY)
{
static thread_local std::vector<Core::ECS::EntityID> Result;
if (sActiveStaticSpatialHash)
{
sActiveStaticSpatialHash->QueryInto(
MinimumX, MinimumY, MaximumX, MaximumY, Result);
if (sActiveNavigationStats)
sActiveNavigationStats->NeighborPairs += Result.size();
return Result;
}
auto& SpatialRes = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
SpatialRes.BuildingsHash.QueryInto(MinimumX, MinimumY, MaximumX, MaximumY, Result);
static thread_local std::vector<Core::ECS::EntityID> ResTemp;
SpatialRes.ResourcesHash.QueryInto(MinimumX, MinimumY, MaximumX, MaximumY, ResTemp);
Result.insert(Result.end(), ResTemp.begin(), ResTemp.end());
std::sort(Result.begin(), Result.end());
Result.erase(std::unique(Result.begin(), Result.end()), Result.end());
if (sActiveNavigationStats)
sActiveNavigationStats->NeighborPairs += Result.size();
return Result;
}

static void LogMovementStall(Core::ECS::EntityManager& EM,
Core::ECS::EntityID EntityID,
const TransformComponent& Transform,
const PathfindingComponent& Path,
const char* Phase,
const Game::Map::GameMap* Map)
{
if (sActiveNavigationStats)
++sActiveNavigationStats->StuckEvents;

if (!PathingDebugEnabled())
return;

++sDiagnosticTelemetry.TotalStallEvents;
int CurTileX = 0;
int CurTileY = 0;
int CurHeight = 0;
if (Map)
{
Map->WorldToTile(Transform.Position, CurTileX, CurTileY);
CurHeight = Map->GetGroundHeight(CurTileX, CurTileY);
}

std::string Cause = "Other";
if (Path.BlockedByEntityID >= 0)
{
if (IsStaticGroundBlocker(EM, Path.BlockedByEntityID))
Cause = "StaticBlocker";
else
Cause = "DynamicUnit";
}
else if (Map)
{
bool NearCliff = false;
bool NearWaterOrUnwalkable = false;
for (int DY = -1; DY <= 1; ++DY)
{
for (int DX = -1; DX <= 1; ++DX)
{
if (DX == 0 && DY == 0) continue;
int NX = CurTileX + DX;
int NY = CurTileY + DY;
if (!Map->IsInBounds(NX, NY))
{
NearWaterOrUnwalkable = true;
continue;
}
if (!Map->IsWalkable(NX, NY))
{
const auto& Tile = Map->GetTile(NX, NY);
if (Tile.Terrain == TerrainType::Water)
NearWaterOrUnwalkable = true;
else
NearCliff = true;
}
else if (std::abs(Map->GetGroundHeight(NX, NY) - CurHeight) >= 1 &&
!Map->CanTraverseGround(CurTileX, CurTileY, NX, NY))
{
NearCliff = true;
}
}
}
if (NearCliff)
Cause = "CliffElevationEdge";
else if (NearWaterOrUnwalkable)
Cause = "WaterCoastEdge";
}

if (Cause == "CliffElevationEdge") ++sDiagnosticTelemetry.CliffElevationStalls;
else if (Cause == "WaterCoastEdge") ++sDiagnosticTelemetry.WaterCoastStalls;
else if (Cause == "StaticBlocker") ++sDiagnosticTelemetry.StaticBlockerStalls;
else if (Cause == "DynamicUnit") ++sDiagnosticTelemetry.DynamicUnitStalls;
else ++sDiagnosticTelemetry.OtherStalls;

const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
auto& Hotspot = sDiagnosticTelemetry.Hotspots[{CurTileX, CurTileY}];
++Hotspot.Count;
Hotspot.PrimaryReason = Cause;
Hotspot.Height = CurHeight;
Hotspot.ChkUnitID = Unit ? Unit->ChkUnitID : -1;

const auto* Command = EM.GetComponent<CommandComponent>(EntityID);
const auto* Harvest = EM.GetComponent<HarvestComponent>(EntityID);
Core::Utils::Logger::Info("Pathing",
std::string("stall cause=") + Cause +
" phase=" + Phase +
" entity=" + std::to_string(EntityID) +
" chk=" + std::to_string(Unit ? Unit->ChkUnitID : -1) +
" tile=" + std::to_string(CurTileX) + "," + std::to_string(CurTileY) +
" height=" + std::to_string(CurHeight) +
" pos=" + std::to_string((int)Transform.Position.X) + "," +
std::to_string((int)Transform.Position.Y) +
" target=" + std::to_string((int)Path.RequestedDestination.X) + "," +
std::to_string((int)Path.RequestedDestination.Y) +
" blocker=" + std::to_string(Path.BlockedByEntityID) +
" cmd=" + std::to_string(Command ? (int)Command->CurrentCommand : -1) +
" harvest=" + std::to_string(Harvest ? (int)Harvest->State : -1));
}

static bool IsSiegedTankUnit(int ChkUnitID)
{
return ChkUnitID == 30 || // Terran Siege Tank Siege Mode
ChkUnitID == 25; // Hero Edmund Duke Siege Mode
}

struct StaticBlockerRect
{
float Left = 16.0f;
float Top = 16.0f;
float Right = 16.0f;
float Bottom = 16.0f;
Core::Math::Vec2 Position;
};

struct MovementEntitySnapshot
{
uint32_t Generation = 0;
TransformComponent* Transform = nullptr;
UnitComponent* Unit = nullptr;
PathfindingComponent* Path = nullptr;
CommandComponent* Command = nullptr;
PlayerComponent* Player = nullptr;
CombatComponent* Combat = nullptr;
HarvestComponent* Harvest = nullptr;
StatusEffectComponent* Status = nullptr;
StaticBlockerRect CollisionRect;
int CommandPriority = 0;
bool StaticGroundBlocker = false;
bool ActiveHarvester = false;
bool AssignedConstructionWorker = false;
bool CanMove = false;
bool IsAir = false;
};

static const std::vector<MovementEntitySnapshot>*
sActiveMovementSnapshots = nullptr;
static uint32_t sActiveMovementSnapshotGeneration = 0;

static const MovementEntitySnapshot* ActiveMovementSnapshot(
Core::ECS::EntityID EntityID)
{
if (!sActiveMovementSnapshots || EntityID < 0 ||
(size_t)EntityID >= sActiveMovementSnapshots->size())
{
return nullptr;
}
const auto& Snapshot =
(*sActiveMovementSnapshots)[(size_t)EntityID];
return Snapshot.Generation == sActiveMovementSnapshotGeneration
? &Snapshot : nullptr;
}

static bool CanMoveEntity(Core::ECS::EntityManager& EM, Core::ECS::EntityID ID)
{
if (const auto* Snap = ActiveMovementSnapshot(ID))
return Snap->CanMove;
auto* Trans = EM.GetComponent<TransformComponent>(ID);
auto* Unit = EM.GetComponent<UnitComponent>(ID);
if (!Trans || !Unit)
return false;
if (Unit->HitPoints <= 0)
return false;
if (EM.GetComponent<ResourceComponent>(ID))
return false;
if (auto* Building = EM.GetComponent<BuildingComponent>(ID))
return Building->IsLifted &&
Building->FlightPhase == BuildingFlightPhase::Airborne;
if (IsSiegedTankUnit(Unit->ChkUnitID))
return false;
if (const auto* Burrow = EM.GetComponent<BurrowComponent>(ID))
{
if (Burrow->IsBurrowed || Burrow->Phase != BurrowPhase::Surface)
return false;
}
return Unit->Speed > 0.0f;
}

static bool ShouldMoveAsAir(Core::ECS::EntityManager& EM, Core::ECS::EntityID ID)
{
if (const auto* Snap = ActiveMovementSnapshot(ID))
return Snap->IsAir;
if (auto* Building = EM.GetComponent<BuildingComponent>(ID))
if (Building->IsLifted)
return true;
auto* Unit = EM.GetComponent<UnitComponent>(ID);
return Unit && Game::Data::IsAirUnitType(Unit->ChkUnitID);
}

static bool IsStaticGroundBlocker(Core::ECS::EntityManager& EM, Core::ECS::EntityID ID)
{
if (const auto* Snap = ActiveMovementSnapshot(ID))
return Snap->StaticGroundBlocker;
if (auto* Building = EM.GetComponent<BuildingComponent>(ID))
return !Building->IsLifted ||
Building->FlightPhase == BuildingFlightPhase::Landing;
return EM.GetComponent<ResourceComponent>(ID) != nullptr;
}

static int CollisionRadiusFromBounds(const PathfindingComponent& Path)
{
if (!Path.HasCollisionBounds)
return Path.CollisionRadius;
const int Width = Path.CollisionLeft + Path.CollisionRight + 1;
const int Height = Path.CollisionUp + Path.CollisionDown + 1;
return std::max(4, std::max(Width, Height) / 2);
}

static Game::Pathfinding::NavigationFootprint NavFootprintForPath(
const PathfindingComponent& Path)
{
if (Path.HasCollisionBounds)
{
return {
Path.CollisionLeft, Path.CollisionUp,
Path.CollisionRight, Path.CollisionDown
};
}
return {
Path.CollisionRadius, Path.CollisionRadius,
Path.CollisionRadius, Path.CollisionRadius
};
}

static StaticBlockerRect StaticBlockerRectForEntity(const UnitComponent* Unit,
const BuildingComponent* Building,
const ResourceComponent* Resource,
const PathfindingComponent* Path)
{
if (Path && Path->HasCollisionBounds)
{
return {
(float)Path->CollisionLeft,
(float)Path->CollisionUp,
(float)Path->CollisionRight + 1.0f,
(float)Path->CollisionDown + 1.0f
};
}
Core::Math::Vec2 Half{16.0f, 16.0f};
if (Resource)
{
Half = {32.0f, 20.0f};
}
else if (Building && Unit)
{
switch (Unit->ChkUnitID)
{
case 106: // Terran Command Center
case 131: // Zerg Hatchery
case 132: // Zerg Lair
case 133: // Zerg Hive
case 154: // Protoss Nexus
Half = {64.0f, 48.0f};
break;
case 111: // Terran Barracks
case 113: // Terran Factory
case 114: // Terran Starport
case 116: // Terran Science Facility
case 160: // Protoss Gateway
case 167: // Protoss Stargate
Half = {56.0f, 40.0f};
break;
case 107: // Terran Comsat Station
case 108: // Terran Nuclear Silo
case 109: // Terran Supply Depot
case 115: // Terran Control Tower
case 120: // Terran Machine Shop
case 124: // Terran Missile Turret
case 125: // Terran Bunker
case 156: // Protoss Pylon
case 162: // Protoss Photon Cannon
Half = {32.0f, 24.0f};
break;
default:
Half = Building->IsAddon ? Core::Math::Vec2{32.0f, 24.0f} :
Core::Math::Vec2{48.0f, 32.0f};
break;
}
}
return {Half.X, Half.Y, Half.X, Half.Y};
}

static void ClearMotion(PathfindingComponent& Path)
{
Path.Path.clear();
Path.PathIndex = 0;
Path.CurrentSpeed = 0.0f;
Path.TargetSpeed = 0.0f;
Path.CurrentVelocity = {0.0f, 0.0f};
Path.FixedVelocity = Core::Math::FixedVec2::Zero();
Path.StallTimer = 0.0f;
Path.StallTicks = 0;
Path.HasLastWaypointDistance = false;
Path.BlockedByEntityID = -1;
}

static void CommitAuthoritativePosition(
TransformComponent& Transform,
PathfindingComponent& Path,
const Core::Math::Vec2& Position,
const Core::Math::Vec2& Velocity)
{
Path.FixedPosition = Core::Math::FixedVec2(Position.X, Position.Y);
Path.FixedVelocity = Core::Math::FixedVec2(Velocity.X, Velocity.Y);
Path.HasFixedPosition = true;
Transform.Position = {
Path.FixedPosition.X.AsFloat(),
Path.FixedPosition.Y.AsFloat()
};
}

static bool IsMovementCommand(CommandType Type)
{
return Type == CommandType::Move ||
Type == CommandType::AttackMove ||
Type == CommandType::Attack ||
Type == CommandType::Patrol ||
Type == CommandType::Harvest ||
Type == CommandType::Build ||
Type == CommandType::Repair ||
Type == CommandType::Follow ||
Type == CommandType::Heal;
}

static bool HasActiveCombatPursuit(Core::ECS::EntityManager& EM,
Core::ECS::EntityID EntityID,
const CommandComponent* Cmd)
{
if (Cmd && Cmd->CurrentCommand == CommandType::Attack)
return true;
const auto* Snap = ActiveMovementSnapshot(EntityID);
const auto* Combat = Snap ? Snap->Combat : EM.GetComponent<CombatComponent>(EntityID);
if (!Combat)
return false;
return Combat->TargetEntityID >= 0 || Combat->AutoTargetEntityID >= 0;
}

static bool IsSatisfiedInteractionPartial(
const TransformComponent* Transform,
const PathfindingComponent& Path)
{
if (!Transform ||
Path.LastPathStatus != Game::Pathfinding::PathStatus::Partial)
{
return false;
}
const float Tolerance = std::max(
Path.NodeTolerance,
(float)CollisionRadiusFromBounds(Path));
return Transform->Position.DistanceTo(
Path.ResolvedPathDestination) <= Tolerance;
}

static bool IsArrivedAtDestinationCluster(
Core::ECS::EntityManager& EM,
Core::ECS::EntityID MovingID,
const TransformComponent* Trans,
const PathfindingComponent& Path,
const CommandComponent* Cmd,
const Core::Math::Vec2& TargetDestination)
{
if (!Trans || !Cmd || !IsMovementCommand(Cmd->CurrentCommand))
return false;
if (Cmd->CurrentCommand == CommandType::Harvest ||
Cmd->CurrentCommand == CommandType::Build ||
Cmd->CurrentCommand == CommandType::Repair ||
Cmd->CurrentCommand == CommandType::Attack)
{
return false;
}

// Units actively pursuing an enemy to surround/engage must not stop at
// the outer friendly cluster boundary unless they repeatedly fail to enter range.
if (HasActiveCombatPursuit(EM, MovingID, Cmd) && Path.ConsecutiveMoveFailures < 2)
return false;

const float MovingRadius = (float)CollisionRadiusFromBounds(Path);
const float DistToDest = Trans->Position.DistanceTo(TargetDestination);
if (Cmd->CurrentCommand == CommandType::AttackMove && DistToDest > 64.0f)
return false;
if (DistToDest > 112.0f)
return false;

const auto* SnapMoving = ActiveMovementSnapshot(MovingID);
const auto* MyPlayer = SnapMoving ? SnapMoving->Player : EM.GetComponent<PlayerComponent>(MovingID);

const auto CheckBlocker = [&](Core::ECS::EntityID BlockerID) -> bool
{
if (BlockerID < 0 || BlockerID == MovingID)
return false;
const auto* SnapOther = ActiveMovementSnapshot(BlockerID);
if (SnapOther)
{
if (SnapOther->StaticGroundBlocker) return false;
}
else if (IsStaticGroundBlocker(EM, BlockerID))
{
return false;
}
const auto* OtherUnit = SnapOther ? SnapOther->Unit : EM.GetComponent<UnitComponent>(BlockerID);
const auto* OtherPlayer = SnapOther ? SnapOther->Player : EM.GetComponent<PlayerComponent>(BlockerID);
const auto* OtherTrans = SnapOther ? SnapOther->Transform : EM.GetComponent<TransformComponent>(BlockerID);
const auto* OtherPath = SnapOther ? SnapOther->Path : EM.GetComponent<PathfindingComponent>(BlockerID);

if (OtherUnit && OtherUnit->HitPoints > 0 &&
OtherTrans && OtherPath &&
OtherPath->IsAir == Path.IsAir &&
OtherPlayer && MyPlayer &&
OtherPlayer->PlayerID == MyPlayer->PlayerID)
{
const bool OtherStopped =
OtherPath->CurrentVelocity.LengthSq() < 0.1f &&
OtherPath->CurrentSpeed < 1.0f &&
OtherPath->Path.empty();
if (OtherStopped)
{
const float OtherRadius = (float)CollisionRadiusFromBounds(*OtherPath);
const float OtherDistToDest = OtherTrans->Position.DistanceTo(TargetDestination);

if (OtherDistToDest > DistToDest + 4.0f)
return false;

const float BlockerMaxDist = OtherRadius + Path.NodeTolerance + 8.0f;
const bool AtDestinationCenter = OtherDistToDest <= BlockerMaxDist;
const bool SameGroupArrived =
OtherPath->RequestedDestination.LengthSq() > 0.0f &&
(OtherPath->RequestedDestination.DistanceTo(TargetDestination) <= 32.0f ||
 (OtherPath->LastPathStatus == Game::Pathfinding::PathStatus::Partial &&
  OtherPath->ResolvedPathDestination.DistanceTo(TargetDestination) <= 32.0f)) &&
OtherDistToDest <= 88.0f;

if (AtDestinationCenter || SameGroupArrived)
{
const float SepDist = Trans->Position.DistanceTo(OtherTrans->Position);
const float MaxSep = MovingRadius + OtherRadius + Path.NodeTolerance + 6.0f;
if (SepDist <= MaxSep)
return true;
}
}
}
return false;
};

if (CheckBlocker(Path.BlockedByEntityID))
return true;

if (sActiveSpatialHash && DistToDest <= 96.0f)
{
static thread_local std::vector<Core::ECS::EntityID> DestNeighbors;
DestNeighbors.clear();
sActiveSpatialHash->QueryInto(
Trans->Position.X - 48.0f, Trans->Position.Y - 48.0f,
Trans->Position.X + 48.0f, Trans->Position.Y + 48.0f,
DestNeighbors);
for (auto OtherID : DestNeighbors)
{
if (CheckBlocker(OtherID))
return true;
}
}
return false;
}

static bool ShouldHaveMovementSpeed(Core::ECS::EntityManager& EM,
Core::ECS::EntityID ID,
const TransformComponent* Trans,
const CommandComponent* Cmd,
const PathfindingComponent& Path)
{
const Core::Math::Vec2 EffectiveClusterDest =
(Path.LastPathStatus == Game::Pathfinding::PathStatus::Partial)
? Path.ResolvedPathDestination
: Path.RequestedDestination;
if (IsArrivedAtDestinationCluster(EM, ID, Trans, Path, Cmd, EffectiveClusterDest))
return false;
if (Path.PathIndex < (int)Path.Path.size())
return true;
const auto* Snap = ActiveMovementSnapshot(ID);
if (const auto* Harvest = Snap ? Snap->Harvest : EM.GetComponent<HarvestComponent>(ID))
{
if (Harvest->InsideGasBuilding ||
Harvest->QueuedAtResource ||
Harvest->State == HarvestState::Mining ||
Harvest->State == HarvestState::Depositing ||
Harvest->State == HarvestState::Idle)
{
return false;
}
}
const auto* Combat = Snap ? Snap->Combat : EM.GetComponent<CombatComponent>(ID);
if (Combat && (Combat->TargetEntityID >= 0 || Combat->AutoTargetEntityID >= 0) &&
Path.Path.empty())
return false;
if (Path.CurrentSpeed > 0.01f || Path.CurrentVelocity.LengthSq() > 0.01f)
return true;
if (!Trans || !Cmd || !IsMovementCommand(Cmd->CurrentCommand))
return false;
if (IsSatisfiedInteractionPartial(Trans, Path))
return false;

const float ArrivalTolerance =
std::max(Path.NodeTolerance, (float)CollisionRadiusFromBounds(Path));
if (Trans->Position.DistanceTo(Path.RequestedDestination) <= ArrivalTolerance)
return false;
if (Path.HasRecentMoveFailure &&
Path.RecentFailedMoveTarget.DistanceTo(Path.RequestedDestination) <=
std::max(16.0f, ArrivalTolerance))
{
return false;
}
return true;
}

static bool RecentlyFailedCurrentRequest(const PathfindingComponent& Path)
{
return Path.HasRecentMoveFailure &&
Path.RecentFailedMoveTarget.DistanceTo(Path.RequestedDestination) <=
std::max(16.0f, Path.NodeTolerance);
}

static void MarkMoveTargetFailed(PathfindingComponent& Path)
{
const int PreservedBlockerID = Path.BlockedByEntityID;
if (Path.RequestedDestination.LengthSq() > 0.0f)
{
Path.HasRecentMoveFailure = true;
Path.RecentFailedMoveTarget = Path.RequestedDestination;
}
else if (!Path.Path.empty())
{
Path.HasRecentMoveFailure = true;
Path.RecentFailedMoveTarget = Path.Path.back();
}
Path.ConsecutiveMoveFailures =
std::min(Path.ConsecutiveMoveFailures + 1, 8);
const int BackoffTicks = std::min(
96, 12 << std::min(Path.ConsecutiveMoveFailures - 1, 3));
Path.RecentMoveFailureCooldown = (float)BackoffTicks / 24.0f;
Path.RecentMoveFailureTicks = BackoffTicks;
ClearMotion(Path);
if (PreservedBlockerID >= 0)
Path.BlockedByEntityID = PreservedBlockerID;
}

static bool TrackMovementProgress(PathfindingComponent& Path,
float OldDistance,
float NewDistance,
float AppliedDistance,
float DeltaTime)
{
const float ProgressEpsilon =
Path.BlockedByEntityID >= 0 ? 0.20f : (1.0f / 256.0f);
const float MinApplied =
Path.BlockedByEntityID >= 0 ? 0.05f : (1.0f / 256.0f);
const bool MadeProgress =
AppliedDistance >= MinApplied &&
NewDistance <= OldDistance - ProgressEpsilon;

if (MadeProgress)
{
Path.StallTimer = 0.0f;
Path.StallTicks = 0;
Path.LastWaypointDistance = NewDistance;
Path.HasLastWaypointDistance = true;
return true;
}

if (Path.HasLastWaypointDistance &&
NewDistance <= Path.LastWaypointDistance - ProgressEpsilon)
{
Path.StallTimer = 0.0f;
Path.StallTicks = 0;
Path.LastWaypointDistance = NewDistance;
return true;
}

(void)DeltaTime;
++Path.StallTicks;
Path.StallTimer = (float)Path.StallTicks / 24.0f;
Path.LastWaypointDistance = NewDistance;
Path.HasLastWaypointDistance = true;
return Path.StallTicks < 14;
}

static bool TrackDestinationProgress(PathfindingComponent& Path,
const TransformComponent& Trans,
float AppliedDistance,
float DeltaTime)
{
if (Path.RequestedDestination.LengthSq() <= 0.0f)
return true;
if (Path.PathIndex + 1 < (int)Path.Path.size())
{
Path.DestinationStallTimer = 0.0f;
Path.DestinationStallTicks = 0;
Path.HasLastDestinationDistance = false;
return true;
}

const float Distance = Trans.Position.DistanceTo(Path.RequestedDestination);
const float ArrivalTolerance =
std::max(Path.NodeTolerance, (float)CollisionRadiusFromBounds(Path));
if (Distance <= ArrivalTolerance)
{
Path.RepathFailCount = 0;
Path.DestinationStallTimer = 0.0f;
Path.DestinationStallTicks = 0;
Path.LastDestinationDistance = Distance;
Path.HasLastDestinationDistance = true;
return true;
}

if (!Path.HasLastDestinationDistance ||
Distance < Path.LastDestinationDistance - 12.0f)
{
Path.RepathFailCount = 0;
Path.DestinationStallTimer = 0.0f;
Path.DestinationStallTicks = 0;
Path.LastDestinationDistance = Distance;
Path.HasLastDestinationDistance = true;
return true;
}

(void)DeltaTime;
(void)AppliedDistance;
++Path.DestinationStallTicks;
Path.DestinationStallTimer =
(float)Path.DestinationStallTicks / 24.0f;
Path.HasLastDestinationDistance = true;
return Path.DestinationStallTicks < 192;
}

static float MovementSpeedMultiplier(const StatusEffectComponent* Status)
{
if (!Status)
return 1.0f;
int Mod = 0;
if (Status->HasEffect(StatusEffect::Stim))
++Mod;
if (Status->HasEffect(StatusEffect::Ensnare))
--Mod;
if (Mod > 0)
return 1.5f;
if (Mod < 0)
return 0.5f;
return 1.0f;
}

static Core::Math::Vec2 ResolveMapWalkability(const Game::Map::GameMap* Map,
Core::Math::Vec2 Current,
Core::Math::Vec2 Proposed,
const PathfindingComponent& Path,
bool IsAir)
{
if (IsAir || !Map)
return Proposed;

const Game::Pathfinding::NavigationFootprint Footprint =
NavFootprintForPath(Path);
const auto& Grid = Map->GetNavigationGrid();
const auto CanUse = [&](const Core::Math::Vec2& Candidate)
{
return Grid.CanTraverseWorld(
Current, Candidate, Footprint,
Path.RequestedIgnoreEntityID,
Path.RequestedAdditionalIgnoreEntityID);
};

if (CanUse(Proposed))
return Proposed;

const Core::Math::Vec2 SlideX{Proposed.X, Current.Y};
if (std::abs(Proposed.X - Current.X) > 0.001f && CanUse(SlideX))
return SlideX;

const Core::Math::Vec2 SlideY{Current.X, Proposed.Y};
if (std::abs(Proposed.Y - Current.Y) > 0.001f && CanUse(SlideY))
return SlideY;

const float StepLen = (Proposed - Current).Length();
const int CurrentCellX = Game::Pathfinding::NavigationGrid::WorldPixelToCell((int)std::floor(Current.X));
const int CurrentCellY = Game::Pathfinding::NavigationGrid::WorldPixelToCell((int)std::floor(Current.Y));
if (StepLen > 0.01f && !Grid.CanOccupy(
CurrentCellX, CurrentCellY, Footprint,
Path.RequestedIgnoreEntityID, Path.RequestedAdditionalIgnoreEntityID))
{
static const Core::Math::Vec2 kEscapeDirs[8] = {
{1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
{0.707f, 0.707f}, {-0.707f, 0.707f}, {0.707f, -0.707f}, {-0.707f, -0.707f}
};
for (const auto& Dir : kEscapeDirs)
{
const Core::Math::Vec2 EscPos = Current + Dir * std::min(4.0f, StepLen);
const int CellX = Game::Pathfinding::NavigationGrid::WorldPixelToCell((int)std::floor(EscPos.X));
const int CellY = Game::Pathfinding::NavigationGrid::WorldPixelToCell((int)std::floor(EscPos.Y));
if (Grid.CanTraverse(CurrentCellX, CurrentCellY, CellX, CellY, Footprint,
Path.RequestedIgnoreEntityID,
Path.RequestedAdditionalIgnoreEntityID))
{
return EscPos;
}
}
}

return Current;
}

static Core::Math::Vec2 PreventBackwardStep(Core::Math::Vec2 Current,
Core::Math::Vec2 Proposed,
Core::Math::Vec2 MoveDir)
{
const Core::Math::Vec2 Applied = Proposed - Current;
if (Applied.Dot(MoveDir) >= -0.001f)
return Proposed;
const Core::Math::Vec2 Lateral = Applied - MoveDir * Applied.Dot(MoveDir);
if (Lateral.LengthSq() > 0.001f)
return Current + Lateral;
return Current;
}

static StaticBlockerRect CollisionRectForPath(const PathfindingComponent& Path)
{
StaticBlockerRect Rect;
if (Path.HasCollisionBounds)
{
Rect.Left = (float)Path.CollisionLeft;
Rect.Top = (float)Path.CollisionUp;
Rect.Right = (float)Path.CollisionRight + 1.0f;
Rect.Bottom = (float)Path.CollisionDown + 1.0f;
}
else
{
const float Radius = (float)std::max(4, Path.CollisionRadius);
Rect.Left = Radius;
Rect.Top = Radius;
Rect.Right = Radius;
Rect.Bottom = Radius;
}
return Rect;
}

static bool IsResourceDepotUnit(int ChkUnitID)
{
return ChkUnitID == 106 || // Terran Command Center
ChkUnitID == 131 || // Zerg Hatchery
ChkUnitID == 132 || // Zerg Lair
ChkUnitID == 133 || // Zerg Hive
ChkUnitID == 154; // Protoss Nexus
}

static bool IsOwnedResourceDepot(Core::ECS::EntityManager& EM,
Core::ECS::EntityID WorkerID,
Core::ECS::EntityID BlockerID)
{
const auto* WorkerPlayer = EM.GetComponent<PlayerComponent>(WorkerID);
const auto* DepotPlayer = EM.GetComponent<PlayerComponent>(BlockerID);
const auto* DepotUnit = EM.GetComponent<UnitComponent>(BlockerID);
const auto* DepotBuilding = EM.GetComponent<BuildingComponent>(BlockerID);
return WorkerPlayer && DepotPlayer && DepotUnit && DepotBuilding &&
WorkerPlayer->PlayerID == DepotPlayer->PlayerID &&
IsResourceDepotUnit(DepotUnit->ChkUnitID);
}

static bool IsEmbeddedInStaticBlocker(Core::ECS::EntityManager& EM,
Core::ECS::EntityID MovingID,
Core::ECS::EntityID BlockerID,
const Core::Math::Vec2& Position);

static bool ShouldIgnoreStaticBlockerAtRuntime(
Core::ECS::EntityManager& EM,
Core::ECS::EntityID MovingID,
Core::ECS::EntityID BlockerID)
{
const auto* Path = EM.GetComponent<PathfindingComponent>(MovingID);
if (!Path)
return false;
const auto* Harvest = EM.GetComponent<HarvestComponent>(MovingID);
if (Harvest && Harvest->State != HarvestState::Idle)
{
if (BlockerID == Harvest->TargetResourceID)
{
const auto* TargetResource =
EM.GetComponent<ResourceComponent>(BlockerID);
if (TargetResource &&
TargetResource->Type == ResourceType::VespeneGas)
{
return true;
}
}
}

if (const auto* Building = EM.GetComponent<BuildingComponent>(BlockerID);
Building && Building->UnderConstruction && Building->HasBuilderUnit(MovingID))
{
return true;
}

return false;
}

static bool CanEscapeOrTouchStaticBlocker(
    Core::ECS::EntityManager& EM,
    Core::ECS::EntityID MovingID,
    Core::ECS::EntityID BlockerID,
    const PathfindingComponent& MovingPath,
    const Core::Math::Vec2& Current,
    const Core::Math::Vec2& Proposed,
    const Core::Math::Vec2& BlockerPos,
    const StaticBlockerRect& Rect,
    float ExpandedLeft,
    float ExpandedRight,
    float ExpandedTop,
    float ExpandedBottom,
    float ProposedDepth)
{
    const float CurrentDepth = std::min(
        std::min(Current.X - ExpandedLeft, ExpandedRight - Current.X),
        std::min(Current.Y - ExpandedTop, ExpandedBottom - Current.Y));

    const auto* Harvest = EM.GetComponent<HarvestComponent>(MovingID);
    const auto* Building = EM.GetComponent<BuildingComponent>(BlockerID);
    const bool IsConstructingThisBuilding =
        Building && Building->UnderConstruction &&
        Building->HasBuilderUnit(MovingID);
    const bool IsReturningToThisDepot =
        Harvest && Harvest->State == HarvestState::MovingToDepot &&
        (BlockerID == Harvest->ReturnBuildingID ||
         IsOwnedResourceDepot(EM, MovingID, BlockerID));

    constexpr float kUnexpandedEpsilon = 0.01f;
    const float UnexpandedLeft = BlockerPos.X - Rect.Left;
    const float UnexpandedRight = BlockerPos.X + Rect.Right;
    const float UnexpandedTop = BlockerPos.Y - Rect.Top;
    const float UnexpandedBottom = BlockerPos.Y + Rect.Bottom;
    const bool InsideUnexpanded =
        Current.X > UnexpandedLeft + kUnexpandedEpsilon &&
        Current.X < UnexpandedRight - kUnexpandedEpsilon &&
        Current.Y > UnexpandedTop + kUnexpandedEpsilon &&
        Current.Y < UnexpandedBottom - kUnexpandedEpsilon;

    const bool AllowedEmbeddedEscape =
        IsOwnedResourceDepot(EM, MovingID, BlockerID) ||
        BlockerID == MovingPath.RequestedIgnoreEntityID ||
        BlockerID == MovingPath.RequestedAdditionalIgnoreEntityID ||
        (Building && Building->HasBuilderUnit(MovingID)) ||
        (Harvest && Harvest->State != HarvestState::Idle &&
         (BlockerID == Harvest->ReturnBuildingID ||
          BlockerID == Harvest->TargetResourceID));

    if (!AllowedEmbeddedEscape)
        return false;

    if (InsideUnexpanded || IsConstructingThisBuilding)
        return true;

    if (CurrentDepth > 0.0f)
    {
        if (ProposedDepth <= CurrentDepth + 0.05f)
            return true;
        if ((Current.X <= UnexpandedLeft + kUnexpandedEpsilon &&
             Proposed.X <= Current.X + 0.05f) ||
            (Current.X >= UnexpandedRight - kUnexpandedEpsilon &&
             Proposed.X >= Current.X - 0.05f) ||
            (Current.Y <= UnexpandedTop + kUnexpandedEpsilon &&
             Proposed.Y <= Current.Y + 0.05f) ||
            (Current.Y >= UnexpandedBottom - kUnexpandedEpsilon &&
             Proposed.Y >= Current.Y - 0.05f))
        {
            return true;
        }
    }

    if (IsReturningToThisDepot)
    {
        return true;
    }

    return false;
}

static Core::Math::Vec2 ResolveStaticBlockers(Core::ECS::EntityManager& EM,
                                              Core::ECS::EntityID MovingID,
                                              Core::Math::Vec2 Proposed,
                                              Core::Math::Vec2 MoveDir,
                                              bool IsAir)
{
    if (IsAir || (sActiveStaticSpatialHash && sActiveStaticSpatialHash->Empty()))
        return Proposed;

    constexpr float kMaximumCollisionExtent = 128.0f;
    const auto& Blockers = QueryNearbyStaticEntities(
        EM,
        Proposed.X - kMaximumCollisionExtent,
        Proposed.Y - kMaximumCollisionExtent,
        Proposed.X + kMaximumCollisionExtent,
        Proposed.Y + kMaximumCollisionExtent);
    if (Blockers.empty())
        return Proposed;

    const auto* SnapMoving = ActiveMovementSnapshot(MovingID);
    auto* MovingPath = SnapMoving ? SnapMoving->Path : EM.GetComponent<PathfindingComponent>(MovingID);
    if (!MovingPath)
        return Proposed;
    const auto* MovingTrans = SnapMoving ? SnapMoving->Transform : EM.GetComponent<TransformComponent>(MovingID);

    for (auto BlockerID : Blockers)
    {
        if (BlockerID == MovingID || !IsStaticGroundBlocker(EM, BlockerID))
            continue;
        if (ShouldIgnoreStaticBlockerAtRuntime(EM, MovingID, BlockerID))
            continue;

        auto* BlockerTrans = EM.GetComponent<TransformComponent>(BlockerID);
        auto* BlockerUnit = EM.GetComponent<UnitComponent>(BlockerID);
        auto* BlockerBuilding = EM.GetComponent<BuildingComponent>(BlockerID);
        auto* BlockerResource = EM.GetComponent<ResourceComponent>(BlockerID);
        auto* BlockerPath = EM.GetComponent<PathfindingComponent>(BlockerID);
        if (!BlockerTrans || !BlockerUnit)
            continue;

        const StaticBlockerRect Rect = StaticBlockerRectForEntity(
            BlockerUnit, BlockerBuilding, BlockerResource, BlockerPath);
        const StaticBlockerRect MovingRect = CollisionRectForPath(*MovingPath);
        const float ExpandedLeft = BlockerTrans->Position.X - Rect.Left - MovingRect.Right;
        const float ExpandedRight = BlockerTrans->Position.X + Rect.Right + MovingRect.Left;
        const float ExpandedTop = BlockerTrans->Position.Y - Rect.Top - MovingRect.Bottom;
        const float ExpandedBottom = BlockerTrans->Position.Y + Rect.Bottom + MovingRect.Top;

        if (Proposed.X <= ExpandedLeft || Proposed.X >= ExpandedRight ||
            Proposed.Y <= ExpandedTop || Proposed.Y >= ExpandedBottom)
            continue;

        const float PushLeft = Proposed.X - ExpandedLeft;
        const float PushRight = ExpandedRight - Proposed.X;
        const float PushTop = Proposed.Y - ExpandedTop;
        const float PushBottom = ExpandedBottom - Proposed.Y;
        const float MinPushX = std::min(PushLeft, PushRight);
        const float MinPushY = std::min(PushTop, PushBottom);
        const float ProposedDepth = std::min(MinPushX, MinPushY);

        if (MovingTrans &&
            CanEscapeOrTouchStaticBlocker(
                EM, MovingID, BlockerID, *MovingPath,
                MovingTrans->Position, Proposed, BlockerTrans->Position, Rect,
                ExpandedLeft, ExpandedRight, ExpandedTop, ExpandedBottom,
                ProposedDepth))
        {
            continue;
        }

        MovingPath->BlockedByEntityID = BlockerID;

        if (MinPushX < MinPushY)
        {
            if (PushLeft < PushRight || (PushLeft == PushRight && MoveDir.X >= 0.0f))
                Proposed.X = ExpandedLeft;
            else
                Proposed.X = ExpandedRight;
        }
        else
        {
            if (PushTop < PushBottom || (PushTop == PushBottom && MoveDir.Y >= 0.0f))
                Proposed.Y = ExpandedTop;
            else
                Proposed.Y = ExpandedBottom;
        }
    }
    return Proposed;
}

static bool HasClearStaticBlockerSegment(Core::ECS::EntityManager& EM,
                                         Core::ECS::EntityID MovingID,
                                         Core::Math::Vec2 From,
                                         Core::Math::Vec2 To,
                                         const PathfindingComponent& MovingPath)
{
  if (MovingPath.IsAir || (sActiveStaticSpatialHash && sActiveStaticSpatialHash->Empty()))
    return true;

  constexpr float kMaximumCollisionExtent = 128.0f;
  const float MinX = std::min(From.X, To.X) - kMaximumCollisionExtent;
  const float MinY = std::min(From.Y, To.Y) - kMaximumCollisionExtent;
  const float MaxX = std::max(From.X, To.X) + kMaximumCollisionExtent;
  const float MaxY = std::max(From.Y, To.Y) + kMaximumCollisionExtent;
  const auto& Blockers = QueryNearbyStaticEntities(EM, MinX, MinY, MaxX, MaxY);
  if (Blockers.empty())
    return true;

  const StaticBlockerRect MovingRect = CollisionRectForPath(MovingPath);
  const float Dist = From.DistanceTo(To);
  const int Steps = std::max(1, static_cast<int>(std::ceil(Dist / 4.0f)));

  for (auto BlockerID : Blockers)
  {
    if (BlockerID == MovingID || !IsStaticGroundBlocker(EM, BlockerID))
      continue;
    if (ShouldIgnoreStaticBlockerAtRuntime(EM, MovingID, BlockerID))
      continue;

    auto* BlockerTrans = EM.GetComponent<TransformComponent>(BlockerID);
    auto* BlockerUnit = EM.GetComponent<UnitComponent>(BlockerID);
    auto* BlockerBuilding = EM.GetComponent<BuildingComponent>(BlockerID);
    auto* BlockerResource = EM.GetComponent<ResourceComponent>(BlockerID);
    auto* BlockerPath = EM.GetComponent<PathfindingComponent>(BlockerID);
    if (!BlockerTrans || !BlockerUnit)
      continue;

    const StaticBlockerRect Rect = StaticBlockerRectForEntity(
        BlockerUnit, BlockerBuilding, BlockerResource, BlockerPath);
    const float ExpandedLeft =
        BlockerTrans->Position.X - Rect.Left - MovingRect.Right;
    const float ExpandedRight =
        BlockerTrans->Position.X + Rect.Right + MovingRect.Left;
    const float ExpandedTop =
        BlockerTrans->Position.Y - Rect.Top - MovingRect.Bottom;
    const float ExpandedBottom =
        BlockerTrans->Position.Y + Rect.Bottom + MovingRect.Top;

    for (int Step = 1; Step <= Steps; ++Step)
    {
      const float T = static_cast<float>(Step) / static_cast<float>(Steps);
      const Core::Math::Vec2 Sample = From + (To - From) * T;
      if (Sample.X <= ExpandedLeft || Sample.X >= ExpandedRight ||
          Sample.Y <= ExpandedTop || Sample.Y >= ExpandedBottom)
      {
        continue;
      }

      const float PushLeft = Sample.X - ExpandedLeft;
      const float PushRight = ExpandedRight - Sample.X;
      const float PushTop = Sample.Y - ExpandedTop;
      const float PushBottom = ExpandedBottom - Sample.Y;
      const float SampleDepth =
          std::min(std::min(PushLeft, PushRight), std::min(PushTop, PushBottom));
      if (!CanEscapeOrTouchStaticBlocker(
              EM, MovingID, BlockerID, MovingPath, From, Sample,
              BlockerTrans->Position, Rect, ExpandedLeft, ExpandedRight,
              ExpandedTop, ExpandedBottom, SampleDepth))
      {
        return false;
      }
    }
  }
  return true;
}

static StaticBlockerRect DynamicCollisionRectForPath(
const PathfindingComponent& Path)
{
return CollisionRectForPath(Path);
}

static float DynamicCollisionOverlap(Core::Math::Vec2 PositionA,
const StaticBlockerRect& RectA,
Core::Math::Vec2 PositionB,
const StaticBlockerRect& RectB)
{
const float OverlapX = std::min(PositionA.X + RectA.Right,
PositionB.X + RectB.Right) -
std::max(PositionA.X - RectA.Left,
PositionB.X - RectB.Left);
const float OverlapY = std::min(PositionA.Y + RectA.Bottom,
PositionB.Y + RectB.Bottom) -
std::max(PositionA.Y - RectA.Top,
PositionB.Y - RectB.Top);
if (OverlapX <= 0.0f || OverlapY <= 0.0f)
return 0.0f;
return OverlapX * OverlapY;
}

static bool IsActiveHarvester(Core::ECS::EntityManager& EM,
Core::ECS::EntityID EntityID)
{
const auto* Harvest = EM.GetComponent<HarvestComponent>(EntityID);
return Harvest && Harvest->State != HarvestState::Idle;
}

static bool IsEmbeddedInStaticBlocker(Core::ECS::EntityManager& EM,
Core::ECS::EntityID MovingID,
Core::ECS::EntityID BlockerID,
const Core::Math::Vec2& Position)
{
if (BlockerID < 0 || !IsStaticGroundBlocker(EM, BlockerID))
return false;
const auto* MovingPath = EM.GetComponent<PathfindingComponent>(MovingID);
const auto* BlockerTrans = EM.GetComponent<TransformComponent>(BlockerID);
const auto* BlockerUnit = EM.GetComponent<UnitComponent>(BlockerID);
const auto* BlockerBuilding = EM.GetComponent<BuildingComponent>(BlockerID);
const auto* BlockerResource = EM.GetComponent<ResourceComponent>(BlockerID);
const auto* BlockerPath = EM.GetComponent<PathfindingComponent>(BlockerID);
if (!MovingPath || !BlockerTrans || !BlockerUnit)
return false;

const StaticBlockerRect BlockerRect = StaticBlockerRectForEntity(
BlockerUnit, BlockerBuilding, BlockerResource, BlockerPath);
const StaticBlockerRect MovingRect = CollisionRectForPath(*MovingPath);
const float ExpandedLeft =
BlockerTrans->Position.X - BlockerRect.Left - MovingRect.Right;
const float ExpandedRight =
BlockerTrans->Position.X + BlockerRect.Right + MovingRect.Left;
const float ExpandedTop =
BlockerTrans->Position.Y - BlockerRect.Top - MovingRect.Bottom;
const float ExpandedBottom =
BlockerTrans->Position.Y + BlockerRect.Bottom + MovingRect.Top;

return Position.X > ExpandedLeft && Position.X < ExpandedRight &&
Position.Y > ExpandedTop && Position.Y < ExpandedBottom;
}

static bool IsAssignedConstructionWorker(Core::ECS::EntityManager& EM,
Core::ECS::EntityID EntityID)
{
const auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
if (!Path || Path->RequestedIgnoreEntityID < 0)
return false;
const auto* Building =
EM.GetComponent<BuildingComponent>(Path->RequestedIgnoreEntityID);
if (!Building || !Building->UnderConstruction || !Building->HasBuilderUnit(EntityID))
return false;

const auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
const auto* BldgTrans = EM.GetComponent<TransformComponent>(Path->RequestedIgnoreEntityID);
const auto* BldgUnit = EM.GetComponent<UnitComponent>(Path->RequestedIgnoreEntityID);
const auto* BldgPath = EM.GetComponent<PathfindingComponent>(Path->RequestedIgnoreEntityID);
if (!Trans || !BldgTrans || !BldgUnit)
return false;

const StaticBlockerRect BlockerRect = StaticBlockerRectForEntity(
BldgUnit, Building, nullptr, BldgPath);
const float WorkerRadius = (float)CollisionRadiusFromBounds(*Path);
constexpr float kProximityMargin = 28.0f;
const float NearLeft = BldgTrans->Position.X - BlockerRect.Left - WorkerRadius - kProximityMargin;
const float NearRight = BldgTrans->Position.X + BlockerRect.Right + WorkerRadius + kProximityMargin;
const float NearTop = BldgTrans->Position.Y - BlockerRect.Top - WorkerRadius - kProximityMargin;
const float NearBottom = BldgTrans->Position.Y + BlockerRect.Bottom + WorkerRadius + kProximityMargin;
return Trans->Position.X >= NearLeft && Trans->Position.X <= NearRight &&
Trans->Position.Y >= NearTop && Trans->Position.Y <= NearBottom;
}

static Core::Math::Vec2 DynamicCollisionSeparation(
Core::Math::Vec2 PositionA,
const StaticBlockerRect& RectA,
Core::Math::Vec2 PositionB,
const StaticBlockerRect& RectB,
bool PreferPositive)
{
const float OverlapX = std::min(PositionA.X + RectA.Right,
PositionB.X + RectB.Right) -
std::max(PositionA.X - RectA.Left,
PositionB.X - RectB.Left);
const float OverlapY = std::min(PositionA.Y + RectA.Bottom,
PositionB.Y + RectB.Bottom) -
std::max(PositionA.Y - RectA.Top,
PositionB.Y - RectB.Top);
if (OverlapX <= 0.0f || OverlapY <= 0.0f)
return {0.0f, 0.0f};

if (OverlapX < OverlapY)
{
const float Direction = PositionA.X < PositionB.X ? -1.0f :
(PositionA.X > PositionB.X ? 1.0f : (PreferPositive ? 1.0f : -1.0f));
return {Direction * OverlapX, 0.0f};
}

const float Direction = PositionA.Y < PositionB.Y ? -1.0f :
(PositionA.Y > PositionB.Y ? 1.0f : (PreferPositive ? 1.0f : -1.0f));
return {0.0f, Direction * OverlapY};
}

static Core::ECS::EntityID FindDynamicUnitCollision(
Core::ECS::EntityManager& EM,
Core::ECS::EntityID MovingID,
Core::Math::Vec2 Current,
Core::Math::Vec2 Proposed)
{
const auto* MovingSnapshot = ActiveMovementSnapshot(MovingID);
auto* MovingPath = MovingSnapshot
? MovingSnapshot->Path
: EM.GetComponent<PathfindingComponent>(MovingID);
if (!MovingPath || MovingPath->IsAir)
return -1;

if (MovingSnapshot
? (MovingSnapshot->AssignedConstructionWorker || MovingSnapshot->ActiveHarvester)
: (IsAssignedConstructionWorker(EM, MovingID) || IsActiveHarvester(EM, MovingID)))
{
sLastPredictiveQuery = nullptr;
sLastPredictiveQueryEntityID = -1;
return -1;
}

const StaticBlockerRect MovingRect =
DynamicCollisionRectForPath(*MovingPath);
constexpr float kDynamicQueryExtent = 96.0f;
const float MinimumX =
std::min(Current.X, Proposed.X) - kDynamicQueryExtent;
const float MinimumY =
std::min(Current.Y, Proposed.Y) - kDynamicQueryExtent;
const float MaximumX =
std::max(Current.X, Proposed.X) + kDynamicQueryExtent;
const float MaximumY =
std::max(Current.Y, Proposed.Y) + kDynamicQueryExtent;

const std::vector<Core::ECS::EntityID>* UnitsPointer = nullptr;
if (sLastPredictiveQuery && sLastPredictiveQueryEntityID == MovingID)
{
UnitsPointer = sLastPredictiveQuery;
}
else
{
UnitsPointer = &QueryNearbyEntities(
EM, MinimumX, MinimumY, MaximumX, MaximumY);
}
const auto& Units = *UnitsPointer;
sLastPredictiveQuery = nullptr;
sLastPredictiveQueryEntityID = -1;

for (auto OtherID : Units)
{
const auto* OtherSnapshot = ActiveMovementSnapshot(OtherID);
if (OtherID == MovingID ||
(OtherSnapshot && OtherSnapshot->StaticGroundBlocker) ||
(!OtherSnapshot && !sActiveSpatialHash && IsStaticGroundBlocker(EM, OtherID)))
continue;

if (OtherSnapshot
? (OtherSnapshot->AssignedConstructionWorker || OtherSnapshot->ActiveHarvester)
: (IsAssignedConstructionWorker(EM, OtherID) || IsActiveHarvester(EM, OtherID)))
{
continue;
}

auto* OtherTrans = OtherSnapshot
? OtherSnapshot->Transform
: EM.GetComponent<TransformComponent>(OtherID);
auto* OtherUnit = OtherSnapshot
? OtherSnapshot->Unit
: EM.GetComponent<UnitComponent>(OtherID);
auto* OtherPath = OtherSnapshot
? OtherSnapshot->Path
: EM.GetComponent<PathfindingComponent>(OtherID);
if (!OtherTrans || !OtherUnit || !OtherPath ||
OtherUnit->HitPoints <= 0 || OtherPath->IsAir)
{
continue;
}

const StaticBlockerRect OtherRect = OtherSnapshot
? OtherSnapshot->CollisionRect
: DynamicCollisionRectForPath(*OtherPath);
const float ProposedOverlap = DynamicCollisionOverlap(
Proposed, MovingRect, OtherTrans->Position, OtherRect);
if (ProposedOverlap <= 0.0f)
continue;
const float CurrentOverlap = DynamicCollisionOverlap(
Current, MovingRect, OtherTrans->Position, OtherRect);

const Core::Math::Vec2 Applied = Proposed - Current;
const bool MovingAwayFromOther =
CurrentOverlap > 0.0f &&
Applied.Dot(Current - OtherTrans->Position) > 0.0f &&
ProposedOverlap <= CurrentOverlap + 0.01f;
if (MovingAwayFromOther)
continue;

const bool EntersNewOverlap =
CurrentOverlap <= 0.0f && ProposedOverlap > 0.0f;
const bool FollowsMovingBlocker =
Applied.LengthSq() > 0.001f &&
OtherPath->CurrentVelocity.LengthSq() > 0.001f &&
Applied.Dot(OtherPath->CurrentVelocity) > 0.0f;
const bool WorsensExistingOverlap =
CurrentOverlap > 0.0f &&
ProposedOverlap > CurrentOverlap + 0.01f &&
!FollowsMovingBlocker;

const float MovingArea =
(MovingRect.Left + MovingRect.Right) *
(MovingRect.Top + MovingRect.Bottom);
const float OtherArea =
(OtherRect.Left + OtherRect.Right) *
(OtherRect.Top + OtherRect.Bottom);
const float SmallerArea = std::min(MovingArea, OtherArea);
const bool AlreadyNearlyMerged =
SmallerArea > 0.0f && CurrentOverlap >= SmallerArea * 0.90f;
const bool FailsToEscapeExistingOverlap =
AlreadyNearlyMerged && ProposedOverlap > 0.0f &&
ProposedOverlap >= CurrentOverlap - 0.01f;

if (EntersNewOverlap || WorsensExistingOverlap || FailsToEscapeExistingOverlap)
{
return OtherID;
}
}
return -1;
}

static int MovementCommandPriority(const CommandComponent* Command)
{
if (!Command)
return 0;
switch (Command->CurrentCommand)
{
case CommandType::HoldPosition:
return 5;
case CommandType::Harvest:
case CommandType::Build:
case CommandType::Repair:
return 4;
case CommandType::Attack:
case CommandType::AttackMove:
case CommandType::Heal:
return 3;
case CommandType::Patrol:
case CommandType::Follow:
return 2;
case CommandType::Move:
return 1;
default:
return 0;
}
}

struct AvoidanceCandidate
{
int Index = 18;
int DirectionOffset = 0;
int SpeedPercent = 0;
Core::Math::FixedVec2 Velocity;
int CollisionTick = 13;
int64_t Progress = 0;
int Deviation = 256;
int Slowdown = 2;
int FacingDeviation = 256;
bool SameTurnSide = false;
};

static Core::Math::FixedVec2 QuantizedDirectionVelocity(
int DirectionIndex, float Speed, int SpeedPercent)
{
static const std::array<std::pair<int16_t, int16_t>, 256>
DirectionTable = []
{
std::array<std::pair<int16_t, int16_t>, 256> Table{};
constexpr double kTau = 6.28318530717958647692;
for (int Index = 0; Index < 256; ++Index)
{
const double Angle = (double)Index * kTau / 256.0;
Table[(size_t)Index] = {
(int16_t)std::lround(std::cos(Angle) * 256.0),
(int16_t)std::lround(std::sin(Angle) * 256.0)};
}
return Table;
}();

const auto [DirectionX, DirectionY] =
DirectionTable[(size_t)(DirectionIndex & 255)];
const int SpeedRaw = Core::Math::fp8::FromFloat(Speed).raw_value;
return {
Core::Math::fp8::FromRaw(
(Core::Math::fp8::raw_type)(
(int64_t)DirectionX * SpeedRaw *
SpeedPercent / (256 * 100))),
Core::Math::fp8::FromRaw(
(Core::Math::fp8::raw_type)(
(int64_t)DirectionY * SpeedRaw *
SpeedPercent / (256 * 100)))
};
}

static bool SweptAxisInterval(
int64_t Delta,
int64_t RelativeVelocity,
int64_t LowerBound,
int64_t UpperBound,
int64_t& InOutEntry,
int64_t& InOutExit)
{
if (RelativeVelocity == 0)
return Delta > LowerBound && Delta < UpperBound;

constexpr int64_t kTickQ16Scale = 24LL * 65536LL;
int64_t First =
(LowerBound - Delta) * kTickQ16Scale / RelativeVelocity;
int64_t Second =
(UpperBound - Delta) * kTickQ16Scale / RelativeVelocity;
if (First > Second)
std::swap(First, Second);
InOutEntry = std::max(InOutEntry, First);
InOutExit = std::min(InOutExit, Second);
return InOutEntry <= InOutExit;
}

static int SweptAabbCollisionTick(
const Core::Math::FixedVec2& MovingStart,
const Core::Math::FixedVec2& MovingVelocity,
const StaticBlockerRect& MovingRect,
const Core::Math::FixedVec2& OtherStart,
const Core::Math::FixedVec2& OtherVelocity,
const StaticBlockerRect& OtherRect)
{
constexpr int64_t kHorizonQ16 = 12LL * 65536LL;
int64_t Entry = 0;
int64_t Exit = kHorizonQ16;

const int64_t DeltaX =
(int64_t)OtherStart.X.raw_value - (int64_t)MovingStart.X.raw_value;
const int64_t DeltaY =
(int64_t)OtherStart.Y.raw_value - (int64_t)MovingStart.Y.raw_value;
const int64_t RelativeX =
(int64_t)OtherVelocity.X.raw_value - (int64_t)MovingVelocity.X.raw_value;
const int64_t RelativeY =
(int64_t)OtherVelocity.Y.raw_value - (int64_t)MovingVelocity.Y.raw_value;

const int64_t LowerX = -(int64_t)(
(OtherRect.Right + MovingRect.Left) * 256.0f + 0.5f);
const int64_t UpperX = (int64_t)(
(MovingRect.Right + OtherRect.Left) * 256.0f + 0.5f);
const int64_t LowerY = -(int64_t)(
(OtherRect.Bottom + MovingRect.Top) * 256.0f + 0.5f);
const int64_t UpperY = (int64_t)(
(MovingRect.Bottom + OtherRect.Top) * 256.0f + 0.5f);

if (!SweptAxisInterval(
DeltaX, RelativeX, LowerX, UpperX, Entry, Exit) ||
!SweptAxisInterval(
DeltaY, RelativeY, LowerY, UpperY, Entry, Exit) ||
Exit < 0 || Entry > kHorizonQ16)
{
return 13;
}

const bool TrailingX =
(MovingVelocity.X.raw_value > 0 && DeltaX <= LowerX + 256) ||
(MovingVelocity.X.raw_value < 0 && DeltaX >= UpperX - 256);
const bool TrailingY =
(MovingVelocity.Y.raw_value > 0 && DeltaY <= LowerY + 256) ||
(MovingVelocity.Y.raw_value < 0 && DeltaY >= UpperY - 256);
const bool PrimaryX =
std::abs(MovingVelocity.X.raw_value) >= std::abs(MovingVelocity.Y.raw_value);
if (PrimaryX ? TrailingX : TrailingY)
{
return 13;
}

Entry = std::max<int64_t>(0, Entry);
return std::clamp(
(int)((Entry + 65535LL) / 65536LL), 1, 12);
}

struct PredictiveNeighbor
{
Core::ECS::EntityID EntityID = -1;
Core::Math::FixedVec2 Start;
Core::Math::FixedVec2 Velocity;
StaticBlockerRect Rect;
int64_t DistanceSquared = 0;
int CommandPriority = 0;
bool IsMoving = false;
};

static int PredictedCollisionTick(
const Core::Math::FixedVec2& Start,
const StaticBlockerRect& MovingRect,
int MovingPriority,
Core::ECS::EntityID MovingID,
const Core::Math::FixedVec2& CandidateVelocity,
const std::vector<PredictiveNeighbor>& Neighbors,
Core::ECS::EntityID* OutBlockerID = nullptr)
{
int EarliestTick = 13;
Core::ECS::EntityID EarliestBlocker = -1;
for (const auto& Other : Neighbors)
{
const int64_t ForwardDot =
((int64_t)Other.Start.X.raw_value - (int64_t)Start.X.raw_value) *
CandidateVelocity.X.raw_value +
((int64_t)Other.Start.Y.raw_value - (int64_t)Start.Y.raw_value) *
CandidateVelocity.Y.raw_value;
const bool OtherIsAhead = ForwardDot > 0;
const bool MovingKeepsRoute =
Other.IsMoving && !OtherIsAhead &&
(MovingPriority > Other.CommandPriority ||
(MovingPriority == Other.CommandPriority &&
MovingID < Other.EntityID));
if (MovingKeepsRoute)
continue;

const int CollisionTick = SweptAabbCollisionTick(
Start, CandidateVelocity, MovingRect,
Other.Start, Other.Velocity, Other.Rect);
if (CollisionTick < EarliestTick ||
(CollisionTick == EarliestTick &&
Other.EntityID < EarliestBlocker))
{
EarliestTick = CollisionTick;
EarliestBlocker = Other.EntityID;
}
if (EarliestTick == 1)
break;
}
if (OutBlockerID)
*OutBlockerID = EarliestBlocker;
return EarliestTick;
}

static Core::Math::Vec2 SelectPredictiveAvoidanceDirection(
Core::ECS::EntityManager& EM,
Core::ECS::EntityID MovingID,
const Core::Math::Vec2& DesiredDirection,
float Speed,
const Game::Map::GameMap* Map = nullptr)
{
const auto* MovingSnapshot = ActiveMovementSnapshot(MovingID);
const auto* MovingTransform = MovingSnapshot
? MovingSnapshot->Transform
: EM.GetComponent<TransformComponent>(MovingID);
auto* MovingPath = MovingSnapshot
? MovingSnapshot->Path
: EM.GetComponent<PathfindingComponent>(MovingID);
if (!MovingTransform || !MovingPath || Speed <= 0.0f)
return DesiredDirection;

constexpr double kDirectionScale = 256.0 / 6.28318530717958647692;
int DesiredIndex = (int)std::lround(
std::atan2(DesiredDirection.Y, DesiredDirection.X) * kDirectionScale);
DesiredIndex &= 255;

if (MovingPath->IsAir ||
(MovingSnapshot
? (MovingSnapshot->ActiveHarvester || MovingSnapshot->AssignedConstructionWorker)
: (IsActiveHarvester(EM, MovingID) || IsAssignedConstructionWorker(EM, MovingID))))
{
return DesiredDirection;
}

const float PredictionExtent = std::clamp(
Speed * (12.0f / 24.0f) + 48.0f, 64.0f, 128.0f);
const auto& NeighborIDs = QueryNearbyEntities(
EM,
MovingTransform->Position.X - PredictionExtent,
MovingTransform->Position.Y - PredictionExtent,
MovingTransform->Position.X + PredictionExtent,
MovingTransform->Position.Y + PredictionExtent);
sLastPredictiveQuery = &NeighborIDs;
sLastPredictiveQueryEntityID = MovingID;

if (NeighborIDs.empty() || (NeighborIDs.size() == 1 && NeighborIDs[0] == MovingID))
{
return DesiredDirection;
}

const Core::Math::FixedVec2 MovingStart =
MovingPath->HasFixedPosition
? MovingPath->FixedPosition
: Core::Math::FixedVec2(
MovingTransform->Position.X,
MovingTransform->Position.Y);
const StaticBlockerRect MovingRect = DynamicCollisionRectForPath(*MovingPath);
const int MovingPriority = MovingSnapshot
? MovingSnapshot->CommandPriority
: MovementCommandPriority(EM.GetComponent<CommandComponent>(MovingID));

const Core::Math::FixedVec2 DesiredFullVelocity =
QuantizedDirectionVelocity(DesiredIndex, Speed, 100);
int DesiredCollisionTick = 13;

static thread_local std::vector<PredictiveNeighbor> PredictiveNeighbors;
PredictiveNeighbors.clear();
constexpr size_t kMaximumPredictiveNeighbors = 8;
if (PredictiveNeighbors.capacity() < kMaximumPredictiveNeighbors)
{
PredictiveNeighbors.reserve(kMaximumPredictiveNeighbors);
}

const auto Nearer = [](
const PredictiveNeighbor& A,
const PredictiveNeighbor& B)
{
if (A.DistanceSquared != B.DistanceSquared)
return A.DistanceSquared < B.DistanceSquared;
return A.EntityID < B.EntityID;
};

for (const auto OtherID : NeighborIDs)
{
const auto* OtherSnapshot = ActiveMovementSnapshot(OtherID);
if (OtherID == MovingID ||
(OtherSnapshot && OtherSnapshot->StaticGroundBlocker) ||
(!OtherSnapshot && !sActiveSpatialHash && IsStaticGroundBlocker(EM, OtherID)) ||
(OtherSnapshot
? (OtherSnapshot->AssignedConstructionWorker || OtherSnapshot->ActiveHarvester)
: (IsAssignedConstructionWorker(EM, OtherID) || IsActiveHarvester(EM, OtherID))))
{
continue;
}

const auto* OtherTransform = OtherSnapshot
? OtherSnapshot->Transform
: EM.GetComponent<TransformComponent>(OtherID);
const auto* OtherUnit = OtherSnapshot
? OtherSnapshot->Unit
: EM.GetComponent<UnitComponent>(OtherID);
const auto* OtherPath = OtherSnapshot
? OtherSnapshot->Path
: EM.GetComponent<PathfindingComponent>(OtherID);
if (!OtherTransform || !OtherUnit || !OtherPath ||
OtherUnit->HitPoints <= 0 ||
OtherPath->IsAir != MovingPath->IsAir)
{
continue;
}

const Core::Math::FixedVec2 OtherStart = OtherPath->HasFixedPosition
? OtherPath->FixedPosition
: Core::Math::FixedVec2(
OtherTransform->Position.X,
OtherTransform->Position.Y);
const int64_t NeighborDeltaX =
(int64_t)OtherStart.X.raw_value - (int64_t)MovingStart.X.raw_value;
const int64_t NeighborDeltaY =
(int64_t)OtherStart.Y.raw_value - (int64_t)MovingStart.Y.raw_value;
const int64_t DistanceSquared =
NeighborDeltaX * NeighborDeltaX + NeighborDeltaY * NeighborDeltaY;

if (PredictiveNeighbors.size() >= kMaximumPredictiveNeighbors)
{
const auto& Furthest = PredictiveNeighbors.front();
if (DistanceSquared > Furthest.DistanceSquared ||
(DistanceSquared == Furthest.DistanceSquared &&
OtherID >= Furthest.EntityID))
{
continue;
}
}

PredictiveNeighbor Other;
Other.EntityID = OtherID;
Other.Start = OtherStart;
Other.Velocity = Core::Math::FixedVec2(
OtherPath->CurrentVelocity.X,
OtherPath->CurrentVelocity.Y);
Other.Rect = OtherSnapshot
? OtherSnapshot->CollisionRect
: DynamicCollisionRectForPath(*OtherPath);
Other.DistanceSquared = DistanceSquared;
Other.CommandPriority = OtherSnapshot
? OtherSnapshot->CommandPriority
: MovementCommandPriority(EM.GetComponent<CommandComponent>(OtherID));
Other.IsMoving =
Other.Velocity.X.raw_value != 0 || Other.Velocity.Y.raw_value != 0;

if (PredictiveNeighbors.size() < kMaximumPredictiveNeighbors)
{
PredictiveNeighbors.push_back(Other);
std::push_heap(
PredictiveNeighbors.begin(), PredictiveNeighbors.end(), Nearer);
}
else
{
std::pop_heap(
PredictiveNeighbors.begin(), PredictiveNeighbors.end(), Nearer);
PredictiveNeighbors.back() = Other;
std::push_heap(
PredictiveNeighbors.begin(), PredictiveNeighbors.end(), Nearer);
}
}

for (const auto& Other : PredictiveNeighbors)
{
const int64_t NeighborDeltaX =
(int64_t)Other.Start.X.raw_value - (int64_t)MovingStart.X.raw_value;
const int64_t NeighborDeltaY =
(int64_t)Other.Start.Y.raw_value - (int64_t)MovingStart.Y.raw_value;
const int64_t ForwardDot =
NeighborDeltaX * DesiredFullVelocity.X.raw_value +
NeighborDeltaY * DesiredFullVelocity.Y.raw_value;
const bool OtherIsAhead = ForwardDot > 0;
const bool MovingKeepsRoute =
Other.IsMoving && !OtherIsAhead &&
(MovingPriority > Other.CommandPriority ||
(MovingPriority == Other.CommandPriority &&
MovingID < Other.EntityID));

if (!MovingKeepsRoute)
{
DesiredCollisionTick = std::min(
DesiredCollisionTick,
SweptAabbCollisionTick(
MovingStart, DesiredFullVelocity, MovingRect,
Other.Start, Other.Velocity, Other.Rect));
}
}

if (!MovingPath->IsAir && !PredictiveNeighbors.empty())
{
Core::Math::Vec2 Separation;
for (const auto& Other : PredictiveNeighbors)
{
Separation = Separation + DynamicCollisionSeparation(
MovingTransform->Position, MovingRect,
{Other.Start.X.AsFloat(), Other.Start.Y.AsFloat()},
Other.Rect, MovingID > Other.EntityID);
}
const Core::Math::Vec2 LateralSeparation =
    Separation - DesiredDirection * Separation.Dot(DesiredDirection);
const float LateralLen = LateralSeparation.Length();
if (LateralLen > 0.5f)
{
    const float BlendWeight = std::min(1.0f, LateralLen / 8.0f);
    const Core::Math::Vec2 ProposedDir =
        (DesiredDirection * (1.0f - BlendWeight * 0.3f) +
        (LateralSeparation / LateralLen) * (BlendWeight * 0.7f)).Normalized();

    const Core::Math::Vec2 ProbePos =
        MovingTransform->Position + ProposedDir * 16.0f;
    if (!Map || MovingPath->IsAir ||
        Map->GetNavigationGrid().CanTraverseWorld(
            MovingTransform->Position, ProbePos, NavFootprintForPath(*MovingPath),
            MovingPath->RequestedIgnoreEntityID,
            MovingPath->RequestedAdditionalIgnoreEntityID))
    {
        return ProposedDir;
    }
}
}

if (DesiredCollisionTick > 12)
return DesiredDirection;

std::sort(
PredictiveNeighbors.begin(), PredictiveNeighbors.end(),
[](const PredictiveNeighbor& A, const PredictiveNeighbor& B)
{
return A.EntityID < B.EntityID;
});

const Game::Pathfinding::NavigationFootprint NavFootprint =
NavFootprintForPath(*MovingPath);
const int CurrentFacingIndex =
(int)std::lround(MovingPath->FacingAngle * kDirectionScale) & 255;
int CurrentTurnSide = (CurrentFacingIndex - DesiredIndex) & 255;
if (CurrentTurnSide > 128)
CurrentTurnSide -= 256;



static constexpr int DirectionOffsets[9] =
{0, 8, -8, 16, -16, 32, -32, 64, -64};
std::array<AvoidanceCandidate, 19> Candidates{};
size_t CandidateCount = 0;

for (int DirectionOrdinal = 0; DirectionOrdinal < 9; ++DirectionOrdinal)
{
const int DirectionOffset = DirectionOffsets[DirectionOrdinal];
if (Map && !MovingPath->IsAir && DirectionOffset != 0)
{
const auto ProbeVel = QuantizedDirectionVelocity(
DesiredIndex + DirectionOffset, Speed, 100);
const Core::Math::Vec2 CandDir{
ProbeVel.X.AsFloat(), ProbeVel.Y.AsFloat()};
if (CandDir.LengthSq() > 0.001f)
{
const Core::Math::Vec2 ProbePos =
MovingTransform->Position + CandDir.Normalized() * 16.0f;
if (!Map->GetNavigationGrid().CanTraverseWorld(
MovingTransform->Position, ProbePos, NavFootprint,
MovingPath->RequestedIgnoreEntityID,
MovingPath->RequestedAdditionalIgnoreEntityID))
{
continue;
}
}
}

for (int SpeedOrdinal = 0; SpeedOrdinal < 2; ++SpeedOrdinal)
{
AvoidanceCandidate Candidate;
Candidate.Index = DirectionOrdinal * 2 + SpeedOrdinal;
Candidate.DirectionOffset = DirectionOffset;
Candidate.SpeedPercent = SpeedOrdinal == 0 ? 100 : 50;
Candidate.Velocity = QuantizedDirectionVelocity(
DesiredIndex + Candidate.DirectionOffset, Speed, Candidate.SpeedPercent);
Candidate.CollisionTick = PredictedCollisionTick(
MovingStart, MovingRect, MovingPriority, MovingID,
Candidate.Velocity, PredictiveNeighbors);

if (Candidate.Index == 0 && Candidate.CollisionTick > 12)
{
return DesiredDirection;
}

Candidate.Progress =
(int64_t)Candidate.Velocity.X.raw_value *
Core::Math::fp8::FromFloat(DesiredDirection.X).raw_value +
(int64_t)Candidate.Velocity.Y.raw_value *
Core::Math::fp8::FromFloat(DesiredDirection.Y).raw_value;
Candidate.Deviation = std::abs(Candidate.DirectionOffset);
const int CandidateAngle =
(DesiredIndex + Candidate.DirectionOffset) & 255;
int FacingDiff = std::abs(CandidateAngle - CurrentFacingIndex);
if (FacingDiff > 128)
FacingDiff = 256 - FacingDiff;
Candidate.FacingDeviation = FacingDiff;
Candidate.SameTurnSide =
(std::abs(CurrentTurnSide) >= 4) &&
((CurrentTurnSide > 0 && Candidate.DirectionOffset > 0) ||
(CurrentTurnSide < 0 && Candidate.DirectionOffset < 0));
Candidate.Slowdown = SpeedOrdinal;
Candidates[CandidateCount++] = Candidate;
}
}

AvoidanceCandidate Stop;
Stop.CollisionTick = 1;
Candidates[CandidateCount++] = Stop;

if (Candidates.front().CollisionTick > 12)
return DesiredDirection;

if (sActiveNavigationStats)
{
for (size_t Index = 0; Index < CandidateCount; ++Index)
{
const auto& Candidate = Candidates[Index];
if (Candidate.CollisionTick <= 12)
++sActiveNavigationStats->CandidatesRejected;
}
}

const auto* MovingPlayer = MovingSnapshot
? MovingSnapshot->Player
: EM.GetComponent<PlayerComponent>(MovingID);
Core::ECS::EntityID PredictedBlockerID = -1;
PredictedCollisionTick(
MovingStart, MovingRect, MovingPriority, MovingID,
Candidates.front().Velocity, PredictiveNeighbors,
&PredictedBlockerID);
MovingPath->BlockedByEntityID = PredictedBlockerID;

// Request friendly idle/stopped units ahead to yield laterally and forward
for (const auto& Other : PredictiveNeighbors)
{
const auto OtherID = Other.EntityID;
const auto* OtherSnapshot = ActiveMovementSnapshot(OtherID);
auto* OtherPath = OtherSnapshot
? OtherSnapshot->Path
: EM.GetComponent<PathfindingComponent>(OtherID);
const auto* OtherPlayer = OtherSnapshot
? OtherSnapshot->Player
: EM.GetComponent<PlayerComponent>(OtherID);
const auto* OtherCommand = OtherSnapshot
? OtherSnapshot->Command
: EM.GetComponent<CommandComponent>(OtherID);

if (OtherID == MovingID || !OtherPath || !OtherPlayer ||
!MovingPlayer ||
OtherPlayer->PlayerID != MovingPlayer->PlayerID ||
OtherPath->CurrentVelocity.LengthSq() > 0.01f ||
MovementCommandPriority(OtherCommand) >= 4)
{
continue;
}

// Do not push an allied unit that is actively firing at an enemy in range
const auto* OtherCombat = OtherSnapshot
? OtherSnapshot->Combat
: EM.GetComponent<CombatComponent>(OtherID);
if (OtherCombat &&
(OtherCombat->PendingAttackFrame || OtherCombat->CooldownRemaining > 0.0f) &&
(OtherCombat->TargetEntityID >= 0 || OtherCombat->AutoTargetEntityID >= 0))
{
continue;
}

const int64_t ForwardDot =
((int64_t)Other.Start.X.raw_value - (int64_t)MovingStart.X.raw_value) *
Candidates.front().Velocity.X.raw_value +
((int64_t)Other.Start.Y.raw_value - (int64_t)MovingStart.Y.raw_value) *
Candidates.front().Velocity.Y.raw_value;
const bool OtherIsAhead = ForwardDot > 0;
if (!OtherIsAhead ||
SweptAabbCollisionTick(
MovingStart, Candidates.front().Velocity, MovingRect,
Other.Start, Other.Velocity, Other.Rect) > 12)
{
continue;
}

Core::Math::Vec2 AwayFromMover{
Other.Start.X.AsFloat() - MovingTransform->Position.X,
Other.Start.Y.AsFloat() - MovingTransform->Position.Y};
if (AwayFromMover.LengthSq() > 0.01f)
AwayFromMover = AwayFromMover.Normalized();
Core::Math::Vec2 Lateral{-DesiredDirection.Y, DesiredDirection.X};
if (AwayFromMover.Dot(Lateral) < 0.0f)
Lateral = Lateral * -1.0f;
OtherPath->FriendlyYieldVector =
OtherPath->FriendlyYieldVector + (AwayFromMover * 0.5f + Lateral * 0.85f);
}

// Hysteresis-aware candidate selection prevents left/right ping-pong oscillation
const auto Better = [](const AvoidanceCandidate& A,
const AvoidanceCandidate& B)
{
if (A.CollisionTick != B.CollisionTick)
return A.CollisionTick > B.CollisionTick;
if (A.SameTurnSide != B.SameTurnSide)
return A.SameTurnSide;
if (A.Progress != B.Progress)
return A.Progress > B.Progress;
if (A.FacingDeviation != B.FacingDeviation)
return A.FacingDeviation < B.FacingDeviation;
if (A.Deviation != B.Deviation)
return A.Deviation < B.Deviation;
if (A.Slowdown != B.Slowdown)
return A.Slowdown < B.Slowdown;
return A.Index < B.Index;
};

const AvoidanceCandidate* Best = &Candidates.front();
for (size_t Index = 0; Index < CandidateCount; ++Index)
{
const auto& Candidate = Candidates[Index];
if (Better(Candidate, *Best))
Best = &Candidate;
}

if (Best->SpeedPercent <= 0)
return {};
const float VelocityX = Best->Velocity.X.AsFloat();
const float VelocityY = Best->Velocity.Y.AsFloat();
const Core::Math::Vec2 Direction{VelocityX, VelocityY};
return Direction.LengthSq() > 0.0001f
? Direction.Normalized() : Core::Math::Vec2{};
}

static int TerrainClearanceForPath(const PathfindingComponent& Path)
{
return Path.CollisionRadius > 16 ? Path.CollisionRadius : 0;
}

static bool RequestMovementPath(
Core::ECS::EntityManager& EM,
Core::ECS::EntityID EntityID,
const Game::Map::GameMap* Map,
const Core::Math::Vec2& Target,
int IgnoreEntityID,
int AvoidDynamicEntityID)
{
if (const auto Callback = MovementSystem::GetPathRequestCallback())
{
if (sActiveNavigationStats)
++sActiveNavigationStats->RepathRequests;
return Callback(
EM,
EntityID,
Map,
Target,
IgnoreEntityID,
AvoidDynamicEntityID);
}

auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
if (!Transform || !Path)
return false;

Path->RequestedDestination = Target;
Path->RequestedIgnoreEntityID = IgnoreEntityID;
Path->Path.clear();
Path->PathIndex = 0;
Path->BlockedByEntityID = -1;
if (Path->IsAir || !Map)
{
Path->Path.push_back(Target);
return true;
}

const auto* AvoidTransform =
EM.GetComponent<TransformComponent>(AvoidDynamicEntityID);
const auto* AvoidPath =
EM.GetComponent<PathfindingComponent>(AvoidDynamicEntityID);
const float MovingRadius =
(float)std::max(4, CollisionRadiusFromBounds(*Path));
const std::function<bool(int, int)> ExtraBlockedTile =
[Map, AvoidTransform, AvoidPath, MovingRadius](int TileX, int TileY)
{
if (!AvoidTransform || !AvoidPath)
return false;
const Core::Math::Vec2 Center = Map->TileToWorld(TileX, TileY);
const float Left = AvoidPath->HasCollisionBounds
? (float)AvoidPath->CollisionLeft
: (float)AvoidPath->CollisionRadius;
const float Right = AvoidPath->HasCollisionBounds
? (float)AvoidPath->CollisionRight + 1.0f
: (float)AvoidPath->CollisionRadius;
const float Up = AvoidPath->HasCollisionBounds
? (float)AvoidPath->CollisionUp
: (float)AvoidPath->CollisionRadius;
const float Down = AvoidPath->HasCollisionBounds
? (float)AvoidPath->CollisionDown + 1.0f
: (float)AvoidPath->CollisionRadius;
constexpr float kExactCollisionRasterPadding = 16.0f;
return Center.X >= AvoidTransform->Position.X - Left - MovingRadius -
kExactCollisionRasterPadding &&
Center.X <= AvoidTransform->Position.X + Right + MovingRadius +
kExactCollisionRasterPadding &&
Center.Y >= AvoidTransform->Position.Y - Up - MovingRadius -
kExactCollisionRasterPadding &&
Center.Y <= AvoidTransform->Position.Y + Down + MovingRadius +
kExactCollisionRasterPadding;
};

auto Result = Game::Pathfinding::AStar::FindPath(
*Map,
Transform->Position,
Target,
2048,
TerrainClearanceForPath(*Path),
ExtraBlockedTile);

if (Result.Found)
{
Path->Path = std::move(Result.Waypoints);
Path->LastPathStatus = Result.Status;
Path->ResolvedPathDestination = Result.ResolvedDestination;
}
return !Path->Path.empty();
}

static Core::Math::Vec2 ResolveEdgeApproachTarget(Core::ECS::EntityManager& EM,
const Core::Math::Vec2& Current,
int TargetEntityID,
const Core::Math::Vec2& Fallback)
{
auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetEntityID);
auto* TargetPath = EM.GetComponent<PathfindingComponent>(TargetEntityID);
if (!TargetTrans || !TargetPath || !TargetPath->HasCollisionBounds)
return Fallback;

const Core::Math::Vec2 Delta = Current - TargetTrans->Position;
const float Left = (float)TargetPath->CollisionLeft;
const float Right = (float)TargetPath->CollisionRight;
const float Up = (float)TargetPath->CollisionUp;
const float Down = (float)TargetPath->CollisionDown;
const float ClampedX = std::clamp(Delta.X, -Left, Right);
const float ClampedY = std::clamp(Delta.Y, -Up, Down);
const bool HorizontalPrimary = std::fabs(Delta.X) >= std::fabs(Delta.Y);
Core::Math::Vec2 Result = TargetTrans->Position;
if (HorizontalPrimary)
{
Result.X += Delta.X < 0.0f ? -Left - 12.0f : Right + 12.0f;
Result.Y += ClampedY;
}
else
{
Result.X += ClampedX;
Result.Y += Delta.Y < 0.0f ? -Up - 12.0f : Down + 12.0f;
}
return Result;
}

static bool RefreshPathForCurrentRequest(Core::ECS::EntityManager& EM,
Core::ECS::EntityID EntityID,
const Game::Map::GameMap* Map,
bool UseEdgeTarget,
int AvoidDynamicEntityID = -1)
{
auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
if (!Path || !Trans)
return false;

Core::Math::Vec2 FinalDestination = Path->RequestedDestination;
const int RequestedTargetEntityID = Path->RequestedTargetEntityID;
const int RequestedIgnoreEntityID = Path->RequestedIgnoreEntityID;
int TargetEntityID = Path->RequestedTargetEntityID >= 0
? Path->RequestedTargetEntityID
: Path->RequestedIgnoreEntityID;
if (Path->RequestedTargetEntityID >= 0)
{
if (auto* TargetTrans = EM.GetComponent<TransformComponent>(Path->RequestedTargetEntityID))
{
FinalDestination = TargetTrans->Position;
if (auto* Command = EM.GetComponent<CommandComponent>(EntityID))
{
Command->TargetPosition = FinalDestination;
if (Command->CurrentOrderIndex >= 0 &&
Command->CurrentOrderIndex < (int)Command->Orders.size())
{
Command->Orders[Command->CurrentOrderIndex].TargetPosition =
FinalDestination;
}
}
}
}
Core::Math::Vec2 Target = FinalDestination;
if (UseEdgeTarget && TargetEntityID >= 0)
Target = ResolveEdgeApproachTarget(EM, Trans->Position, TargetEntityID, Target);

RequestMovementPath(
EM,
EntityID,
Map,
Target,
Path->RequestedIgnoreEntityID,
AvoidDynamicEntityID);

    if (auto* RefreshedPath = EM.GetComponent<PathfindingComponent>(EntityID))
    {
        RefreshedPath->RequestedDestination = FinalDestination;
        RefreshedPath->RequestedTargetEntityID = RequestedTargetEntityID;
        RefreshedPath->RequestedIgnoreEntityID = RequestedIgnoreEntityID;
        RefreshedPath->LastRepathTime = 0.0f;
        if (!UseEdgeTarget && !RefreshedPath->Path.empty() &&
            RefreshedPath->LastPathStatus == Game::Pathfinding::PathStatus::Partial &&
            IsActiveHarvester(EM, EntityID) &&
            RefreshedPath->ResolvedPathDestination.DistanceTo(FinalDestination) <= 24.0f)
        {
            if (RefreshedPath->Path.back().DistanceTo(FinalDestination) > 0.5f)
            {
                RefreshedPath->Path.push_back(FinalDestination);
            }
            RefreshedPath->ResolvedPathDestination = FinalDestination;
            RefreshedPath->NodeTolerance = std::min(RefreshedPath->NodeTolerance, 1.0f);
        }
        return !RefreshedPath->Path.empty();
    }
    return false;
}

static bool HandleMovementStall(Core::ECS::EntityManager& EM,
Core::ECS::EntityID EntityID,
PathfindingComponent& Path,
const Game::Map::GameMap* Map)
{
const auto RefreshAndRestartWaypointTracking =
[&](bool UseEdgeTarget, int AvoidEntityID = -1)
{
const bool Refreshed = RefreshPathForCurrentRequest(
EM, EntityID, Map, UseEdgeTarget, AvoidEntityID);
if (Refreshed)
{
Path.StallTimer = 0.0f;
Path.StallTicks = 0;
Path.HasLastWaypointDistance = false;
}
return Refreshed;
};

if (Path.RepathFailCount == 0)
{
const int ExactStaticBlockerID =
Path.BlockedByEntityID >= 0 &&
IsStaticGroundBlocker(EM, Path.BlockedByEntityID)
? Path.BlockedByEntityID
: -1;
Path.RepathFailCount = 1;
Path.LastRepathTime = 0.0f;
const bool Refreshed = RefreshAndRestartWaypointTracking(
false, ExactStaticBlockerID);
if (!Refreshed && ExactStaticBlockerID >= 0)
{
MarkMoveTargetFailed(Path);
Path.BlockedByEntityID = ExactStaticBlockerID;
Path.RepathFailCount = 0;
}
return Refreshed;
}

if (Path.RepathFailCount == 1)
{
Path.RepathFailCount = 2;
Path.LastRepathTime = 0.0f;
return RefreshAndRestartWaypointTracking(true);
}

if (Path.LastPathStatus == Game::Pathfinding::PathStatus::Partial &&
Path.RequestedIgnoreEntityID >= 0)
{
if (const auto* Transform = EM.GetComponent<TransformComponent>(EntityID))
{
Path.ResolvedPathDestination = Transform->Position;
}
Path.Path.clear();
Path.PathIndex = 0;
ClearMotion(Path);
Path.HasRecentMoveFailure = false;
Path.RepathFailCount = 0;
return false;
}

MarkMoveTargetFailed(Path);
Path.RepathFailCount = 0;
return false;
}

static bool IsTemporaryUnitBlocker(Core::ECS::EntityManager& EM,
const PathfindingComponent& Path)
{
if (Path.BlockedByEntityID < 0 ||
IsStaticGroundBlocker(EM, Path.BlockedByEntityID))
{
return false;
}
const auto* Unit = EM.GetComponent<UnitComponent>(Path.BlockedByEntityID);
const auto* OtherPath = EM.GetComponent<PathfindingComponent>(Path.BlockedByEntityID);
if (!Unit || Unit->HitPoints <= 0 || !OtherPath || OtherPath->IsAir)
return false;
return true;
}

// Shared step resolver for both waypoint-snap and full-step movement
static Core::Math::Vec2 ResolveStepWithCollisionsAndSliding(
Core::ECS::EntityManager& EM,
Core::ECS::EntityID ID,
const Game::Map::GameMap* Map,
TransformComponent& Trans,
PathfindingComponent& PathComp,
Core::Math::Vec2 Proposed,
const Core::Math::Vec2& MoveDir)
{
Proposed = ResolveStaticBlockers(
EM, ID, Proposed, MoveDir, PathComp.IsAir);
const auto* Snap = ActiveMovementSnapshot(ID);
const bool EscapingStaticOverlap =
!(Snap ? Snap->ActiveHarvester : IsActiveHarvester(EM, ID)) &&
IsEmbeddedInStaticBlocker(
EM, ID, PathComp.BlockedByEntityID, Trans.Position);
Proposed = ResolveMapWalkability(
Map, Trans.Position, Proposed, PathComp, PathComp.IsAir);
const Core::Math::Vec2 AfterFirstWalk = Proposed;
if (!EscapingStaticOverlap)
Proposed = PreventBackwardStep(Trans.Position, Proposed, MoveDir);
const Core::Math::Vec2 AfterSecondBlocker = ResolveStaticBlockers(
EM, ID, Proposed, MoveDir, PathComp.IsAir);
if (AfterSecondBlocker.X != AfterFirstWalk.X ||
AfterSecondBlocker.Y != AfterFirstWalk.Y)
{
Proposed = ResolveMapWalkability(
Map, Trans.Position, AfterSecondBlocker, PathComp, PathComp.IsAir);
}
else
{
Proposed = AfterSecondBlocker;
}

if (const auto BlockerID = FindDynamicUnitCollision(
EM, ID, Trans.Position, Proposed);
BlockerID >= 0)
{
PathComp.BlockedByEntityID = BlockerID;
bool Slid = false;
const bool PreferX =
std::fabs(Proposed.X - Trans.Position.X) >=
std::fabs(Proposed.Y - Trans.Position.Y);
const Core::Math::Vec2 PrimarySlide = PreferX
? Core::Math::Vec2{Proposed.X, Trans.Position.Y}
: Core::Math::Vec2{Trans.Position.X, Proposed.Y};
const Core::Math::Vec2 SecondarySlide = PreferX
? Core::Math::Vec2{Trans.Position.X, Proposed.Y}
: Core::Math::Vec2{Proposed.X, Trans.Position.Y};

for (const auto& Cand : {PrimarySlide, SecondarySlide})
{
if ((Cand - Trans.Position).LengthSq() < 0.001f)
continue;
Core::Math::Vec2 SlideCand = ResolveMapWalkability(
Map, Trans.Position, Cand, PathComp, PathComp.IsAir);
const Core::Math::Vec2 AfterSlideWalk = SlideCand;
if (!EscapingStaticOverlap)
SlideCand = PreventBackwardStep(Trans.Position, SlideCand, MoveDir);
const Core::Math::Vec2 AfterSlideBlocker = ResolveStaticBlockers(
EM, ID, SlideCand, MoveDir, PathComp.IsAir);
if (AfterSlideBlocker.X != AfterSlideWalk.X ||
AfterSlideBlocker.Y != AfterSlideWalk.Y)
{
SlideCand = ResolveMapWalkability(
Map, Trans.Position, AfterSlideBlocker, PathComp, PathComp.IsAir);
}
else
{
SlideCand = AfterSlideBlocker;
}

if ((SlideCand - Trans.Position).LengthSq() < 0.001f)
continue;

if (FindDynamicUnitCollision(EM, ID, Trans.Position, SlideCand) < 0)
{
Proposed = SlideCand;
Slid = true;
break;
}
}

if (!Slid)
{
Proposed = Trans.Position;
PathComp.CurrentSpeed = 0.0f;
}
}
return Proposed;
}

} // anonymous namespace

void MovementSystem::Update(float DeltaTime)
{
if (DeltaTime <= 0.0f) return;

static float sElapsedSimulationTime = 0.0f;
sElapsedSimulationTime += DeltaTime;
MaybePrintStallTelemetryReport(sElapsedSimulationTime);

auto& EM = mSystemMgr.GetEntityManager();
if (auto* SpatialRes = EM.Context().GetResource<Game::Pathfinding::SpatialIndexResource>())
    SpatialRes->MarkPositionsDirty();
mLastNavigationStats = {};
sActiveNavigationStats = &mLastNavigationStats;

if (const auto SyncNavigation = MovementSystem::GetNavigationSyncCallback())
{
SyncNavigation(EM, mMap);
}

const auto& Entities = EM.GetEntitiesWithCached<TransformComponent, PathfindingComponent>();
const auto& UnitEntities = EM.GetEntitiesWithCached<TransformComponent, UnitComponent>();

static thread_local Game::Pathfinding::NavigationSpatialHash SpatialHash;
static thread_local Game::Pathfinding::NavigationSpatialHash StaticSpatialHash;
SpatialHash.Clear();
StaticSpatialHash.Clear();

static thread_local std::vector<MovementEntitySnapshot> Snapshots;
static thread_local uint32_t SnapshotGeneration = 0;
++SnapshotGeneration;
if (SnapshotGeneration == 0)
{
for (auto& Snapshot : Snapshots)
Snapshot.Generation = 0;
++SnapshotGeneration;
}

const size_t RequiredSize = EM.GetEntityCapacity();
if (Snapshots.size() < RequiredSize)
Snapshots.resize(RequiredSize);

Core::Concurrency::JobSystem::Instance().ParallelForRange(
    static_cast<uint32_t>(UnitEntities.size()),
    1024u,
    [&](uint32_t BeginIdx, uint32_t EndIdx, uint32_t /*ThreadIdx*/)
    {
        for (uint32_t I = BeginIdx; I < EndIdx; ++I)
        {
            const auto ID = UnitEntities[I];
            if (ID < 0 || (size_t)ID >= Snapshots.size())
                continue;
            auto& Snapshot = Snapshots[(size_t)ID];
            Snapshot = {};
            Snapshot.Transform = EM.GetComponent<TransformComponent>(ID);
            Snapshot.Unit = EM.GetComponent<UnitComponent>(ID);
            Snapshot.Path = EM.GetComponent<PathfindingComponent>(ID);
            Snapshot.Command = EM.GetComponent<CommandComponent>(ID);
            Snapshot.Player = EM.GetComponent<PlayerComponent>(ID);
            Snapshot.Combat = EM.GetComponent<CombatComponent>(ID);
            Snapshot.Harvest = EM.GetComponent<HarvestComponent>(ID);
            Snapshot.Status = EM.GetComponent<StatusEffectComponent>(ID);
            const auto* Building = EM.GetComponent<BuildingComponent>(ID);
            const auto* Resource = EM.GetComponent<ResourceComponent>(ID);
            const auto* Burrow = EM.GetComponent<BurrowComponent>(ID);
            Snapshot.StaticGroundBlocker = Building
                ? (!Building->IsLifted || Building->FlightPhase == BuildingFlightPhase::Landing)
                : (Resource != nullptr);
            Snapshot.ActiveHarvester =
                Snapshot.Harvest && Snapshot.Harvest->State != HarvestState::Idle;
            Snapshot.AssignedConstructionWorker = IsAssignedConstructionWorker(EM, ID);
            Snapshot.CommandPriority = MovementCommandPriority(Snapshot.Command);
            Snapshot.IsAir = (Building && Building->IsLifted) ||
                (Snapshot.Unit && Game::Data::IsAirUnitType(Snapshot.Unit->ChkUnitID));
            Snapshot.CanMove = Snapshot.Transform && Snapshot.Unit &&
                Snapshot.Unit->HitPoints > 0 && !Resource &&
                (!Building || (Building->IsLifted && Building->FlightPhase == BuildingFlightPhase::Airborne)) &&
                !IsSiegedTankUnit(Snapshot.Unit->ChkUnitID) &&
                (!Burrow || (!Burrow->IsBurrowed && Burrow->Phase == BurrowPhase::Surface)) &&
                Snapshot.Unit->Speed > 0.0f;
            if (Snapshot.Path)
                Snapshot.CollisionRect = DynamicCollisionRectForPath(*Snapshot.Path);
            Snapshot.Generation = SnapshotGeneration;
        }
    });

for (const auto ID : UnitEntities)
{
    if (ID < 0 || (size_t)ID >= Snapshots.size())
        continue;
    const auto& Snapshot = Snapshots[(size_t)ID];
    if (!Snapshot.Transform)
        continue;
    if (Snapshot.StaticGroundBlocker)
        StaticSpatialHash.Insert(ID, Snapshot.Transform->Position);
    else
        SpatialHash.Insert(ID, Snapshot.Transform->Position);
}
StaticSpatialHash.FinalizeCSR();
SpatialHash.FinalizeCSR();

struct SpatialHashScope
{
explicit SpatialHashScope(
const Game::Pathfinding::NavigationSpatialHash& Hash,
const Game::Pathfinding::NavigationSpatialHash& StaticHash,
const std::vector<MovementEntitySnapshot>& EntitySnapshots,
uint32_t SnapshotGen)
{
sActiveSpatialHash = &Hash;
sActiveStaticSpatialHash = &StaticHash;
sActiveMovementSnapshots = &EntitySnapshots;
sActiveMovementSnapshotGeneration = SnapshotGen;
}
~SpatialHashScope()
{
sActiveSpatialHash = nullptr;
sActiveStaticSpatialHash = nullptr;
sActiveMovementSnapshots = nullptr;
sActiveMovementSnapshotGeneration = 0;
sLastPredictiveQuery = nullptr;
sLastPredictiveQueryEntityID = -1;
sActiveNavigationStats = nullptr;
}
} ActiveSpatialHash(
SpatialHash, StaticSpatialHash, Snapshots, SnapshotGeneration);

float Dt = std::min(DeltaTime, 0.1f);

// --- Initialize movement fields ---
for (auto ID : Entities)
{
const auto* Snap = ActiveMovementSnapshot(ID);
auto* PathComp = Snap ? Snap->Path : EM.GetComponent<PathfindingComponent>(ID);
auto* Unit = Snap ? Snap->Unit : EM.GetComponent<UnitComponent>(ID);
auto* Trans = Snap ? Snap->Transform : EM.GetComponent<TransformComponent>(ID);
auto* Cmd = Snap ? Snap->Command : EM.GetComponent<CommandComponent>(ID);
if (!PathComp) continue;

if (Trans && !PathComp->HasFixedPosition)
{
PathComp->FixedPosition =
Core::Math::FixedVec2(Trans->Position.X, Trans->Position.Y);
PathComp->FixedVelocity = Core::Math::FixedVec2::Zero();
PathComp->HasFixedPosition = true;
}

if (!Snap && EM.GetComponent<BulletComponent>(ID))
continue;

if (PathComp->HasRecentMoveFailure)
{
if (PathComp->RecentMoveFailureTicks <= 0 &&
PathComp->RecentMoveFailureCooldown > 0.0f)
{
PathComp->RecentMoveFailureTicks = std::max(
1, (int)std::ceil(PathComp->RecentMoveFailureCooldown * 24.0f));
}
PathComp->RecentMoveFailureTicks =
std::max(0, PathComp->RecentMoveFailureTicks - 1);
PathComp->RecentMoveFailureCooldown =
(float)PathComp->RecentMoveFailureTicks / 24.0f;
if (PathComp->RecentMoveFailureTicks <= 0)
PathComp->HasRecentMoveFailure = false;
}

if (Unit)
PathComp->IsAir = Snap ? Snap->IsAir : ShouldMoveAsAir(EM, ID);

if (!(Snap ? Snap->CanMove : CanMoveEntity(EM, ID)))
{
ClearMotion(*PathComp);
continue;
}

const bool HasMovementSpeedIntent =
ShouldHaveMovementSpeed(EM, ID, Trans, Cmd, *PathComp);
if (!HasMovementSpeedIntent)
{
PathComp->TargetSpeed = 0.0f;
PathComp->CurrentSpeed = 0.0f;
PathComp->CurrentVelocity = {0.0f, 0.0f};
continue;
}

if (PathComp->TargetSpeed <= 0.0f && Unit)
{
PathComp->TargetSpeed = Tuning::ToWorldMoveSpeed(Unit->Speed);
PathComp->Acceleration = 200.0f;
PathComp->TurnRate = 8.0f;
if (PathComp->HasCollisionBounds)
PathComp->CollisionRadius = CollisionRadiusFromBounds(*PathComp);
else
{
PathComp->CollisionRadius = 12;
if (Unit->SizeType == UnitSize::Large)
PathComp->CollisionRadius = 20;
}
}
else if (PathComp->TargetSpeed <= 0.0f)
{
PathComp->TargetSpeed = Tuning::ToWorldMoveSpeed(2.0f);
PathComp->Acceleration = 200.0f;
PathComp->TurnRate = 6.0f;
PathComp->CollisionRadius = PathComp->HasCollisionBounds ?
CollisionRadiusFromBounds(*PathComp) : 16;
}
}

// --- Movement & Tactical Steering Update ---
for (auto ID : Entities)
{
const auto* Snap = ActiveMovementSnapshot(ID);
auto* Trans = Snap ? Snap->Transform : EM.GetComponent<TransformComponent>(ID);
auto* PathComp = Snap ? Snap->Path : EM.GetComponent<PathfindingComponent>(ID);
auto* Cmd = Snap ? Snap->Command : EM.GetComponent<CommandComponent>(ID);
if (!Trans || !PathComp) continue;
if (!Snap && EM.GetComponent<BulletComponent>(ID)) continue;

if (Cmd && Cmd->CurrentCommand != CommandType::Patrol)
Cmd->HasPatrolAnchor = false;

const Core::Math::Vec2 FriendlyYield = PathComp->FriendlyYieldVector;
PathComp->FriendlyYieldVector = {0.0f, 0.0f};
auto& Path = PathComp->Path;

const auto* HarvestComp = Snap ? Snap->Harvest : EM.GetComponent<HarvestComponent>(ID);
const bool IsActiveHarvesting =
HarvestComp && HarvestComp->State != HarvestState::Idle;
const bool IsBuildOrRepairOrder =
Cmd && (Cmd->CurrentCommand == CommandType::Build ||
Cmd->CurrentCommand == CommandType::Repair ||
Cmd->CurrentCommand == CommandType::Harvest);
const bool IsTargetingIgnoredEntity =
PathComp->RequestedTargetEntityID == PathComp->RequestedIgnoreEntityID ||
(Cmd && Cmd->TargetEntityID == PathComp->RequestedIgnoreEntityID);

if (!IsActiveHarvesting && !IsBuildOrRepairOrder && !IsTargetingIgnoredEntity &&
PathComp->RequestedIgnoreEntityID >= 0 &&
!IsEmbeddedInStaticBlocker(EM, ID, PathComp->RequestedIgnoreEntityID, Trans->Position))
{
const auto CheckFootprint = NavFootprintForPath(*PathComp);
if (!mMap || mMap->GetNavigationGrid().CanOccupyWorld(
Trans->Position, CheckFootprint, -1, -1))
{
PathComp->RequestedIgnoreEntityID = -1;
PathComp->RequestedAdditionalIgnoreEntityID = -1;
}
}

if (!(Snap ? Snap->CanMove : CanMoveEntity(EM, ID)))
{
ClearMotion(*PathComp);
continue;
}

PathComp->LastRepathTime += Dt;

auto* Status = Snap ? Snap->Status : EM.GetComponent<StatusEffectComponent>(ID);
if (Status && (Status->HasEffect(StatusEffect::Stasis) ||
Status->HasEffect(StatusEffect::Lockdown) ||
Status->HasEffect(StatusEffect::Maelstrom)))
{
PathComp->CurrentSpeed = 0.0f;
PathComp->CurrentVelocity = {0.0f, 0.0f};
continue;
}

if (FriendlyYield.LengthSq() <= 0.001f)
    PathComp->HasFriendlyYieldAnchor = false;

// Cascading Friendly Yield
if (FriendlyYield.LengthSq() > 0.001f &&
PathComp->CurrentVelocity.LengthSq() <= 0.01f &&
(!Cmd || Cmd->CurrentCommand != CommandType::HoldPosition))
{
const auto* Unit = EM.GetComponent<UnitComponent>(ID);
const float BaseSpd = PathComp->TargetSpeed > 0.0f
? PathComp->TargetSpeed
: (Unit ? Tuning::ToWorldMoveSpeed(Unit->Speed) : 64.0f);
constexpr float kFriendlyYieldMaxDistance = 24.0f;
const float kFriendlyYieldSpeed = std::min(24.0f, std::max(16.0f, BaseSpd * 0.4f));

if (!PathComp->HasFriendlyYieldAnchor)
{
    PathComp->FriendlyYieldAnchor = Trans->Position;
    PathComp->HasFriendlyYieldAnchor = true;
}

const Core::Math::Vec2 YieldDir = FriendlyYield.Normalized();
Core::Math::Vec2 Yielded =
Trans->Position + YieldDir * (kFriendlyYieldSpeed * Dt);

const Core::Math::Vec2 FromAnchor = Yielded - PathComp->FriendlyYieldAnchor;
if (FromAnchor.Length() > kFriendlyYieldMaxDistance)
{
    Yielded = PathComp->FriendlyYieldAnchor +
        FromAnchor.Normalized() * kFriendlyYieldMaxDistance;
}

Yielded = ResolveStaticBlockers(EM, ID, Yielded, YieldDir, PathComp->IsAir);
Yielded = ResolveMapWalkability(
mMap, Trans->Position, Yielded,
*PathComp, PathComp->IsAir);

const Core::ECS::EntityID YieldBlockerID =
FindDynamicUnitCollision(EM, ID, Trans->Position, Yielded);
if (YieldBlockerID < 0)
{
CommitAuthoritativePosition(
*Trans, *PathComp, Yielded,
Core::Math::Vec2{0.0f, 0.0f});
}
else
{
// Propagate the yield push to the next stationary friendly unit in the choke
const auto* MyPlayer = EM.GetComponent<PlayerComponent>(ID);
const auto* BlockerPlayer = EM.GetComponent<PlayerComponent>(YieldBlockerID);
auto* BlockerPath = EM.GetComponent<PathfindingComponent>(YieldBlockerID);
const auto* BlockerCmd = EM.GetComponent<CommandComponent>(YieldBlockerID);
if (MyPlayer && BlockerPlayer && BlockerPath &&
MyPlayer->PlayerID == BlockerPlayer->PlayerID &&
BlockerPath->CurrentVelocity.LengthSq() <= 0.01f &&
MovementCommandPriority(BlockerCmd) < 4)
{
BlockerPath->FriendlyYieldVector =
BlockerPath->FriendlyYieldVector + YieldDir * 0.85f;
}

const Core::Math::Vec2 SlideX = ResolveMapWalkability(
mMap, Trans->Position, {Yielded.X, Trans->Position.Y},
*PathComp, PathComp->IsAir);
const Core::Math::Vec2 SlideY = ResolveMapWalkability(
mMap, Trans->Position, {Trans->Position.X, Yielded.Y},
*PathComp, PathComp->IsAir);
if ((SlideX - Trans->Position).LengthSq() >= 0.001f &&
FindDynamicUnitCollision(EM, ID, Trans->Position, SlideX) < 0)
{
CommitAuthoritativePosition(
*Trans, *PathComp, SlideX,
Core::Math::Vec2{0.0f, 0.0f});
}
else if ((SlideY - Trans->Position).LengthSq() >= 0.001f &&
FindDynamicUnitCollision(EM, ID, Trans->Position, SlideY) < 0)
{
CommitAuthoritativePosition(
*Trans, *PathComp, SlideY,
Core::Math::Vec2{0.0f, 0.0f});
}
}
}

if (Cmd && Cmd->CurrentCommand == CommandType::HoldPosition)
{
ClearMotion(*PathComp);
continue;
}

if (Cmd && Cmd->CurrentCommand == CommandType::Stop)
{
const auto* CombatComp = EM.GetComponent<CombatComponent>(ID);
const bool InCombatChase = CombatComp &&
(CombatComp->TargetEntityID >= 0 || CombatComp->AutoTargetEntityID >= 0);
if (!InCombatChase || Path.empty())
{
ClearMotion(*PathComp);
continue;
}
}

// Follow: re-issue move toward target if path is empty
if (Cmd && Cmd->CurrentCommand == CommandType::Follow && Path.empty())
{
if (Cmd->TargetEntityID >= 0)
{
if (auto* TTrans = EM.GetComponent<TransformComponent>(Cmd->TargetEntityID))
{
if (Trans->Position.DistanceTo(TTrans->Position) > 32.0f)
{
Cmd->TargetPosition = TTrans->Position;
PathComp->RequestedDestination = TTrans->Position;
RefreshPathForCurrentRequest(EM, ID, mMap, false);
}
}
}
continue;
}

if (Path.empty())
{
if (Cmd &&
(Cmd->CurrentCommand == CommandType::Move ||
Cmd->CurrentCommand == CommandType::AttackMove ||
Cmd->CurrentCommand == CommandType::Attack ||
Cmd->CurrentCommand == CommandType::Follow ||
Cmd->CurrentCommand == CommandType::Harvest) &&
!IsSatisfiedInteractionPartial(Trans, *PathComp) &&
!RecentlyFailedCurrentRequest(*PathComp) &&
PathComp->LastRepathTime >= 0.45f &&
Trans->Position.DistanceTo(PathComp->RequestedDestination) >
std::max(PathComp->NodeTolerance, (float)CollisionRadiusFromBounds(*PathComp)))
{
if (RefreshPathForCurrentRequest(EM, ID, mMap, false))
continue;
MarkMoveTargetFailed(*PathComp);
if (Cmd && Cmd->HasPendingOrders() &&
Cmd->CurrentCommand != CommandType::Build &&
Cmd->CurrentCommand != CommandType::Harvest)
{
CommandSystem::AdvanceQueue(EM, ID);
}
continue;
}

if (Cmd && (Cmd->CurrentCommand == CommandType::Repair ||
Cmd->CurrentCommand == CommandType::Build ||
Cmd->CurrentCommand == CommandType::Harvest))
{
ClearMotion(*PathComp);
continue;
}

if (Cmd && Cmd->CurrentCommand == CommandType::Patrol)
{
const Core::Math::Vec2 OldTarget = Cmd->TargetPosition;
const Core::Math::Vec2 NewTarget =
Cmd->HasPatrolAnchor ? Cmd->PatrolAnchor : Trans->Position;
if (NewTarget.DistanceTo(OldTarget) > 1.0f &&
PathComp->LastRepathTime >= 0.5f)
{
PathComp->RequestedDestination = NewTarget;
PathComp->RequestedTargetEntityID = -1;
PathComp->RequestedIgnoreEntityID = -1;
if (RefreshPathForCurrentRequest(EM, ID, mMap, false))
{
Cmd->TargetPosition = NewTarget;
Cmd->PatrolAnchor = OldTarget;
Cmd->HasPatrolAnchor = true;
if (Cmd->CurrentOrderIndex >= 0 &&
Cmd->CurrentOrderIndex < (int)Cmd->Orders.size())
{
Cmd->Orders[Cmd->CurrentOrderIndex].TargetPosition =
NewTarget;
}
continue;
}
PathComp->RequestedDestination = OldTarget;
PathComp->LastRepathTime = 0.0f;
}
PathComp->TargetSpeed = 0.0f;
PathComp->CurrentSpeed = 0.0f;
PathComp->CurrentVelocity = {0.0f, 0.0f};
}
else
{
if (Cmd && Cmd->HasPendingOrders() &&
Cmd->CurrentCommand != CommandType::Harvest)
CommandSystem::AdvanceQueue(EM, ID);
PathComp->TargetSpeed = 0.0f;
PathComp->CurrentSpeed = 0.0f;
PathComp->CurrentVelocity = {0.0f, 0.0f};
}
continue;
}

if (Cmd && Cmd->CurrentCommand == CommandType::Patrol &&
!Cmd->HasPatrolAnchor)
{
Cmd->PatrolAnchor = Trans->Position;
Cmd->HasPatrolAnchor = true;
}

Core::Math::Vec2 MoveDir;

// Follow next waypoint
while (PathComp->PathIndex < (int)Path.size())
{
Core::Math::Vec2& Waypoint = Path[PathComp->PathIndex];
Core::Math::Vec2 ToTarget = {Waypoint.X - Trans->Position.X,
Waypoint.Y - Trans->Position.Y};
float Dist = ToTarget.Length();
const float MovingRadius = (float)CollisionRadiusFromBounds(*PathComp);
const bool IsFinalWaypoint = PathComp->PathIndex >= (int)Path.size() - 1;
const bool IsCombatMove = HasActiveCombatPursuit(EM, ID, Cmd);
const bool PreciseInteractionEndpoint =
Cmd &&
(Cmd->CurrentCommand == CommandType::Harvest ||
Cmd->CurrentCommand == CommandType::Build ||
Cmd->CurrentCommand == CommandType::Repair ||
IsCombatMove);

// Squad Marching Cornering:
const float IntermediateTolerance =
std::max(PathComp->NodeTolerance, MovingRadius * 1.35f);
bool PassedIntermediateWaypoint = false;
if (!IsFinalWaypoint && Dist <= MovingRadius * 3.0f && mMap)
{
const Core::Math::Vec2& NextWaypoint = Path[PathComp->PathIndex + 1];
const Core::Math::Vec2& SegDir = NextWaypoint - Waypoint;
const Core::Math::Vec2 FromWaypoint = Trans->Position - Waypoint;
if (SegDir.LengthSq() > 1.0f && FromWaypoint.Dot(SegDir) >= 0.0f)
{
if (PathComp->IsAir ||
(mMap->GetNavigationGrid().HasLineOfSightWorld(
Trans->Position, NextWaypoint,
NavFootprintForPath(*PathComp),
PathComp->RequestedIgnoreEntityID,
PathComp->RequestedAdditionalIgnoreEntityID) &&
HasClearStaticBlockerSegment(EM, ID, Trans->Position,
NextWaypoint, *PathComp)))
{
PassedIntermediateWaypoint = true;
}
}
}

const float ArrivalTolerance = IsFinalWaypoint
? (IsCombatMove
? std::max(8.0f, PathComp->NodeTolerance)
: (PreciseInteractionEndpoint
? PathComp->NodeTolerance
: std::max(PathComp->NodeTolerance, MovingRadius)))
: IntermediateTolerance;

const bool ArrivedCluster = IsFinalWaypoint && !PreciseInteractionEndpoint &&
IsArrivedAtDestinationCluster(EM, ID, Trans, *PathComp, Cmd, Waypoint);

bool ShouldAdvanceWaypoint = PassedIntermediateWaypoint || ArrivedCluster;
if (!ShouldAdvanceWaypoint && Dist < ArrivalTolerance)
{
if (IsFinalWaypoint)
{
ShouldAdvanceWaypoint = true;
}
else if (Dist <= PathComp->NodeTolerance)
{
ShouldAdvanceWaypoint = true;
}
else
{
const Core::Math::Vec2& NextWaypoint = Path[PathComp->PathIndex + 1];
if (PathComp->IsAir || !mMap ||
(mMap->GetNavigationGrid().HasLineOfSightWorld(
Trans->Position, NextWaypoint,
NavFootprintForPath(*PathComp),
PathComp->RequestedIgnoreEntityID,
PathComp->RequestedAdditionalIgnoreEntityID) &&
HasClearStaticBlockerSegment(EM, ID, Trans->Position,
NextWaypoint, *PathComp)))
{
ShouldAdvanceWaypoint = true;
}
}
}

if (ShouldAdvanceWaypoint)
{
PathComp->PathIndex++;
PathComp->StallTimer = 0.0f;
PathComp->StallTicks = 0;
PathComp->HasLastWaypointDistance = false;
continue;
}

// Speed management with acceleration
const float TargetSpd =
PathComp->TargetSpeed *
MovementSpeedMultiplier(Status) *
UpgradeSystem::GetMovementSpeedMultiplier(EM, ID);
float CurSpd = PathComp->CurrentSpeed;
if (CurSpd < TargetSpd)
CurSpd = std::min(TargetSpd, CurSpd + PathComp->Acceleration * Dt);
else if (CurSpd > TargetSpd)
CurSpd = std::max(TargetSpd, CurSpd - PathComp->Acceleration * Dt * 2.0f);
PathComp->CurrentSpeed = CurSpd;

MoveDir = ToTarget.Normalized();
PathComp->BlockedByEntityID = -1;

const Core::Math::Vec2 ActualMove =
SelectPredictiveAvoidanceDirection(
EM, ID, MoveDir, CurSpd, mMap);

const float OldFacingAngle = PathComp->FacingAngle;
if (ActualMove.LengthSq() >= 0.0001f)
{
float DesiredAngle = atan2f(ActualMove.Y, ActualMove.X);
float Current = PathComp->FacingAngle;
float Diff = DesiredAngle - Current;
while (Diff > 3.14159265359f) Diff -= 6.28318530718f;
while (Diff < -3.14159265359f) Diff += 6.28318530718f;
float MaxTurn = PathComp->TurnRate * Dt;
if (std::fabs(Diff) <= MaxTurn)
PathComp->FacingAngle = DesiredAngle;
else
PathComp->FacingAngle += (Diff > 0.0f ? MaxTurn : -MaxTurn);
}

MoveDir = ActualMove;
const float MoveAmount = CurSpd * Dt;
const float OldDistance = Dist;
const Core::Math::Vec2 RawTarget = (MoveAmount > Dist)
? Waypoint
: (Trans->Position + MoveDir * MoveAmount);

const Core::Math::Vec2 Proposed = ResolveStepWithCollisionsAndSliding(
EM, ID, mMap, *Trans, *PathComp, RawTarget, MoveDir);

if (ID == DebugMovementEntity())
{
std::cerr << "MV ent=" << ID
<< " pos=" << Trans->Position.X << "," << Trans->Position.Y
<< " movedir=" << MoveDir.X << "," << MoveDir.Y
<< " prop=" << Proposed.X << "," << Proposed.Y
<< " curspd=" << PathComp->CurrentSpeed
<< " dist=" << Dist
<< " wp=" << Waypoint.X << "," << Waypoint.Y
<< " blk=" << PathComp->BlockedByEntityID
<< " stallticks=" << PathComp->StallTicks
<< std::endl;
}

const Core::Math::Vec2 AppliedMove = Proposed - Trans->Position;
const float EffectiveSpd = (MoveAmount > Dist) ? TargetSpd : PathComp->CurrentSpeed;
CommitAuthoritativePosition(
*Trans, *PathComp, Proposed,
AppliedMove.Length() > 0.001f
? AppliedMove.Normalized() * EffectiveSpd
: Core::Math::Vec2{});

const float AppliedDistance = AppliedMove.Length();
if (AppliedDistance <= 0.05f)
PathComp->FacingAngle = OldFacingAngle;

const float NewDistance = (Waypoint - Trans->Position).Length();
if (!PathComp->IsAir &&
(!TrackMovementProgress(*PathComp, OldDistance, NewDistance, AppliedDistance, Dt) ||
!TrackDestinationProgress(*PathComp, *Trans, AppliedDistance, Dt)))
{
LogMovementStall(EM, ID, *Trans, *PathComp,
(MoveAmount > Dist) ? "waypoint" : "segment", mMap);
if (PathComp->LastRepathTime >= 0.45f)
{
PathComp->LastRepathTime = 0.0f;
if (IsTemporaryUnitBlocker(EM, *PathComp))
{
++PathComp->RepathFailCount;
PathComp->StallTimer = 0.0f;
PathComp->StallTicks = 0;
PathComp->HasLastWaypointDistance = false;
const Core::Math::Vec2 EffectiveDest =
(PathComp->LastPathStatus == Game::Pathfinding::PathStatus::Partial)
? PathComp->ResolvedPathDestination
: PathComp->RequestedDestination;
if (PathComp->RepathFailCount >= 2 &&
IsArrivedAtDestinationCluster(
EM, ID, Trans, *PathComp, Cmd, EffectiveDest))
{
if (Trans)
{
PathComp->LastPathStatus = Game::Pathfinding::PathStatus::Partial;
PathComp->ResolvedPathDestination = Trans->Position;
}
ClearMotion(*PathComp);
if (Cmd && Cmd->HasPendingOrders() &&
Cmd->CurrentCommand != CommandType::Build &&
Cmd->CurrentCommand != CommandType::Harvest)
{
CommandSystem::AdvanceQueue(EM, ID);
}
break;
}
if (PathComp->RepathFailCount <= 2 && mMap)
{
RefreshPathForCurrentRequest(
EM, ID, mMap, false, PathComp->BlockedByEntityID);
break;
}
if (PathComp->RepathFailCount >= 3)
{
const auto* BlockerPath =
EM.GetComponent<PathfindingComponent>(PathComp->BlockedByEntityID);
const bool BlockerActivelyMoving =
BlockerPath &&
(BlockerPath->CurrentSpeed > 1.0f || !BlockerPath->Path.empty());
if (!BlockerActivelyMoving)
{
MarkMoveTargetFailed(*PathComp);
PathComp->RepathFailCount = 0;
}
else
{
PathComp->StallTimer = 0.0f;
}
break;
}
}
else if (!HandleMovementStall(EM, ID, *PathComp, mMap))
{
if (Cmd && Cmd->HasPendingOrders() &&
Cmd->CurrentCommand != CommandType::Build &&
Cmd->CurrentCommand != CommandType::Harvest)
{
CommandSystem::AdvanceQueue(EM, ID);
}
break;
}
}
break;
}

if (MoveAmount > Dist && NewDistance < ArrivalTolerance)
{
PathComp->PathIndex++;
PathComp->StallTimer = 0.0f;
PathComp->StallTicks = 0;
PathComp->HasLastWaypointDistance = false;
}

PathComp->CurrentVelocity = AppliedMove.Length() > 0.001f
? AppliedMove.Normalized() * EffectiveSpd
: Core::Math::Vec2{};
break;
}

if (PathComp->PathIndex >= (int)Path.size())
{
const bool IsPartial =
PathComp->LastPathStatus == Game::Pathfinding::PathStatus::Partial;
const Core::Math::Vec2 EffectiveDest = IsPartial
? PathComp->ResolvedPathDestination
: PathComp->RequestedDestination;
const bool ArrivedCluster = IsArrivedAtDestinationCluster(
EM, ID, Trans, *PathComp, Cmd, EffectiveDest);
if (ArrivedCluster && Trans)
{
PathComp->LastPathStatus = Game::Pathfinding::PathStatus::Partial;
PathComp->ResolvedPathDestination = Trans->Position;
}
const auto* Harvest = EM.GetComponent<HarvestComponent>(ID);
const bool HarvestAllowsContinuation =
!Harvest ||
(!Harvest->InsideGasBuilding &&
!Harvest->QueuedAtResource &&
(Harvest->State == HarvestState::MovingToResource ||
Harvest->State == HarvestState::MovingToDepot));
const bool NeedsContinuation =
!ArrivedCluster &&
HarvestAllowsContinuation &&
!IsSatisfiedInteractionPartial(Trans, *PathComp) &&
Trans->Position.DistanceTo(EffectiveDest) >
std::max(PathComp->NodeTolerance, (float)CollisionRadiusFromBounds(*PathComp));

const int FailedStaticBlockerID =
PathComp->BlockedByEntityID >= 0 &&
IsStaticGroundBlocker(EM, PathComp->BlockedByEntityID)
? PathComp->BlockedByEntityID
: -1;
ClearMotion(*PathComp);
if (FailedStaticBlockerID >= 0)
PathComp->BlockedByEntityID = FailedStaticBlockerID;

if (NeedsContinuation &&
Cmd &&
(Cmd->CurrentCommand == CommandType::Move ||
Cmd->CurrentCommand == CommandType::AttackMove ||
Cmd->CurrentCommand == CommandType::Attack ||
Cmd->CurrentCommand == CommandType::Follow ||
Cmd->CurrentCommand == CommandType::Harvest) &&
!RecentlyFailedCurrentRequest(*PathComp))
{
if (PathComp->LastRepathTime >= 0.45f)
{
if (!RefreshPathForCurrentRequest(EM, ID, mMap, false))
{
MarkMoveTargetFailed(*PathComp);
if (Cmd->HasPendingOrders() &&
Cmd->CurrentCommand != CommandType::Build &&
Cmd->CurrentCommand != CommandType::Harvest)
{
CommandSystem::AdvanceQueue(EM, ID);
}
}
}
}
else if (Cmd &&
(Cmd->CurrentCommand == CommandType::Repair ||
Cmd->CurrentCommand == CommandType::Build ||
Cmd->CurrentCommand == CommandType::Harvest))
{
// Repair, construction, and harvest systems consume their own approach
}
else if (Cmd && Cmd->HasPendingOrders())
CommandSystem::AdvanceQueue(EM, ID);
}
}
}

} // namespace Game::Systems
