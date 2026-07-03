#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Components.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <limits>

namespace Game::Systems
{

using namespace Game::Components;

static float GetSizeDamageModifier(DamageType DmgType, UnitSize TargetSize)
{
    switch (DmgType)
    {
        case DamageType::Concussive:
            switch (TargetSize)
            {
                case UnitSize::Small:  return 1.0f;
                case UnitSize::Medium: return 0.5f;
                case UnitSize::Large:  return 0.25f;
            }
            break;
        case DamageType::Explosive:
            switch (TargetSize)
            {
                case UnitSize::Small:  return 0.5f;
                case UnitSize::Medium: return 0.75f;
                case UnitSize::Large:  return 1.0f;
            }
            break;
        case DamageType::Normal:
        default:
            return 1.0f;
    }
    return 1.0f;
}

static void IssueMovePreservingCommand(Core::ECS::EntityManager& EM,
                                       int EntityID,
                                       const Core::Math::Vec2& MoveTarget,
                                       CommandType Command,
                                       const Core::Math::Vec2& CommandTarget,
                                       int TargetEntityID)
{
    CommandSystem::IssueMove(EM, {EntityID}, MoveTarget);

    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd)
        Cmd = &EM.AddComponent<CommandComponent>(EntityID);
    Cmd->CurrentCommand = Command;
    Cmd->TargetPosition = CommandTarget;
    Cmd->TargetEntityID = TargetEntityID;
}

void CombatSystem::IssueAttack(Core::ECS::EntityManager& EM,
                                const std::vector<int>& Attackers,
                                int TargetEntityID)
{
    auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetEntityID);
    if (!TargetTrans) return;

    for (int ID : Attackers)
    {
        auto* Combat = EM.GetComponent<CombatComponent>(ID);
        if (!Combat) continue;

        Combat->TargetEntityID = TargetEntityID;

        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::Attack;
        Cmd->TargetEntityID = TargetEntityID;

        IssueMovePreservingCommand(EM, ID, TargetTrans->Position,
                                   CommandType::Attack, TargetTrans->Position, TargetEntityID);
    }
}

void CombatSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                    const std::vector<int>& Units,
                                    Core::Math::Vec2 Target)
{
    for (int ID : Units)
    {
        CommandSystem::IssueAttackMove(EM, {ID}, Target);
    }
}

bool CombatSystem::AreEnemies(int A, int B, Core::ECS::EntityManager& EM)
{
    auto* PA = EM.GetComponent<PlayerComponent>(A);
    auto* PB = EM.GetComponent<PlayerComponent>(B);
    if (!PA || !PB) return false;
    return PA->PlayerID != PB->PlayerID;
}

int CombatSystem::FindNearestEnemy(Core::ECS::EntityManager& EM, int EntityID, float MaxRange)
{
    auto* MyTrans = EM.GetComponent<TransformComponent>(EntityID);
    if (!MyTrans) return -1;

    int BestID = -1;
    float BestDist = MaxRange;

    auto Entities = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
    for (int ID : Entities)
    {
        if (ID == EntityID) continue;
        if (!AreEnemies(EntityID, ID, EM)) continue;

        auto* TargetTrans = EM.GetComponent<TransformComponent>(ID);
        auto* TargetUnit = EM.GetComponent<UnitComponent>(ID);
        if (!TargetTrans || !TargetUnit) continue;
        if (TargetUnit->HitPoints <= 0) continue;

        // Skip cloaked units we can't detect
        auto* Cloak = EM.GetComponent<CloakComponent>(ID);
        if (Cloak && (Cloak->IsCloaked || Cloak->IsBurrowed))
        {
            auto* MyUnit = EM.GetComponent<UnitComponent>(EntityID);
            bool IsDetector = MyUnit && MyUnit->IsDetector;
            if (!IsDetector) continue;
        }

        float Dist = MyTrans->Position.DistanceTo(TargetTrans->Position);
        if (Dist < BestDist)
        {
            BestDist = Dist;
            BestID = ID;
        }
    }
    return BestID;
}

int CombatSystem::PickWeaponDamage(const CombatComponent& Combat,
                                    const UnitComponent* TargetUnit) const
{
    if (!TargetUnit) return 0;

    // Pick ground or air weapon based on what the target accepts and what we have
    // For now, prefer ground unless the unit explicitly targets air only
    if (Combat.CanTargetAir && Combat.AirDamage > 0 && !Combat.CanTargetGround)
        return Combat.AirDamage;
    if (Combat.CanTargetGround && Combat.GroundDamage > 0)
        return Combat.GroundDamage;
    if (Combat.CanTargetAir && Combat.AirDamage > 0)
        return Combat.AirDamage;

    return Combat.GroundDamage > 0 ? Combat.GroundDamage : Combat.AirDamage;
}

bool CombatSystem::RollMissChance(const CombatComponent& Combat,
                                   const TransformComponent& AttackerTrans,
                                   const TransformComponent& TargetTrans,
                                   Core::ECS::EntityManager& EM) const
{
    // Cliff elevation miss: if attacker is on lower cliff than target, chance to miss
    // Higher ground advantage: 30% miss chance for lower attacker
    // Melee weapons always hit unless target is on significantly higher cliff
    if (Combat.WeaponBeh == WeaponBehavior::Melee)
        return false; // melee doesn't miss

    // For simplicity, check position distance-based miss: if target is at range limit, slight chance
    float Dist = AttackerTrans.Position.DistanceTo(TargetTrans.Position);
    float MaxRng = Combat.AttackRange * 32.0f;
    if (Dist > MaxRng * 0.85f)
    {
        // 5% miss at max range
        if (std::rand() % 100 < 5)
            return true;
    }

    return false;
}

void CombatSystem::ApplyDamage(Core::ECS::EntityManager& EM, int TargetID,
                                const CombatComponent& Attacker, int AttackerID,
                                int BaseDamage)
{
    auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
    if (!TargetUnit || TargetUnit->HitPoints <= 0) return;

    // Size-based damage modifier
    float SizeMod = GetSizeDamageModifier(Attacker.GroundDmgType, TargetUnit->SizeType);
    int UpgradedArmor = TargetUnit->Armor + UpgradeSystem::GetArmorBonus(EM, TargetID);
    int ModifiedDamage = std::max(1, (int)((float)BaseDamage * SizeMod));
    int EffectiveDamage = std::max(1, ModifiedDamage - UpgradedArmor);

    // Apply to shields first
    if (TargetUnit->ShieldPoints > 0)
    {
        if (EffectiveDamage <= TargetUnit->ShieldPoints)
        {
            TargetUnit->ShieldPoints -= EffectiveDamage;
            EffectiveDamage = 0;
        }
        else
        {
            EffectiveDamage -= TargetUnit->ShieldPoints;
            TargetUnit->ShieldPoints = 0;
        }
    }

    TargetUnit->HitPoints -= EffectiveDamage;
    if (TargetUnit->HitPoints <= 0)
    {
        TargetUnit->HitPoints = 0;
        RecordUnitDeath(EM, TargetID, AttackerID, true);
        EM.DestroyEntity(TargetID);
        Core::Utils::Logger::Info("CombatSystem",
            "Entity " + std::to_string(TargetID) + " destroyed by " +
            std::to_string(AttackerID));
    }
}

bool CombatSystem::ResolvePendingAttackFrame(Core::ECS::EntityManager& EM,
                                             BulletSystem& BulletSys,
                                             int AttackerID)
{
    auto* Combat = EM.GetComponent<CombatComponent>(AttackerID);
    auto* Trans = EM.GetComponent<TransformComponent>(AttackerID);
    auto* PlayerComp = EM.GetComponent<PlayerComponent>(AttackerID);
    if (!Combat || !Trans || !Combat->PendingAttackFrame)
        return false;

    const int TargetID = Combat->PendingAttackTargetEntityID;
    Core::Math::Vec2 TargetPos = Combat->PendingAttackTargetPos;
    if (TargetID >= 0)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
        if (!TargetUnit || TargetUnit->HitPoints <= 0 || !TargetTrans)
        {
            Combat->PendingAttackFrame = false;
            Combat->PendingAttackTargetEntityID = -1;
            return false;
        }
        TargetPos = TargetTrans->Position;
    }

    if (Combat->PendingAttackDamage <= 0)
    {
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackTargetEntityID = -1;
        return false;
    }

    if (Combat->WeaponBeh == WeaponBehavior::Melee)
    {
        if (TargetID >= 0)
            ApplyDamage(EM, TargetID, *Combat, AttackerID, Combat->PendingAttackDamage);
    }
    else if (PlayerComp)
    {
        BulletSys.SpawnBullet(EM, *Combat, *Trans, *PlayerComp,
            AttackerID, Combat->PendingAttackDamage, Combat->PendingAttackUpgradeBonus,
            Combat->PendingAttackRangePixels, TargetID, TargetPos);
    }

    Combat->PendingAttackFrame = false;
    Combat->PendingAttackTargetEntityID = -1;
    Combat->PendingAttackDamage = 0;
    Combat->PendingAttackUpgradeBonus = 0;
    Combat->PendingAttackElapsed = 0.0f;
    return true;
}

bool CombatSystem::BeginOrResolveAttack(Core::ECS::EntityManager& EM,
                                        int EntityID,
                                        int TargetID,
                                        const TransformComponent& TargetTrans,
                                        int TotalDamage,
                                        float RangePixels)
{
    auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
    auto* Render = EM.GetComponent<RenderComponent>(EntityID);
    if (!Combat)
        return false;

    const bool HasIScriptAttack =
        Render &&
        (AnimationSystem::HasIScriptAnimationForImage((int)Render->ImageID, Game::IScriptAnims::Anim_GndAttkInit) ||
         AnimationSystem::HasIScriptAnimationForImage((int)Render->ImageID, Game::IScriptAnims::Anim_AirAttkInit));

    if (HasIScriptAttack)
    {
        Combat->PendingAttackFrame = true;
        Combat->PendingAttackTargetEntityID = TargetID;
        Combat->PendingAttackTargetPos = TargetTrans.Position;
        Combat->PendingAttackDamage = TotalDamage;
        Combat->PendingAttackUpgradeBonus = 0;
        Combat->PendingAttackRangePixels = RangePixels;
        Combat->PendingAttackElapsed = 0.0f;
        return true;
    }

    Combat->PendingAttackFrame = true;
    Combat->PendingAttackTargetEntityID = TargetID;
    Combat->PendingAttackTargetPos = TargetTrans.Position;
    Combat->PendingAttackDamage = TotalDamage;
    Combat->PendingAttackUpgradeBonus = 0;
    Combat->PendingAttackRangePixels = RangePixels;
    Combat->PendingAttackElapsed = 0.0f;
    ResolvePendingAttackFrame(EM, mBulletSystem, EntityID);
    return true;
}

void CombatSystem::UpdateStatusEffects(Core::ECS::EntityManager& EM, float DeltaTime)
{
    auto Entities = EM.GetEntitiesWith<UnitComponent>();
    for (int ID : Entities)
    {
        auto* Status = EM.GetComponent<StatusEffectComponent>(ID);
        if (!Status) continue;

        Status->Tick(DeltaTime);

        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (!Unit || Unit->HitPoints <= 0) continue;

        // Plague: drain HP to minimum 1
        if (Status->HasEffect(StatusEffect::Plague))
        {
            int Drain = std::max(1, Unit->MaxHitPoints / 40);
            if (Unit->HitPoints > 1)
                Unit->HitPoints = std::max(1, Unit->HitPoints - Drain);
        }

        // Irradiate: AoE damage (handled per tick in BulletSystem Persist style)
        // For now, direct self-damage each second
        if (Status->HasEffect(StatusEffect::Irradiate))
        {
            // Irradiate deals ~10 damage per tick to the unit itself
            int IrradDmg = std::max(1, Unit->MaxHitPoints / 25);
            if (Unit->HitPoints > 1)
                Unit->HitPoints = std::max(1, Unit->HitPoints - IrradDmg);
        }

        // EMP: zero shields (handled on application), energy drain
        if (Status->HasEffect(StatusEffect::EMP))
        {
            Unit->ShieldPoints = 0;
            Unit->EnergyPoints = 0;
        }

        // Stasis: freeze unit (handled by disabling combat/movement in respective systems)
    }
}

void CombatSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();

    // Shield regeneration for all units with shields
    auto ShieldUnits = EM.GetEntitiesWith<UnitComponent>();
    for (int ID : ShieldUnits)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (Unit && Unit->ShieldPoints > 0 && Unit->ShieldPoints < Unit->MaxShieldPoints)
        {
            float Regen = 1.0f * DeltaTime;
            Unit->ShieldPoints = std::min(Unit->MaxShieldPoints,
                Unit->ShieldPoints + (int)(Regen));
        }
    }

    // Status effect tick
    UpdateStatusEffects(EM, DeltaTime);

    // Combat processing
    auto Entities = EM.GetEntitiesWith<TransformComponent, CombatComponent>();
    for (auto ID : Entities)
        ProcessCombat(EM, ID, DeltaTime);
}

void CombatSystem::ProcessCombat(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime)
{
    auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
    auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);

    if (!Combat || !Trans) return;

    // Check if unit is frozen by stasis/lockdown/maelstrom
    auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
    if (Status && (Status->HasEffect(StatusEffect::Stasis) ||
                   Status->HasEffect(StatusEffect::Lockdown) ||
                   Status->HasEffect(StatusEffect::Maelstrom)))
        return;

    // Reduce cooldown
    if (Combat->CooldownRemaining > 0.0f)
        Combat->CooldownRemaining -= DeltaTime;

    if (Combat->PendingAttackFrame)
    {
        Combat->PendingAttackElapsed += DeltaTime;
        if (Combat->PendingAttackElapsed >= 0.8f)
            ResolvePendingAttackFrame(EM, mBulletSystem, EntityID);
        return;
    }

    // HoldPosition: don't chase, fire at enemies in range without moving
    if (Cmd && Cmd->CurrentCommand == CommandType::HoldPosition)
    {
        int Nearby = FindNearestEnemy(EM, EntityID, Combat->AttackRange * 32.0f);
        if (Nearby >= 0)
        {
            Combat->TargetEntityID = Nearby;
            auto* TTrans = EM.GetComponent<TransformComponent>(Nearby);
            if (TTrans)
            {
                float D = Trans->Position.DistanceTo(TTrans->Position);
                if (D <= Combat->AttackRange * 32.0f && Combat->CooldownRemaining <= 0.0f)
                {
                    Combat->CooldownRemaining = Combat->AttackCooldown;

                    int Damage = PickWeaponDamage(*Combat, EM.GetComponent<UnitComponent>(Nearby));
                    if (Damage <= 0 && Combat->AirDamage > 0)
                        Damage = Combat->AirDamage;
                    if (Damage <= 0) Damage = Combat->GroundDamage;
                    if (Damage <= 0) return;

                    int UpgradeBonus = UpgradeSystem::GetWeaponBonus(EM, EntityID);
                    int TotalDamage = Damage + UpgradeBonus;

                    if (Combat->WeaponBeh != WeaponBehavior::Melee ||
                        !RollMissChance(*Combat, *Trans, *TTrans, EM))
                        BeginOrResolveAttack(EM, EntityID, Nearby, *TTrans, TotalDamage,
                                             Combat->AttackRange * 32.0f);
                }
            }
        }
        return;
    }

    int TargetID = -1;
    if (Cmd && (Cmd->CurrentCommand == CommandType::Attack ||
                Cmd->CurrentCommand == CommandType::AttackMove))
    {
        TargetID = Cmd->TargetEntityID;
    }
    if (TargetID < 0 && Combat->TargetEntityID >= 0)
    {
        TargetID = Combat->TargetEntityID;
    }

    // Validate target
    if (TargetID >= 0)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
        if (!TargetUnit || TargetUnit->HitPoints <= 0 || !TargetTrans)
        {
            TargetID = -1;
            Combat->TargetEntityID = -1;
            if (Cmd && Cmd->CurrentCommand == CommandType::Attack)
            {
                Cmd->CurrentCommand = CommandType::Stop;
                Cmd->TargetEntityID = -1;
            }
            else if (Cmd && Cmd->CurrentCommand == CommandType::AttackMove)
            {
                Cmd->TargetEntityID = -1;
            }
        }
    }

    // Find new target for attack-move
    if (TargetID < 0 && Cmd && Cmd->CurrentCommand == CommandType::AttackMove)
    {
        TargetID = FindNearestEnemy(EM, EntityID, Combat->AttackRange * 32.0f * 2.0f);
        if (TargetID >= 0)
        {
            const Core::Math::Vec2 AttackMoveDestination = Cmd->TargetPosition;
            Combat->TargetEntityID = TargetID;
            Cmd->TargetEntityID = TargetID;
            if (auto* TTrans = EM.GetComponent<TransformComponent>(TargetID))
                IssueMovePreservingCommand(EM, EntityID, TTrans->Position,
                                           CommandType::AttackMove, AttackMoveDestination, TargetID);
        }
    }

    if (TargetID < 0)
    {
        if (Cmd && Cmd->CurrentCommand == CommandType::AttackMove)
        {
            auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
            const bool HasActivePath = Path && Path->PathIndex < (int)Path->Path.size();
            if (!HasActivePath && Trans->Position.DistanceTo(Cmd->TargetPosition) > 8.0f)
            {
                IssueMovePreservingCommand(EM, EntityID, Cmd->TargetPosition,
                                           CommandType::AttackMove, Cmd->TargetPosition, -1);
            }
        }
        return;
    }

    auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
    if (!TargetTrans) return;

    // Check range
    float Dist = Trans->Position.DistanceTo(TargetTrans->Position);
    float RangePixels = Combat->AttackRange * 32.0f;

    if (Dist > RangePixels)
    {
        const CommandType PreserveCommand = Cmd ? Cmd->CurrentCommand : CommandType::Attack;
        const Core::Math::Vec2 PreserveTarget = Cmd ? Cmd->TargetPosition : TargetTrans->Position;
        const int PreserveTargetEntity = Cmd ? Cmd->TargetEntityID : TargetID;
        IssueMovePreservingCommand(EM, EntityID, TargetTrans->Position,
                                   PreserveCommand, PreserveTarget, PreserveTargetEntity);
        return;
    }

    // In range - stop and fire
    auto* PathComp = EM.GetComponent<PathfindingComponent>(EntityID);
    if (PathComp) { PathComp->Path.clear(); PathComp->PathIndex = 0; }

    if (Combat->CooldownRemaining <= 0.0f)
    {
        Combat->CooldownRemaining = Combat->AttackCooldown;

        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);

        int Damage = PickWeaponDamage(*Combat, TargetUnit);
        if (Damage <= 0 && Combat->AirDamage > 0)
            Damage = Combat->AirDamage;
        if (Damage <= 0) Damage = Combat->GroundDamage;
        if (Damage <= 0) return;

        int UpgradeBonus = UpgradeSystem::GetWeaponBonus(EM, EntityID);
        int TotalDamage = Damage + UpgradeBonus;

        if (Combat->WeaponBeh != WeaponBehavior::Melee ||
            !RollMissChance(*Combat, *Trans, *TargetTrans, EM))
            BeginOrResolveAttack(EM, EntityID, TargetID, *TargetTrans, TotalDamage, RangePixels);
    }
}

} // namespace Game::Systems
