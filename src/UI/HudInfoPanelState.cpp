#include "UI/Hud.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Game/MovementTuning.h"
#include "Game/Systems/UpgradeSystem.h"
#include "UI/HudText.h"

namespace Game::Client::UI
{

void Hud::ResetInfoPanel()
{
    mState.InfoPanel = {};
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
        mState.InfoPanel.SightValue = Unit->SightRange;
        mState.InfoPanel.SpeedValue = (int)std::lround(Game::Tuning::ToWorldMoveSpeed(Unit->Speed));
        mState.InfoPanel.TypeLine = RaceLabel(Unit->UnitRace) +
                              std::string(mState.InfoPanel.IsBuilding ? " Structure" : " Unit");
        mState.InfoPanel.StatLine =
            "Armor " + std::to_string(Unit->Armor) +
            "  Sight " + std::to_string(Unit->SightRange) +
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
            mState.InfoPanel.QueueLabel = QueueLabel;
            mState.InfoPanel.QueueEntries = {ResearchName};
            mState.InfoPanel.StatusLine = std::string(Verb) + ResearchName;
            mState.InfoPanel.DetailLine =
                std::string(DetailPrefix) +
                std::to_string((int)std::lround(mState.InfoPanel.Progress * 100.0f)) +
                "%";
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
        for (size_t Index = 0; Index < Production->Queue.size() && Index < 5; ++Index)
        {
            mState.InfoPanel.QueueEntries.push_back(
                DisplayLabelForDef(Production->Queue[Index]));
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
