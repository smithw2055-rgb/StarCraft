#include "Game/Systems/HarvestSystem.h"

#include <limits>
#include <cmath>
#include <array>
#include <string>

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
        auto* HarvesterTrans = EM.GetComponent<TransformComponent>(HarvesterID);
        const int ApproachID = ApproachEntityID >= 0 ? ApproachEntityID : TargetResourceID;
        auto* TargetTrans = EM.GetComponent<TransformComponent>(ApproachID);
        Core::Math::Vec2 MoveTarget = TargetPosition;
        if (HarvesterTrans && TargetTrans)
        {
            const float Range = ApproachRange >= 0.0f
                ? ApproachRange
                : std::max(12.0f, HarvestInteractionRange(EM, TargetResourceID) - 8.0f);
            Core::Math::Vec2 Delta = HarvesterTrans->Position - TargetTrans->Position;
            if (Delta.Length() < 0.001f)
                Delta = {0.0f, 1.0f};
            MoveTarget = TargetTrans->Position + Delta.Normalized() * Range;
        }

        auto RestoreHarvestCommand = [&]()
        {
            if (auto* Cmd = EM.GetComponent<CommandComponent>(HarvesterID))
            {
                Cmd->CurrentCommand = CommandType::Harvest;
                Cmd->TargetEntityID = TargetResourceID;
                Cmd->TargetPosition = TargetPosition;
            }
        };

        auto TryMove = [&](const Core::Math::Vec2& Candidate)
        {
            const bool HasPath = CommandSystem::IssueMoveAndHasPath(EM, HarvesterID, Candidate);
            RestoreHarvestCommand();
            return HasPath;
        };

        bool HasIssuedReachableMove = TryMove(MoveTarget);
        if (!HasIssuedReachableMove && HarvesterTrans && TargetTrans)
        {
            const float Range = ApproachRange >= 0.0f
                ? ApproachRange
                : std::max(12.0f, HarvestInteractionRange(EM, TargetResourceID) - 8.0f);
            const std::array<Core::Math::Vec2, 12> Directions = {{
                {1.0f, 0.0f}, {-1.0f, 0.0f}, {0.0f, 1.0f}, {0.0f, -1.0f},
                {0.7071f, 0.7071f}, {-0.7071f, 0.7071f},
                {0.7071f, -0.7071f}, {-0.7071f, -0.7071f},
                {0.9239f, 0.3827f}, {-0.9239f, 0.3827f},
                {0.9239f, -0.3827f}, {-0.9239f, -0.3827f}
            }};
            for (const auto& Dir : Directions)
            {
                const Core::Math::Vec2 Candidate = TargetTrans->Position + Dir * Range;
                if (TryMove(Candidate))
                {
                    HasIssuedReachableMove = true;
                    break;
                }
            }
        }

        if (!HasIssuedReachableMove)
            CommandSystem::IssueMove(EM, {HarvesterID}, MoveTarget);

        if (auto* Cmd = EM.GetComponent<CommandComponent>(HarvesterID))
        {
            Cmd->CurrentCommand = CommandType::Harvest;
            Cmd->TargetEntityID = TargetResourceID;
            Cmd->TargetPosition = TargetPosition;
        }
    }
}

bool HarvestSystem::CanHarvest(const Core::ECS::EntityManager& EM,
                               int UnitID,
                               int TargetResourceID,
                               std::string* OutReason)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Resource = MutableEM.GetComponent<ResourceComponent>(TargetResourceID);
    if (!Resource || Resource->IsDepleted)
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

        Harvest->State = HarvestState::MovingToResource;
        Harvest->TargetResourceID = TargetResourceID;
        Harvest->CarryingAmount = 0;
        Harvest->ReturnBuildingID = -1;

        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::Harvest;
        Cmd->TargetEntityID = TargetResourceID;

        if (ResTarget)
            IssueHarvestMove(EM, ID, TargetResourceID, ResTarget->Position);

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
    // Multi-race depot support
    const int NexusDefID = Game::Races::GetUnitDefID("Nexus");
    const int CommandCenterDefID = Game::Races::GetUnitDefID("CommandCenter");
    const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    const int LairDefID = Game::Races::GetUnitDefID("Lair");
    const int HiveDefID = Game::Races::GetUnitDefID("Hive");

    int BestID = -1;
    float BestDist = std::numeric_limits<float>::max();

    auto Depots = EM.GetEntitiesWith<TransformComponent, BuildingComponent, PlayerComponent>();
    for (int DepotID : Depots)
    {
        auto* Building = EM.GetComponent<BuildingComponent>(DepotID);
        auto* Owner = EM.GetComponent<PlayerComponent>(DepotID);
        auto* DepotTrans = EM.GetComponent<TransformComponent>(DepotID);
        if (!Building || !Owner || !DepotTrans || Building->UnderConstruction)
            continue;
        if (Owner->PlayerID != PlayerID)
            continue;

        const int DefID = ResolveBuildingDefID(EM, DepotID);
        const bool IsValidMineralDepot =
            DefID == CommandCenterDefID || DefID == NexusDefID ||
            DefID == HatcheryDefID || DefID == LairDefID || DefID == HiveDefID;

        const bool IsValidDepot =
            (Type == ResourceType::Minerals || Type == ResourceType::VespeneGas) &&
            IsValidMineralDepot;
        if (!IsValidDepot)
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
    auto* WorkerOwner = EM.GetComponent<PlayerComponent>(ID);
    if (!Trans || !Harvest)
        return;

    const int WorkerPlayerID = WorkerOwner ? WorkerOwner->PlayerID : 0;

    switch (Harvest->State)
    {
        case HarvestState::Idle:
            break;

        case HarvestState::MovingToResource:
        {
            if (!CanHarvest(EM, ID, Harvest->TargetResourceID))
            {
                Harvest->State = HarvestState::Idle;
                if (auto* Cmd = EM.GetComponent<CommandComponent>(ID))
                    Cmd->CurrentCommand = CommandType::Stop;
                break;
            }

            auto* ResTarget = EM.GetComponent<TransformComponent>(Harvest->TargetResourceID);
            if (ResTarget && IsWithinHarvestRange(EM, ID, Harvest->TargetResourceID))
            {
                Harvest->State = HarvestState::Mining;
                Harvest->MineTimer = 0.0f;
                if (auto* PathComp = EM.GetComponent<PathfindingComponent>(ID))
                {
                    PathComp->Path.clear();
                    PathComp->PathIndex = 0;
                    PathComp->CurrentSpeed = 0.0f;
                    PathComp->CurrentVelocity = {0.0f, 0.0f};
                }
                break;
            }

            auto* Path = EM.GetComponent<PathfindingComponent>(ID);
            if (!Path || Path->Path.empty())
            {
                if (ResTarget)
                {
                    const float Dist = Trans->Position.DistanceTo(ResTarget->Position);
                    if (Dist <= HarvestInteractionRange(EM, Harvest->TargetResourceID) + 8.0f)
                    {
                        Harvest->State = HarvestState::Mining;
                        Harvest->MineTimer = 0.0f;
                        break;
                    }
                    IssueHarvestMove(EM, ID, Harvest->TargetResourceID, ResTarget->Position);
                }
                else
                {
                    Harvest->State = HarvestState::Idle;
                    if (auto* Cmd = EM.GetComponent<CommandComponent>(ID))
                        Cmd->CurrentCommand = CommandType::Stop;
                }
                return;
            }
            break;
        }

        case HarvestState::Mining:
        {
            auto* ResComp = EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID);
            if (!ResComp || ResComp->IsDepleted || !CanHarvest(EM, ID, Harvest->TargetResourceID))
            {
                Harvest->State = HarvestState::Idle;
                if (auto* Cmd = EM.GetComponent<CommandComponent>(ID))
                    Cmd->CurrentCommand = CommandType::Stop;
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
                    Harvest->State = HarvestState::MovingToDepot;
                    Harvest->CarryingType = ResComp->Type;
                    Harvest->ReturnBuildingID = FindNearestDepot(EM, ID, ResComp->Type);
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

            auto* Path = EM.GetComponent<PathfindingComponent>(ID);
            if (Path && Path->Path.empty())
            {
                float DepositRange = 32.0f;
                auto* DepotTrans = EM.GetComponent<TransformComponent>(Harvest->ReturnBuildingID);
                if (!DepotTrans)
                {
                    Harvest->ReturnBuildingID = -1;
                    break;
                }

                float Dist = Trans->Position.DistanceTo(DepotTrans->Position);
                if (Dist <= DepositRange)
                {
                    auto& Resources = GetResources(WorkerPlayerID);
                    if (Harvest->CarryingType == ResourceType::Minerals)
                        Resources.Minerals += Harvest->CarryingAmount;
                    else if (Harvest->CarryingType == ResourceType::VespeneGas)
                        Resources.VespeneGas += Harvest->CarryingAmount;

                    Harvest->CarryingAmount = 0;
                    Harvest->CarryingType = ResourceType::None;
                    Harvest->State = HarvestState::Idle;

                    auto* ResComp = EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID);
                    if (ResComp && !ResComp->IsDepleted && CanHarvest(EM, ID, Harvest->TargetResourceID))
                    {
                        if (auto* ResTarget = EM.GetComponent<TransformComponent>(Harvest->TargetResourceID))
                        {
                            Harvest->State = HarvestState::MovingToResource;
                            IssueHarvestMove(EM, ID, Harvest->TargetResourceID, ResTarget->Position);
                        }
                    }

                    if (Harvest->State == HarvestState::Idle)
                    {
                        if (auto* Cmd = EM.GetComponent<CommandComponent>(ID))
                            Cmd->CurrentCommand = CommandType::Stop;
                    }
                }
                else
                {
                    IssueHarvestMove(EM, ID, Harvest->TargetResourceID,
                                     DepotTrans->Position,
                                     Harvest->ReturnBuildingID, 28.0f);
                }
            }
            break;
        }

        case HarvestState::Depositing:
            break;
    }
}

} // namespace Game::Systems
