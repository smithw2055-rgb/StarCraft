#pragma once

#include <algorithm>
#include <cmath>

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/IScriptVM.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"

namespace Game::Systems
{

inline void SpawnDeathVisual(Core::ECS::EntityManager& EM,
                             int VictimEntityID,
                             const Game::Components::UnitComponent& VictimUnit,
                             bool ForceHallucinationBurst = false)
{
    auto* VictimTransform =
        EM.GetComponent<Game::Components::TransformComponent>(VictimEntityID);
    const auto* VictimRender =
        EM.GetComponent<Game::Components::RenderComponent>(VictimEntityID);
    if (!VictimTransform || !VictimRender)
        return;

    const auto* VictimStatus =
        EM.GetComponent<Game::Components::StatusEffectComponent>(VictimEntityID);
    const auto* VictimPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(VictimEntityID);
    const auto* VictimCombat =
        EM.GetComponent<Game::Components::CombatComponent>(VictimEntityID);
    const auto* VictimBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(VictimEntityID);
    const bool IsKilledHallucination =
        ForceHallucinationBurst ||
        (VictimStatus && VictimStatus->HasEffect(
            Game::Components::StatusEffect::Hallucination));

    const int VisualID = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(VisualID) = *VictimTransform;

    auto& Render = EM.AddComponent<Game::Components::RenderComponent>(VisualID);
    Render = *VictimRender;
    // OpenBW uses an independent Hallucination Death thingy when a copy is
    // killed. Natural lifetime expiry has already removed the status before
    // reaching this helper and therefore follows the unit's normal Death.
    if (IsKilledHallucination)
    {
        constexpr uint32_t kHallucinationDeathImageID = 557u;
        Render.ImageID = kHallucinationDeathImageID;
        Render.SubunitImageID = UINT32_MAX;
        Render.AddonConnectorImageID = UINT32_MAX;
        Render.VisualOffset = {};
        Render.FrameIndex = 0;
        Render.Flipped = false;
        Render.Visible = true;
    }
    Render.Layer = Game::Components::RenderLayer::Effects;
    if (IsKilledHallucination)
        Render.ZOrder += 1;
    Render.Visible = true;
    Render.TotalFrames = std::max(1, Render.TotalFrames);

    auto& Effect = EM.AddComponent<Game::Components::EffectComponent>(VisualID);
    Effect.LifetimeRemaining = 8.0f;
    Effect.ParentEntityID = -1;
    Effect.FollowParent = false;
    Effect.InitialAnimation = IsKilledHallucination
        ? Game::IScriptAnims::Anim_Init
        : Game::IScriptAnims::Anim_Death;
    Effect.DestroyWhenAnimationCompletes = true;
    Effect.DrawAbove = IsKilledHallucination;
    const bool WasAirborne =
        (VictimPath && VictimPath->IsAir) ||
        (VictimCombat &&
         VictimCombat->MyElevation == Game::Components::Elevation::Air) ||
        (VictimBuilding && VictimBuilding->IsLifted);
    Effect.RenderBand = IsKilledHallucination ? 3 : (WasAirborne ? 2 : 1);
    Effect.SpawnSoundID = IsKilledHallucination ? 619 : -1;
    if (VictimPath)
    {
        const Core::Math::Vec2 Facing{
            std::cos(VictimPath->FacingAngle),
            std::sin(VictimPath->FacingAngle)};
        float Angle = std::atan2(Facing.X, -Facing.Y);
        if (Angle < 0.0f)
            Angle += 6.28318530718f;
        Effect.IScriptDirection =
            (int)std::lround(Angle * 256.0f / 6.28318530718f) & 0xff;
    }

    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(VisualID);
    Unit = VictimUnit;
    Unit.HitPoints = 0;
    Unit.SupplyCost = 0;

    if (const auto* VictimPlayer =
            EM.GetComponent<Game::Components::PlayerComponent>(VictimEntityID))
    {
        EM.AddComponent<Game::Components::PlayerComponent>(VisualID) = *VictimPlayer;
    }
    if (!IsKilledHallucination)
    {
        if (VictimBuilding)
        {
            auto& VisualBuilding =
                EM.AddComponent<Game::Components::BuildingComponent>(VisualID);
            VisualBuilding.IsBuilding = true;
            VisualBuilding.IsLifted = VictimBuilding->IsLifted;
            VisualBuilding.FlightPhase = VictimBuilding->FlightPhase;
            VisualBuilding.IsAddon = VictimBuilding->IsAddon;
            VisualBuilding.BuildingDefID = VictimBuilding->BuildingDefID;
            VisualBuilding.UnderConstruction = false;
        }
        if (const auto* VictimTree =
                EM.GetComponent<Game::Components::ImageTreeComponent>(VictimEntityID))
        {
            EM.AddComponent<Game::Components::ImageTreeComponent>(VisualID) =
                *VictimTree;
        }
    }
}

inline void SettleSupplyForDeath(Core::ECS::EntityManager& EM,
                                 int VictimEntityID,
                                 const Game::Components::UnitComponent& VictimUnit,
                                 int VictimPlayerID)
{
    if (VictimPlayerID < 0 || VictimPlayerID >= kPlayerResourceSlots)
        return;

    // Hallucinations are visual combat copies: they never reserved unit
    // supply, queued production, or provider capacity in the real economy.
    if (const auto* Status =
            EM.GetComponent<Game::Components::StatusEffectComponent>(VictimEntityID);
        Status && Status->HasEffect(Game::Components::StatusEffect::Hallucination))
    {
        return;
    }

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
        if (!UpgradeSystem::CancelResearch(EM, VictimEntityID, true))
        {
            MutableBuilding->BuildTimeRemaining = 0.0f;
            MutableBuilding->IsResearching = false;
            MutableBuilding->ResearchingID = -1;
            MutableBuilding->ResearchPlayer = -1;
        }
    }

    int SupplyUsed = 0;
    if (auto* Shared =
            EM.GetComponent<Game::Components::SharedSupplyComponent>(VictimEntityID);
        Shared && Shared->ReservedSupply > 0)
    {
        const auto* PartnerUnit =
            EM.GetComponent<Game::Components::UnitComponent>(Shared->PartnerEntityID);
        const bool PartnerStillActive =
            Shared->PartnerEntityID >= 0 &&
            EM.IsAlive(Shared->PartnerEntityID) &&
            PartnerUnit && PartnerUnit->HitPoints > 0 &&
            !EM.GetComponent<Game::Components::DeathRecordComponent>(
                Shared->PartnerEntityID);
        if (!PartnerStillActive)
        {
            SupplyUsed = Shared->ReservedSupply;
            Shared->ReservedSupply = 0;
            if (auto* PartnerShared =
                    EM.GetComponent<Game::Components::SharedSupplyComponent>(
                        Shared->PartnerEntityID))
            {
                PartnerShared->ReservedSupply = 0;
            }
        }
    }
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

    const bool IsHallucination =
        (EM.GetComponent<Game::Components::StatusEffectComponent>(VictimEntityID) &&
         EM.GetComponent<Game::Components::StatusEffectComponent>(VictimEntityID)
             ->HasEffect(Game::Components::StatusEffect::Hallucination));
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
    Record.AwardKill = AwardKill && !IsHallucination &&
        Record.KillerPlayerID >= 0 &&
        Record.KillerPlayerID != Record.VictimPlayerID;
}

} // namespace Game::Systems
