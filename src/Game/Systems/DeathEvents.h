#pragma once

#include <algorithm>

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/HarvestSystem.h"

namespace Game::Systems
{

inline void SpawnDeathVisual(Core::ECS::EntityManager& EM,
                             int VictimEntityID,
                             const Game::Components::UnitComponent& VictimUnit)
{
    auto* VictimTransform =
        EM.GetComponent<Game::Components::TransformComponent>(VictimEntityID);
    if (!VictimTransform)
        return;

    const auto* VictimRender =
        EM.GetComponent<Game::Components::RenderComponent>(VictimEntityID);

    const int VisualID = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(VisualID) = *VictimTransform;

    auto& Render = EM.AddComponent<Game::Components::RenderComponent>(VisualID);
    if (VictimRender)
        Render = *VictimRender;
    else
        Render.ImageID = (uint32_t)VictimUnit.ChkUnitID;
    Render.Layer = Game::Components::RenderLayer::Effects;
    Render.ZOrder += 1;
    Render.Visible = true;
    Render.TotalFrames = std::max(1, Render.TotalFrames);

    auto& Effect = EM.AddComponent<Game::Components::EffectComponent>(VisualID);
    Effect.LifetimeRemaining = 1.0f;
    Effect.ParentEntityID = -1;
    Effect.FollowParent = false;

    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(VisualID);
    Unit = VictimUnit;
    Unit.HitPoints = 0;
    Unit.SupplyCost = 0;
}

inline void SettleSupplyForDeath(Core::ECS::EntityManager& EM,
                                 int VictimEntityID,
                                 const Game::Components::UnitComponent& VictimUnit,
                                 int VictimPlayerID)
{
    if (VictimPlayerID < 0 || VictimPlayerID >= kPlayerResourceSlots)
        return;

    auto& Resources = HarvestSystem::GetResources(VictimPlayerID);
    const int DefID = Game::Races::GetUnitDefIDByChkUnitID(VictimUnit.ChkUnitID);
    const auto Def = Game::Races::GetUnitDefByID(DefID);
    const auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(VictimEntityID);
    const auto* Morph = EM.GetComponent<Game::Components::MorphComponent>(VictimEntityID);

    if (DefID >= 0)
    {
        const int SupplyProvided = Game::Races::GetSupplyProvidedByDefID(DefID);
        const bool ExistingProvider =
            !Building ||
            !Building->UnderConstruction ||
            (Morph && Morph->SourceUnitID == VictimEntityID);
        if (SupplyProvided > 0 && ExistingProvider)
        {
            Resources.SupplyMax = std::max(0, Resources.SupplyMax - SupplyProvided);
        }
    }

    if (auto* Production = EM.GetComponent<Game::Components::ProductionComponent>(VictimEntityID))
    {
        int QueuedSupply = 0;
        if (!Production->Queue.empty())
        {
            for (int QueuedDefID : Production->Queue)
            {
                const auto QueuedDef = Game::Races::GetUnitDefByID(QueuedDefID);
                QueuedSupply += QueuedDef.SupplyCost;
            }
        }
        else if (Production->CurrentTrainingUnitDefID >= 0)
        {
            const auto QueuedDef =
                Game::Races::GetUnitDefByID(Production->CurrentTrainingUnitDefID);
            QueuedSupply += QueuedDef.SupplyCost;
        }

        if (QueuedSupply > 0)
            Resources.SupplyUsed = std::max(0, Resources.SupplyUsed - QueuedSupply);

        Production->Queue.clear();
        Production->CurrentTrainingUnitDefID = -1;
        Production->TrainingProgress = 0.0f;
        Production->TrainingTimePerUnit = 0.0f;
    }
    if (auto* MutableBuilding = EM.GetComponent<Game::Components::BuildingComponent>(VictimEntityID))
    {
        MutableBuilding->BuildTimeRemaining = 0.0f;
        MutableBuilding->IsResearching = false;
        MutableBuilding->ResearchingID = -1;
        MutableBuilding->ResearchPlayer = -1;
    }

    int SupplyUsed = 0;
    if (Morph && Morph->IsMorphing && Morph->MorphTargetDefID >= 0)
    {
        const auto TargetDef = Game::Races::GetUnitDefByID(Morph->MorphTargetDefID);
        if (!TargetDef.IsBuilding)
            SupplyUsed = std::max(SupplyUsed, TargetDef.SupplyCost);
    }
    if (!Building && !Def.IsBuilding)
        SupplyUsed = std::max(SupplyUsed, VictimUnit.SupplyCost);

    if (SupplyUsed > 0)
        Resources.SupplyUsed = std::max(0, Resources.SupplyUsed - SupplyUsed);
}

inline void RecordUnitDeath(Core::ECS::EntityManager& EM,
                            int VictimEntityID,
                            int KillerEntityID = -1,
                            bool AwardKill = true)
{
    auto* VictimUnit = EM.GetComponent<Game::Components::UnitComponent>(VictimEntityID);
    auto* VictimPlayer = EM.GetComponent<Game::Components::PlayerComponent>(VictimEntityID);
    if (!VictimUnit || !VictimPlayer)
        return;

    auto* Existing = EM.GetComponent<Game::Components::DeathRecordComponent>(VictimEntityID);
    if (Existing)
        return;

    SettleSupplyForDeath(EM, VictimEntityID, *VictimUnit, VictimPlayer->PlayerID);
    SpawnDeathVisual(EM, VictimEntityID, *VictimUnit);

    auto& Record = EM.AddComponent<Game::Components::DeathRecordComponent>(VictimEntityID);
    Record.VictimPlayerID = VictimPlayer->PlayerID;
    Record.VictimUnitID = VictimUnit->ChkUnitID;
    Record.KillerEntityID = KillerEntityID;

    if (KillerEntityID >= 0)
    {
        if (auto* KillerPlayer = EM.GetComponent<Game::Components::PlayerComponent>(KillerEntityID))
            Record.KillerPlayerID = KillerPlayer->PlayerID;
    }
    Record.AwardKill = AwardKill &&
        Record.KillerPlayerID >= 0 &&
        Record.KillerPlayerID != Record.VictimPlayerID;
}

} // namespace Game::Systems
