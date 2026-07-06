#include "Game/Systems/ProductionSystem.h"

#include <algorithm>
#include <utility>

#include "Core/Utils/Logger.h"
#include "Game/DataEnums.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    ProductionSystem::ImageIDResolver gImageResolver;
    ProductionSystem::CollisionBoundsResolver gCollisionBoundsResolver;

    void SetReason(std::string* OutReason, const std::string& Reason)
    {
        if (OutReason)
            *OutReason = Reason;
    }

    uint32_t ResolveImageID(int ChkUnitID)
    {
        return gImageResolver ? gImageResolver(ChkUnitID) : (uint32_t)ChkUnitID;
    }

    UnitCollisionBounds ResolveCollisionBounds(int ChkUnitID)
    {
        return gCollisionBoundsResolver ? gCollisionBoundsResolver(ChkUnitID) : UnitCollisionBounds{};
    }

    int ResolveBuildingDefID(const Core::ECS::EntityManager& EM, int BuildingID)
    {
        if (const auto* Building = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<BuildingComponent>(BuildingID))
        {
            if (Building->BuildingDefID >= 0)
                return Building->BuildingDefID;
        }
        if (const auto* Unit = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<UnitComponent>(BuildingID))
            return Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
    }

    bool IsCompletedBuildingDef(const Core::ECS::EntityManager& EM, int PlayerID, int RequiredDefID)
    {
        auto BuildingIds =
            const_cast<Core::ECS::EntityManager&>(EM)
                .GetEntitiesWith<BuildingComponent, PlayerComponent>();
        for (int EntityID : BuildingIds)
        {
            auto* Building = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<BuildingComponent>(EntityID);
            auto* Player = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<PlayerComponent>(EntityID);
            if (!Building || !Player || Player->PlayerID != PlayerID || Building->UnderConstruction)
                continue;

            int DefID = ResolveBuildingDefID(EM, EntityID);
            if (DefID == RequiredDefID)
                return true;
        }
        return false;
    }

    int NormalizeDefIDForRace(int DefID, Race RaceType)
    {
        if (DefID < 0)
            return DefID;

        const auto Candidate = Races::GetUnitDefByID(DefID);
        if (Candidate.Name != "Unknown" && Candidate.RaceType == RaceType)
            return DefID;

        if (RaceType == Race::Zerg)
            return (int)Races::GetTerranDefs().size() + DefID;
        if (RaceType == Race::Protoss)
            return (int)Races::GetTerranDefs().size() +
                   (int)Races::GetZergDefs().size() +
                   DefID;
        return DefID;
    }

    void RefundTrainingReservation(Core::ECS::EntityManager& EM,
                                   int ProducerID,
                                   int UnitDefID)
    {
        auto Def = Races::GetUnitDefByID(UnitDefID);
        auto* Player = EM.GetComponent<PlayerComponent>(ProducerID);
        const int PlayerID = Player ? Player->PlayerID : 0;
        auto& Res = HarvestSystem::GetResources(PlayerID);
        Res.Minerals += Def.MineralCost;
        Res.VespeneGas += Def.GasCost;
        Res.SupplyUsed = std::max(0, Res.SupplyUsed - Def.SupplyCost);
    }
}

void ProductionSystem::SetImageResolver(ImageIDResolver Resolver)
{
    gImageResolver = std::move(Resolver);
}

void ProductionSystem::SetCollisionBoundsResolver(CollisionBoundsResolver Resolver)
{
    gCollisionBoundsResolver = std::move(Resolver);
}

bool ProductionSystem::CanTrain(const Core::ECS::EntityManager& EM,
                                int BuildingID, int UnitDefID,
                                std::string* OutReason)
{
    auto* Building = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<BuildingComponent>(BuildingID);
    auto* ProducerUnit = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<UnitComponent>(BuildingID);
    auto* Player = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<PlayerComponent>(BuildingID);
    if (!Building)
    {
        SetReason(OutReason, "producer is not a building");
        return false;
    }
    if (Building->UnderConstruction)
    {
        SetReason(OutReason, "producer is still under construction");
        return false;
    }
    if (Building->IsLifted)
    {
        SetReason(OutReason, "producer is lifted");
        return false;
    }
    if (!ProducerUnit || ProducerUnit->HitPoints <= 0)
    {
        SetReason(OutReason, "producer is not operational");
        return false;
    }
    if (!const_cast<Core::ECS::EntityManager&>(EM).GetComponent<TransformComponent>(BuildingID))
    {
        SetReason(OutReason, "producer missing transform");
        return false;
    }

    auto Def = Races::GetUnitDefByID(UnitDefID);
    if (Def.IsBuilding)
    {
        SetReason(OutReason, "building definitions cannot be trained");
        return false;
    }

    int ProducerDefID = ResolveBuildingDefID(EM, BuildingID);
    const int RequiredProducerDefID = NormalizeDefIDForRace(Def.ProducedBy, Def.RaceType);
    if (ProducerDefID >= 0 && RequiredProducerDefID >= 0 && ProducerDefID != RequiredProducerDefID)
    {
        SetReason(OutReason, "wrong producer for " + Def.Name);
        return false;
    }

    int PlayerID = Player ? Player->PlayerID : 0;
    auto& Res = HarvestSystem::GetResources(PlayerID);
    if (Res.Minerals < Def.MineralCost || Res.VespeneGas < Def.GasCost)
    {
        SetReason(OutReason, "not enough resources");
        return false;
    }
    if (Res.SupplyUsed + Def.SupplyCost > Res.SupplyMax)
    {
        SetReason(OutReason, "not enough supply");
        return false;
    }

    for (int RequiredDefID : Def.RequiredBuildings)
    {
        const int NormalizedRequiredDefID =
            NormalizeDefIDForRace(RequiredDefID, Def.RaceType);
        auto RequiredDef = Races::GetUnitDefByID(NormalizedRequiredDefID);
        const int RequiredAddonParentDefID =
            NormalizeDefIDForRace(RequiredDef.AddonOnBuildingDefID, RequiredDef.RaceType);
        if (RequiredDef.IsAddon && RequiredAddonParentDefID == RequiredProducerDefID)
        {
            if (Building->AddonID < 0 || !EM.IsAlive(Building->AddonID))
            {
                SetReason(OutReason, "required add-on missing");
                return false;
            }
            int AddonDefID = ResolveBuildingDefID(EM, Building->AddonID);
            auto* AddonBuilding = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<BuildingComponent>(Building->AddonID);
            if (AddonDefID != NormalizedRequiredDefID || (AddonBuilding && AddonBuilding->UnderConstruction))
            {
                SetReason(OutReason, "required add-on incomplete");
                return false;
            }
            continue;
        }

        if (!IsCompletedBuildingDef(EM, PlayerID, NormalizedRequiredDefID))
        {
            SetReason(OutReason, "required building missing");
            return false;
        }
    }

    return true;
}

bool ProductionSystem::StartTrain(Core::ECS::EntityManager& EM,
                                  int BuildingID, int UnitDefID)
{
    std::string Reason;
    if (!CanTrain(EM, BuildingID, UnitDefID, &Reason))
    {
        Core::Utils::Logger::Debug("ProductionSystem", "Cannot train: " + Reason);
        return false;
    }

    auto Def = Races::GetUnitDefByID(UnitDefID);
    auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    int PlayerID = Player ? Player->PlayerID : 0;
    auto& Res = HarvestSystem::GetResources(PlayerID);

    auto* Prod = EM.GetComponent<ProductionComponent>(BuildingID);
    if (!Prod)
        Prod = &EM.AddComponent<ProductionComponent>(BuildingID);

    Prod->Queue.push_back(UnitDefID);
    Res.Minerals -= Def.MineralCost;
    Res.VespeneGas -= Def.GasCost;
    Res.SupplyUsed += Def.SupplyCost;

    Core::Utils::Logger::Info("ProductionSystem",
        "Queued " + Def.Name + " in building " + std::to_string(BuildingID) +
        " (queue size: " + std::to_string(Prod->Queue.size()) + ")");
    return true;
}

bool ProductionSystem::CancelTrain(Core::ECS::EntityManager& EM,
                                   int BuildingID, int QueueIndex)
{
    auto* Prod = EM.GetComponent<ProductionComponent>(BuildingID);
    if (!Prod || Prod->Queue.empty())
        return false;

    int RefundUnitID = -1;
    int RemovedIndex = -1;
    if (QueueIndex >= 0 && QueueIndex < (int)Prod->Queue.size())
    {
        RemovedIndex = QueueIndex;
        RefundUnitID = Prod->Queue[QueueIndex];
        Prod->Queue.erase(Prod->Queue.begin() + QueueIndex);
    }
    else
    {
        RemovedIndex = (int)Prod->Queue.size() - 1;
        RefundUnitID = Prod->Queue.back();
        Prod->Queue.pop_back();
    }

    auto Def = Races::GetUnitDefByID(RefundUnitID);
    auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    int PlayerID = Player ? Player->PlayerID : 0;
    auto& Res = HarvestSystem::GetResources(PlayerID);
    Res.Minerals += Def.MineralCost;
    Res.VespeneGas += Def.GasCost;
    Res.SupplyUsed = std::max(0, Res.SupplyUsed - Def.SupplyCost);

    const bool CancelledCurrent =
        RemovedIndex == 0 &&
        RefundUnitID == Prod->CurrentTrainingUnitDefID;
    if (CancelledCurrent)
    {
        Prod->CurrentTrainingUnitDefID = -1;
        Prod->TrainingProgress = 0.0f;
        Prod->TrainingTimePerUnit = 0.0f;
        if (!Prod->Queue.empty())
        {
            Prod->CurrentTrainingUnitDefID = Prod->Queue.front();
            auto NextDef = Races::GetUnitDefByID(Prod->CurrentTrainingUnitDefID);
            Prod->TrainingTimePerUnit = (float)NextDef.BuildTime;
        }
    }
    return true;
}

void ProductionSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f)
        return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<ProductionComponent>();
    for (auto ID : Entities)
        ProcessTraining(EM, ID, DeltaTime);
}

void ProductionSystem::ProcessTraining(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime)
{
    auto* Prod = EM.GetComponent<ProductionComponent>(EntityID);
    if (!Prod || Prod->Queue.empty())
        return;

    auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Building || Building->UnderConstruction || !Unit || Unit->HitPoints <= 0)
        return;

    if (Prod->CurrentTrainingUnitDefID == -1)
    {
        Prod->CurrentTrainingUnitDefID = Prod->Queue.front();
        auto Def = Races::GetUnitDefByID(Prod->CurrentTrainingUnitDefID);
        Prod->TrainingTimePerUnit = (float)Def.BuildTime;
        Prod->TrainingProgress = 0.0f;
    }

    Prod->TrainingProgress += DeltaTime;
    if (Prod->TrainingProgress < Prod->TrainingTimePerUnit)
        return;

    Prod->TrainingProgress = 0.0f;
    int TrainedDefID = Prod->CurrentTrainingUnitDefID;
    auto Def = Races::GetUnitDefByID(TrainedDefID);

    int NewUnitID = SpawnUnit(EM, EntityID, TrainedDefID);
    if (NewUnitID != -1)
    {
        Core::Utils::Logger::Info("ProductionSystem",
            "Trained " + Def.Name + " (entity " + std::to_string(NewUnitID) + ")");
    }
    else
    {
        RefundTrainingReservation(EM, EntityID, TrainedDefID);
        Core::Utils::Logger::Warning("ProductionSystem",
            "Failed to spawn " + Def.Name + " from producer " + std::to_string(EntityID) +
            "; refunded reserved resources and supply");
    }

    Prod->Queue.erase(Prod->Queue.begin());
    Prod->CurrentTrainingUnitDefID = -1;
    if (!Prod->Queue.empty())
    {
        Prod->CurrentTrainingUnitDefID = Prod->Queue.front();
        auto NextDef = Races::GetUnitDefByID(Prod->CurrentTrainingUnitDefID);
        Prod->TrainingTimePerUnit = (float)NextDef.BuildTime;
    }
}

int ProductionSystem::DebugSpawnUnit(Core::ECS::EntityManager& EM,
                                     int UnitDefID,
                                     Core::Math::Vec2 Position,
                                     int PlayerID)
{
    auto Def = Races::GetUnitDefByID(UnitDefID);
    if (Def.IsBuilding)
        return -1;

    int EntityID = EM.CreateEntity();
    auto& Trans = EM.AddComponent<TransformComponent>(EntityID);
    Trans.Position = Position;

    auto& Render = EM.AddComponent<RenderComponent>(EntityID);
    Render.ImageID = ResolveImageID(Def.ChkUnitID);
    Render.FrameIndex = 0;
    Render.TotalFrames = 1;
    Render.Tint = Core::Colors::White();
    Render.Layer = RenderLayer::Units;

    auto& Unit = EM.AddComponent<UnitComponent>(EntityID);
    Unit.ChkUnitID = Def.ChkUnitID;
    Unit.UnitRace = Def.RaceType;
    Unit.SizeType = Def.SizeType;
    Unit.HitPoints = Def.HitPoints;
    Unit.MaxHitPoints = Def.HitPoints;
    Unit.ShieldPoints = Def.ShieldPoints;
    Unit.MaxShieldPoints = Def.ShieldPoints;
    Unit.EnergyPoints = Def.EnergyPoints;
    Unit.MaxEnergyPoints = Def.EnergyPoints;
    Unit.Armor = Def.Armor;
    Unit.Speed = Def.Speed;
    Unit.SightRange = Def.SightRange;
    Unit.SupplyCost = Def.SupplyCost;
    Unit.IsDetector = Races::IsDetectorChkUnitID(Unit.ChkUnitID);

    auto& Player = EM.AddComponent<PlayerComponent>(EntityID);
    Player.PlayerID = PlayerID;
    UpgradeSystem::RefreshUnitEnergyCap(EM, EntityID);

    if (Def.GroundDamage > 0 || Def.AirDamage > 0)
    {
        auto& Combat = EM.AddComponent<CombatComponent>(EntityID);
        Races::ApplyCombatDefinition(
            Combat, Def, Game::Data::IsAirUnitType(Unit.ChkUnitID));
    }

    auto& Command = EM.AddComponent<CommandComponent>(EntityID);
    Command.CurrentCommand = CommandType::Stop;
    auto& Path = EM.AddComponent<PathfindingComponent>(EntityID);
    Path.NodeTolerance = 8.0f;
    Path.IsAir = Game::Data::IsAirUnitType(Unit.ChkUnitID);
    ApplyCollisionBounds(Path, ResolveCollisionBounds(Unit.ChkUnitID));
    return EntityID;
}

int ProductionSystem::SpawnUnit(Core::ECS::EntityManager& EM,
                                int BuildingID, int UnitDefID)
{
    auto* BuildingTrans = EM.GetComponent<TransformComponent>(BuildingID);
    if (!BuildingTrans)
        return -1;

    Core::Math::Vec2 SpawnPos = BuildingTrans->Position;
    const int ProducerDefID = ResolveBuildingDefID(EM, BuildingID);
    float ExitOffsetY = 48.0f;
    if (ProducerDefID >= 0)
    {
        if (ProducerDefID == Races::GetUnitDefID("CommandCenter"))
        {
            ExitOffsetY = 72.0f;
        }
        else if (ProducerDefID == Races::GetUnitDefID("Barracks") ||
                 ProducerDefID == Races::GetUnitDefID("Factory") ||
                 ProducerDefID == Races::GetUnitDefID("Starport") ||
                 ProducerDefID == Races::GetUnitDefID("ScienceFacility"))
        {
            ExitOffsetY = 56.0f;
        }
    }
    SpawnPos.Y += ExitOffsetY;
    int PlayerID = 0;
    if (auto* SrcPlayer = EM.GetComponent<PlayerComponent>(BuildingID))
        PlayerID = SrcPlayer->PlayerID;
    const int NewUnitID = DebugSpawnUnit(EM, UnitDefID, SpawnPos, PlayerID);
    if (NewUnitID >= 0)
    {
        if (const auto* ProducerCmd = EM.GetComponent<CommandComponent>(BuildingID);
            ProducerCmd && ProducerCmd->HasRallyPoint)
        {
            Order RallyOrder;
            RallyOrder.Type = OrderType::Move;
            RallyOrder.TargetPosition = ProducerCmd->RallyPoint;
            CommandSystem::IssueOrder(EM, NewUnitID, RallyOrder, false);
        }
    }
    return NewUnitID;
}

} // namespace Game::Systems
