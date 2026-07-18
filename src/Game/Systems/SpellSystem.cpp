#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cmath>

namespace Game::Systems
{

using namespace Game::Components;
using Core::Utils::Logger;

namespace
{
    bool IsMechanicalLockdownTarget(Core::ECS::EntityManager& EM, int EntityID)
    {
        const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        if (!Unit || Unit->HitPoints <= 0)
            return false;
        if (EM.GetComponent<ResourceComponent>(EntityID))
            return false;
        if (EM.GetComponent<BuildingComponent>(EntityID))
            return false;

        const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        if (DefID >= 0)
        {
            const auto Def = Game::Races::GetUnitDefByID(DefID);
            if (Def.IsBuilding)
                return false;

            switch (Def.RaceType)
            {
                case Race::Terran:
                    return Def.Name != "SCV" &&
                           Def.Name != "Marine" &&
                           Def.Name != "Firebat" &&
                           Def.Name != "Medic" &&
                           Def.Name != "Ghost";
                case Race::Protoss:
                    return Def.Name == "Dragoon" ||
                           Def.Name == "Reaver" ||
                           Def.Name == "Shuttle" ||
                           Def.Name == "Scout" ||
                           Def.Name == "Corsair" ||
                           Def.Name == "Carrier" ||
                           Def.Name == "Arbiter" ||
                           Def.Name == "Observer";
                case Race::Zerg:
                    return Def.Name == "Infested Terran";
            }
        }

        if (Unit->UnitRace == Race::Terran)
            return Unit->SizeType != UnitSize::Small;
        if (Unit->UnitRace == Race::Protoss)
            return Unit->SizeType != UnitSize::Small;
        return false;
    }

    void ClearMotionAfterTeleport(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->TargetSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }

        if (auto* Cmd = EM.GetComponent<CommandComponent>(EntityID))
        {
            Cmd->CancelAll();
            Cmd->CommandQueue.clear();
            Cmd->TargetEntityID = -1;
            Cmd->TargetPosition = {};
        }
    }

    bool HasHallucinationStatus(Core::ECS::EntityManager& EM, int EntityID)
    {
        const auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
        return Status && Status->HasEffect(StatusEffect::Hallucination);
    }

    void ResetHallucinationPath(PathfindingComponent& Path,
                                const Core::Math::Vec2& SpawnPosition)
    {
        Path.Path.clear();
        Path.PathIndex = 0;
        Path.RequestedDestination = SpawnPosition;
        Path.RequestedTargetEntityID = -1;
        Path.RequestedIgnoreEntityID = -1;
        Path.RepathFailCount = 0;
        Path.LastRepathTime = 0.0f;
        Path.BlockedByEntityID = -1;
        Path.CurrentVelocity = {};
        Path.CurrentSpeed = 0.0f;
        Path.TargetSpeed = 0.0f;
        Path.RepulseVector = {};
        Path.FriendlyYieldVector = {};
        Path.FriendlyYieldAnchor = {};
        Path.HasFriendlyYieldAnchor = false;
        Path.HasRecentMoveFailure = false;
        Path.RecentFailedMoveTarget = {};
        Path.RecentMoveFailureCooldown = 0.0f;
        Path.StallTimer = 0.0f;
        Path.LastWaypointDistance = 0.0f;
        Path.HasLastWaypointDistance = false;
        Path.DestinationStallTimer = 0.0f;
        Path.LastDestinationDistance = 0.0f;
        Path.HasLastDestinationDistance = false;
    }

    void ResetHallucinationCombat(CombatComponent& Combat)
    {
        Combat.CooldownRemaining = 0.0f;
        Combat.TargetEntityID = -1;
        Combat.PendingAttackFrame = false;
        Combat.PendingAttackTargetEntityID = -1;
        Combat.PendingAttackTargetPos = {};
        Combat.PendingAttackDamage = 0;
        Combat.PendingAttackUpgradeBonus = 0;
        Combat.PendingAttackHitCount = 1;
        Combat.PendingAttackRangePixels = 0.0f;
        Combat.PendingAttackElapsed = 0.0f;
        Combat.PendingAttackMissedAtLaunch = false;
        Combat.AutoTargetEntityID = -1;
        Combat.LastDamagerEntityID = -1;
        Combat.AutoTargetLockSeconds = 0.0f;
    }
}

int SpellSystem::GetSpellEnergyCost(SpellType Spell)
{
    switch (Spell)
    {
        case SpellType::PsionicStorm:  return 75;
        case SpellType::YamatoGun:     return 150;
        case SpellType::Lockdown:      return 100;
        case SpellType::EMPShockwave:  return 100;
        case SpellType::ScannerSweep:  return 50;
        case SpellType::Hallucination: return 100;
        case SpellType::StasisField:   return 100;
        case SpellType::Recall:        return 150;
        case SpellType::DarkSwarm:     return 100;
        case SpellType::Plague:        return 150;
        case SpellType::Ensnare:       return 75;
        case SpellType::SpawnBroodling:return 150;
        case SpellType::Parasite:      return 75;
        case SpellType::Consume:       return 0;
        default: return 0;
    }
}

float SpellSystem::GetSpellCooldown(SpellType Spell)
{
    switch (Spell)
    {
        case SpellType::PsionicStorm:  return 3.0f;
        case SpellType::YamatoGun:     return 10.0f;
        case SpellType::Lockdown:      return 4.0f;
        case SpellType::EMPShockwave:  return 6.0f;
        case SpellType::ScannerSweep:  return 5.0f;
        case SpellType::StasisField:   return 4.0f;
        case SpellType::Recall:        return 6.0f;
        case SpellType::DarkSwarm:     return 4.0f;
        case SpellType::Plague:        return 4.0f;
        default: return 2.0f;
    }
}

bool SpellSystem::CanCastSpell(Core::ECS::EntityManager& EM, int CasterID, SpellType Spell,
                                int TargetEntityID, const Core::Math::Vec2& TargetPos,
                                std::string* OutReason)
{
    (void)TargetPos;
    auto* CasterUnit = EM.GetComponent<UnitComponent>(CasterID);
    auto* CasterTrans = EM.GetComponent<TransformComponent>(CasterID);
    auto* CasterPlayer = EM.GetComponent<PlayerComponent>(CasterID);
    if (!CasterUnit || !CasterTrans || !CasterPlayer)
    {
        if (OutReason) *OutReason = "invalid caster";
        return false;
    }

    if (HasHallucinationStatus(EM, CasterID))
    {
        if (OutReason) *OutReason = "hallucinations cannot cast spells";
        return false;
    }

    int EnergyCost = GetSpellEnergyCost(Spell);
    if (CasterUnit->EnergyPoints < EnergyCost)
    {
        if (OutReason) *OutReason = "not enough energy";
        return false;
    }

    // Check cooldown
    auto* SpellComp = EM.GetComponent<Components::SpellCooldown>(CasterID);
    if (SpellComp && SpellComp->CooldownRemaining > 0.0f)
    {
        if (OutReason) *OutReason = "spell on cooldown";
        return false;
    }

    if (TargetEntityID >= 0)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
        if (!TargetUnit || TargetUnit->HitPoints <= 0)
        {
            if (OutReason) *OutReason = "invalid target";
            return false;
        }
    }

    if (Spell == SpellType::Consume)
    {
        if (TargetEntityID < 0 || TargetEntityID == CasterID)
        {
            if (OutReason) *OutReason = "invalid consume target";
            return false;
        }
        auto* TargetPlayer = EM.GetComponent<PlayerComponent>(TargetEntityID);
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
        if (!TargetPlayer || !TargetUnit || TargetPlayer->PlayerID != CasterPlayer->PlayerID)
        {
            if (OutReason) *OutReason = "consume requires a friendly target";
            return false;
        }
        if (TargetUnit->UnitRace != Race::Zerg || EM.GetComponent<BuildingComponent>(TargetEntityID))
        {
            if (OutReason) *OutReason = "consume requires a friendly zerg unit";
            return false;
        }
    }

    if (Spell == SpellType::Lockdown)
    {
        if (TargetEntityID < 0 || !IsMechanicalLockdownTarget(EM, TargetEntityID))
        {
            if (OutReason) *OutReason = "lockdown requires a mechanical target";
            return false;
        }
    }


    if (Spell == SpellType::Hallucination)
    {
        const auto* TargetUnit = TargetEntityID >= 0
            ? EM.GetComponent<UnitComponent>(TargetEntityID) : nullptr;
        const auto* TargetPlayer = TargetEntityID >= 0
            ? EM.GetComponent<PlayerComponent>(TargetEntityID) : nullptr;
        const auto* TargetTransform = TargetEntityID >= 0
            ? EM.GetComponent<TransformComponent>(TargetEntityID) : nullptr;
        if (!TargetUnit || !TargetPlayer || !TargetTransform ||
            TargetUnit->HitPoints <= 0 ||
            TargetPlayer->PlayerID != CasterPlayer->PlayerID ||
            EM.GetComponent<BuildingComponent>(TargetEntityID) ||
            EM.GetComponent<ResourceComponent>(TargetEntityID) ||
            HasHallucinationStatus(EM, TargetEntityID))
        {
            if (OutReason) *OutReason = "hallucination requires a friendly real unit";
            return false;
        }
    }

    return true;
}

bool SpellSystem::CastSpell(Core::ECS::EntityManager& EM, int CasterID, SpellType Spell,
                             int TargetEntityID, const Core::Math::Vec2& TargetPos)
{
    std::string Reason;
    if (!CanCastSpell(EM, CasterID, Spell, TargetEntityID, TargetPos, &Reason))
    {
        Logger::Debug("SpellSystem", "Cannot cast: " + Reason);
        return false;
    }

    auto* CasterUnit = EM.GetComponent<UnitComponent>(CasterID);
    auto* CasterTrans = EM.GetComponent<TransformComponent>(CasterID);
    auto* CasterPlayer = EM.GetComponent<PlayerComponent>(CasterID);
    if (!CasterUnit || !CasterTrans || !CasterPlayer) return false;

    int EnergyCost = GetSpellEnergyCost(Spell);
    CasterUnit->EnergyPoints -= EnergyCost;

    // Apply cooldown
    auto* SpellComp = EM.GetComponent<Components::SpellCooldown>(CasterID);
    if (!SpellComp)
        SpellComp = &EM.AddComponent<Components::SpellCooldown>(CasterID);
    SpellComp->SpellType = (uint8_t)Spell;
    SpellComp->CooldownRemaining = GetSpellCooldown(Spell);

    Core::Math::Vec2 Pos = TargetPos;
    if (TargetEntityID >= 0)
    {
        if (auto* TTrans = EM.GetComponent<TransformComponent>(TargetEntityID))
            Pos = TTrans->Position;
    }

    switch (Spell)
    {
        case SpellType::PsionicStorm:
        {
            // AoE 112 damage over 7 seconds in 3-tile radius
            float Radius = 96.0f; // 3 tiles
            float Duration = 7.0f;
            float TickDamage = 112.0f / (Duration / 0.25f); // 4 ticks/sec

            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* UPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!UUnit || !UTrans || !UPlayer) continue;
                if (UUnit->HitPoints <= 0) continue;
                if (UPlayer->PlayerID == CasterPlayer->PlayerID) continue;

                float D = UTrans->Position.DistanceTo(Pos);
                if (D <= Radius)
                {
                    int Dmg = std::max(1, (int)TickDamage);
                    if (UUnit->ShieldPoints > 0)
                    {
                        if (Dmg <= UUnit->ShieldPoints)
                        { UUnit->ShieldPoints -= Dmg; Dmg = 0; }
                        else
                        { Dmg -= UUnit->ShieldPoints; UUnit->ShieldPoints = 0; }
                    }
                    UUnit->HitPoints = std::max(0, UUnit->HitPoints - Dmg);
                    if (UUnit->HitPoints <= 0)
                    {
                        RecordUnitDeath(EM, Uid, CasterID, true);
                        EM.DestroyEntity(Uid);
                    }
                }
            }
            Logger::Info("SpellSystem", "Psionic Storm cast at (" +
                std::to_string((int)Pos.X) + "," + std::to_string((int)Pos.Y) + ")");
            break;
        }

        case SpellType::YamatoGun:
        {
            if (TargetEntityID >= 0)
            {
                CombatComponent FakeCombat;
                FakeCombat.GroundDmgType = DamageType::Normal;
                FakeCombat.GroundDamage = 260;
                FakeCombat.AirDamage = 260;
                FakeCombat.WeaponBeh = WeaponBehavior::AppearOnTarget;
                FakeCombat.Splash = SplashType::None;
                FakeCombat.CanTargetGround = true;
                FakeCombat.CanTargetAir = true;
                FakeCombat.AttackRange = 10.0f;
                FakeCombat.AttackCooldown = 0.0f;
                CombatSystem::ApplyDamage(EM, TargetEntityID, FakeCombat, CasterID, 260);
            }
            Logger::Info("SpellSystem", "Yamato Gun fired");
            break;
        }

        case SpellType::Lockdown:
        {
            if (TargetEntityID >= 0)
            {
                auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
                if (!Status)
                    Status = &EM.AddComponent<StatusEffectComponent>(TargetEntityID);
                Status->AddEffect(StatusEffect::Lockdown, 15.0f, CasterID);
            }
            Logger::Info("SpellSystem", "Lockdown applied");
            break;
        }

        case SpellType::EMPShockwave:
        {
            float Radius = 96.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* UPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!UUnit || !UTrans || !UPlayer) continue;

                float D = UTrans->Position.DistanceTo(Pos);
                if (D <= Radius)
                {
                    UUnit->ShieldPoints = 0;
                    UUnit->EnergyPoints = 0;
                    if (auto* Cloak = EM.GetComponent<CloakComponent>(Uid);
                        Cloak && Cloak->IsCloaked && !Cloak->IsPassiveCloak && !Cloak->IsBurrowed)
                    {
                        Cloak->IsCloaked = false;
                    }
                    auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                    if (!Status)
                        Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                    Status->AddEffect(StatusEffect::EMP, 1.0f, CasterID);
                }
            }
            Logger::Info("SpellSystem", "EMP Shockwave detonated");
            break;
        }

        case SpellType::ScannerSweep:
        {
            constexpr float Radius = 160.0f;
            int RevealID = EM.CreateEntity();
            EM.AddComponent<TransformComponent>(RevealID).Position = Pos;
            auto& Reveal = EM.AddComponent<RevealAreaComponent>(RevealID);
            Reveal.PlayerID = CasterPlayer->PlayerID;
            Reveal.SightRange = std::max(1, (int)(Radius / 32.0f));
            Reveal.DurationRemaining = 8.0f;
            Reveal.ProvidesDetection = true;
            Logger::Info("SpellSystem", "Scanner Sweep at (" +
                std::to_string((int)Pos.X) + "," + std::to_string((int)Pos.Y) + ")");
            break;
        }

        case SpellType::StasisField:
        {
            float Radius = 64.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                if (!UUnit || !UTrans) continue;
                if (UTrans->Position.DistanceTo(Pos) <= Radius)
                {
                    auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                    if (!Status)
                        Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                    Status->AddEffect(StatusEffect::Stasis, 5.0f, CasterID);
                }
            }
            Logger::Info("SpellSystem", "Stasis Field cast");
            break;
        }

        case SpellType::Recall:
        {
            static const Core::Math::Vec2 kRecallOffsets[] =
            {
                {-32.0f, 0.0f},
                {32.0f, 0.0f},
                {0.0f, -32.0f},
                {0.0f, 32.0f},
                {-32.0f, -32.0f},
                {32.0f, -32.0f},
                {-32.0f, 32.0f},
                {32.0f, 32.0f},
            };

            constexpr float Radius = 96.0f;
            int RecalledCount = 0;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
            for (int Uid : Units)
            {
                if (Uid == CasterID) continue;
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* UPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!UUnit || !UTrans || !UPlayer) continue;
                if (UUnit->HitPoints <= 0) continue;
                if (UPlayer->PlayerID != CasterPlayer->PlayerID) continue;
                if (EM.GetComponent<BuildingComponent>(Uid) ||
                    EM.GetComponent<ResourceComponent>(Uid))
                {
                    continue;
                }
                if (UTrans->Position.DistanceTo(Pos) > Radius) continue;

                const auto& Offset =
                    kRecallOffsets[RecalledCount %
                                   (int)(sizeof(kRecallOffsets) / sizeof(kRecallOffsets[0]))];
                UTrans->Position = CasterTrans->Position + Offset;
                ClearMotionAfterTeleport(EM, Uid);
                ++RecalledCount;
            }

            Logger::Info("SpellSystem", "Recall cast, teleported " +
                std::to_string(RecalledCount) + " units");
            break;
        }

        case SpellType::DarkSwarm:
        {
            // Brood War represents Dark Swarm as a persistent spell entity.
            // Protection follows current position, so units entering or leaving
            // the cloud gain or lose cover immediately instead of retaining a
            // cast-time status snapshot.
            const int AreaID = EM.CreateEntity();
            EM.AddComponent<TransformComponent>(AreaID).Position = Pos;
            auto& Area = EM.AddComponent<SpellAreaComponent>(AreaID);
            Area.Effect = StatusEffect::DarkSwarm;
            Area.Radius = 96.0f;
            Area.DurationRemaining = 900.0f / 24.0f;
            Area.SourceEntityID = CasterID;
            Logger::Info("SpellSystem", "Dark Swarm cast");
            break;
        }

        case SpellType::Plague:
        {
            constexpr float HalfExtent = 64.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                if (!UUnit || !UTrans || Uid == CasterID || UUnit->IsInvincible)
                    continue;
                if (auto* ExistingStatus = EM.GetComponent<StatusEffectComponent>(Uid);
                    ExistingStatus && ExistingStatus->HasEffect(StatusEffect::Stasis))
                    continue;
                const auto* Burrow = EM.GetComponent<BurrowComponent>(Uid);
                const auto* Cloak = EM.GetComponent<CloakComponent>(Uid);
                if ((Burrow && (Burrow->IsBurrowed ||
                                Burrow->Phase == BurrowPhase::Burrowed)) ||
                    (Cloak && Cloak->IsBurrowed))
                    continue;
                const Core::Math::Vec2 Offset = UTrans->Position - Pos;
                if (std::abs(Offset.X) > HalfExtent ||
                    std::abs(Offset.Y) > HalfExtent)
                    continue;

                auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                if (!Status)
                    Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                Status->AddEffect(StatusEffect::Plague, 75.0f * 8.0f / 24.0f,
                                  CasterID);
            }
            Logger::Info("SpellSystem", "Plague cast");
            break;
        }

        case SpellType::Ensnare:
        {
            constexpr float Radius = 96.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* TargetUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* TargetTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* TargetPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!TargetUnit || !TargetTrans || !TargetPlayer) continue;
                if (TargetUnit->HitPoints <= 0) continue;
                if (TargetPlayer->PlayerID == CasterPlayer->PlayerID) continue;
                if (EM.GetComponent<ResourceComponent>(Uid)) continue;
                if (TargetTrans->Position.DistanceTo(Pos) > Radius) continue;

                auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                if (!Status)
                    Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                Status->AddEffect(StatusEffect::Ensnare, 8.0f, CasterID);

                if (auto* Cloak = EM.GetComponent<CloakComponent>(Uid))
                {
                    Cloak->IsCloaked = false;
                    Cloak->IsBurrowed = false;
                }
            }
            Logger::Info("SpellSystem", "Ensnare cast");
            break;
        }

        case SpellType::Parasite:
        {
            if (TargetEntityID >= 0)
            {
                auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
                if (!Status)
                    Status = &EM.AddComponent<StatusEffectComponent>(TargetEntityID);
                Status->AddEffect(StatusEffect::Parasite, 999.0f, CasterID);
            }
            Logger::Info("SpellSystem", "Parasite cast");
            break;
        }

        case SpellType::Hallucination:
        {
            // Brood War creates two full-vitality copies of the selected
            // friendly unit. They retain its movement, weapon, armor and
            // presentation data, but cannot deal damage or cast spells.
            auto* SourceUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
            auto* SourceTrans = EM.GetComponent<TransformComponent>(TargetEntityID);
            auto* SourcePlayer = EM.GetComponent<PlayerComponent>(TargetEntityID);
            if (SourceUnit && SourceTrans && SourcePlayer)
            {
                // Entity/component pools may grow while the two copies are
                // created, so snapshot every source value before adding any
                // new component rather than retaining invalidatable pointers.
                const UnitComponent SourceUnitCopy = *SourceUnit;
                const TransformComponent SourceTransformCopy = *SourceTrans;
                const PlayerComponent SourcePlayerCopy = *SourcePlayer;
                const auto* SourceRender = EM.GetComponent<RenderComponent>(TargetEntityID);
                const auto* SourcePath = EM.GetComponent<PathfindingComponent>(TargetEntityID);
                const auto* SourceCombat = EM.GetComponent<CombatComponent>(TargetEntityID);
                const auto* SourceCloak = EM.GetComponent<CloakComponent>(TargetEntityID);
                const bool HasSourceRender = SourceRender != nullptr;
                const bool HasSourcePath = SourcePath != nullptr;
                const bool HasSourceCombat = SourceCombat != nullptr;
                const bool HasSourceCloak = SourceCloak != nullptr;
                const RenderComponent SourceRenderCopy =
                    SourceRender ? *SourceRender : RenderComponent{};
                const PathfindingComponent SourcePathCopy =
                    SourcePath ? *SourcePath : PathfindingComponent{};
                const CombatComponent SourceCombatCopy =
                    SourceCombat ? *SourceCombat : CombatComponent{};
                const CloakComponent SourceCloakCopy =
                    SourceCloak ? *SourceCloak : CloakComponent{};
                for (int I = 0; I < 2; I++)
                {
                    int IllusionID = EM.CreateEntity();
                    auto& ITrans = EM.AddComponent<TransformComponent>(IllusionID);
                    auto& IUnit = EM.AddComponent<UnitComponent>(IllusionID);
                    auto& IPlayer = EM.AddComponent<PlayerComponent>(IllusionID);
                    auto& IStatus = EM.AddComponent<StatusEffectComponent>(IllusionID);

                    ITrans = SourceTransformCopy;
                    ITrans.Position.X += (float)((I == 0 ? -32.0f : 32.0f));
                    IUnit = SourceUnitCopy;
                    // Hallucinations never reserve or release real supply.
                    IUnit.SupplyCost = 0;
                    IUnit.HitPoints = IUnit.MaxHitPoints;
                    IUnit.ShieldPoints = IUnit.MaxShieldPoints;
                    IUnit.HitPointSubDamage = 0;
                    IUnit.ShieldSubDamage = 0;
                    IUnit.ShieldRegenAccumulator = 0.0f;
                    IUnit.HealthRegenAccumulator = 0.0f;
                    IUnit.RepairHitPointAccumulator = 0.0f;
                    IUnit.RepairMineralCostAccumulator = 0.0f;
                    IUnit.RepairGasCostAccumulator = 0.0f;
                    IUnit.RepairTargetEntityID = -1;
                    IUnit.PlagueDamageAccumulator = 0.0f;
                    IUnit.IrradiateDamageAccumulator = 0.0f;
                    IUnit.EnergyPoints = 0;
                    IUnit.HangarBuildProgress = 0.0f;
                    IUnit.HangarBuildInProgress = false;
                    IPlayer = SourcePlayerCopy;
                    if (HasSourceRender)
                        EM.AddComponent<RenderComponent>(IllusionID) = SourceRenderCopy;
                    if (HasSourcePath)
                    {
                        auto& IPath = EM.AddComponent<PathfindingComponent>(IllusionID);
                        IPath = SourcePathCopy;
                        ResetHallucinationPath(IPath, ITrans.Position);
                    }
                    if (HasSourceCombat)
                    {
                        auto& ICombat = EM.AddComponent<CombatComponent>(IllusionID);
                        ICombat = SourceCombatCopy;
                        ResetHallucinationCombat(ICombat);
                    }
                    if (HasSourceCloak)
                    {
                        auto& ICloak = EM.AddComponent<CloakComponent>(IllusionID);
                        ICloak = SourceCloakCopy;
                        ICloak.IsBurrowed = false;
                    }
                    EM.AddComponent<CommandComponent>(IllusionID);
                    IStatus.AddEffect(StatusEffect::Hallucination, 180.0f, CasterID);
                }
            }
            Logger::Info("SpellSystem", "Hallucination cast");
            break;
        }

        case SpellType::SpawnBroodling:
        {
            if (TargetEntityID >= 0)
            {
                auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
                if (TargetUnit && TargetUnit->HitPoints > 0)
                {
                    // Kill target, spawn 2 broodlings
                    TargetUnit->HitPoints = 0;
                    RecordUnitDeath(EM, TargetEntityID, CasterID, true);
                    EM.DestroyEntity(TargetEntityID);

                    const int BroodlingDefID = Game::Races::GetUnitDefID("Broodling");
                    const auto BroodlingDef = Game::Races::GetUnitDefByID(BroodlingDefID);
                    for (int I = 0; I < 2 && BroodlingDef.Name != "Unknown"; I++)
                    {
                        int BroodID = EM.CreateEntity();
                        auto& BTrans = EM.AddComponent<TransformComponent>(BroodID);
                        auto& BUnit = EM.AddComponent<UnitComponent>(BroodID);
                        auto& BPlayer = EM.AddComponent<PlayerComponent>(BroodID);
                        auto& BCombat = EM.AddComponent<CombatComponent>(BroodID);
                        auto& BCommand = EM.AddComponent<CommandComponent>(BroodID);
                        auto& BPath = EM.AddComponent<PathfindingComponent>(BroodID);
                        auto& BRender = EM.AddComponent<RenderComponent>(BroodID);

                        BTrans.Position = Pos;
                        BTrans.Position.X += (float)(I * 24.0f);
                        BUnit.UnitRace = BroodlingDef.RaceType;
                        BUnit.ChkUnitID = BroodlingDef.ChkUnitID;
                        BUnit.HitPoints = BroodlingDef.HitPoints;
                        BUnit.MaxHitPoints = BroodlingDef.HitPoints;
                        BUnit.ShieldPoints = BroodlingDef.ShieldPoints;
                        BUnit.MaxShieldPoints = BroodlingDef.ShieldPoints;
                        BUnit.EnergyPoints = BroodlingDef.EnergyPoints;
                        BUnit.MaxEnergyPoints = BroodlingDef.EnergyPoints;
                        BUnit.Armor = BroodlingDef.Armor;
                        BUnit.Speed = BroodlingDef.Speed;
                        BUnit.SightRange = BroodlingDef.SightRange;
                        BUnit.SupplyCost = BroodlingDef.SupplyCost;
                        BUnit.SizeType = BroodlingDef.SizeType;
                        BPlayer = *CasterPlayer;

                        Races::ApplyCombatDefinition(BCombat, BroodlingDef, false);

                        BCommand.CurrentCommand = CommandType::Stop;
                        BPath.NodeTolerance = 8.0f;
                        BPath.IsAir = false;

                        BRender.ImageID = (uint32_t)BroodlingDef.ChkUnitID;
                        BRender.FrameIndex = 0;
                        BRender.TotalFrames = 1;
                        BRender.Tint = Core::Colors::White();
                        BRender.Layer = RenderLayer::Units;
                    }
                }
            }
            Logger::Info("SpellSystem", "Spawn Broodling cast");
            break;
        }

        case SpellType::Consume:
        {
            if (TargetEntityID >= 0)
            {
                if (auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
                    TargetUnit && TargetUnit->HitPoints > 0)
                {
                    TargetUnit->HitPoints = 0;
                    RecordUnitDeath(EM, TargetEntityID, CasterID, true);
                    EM.DestroyEntity(TargetEntityID);
                    UpgradeSystem::RefreshUnitEnergyCap(EM, CasterID);
                    const int MaxEnergy = std::max(
                        CasterUnit->MaxEnergyPoints,
                        UpgradeSystem::GetEffectiveMaxEnergyPoints(EM, CasterID));
                    CasterUnit->MaxEnergyPoints = MaxEnergy;
                    CasterUnit->EnergyPoints =
                        std::min(MaxEnergy, CasterUnit->EnergyPoints + 50);
                }
            }
            Logger::Info("SpellSystem", "Consume cast");
            break;
        }

        default: break;
    }

    return true;
}

void SpellSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Areas = EM.GetEntitiesWith<Components::SpellAreaComponent>();
    for (int Id : Areas)
    {
        auto* Area = EM.GetComponent<Components::SpellAreaComponent>(Id);
        if (!Area) continue;
        Area->DurationRemaining -= DeltaTime;
        if (Area->DurationRemaining <= 0.0f)
            EM.DestroyEntity(Id);
    }

    auto Entities = EM.GetEntitiesWith<Components::SpellCooldown>();
    for (int Id : Entities)
    {
        auto* SC = EM.GetComponent<Components::SpellCooldown>(Id);
        if (SC && SC->CooldownRemaining > 0.0f)
            SC->CooldownRemaining -= DeltaTime;
    }
}

} // namespace Game::Systems
