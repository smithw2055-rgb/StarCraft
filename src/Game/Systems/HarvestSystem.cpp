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
#include "Game/Systems/CommandSystem.h"

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    constexpr int TerranScvChkUnitID = 7;
    constexpr int ZergDroneChkUnitID = 41;
    constexpr int ProtossProbeChkUnitID = 64;

    bool IsWorkerChkUnitID(int ChkUnitID)
    {
        return ChkUnitID == TerranScvChkUnitID ||
               ChkUnitID == ZergDroneChkUnitID ||
               ChkUnitID == ProtossProbeChkUnitID;
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
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
        if (const auto* Building = MutableEM.GetComponent<BuildingComponent>(EntityID))
        {
            if (Building->BuildingDefID >= 0)
                return Building->BuildingDefID;
        }
        if (const auto* Unit = MutableEM.GetComponent<UnitComponent>(EntityID))
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
            Path->HasLastWaypointDistance = false;
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
            // Mineral gatherers are temporarily non-colliding in the same way
            // as openbw's gathering order, so let the SCV close the last few
            // pixels instead of leaving a full worker-radius visual gap.
            Margin = std::max(4.0f, WorkerRadius - 6.0f);
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
             EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>())
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

    bool IsWithinHarvestRange(Core::ECS::EntityManager& EM, int HarvesterID, int TargetResourceID)
    {
        auto* HarvesterTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetResourceID);
        if (!HarvesterTrans || !TargetTrans)
            return false;
        if (auto* TargetPath = EM.GetComponent<PathfindingComponent>(TargetResourceID);
            TargetPath && TargetPath->HasCollisionBounds)
        {
            const auto* WorkerPath = EM.GetComponent<PathfindingComponent>(HarvesterID);
            const float WorkerRadius = CollisionRadiusFromBounds(WorkerPath, 16.0f);
            // Movement accepts a final approach point within NodeTolerance.
            // Use the same tolerance for the resource contact test; otherwise
            // asymmetric DAT bounds can leave a worker parked at a completed
            // approach waypoint which is a few pixels outside the old range.
            const float ArrivalTolerance =
                WorkerPath ? std::max(2.0f, WorkerPath->NodeTolerance + 2.0f) : 2.0f;
            return EntityEdgeDistance(EM, HarvesterTrans->Position, TargetResourceID) <=
                   WorkerRadius + ArrivalTolerance;
        }
        return HarvesterTrans->Position.DistanceTo(TargetTrans->Position) <=
               HarvestInteractionRange(EM, TargetResourceID);
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

    void IssueDepotReturnMove(Core::ECS::EntityManager& EM,
                              int HarvesterID,
                              int TargetResourceID,
                              int DepotID)
    {
        const auto* DepotTrans = EM.GetComponent<TransformComponent>(DepotID);
        if (!DepotTrans)
            return;

        const Core::Math::Vec2 DeliveryTarget =
            ResolveDepotDeliveryTarget(EM, HarvesterID, DepotID);
        bool HasPath = CommandSystem::IssueMoveAndHasPath(
            EM, HarvesterID, DeliveryTarget, DepotID);
        RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, DepotTrans->Position);
        if (!HasPath && DeliveryTarget.DistanceTo(DepotTrans->Position) > 1.0f)
        {
            HasPath = CommandSystem::IssueMoveAndHasPath(
                EM, HarvesterID, DepotTrans->Position, DepotID);
            RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, DepotTrans->Position);
        }

        // A* can legitimately report a same-tile route with no waypoints.  A
        // returning worker still has to travel the final few pixels into the
        // depot, so preserve that short segment explicitly.  The movement
        // system ignores DepotID for both collision resolution and repulsion.
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
                Path->RequestedTargetEntityID = DepotID;
                Path->RequestedIgnoreEntityID = DepotID;
                Path->NodeTolerance = 2.0f;
                Path->RepathFailCount = 0;
                Path->LastRepathTime = 0.0f;
                Path->BlockedByEntityID = -1;
                Path->HasRecentMoveFailure = false;
                Path->RecentMoveFailureCooldown = 0.0f;
                Path->StallTimer = 0.0f;
                Path->HasLastWaypointDistance = false;
                Path->DestinationStallTimer = 0.0f;
                Path->HasLastDestinationDistance = false;
                RestoreHarvestCommand(
                    EM, HarvesterID, TargetResourceID, DepotTrans->Position);
            }
        }
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
        const int IgnoreID = PathingIgnoreEntityID >= 0
            ? PathingIgnoreEntityID
            : ApproachID;
        const auto Candidates = BuildApproachCandidates(
            EM, HarvesterID, TargetResourceID, TargetPosition, ApproachID, ApproachRange);
        const Core::Math::Vec2 MoveTarget =
            Candidates.empty() ? TargetPosition : Candidates.front();

        auto TryMove = [&](const Core::Math::Vec2& Candidate)
        {
            const bool HasPath = CommandSystem::IssueMoveAndHasPath(
                EM,
                HarvesterID,
                Candidate,
                IgnoreID);
            RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, TargetPosition);
            return HasPath;
        };

        bool HasIssuedReachableMove = false;
        for (const auto& Candidate : Candidates)
        {
            if (IsRecentFailedMoveTarget(
                    EM.GetComponent<PathfindingComponent>(HarvesterID), Candidate))
            {
                continue;
            }
            if (TryMove(Candidate))
            {
                HasIssuedReachableMove = true;
                break;
            }
        }

        if (!HasIssuedReachableMove)
        {
            if (auto* Path = EM.GetComponent<PathfindingComponent>(HarvesterID))
                Path->HasRecentMoveFailure = false;
            for (const auto& Candidate : Candidates)
            {
                if (TryMove(Candidate))
                {
                    HasIssuedReachableMove = true;
                    break;
                }
            }
        }

        if (!HasIssuedReachableMove)
            HasIssuedReachableMove = CommandSystem::IssueMoveAndHasPath(
                EM, HarvesterID, MoveTarget, IgnoreID);

        RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, TargetPosition);
        return HasIssuedReachableMove ||
            IsWithinHarvestRange(EM, HarvesterID, TargetResourceID);
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
        Resource.QueuedGatherers.erase(
            std::remove_if(
                Resource.QueuedGatherers.begin(),
                Resource.QueuedGatherers.end(),
                [&](int GathererID)
                {
                    if (!EM.IsAlive(GathererID))
                        return true;
                    auto* Harvest = EM.GetComponent<HarvestComponent>(GathererID);
                    return !Harvest || Harvest->TargetResourceID != ResourceID;
                }),
            Resource.QueuedGatherers.end());

        // openbw joins WaitForGas only after a worker reaches the refinery.
        // Our queue is created when the command is issued, so preserve the
        // same arrival-order behavior by choosing the worker nearest any edge
        // of the refinery whenever its gathering slot becomes free.
        if (Resource.Type == ResourceType::VespeneGas &&
            Resource.ActiveGathererEntityID < 0)
        {
            std::stable_sort(
                Resource.QueuedGatherers.begin(),
                Resource.QueuedGatherers.end(),
                [&](int A, int B)
                {
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

    int FindAvailableNearbyMineral(Core::ECS::EntityManager& EM,
                                   int HarvesterID,
                                   int CurrentResourceID)
    {
        const auto* WorkerTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        const auto* CurrentTrans = EM.GetComponent<TransformComponent>(CurrentResourceID);
        if (!WorkerTrans || !CurrentTrans)
            return -1;

        constexpr float ClusterRange = 32.0f * 8.0f;
        int BestID = -1;
        float BestDistance = std::numeric_limits<float>::max();
        for (int CandidateID : EM.GetEntitiesWith<TransformComponent, ResourceComponent>())
        {
            if (CandidateID == CurrentResourceID)
                continue;
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
        return Resource->QueuedGatherers.empty() ||
               Resource->QueuedGatherers.front() == HarvesterID;
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
        for (int CandidateID : EM.GetEntitiesWith<TransformComponent, ResourceComponent>())
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
                                 int DepartureIgnoreEntityID = -1)
    {
        const int PreviousResource = Harvest.TargetResourceID;
        if (Type == ResourceType::Minerals)
        {
            const auto* HarvesterTrans = EM.GetComponent<TransformComponent>(HarvesterID);
            if (!HarvesterTrans)
                return false;

            std::vector<std::pair<float, int>> Candidates;
            for (int CandidateID :
                 EM.GetEntitiesWith<TransformComponent, ResourceComponent>())
            {
                auto* Resource = EM.GetComponent<ResourceComponent>(CandidateID);
                auto* CandidateTrans = EM.GetComponent<TransformComponent>(CandidateID);
                if (!Resource || !CandidateTrans ||
                    Resource->Type != ResourceType::Minerals ||
                    Resource->IsDepleted || Resource->Amount <= 0)
                {
                    continue;
                }
                PruneQueuedGatherers(EM, CandidateID, *Resource);
                const size_t Load = Resource->QueuedGatherers.size() +
                    (Resource->ActiveGathererEntityID >= 0 ? 1u : 0u);
                const float PreferredBias =
                    CandidateID == Harvest.DesiredResourceID ? -1000000.0f : 0.0f;
                Candidates.emplace_back(
                    PreferredBias +
                        HarvesterTrans->Position.DistanceTo(CandidateTrans->Position) +
                        (float)Load * 96.0f,
                    CandidateID);
            }
            std::sort(Candidates.begin(), Candidates.end());

            for (const auto& [Distance, CandidateID] : Candidates)
            {
                (void)Distance;
                auto* CandidateTrans = EM.GetComponent<TransformComponent>(CandidateID);
                if (!CandidateTrans ||
                    !IssueHarvestMove(
                        EM, HarvesterID, CandidateID, CandidateTrans->Position,
                        CandidateID, -1.0f, DepartureIgnoreEntityID))
                {
                    continue;
                }
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
    auto* ResTarget = EM.GetComponent<TransformComponent>(TargetResourceID);
    for (int ID : Units)
    {
        if (!CanHarvest(EM, ID, TargetResourceID))
            continue;

        auto* Harvest = EM.GetComponent<HarvestComponent>(ID);
        if (!Harvest)
            Harvest = &EM.AddComponent<HarvestComponent>(ID);

        LeaveGasBuilding(EM, ID, *Harvest);
        ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
        Harvest->State = HarvestState::MovingToResource;
        Harvest->TargetResourceID = TargetResourceID;
        Harvest->DesiredResourceID = TargetResourceID;
        Harvest->ReturnBuildingID = -1;
        Harvest->QueuedAtResource = false;

        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::Harvest;
        Cmd->TargetEntityID = TargetResourceID;

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
                    EM, ID, TargetResourceID, Harvest->ReturnBuildingID);
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
            QueueGathererForResource(EM, TargetResourceID, ID);
            auto* Resource = EM.GetComponent<ResourceComponent>(TargetResourceID);
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
                    TargetResourceID,
                    QueueIndex);
            }
            else
            {
                IssueHarvestMove(EM, ID, TargetResourceID, ResTarget->Position);
            }
            DebugHarvestLog(
                "issue worker=" + std::to_string(ID) +
                " resource=" + std::to_string(TargetResourceID) +
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
                                    ResourceType Type)
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
    auto Depots = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
    for (int DepotID : Depots)
    {
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
    for (int WorkerID : EM.GetEntitiesWith<TransformComponent, HarvestComponent,
                                           PlayerComponent>())
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
            QueueGathererForResource(EM, Harvest->TargetResourceID, ID);
            PruneQueuedGatherers(EM, Harvest->TargetResourceID, *ResComp);
            const bool HasGatherTurn =
                ResComp->ActiveGathererEntityID == ID ||
                (ResComp->ActiveGathererEntityID < 0 &&
                 !ResComp->QueuedGatherers.empty() &&
                 ResComp->QueuedGatherers.front() == ID);
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
                    else if (!Path || IsMoveSettled(Path) ||
                             Path->RequestedDestination.DistanceTo(EdgeTarget) > 1.0f)
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
                    if (!Path || IsMoveSettled(Path) ||
                        Path->RequestedDestination.DistanceTo(EdgeTarget) > 1.0f)
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
                    if (IsMoveSettled(Path))
                    {
                        IssueHarvestMove(
                            EM, ID, Harvest->TargetResourceID, ResTarget->Position);
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

            if (IsMoveSettled(Path))
            {
                Harvest->State = HarvestState::MovingToResource;
                Harvest->QueuedAtResource = ResComp->ActiveGathererEntityID >= 0 &&
                    ResComp->ActiveGathererEntityID != ID;
                IssueHarvestMove(EM, ID, Harvest->TargetResourceID, ResTarget->Position);
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

            // Approach goals are accepted using the pathfinder's node tolerance.
            // Include that same tolerance here so a worker which has legitimately
            // reached the depot edge cannot remain in an endless return/repath loop.
            const float ArrivalTolerance =
                Path ? std::max(2.0f, Path->NodeTolerance + 2.0f) : 2.0f;
            const float DepositRange =
                WorkerContactRange(EM, ID, Path, ArrivalTolerance);
            const float Dist = EntityEdgeDistance(
                EM, Trans->Position, Harvest->ReturnBuildingID);
            DebugHarvestLog(
                "return worker=" + std::to_string(ID) +
                " depot=" + std::to_string(Harvest->ReturnBuildingID) +
                " dist=" + std::to_string((int)Dist) +
                " range=" + std::to_string((int)DepositRange) +
                " carry=" + std::to_string(Harvest->CarryingAmount) +
                " pathSize=" +
                std::to_string(Path ? (int)Path->Path.size() : -1) +
                " pathIndex=" +
                std::to_string(Path ? Path->PathIndex : -1));
            // openbw completes ReturnMinerals when the worker reaches its
            // depot move target.  Keep our deeper delivery waypoint for the
            // visual pass through the CC entrance, but also accept collision-
            // edge contact so a settled/repathed SCV cannot loop forever at
            // the depot boundary.
            if (IsInsideDepotDeliveryZone(EM, ID, Harvest->ReturnBuildingID) ||
                Dist <= DepositRange)
            {
                const ResourceType ReturnType = Harvest->CarryingType;
                const int DeliveredDepotID = Harvest->ReturnBuildingID;
                auto& Resources = GetResources(WorkerPlayerID);
                if (Harvest->CarryingType == ResourceType::Minerals)
                    Resources.Minerals += Harvest->CarryingAmount;
                else if (Harvest->CarryingType == ResourceType::VespeneGas)
                    Resources.VespeneGas += Harvest->CarryingAmount;

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
            else if (IsMoveSettled(Path))
            {
                IssueDepotReturnMove(
                    EM, ID, Harvest->TargetResourceID,
                    Harvest->ReturnBuildingID);
            }
            break;
        }

        case HarvestState::Depositing:
            break;
    }
}

} // namespace Game::Systems
