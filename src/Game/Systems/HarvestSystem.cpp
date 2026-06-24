#include "Game/Systems/HarvestSystem.h"

#include <limits>
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
            return Game::Races::GetTerranDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
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
    if (!Unit || !Transform || Unit->ChkUnitID != 0x07)
    {
        SetReason(OutReason, "only SCV can harvest in this Terran flow");
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
    const int TargetDefID = ResolveBuildingDefID(EM, TargetResourceID);
    const int RefineryDefID = Game::Races::GetUnitDefID("Refinery");
    if (!Building || Building->UnderConstruction || TargetDefID != RefineryDefID)
    {
        SetReason(OutReason, "gas must be harvested from a completed refinery");
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
            CommandSystem::IssueMove(EM, {ID}, ResTarget->Position);

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
    const int CommandCenterDefID = Game::Races::GetUnitDefID("CommandCenter");
    const int RefineryDefID = Game::Races::GetUnitDefID("Refinery");

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
        const bool IsValidDepot =
            (Type == ResourceType::Minerals && DefID == CommandCenterDefID) ||
            (Type == ResourceType::VespeneGas && DefID == RefineryDefID);
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

            auto* Path = EM.GetComponent<PathfindingComponent>(ID);
            if (!Path || Path->Path.empty())
            {
                auto* ResTarget = EM.GetComponent<TransformComponent>(Harvest->TargetResourceID);
                if (ResTarget)
                {
                    CommandSystem::IssueMove(EM, {ID}, ResTarget->Position);
                }
                else
                {
                    Harvest->State = HarvestState::Idle;
                    if (auto* Cmd = EM.GetComponent<CommandComponent>(ID))
                        Cmd->CurrentCommand = CommandType::Stop;
                }
                return;
            }

            float MiningRange = 24.0f;
            auto* ResTarget = EM.GetComponent<TransformComponent>(Harvest->TargetResourceID);
            if (ResTarget && Trans->Position.DistanceTo(ResTarget->Position) <= MiningRange)
            {
                Harvest->State = HarvestState::Mining;
                Harvest->MineTimer = 0.0f;
                if (auto* PathComp = EM.GetComponent<PathfindingComponent>(ID))
                {
                    PathComp->Path.clear();
                    PathComp->PathIndex = 0;
                }
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
                            CommandSystem::IssueMove(EM, {ID}, DepotTrans->Position);
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
                    CommandSystem::IssueMove(EM, {ID}, DepotTrans->Position);
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
                            CommandSystem::IssueMove(EM, {ID}, ResTarget->Position);
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
                    CommandSystem::IssueMove(EM, {ID}, DepotTrans->Position);
                }
            }
            break;
        }

        case HarvestState::Depositing:
            break;
    }
}

} // namespace Game::Systems
