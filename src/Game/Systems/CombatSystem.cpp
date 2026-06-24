#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Components.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <limits>

namespace Game::Systems
{

using namespace Game::Components;

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

        CommandSystem::IssueMove(EM, {ID}, TargetTrans->Position);
    }
}

void CombatSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                    const std::vector<int>& Units,
                                    Core::Math::Vec2 Target)
{
    for (int ID : Units)
    {
        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::AttackMove;

        CommandSystem::IssueMove(EM, {ID}, Target);
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

        float Dist = MyTrans->Position.DistanceTo(TargetTrans->Position);
        if (Dist < BestDist)
        {
            BestDist = Dist;
            BestID = ID;
        }
    }
    return BestID;
}

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

void CombatSystem::ApplyDamage(Core::ECS::EntityManager& EM, int TargetID,
                                const CombatComponent& Attacker, int AttackerID)
{
    auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
    if (!TargetUnit || TargetUnit->HitPoints <= 0) return;

    // Determine damage type and base damage
    DamageType DmgType = Attacker.GroundDmgType;
    int Damage = Attacker.GroundDamage;
    if (Damage <= 0) { Damage = Attacker.AirDamage; DmgType = Attacker.AirDmgType; }
    if (Damage <= 0) return;

    // Apply upgrade bonuses
    int UpgradedDamage = Damage + UpgradeSystem::GetWeaponBonus(EM, AttackerID);
    int UpgradedArmor = TargetUnit->Armor + UpgradeSystem::GetArmorBonus(EM, TargetID);

    // Size-based damage modifier
    float SizeMod = GetSizeDamageModifier(DmgType, TargetUnit->SizeType);
    int ModifiedDamage = std::max(1, (int)((float)UpgradedDamage * SizeMod));

    // Armor reduces damage
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
        EM.DestroyEntity(TargetID);
        Core::Utils::Logger::Info("CombatSystem",
            "Entity " + std::to_string(TargetID) + " destroyed by " +
            std::to_string(AttackerID));
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
            // Regenerate shield: 1 point per second
            float Regen = 1.0f * DeltaTime;
            Unit->ShieldPoints = std::min(Unit->MaxShieldPoints,
                Unit->ShieldPoints + (int)(Regen));
        }
    }

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

    // Reduce cooldown
    if (Combat->CooldownRemaining > 0.0f)
        Combat->CooldownRemaining -= DeltaTime;

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
                    ApplyDamage(EM, Nearby, *Combat, EntityID);
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
        }
    }

    // Find new target for attack-move
    if (TargetID < 0 && Cmd && Cmd->CurrentCommand == CommandType::AttackMove)
    {
        TargetID = FindNearestEnemy(EM, EntityID, Combat->AttackRange * 32.0f * 2.0f);
        if (TargetID >= 0)
        {
            Combat->TargetEntityID = TargetID;
            Cmd->TargetEntityID = TargetID;
            if (auto* TTrans = EM.GetComponent<TransformComponent>(TargetID))
                CommandSystem::IssueMove(EM, {EntityID}, TTrans->Position);
        }
    }

    if (TargetID < 0) return;

    auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
    if (!TargetTrans) return;

    // Check range
    float Dist = Trans->Position.DistanceTo(TargetTrans->Position);
    float RangePixels = Combat->AttackRange * 32.0f;

    if (Dist > RangePixels)
    {
        // Move closer
        CommandSystem::IssueMove(EM, {EntityID}, TargetTrans->Position);
        return;
    }

    // In range - stop and fire
    auto* PathComp = EM.GetComponent<PathfindingComponent>(EntityID);
    if (PathComp) { PathComp->Path.clear(); PathComp->PathIndex = 0; }

    if (Combat->CooldownRemaining <= 0.0f)
    {
        Combat->CooldownRemaining = Combat->AttackCooldown;
        ApplyDamage(EM, TargetID, *Combat, EntityID);
    }
}

} // namespace Game::Systems
