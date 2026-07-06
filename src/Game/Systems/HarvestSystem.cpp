#include "Game/Systems/HarvestSystem.h"

#include <algorithm>
#include <limits>
#include <cmath>
#include <array>
#include <cstdlib>
#include <string>
#include <vector>

#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    void SetReason(std::string* OutReason, const std::string& Reason)
    {
        if (OutReason)
            *OutReason = Reason;
    }

    float HarvestInteractionRange(const Core::ECS::EntityManager& EM, int TargetResourceID);

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
        const float Margin = std::max(0.0f, WorkerRadius - 1.0f);
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
        if (!Building || !Owner || !DepotTrans || !DepotUnit)
            return false;
        if (Building->UnderConstruction || DepotUnit->HitPoints <= 0)
            return false;
        if (Owner->PlayerID != PlayerID)
            return false;

        const int NexusDefID = Game::Races::GetUnitDefID("Nexus");
        const int CommandCenterDefID = Game::Races::GetUnitDefID("CommandCenter");
        const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
        const int LairDefID = Game::Races::GetUnitDefID("Lair");
        const int HiveDefID = Game::Races::GetUnitDefID("Hive");
        const int DefID = ResolveBuildingDefID(EM, DepotID);
        const bool IsResourceType =
            Type == ResourceType::Minerals || Type == ResourceType::VespeneGas;
        const bool IsDepot =
            DefID == CommandCenterDefID || DefID == NexusDefID ||
            DefID == HatcheryDefID || DefID == LairDefID || DefID == HiveDefID;
        return IsResourceType && IsDepot;
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
            const float WorkerRadius = CollisionRadiusFromBounds(
                EM.GetComponent<PathfindingComponent>(HarvesterID), 16.0f);
            return EntityEdgeDistance(EM, HarvesterTrans->Position, TargetResourceID) <=
                   WorkerRadius + 2.0f;
        }
        return HarvesterTrans->Position.DistanceTo(TargetTrans->Position) <=
               HarvestInteractionRange(EM, TargetResourceID);
    }

    void IssueHarvestMove(Core::ECS::EntityManager& EM,
                          int HarvesterID,
                          int TargetResourceID,
                          Core::Math::Vec2 TargetPosition,
                          int ApproachEntityID = -1,
                          float ApproachRange = -1.0f)
    {
        const int ApproachID = ApproachEntityID >= 0 ? ApproachEntityID : TargetResourceID;
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
                ApproachID);
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
            CommandSystem::IssueMove(EM, {HarvesterID}, MoveTarget, ApproachID);

        RestoreHarvestCommand(EM, HarvesterID, TargetResourceID, TargetPosition);
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
        if (!Resource || Resource->IsDepleted || Resource->Amount <= 0)
            return false;
        PruneQueuedGatherers(EM, ResourceID, *Resource);
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
                !Preferred->IsDepleted &&
                Preferred->Amount > 0 &&
                Preferred->Type == Type &&
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
            if (!Resource || !CandidateTrans || Resource->IsDepleted || Resource->Amount <= 0)
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
                                 int PlayerID)
    {
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
            IssueHarvestMove(
                EM,
                HarvesterID,
                Replacement,
                ResourceTrans->Position,
                Replacement,
                -1.0f);
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
    if (!Resource || Resource->IsDepleted || Resource->Amount <= 0)
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

    // Support SCV (0x07), Drone (0x29), Probe (0x2D)
    bool IsWorker = (Unit->ChkUnitID == 0x07 || Unit->ChkUnitID == 0x29 ||
                     Unit->ChkUnitID == 0x2D);
    if (!IsWorker)
    {
        SetReason(OutReason, "only worker units can harvest (SCV/Drone/Probe)");
        return false;
    }

    if (Resource->Type == ResourceType::Minerals)
        return true;

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

        ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
        Harvest->State = HarvestState::MovingToResource;
        Harvest->TargetResourceID = TargetResourceID;
        Harvest->DesiredResourceID = TargetResourceID;
        Harvest->CarryingAmount = 0;
        Harvest->CarryingType = ResourceType::None;
        Harvest->ReturnBuildingID = -1;
        Harvest->QueuedAtResource = false;

        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::Harvest;
        Cmd->TargetEntityID = TargetResourceID;

        if (ResTarget)
        {
            QueueGathererForResource(EM, TargetResourceID, ID);
            IssueHarvestMove(EM, ID, TargetResourceID, ResTarget->Position);
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

    auto Depots = EM.GetEntitiesWith<TransformComponent, BuildingComponent, PlayerComponent>();
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
        ReleaseResourceAssignment(EM, Harvest->TargetResourceID, ID);
        Harvest->State = HarvestState::Idle;
        StopHarvestCommand(EM, ID);
        return;
    }

    const int WorkerPlayerID = WorkerOwner ? WorkerOwner->PlayerID : 0;
    const auto StopHarvesting = [&]()
    {
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
            if (IsWithinHarvestRange(EM, ID, Harvest->TargetResourceID))
            {
                if (CanClaimResource(EM, Harvest->TargetResourceID, ID))
                {
                    ClaimResource(EM, Harvest->TargetResourceID, ID);
                    Harvest->State = HarvestState::Mining;
                    Harvest->QueuedAtResource = false;
                    Harvest->MineTimer = 0.0f;
                    ClearMotion(EM, ID);
                }
                else
                {
                    Harvest->State = HarvestState::QueuedAtResource;
                    Harvest->QueuedAtResource = true;
                    ClearMotion(EM, ID);
                }
                break;
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
            if (!ResComp || ResComp->IsDepleted || !CanHarvest(EM, ID, Harvest->TargetResourceID))
            {
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
                int Gather = std::min(Harvest->CarryCapacity - Harvest->CarryingAmount,
                                      std::min(8, ResComp->Amount));
                if (Gather > 0)
                {
                    Harvest->CarryingAmount += Gather;
                    ResComp->Amount -= Gather;
                    if (ResComp->Amount <= 0)
                    {
                        ResComp->Amount = 0;
                        ResComp->IsDepleted = true;
                    }
                }

                if (Harvest->CarryingAmount >= Harvest->CarryCapacity || ResComp->IsDepleted)
                {
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
                            IssueHarvestMove(EM, ID, Harvest->TargetResourceID,
                                             DepotTrans->Position,
                                             Harvest->ReturnBuildingID, 28.0f);
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
                    IssueHarvestMove(EM, ID, Harvest->TargetResourceID,
                                     DepotTrans->Position,
                                     Harvest->ReturnBuildingID, 28.0f);
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
                    IssueHarvestMove(EM, ID, Harvest->TargetResourceID,
                                     DepotTrans->Position,
                                     Harvest->ReturnBuildingID, 28.0f);
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

            const float DepositRange = WorkerContactRange(EM, ID, Path, 2.0f);
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
            if (Dist <= DepositRange)
            {
                const ResourceType ReturnType = Harvest->CarryingType;
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
                if (!RetargetHarvestResource(
                        EM,
                        ID,
                        *Harvest,
                        ReturnType,
                        WorkerPlayerID))
                {
                    StopHarvesting();
                }
            }
            else if (IsMoveSettled(Path))
            {
                IssueHarvestMove(EM, ID, Harvest->TargetResourceID,
                                 DepotTrans->Position,
                                 Harvest->ReturnBuildingID, 28.0f);
            }
            break;
        }

        case HarvestState::Depositing:
            break;
    }
}

} // namespace Game::Systems
