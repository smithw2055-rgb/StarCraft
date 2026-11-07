#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Systems/HarvestSystem.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <array>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>

#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Races/UnitPrototypeRegistry.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/CommandSystem.h"

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    bool IsWorkerChkUnitID(int ChkUnitID)
    {
        return Game::Races::IsWorkerChkUnitID(ChkUnitID);
    }

    void SetReason(std::string* OutReason, const std::string& Reason)
    {
        if (OutReason)
            *OutReason = Reason;
    }

    float HarvestInteractionRange(const Core::ECS::EntityManager& EM, int TargetResourceID);
    bool IssueHarvestMove(Core::ECS::EntityManager& EM,
                          int HarvesterID,
                          int TargetResourceID,
                          Core::Math::Vec2 TargetPosition,
                          int ApproachEntityID,
                          float ApproachRange,
                          int PathingIgnoreEntityID);

    int ResolveBuildingDefID(const Core::ECS::EntityManager& EM, int EntityID)
    {
        if (const auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
        {
            if (Building->BuildingDefID >= 0)
                return Building->BuildingDefID;
        }
        if (const auto* Unit = EM.GetComponent<UnitComponent>(EntityID))
            return Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
    }

    void ClearMotion(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
            Path->StallTimer = 0.0f;
            Path->StallTicks = 0;
            Path->HasLastWaypointDistance = false;
            if (const auto* Trans = EM.GetComponent<TransformComponent>(EntityID))
            {
                Path->RequestedDestination = Trans->Position;
                Path->ResolvedPathDestination = Trans->Position;
            }
        }
    }

    bool DebugHarvestEnabled()
    {
        static const bool Enabled = std::getenv("STARCRAFT_DEBUG_HARVEST") != nullptr;
        return Enabled;
    }

    bool IsMoveSettled(const PathfindingComponent* Path)
    {
        if (!Path)
            return true;
        if (Path->Path.empty())
            return true;
        return Path->PathIndex >= (int)Path->Path.size();
    }

    void RecordHarvestPathResult(HarvestComponent& Harvest, bool HasPath, const PathfindingComponent* Path = nullptr)
    {
        if (HasPath)
        {
            Harvest.ConsecutivePathFailures = 0;
            Harvest.PathRetryTicks =
                (Path && (IsMoveSettled(Path) || Path->Path.size() <= 1)) ? 8 : 0;
            return;
        }

        Harvest.ConsecutivePathFailures =
            std::min(Harvest.ConsecutivePathFailures + 1, 8);
        Harvest.PathRetryTicks = std::min(
            240,
            24 << std::min(Harvest.ConsecutivePathFailures - 1, 3));
    }

    float CollisionRadiusFromBounds(const PathfindingComponent* Path, float Fallback)
    {
        if (!Path)
            return Fallback;
        if (!Path->HasCollisionBounds)
            return Path->CollisionRadius > 0 ? (float)Path->CollisionRadius : Fallback;

        const int Width = Path->CollisionLeft + Path->CollisionRight + 1;
        const int Height = Path->CollisionUp + Path->CollisionDown + 1;
        return (float)std::max(4, std::max(Width, Height) / 2);
    }

    float WorkerContactRange(Core::ECS::EntityManager& EM,
                             int WorkerID,
                             const PathfindingComponent* Path,
                             float ExtraTolerance)
    {
        return CollisionRadiusFromBounds(
            Path ? Path : EM.GetComponent<PathfindingComponent>(WorkerID),
            16.0f) + ExtraTolerance;
    }

    float EntityEdgeDistance(Core::ECS::EntityManager& EM,
                             Core::Math::Vec2 From,
                             int TargetEntityID)
    {
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetEntityID);
        if (!TargetTrans)
            return std::numeric_limits<float>::max();

        auto* TargetPath = EM.GetComponent<PathfindingComponent>(TargetEntityID);
        if (!TargetPath || !TargetPath->HasCollisionBounds)
            return From.DistanceTo(TargetTrans->Position);

        const Core::Math::Vec2 Delta = From - TargetTrans->Position;
        const float NearestX = std::clamp(
            Delta.X,
            -(float)TargetPath->CollisionLeft,
            (float)TargetPath->CollisionRight);
        const float NearestY = std::clamp(
            Delta.Y,
            -(float)TargetPath->CollisionUp,
            (float)TargetPath->CollisionDown);
        const Core::Math::Vec2 Nearest{
            TargetTrans->Position.X + NearestX,
            TargetTrans->Position.Y + NearestY
        };
        return From.DistanceTo(Nearest);
    }
    float CollisionBoundsGap(Core::ECS::EntityManager& EM,
                             int FirstEntityID,
                             int SecondEntityID)
    {
        const auto* FirstTransform =
            EM.GetComponent<TransformComponent>(FirstEntityID);
        const auto* SecondTransform =
            EM.GetComponent<TransformComponent>(SecondEntityID);
        if (!FirstTransform || !SecondTransform)
            return std::numeric_limits<float>::max();

        const auto ResolveBounds = [&](int EntityID)
        {
            struct Bounds
            {
                float Left;
                float Up;
                float Right;
                float Down;
            };
            const auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
            if (Path && Path->HasCollisionBounds)
            {
                return Bounds{
                    (float)Path->CollisionLeft,
                    (float)Path->CollisionUp,
                    (float)Path->CollisionRight + 1.0f,
                    (float)Path->CollisionDown + 1.0f};
            }
            const float Radius = Path && Path->CollisionRadius > 0
                ? (float)Path->CollisionRadius
                : 16.0f;
            return Bounds{Radius, Radius, Radius, Radius};
        };

        const auto First = ResolveBounds(FirstEntityID);
        const auto Second = ResolveBounds(SecondEntityID);
        const float FirstMinX = FirstTransform->Position.X - First.Left;
        const float FirstMaxX = FirstTransform->Position.X + First.Right;
        const float FirstMinY = FirstTransform->Position.Y - First.Up;
        const float FirstMaxY = FirstTransform->Position.Y + First.Down;
        const float SecondMinX = SecondTransform->Position.X - Second.Left;
        const float SecondMaxX = SecondTransform->Position.X + Second.Right;
        const float SecondMinY = SecondTransform->Position.Y - Second.Up;
        const float SecondMaxY = SecondTransform->Position.Y + Second.Down;
        const float GapX = std::max(
            0.0f,
            std::max(SecondMinX - FirstMaxX, FirstMinX - SecondMaxX));
        const float GapY = std::max(
            0.0f,
            std::max(SecondMinY - FirstMaxY, FirstMinY - SecondMaxY));
        return std::sqrt(GapX * GapX + GapY * GapY);
    }

    void AddUniqueCandidate(std::vector<Core::Math::Vec2>& Candidates,
                            const Core::Math::Vec2& Candidate)
    {
        for (const auto& Existing : Candidates)
            if ((Existing - Candidate).LengthSq() < 1.0f)
                return;
        Candidates.push_back(Candidate);
    }

    bool IsRecentFailedMoveTarget(const PathfindingComponent* Path,
                                  const Core::Math::Vec2& Candidate)
    {
        return Path && Path->HasRecentMoveFailure &&
               (Path->RecentFailedMoveTarget - Candidate).LengthSq() <= 16.0f * 16.0f;
    }

    float ResolveApproachRange(Core::ECS::EntityManager& EM,
                               int HarvesterID,
                               int TargetResourceID,
                               int ApproachID,
                               float ApproachRange)
    {
        float Range = ApproachRange >= 0.0f
            ? ApproachRange
            : std::max(12.0f, HarvestInteractionRange(EM, TargetResourceID) - 8.0f);

        const auto* TargetPath = EM.GetComponent<PathfindingComponent>(ApproachID);
        if (TargetPath && TargetPath->HasCollisionBounds)
        {
            const float TargetRadius = CollisionRadiusFromBounds(TargetPath, Range);
            const float HarvesterRadius = CollisionRadiusFromBounds(
                EM.GetComponent<PathfindingComponent>(HarvesterID), 16.0f);
            Range = std::max(Range, TargetRadius + HarvesterRadius + 8.0f);
        }
        return Range;
    }

    void AddBoundsApproachCandidates(std::vector<Core::Math::Vec2>& Candidates,
                                     Core::ECS::EntityManager& EM,
                                     int HarvesterID,
                                     int ApproachID)
    {
        auto* HarvesterTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(ApproachID);
        auto* TargetPath = EM.GetComponent<PathfindingComponent>(ApproachID);
        if (!HarvesterTrans || !TargetTrans || !TargetPath || !TargetPath->HasCollisionBounds)
            return;

        const float WorkerRadius = CollisionRadiusFromBounds(
            EM.GetComponent<PathfindingComponent>(HarvesterID), 16.0f);
        float Margin = std::max(0.0f, WorkerRadius - 1.0f);
        if (const auto* Resource = EM.GetComponent<ResourceComponent>(ApproachID);
            Resource && Resource->Type == ResourceType::Minerals)
        {
            // A mineral is an interaction target, never a travel lane. Keep
            // the complete worker footprint outside the mineral collision box
            // so SCVs, Drones, and Probes touch the patch without entering it.
            Margin = WorkerRadius + 2.0f;
        }
        const float Left = (float)TargetPath->CollisionLeft;
        const float Right = (float)TargetPath->CollisionRight;
        const float Up = (float)TargetPath->CollisionUp;
        const float Down = (float)TargetPath->CollisionDown;
        const Core::Math::Vec2 Delta = HarvesterTrans->Position - TargetTrans->Position;
        const float ClampedX = std::clamp(Delta.X, -Left, Right);
        const float ClampedY = std::clamp(Delta.Y, -Up, Down);

        auto AddSide = [&](int Side)
        {
            Core::Math::Vec2 Candidate = TargetTrans->Position;
            switch (Side)
            {
                case 0:
                    Candidate.X -= Left + Margin;
                    Candidate.Y += ClampedY;
                    break;
                case 1:
                    Candidate.X += Right + Margin;
                    Candidate.Y += ClampedY;
                    break;
                case 2:
                    Candidate.X += ClampedX;
                    Candidate.Y -= Up + Margin;
                    break;
                default:
                    Candidate.X += ClampedX;
                    Candidate.Y += Down + Margin;
                    break;
            }
            AddUniqueCandidate(Candidates, Candidate);
        };

        const float HorizontalScale = Delta.X < 0.0f ? std::max(Left, 1.0f) : std::max(Right, 1.0f);
        const float VerticalScale = Delta.Y < 0.0f ? std::max(Up, 1.0f) : std::max(Down, 1.0f);
        const int PrimarySide =
            std::abs(Delta.X) / HorizontalScale >= std::abs(Delta.Y) / VerticalScale
                ? (Delta.X < 0.0f ? 0 : 1)
                : (Delta.Y < 0.0f ? 2 : 3);

        AddSide(PrimarySide);
        AddSide(0);
        AddSide(1);
        AddSide(2);
        AddSide(3);

        AddUniqueCandidate(Candidates, {
            TargetTrans->Position.X - Left - Margin,
            TargetTrans->Position.Y - Up - Margin
        });
        AddUniqueCandidate(Candidates, {
            TargetTrans->Position.X + Right + Margin,
            TargetTrans->Position.Y - Up - Margin
        });
        AddUniqueCandidate(Candidates, {
            TargetTrans->Position.X - Left - Margin,
            TargetTrans->Position.Y + Down + Margin
        });
        AddUniqueCandidate(Candidates, {
            TargetTrans->Position.X + Right + Margin,
            TargetTrans->Position.Y + Down + Margin
        });
    }

    std::vector<Core::Math::Vec2> BuildApproachCandidates(Core::ECS::EntityManager& EM,
                                                          int HarvesterID,
                                                          int TargetResourceID,
                                                          Core::Math::Vec2 TargetPosition,
                                                          int ApproachID,
                                                          float ApproachRange)
    {
        std::vector<Core::Math::Vec2> Candidates;
        AddBoundsApproachCandidates(Candidates, EM, HarvesterID, ApproachID);
        if (!Candidates.empty())
            return Candidates;

        auto* HarvesterTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(ApproachID);
        if (!HarvesterTrans || !TargetTrans)
        {
            Candidates.push_back(TargetPosition);
            return Candidates;
        }

        const float Range = ResolveApproachRange(
            EM, HarvesterID, TargetResourceID, ApproachID, ApproachRange);
        Core::Math::Vec2 Delta = HarvesterTrans->Position - TargetTrans->Position;
        if (Delta.Length() < 0.001f)
            Delta = {0.0f, 1.0f};
        AddUniqueCandidate(Candidates, TargetTrans->Position + Delta.Normalized() * Range);

        const std::array<Core::Math::Vec2, 12> Directions = {{
            {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
            {0.7071f, 0.7071f}, {-0.7071f, 0.7071f},
            {0.7071f, -0.7071f}, {-0.7071f, -0.7071f},
            {0.9239f, 0.3827f}, {-0.9239f, 0.3827f},
            {0.9239f, -0.3827f}, {-0.9239f, -0.3827f}
        }};
        for (const auto& Dir : Directions)
            AddUniqueCandidate(Candidates, TargetTrans->Position + Dir * Range);

        return Candidates;
    }

    void RestoreHarvestCommand(Core::ECS::EntityManager& EM,
                               int HarvesterID,
                               int TargetResourceID,
                               Core::Math::Vec2 TargetPosition)
    {
        if (auto* Cmd = EM.GetComponent<CommandComponent>(HarvesterID))
        {
            Cmd->CurrentCommand = CommandType::Harvest;
            Cmd->TargetEntityID = TargetResourceID;
            Cmd->TargetPosition = TargetPosition;
            if (Cmd->CurrentOrderIndex >= 0 &&
                Cmd->CurrentOrderIndex < (int)Cmd->Orders.size())
            {
                Cmd->Orders[Cmd->CurrentOrderIndex].Type = OrderType::Gather;
                Cmd->Orders[Cmd->CurrentOrderIndex].TargetEntityID = TargetResourceID;
                Cmd->Orders[Cmd->CurrentOrderIndex].TargetPosition = TargetPosition;
            }
        }
    }

    void DebugHarvestLog(const std::string& Message)
    {
        if (!DebugHarvestEnabled())
            return;
        Core::Utils::Logger::Info("HarvestSystem", Message);
    }

    void StopHarvestCommand(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Cmd = EM.GetComponent<CommandComponent>(EntityID))
        {
            Cmd->CurrentCommand = CommandType::Stop;
            Cmd->TargetEntityID = -1;
        }
        ClearMotion(EM, EntityID);
    }

    bool IsValidDepotForReturn(Core::ECS::EntityManager& EM,
                               int DepotID,
                               int PlayerID,
                               ResourceType Type)
    {
        auto* Building = EM.GetComponent<BuildingComponent>(DepotID);
        auto* Owner = EM.GetComponent<PlayerComponent>(DepotID);
        auto* DepotTrans = EM.GetComponent<TransformComponent>(DepotID);
        auto* DepotUnit = EM.GetComponent<UnitComponent>(DepotID);
        if (!Owner || !DepotTrans || !DepotUnit)
            return false;
        const int NexusDefID = Game::Races::GetUnitDefID("Nexus");
        const int CommandCenterDefID = Game::Races::GetUnitDefID("CommandCenter");
        const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
        const int LairDefID = Game::Races::GetUnitDefID("Lair");
        const int HiveDefID = Game::Races::GetUnitDefID("Hive");
        const int DefID = ResolveBuildingDefID(EM, DepotID);
        const auto* Morph = EM.GetComponent<MorphComponent>(DepotID);
        const bool IsZergDepotUpgrade = Building && Building->UnderConstruction &&
            Morph && Morph->IsMorphing && Morph->SourceUnitID == DepotID &&
            (DefID == HatcheryDefID || DefID == LairDefID || DefID == HiveDefID) &&
            (Morph->MorphTargetDefID == LairDefID ||
             Morph->MorphTargetDefID == HiveDefID);
        // Hatchery -> Lair -> Hive keeps the same living resource depot.
        // It must accept minerals and gas throughout the upgrade morph.
        if ((Building && Building->IsLifted) ||
            (Building && Building->UnderConstruction && !IsZergDepotUpgrade) ||
            DepotUnit->HitPoints <= 0)
        {
            return false;
        }
        if (Owner->PlayerID != PlayerID)
            return false;

        const bool IsResourceType =
            Type == ResourceType::Minerals || Type == ResourceType::VespeneGas;
        const bool IsDepot =
            DefID == CommandCenterDefID || DefID == NexusDefID ||
            DefID == HatcheryDefID || DefID == LairDefID || DefID == HiveDefID;
        return IsResourceType && IsDepot;
    }

    Core::Math::Vec2 ResolveGasPortPosition(Core::ECS::EntityManager& EM,
                                             int HarvesterID,
                                             int GasBuildingID)
    {
        const auto* GasTrans = EM.GetComponent<TransformComponent>(GasBuildingID);
        if (!GasTrans)
            return {};

        // openbw moves a worker to the refinery unit itself. Unit pathing then
        // resolves that target to the nearest reachable point on its collision
        // bounds; there is no single texture-authored entry doorway.
        const auto Candidates = BuildApproachCandidates(
            EM,
            HarvesterID,
            GasBuildingID,
            GasTrans->Position,
            GasBuildingID,
            -1.0f);
        return Candidates.empty() ? GasTrans->Position : Candidates.front();
    }

    Core::Math::Vec2 ResolveGasExitPosition(Core::ECS::EntityManager& EM,
                                             int HarvesterID,
                                             int GasBuildingID)
    {
        const auto* GasTrans = EM.GetComponent<TransformComponent>(GasBuildingID);
        const auto* GasPath = EM.GetComponent<PathfindingComponent>(GasBuildingID);
        const auto* WorkerTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        const auto* WorkerOwner = EM.GetComponent<PlayerComponent>(HarvesterID);
        if (!GasTrans)
            return WorkerTrans ? WorkerTrans->Position : Core::Math::Vec2{};

        int DepotID = -1;
        float BestDistance = std::numeric_limits<float>::max();
        const int PlayerID = WorkerOwner ? WorkerOwner->PlayerID : 0;
        for (int CandidateID :
             EM.GetEntitiesWithCached<TransformComponent, UnitComponent, PlayerComponent>())
        {
            const auto* CandidateTrans =
                EM.GetComponent<TransformComponent>(CandidateID);
            if (!CandidateTrans ||
                !IsValidDepotForReturn(
                    EM, CandidateID, PlayerID, ResourceType::VespeneGas))
            {
                continue;
            }
            const float Distance =
                CandidateTrans->Position.DistanceTo(GasTrans->Position);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                DepotID = CandidateID;
            }
        }

        const auto* DepotTrans = DepotID >= 0
            ? EM.GetComponent<TransformComponent>(DepotID)
            : nullptr;
        if (!DepotTrans || !GasPath || !GasPath->HasCollisionBounds)
            return WorkerTrans ? WorkerTrans->Position
                               : ResolveGasPortPosition(EM, HarvesterID, GasBuildingID);

        // openbw's find_gas_exit_position selects a legal point on the side of
        // the expanded refinery bounds facing the nearest active resource
        // depot. Mirror that geometry while leaving final route validation to
        // the normal return-to-depot path request.
        const float WorkerRadius = CollisionRadiusFromBounds(
            EM.GetComponent<PathfindingComponent>(HarvesterID), 16.0f);
        const float Margin = WorkerRadius + 2.0f;
        const float Left = (float)GasPath->CollisionLeft;
        const float Right = (float)GasPath->CollisionRight;
        const float Up = (float)GasPath->CollisionUp;
        const float Down = (float)GasPath->CollisionDown;
        const Core::Math::Vec2 Delta = DepotTrans->Position - GasTrans->Position;
        const float HorizontalScale = Delta.X < 0.0f ? std::max(Left, 1.0f)
                                                     : std::max(Right, 1.0f);
        const float VerticalScale = Delta.Y < 0.0f ? std::max(Up, 1.0f)
                                                   : std::max(Down, 1.0f);
        const bool HorizontalExit =
            std::abs(Delta.X) / HorizontalScale >=
            std::abs(Delta.Y) / VerticalScale;

        Core::Math::Vec2 Exit = GasTrans->Position;
        if (HorizontalExit)
        {
            Exit.X += Delta.X < 0.0f ? -(Left + Margin) : Right + Margin;
            Exit.Y += std::clamp(Delta.Y, -Up, Down);
        }
        else
        {
            Exit.X += std::clamp(Delta.X, -Left, Right);
            Exit.Y += Delta.Y < 0.0f ? -(Up + Margin) : Down + Margin;
        }
        return Exit;
    }

    void LeaveGasBuilding(Core::ECS::EntityManager& EM,
                          int HarvesterID,
                          HarvestComponent& Harvest)
    {
        if (!Harvest.InsideGasBuilding)
            return;
        if (auto* Transform = EM.GetComponent<TransformComponent>(HarvesterID))
            Transform->Position = Harvest.GasExitPosition;
        if (auto* Path = EM.GetComponent<PathfindingComponent>(HarvesterID))
        {
            Path->FixedPosition = Core::Math::FixedVec2(
                Harvest.GasExitPosition.X, Harvest.GasExitPosition.Y);
            Path->FixedVelocity = Core::Math::FixedVec2::Zero();
            Path->HasFixedPosition = true;
            Path->RequestedDestination = Harvest.GasExitPosition;
            Path->ResolvedPathDestination = Harvest.GasExitPosition;
        }
        if (auto* Render = EM.GetComponent<RenderComponent>(HarvesterID))
            Render->Visible = true;
        Harvest.InsideGasBuilding = false;
    }

    void IssueGasQueueMove(Core::ECS::EntityManager& EM,
                           int HarvesterID,
                           int GasBuildingID,
                           size_t QueueIndex,
                           int PathingIgnoreEntityID = -1)
    {
        const auto* GasTrans = EM.GetComponent<TransformComponent>(GasBuildingID);
        if (!GasTrans)
            return;
        (void)QueueIndex;
        if (PathingIgnoreEntityID < 0)
        {
            if (const auto* Path = EM.GetComponent<PathfindingComponent>(HarvesterID))
            {
                if (Path->RequestedIgnoreEntityID >= 0 &&
                    Path->RequestedIgnoreEntityID != GasBuildingID &&
                    EM.GetComponent<BuildingComponent>(Path->RequestedIgnoreEntityID))
                {
                    if (const auto* Trans = EM.GetComponent<TransformComponent>(HarvesterID))
                    {
                        const float DepartureClearance =
                            WorkerContactRange(EM, HarvesterID, Path, 2.0f);
                        if (EntityEdgeDistance(EM, Trans->Position, Path->RequestedIgnoreEntityID) <= DepartureClearance)
                        {
                            PathingIgnoreEntityID = Path->RequestedIgnoreEntityID;
                        }
                    }
                }
            }
        }
        IssueHarvestMove(
            EM,
            HarvesterID,
            GasBuildingID,
            GasTrans->Position,
            GasBuildingID,
            -1.0f,
            PathingIgnoreEntityID >= 0 ? PathingIgnoreEntityID : GasBuildingID);
    }

    float HarvestInteractionRange(const Core::ECS::EntityManager& EM, int TargetResourceID)
    {
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
        const auto* Resource = MutableEM.GetComponent<ResourceComponent>(TargetResourceID);
        if (!Resource)
            return 24.0f;
        return Resource->Type == ResourceType::VespeneGas ? 64.0f : 40.0f;
    }

    bool IsWithinHarvestRange(Core::ECS::EntityManager& EM,
                              int HarvesterID,
                              int TargetResourceID)
    {
        // Mining starts only when the DAT collision boxes touch. Keep a tiny
        // fixed-point allowance, but do not add path node tolerance here: that
        // made workers mine visibly far away and left Partial endpoints in a
        // state that was neither moving nor interacting.
        const auto* Resource = EM.GetComponent<ResourceComponent>(TargetResourceID);
        if (Resource && Resource->Type == ResourceType::VespeneGas)
        {
            constexpr float kGasDoorwayTolerance = 16.0f;
            if (CollisionBoundsGap(EM, HarvesterID, TargetResourceID) <= kGasDoorwayTolerance)
                return true;
            if (const auto* Trans = EM.GetComponent<TransformComponent>(HarvesterID))
            {
                if (EntityEdgeDistance(EM, Trans->Position, TargetResourceID) <= kGasDoorwayTolerance)
                    return true;
            }
            return false;
        }

        constexpr float kInteractionGapTolerance = 2.0f;
        return CollisionBoundsGap(EM, HarvesterID, TargetResourceID) <=
               kInteractionGapTolerance;
    }

    Core::Math::Vec2 ResolveDepotDeliveryTarget(Core::ECS::EntityManager& EM,
                                                 int HarvesterID,
                                                 int DepotID)
    {
        const auto* WorkerTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        const auto* DepotTrans = EM.GetComponent<TransformComponent>(DepotID);
        const auto* DepotPath = EM.GetComponent<PathfindingComponent>(DepotID);
        if (!WorkerTrans || !DepotTrans || !DepotPath || !DepotPath->HasCollisionBounds)
            return DepotTrans ? DepotTrans->Position : Core::Math::Vec2{};

        constexpr float DeliveryDepth = 24.0f;
        constexpr float SidePadding = 16.0f;
        const float Left = (float)DepotPath->CollisionLeft;
        const float Right = (float)DepotPath->CollisionRight;
        const float Up = (float)DepotPath->CollisionUp;
        const float Down = (float)DepotPath->CollisionDown;
        const Core::Math::Vec2 Delta = WorkerTrans->Position - DepotTrans->Position;
        const float HorizontalScale = Delta.X < 0.0f ? std::max(Left, 1.0f)
                                                     : std::max(Right, 1.0f);
        const float VerticalScale = Delta.Y < 0.0f ? std::max(Up, 1.0f)
                                                   : std::max(Down, 1.0f);
        const bool HorizontalEntry =
            std::abs(Delta.X) / HorizontalScale >= std::abs(Delta.Y) / VerticalScale;

        Core::Math::Vec2 Target = DepotTrans->Position;
        if (HorizontalEntry)
        {
            Target.X += Delta.X < 0.0f
                ? -Left + std::min(DeliveryDepth, Left * 0.5f)
                : Right - std::min(DeliveryDepth, Right * 0.5f);
            Target.Y += std::clamp(
                Delta.Y,
                -std::max(0.0f, Up - SidePadding),
                std::max(0.0f, Down - SidePadding));
        }
        else
        {
            Target.X += std::clamp(
                Delta.X,
                -std::max(0.0f, Left - SidePadding),
                std::max(0.0f, Right - SidePadding));
            Target.Y += Delta.Y < 0.0f
                ? -Up + std::min(DeliveryDepth, Up * 0.5f)
                : Down - std::min(DeliveryDepth, Down * 0.5f);
        }
        return Target;
    }

    bool IsInsideDepotDeliveryZone(Core::ECS::EntityManager& EM,
                                   int HarvesterID,
                                   int DepotID)
    {
        const auto* WorkerTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        const auto* DepotTrans = EM.GetComponent<TransformComponent>(DepotID);
        const auto* DepotPath = EM.GetComponent<PathfindingComponent>(DepotID);
        if (!WorkerTrans || !DepotTrans)
            return false;
        if (!DepotPath || !DepotPath->HasCollisionBounds)
            return WorkerTrans->Position.DistanceTo(DepotTrans->Position) <= 28.0f;

        constexpr float RequiredDepth = 12.0f;
        const Core::Math::Vec2 Delta = WorkerTrans->Position - DepotTrans->Position;
        return Delta.X >= -(float)DepotPath->CollisionLeft + RequiredDepth &&
               Delta.X <= (float)DepotPath->CollisionRight - RequiredDepth &&
               Delta.Y >= -(float)DepotPath->CollisionUp + RequiredDepth &&
               Delta.Y <= (float)DepotPath->CollisionDown - RequiredDepth;
    }

    bool IssueMineralDepartureMove(Core::ECS::EntityManager& EM,
                                   int HarvesterID,
                                   int TargetResourceID)
    {
        const auto* Resource =
            EM.GetComponent<ResourceComponent>(TargetResourceID);
        const auto* WorkerTransform =
            EM.GetComponent<TransformComponent>(HarvesterID);
        const auto* MineralTransform =
            EM.GetComponent<TransformComponent>(TargetResourceID);
        auto* WorkerPath =
            EM.GetComponent<PathfindingComponent>(HarvesterID);
        const auto* MineralPath =
            EM.GetComponent<PathfindingComponent>(TargetResourceID);
        if (!Resource || Resource->Type != ResourceType::Minerals ||
            !WorkerTransform || !MineralTransform ||
            !WorkerPath || !MineralPath ||
            CollisionBoundsGap(EM, HarvesterID, TargetResourceID) > 2.1f)
        {
            return false;
        }

        const float WorkerLeft = WorkerPath->HasCollisionBounds
            ? (float)WorkerPath->CollisionLeft
            : (float)WorkerPath->CollisionRadius;
        const float WorkerRight = WorkerPath->HasCollisionBounds
            ? (float)WorkerPath->CollisionRight
            : (float)WorkerPath->CollisionRadius;
        const float WorkerUp = WorkerPath->HasCollisionBounds
            ? (float)WorkerPath->CollisionUp
            : (float)WorkerPath->CollisionRadius;
        const float WorkerDown = WorkerPath->HasCollisionBounds
            ? (float)WorkerPath->CollisionDown
            : (float)WorkerPath->CollisionRadius;
        const float MineralLeft = MineralPath->HasCollisionBounds
            ? (float)MineralPath->CollisionLeft
            : (float)MineralPath->CollisionRadius;
        const float MineralRight = MineralPath->HasCollisionBounds
            ? (float)MineralPath->CollisionRight
            : (float)MineralPath->CollisionRadius;
        const float MineralUp = MineralPath->HasCollisionBounds
            ? (float)MineralPath->CollisionUp
            : (float)MineralPath->CollisionRadius;
        const float MineralDown = MineralPath->HasCollisionBounds
            ? (float)MineralPath->CollisionDown
            : (float)MineralPath->CollisionRadius;

        const float Left =
            MineralTransform->Position.X - MineralLeft - WorkerRight;
        const float Right =
            MineralTransform->Position.X + MineralRight + WorkerLeft;
        const float Top =
            MineralTransform->Position.Y - MineralUp - WorkerDown;
        const float Bottom =
            MineralTransform->Position.Y + MineralDown + WorkerUp;
        constexpr float DepartureClearance = 16.0f;
        const auto Position = WorkerTransform->Position;
        struct Candidate
        {
            float Distance;
            int Side;
            Core::Math::Vec2 Position;
        };
        std::array<Candidate, 4> Candidates{{
            {std::abs(Position.X - Left), 0,
             {Left - DepartureClearance, Position.Y}},
            {std::abs(Position.X - Right), 1,
             {Right + DepartureClearance, Position.Y}},
            {std::abs(Position.Y - Top), 2,
             {Position.X, Top - DepartureClearance}},
            {std::abs(Position.Y - Bottom), 3,
             {Position.X, Bottom + DepartureClearance}}
        }};
        std::sort(
            Candidates.begin(), Candidates.end(),
            [](const Candidate& A, const Candidate& B)
            {
                if (A.Distance != B.Distance)
                    return A.Distance < B.Distance;
                return A.Side < B.Side;
            });

        const auto& Departure = Candidates.front();
        WorkerPath->Path = {Departure.Position};
        WorkerPath->PathIndex = 0;
        WorkerPath->RequestedDestination = Departure.Position;
        WorkerPath->ResolvedPathDestination = Departure.Position;
        WorkerPath->LastPathStatus = Game::Pathfinding::PathStatus::Exact;
        WorkerPath->LastPathEndReason = Game::Pathfinding::PathEndReason::None;
        WorkerPath->RequestedTargetEntityID = -1;
        WorkerPath->RequestedIgnoreEntityID = -1;
        WorkerPath->RequestedAdditionalIgnoreEntityID = -1;
        WorkerPath->NodeTolerance = 1.0f;
        WorkerPath->BlockedByEntityID = -1;
        WorkerPath->RepathFailCount = 0;
        WorkerPath->LastRepathTime = 0.0f;
        WorkerPath->HasRecentMoveFailure = false;
        WorkerPath->RecentMoveFailureCooldown = 0.0f;
        WorkerPath->RecentMoveFailureTicks = 0;
        WorkerPath->StallTimer = 0.0f;
        WorkerPath->HasLastWaypointDistance = false;
        WorkerPath->DestinationStallTimer = 0.0f;
        WorkerPath->HasLastDestinationDistance = false;
        DebugHarvestLog(
            "mineral-depart worker=" + std::to_string(HarvesterID) +
            " resource=" + std::to_string(TargetResourceID) +
            " side=" + std::to_string(Departure.Side) +
            " target=" + std::to_string((int)Departure.Position.X) + "," +
            std::to_string((int)Departure.Position.Y));
        return true;
    }
    bool IssueDepotReturnMove(Core::ECS::EntityManager& EM,
                              int HarvesterID,
                              int TargetResourceID,
                              int DepotID)
    {
        const auto* DepotTrans = EM.GetComponent<TransformComponent>(DepotID);
        if (!DepotTrans)
            return false;

        const Core::Math::Vec2 DeliveryTarget =
            ResolveDepotDeliveryTarget(EM, HarvesterID, DepotID);

        if (const auto* ExistingPath =
                EM.GetComponent<PathfindingComponent>(HarvesterID);
            ExistingPath && ExistingPath->HasRecentMoveFailure &&
            ExistingPath->RecentMoveFailureTicks > 0 &&
            (ExistingPath->RecentFailedMoveTarget.DistanceTo(DeliveryTarget) <= 32.0f ||
             ExistingPath->RecentFailedMoveTarget.DistanceTo(DepotTrans->Position) <= 32.0f))
        {
            // A blocked depot approach waits for the deterministic retry
            // window instead of issuing the same searches every tick.
            return false;
        }

        bool HasPath = CommandSystem::IssueMoveAndHasPath(
            EM, HarvesterID, DeliveryTarget, DepotID);
        RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, DepotTrans->Position);
        if (!HasPath && DeliveryTarget.DistanceTo(DepotTrans->Position) > 1.0f)
        {
            HasPath = CommandSystem::IssueMoveAndHasPath(
                EM, HarvesterID, DepotTrans->Position, DepotID);
            RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, DepotTrans->Position);
        }
        if (!HasPath)
        {
            std::vector<Core::Math::Vec2> Candidates;
            AddBoundsApproachCandidates(Candidates, EM, HarvesterID, DepotID);
            for (const auto& Candidate : Candidates)
            {
                if (CommandSystem::IssueMoveAndHasPath(EM, HarvesterID, Candidate, DepotID))
                {
                    RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, DepotTrans->Position);
                    HasPath = true;
                    break;
                }
            }
        }
        if (HasPath && !IsInsideDepotDeliveryZone(EM, HarvesterID, DepotID))
        {
            if (auto* Path = EM.GetComponent<PathfindingComponent>(HarvesterID))
            {
                if (Path->LastPathStatus == Game::Pathfinding::PathStatus::Partial &&
                    (Path->ResolvedPathDestination.DistanceTo(DeliveryTarget) <= 64.0f ||
                     EntityEdgeDistance(EM, Path->ResolvedPathDestination, DepotID) <= 24.0f))
                {
                    Path->NodeTolerance = 1.0f;
                    if (Path->Path.empty() ||
                        Path->Path.back().DistanceTo(DeliveryTarget) > 0.5f)
                    {
                        Path->Path.push_back(DeliveryTarget);
                    }
                    Path->RequestedDestination = DeliveryTarget;
                    Path->ResolvedPathDestination = DeliveryTarget;
                }
            }
        }

        // A same-tile query can have no waypoints while the worker still needs
        // to move the final few pixels into the depot. Preserve that short
        // deterministic segment and report it as a usable attempt.
        if (!HasPath && !IsInsideDepotDeliveryZone(EM, HarvesterID, DepotID))
        {
            auto* WorkerTrans = EM.GetComponent<TransformComponent>(HarvesterID);
            auto* Path = EM.GetComponent<PathfindingComponent>(HarvesterID);
            if (WorkerTrans && Path &&
                EntityEdgeDistance(EM, WorkerTrans->Position, DepotID) <= 32.0f &&
                WorkerTrans->Position.DistanceTo(DeliveryTarget) > 0.5f)
            {
                Path->Path = {DeliveryTarget};
                Path->PathIndex = 0;
                Path->RequestedDestination = DeliveryTarget;
                Path->ResolvedPathDestination = DeliveryTarget;
                Path->LastPathStatus = Game::Pathfinding::PathStatus::Exact;
                Path->LastPathEndReason = Game::Pathfinding::PathEndReason::None;
                Path->RequestedTargetEntityID = -1;
                Path->RequestedIgnoreEntityID = DepotID;
                Path->NodeTolerance = 2.0f;
                Path->RepathFailCount = 0;
                Path->LastRepathTime = 0.0f;
                Path->BlockedByEntityID = -1;
                Path->HasRecentMoveFailure = false;
                Path->RecentMoveFailureCooldown = 0.0f;
                Path->RecentMoveFailureTicks = 0;
                Path->StallTimer = 0.0f;
                Path->HasLastWaypointDistance = false;
                Path->DestinationStallTimer = 0.0f;
                Path->HasLastDestinationDistance = false;
                RestoreHarvestCommand(
                    EM, HarvesterID, TargetResourceID, DepotTrans->Position);
                HasPath = true;
            }
        }
        return HasPath;
    }

    bool IssueAdjacentMineralEscapeMove(
        Core::ECS::EntityManager& EM,
        int HarvesterID,
        HarvestComponent& Harvest,
        PathfindingComponent& Path)
    {
        const int BlockerID = Path.BlockedByEntityID;
        if (BlockerID < 0 || BlockerID == Harvest.TargetResourceID)
            return false;

        const auto* BlockerResource =
            EM.GetComponent<ResourceComponent>(BlockerID);
        const auto* WorkerTransform =
            EM.GetComponent<TransformComponent>(HarvesterID);
        const auto* BlockerTransform =
            EM.GetComponent<TransformComponent>(BlockerID);
        const auto* BlockerPath =
            EM.GetComponent<PathfindingComponent>(BlockerID);
        if (!BlockerResource ||
            BlockerResource->Type != ResourceType::Minerals ||
            !WorkerTransform || !BlockerTransform || !BlockerPath)
        {
            return false;
        }

        const float WorkerLeft = Path.HasCollisionBounds
            ? (float)Path.CollisionLeft : (float)Path.CollisionRadius;
        const float WorkerRight = Path.HasCollisionBounds
            ? (float)Path.CollisionRight : (float)Path.CollisionRadius;
        const float WorkerUp = Path.HasCollisionBounds
            ? (float)Path.CollisionUp : (float)Path.CollisionRadius;
        const float WorkerDown = Path.HasCollisionBounds
            ? (float)Path.CollisionDown : (float)Path.CollisionRadius;
        const float BlockerLeft = BlockerPath->HasCollisionBounds
            ? (float)BlockerPath->CollisionLeft
            : (float)BlockerPath->CollisionRadius;
        const float BlockerRight = BlockerPath->HasCollisionBounds
            ? (float)BlockerPath->CollisionRight
            : (float)BlockerPath->CollisionRadius;
        const float BlockerUp = BlockerPath->HasCollisionBounds
            ? (float)BlockerPath->CollisionUp
            : (float)BlockerPath->CollisionRadius;
        const float BlockerDown = BlockerPath->HasCollisionBounds
            ? (float)BlockerPath->CollisionDown
            : (float)BlockerPath->CollisionRadius;

        const float Left =
            BlockerTransform->Position.X - BlockerLeft - WorkerRight;
        const float Right =
            BlockerTransform->Position.X + BlockerRight + WorkerLeft;
        const float Top =
            BlockerTransform->Position.Y - BlockerUp - WorkerDown;
        const float Bottom =
            BlockerTransform->Position.Y + BlockerDown + WorkerUp;
        const auto Position = WorkerTransform->Position;
        const bool IsEmbedded =
            Position.X > Left && Position.X < Right &&
            Position.Y > Top && Position.Y < Bottom;

        struct EscapeCandidate
        {
            float Distance = 0.0f;
            int Side = 0;
            Core::Math::Vec2 Position;
        };
        std::vector<EscapeCandidate> Candidates;
        if (IsEmbedded)
        {
            Candidates = {
                {Position.X - Left, 0, {Left - 1.0f, Position.Y}},
                {Right - Position.X, 1, {Right + 1.0f, Position.Y}},
                {Position.Y - Top, 2, {Position.X, Top - 1.0f}},
                {Bottom - Position.Y, 3, {Position.X, Bottom + 1.0f}}
            };
        }
        else
        {
            // The worker may be just outside a neighboring mineral while its
            // depot segment points through that mineral. Move to a corner on
            // the worker's current side before replanning. Every candidate
            // segment stays outside the expanded mineral rectangle.
            // Keep the corner target beyond the ordinary movement node
            // tolerance; otherwise a short 5-8 pixel sidestep is accepted as
            // already reached and the worker never actually leaves the lane.
            constexpr float Clearance = 16.0f;
            if (Position.Y <= Top)
            {
                const float SafeY = std::min(Position.Y, Top - Clearance);
                Candidates.push_back({
                    Position.DistanceTo({Left - Clearance, SafeY}),
                    4, {Left - Clearance, SafeY}});
                Candidates.push_back({
                    Position.DistanceTo({Right + Clearance, SafeY}),
                    5, {Right + Clearance, SafeY}});
            }
            if (Position.Y >= Bottom)
            {
                const float SafeY = std::max(Position.Y, Bottom + Clearance);
                Candidates.push_back({
                    Position.DistanceTo({Left - Clearance, SafeY}),
                    6, {Left - Clearance, SafeY}});
                Candidates.push_back({
                    Position.DistanceTo({Right + Clearance, SafeY}),
                    7, {Right + Clearance, SafeY}});
            }
            if (Position.X <= Left)
            {
                const float SafeX = std::min(Position.X, Left - Clearance);
                Candidates.push_back({
                    Position.DistanceTo({SafeX, Top - Clearance}),
                    8, {SafeX, Top - Clearance}});
                Candidates.push_back({
                    Position.DistanceTo({SafeX, Bottom + Clearance}),
                    9, {SafeX, Bottom + Clearance}});
            }
            if (Position.X >= Right)
            {
                const float SafeX = std::max(Position.X, Right + Clearance);
                Candidates.push_back({
                    Position.DistanceTo({SafeX, Top - Clearance}),
                    10, {SafeX, Top - Clearance}});
                Candidates.push_back({
                    Position.DistanceTo({SafeX, Bottom + Clearance}),
                    11, {SafeX, Bottom + Clearance}});
            }
        }
        if (Candidates.empty())
            return false;
        std::sort(
            Candidates.begin(), Candidates.end(),
            [](const EscapeCandidate& A, const EscapeCandidate& B)
            {
                if (A.Distance != B.Distance)
                    return A.Distance < B.Distance;
                return A.Side < B.Side;
            });

        const auto* ResourceTransform =
            EM.GetComponent<TransformComponent>(Harvest.TargetResourceID);
        for (const auto& Candidate : Candidates)
        {
            Path.HasRecentMoveFailure = false;
            Path.RecentMoveFailureCooldown = 0.0f;
            Path.RecentMoveFailureTicks = 0;
            if (!CommandSystem::IssueMoveAndHasPath(
                    EM, HarvesterID, Candidate.Position, BlockerID))
            {
                continue;
            }
            RestoreHarvestCommand(
                EM, HarvesterID, Harvest.TargetResourceID,
                ResourceTransform ? ResourceTransform->Position
                                  : Candidate.Position);
            DebugHarvestLog(
                "return-mineral-escape worker=" +
                std::to_string(HarvesterID) +
                " blocker=" + std::to_string(BlockerID) +
                " side=" + std::to_string(Candidate.Side) +
                " mode=" + (IsEmbedded ? "embedded" : "detour") +
                " target=" +
                std::to_string((int)Candidate.Position.X) + "," +
                std::to_string((int)Candidate.Position.Y));
            return true;
        }
        return false;
    }
    bool IssueHarvestMove(Core::ECS::EntityManager& EM,
                          int HarvesterID,
                          int TargetResourceID,
                          Core::Math::Vec2 TargetPosition,
                          int ApproachEntityID = -1,
                          float ApproachRange = -1.0f,
                          int PathingIgnoreEntityID = -1)
    {
        const int ApproachID = ApproachEntityID >= 0 ? ApproachEntityID : TargetResourceID;
        const auto* ApproachResource =
            EM.GetComponent<ResourceComponent>(ApproachID);
        const bool IsSolidMineral =
            ApproachResource &&
            ApproachResource->Type == ResourceType::Minerals;
        const int IgnoreID = PathingIgnoreEntityID >= 0
            ? PathingIgnoreEntityID
            : (IsSolidMineral ? -1 : ApproachID);
        if (const auto* ExistingPath =
                EM.GetComponent<PathfindingComponent>(HarvesterID);
            ExistingPath && ExistingPath->HasRecentMoveFailure &&
            ExistingPath->RecentMoveFailureTicks > 0)
        {
            // A failed static approach is retried after one deterministic
            // second, not on every simulation tick.
            return IsWithinHarvestRange(
                EM, HarvesterID, TargetResourceID);
        }
        const auto Candidates = BuildApproachCandidates(
            EM, HarvesterID, TargetResourceID, TargetPosition, ApproachID, ApproachRange);

        auto TryMove = [&](const Core::Math::Vec2& Candidate)
        {
            const bool HasPath = CommandSystem::IssueMoveAndHasPath(
                EM,
                HarvesterID,
                Candidate,
                IgnoreID);
            if (HasPath && IsSolidMineral)
            {
                // The minitile search may stop one 8-pixel cell outside the
                // exact DAT contact point and report Partial/GoalBlocked.
                // Finish with a short same-side segment. Runtime mineral
                // collision stays enabled, so this can touch but never cross
                // the patch.
                if (auto* Path =
                        EM.GetComponent<PathfindingComponent>(HarvesterID))
                {
                    if (Path->LastPathStatus ==
                            Game::Pathfinding::PathStatus::Partial &&
                        Path->ResolvedPathDestination.DistanceTo(Candidate) > 16.0f)
                    {
                        ClearMotion(EM, HarvesterID);
                        Path->HasRecentMoveFailure = true;
                        Path->RecentFailedMoveTarget = Candidate;
                        Path->RecentMoveFailureTicks = 24;
                        Path->RecentMoveFailureCooldown = 1.0f;
                        RestoreHarvestCommand(
                            EM, HarvesterID, TargetResourceID, TargetPosition);
                        return false;
                    }
                    Path->NodeTolerance = 1.0f;
                    if (Path->Path.empty() ||
                        Path->Path.back().DistanceTo(Candidate) > 0.5f)
                    {
                        Path->Path.push_back(Candidate);
                    }
                    Path->RequestedDestination = Candidate;
                    Path->ResolvedPathDestination = Candidate;
                }
            }
            RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, TargetPosition);
            return HasPath;
        };

        bool HasIssuedReachableMove = false;
        constexpr int MaximumApproachPathAttempts = 4;
        int ApproachPathAttempts = 0;
        for (const auto& Candidate : Candidates)
        {
            if (IsRecentFailedMoveTarget(
                    EM.GetComponent<PathfindingComponent>(HarvesterID), Candidate))
            {
                continue;
            }
            if (ApproachPathAttempts >= MaximumApproachPathAttempts)
                break;
            ++ApproachPathAttempts;
            if (TryMove(Candidate))
            {
                HasIssuedReachableMove = true;
                break;
            }
        }

        // If every candidate was suppressed by the recent-failure marker,
        // permit one bounded retry set.  Do not run the complete candidate
        // list twice and then issue the first point a third time.
        if (!HasIssuedReachableMove && ApproachPathAttempts == 0)
        {
            if (auto* Path = EM.GetComponent<PathfindingComponent>(HarvesterID))
                Path->HasRecentMoveFailure = false;
            for (const auto& Candidate : Candidates)
            {
                if (ApproachPathAttempts >= MaximumApproachPathAttempts)
                    break;
                ++ApproachPathAttempts;
                if (TryMove(Candidate))
                {
                    HasIssuedReachableMove = true;
                    break;
                }
            }
        }

        RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, TargetPosition);
        return HasIssuedReachableMove ||
            IsWithinHarvestRange(EM, HarvesterID, TargetResourceID);
    }

    bool RefreshResourceCollisionIgnoreAfterDepotDeparture(
        Core::ECS::EntityManager& EM,
        int HarvesterID,
        int TargetResourceID)
    {
        auto* Path = EM.GetComponent<PathfindingComponent>(HarvesterID);
        if (!Path || TargetResourceID < 0 ||
            Path->RequestedIgnoreEntityID < 0 ||
            Path->RequestedIgnoreEntityID == TargetResourceID)
        {
            return false;
        }

        const int DepartureDepotID = Path->RequestedIgnoreEntityID;
        if (!EM.GetComponent<BuildingComponent>(DepartureDepotID))
            return false;

        const auto* HarvesterTrans =
            EM.GetComponent<TransformComponent>(HarvesterID);
        if (!HarvesterTrans)
            return false;

        // A worker which has just deposited can still be inside its depot's
        // delivery footprint. Keep ignoring that depot until the worker has
        // cleared it completely, then make the actual resource the collision
        // exception. Leaving the depot as the only ignored entity makes the
        // worker repeatedly collide with its mineral field at the final
        // approach point.
        const float DepartureClearance =
            WorkerContactRange(EM, HarvesterID, Path, 2.0f);
        if (EntityEdgeDistance(
                EM, HarvesterTrans->Position, DepartureDepotID) <=
            DepartureClearance)
        {
            return false;
        }

        const auto* ResourceTrans =
            EM.GetComponent<TransformComponent>(TargetResourceID);
        if (!ResourceTrans)
            return false;

        IssueHarvestMove(
            EM, HarvesterID, TargetResourceID, ResourceTrans->Position);
        return true;
    }

    void RemoveQueuedGatherer(ResourceComponent& Resource, int HarvesterID)
    {
        Resource.QueuedGatherers.erase(
            std::remove(Resource.QueuedGatherers.begin(),
                        Resource.QueuedGatherers.end(),
                        HarvesterID),
            Resource.QueuedGatherers.end());
    }

    void QueueGathererForResource(Core::ECS::EntityManager& EM,
                                  int ResourceID,
                                  int HarvesterID)
    {
        auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
        if (!Resource)
            return;
        if (Resource->ActiveGathererEntityID == HarvesterID)
            return;
        if (std::find(Resource->QueuedGatherers.begin(),
                      Resource->QueuedGatherers.end(),
                      HarvesterID) == Resource->QueuedGatherers.end())
        {
            Resource->QueuedGatherers.push_back(HarvesterID);
        }
    }

    void PruneQueuedGatherers(Core::ECS::EntityManager& EM,
                              int ResourceID,
                              ResourceComponent& Resource)
    {
        if (Resource.ActiveGathererEntityID >= 0)
        {
            const int ActiveID = Resource.ActiveGathererEntityID;
            const auto* ActiveHarvest =
                EM.GetComponent<HarvestComponent>(ActiveID);
            if (!EM.IsAlive(ActiveID) ||
                !ActiveHarvest ||
                ActiveHarvest->TargetResourceID != ResourceID ||
                ActiveHarvest->State != HarvestState::Mining)
            {
                Resource.ActiveGathererEntityID = -1;
            }
        }

        Resource.QueuedGatherers.erase(
            std::remove_if(
                Resource.QueuedGatherers.begin(),
                Resource.QueuedGatherers.end(),
                [&](int GathererID)
                {
                    if (!EM.IsAlive(GathererID))
                        return true;
                    auto* Harvest = EM.GetComponent<HarvestComponent>(GathererID);
                    return !Harvest ||
                        Harvest->TargetResourceID != ResourceID ||
                        (Harvest->State != HarvestState::MovingToResource &&
                         Harvest->State != HarvestState::QueuedAtResource &&
                         Harvest->State != HarvestState::Mining);
                }),
            Resource.QueuedGatherers.end());

        // Sort queued gatherers when slot is free: prioritize workers that
        // have already arrived within harvest range, followed by distance.
        if (Resource.ActiveGathererEntityID < 0 && !Resource.QueuedGatherers.empty())
        {
            std::stable_sort(
                Resource.QueuedGatherers.begin(),
                Resource.QueuedGatherers.end(),
                [&](int A, int B)
                {
                    const bool AInRange = IsWithinHarvestRange(EM, A, ResourceID);
                    const bool BInRange = IsWithinHarvestRange(EM, B, ResourceID);
                    if (AInRange != BInRange)
                        return AInRange;
                    const auto* AT = EM.GetComponent<TransformComponent>(A);
                    const auto* BT = EM.GetComponent<TransformComponent>(B);
                    if (!AT || !BT)
                        return AT != nullptr;
                    const float ADistance =
                        EntityEdgeDistance(EM, AT->Position, ResourceID);
                    const float BDistance =
                        EntityEdgeDistance(EM, BT->Position, ResourceID);
                    return ADistance < BDistance;
                });
        }
    }

    int FindBalancedNearbyMineral(Core::ECS::EntityManager& EM,
                                  int HarvesterID,
                                  int AnchorResourceID)
    {
        const auto* WorkerTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        const auto* AnchorTrans = EM.GetComponent<TransformComponent>(AnchorResourceID);
        if (!WorkerTrans || !AnchorTrans)
            return AnchorResourceID;

        constexpr float ClusterRange = 32.0f * 8.0f;
        int BestID = AnchorResourceID;
        size_t BestLoad = std::numeric_limits<size_t>::max();
        float BestDistance = std::numeric_limits<float>::max();
        for (int CandidateID :
             EM.GetEntitiesWithCached<TransformComponent, ResourceComponent>())
        {
            auto* Resource = EM.GetComponent<ResourceComponent>(CandidateID);
            const auto* CandidateTrans =
                EM.GetComponent<TransformComponent>(CandidateID);
            if (!Resource || !CandidateTrans ||
                Resource->Type != ResourceType::Minerals ||
                Resource->IsDepleted || Resource->Amount <= 0 ||
                CandidateTrans->Position.DistanceTo(AnchorTrans->Position) >
                    ClusterRange)
            {
                continue;
            }

            PruneQueuedGatherers(EM, CandidateID, *Resource);
            const size_t Load = Resource->QueuedGatherers.size() +
                (Resource->ActiveGathererEntityID >= 0 ? 1u : 0u);
            const float Distance =
                WorkerTrans->Position.DistanceTo(CandidateTrans->Position);
            if (Load < BestLoad ||
                (Load == BestLoad &&
                 (Distance < BestDistance ||
                  (Distance == BestDistance && CandidateID < BestID))))
            {
                BestLoad = Load;
                BestDistance = Distance;
                BestID = CandidateID;
            }
        }
        return BestID;
    }
    int FindAvailableNearbyMineral(Core::ECS::EntityManager& EM,
                                   int HarvesterID,
                                   int CurrentResourceID,
                                   int ExcludedResourceID = -1)
    {
        const auto* WorkerTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        const auto* CurrentTrans = EM.GetComponent<TransformComponent>(CurrentResourceID);
        if (!WorkerTrans || !CurrentTrans)
            return -1;

        constexpr float ClusterRange = 32.0f * 8.0f;
        int BestID = -1;
        float BestDistance = std::numeric_limits<float>::max();
        for (int CandidateID : EM.GetEntitiesWithCached<TransformComponent, ResourceComponent>())
        {
            if (CandidateID == CurrentResourceID ||
                CandidateID == ExcludedResourceID)
            {
                continue;
            }
            auto* Candidate = EM.GetComponent<ResourceComponent>(CandidateID);
            const auto* CandidateTrans = EM.GetComponent<TransformComponent>(CandidateID);
            if (!Candidate || !CandidateTrans ||
                Candidate->Type != ResourceType::Minerals ||
                Candidate->IsDepleted || Candidate->Amount <= 0)
            {
                continue;
            }
            PruneQueuedGatherers(EM, CandidateID, *Candidate);
            if (Candidate->ActiveGathererEntityID >= 0 &&
                !EM.IsAlive(Candidate->ActiveGathererEntityID))
            {
                Candidate->ActiveGathererEntityID = -1;
            }
            if (Candidate->ActiveGathererEntityID >= 0 ||
                !Candidate->QueuedGatherers.empty() ||
                CandidateTrans->Position.DistanceTo(CurrentTrans->Position) > ClusterRange)
            {
                continue;
            }

            const float Distance =
                WorkerTrans->Position.DistanceTo(CandidateTrans->Position);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                BestID = CandidateID;
            }
        }
        return BestID;
    }

    void ReleaseResourceAssignment(Core::ECS::EntityManager& EM,
                                   int ResourceID,
                                   int HarvesterID)
    {
        auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
        if (!Resource)
            return;
        if (Resource->ActiveGathererEntityID == HarvesterID)
            Resource->ActiveGathererEntityID = -1;
        RemoveQueuedGatherer(*Resource, HarvesterID);
    }

    bool CanClaimResource(Core::ECS::EntityManager& EM,
                          int ResourceID,
                          int HarvesterID)
    {
        auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
        if (!Resource ||
            (Resource->Type == ResourceType::Minerals &&
             (Resource->IsDepleted || Resource->Amount <= 0)))
            return false;
        PruneQueuedGatherers(EM, ResourceID, *Resource);
        if (Resource->ActiveGathererEntityID == HarvesterID)
            return true;
        if (Resource->ActiveGathererEntityID >= 0 &&
            Resource->ActiveGathererEntityID != HarvesterID &&
            EM.IsAlive(Resource->ActiveGathererEntityID))
        {
            return false;
        }
        if (Resource->QueuedGatherers.empty() ||
            Resource->QueuedGatherers.front() == HarvesterID)
        {
            return true;
        }
        if (IsWithinHarvestRange(EM, HarvesterID, ResourceID) &&
            !IsWithinHarvestRange(EM, Resource->QueuedGatherers.front(), ResourceID))
        {
            return true;
        }
        return false;
    }

    void ClaimResource(Core::ECS::EntityManager& EM,
                       int ResourceID,
                       int HarvesterID)
    {
        auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
        if (!Resource)
            return;
        RemoveQueuedGatherer(*Resource, HarvesterID);
        Resource->ActiveGathererEntityID = HarvesterID;
    }

    bool RetargetBlockedMineralApproach(Core::ECS::EntityManager& EM,
                                        int HarvesterID,
                                        HarvestComponent& Harvest,
                                        PathfindingComponent* Path)
    {
        if (!Path || Path->BlockedByEntityID < 0 ||
            Path->BlockedByEntityID == Harvest.TargetResourceID)
        {
            return false;
        }

        // Once the worker touches its current target, that semantic target
        // wins over any stale adjacent-mineral blocker reported by the final
        // segment. Otherwise tightly packed mineral fields can toggle the SCV
        // between two touching patches every tick without ever mining.
        if (IsWithinHarvestRange(EM, HarvesterID, Harvest.TargetResourceID))
        {
            Path->BlockedByEntityID = -1;
            return false;
        }

        const int BlockingMineralID = Path->BlockedByEntityID;
        const auto* TargetResource =
            EM.GetComponent<ResourceComponent>(Harvest.TargetResourceID);
        auto* BlockingResource =
            EM.GetComponent<ResourceComponent>(BlockingMineralID);
        const auto* BlockingTransform =
            EM.GetComponent<TransformComponent>(BlockingMineralID);
        if (!TargetResource || TargetResource->Type != ResourceType::Minerals ||
            !BlockingResource ||
            BlockingResource->Type != ResourceType::Minerals ||
            BlockingResource->IsDepleted || BlockingResource->Amount <= 0 ||
            !BlockingTransform ||
            !IsWithinHarvestRange(EM, HarvesterID, BlockingMineralID))
        {
            return false;
        }

        // The tile pathfinder can accept a pixel target whose tile center is
        // clear even though the exact segment intersects an adjacent mineral's
        // DAT collision bounds. Movement reports that precise blocker. Do not
        // make every mineral non-colliding; gather (or wait) at the live patch
        // the worker has actually reached instead of retrying the unreachable
        // patch behind it forever.
        const int PreviousResourceID = Harvest.TargetResourceID;
        ReleaseResourceAssignment(EM, PreviousResourceID, HarvesterID);
        PruneQueuedGatherers(EM, BlockingMineralID, *BlockingResource);
        const bool BlockingPatchBusy =
            BlockingResource->ActiveGathererEntityID >= 0 ||
            !BlockingResource->QueuedGatherers.empty();
        if (BlockingPatchBusy)
        {
            const int Alternative = FindAvailableNearbyMineral(
                EM, HarvesterID, BlockingMineralID, PreviousResourceID);
            const auto* AlternativeTransform = Alternative >= 0
                ? EM.GetComponent<TransformComponent>(Alternative)
                : nullptr;
            if (AlternativeTransform)
            {
                Harvest.TargetResourceID = Alternative;
                Harvest.DesiredResourceID = Alternative;
                Harvest.State = HarvestState::MovingToResource;
                Harvest.QueuedAtResource = false;
                QueueGathererForResource(EM, Alternative, HarvesterID);
                if (IssueHarvestMove(
                        EM, HarvesterID, Alternative,
                        AlternativeTransform->Position))
                {
                    DebugHarvestLog(
                        "blocked-mineral-spread worker=" +
                        std::to_string(HarvesterID) +
                        " old=" + std::to_string(PreviousResourceID) +
                        " blocker=" + std::to_string(BlockingMineralID) +
                        " new=" + std::to_string(Alternative));
                    return true;
                }
                ReleaseResourceAssignment(EM, Alternative, HarvesterID);
            }
        }
        Harvest.TargetResourceID = BlockingMineralID;
        Harvest.DesiredResourceID = BlockingMineralID;
        Harvest.QueuedAtResource = true;
        QueueGathererForResource(EM, BlockingMineralID, HarvesterID);
        PruneQueuedGatherers(EM, BlockingMineralID, *BlockingResource);
        RestoreHarvestCommand(
            EM, HarvesterID, BlockingMineralID, BlockingTransform->Position);

        ClearMotion(EM, HarvesterID);
        if (auto* UpdatedPath =
                EM.GetComponent<PathfindingComponent>(HarvesterID))
        {
            const auto* HarvesterTransform =
                EM.GetComponent<TransformComponent>(HarvesterID);
            UpdatedPath->RequestedDestination = HarvesterTransform
                ? HarvesterTransform->Position
                : BlockingTransform->Position;
            UpdatedPath->RequestedTargetEntityID = -1;
            UpdatedPath->RequestedIgnoreEntityID = BlockingMineralID;
            UpdatedPath->RepathFailCount = 0;
            UpdatedPath->BlockedByEntityID = -1;
            UpdatedPath->HasRecentMoveFailure = false;
            UpdatedPath->RecentMoveFailureCooldown = 0.0f;
            UpdatedPath->DestinationStallTimer = 0.0f;
            UpdatedPath->HasLastDestinationDistance = false;
        }

        if (CanClaimResource(EM, BlockingMineralID, HarvesterID))
        {
            ClaimResource(EM, BlockingMineralID, HarvesterID);
            Harvest.State = HarvestState::Mining;
            Harvest.QueuedAtResource = false;
            Harvest.MineTimer = 0.0f;
        }
        else
        {
            Harvest.State = HarvestState::QueuedAtResource;
        }

        DebugHarvestLog(
            "blocked-mineral-reroute worker=" + std::to_string(HarvesterID) +
            " old=" + std::to_string(PreviousResourceID) +
            " blocker=" + std::to_string(BlockingMineralID));
        return true;
    }

    bool IsOwnedGasTarget(Core::ECS::EntityManager& EM, int ResourceID, int PlayerID)
    {
        auto* Resource = EM.GetComponent<ResourceComponent>(ResourceID);
        if (!Resource || Resource->Type != ResourceType::VespeneGas)
            return false;
        auto* Building = EM.GetComponent<BuildingComponent>(ResourceID);
        auto* Owner = EM.GetComponent<PlayerComponent>(ResourceID);
        auto* Unit = EM.GetComponent<UnitComponent>(ResourceID);
        return Building && Unit && Unit->HitPoints > 0 &&
               !Building->UnderConstruction &&
               Owner && Owner->PlayerID == PlayerID;
    }

    int FindReplacementResource(Core::ECS::EntityManager& EM,
                                int HarvesterID,
                                ResourceType Type,
                                int PreferredResourceID,
                                int PlayerID)
    {
        auto* HarvesterTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        if (!HarvesterTrans)
            return -1;

        if (PreferredResourceID >= 0)
        {
            auto* Preferred = EM.GetComponent<ResourceComponent>(PreferredResourceID);
            if (Preferred &&
                Preferred->Type == Type &&
                (Type == ResourceType::VespeneGas ||
                 (!Preferred->IsDepleted && Preferred->Amount > 0)) &&
                (Type != ResourceType::VespeneGas ||
                 IsOwnedGasTarget(EM, PreferredResourceID, PlayerID)))
            {
                return PreferredResourceID;
            }
        }

        int BestID = -1;
        float BestDistance = std::numeric_limits<float>::max();
        for (int CandidateID : EM.GetEntitiesWithCached<TransformComponent, ResourceComponent>())
        {
            auto* Resource = EM.GetComponent<ResourceComponent>(CandidateID);
            auto* CandidateTrans = EM.GetComponent<TransformComponent>(CandidateID);
            if (!Resource || !CandidateTrans ||
                (Type == ResourceType::Minerals &&
                 (Resource->IsDepleted || Resource->Amount <= 0)))
                continue;
            if (Resource->Type != Type)
                continue;
            if (Type == ResourceType::VespeneGas &&
                !IsOwnedGasTarget(EM, CandidateID, PlayerID))
            {
                continue;
            }

            const float Distance =
                HarvesterTrans->Position.DistanceTo(CandidateTrans->Position);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                BestID = CandidateID;
            }
        }
        return BestID;
    }

    bool RetargetHarvestResource(Core::ECS::EntityManager& EM,
                                 int HarvesterID,
                                 HarvestComponent& Harvest,
                                 ResourceType Type,
                                 int PlayerID,
                                 int DepartureIgnoreEntityID = -1,
                                 bool ExcludePreviousResource = false)
    {
        if (Harvest.DesiredResourceID >= 0)
        {
            if (const auto* DesiredRes = EM.GetComponent<ResourceComponent>(Harvest.DesiredResourceID))
            {
                if (DesiredRes->Type == ResourceType::VespeneGas &&
                    IsOwnedGasTarget(EM, Harvest.DesiredResourceID, PlayerID))
                {
                    Type = ResourceType::VespeneGas;
                }
                else if (DesiredRes->Type == ResourceType::Minerals &&
                         !DesiredRes->IsDepleted && DesiredRes->Amount > 0)
                {
                    Type = ResourceType::Minerals;
                }
            }
        }
        const int PreviousResource = Harvest.TargetResourceID;
        if (Type == ResourceType::Minerals)
        {
            const auto* HarvesterTrans = EM.GetComponent<TransformComponent>(HarvesterID);
            if (!HarvesterTrans)
                return false;

            std::vector<std::pair<float, int>> Candidates;
            for (int CandidateID :
                 EM.GetEntitiesWithCached<TransformComponent, ResourceComponent>())
            {
                auto* Resource = EM.GetComponent<ResourceComponent>(CandidateID);
                auto* CandidateTrans = EM.GetComponent<TransformComponent>(CandidateID);
                if (!Resource || !CandidateTrans ||
                    (ExcludePreviousResource && CandidateID == PreviousResource) ||
                    Resource->Type != ResourceType::Minerals ||
                    Resource->IsDepleted || Resource->Amount <= 0)
                {
                    continue;
                }
                PruneQueuedGatherers(EM, CandidateID, *Resource);
                const size_t Load = Resource->QueuedGatherers.size() +
                    (Resource->ActiveGathererEntityID >= 0 ? 1u : 0u);
                const float PreferredBias =
                    CandidateID == Harvest.DesiredResourceID ? -48.0f : 0.0f;
                Candidates.emplace_back(
                    PreferredBias +
                        HarvesterTrans->Position.DistanceTo(CandidateTrans->Position) +
                        (float)Load * 96.0f,
                    CandidateID);
            }
            std::sort(Candidates.begin(), Candidates.end());

            constexpr int MaximumResourcePathAttempts = 8;
            int ResourcePathAttempts = 0;
            for (const auto& [Distance, CandidateID] : Candidates)
            {
                (void)Distance;
                if (ResourcePathAttempts >= MaximumResourcePathAttempts)
                    break;
                ++ResourcePathAttempts;
                auto* CandidateTrans = EM.GetComponent<TransformComponent>(CandidateID);
                if (!CandidateTrans)
                    continue;
                const int SavedTargetResourceID = Harvest.TargetResourceID;
                const HarvestState SavedState = Harvest.State;
                Harvest.TargetResourceID = CandidateID;
                Harvest.State = HarvestState::MovingToResource;
                const bool HasPath = IssueHarvestMove(
                    EM, HarvesterID, CandidateID, CandidateTrans->Position,
                    CandidateID, -1.0f, DepartureIgnoreEntityID);
                Harvest.TargetResourceID = SavedTargetResourceID;
                Harvest.State = SavedState;
                if (!HasPath)
                    continue;
                Harvest.TargetResourceID = CandidateID;
                Harvest.DesiredResourceID = CandidateID;
                Harvest.State = HarvestState::MovingToResource;
                Harvest.QueuedAtResource = false;
                QueueGathererForResource(EM, CandidateID, HarvesterID);
                if (PreviousResource != CandidateID)
                {
                    DebugHarvestLog(
                        "mineral-reroute worker=" + std::to_string(HarvesterID) +
                        " old=" + std::to_string(PreviousResource) +
                        " new=" + std::to_string(CandidateID));
                }
                return true;
            }
            return false;
        }

        const int Replacement = FindReplacementResource(
            EM,
            HarvesterID,
            Type,
            Harvest.DesiredResourceID,
            PlayerID);
        if (Replacement < 0)
            return false;

        Harvest.TargetResourceID = Replacement;
        Harvest.DesiredResourceID = Replacement;
        Harvest.State = HarvestState::MovingToResource;
        Harvest.QueuedAtResource = false;
        if (auto* ResourceTrans = EM.GetComponent<TransformComponent>(Replacement))
        {
            QueueGathererForResource(EM, Replacement, HarvesterID);
            if (Type == ResourceType::VespeneGas)
            {
                auto* Resource = EM.GetComponent<ResourceComponent>(Replacement);
                size_t QueueIndex = 0;
                if (Resource)
                {
                    const auto It = std::find(
                        Resource->QueuedGatherers.begin(),
                        Resource->QueuedGatherers.end(),
                        HarvesterID);
                    if (It != Resource->QueuedGatherers.end())
                    {
                        QueueIndex = (size_t)std::distance(
                            Resource->QueuedGatherers.begin(), It);
                    }
                    if (Resource->ActiveGathererEntityID >= 0)
                        ++QueueIndex;
                }
                // Returning gas workers must rejoin the authored doorway
                // queue. The generic resource move targets the refinery's
                // blocked center and can leave the SCV permanently settled
                // outside after its first deposit.
                IssueGasQueueMove(
                    EM, HarvesterID, Replacement, QueueIndex,
                    DepartureIgnoreEntityID);
            }
            else
            {
                IssueHarvestMove(
                    EM,
                    HarvesterID,
                    Replacement,
                    ResourceTrans->Position,
                    Replacement,
                    -1.0f,
                    DepartureIgnoreEntityID);
            }
        }
        return true;
    }
}

bool HarvestSystem::CanHarvest(const Core::ECS::EntityManager& EM,
                               int UnitID,
                               int TargetResourceID,
                               std::string* OutReason)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Resource = MutableEM.GetComponent<ResourceComponent>(TargetResourceID);
    if (!Resource)
    {
        SetReason(OutReason, "target is not an active resource");
        return false;
    }

    auto* Unit = MutableEM.GetComponent<UnitComponent>(UnitID);
    auto* Transform = MutableEM.GetComponent<TransformComponent>(UnitID);
    if (!Unit || !Transform)
    {
        SetReason(OutReason, "unit missing components");
        return false;
    }
    if (Unit->HitPoints <= 0)
    {
        SetReason(OutReason, "worker is not alive");
        return false;
    }

    if (!IsWorkerChkUnitID(Unit->ChkUnitID))
    {
        SetReason(OutReason, "only worker units can harvest (SCV/Drone/Probe)");
        return false;
    }

    if (Resource->Type == ResourceType::Minerals)
    {
        if (Resource->IsDepleted || Resource->Amount <= 0)
        {
            SetReason(OutReason, "mineral field is depleted");
            return false;
        }
        return true;
    }

    if (Resource->Type != ResourceType::VespeneGas)
    {
        SetReason(OutReason, "unsupported resource type");
        return false;
    }

    auto* Building = MutableEM.GetComponent<BuildingComponent>(TargetResourceID);
    auto* ResourceOwner = MutableEM.GetComponent<PlayerComponent>(TargetResourceID);
    auto* WorkerOwner = MutableEM.GetComponent<PlayerComponent>(UnitID);
    const int WorkerPlayerID = WorkerOwner ? WorkerOwner->PlayerID : 0;

    // Accept Refinery, Extractor, or Assimilator
    if (!Building || Building->UnderConstruction)
    {
        SetReason(OutReason, "gas must be harvested from a completed refinery/extractor/assimilator");
        return false;
    }
    if (!ResourceOwner || ResourceOwner->PlayerID != WorkerPlayerID)
    {
        SetReason(OutReason, "refinery belongs to another player");
        return false;
    }
    return true;
}

bool HarvestSystem::IssueHarvest(Core::ECS::EntityManager& EM,
                                 const std::vector<int>& Units,
                                 int TargetResourceID)
{
    bool IssuedAny = false;
    auto* RequestedResource =
        EM.GetComponent<ResourceComponent>(TargetResourceID);
    for (int ID : Units)
    {
        int AssignedResourceID = TargetResourceID;
        if (RequestedResource &&
            RequestedResource->Type == ResourceType::Minerals)
        {
            PruneQueuedGatherers(EM, TargetResourceID, *RequestedResource);
            const size_t RequestedLoad =
                RequestedResource->QueuedGatherers.size() +
                (RequestedResource->ActiveGathererEntityID >= 0 ? 1u : 0u);
            if (RequestedLoad > 0)
            {
                AssignedResourceID =
                    FindBalancedNearbyMineral(EM, ID, TargetResourceID);
            }
        }

        if (!CanHarvest(EM, ID, AssignedResourceID))
            continue;

        auto* ResTarget =
            EM.GetComponent<TransformComponent>(AssignedResourceID);
        auto* Harvest = EM.GetComponent<HarvestComponent>(ID);
        if (!Harvest)
            Harvest = &EM.AddComponent<HarvestComponent>(ID);

        LeaveGasBuilding(EM, ID, *Harvest);
        ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
        Harvest->State = HarvestState::MovingToResource;
        Harvest->TargetResourceID = AssignedResourceID;
        Harvest->DesiredResourceID = AssignedResourceID;
        Harvest->ReturnBuildingID = -1;
        Harvest->QueuedAtResource = false;

        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::Harvest;
        Cmd->TargetEntityID = AssignedResourceID;

        // Reassigning a loaded worker must never delete its visible cargo.
        // Brood War returns the carried minerals/gas first, then continues
        // with the newly selected resource target.
        if (Harvest->CarryingAmount > 0 &&
            Harvest->CarryingType != ResourceType::None)
        {
            Harvest->ReturnBuildingID = FindNearestDepot(
                EM, ID, Harvest->CarryingType);
            if (Harvest->ReturnBuildingID >= 0)
            {
                Harvest->State = HarvestState::MovingToDepot;
                IssueDepotReturnMove(
                    EM, ID, AssignedResourceID, Harvest->ReturnBuildingID);
            }
            else
            {
                Harvest->State = HarvestState::Idle;
                ClearMotion(EM, ID);
            }
            IssuedAny = true;
            continue;
        }

        if (ResTarget)
        {
            QueueGathererForResource(EM, AssignedResourceID, ID);
            auto* Resource =
                EM.GetComponent<ResourceComponent>(AssignedResourceID);
            size_t QueueIndex = 0;
            if (Resource)
            {
                const auto QueueIt = std::find(
                    Resource->QueuedGatherers.begin(),
                    Resource->QueuedGatherers.end(), ID);
                if (QueueIt != Resource->QueuedGatherers.end())
                {
                    QueueIndex = (size_t)std::distance(
                        Resource->QueuedGatherers.begin(), QueueIt);
                }
            }
            if (Resource && Resource->Type == ResourceType::VespeneGas)
            {
                IssueGasQueueMove(
                    EM,
                    ID,
                    AssignedResourceID,
                    QueueIndex);
            }
            else
            {
                IssueHarvestMove(
                    EM, ID, AssignedResourceID, ResTarget->Position);
            }
            DebugHarvestLog(
                "issue worker=" + std::to_string(ID) +
                " requested=" + std::to_string(TargetResourceID) +
                " resource=" + std::to_string(AssignedResourceID) +
                " queue=" + std::to_string(QueueIndex));
        }

        IssuedAny = true;
    }
    return IssuedAny;
}

void HarvestSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f)
        return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<TransformComponent, HarvestComponent>();

    for (auto ID : Entities)
        ProcessHarvester(EM, ID, DeltaTime);
}

int HarvestSystem::FindNearestDepot(Core::ECS::EntityManager& EM,
                                    int HarvesterID,
                                    ResourceType Type,
                                    int ExcludedDepotID)
{
    auto* HarvesterTrans = EM.GetComponent<TransformComponent>(HarvesterID);
    auto* HarvesterPlayer = EM.GetComponent<PlayerComponent>(HarvesterID);
    if (!HarvesterTrans)
        return -1;

    const int PlayerID = HarvesterPlayer ? HarvesterPlayer->PlayerID : 0;
    int BestID = -1;
    float BestDist = std::numeric_limits<float>::max();

    // Real CHK maps and synthesized starts do not always carry identical
    // building metadata. Enumerate owned unit entities, then identify town
    // halls from their canonical unit definition in IsValidDepotForReturn.
    const auto& Depots = EM.GetEntitiesWithCached<TransformComponent, UnitComponent, PlayerComponent>();
    for (int DepotID : Depots)
    {
        if (DepotID == ExcludedDepotID)
            continue;
        auto* DepotTrans = EM.GetComponent<TransformComponent>(DepotID);
        if (!DepotTrans || !IsValidDepotForReturn(EM, DepotID, PlayerID, Type))
            continue;

        float Dist = HarvesterTrans->Position.DistanceTo(DepotTrans->Position);
        if (Dist < BestDist)
        {
            BestDist = Dist;
            BestID = DepotID;
        }
    }

    return BestID;
}

int HarvestSystem::ResumeNearbyHarvestersForDepot(Core::ECS::EntityManager& EM,
                                                   int DepotID)
{
    const auto* DepotTrans = EM.GetComponent<TransformComponent>(DepotID);
    const auto* DepotPlayer = EM.GetComponent<PlayerComponent>(DepotID);
    if (!DepotTrans || !DepotPlayer ||
        !IsValidDepotForReturn(EM, DepotID, DepotPlayer->PlayerID,
                               ResourceType::Minerals))
    {
        return 0;
    }

    constexpr float kResumeRadiusPixels = 16.0f * 32.0f;
    int Resumed = 0;
    static thread_local std::vector<int> sNearbyHarvesterCandidates;
    auto& SpatialRes = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
    SpatialRes.AllUnitsHash.QueryInto(DepotTrans->Position.X - kResumeRadiusPixels, DepotTrans->Position.Y - kResumeRadiusPixels, DepotTrans->Position.X + kResumeRadiusPixels, DepotTrans->Position.Y + kResumeRadiusPixels, sNearbyHarvesterCandidates);
    std::sort(sNearbyHarvesterCandidates.begin(), sNearbyHarvesterCandidates.end());
    for (int WorkerID : sNearbyHarvesterCandidates)
    {
        const auto* WorkerTrans = EM.GetComponent<TransformComponent>(WorkerID);
        const auto* WorkerPlayer = EM.GetComponent<PlayerComponent>(WorkerID);
        auto* Harvest = EM.GetComponent<HarvestComponent>(WorkerID);
        if (!WorkerTrans || !WorkerPlayer || !Harvest ||
            WorkerPlayer->PlayerID != DepotPlayer->PlayerID ||
            Harvest->State != HarvestState::Idle ||
            WorkerTrans->Position.DistanceTo(DepotTrans->Position) >
                kResumeRadiusPixels)
        {
            continue;
        }

        // Explicit Stop/Move clears both IDs in CommandSystem::InterruptHarvest.
        // The automatic no-depot path deliberately preserves them.
        int ResourceID = Harvest->TargetResourceID;
        if (ResourceID < 0)
            ResourceID = Harvest->DesiredResourceID;
        if (ResourceID < 0 || !EM.IsAlive(ResourceID))
            continue;

        if (IssueHarvest(EM, {WorkerID}, ResourceID))
            ++Resumed;
    }
    return Resumed;
}

void HarvestSystem::ProcessHarvester(Core::ECS::EntityManager& EM, int ID, float DeltaTime)
{
    auto* Trans = EM.GetComponent<TransformComponent>(ID);
    auto* Harvest = EM.GetComponent<HarvestComponent>(ID);
    auto* Unit = EM.GetComponent<UnitComponent>(ID);
    auto* WorkerOwner = EM.GetComponent<PlayerComponent>(ID);
    if (!Trans || !Harvest)
        return;
    if (Harvest->PathRetryTicks > 0)
        --Harvest->PathRetryTicks;
    if (!Unit || Unit->HitPoints <= 0)
    {
        LeaveGasBuilding(EM, ID, *Harvest);
        ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
        Harvest->State = HarvestState::Idle;
        StopHarvestCommand(EM, ID);
        return;
    }

    const int WorkerPlayerID = WorkerOwner ? WorkerOwner->PlayerID : 0;
    const auto StopHarvesting = [&]()
    {
        LeaveGasBuilding(EM, ID, *Harvest);
        ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
        Harvest->State = HarvestState::Idle;
        Harvest->QueuedAtResource = false;
        StopHarvestCommand(EM, ID);
    };

    switch (Harvest->State)
    {
        case HarvestState::Idle:
            break;

        case HarvestState::QueuedAtResource:
        case HarvestState::MovingToResource:
        {
            if (!CanHarvest(EM, ID, Harvest->TargetResourceID))
            {
                auto* OldResource = EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID);
                const ResourceType DesiredType =
                    OldResource ? OldResource->Type : ResourceType::Minerals;
                ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
                if (!RetargetHarvestResource(EM, ID, *Harvest, DesiredType, WorkerPlayerID))
                    StopHarvesting();
                break;
            }

            auto* ResTarget = EM.GetComponent<TransformComponent>(Harvest->TargetResourceID);
            auto* ResComp = EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID);
            if (!ResTarget || !ResComp)
            {
                StopHarvesting();
                break;
            }

            auto* Path = EM.GetComponent<PathfindingComponent>(ID);
            if (RefreshResourceCollisionIgnoreAfterDepotDeparture(
                    EM, ID, Harvest->TargetResourceID))
            {
                Path = EM.GetComponent<PathfindingComponent>(ID);
            }
            if (RetargetBlockedMineralApproach(EM, ID, *Harvest, Path))
                break;
            QueueGathererForResource(EM, Harvest->TargetResourceID, ID);
            PruneQueuedGatherers(EM, Harvest->TargetResourceID, *ResComp);
            const bool InRange = IsWithinHarvestRange(EM, ID, Harvest->TargetResourceID);
            const bool HasGatherTurn =
                ResComp->ActiveGathererEntityID == ID ||
                (ResComp->ActiveGathererEntityID < 0 &&
                 (!ResComp->QueuedGatherers.empty() &&
                  (ResComp->QueuedGatherers.front() == ID ||
                   (InRange && !IsWithinHarvestRange(EM, ResComp->QueuedGatherers.front(), Harvest->TargetResourceID)))));
            if (ResComp->Type == ResourceType::VespeneGas)
            {
                size_t QueueIndex = 0;
                const auto QueueIt = std::find(
                    ResComp->QueuedGatherers.begin(),
                    ResComp->QueuedGatherers.end(), ID);
                if (QueueIt != ResComp->QueuedGatherers.end())
                {
                    QueueIndex = (size_t)std::distance(
                        ResComp->QueuedGatherers.begin(), QueueIt);
                }
                const bool AtRefineryEdge =
                    IsWithinHarvestRange(EM, ID, Harvest->TargetResourceID);
                const Core::Math::Vec2 EdgeTarget =
                    ResolveGasPortPosition(EM, ID, Harvest->TargetResourceID);

                if (DebugHarvestEnabled())
                {
                    static std::unordered_map<int, float> GasTraceElapsed;
                    float& Elapsed = GasTraceElapsed[ID];
                    Elapsed += DeltaTime;
                    if (Elapsed >= 1.0f)
                    {
                        Elapsed = 0.0f;
                        DebugHarvestLog(
                            "gas-progress worker=" + std::to_string(ID) +
                            " pos=" + std::to_string((int)Trans->Position.X) + "," +
                            std::to_string((int)Trans->Position.Y) +
                            " edgeTarget=" + std::to_string((int)EdgeTarget.X) + "," +
                            std::to_string((int)EdgeTarget.Y) +
                            " edgeDist=" + std::to_string((int)EntityEdgeDistance(
                                EM, Trans->Position, Harvest->TargetResourceID)) +
                            " active=" + std::to_string(ResComp->ActiveGathererEntityID) +
                            " queueIndex=" + std::to_string(QueueIndex) +
                            " settled=" + std::to_string(Path ? (int)IsMoveSettled(Path) : -1));
                    }
                }

                const bool AlreadyApproachingRefinery =
                    Path && !IsMoveSettled(Path) &&
                    (Path->RequestedDestination.DistanceTo(EdgeTarget) <= 1.0f ||
                     EntityEdgeDistance(
                         EM, Path->RequestedDestination, Harvest->TargetResourceID) <= 32.0f);

                if (!HasGatherTurn)
                {
                    Harvest->State = AtRefineryEdge
                        ? HarvestState::QueuedAtResource
                        : HarvestState::MovingToResource;
                    Harvest->QueuedAtResource = AtRefineryEdge;
                    if (AtRefineryEdge)
                    {
                        ClearMotion(EM, ID);
                    }
                    else if (!AlreadyApproachingRefinery)
                    {
                        IssueGasQueueMove(
                            EM,
                            ID,
                            Harvest->TargetResourceID,
                            QueueIndex);
                    }
                    break;
                }

                if (!AtRefineryEdge)
                {
                    Harvest->State = HarvestState::MovingToResource;
                    Harvest->QueuedAtResource = false;
                    if (!AlreadyApproachingRefinery)
                    {
                        IssueGasQueueMove(
                            EM,
                            ID,
                            Harvest->TargetResourceID,
                            QueueIndex);
                    }
                    break;
                }

                ClaimResource(EM, Harvest->TargetResourceID, ID);
                Harvest->GasExitPosition = ResolveGasExitPosition(
                    EM, ID, Harvest->TargetResourceID);
                ClearMotion(EM, ID);
                if (auto* Render = EM.GetComponent<RenderComponent>(ID))
                    Render->Visible = false;
                Harvest->InsideGasBuilding = true;
                Harvest->State = HarvestState::Mining;
                Harvest->QueuedAtResource = false;
                RecordHarvestPathResult(*Harvest, true);
                Harvest->MineTimer = 0.0f;
                DebugHarvestLog(
                    "gas-enter worker=" + std::to_string(ID) +
                    " refinery=" + std::to_string(Harvest->TargetResourceID) +
                    " entry=" + std::to_string((int)Trans->Position.X) + "," +
                    std::to_string((int)Trans->Position.Y) +
                    " exit=" + std::to_string((int)Harvest->GasExitPosition.X) + "," +
                    std::to_string((int)Harvest->GasExitPosition.Y));
                break;
            }
            if (!HasGatherTurn)
            {
                // OpenBW redirects a worker which has reached a busy mineral
                // to the nearest unused patch in the same eight-tile cluster.
                // This keeps a mineral line spread across its patches while
                // preserving a FIFO wait queue when every patch is occupied.
                if (ResComp->Type == ResourceType::Minerals &&
                    !Harvest->QueuedAtResource &&
                    IsWithinHarvestRange(EM, ID, Harvest->TargetResourceID))
                {
                    const int Alternative = FindAvailableNearbyMineral(
                        EM, ID, Harvest->TargetResourceID);
                    if (Alternative >= 0)
                    {
                        const int PreviousResource = Harvest->TargetResourceID;
                        ReleaseResourceAssignment(EM, PreviousResource, ID);
                        Harvest->TargetResourceID = Alternative;
                        Harvest->DesiredResourceID = Alternative;
                        Harvest->State = HarvestState::MovingToResource;
                        Harvest->QueuedAtResource = false;
                        QueueGathererForResource(EM, Alternative, ID);
                        if (const auto* AlternativeTrans =
                                EM.GetComponent<TransformComponent>(Alternative))
                        {
                            IssueHarvestMove(
                                EM, ID, Alternative, AlternativeTrans->Position);
                        }
                        DebugHarvestLog(
                            "spread worker=" + std::to_string(ID) +
                            " from=" + std::to_string(PreviousResource) +
                            " to=" + std::to_string(Alternative));
                        return;
                    }
                }

                // OpenBW lets every gathering worker reach the resource. Only
                // workers already at the patch enter WaitForMinerals; sending
                // distant workers to artificial ring slots can select an
                // unreachable tile and deadlock the whole queue.
                if (IsWithinHarvestRange(EM, ID, Harvest->TargetResourceID))
                {
                    Harvest->State = HarvestState::QueuedAtResource;
                    Harvest->QueuedAtResource = true;
                    ClearMotion(EM, ID);
                }
                else
                {
                    Harvest->State = HarvestState::MovingToResource;
                    Harvest->QueuedAtResource = false;
                    if (IsMoveSettled(Path) && Harvest->PathRetryTicks == 0)
                    {
                        const bool HasPath = IssueHarvestMove(
                            EM, ID, Harvest->TargetResourceID, ResTarget->Position);
                        RecordHarvestPathResult(*Harvest, HasPath, EM.GetComponent<PathfindingComponent>(ID));
                        if (!HasPath &&
                            Harvest->ConsecutivePathFailures >= 3 &&
                            ResComp->Type == ResourceType::Minerals)
                        {
                            const int PreviousResourceID = Harvest->TargetResourceID;
                            if (RetargetHarvestResource(
                                    EM, ID, *Harvest, ResComp->Type,
                                    WorkerPlayerID, -1, true))
                            {
                                ReleaseResourceAssignment(
                                    EM, PreviousResourceID, ID);
                                RecordHarvestPathResult(*Harvest, true);
                            }
                            else if (Harvest->ConsecutivePathFailures >= 6)
                            {
                                Harvest->State = HarvestState::Idle;
                                ReleaseResourceAssignment(
                                    EM, PreviousResourceID, ID);
                            }
                        }
                    }
                }
                break;
            }

            if (IsWithinHarvestRange(EM, ID, Harvest->TargetResourceID))
            {
                if (CanClaimResource(EM, Harvest->TargetResourceID, ID))
                {
                    ClaimResource(EM, Harvest->TargetResourceID, ID);
                    Harvest->State = HarvestState::Mining;
                    Harvest->QueuedAtResource = false;
                    RecordHarvestPathResult(*Harvest, true);
                    Harvest->MineTimer = 0.0f;
                    ClearMotion(EM, ID);
                    DebugHarvestLog(
                        "mining worker=" + std::to_string(ID) +
                        " resource=" + std::to_string(Harvest->TargetResourceID));
                }
                else
                {
                    Harvest->State = HarvestState::QueuedAtResource;
                    Harvest->QueuedAtResource = true;
                    ClearMotion(EM, ID);
                }
                break;
            }

            if (Harvest->State == HarvestState::QueuedAtResource)
            {
                Harvest->State = HarvestState::MovingToResource;
                Harvest->QueuedAtResource = false;
                IssueHarvestMove(EM, ID, Harvest->TargetResourceID, ResTarget->Position);
                return;
            }

            if (IsMoveSettled(Path) && Harvest->PathRetryTicks == 0)
            {
                Harvest->State = HarvestState::MovingToResource;
                Harvest->QueuedAtResource = ResComp->ActiveGathererEntityID >= 0 &&
                    ResComp->ActiveGathererEntityID != ID;
                const bool HasPath = IssueHarvestMove(
                    EM, ID, Harvest->TargetResourceID, ResTarget->Position);
                RecordHarvestPathResult(*Harvest, HasPath, EM.GetComponent<PathfindingComponent>(ID));
                if (!HasPath &&
                    Harvest->ConsecutivePathFailures >= 3 &&
                    ResComp->Type == ResourceType::Minerals)
                {
                    const int PreviousResourceID = Harvest->TargetResourceID;
                    if (RetargetHarvestResource(
                            EM, ID, *Harvest, ResComp->Type,
                            WorkerPlayerID, -1, true))
                    {
                        ReleaseResourceAssignment(
                            EM, PreviousResourceID, ID);
                        RecordHarvestPathResult(*Harvest, true);
                    }
                    else if (Harvest->ConsecutivePathFailures >= 6)
                    {
                        Harvest->State = HarvestState::Idle;
                        ReleaseResourceAssignment(
                            EM, PreviousResourceID, ID);
                    }
                }
                return;
            }
            break;
        }

        case HarvestState::Mining:
        {
            auto* ResComp = EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID);
            const bool ExhaustedMineral = ResComp &&
                ResComp->Type == ResourceType::Minerals &&
                (ResComp->IsDepleted || ResComp->Amount <= 0);
            if (!ResComp || ExhaustedMineral ||
                !CanHarvest(EM, ID, Harvest->TargetResourceID))
            {
                LeaveGasBuilding(EM, ID, *Harvest);
                ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
                if (Harvest->CarryingAmount > 0)
                {
                    Harvest->State = HarvestState::MovingToDepot;
                    Harvest->CarryingType =
                        ResComp ? ResComp->Type : ResourceType::Minerals;
                    Harvest->ReturnBuildingID =
                        FindNearestDepot(EM, ID, Harvest->CarryingType);
                }
                else
                {
                    const ResourceType RetryType =
                        ResComp ? ResComp->Type : ResourceType::Minerals;
                    if (!RetargetHarvestResource(EM, ID, *Harvest, RetryType, WorkerPlayerID))
                        StopHarvesting();
                }
                break;
            }

            Harvest->MineTimer += DeltaTime;
            if (Harvest->MineTimer >= Harvest->MineInterval)
            {
                Harvest->MineTimer = 0.0f;
                int Gather = 0;
                if (ResComp->Type == ResourceType::VespeneGas && ResComp->Amount < 8)
                {
                    // Brood War/openbw keeps depleted refineries usable: each
                    // trip yields two gas forever once the stored amount drops
                    // below one normal eight-gas load.
                    ResComp->Amount = 0;
                    ResComp->IsDepleted = true;
                    Gather = 2;
                }
                else
                {
                    Gather = std::min(
                        Harvest->CarryCapacity - Harvest->CarryingAmount,
                        std::min(8, ResComp->Amount));
                }
                if (Gather > 0)
                {
                    Harvest->CarryingAmount += Gather;
                    if (!(ResComp->Type == ResourceType::VespeneGas &&
                          ResComp->IsDepleted))
                    {
                        ResComp->Amount -= Gather;
                        if (ResComp->Amount <= 0)
                        {
                            ResComp->Amount = 0;
                            ResComp->IsDepleted = true;
                        }
                    }
                }

                const bool GasTripComplete =
                    ResComp->Type == ResourceType::VespeneGas && Gather > 0;
                if (GasTripComplete ||
                    Harvest->CarryingAmount >= Harvest->CarryCapacity ||
                    (ResComp->Type == ResourceType::Minerals && ResComp->IsDepleted))
                {
                    LeaveGasBuilding(EM, ID, *Harvest);
                    ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
                    Harvest->State = HarvestState::MovingToDepot;
                    Harvest->CarryingType = ResComp->Type;
                    Harvest->ReturnBuildingID = FindNearestDepot(EM, ID, ResComp->Type);
                    Harvest->QueuedAtResource = false;
                    DebugHarvestLog(
                        "full worker=" + std::to_string(ID) +
                        " resource=" + std::to_string(Harvest->TargetResourceID) +
                        " depot=" + std::to_string(Harvest->ReturnBuildingID) +
                        " carry=" + std::to_string(Harvest->CarryingAmount));
                    if (Harvest->ReturnBuildingID != -1)
                    {
                        if (auto* DepotTrans =
                                EM.GetComponent<TransformComponent>(Harvest->ReturnBuildingID))
                        {
                            IssueDepotReturnMove(
                                EM, ID, Harvest->TargetResourceID,
                                Harvest->ReturnBuildingID);
                        }
                    }
                }
            }
            break;
        }

        case HarvestState::MovingToDepot:
        {
            if (Harvest->ReturnBuildingID == -1)
            {
                Harvest->ReturnBuildingID = FindNearestDepot(EM, ID, Harvest->CarryingType);
                if (Harvest->ReturnBuildingID == -1)
                {
                    Harvest->State = HarvestState::Idle;
                    break;
                }
                if (auto* DepotTrans =
                        EM.GetComponent<TransformComponent>(Harvest->ReturnBuildingID))
                {
                    IssueDepotReturnMove(
                        EM, ID, Harvest->TargetResourceID,
                        Harvest->ReturnBuildingID);
                }
                return;
            }
            if (!IsValidDepotForReturn(EM, Harvest->ReturnBuildingID,
                                       WorkerPlayerID, Harvest->CarryingType))
            {
                Harvest->ReturnBuildingID = FindNearestDepot(EM, ID, Harvest->CarryingType);
                if (Harvest->ReturnBuildingID == -1)
                {
                    StopHarvesting();
                    break;
                }
                if (auto* DepotTrans =
                        EM.GetComponent<TransformComponent>(Harvest->ReturnBuildingID))
                {
                    IssueDepotReturnMove(
                        EM, ID, Harvest->TargetResourceID,
                        Harvest->ReturnBuildingID);
                }
                return;
            }

            auto* Path = EM.GetComponent<PathfindingComponent>(ID);
            auto* DepotTrans = EM.GetComponent<TransformComponent>(Harvest->ReturnBuildingID);
            if (!DepotTrans)
            {
                Harvest->ReturnBuildingID = -1;
                break;
            }
            if (Path &&
                Path->RequestedTargetEntityID ==
                    Harvest->TargetResourceID &&
                IsMoveSettled(Path))
            {
                if (Harvest->PathRetryTicks == 0)
                {
                    const bool HasPath = IssueDepotReturnMove(
                        EM, ID, Harvest->TargetResourceID,
                        Harvest->ReturnBuildingID);
                    RecordHarvestPathResult(*Harvest, HasPath, EM.GetComponent<PathfindingComponent>(ID));
                }
                return;
            }

            // Resource delivery requires actual DAT collision-bound contact.
            // Path node tolerance is only a navigation acceptance detail and
            // must not enlarge the gameplay interaction range.
            constexpr float DepositRange = 1.0f;
            const float Dist = EntityEdgeDistance(
                EM, Trans->Position, Harvest->ReturnBuildingID);

            if (!Harvest->HasLastReturnProgressPosition)
            {
                Harvest->LastReturnProgressPosition = Trans->Position;
                Harvest->HasLastReturnProgressPosition = true;
                Harvest->ReturnNoProgressTicks = 0;
            }
            else if (Trans->Position.DistanceTo(
                         Harvest->LastReturnProgressPosition) >= 0.5f)
            {
                Harvest->LastReturnProgressPosition = Trans->Position;
                Harvest->ReturnNoProgressTicks = 0;
                Harvest->ReturnRecoveryAttempts = 0;
            }
            else
            {
                Harvest->ReturnNoProgressTicks =
                    std::min(Harvest->ReturnNoProgressTicks + 1, 240);
            }

            constexpr int ReturnNoProgressRecoveryTicks = 72;
            constexpr int AdjacentMineralEscapeTicks = 12;
            if (Dist > DepositRange && Path &&
                Harvest->ReturnNoProgressTicks >=
                    AdjacentMineralEscapeTicks &&
                IssueAdjacentMineralEscapeMove(
                    EM, ID, *Harvest, *Path))
            {
                Harvest->ReturnNoProgressTicks = 0;
                Harvest->LastReturnProgressPosition = Trans->Position;
                return;
            }
            if (Dist > DepositRange &&
                Harvest->ReturnNoProgressTicks >= ReturnNoProgressRecoveryTicks)
            {
                const int PreviousDepotID = Harvest->ReturnBuildingID;
                Harvest->ReturnNoProgressTicks = 0;
                Harvest->LastReturnProgressPosition = Trans->Position;
                Harvest->ReturnRecoveryAttempts =
                    std::min(Harvest->ReturnRecoveryAttempts + 1, 8);

                int RecoveryDepotID = FindNearestDepot(
                    EM, ID, Harvest->CarryingType, PreviousDepotID);
                if (RecoveryDepotID < 0)
                    RecoveryDepotID = PreviousDepotID;
                Harvest->ReturnBuildingID = RecoveryDepotID;

                if (Path)
                {
                    Path->Path.clear();
                    Path->PathIndex = 0;
                    Path->HasRecentMoveFailure = false;
                    Path->RecentMoveFailureCooldown = 0.0f;
                    Path->RecentMoveFailureTicks = 0;
                }

                const bool RecoveredPath = IssueDepotReturnMove(
                    EM, ID, Harvest->TargetResourceID, RecoveryDepotID);
                RecordHarvestPathResult(*Harvest, RecoveredPath);
                DebugHarvestLog(
                    "return-recover worker=" + std::to_string(ID) +
                    " oldDepot=" + std::to_string(PreviousDepotID) +
                    " depot=" + std::to_string(RecoveryDepotID) +
                    " edgeDist=" + std::to_string((int)Dist) +
                    " attempt=" +
                    std::to_string(Harvest->ReturnRecoveryAttempts) +
                    " hasPath=" + std::to_string(RecoveredPath ? 1 : 0));
                return;
            }
            DebugHarvestLog(
                "return worker=" + std::to_string(ID) +
                " depot=" + std::to_string(Harvest->ReturnBuildingID) +
                " resource=" + std::to_string(Harvest->TargetResourceID) +
                " pos=" + std::to_string((int)Trans->Position.X) + "," +
                std::to_string((int)Trans->Position.Y) +
                " dist=" + std::to_string((int)Dist) +
                " range=" + std::to_string((int)DepositRange) +
                " carry=" + std::to_string(Harvest->CarryingAmount) +
                " blocked=" +
                std::to_string(Path ? Path->BlockedByEntityID : -1) +
                " status=" +
                std::to_string(Path ? (int)Path->LastPathStatus : -1) +
                " pathSize=" +
                std::to_string(Path ? (int)Path->Path.size() : -1) +
                " pathIndex=" +
                std::to_string(Path ? Path->PathIndex : -1));
            // openbw completes ReturnMinerals when the worker reaches its
            // depot move target.  Keep our deeper delivery waypoint for the
            // visual pass through the CC entrance, but also accept the worker
            // center reaching the depot DAT boundary so a settled/repathed SCV
            // cannot loop forever at the entrance.
            if (IsInsideDepotDeliveryZone(EM, ID, Harvest->ReturnBuildingID) ||
                Dist <= DepositRange)
            {
                const ResourceType ReturnType = Harvest->CarryingType;
                const int DeliveredDepotID = Harvest->ReturnBuildingID;
                auto& Resources = GetResources(EM, WorkerPlayerID);
                int MinDelta = 0;
                int GasDelta = 0;
                if (Harvest->CarryingType == ResourceType::Minerals)
                {
                    MinDelta = Harvest->CarryingAmount;
                    Resources.Minerals += MinDelta;
                }
                else if (Harvest->CarryingType == ResourceType::VespeneGas)
                {
                    GasDelta = Harvest->CarryingAmount;
                    Resources.VespeneGas += GasDelta;
                }
                EM.Events().Emit(Game::Events::ResourceChangedEvent{
                    WorkerPlayerID, MinDelta, GasDelta, Resources.Minerals, Resources.VespeneGas});

                if (DebugHarvestEnabled())
                {
                    Core::Utils::Logger::Info("HarvestSystem",
                        "deposit worker=" + std::to_string(ID) +
                        " depot=" + std::to_string(Harvest->ReturnBuildingID) +
                        " amount=" + std::to_string(Harvest->CarryingAmount));
                }

                Harvest->CarryingAmount = 0;
                Harvest->CarryingType = ResourceType::None;
                Harvest->ReturnBuildingID = -1;
                Harvest->HasLastReturnProgressPosition = false;
                Harvest->ReturnNoProgressTicks = 0;
                Harvest->ReturnRecoveryAttempts = 0;
                RecordHarvestPathResult(*Harvest, true);
                const bool ResumedHarvest = RetargetHarvestResource(
                        EM,
                        ID,
                        *Harvest,
                        ReturnType,
                        WorkerPlayerID,
                        DeliveredDepotID);
                if (DebugHarvestEnabled() && ReturnType == ResourceType::VespeneGas)
                {
                    auto* Gas = EM.GetComponent<ResourceComponent>(
                        Harvest->TargetResourceID);
                    DebugHarvestLog(
                        "gas-resume worker=" + std::to_string(ID) +
                        " ok=" + std::to_string(ResumedHarvest ? 1 : 0) +
                        " target=" + std::to_string(Harvest->TargetResourceID) +
                        " amount=" + std::to_string(Gas ? Gas->Amount : -1) +
                        " depleted=" + std::to_string(Gas && Gas->IsDepleted ? 1 : 0));
                }
                if (!ResumedHarvest)
                {
                    StopHarvesting();
                }
            }
            else if (IsMoveSettled(Path) && Harvest->PathRetryTicks == 0)
            {
                const bool HasPath = IssueDepotReturnMove(
                    EM, ID, Harvest->TargetResourceID,
                    Harvest->ReturnBuildingID);
                RecordHarvestPathResult(*Harvest, HasPath, EM.GetComponent<PathfindingComponent>(ID));
                if (!HasPath && Harvest->ConsecutivePathFailures == 3)
                {
                    const int AlternativeDepotID = FindNearestDepot(
                        EM, ID, Harvest->CarryingType,
                        Harvest->ReturnBuildingID);
                    if (AlternativeDepotID >= 0)
                    {
                        Harvest->ReturnBuildingID = AlternativeDepotID;
                        RecordHarvestPathResult(*Harvest, true);
                        const bool AlternativeHasPath = IssueDepotReturnMove(
                            EM, ID, Harvest->TargetResourceID,
                            AlternativeDepotID);
                        RecordHarvestPathResult(
                            *Harvest, AlternativeHasPath);
                    }
                }
            }
            break;
        }

        case HarvestState::Depositing:
            break;
    }
}

} // namespace Game::Systems
