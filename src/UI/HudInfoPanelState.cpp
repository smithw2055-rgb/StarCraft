#include "UI/Hud.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Game/DataEnums.h"
#include "Game/Systems/UpgradeSystem.h"
#include "UI/HudText.h"

namespace Game::Client::UI
{

void Hud::ResetInfoPanel()
{
    mState.InfoPanel.Reset();
}

void Hud::UpdateInfoPanel(Core::ECS::EntityManager& Entities,
                          int SelectedEntityId,
                          int SelectedChkUnitId,
                          int SelectedTerranDefId,
                          Core::ECS::EntityManager* EcsEntities)
{
    ResetInfoPanel();
    if (SelectedEntityId < 0)
        return;

    mState.InfoPanel.HasSelection = true;
    mState.InfoPanel.SelectedCount = mState.Selection.SelectedCount;

    mState.InfoPanel.MultiSelectSlots.clear();
    if (mState.Selection.SelectedCount > 1 && !mState.Selection.LastSelectedIds.empty())
    {
        const size_t MaxUnits = std::min<size_t>(12, mState.Selection.LastSelectedIds.size());
        for (size_t I = 0; I < MaxUnits; ++I)
        {
            const int EntId = mState.Selection.LastSelectedIds[I];
            auto* U = Entities.GetComponent<Game::Components::UnitComponent>(EntId);
            if (!U)
                continue;
            HudMultiSelectSlot Slot;
            Slot.EntityId = EntId;
            Slot.ChkUnitId = U->ChkUnitID;
            Slot.CurrentHp = U->HitPoints;
            Slot.MaxHp = std::max(1, U->MaxHitPoints);
            Slot.Shield = U->ShieldPoints;
            Slot.ShieldMax = U->MaxShieldPoints;
            Slot.Energy = U->EnergyPoints;
            Slot.EnergyMax = U->MaxEnergyPoints;
            Slot.WireframeIndex = U->ChkUnitID;
            Slot.Name = DisplayLabelForChkUnitId(U->ChkUnitID);
            mState.InfoPanel.MultiSelectSlots.push_back(Slot);
        }
    }

    auto* SelectedUnit = Entities.GetComponent<Game::Components::UnitComponent>(SelectedEntityId);
    auto* SelectedBuilding = Entities.GetComponent<Game::Components::BuildingComponent>(SelectedEntityId);
    auto* SelectedResource = Entities.GetComponent<Game::Components::ResourceComponent>(SelectedEntityId);
    mState.InfoPanel.IsBuilding = SelectedBuilding != nullptr;
    mState.InfoPanel.IsResource = SelectedResource != nullptr;
    mState.InfoPanel.CurrentValue = SelectedUnit ? SelectedUnit->HitPoints : 0;
    mState.InfoPanel.MaxValue = SelectedUnit ? SelectedUnit->MaxHitPoints : 0;
    mState.InfoPanel.ResourceAmount = SelectedResource ? SelectedResource->Amount : 0;
    mState.InfoPanel.PreviewChkUnitId = SelectedChkUnitId;

    int ResolvedDefId = SelectedTerranDefId;
    if (ResolvedDefId >= 0)
    {
        mState.InfoPanel.Name = DisplayLabelForDef(ResolvedDefId);
        mState.InfoPanel.IconIndex = IconForDef(ResolvedDefId);
        mState.InfoPanel.PreviewDefID = ResolvedDefId;
        if (ResolvedDefId == Game::Races::GetUnitDefID("SCV"))
            mState.InfoPanel.RoleLine = "Private";
    }
    else
    {
        mState.InfoPanel.Name = DisplayLabelForChkUnitId(SelectedChkUnitId);
    }

    if (!EcsEntities || SelectedEntityId <= 0)
    {
        if (mState.InfoPanel.IsResource)
            mState.InfoPanel.TypeLine = "Neutral resource";
        else if (mState.InfoPanel.IsBuilding)
            mState.InfoPanel.TypeLine = "Structure";
        else
            mState.InfoPanel.TypeLine = "Unit";
        return;
    }

    auto& MutableEM = *EcsEntities;
    auto* Unit = MutableEM.GetComponent<Game::Components::UnitComponent>(SelectedEntityId);
    auto* Player = MutableEM.GetComponent<Game::Components::PlayerComponent>(SelectedEntityId);
    auto* Building = MutableEM.GetComponent<Game::Components::BuildingComponent>(SelectedEntityId);
    auto* Combat = MutableEM.GetComponent<Game::Components::CombatComponent>(SelectedEntityId);
    auto* Command = MutableEM.GetComponent<Game::Components::CommandComponent>(SelectedEntityId);
    auto* Resource = MutableEM.GetComponent<Game::Components::ResourceComponent>(SelectedEntityId);
    auto* Production = MutableEM.GetComponent<Game::Components::ProductionComponent>(SelectedEntityId);

    if (Unit)
    {
        mState.InfoPanel.CurrentValue = Unit->HitPoints;
        mState.InfoPanel.MaxValue = Unit->MaxHitPoints;
        mState.InfoPanel.ShieldValue = Unit->ShieldPoints;
        mState.InfoPanel.ShieldMax = Unit->MaxShieldPoints;
        mState.InfoPanel.EnergyValue = Unit->EnergyPoints;
        mState.InfoPanel.EnergyMax = Unit->MaxEnergyPoints;
        mState.InfoPanel.Armor = Unit->Armor;
        const int SightBonus =
            Game::Systems::UpgradeSystem::GetSightRangeBonusTiles(MutableEM, SelectedEntityId);
        const float SpeedMultiplier =
            Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(MutableEM, SelectedEntityId);
        mState.InfoPanel.SightValue = Unit->SightRange + SightBonus;
        mState.InfoPanel.SpeedValue =
            (int)std::lround(Game::Tuning::ToWorldMoveSpeed(Unit->Speed * SpeedMultiplier));
        mState.InfoPanel.TypeLine = RaceLabel(Unit->UnitRace) +
                              std::string(mState.InfoPanel.IsBuilding ? " Structure" : " Unit");
        mState.InfoPanel.StatLine =
            "Armor " + std::to_string(Unit->Armor) +
            "  Sight " + std::to_string(mState.InfoPanel.SightValue) +
            "  Speed " + std::to_string(mState.InfoPanel.SpeedValue);
    }
    if (Combat)
    {
        mState.InfoPanel.GroundDamage = Combat->GroundDamage;
        mState.InfoPanel.AirDamage = Combat->AirDamage;
        const int RangeBonusPixels =
            Game::Systems::UpgradeSystem::GetRangeBonusPixels(MutableEM, SelectedEntityId);
        const float EffectiveRange =
            Combat->AttackRange + (float)RangeBonusPixels / 32.0f;
        mState.InfoPanel.RangeValue = (int)std::lround(EffectiveRange);
        std::string DamageLine = "Ground " + std::to_string(Combat->GroundDamage);
        if (Combat->AirDamage > 0)
            DamageLine += "  Air " + std::to_string(Combat->AirDamage);
        DamageLine += "  Range " + std::to_string(mState.InfoPanel.RangeValue);
        mState.InfoPanel.CombatLine = std::move(DamageLine);
    }
    if (Player)
    {
        mState.InfoPanel.PlayerID = Player->PlayerID;
        mState.InfoPanel.TypeLine += "  P" + std::to_string(Player->PlayerID);
        if (ResolvedDefId == Game::Races::GetUnitDefID("SCV"))
            mState.InfoPanel.RoleLine = RoleLabelForScv(Player->PlayerID);
    }
    if (Command)
    {
        mState.InfoPanel.StatusLine = CommandLabel(Command->CurrentCommand);
    }
    if (Resource)
    {
        mState.InfoPanel.IsResource = true;
        mState.InfoPanel.ResourceAmount = Resource->Amount;
        mState.InfoPanel.TypeLine = Resource->Type == Game::Components::ResourceType::VespeneGas
            ? "Neutral gas resource"
            : "Neutral mineral resource";
        mState.InfoPanel.StatusLine = Resource->IsDepleted ? "Depleted" : "Available";
        mState.InfoPanel.DetailLine = "Remaining " + std::to_string(Resource->Amount);
    }
    if (Building)
    {
        mState.InfoPanel.IsBuilding = true;
        mState.InfoPanel.UnderConstruction = Building->UnderConstruction;
        if (Building->UnderConstruction)
        {
            float Progress = 0.0f;
            if (Building->TotalBuildTime > 0.0f)
                Progress = std::clamp(Building->BuildProgress / Building->TotalBuildTime, 0.0f, 1.0f);
            mState.InfoPanel.Progress = Progress;
            mState.InfoPanel.StatusLine = "Constructing";
            mState.InfoPanel.DetailLine = "Construct " +
                std::to_string((int)std::lround(Progress * 100.0f)) + "%";
        }
        else if (Building->IsLifted)
        {
            mState.InfoPanel.StatusLine = "Lifted";
        }
        else if (mState.InfoPanel.StatusLine.empty())
        {
            mState.InfoPanel.StatusLine = "Operational";
        }

        const int CommandCenterDef = Game::Races::GetUnitDefID("CommandCenter");
        const int NexusDef = Game::Races::GetUnitDefID("Nexus");
        const int HatcheryDef = Game::Races::GetUnitDefID("Hatchery");
        const int LairDef = Game::Races::GetUnitDefID("Lair");
        const int HiveDef = Game::Races::GetUnitDefID("Hive");
        if (ResolvedDefId == CommandCenterDef || ResolvedDefId == NexusDef ||
            ResolvedDefId == HatcheryDef || ResolvedDefId == LairDef || ResolvedDefId == HiveDef)
        {
            mState.InfoPanel.IsTownCenter = true;
            mState.InfoPanel.SupplyUsed = mState.Resources.SupplyUsed;
            mState.InfoPanel.SupplyProvided = Game::Races::GetSupplyProvidedByDefID(ResolvedDefId);
            mState.InfoPanel.SupplyTotal = mState.Resources.SupplyMax;
            mState.InfoPanel.SupplyMax = 200;
        }

        if (Building->AddonID > 0 && MutableEM.IsAlive(Building->AddonID))
        {
            auto* AddonBuilding = MutableEM.GetComponent<Game::Components::BuildingComponent>(Building->AddonID);
            int AddonDefId = AddonBuilding ? AddonBuilding->BuildingDefID : -1;
            if (AddonDefId >= 0)
            {
                mState.InfoPanel.CargoLine = "Addon " + DisplayLabelForDef(AddonDefId);
            }
        }

        if (Building->IsResearching && Building->ResearchingID >= 0)
        {
            const auto ResearchId =
                static_cast<Game::Systems::ResearchId>(Building->ResearchingID);
            const int ResearchPlayer = Building->ResearchPlayer >= 0
                ? Building->ResearchPlayer
                : (Player ? Player->PlayerID : 0);
            const int TotalTime =
                Game::Systems::UpgradeSystem::GetResearchTimeCost(ResearchId, ResearchPlayer);
            float Progress = 0.0f;
            if (TotalTime > 0)
            {
                Progress = 1.0f -
                    std::clamp(Building->BuildTimeRemaining / static_cast<float>(TotalTime),
                               0.0f,
                               1.0f);
            }
            const std::string ResearchName =
                std::string(Game::Systems::UpgradeSystem::GetResearchDisplayName(ResearchId));
            const bool IsUpgrade =
                Game::Systems::UpgradeSystem::IsUpgradeType(ResearchId);
            const char* QueueLabel = IsUpgrade ? "Upgrade" : "Research";
            const char* Verb = IsUpgrade ? "Upgrading " : "Researching ";
            const char* DetailPrefix = IsUpgrade ? "Upgrade " : "Research ";
            mState.InfoPanel.Progress = std::clamp(Progress, 0.0f, 1.0f);
            mState.InfoPanel.ActiveResearchID = Building->ResearchingID;
            mState.InfoPanel.QueueLabel = QueueLabel;
            mState.InfoPanel.QueueEntries = {ResearchName};
            mState.InfoPanel.StatusLine = std::string(Verb) + ResearchName;
            mState.InfoPanel.DetailLine =
                std::string(DetailPrefix) +
                std::to_string((int)std::lround(mState.InfoPanel.Progress * 100.0f)) +
                "%";
        }

        std::string BldgName = mState.InfoPanel.Name;
        std::vector<Game::Systems::ResearchId> TechResearches;
        auto CheckTechBuilding = [](const std::string& Name, std::vector<Game::Systems::ResearchId>& Out) -> bool
        {
            using namespace Game::Systems;
            Out.clear();
            if (Name == "Engineering Bay" || Name == "EngineeringBay") {
                Out = { ResearchId::Terran_InfantryWeapons, ResearchId::Terran_InfantryArmor };
                return true;
            }
            if (Name == "Armory") {
                Out = { ResearchId::Terran_VehicleWeapons, ResearchId::Terran_VehicleArmor,
                        ResearchId::Terran_ShipWeapons, ResearchId::Terran_ShipArmor };
                return true;
            }
            if (Name == "Academy") {
                Out = { ResearchId::Terran_StimPacks, ResearchId::Terran_U238Shells,
                        ResearchId::Terran_Restoration, ResearchId::Terran_OpticalFlare,
                        ResearchId::Terran_CaduceusReactor };
                return true;
            }
            if (Name == "Machine Shop" || Name == "MachineShop") {
                Out = { ResearchId::Terran_SiegeMode, ResearchId::Terran_SpiderMines,
                        ResearchId::Terran_IonThrusters, ResearchId::Terran_CharonBoosters };
                return true;
            }
            if (Name == "Control Tower" || Name == "ControlTower") {
                Out = { ResearchId::Terran_CloakingField, ResearchId::Terran_ApolloReactor };
                return true;
            }
            if (Name == "Covert Ops" || Name == "CovertOps") {
                Out = { ResearchId::Terran_Lockdown, ResearchId::Terran_MoebiusReactor,
                        ResearchId::Terran_OcularImplants, ResearchId::Terran_PersonalCloaking };
                return true;
            }
            if (Name == "Physics Lab" || Name == "PhysicsLab") {
                Out = { ResearchId::Terran_YamatoGun, ResearchId::Terran_ColossusReactor };
                return true;
            }
            if (Name == "Science Facility" || Name == "ScienceFacility") {
                Out = { ResearchId::Terran_EMPShockwave, ResearchId::Terran_Irradiate,
                        ResearchId::Terran_TitanReactor };
                return true;
            }
            if (Name == "Evolution Chamber" || Name == "EvolutionChamber") {
                Out = { ResearchId::Zerg_MeleeAttacks, ResearchId::Zerg_MissileAttacks, ResearchId::Zerg_Carapace };
                return true;
            }
            if (Name == "Spire" || Name == "Greater Spire" || Name == "GreaterSpire") {
                Out = { ResearchId::Zerg_FlyerAttacks, ResearchId::Zerg_FlyerCarapace };
                return true;
            }
            if (Name == "Spawning Pool" || Name == "SpawningPool") {
                Out = { ResearchId::Zerg_AdrenalGlands, ResearchId::Zerg_MetabolicBoost };
                return true;
            }
            if (Name == "Hydralisk Den" || Name == "HydraliskDen") {
                Out = { ResearchId::Zerg_MuscularAugments, ResearchId::Zerg_GroovedSpines, ResearchId::Zerg_LurkerMorph };
                return true;
            }
            if (Name == "Ultralisk Cavern" || Name == "UltraliskCavern") {
                Out = { ResearchId::Zerg_AnabolicSynthesis, ResearchId::Zerg_ChitinousPlating };
                return true;
            }
            if (Name == "Queen's Nest" || Name == "QueensNest" || Name == "Queen Nest") {
                Out = { ResearchId::Zerg_SpawnBroodling, ResearchId::Zerg_Ensnare, ResearchId::Zerg_QueenEnergy };
                return true;
            }
            if (Name == "Defiler Mound" || Name == "DefilerMound") {
                Out = { ResearchId::Zerg_Plague, ResearchId::Zerg_Consume, ResearchId::Zerg_DefilerEnergy };
                return true;
            }
            if (Name == "Forge") {
                Out = { ResearchId::Protoss_GroundWeapons, ResearchId::Protoss_GroundArmor, ResearchId::Protoss_PlasmaShields };
                return true;
            }
            if (Name == "Cybernetics Core" || Name == "CyberneticsCore") {
                Out = { ResearchId::Protoss_AirWeapons, ResearchId::Protoss_AirArmor, ResearchId::Protoss_SingularityCharge };
                return true;
            }
            if (Name == "Robotics Support Bay" || Name == "RoboticsSupportBay") {
                Out = { ResearchId::Protoss_GraviticDrive, ResearchId::Protoss_ScarabDamage };
                return true;
            }
            if (Name == "Observatory") {
                Out = { ResearchId::Protoss_SensorArray };
                return true;
            }
            if (Name == "Citadel of Adun" || Name == "CitadelOfAdun") {
                Out = { ResearchId::Protoss_ZealotSpeed };
                return true;
            }
            if (Name == "Templar Archives" || Name == "TemplarArchives") {
                Out = { ResearchId::Protoss_PsionicStorm, ResearchId::Protoss_Hallucination, ResearchId::Protoss_KhaydarinAmulet };
                return true;
            }
            if (Name == "Fleet Beacon" || Name == "FleetBeacon") {
                Out = { ResearchId::Protoss_ApialSensors, ResearchId::Protoss_ArgusJewel };
                return true;
            }
            if (Name == "Arbiter Tribunal" || Name == "ArbiterTribunal") {
                Out = { ResearchId::Protoss_Recall, ResearchId::Protoss_KhaydarinCore };
                return true;
            }
            return false;
        };

        if (CheckTechBuilding(BldgName, TechResearches))
        {
            mState.InfoPanel.IsTechBuilding = true;
            mState.InfoPanel.IsProductionBuilding = false;
            mState.InfoPanel.TechUpgrades.clear();
            const int OwnerPlayer = Player ? Player->PlayerID : 0;
            for (auto ResId : TechResearches)
            {
                const int Level = Game::Systems::UpgradeSystem::GetResearchLevel(OwnerPlayer, ResId);
                const bool Has = Game::Systems::UpgradeSystem::HasResearch(OwnerPlayer, ResId);
                const bool IsUp = Game::Systems::UpgradeSystem::IsUpgradeType(ResId);
                const int MaxLvl = IsUp ? 3 : 1;
                const int CurLvl = IsUp ? Level : (Has ? 1 : 0);
                std::string ResName = std::string(Game::Systems::UpgradeSystem::GetResearchDisplayName(ResId));
                mState.InfoPanel.TechUpgrades.push_back({ (int)ResId, CurLvl, MaxLvl, ResName });
            }
        }
        else
        {
            mState.InfoPanel.IsTechBuilding = false;
            mState.InfoPanel.IsProductionBuilding = (Production != nullptr) ||
                (BldgName == "Barracks" || BldgName == "Factory" || BldgName == "Starport" ||
                 BldgName == "Gateway" || BldgName == "Robotics Facility" || BldgName == "Stargate" ||
                 BldgName == "Hatchery" || BldgName == "Lair" || BldgName == "Hive" ||
                 BldgName == "Command Center" || BldgName == "CommandCenter" || BldgName == "Nexus");
        }
    }
    if (auto* Harvest = MutableEM.GetComponent<Game::Components::HarvestComponent>(SelectedEntityId))
    {
        if (Harvest->CarryingAmount > 0)
        {
            const char* CarryLabel =
                Harvest->CarryingType == Game::Components::ResourceType::VespeneGas ? "Gas" :
                Harvest->CarryingType == Game::Components::ResourceType::Minerals ? "Minerals" :
                                                                                     "Cargo";
            mState.InfoPanel.CarryingAmount = Harvest->CarryingAmount;
            mState.InfoPanel.CargoLine =
                std::string(CarryLabel) + " " + std::to_string(Harvest->CarryingAmount) +
                "/" + std::to_string(Harvest->CarryCapacity);
        }
        if (mState.InfoPanel.DetailLine.empty())
            mState.InfoPanel.DetailLine = "Harvest " + HarvestStateLabel(Harvest->State);
    }
    if (Production && !Production->Queue.empty())
    {
        mState.InfoPanel.QueueEntries.clear();
        mState.InfoPanel.QueueUnitDefIDs.clear();
        for (size_t Index = 0; Index < Production->Queue.size() && Index < 5; ++Index)
        {
            mState.InfoPanel.QueueEntries.push_back(
                DisplayLabelForDef(Production->Queue[Index]));
            mState.InfoPanel.QueueUnitDefIDs.push_back(Production->Queue[Index]);
        }
        mState.InfoPanel.QueueLabel = "Queue";
        if (Production->CurrentTrainingUnitDefID >= 0)
        {
            float Progress = 0.0f;
            if (Production->TrainingTimePerUnit > 0.0f)
            {
                Progress = std::clamp(
                    Production->TrainingProgress / Production->TrainingTimePerUnit,
                    0.0f, 1.0f);
            }
            mState.InfoPanel.Progress = Progress;
            mState.InfoPanel.StatusLine = "Training " +
                DisplayLabelForDef(Production->CurrentTrainingUnitDefID);
            mState.InfoPanel.DetailLine =
                "Queue " + std::to_string((int)Production->Queue.size()) +
                "  " + std::to_string((int)std::lround(Progress * 100.0f)) + "%";
        }
    }

    if (mState.InfoPanel.StatusLine.empty())
        mState.InfoPanel.StatusLine = "Ready";
}

} // namespace Game::Client::UI
