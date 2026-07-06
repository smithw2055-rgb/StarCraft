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

static bool CanAttackTarget(const CombatComponent& Combat,
                            Core::ECS::EntityManager& EM,
                            int TargetID);

namespace
{
    constexpr float kAutoTargetLockSeconds = 2.5f;
    constexpr float kInvalidDirectAttackAutoAcquireDelay = 0.35f;

    float CollisionRadiusFromPath(const PathfindingComponent* Path, float Fallback)
    {
        if (!Path)
            return Fallback;
        if (!Path->HasCollisionBounds)
            return Path->CollisionRadius > 0 ? (float)Path->CollisionRadius : Fallback;

        const int Width = Path->CollisionLeft + Path->CollisionRight + 1;
        const int Height = Path->CollisionUp + Path->CollisionDown + 1;
        return (float)std::max(4, std::max(Width, Height) / 2);
    }

    float CollisionRadiusFromUnit(const UnitComponent* Unit, float Fallback)
    {
        if (!Unit)
            return Fallback;
        switch (Unit->SizeType)
        {
            case UnitSize::Small: return 12.0f;
            case UnitSize::Medium: return 16.0f;
            case UnitSize::Large: return 20.0f;
        }
        return Fallback;
    }

    float CombatCollisionRadius(Core::ECS::EntityManager& EM, int EntityID, float Fallback = 16.0f)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
            return CollisionRadiusFromPath(Path, Fallback);
        return CollisionRadiusFromUnit(EM.GetComponent<UnitComponent>(EntityID), Fallback);
    }

    float CombatEdgeDistance(Core::ECS::EntityManager& EM, int A, int B)
    {
        auto* TA = EM.GetComponent<TransformComponent>(A);
        auto* TB = EM.GetComponent<TransformComponent>(B);
        if (!TA || !TB)
            return std::numeric_limits<float>::max();

        const float CenterDistance = TA->Position.DistanceTo(TB->Position);
        const float RadiusA = CombatCollisionRadius(EM, A);
        const float RadiusB = CombatCollisionRadius(EM, B);
        return std::max(0.0f, CenterDistance - RadiusA - RadiusB);
    }

    bool IsWorkerUnit(const UnitComponent* Unit)
    {
        if (!Unit)
            return false;
        return Unit->ChkUnitID == 0x07 || Unit->ChkUnitID == 0x29 || Unit->ChkUnitID == 0x2D;
    }

    bool IsPersistentNonCombatCommand(CommandType Command)
    {
        return Command == CommandType::Harvest ||
               Command == CommandType::Build ||
               Command == CommandType::Train ||
               Command == CommandType::Upgrade ||
               Command == CommandType::UseAbility;
    }

    bool CanChaseTargetTemporarily(const CommandComponent* Cmd,
                                   const BuildingComponent* Building)
    {
        if (Building && !Building->IsLifted)
            return false;
        if (!Cmd)
            return true;
        return !IsPersistentNonCombatCommand(Cmd->CurrentCommand) &&
               Cmd->CurrentCommand != CommandType::HoldPosition;
    }

    bool IsActiveTargetValid(Core::ECS::EntityManager& EM,
                             const CombatComponent& Combat,
                             int TargetID)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
        return TargetID >= 0 &&
               TargetUnit &&
               TargetUnit->HitPoints > 0 &&
               TargetTrans &&
               CanAttackTarget(Combat, EM, TargetID);
    }

    void SetAutoTarget(CombatComponent& Combat, int TargetID, float LockSeconds)
    {
        Combat.AutoTargetEntityID = TargetID;
        Combat.AutoTargetLockSeconds = std::max(Combat.AutoTargetLockSeconds, LockSeconds);
    }

    bool AreEnemyPlayers(Core::ECS::EntityManager& EM, int A, int B)
    {
        auto* PA = EM.GetComponent<PlayerComponent>(A);
        auto* PB = EM.GetComponent<PlayerComponent>(B);
        return PA && PB && PA->PlayerID != PB->PlayerID;
    }
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

static void IssueMovePreservingCommand(Core::ECS::EntityManager& EM,
                                       int EntityID,
                                       const Core::Math::Vec2& MoveTarget,
                                       CommandType Command,
                                       const Core::Math::Vec2& CommandTarget,
                                       int TargetEntityID,
                                       int IgnoreBlockerEntityID = -1)
{
    std::vector<Order> SavedOrders;
    int SavedCurrentOrderIndex = -1;
    Core::Math::Vec2 SavedRallyPoint;
    bool SavedHasRallyPoint = false;
    if (auto* ExistingCmd = EM.GetComponent<CommandComponent>(EntityID))
    {
        SavedOrders = ExistingCmd->Orders;
        SavedCurrentOrderIndex = ExistingCmd->CurrentOrderIndex;
        SavedRallyPoint = ExistingCmd->RallyPoint;
        SavedHasRallyPoint = ExistingCmd->HasRallyPoint;
    }

    CommandSystem::IssueMove(EM, {EntityID}, MoveTarget, IgnoreBlockerEntityID);

    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd)
        Cmd = &EM.AddComponent<CommandComponent>(EntityID);
    Cmd->Orders = std::move(SavedOrders);
    Cmd->CurrentOrderIndex = SavedCurrentOrderIndex;
    Cmd->RallyPoint = SavedRallyPoint;
    Cmd->HasRallyPoint = SavedHasRallyPoint;
    Cmd->CurrentCommand = Command;
    Cmd->TargetPosition = CommandTarget;
    Cmd->TargetEntityID = TargetEntityID;
}

static bool IsAirTarget(Core::ECS::EntityManager& EM, int EntityID)
{
    if (!EM.IsAlive(EntityID))
        return false;

    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
    {
        if (Path->IsAir)
            return true;
    }

    if (auto* Combat = EM.GetComponent<CombatComponent>(EntityID))
        return Combat->MyElevation == Elevation::Air;

    return false;
}

static bool IsRangedAttack(const CombatComponent& Combat)
{
    return Combat.IsRanged || Combat.WeaponBeh != WeaponBehavior::Melee;
}

static bool IsProtectedByDarkSwarm(Core::ECS::EntityManager& EM, int TargetID)
{
    if (IsAirTarget(EM, TargetID))
        return false;

    auto* Status = EM.GetComponent<StatusEffectComponent>(TargetID);
    return Status && Status->HasEffect(StatusEffect::DarkSwarm);
}

static bool IsInvulnerableByStatus(Core::ECS::EntityManager& EM, int TargetID)
{
    if (auto* Unit = EM.GetComponent<UnitComponent>(TargetID))
    {
        if (Unit->IsInvincible)
            return true;
    }

    auto* Status = EM.GetComponent<StatusEffectComponent>(TargetID);
    return Status && Status->HasEffect(StatusEffect::Stasis);
}

static bool IsFrozenByStatus(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
    return Status && (Status->HasEffect(StatusEffect::Stasis) ||
                      Status->HasEffect(StatusEffect::Lockdown) ||
                      Status->HasEffect(StatusEffect::Maelstrom));
}

static float ActiveStatusSeconds(const StatusEffectComponent& Status,
                                 StatusEffect Effect,
                                 float DeltaTime)
{
    for (int I = 0; I < StatusEffectComponent::kMaxEffects; ++I)
    {
        if (Status.ActiveEffects[I] == Effect && Status.EffectTimers[I] > 0.0f)
            return std::min(DeltaTime, Status.EffectTimers[I]);
    }
    return 0.0f;
}

static float AttackCooldownMultiplier(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);

    int Mod = 0;
    if (Status && Status->HasEffect(StatusEffect::Stim))
        ++Mod;
    if (UpgradeSystem::GetAttackCooldownMultiplier(EM, EntityID) < 1.0f)
        ++Mod;
    if (Status && Status->HasEffect(StatusEffect::Ensnare))
        --Mod;
    if (Mod > 0)
        return 0.5f;
    if (Mod < 0)
        return 1.25f;
    return 1.0f;
}

static bool CanAttackTarget(const CombatComponent& Combat,
                            Core::ECS::EntityManager& EM,
                            int TargetID)
{
    if (!EM.IsAlive(TargetID))
        return false;
    if (EM.GetComponent<ResourceComponent>(TargetID))
        return false;
    const bool TargetAir = IsAirTarget(EM, TargetID);
    return TargetAir ? (Combat.CanTargetAir && Combat.AirDamage > 0)
                     : (Combat.CanTargetGround && Combat.GroundDamage > 0);
}

static float EffectiveAttackRangePixels(const Core::ECS::EntityManager& EM,
                                        int EntityID,
                                        const CombatComponent& Combat,
                                        int TargetID = -1)
{
    const bool TargetIsAir = TargetID >= 0 && IsAirTarget(const_cast<Core::ECS::EntityManager&>(EM), TargetID);
    return Combat.AttackRange * 32.0f +
           (float)UpgradeSystem::GetRangeBonusPixelsForTarget(EM, EntityID, TargetIsAir);
}

static void ClearCombatTargetForInvalidTarget(Core::ECS::EntityManager& EM,
                                              int AttackerID,
                                              int TargetID)
{
    auto ClearMotion = [&EM](int EntityID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }
    };

    if (auto* Combat = EM.GetComponent<CombatComponent>(AttackerID))
    {
        if (Combat->TargetEntityID == TargetID)
            Combat->TargetEntityID = -1;
        if (Combat->AutoTargetEntityID == TargetID)
            Combat->AutoTargetEntityID = -1;
        if (Combat->LastDamagerEntityID == TargetID)
            Combat->LastDamagerEntityID = -1;
        if (Combat->PendingAttackTargetEntityID == TargetID)
            Combat->PendingAttackTargetEntityID = -1;
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackDamage = 0;
        Combat->PendingAttackUpgradeBonus = 0;
        Combat->PendingAttackElapsed = 0.0f;
        Combat->AutoTargetLockSeconds = 0.0f;
    }

    if (auto* Cmd = EM.GetComponent<CommandComponent>(AttackerID))
    {
        if (Cmd->TargetEntityID == TargetID)
            Cmd->TargetEntityID = -1;
        if (Cmd->CurrentCommand == CommandType::Attack)
        {
            Cmd->CurrentCommand = CommandType::Stop;
            ClearMotion(AttackerID);
        }
    }
}

static void ClearCombatStateForInactiveAttacker(Core::ECS::EntityManager& EM, int AttackerID)
{
    if (auto* Combat = EM.GetComponent<CombatComponent>(AttackerID))
    {
        Combat->TargetEntityID = -1;
        Combat->AutoTargetEntityID = -1;
        Combat->LastDamagerEntityID = -1;
        Combat->AutoTargetLockSeconds = 0.0f;
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackTargetEntityID = -1;
        Combat->PendingAttackDamage = 0;
        Combat->PendingAttackUpgradeBonus = 0;
        Combat->PendingAttackElapsed = 0.0f;
    }

    if (auto* Cmd = EM.GetComponent<CommandComponent>(AttackerID))
    {
        Cmd->TargetEntityID = -1;
        if (Cmd->CurrentCommand == CommandType::Attack ||
            Cmd->CurrentCommand == CommandType::AttackMove)
        {
            Cmd->CurrentCommand = CommandType::Stop;
        }
    }

    if (auto* Path = EM.GetComponent<PathfindingComponent>(AttackerID))
    {
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
    }
}

static void ClearDirectAttackOrderForInvalidTarget(Core::ECS::EntityManager& EM,
                                                   const std::vector<int>& Attackers,
                                                   int TargetEntityID)
{
    (void)TargetEntityID;
    for (int ID : Attackers)
    {
        if (auto* Combat = EM.GetComponent<CombatComponent>(ID))
        {
            Combat->TargetEntityID = -1;
            Combat->AutoTargetEntityID = -1;
            Combat->LastDamagerEntityID = -1;
            Combat->AutoTargetLockSeconds = 0.0f;
            Combat->PendingAttackFrame = false;
            Combat->PendingAttackTargetEntityID = -1;
            Combat->PendingAttackDamage = 0;
            Combat->PendingAttackUpgradeBonus = 0;
            Combat->PendingAttackElapsed = 0.0f;
            Combat->AutoTargetLockSeconds = kInvalidDirectAttackAutoAcquireDelay;
        }

        if (auto* Cmd = EM.GetComponent<CommandComponent>(ID))
        {
            Cmd->CurrentCommand = CommandType::Stop;
            Cmd->TargetEntityID = -1;
        }

        if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }
    }
}

void CombatSystem::IssueAttack(Core::ECS::EntityManager& EM,
                                const std::vector<int>& Attackers,
                                int TargetEntityID)
{
    auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetEntityID);
    auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
    if (!EM.IsAlive(TargetEntityID) ||
        !TargetTrans ||
        !TargetUnit ||
        TargetUnit->HitPoints <= 0 ||
        EM.GetComponent<ResourceComponent>(TargetEntityID))
    {
        ClearDirectAttackOrderForInvalidTarget(EM, Attackers, TargetEntityID);
        return;
    }

    for (int ID : Attackers)
    {
        auto* Combat = EM.GetComponent<CombatComponent>(ID);
        if (!Combat) continue;
        auto* AttackerUnit = EM.GetComponent<UnitComponent>(ID);
        if (!AttackerUnit || AttackerUnit->HitPoints <= 0 || IsFrozenByStatus(EM, ID))
        {
            ClearCombatStateForInactiveAttacker(EM, ID);
            continue;
        }
        if (!CanAttackTarget(*Combat, EM, TargetEntityID))
        {
            ClearCombatTargetForInvalidTarget(EM, ID, TargetEntityID);
            continue;
        }

        Combat->TargetEntityID = TargetEntityID;

        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::Attack;
        Cmd->TargetEntityID = TargetEntityID;

        IssueMovePreservingCommand(EM, ID, TargetTrans->Position,
                                   CommandType::Attack, TargetTrans->Position, TargetEntityID,
                                   TargetEntityID);
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
        if (EM.GetComponent<ResourceComponent>(ID)) continue;
        auto* MyCombat = EM.GetComponent<CombatComponent>(EntityID);
        if (MyCombat && !CanAttackTarget(*MyCombat, EM, ID)) continue;

        // Skip cloaked units we can't detect
        auto* Cloak = EM.GetComponent<CloakComponent>(ID);
        if (Cloak && (Cloak->IsCloaked || Cloak->IsBurrowed))
        {
            auto* MyUnit = EM.GetComponent<UnitComponent>(EntityID);
            bool IsDetector = MyUnit && MyUnit->IsDetector;
            if (!IsDetector) continue;
        }

        float Dist = CombatEdgeDistance(EM, EntityID, ID);
        if (Dist < BestDist)
        {
            BestDist = Dist;
            BestID = ID;
        }
    }
    return BestID;
}

int CombatSystem::PickWeaponDamage(const CombatComponent& Combat,
                                    const UnitComponent* TargetUnit,
                                    bool TargetIsAir) const
{
    if (!TargetUnit) return 0;

    if (TargetIsAir)
        return Combat.CanTargetAir ? Combat.AirDamage : 0;
    if (!Combat.CanTargetGround)
        return 0;

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
                                   float RangePixels,
                                   Core::ECS::EntityManager& EM) const
{
    (void)EM;
    // Cliff elevation miss: if attacker is on lower cliff than target, chance to miss
    // Higher ground advantage: 30% miss chance for lower attacker
    // Melee weapons always hit unless target is on significantly higher cliff
    if (Combat.WeaponBeh == WeaponBehavior::Melee)
        return false; // melee doesn't miss

    // For simplicity, check position distance-based miss: if target is at range limit, slight chance
    float Dist = AttackerTrans.Position.DistanceTo(TargetTrans.Position);
    float MaxRng = RangePixels;
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
    if (!EM.IsAlive(TargetID) || !TargetUnit || TargetUnit->HitPoints <= 0) return;
    if (BaseDamage <= 0)
        return;
    if (EM.GetComponent<ResourceComponent>(TargetID))
        return;
    if (IsInvulnerableByStatus(EM, TargetID))
        return;
    if (IsRangedAttack(Attacker) && IsProtectedByDarkSwarm(EM, TargetID))
        return;

    const bool TargetIsAir = IsAirTarget(EM, TargetID);
    const DamageType DamageKind = TargetIsAir ? Attacker.AirDmgType : Attacker.GroundDmgType;
    int RemainingDamage = std::max(0, BaseDamage);

    if (TargetUnit->ShieldPoints > 0)
    {
        int ShieldArmor = 0;
        if (auto* TargetPlayer = EM.GetComponent<PlayerComponent>(TargetID))
        {
            ShieldArmor = UpgradeSystem::GetResearchLevel(
                TargetPlayer->PlayerID, ResearchId::Protoss_PlasmaShields);
        }

        int ShieldDamage = std::max(1, RemainingDamage - ShieldArmor);
        const int Absorbed = std::min(TargetUnit->ShieldPoints, ShieldDamage);
        TargetUnit->ShieldPoints -= Absorbed;
        RemainingDamage = ShieldDamage - Absorbed;
    }

    int EffectiveDamage = 0;
    if (RemainingDamage > 0)
    {
        const int UpgradedArmor = TargetUnit->Armor + UpgradeSystem::GetArmorBonus(EM, TargetID);
        int ArmorAdjusted = std::max(1, RemainingDamage - UpgradedArmor);
        const float SizeMod = GetSizeDamageModifier(DamageKind, TargetUnit->SizeType);
        EffectiveDamage = std::max(1, (int)((float)ArmorAdjusted * SizeMod));
    }

    if (EffectiveDamage > 0)
    {
        TargetUnit->HitPoints -= EffectiveDamage;
    }

    if (TargetUnit->HitPoints > 0)
    {
        if (auto* TargetCombat = EM.GetComponent<CombatComponent>(TargetID))
        {
            TargetCombat->LastDamagerEntityID = AttackerID;
            auto* TargetBuilding = EM.GetComponent<BuildingComponent>(TargetID);
            if (AreEnemyPlayers(EM, TargetID, AttackerID) &&
                !IsWorkerUnit(TargetUnit) &&
                CanAttackTarget(*TargetCombat, EM, AttackerID))
            {
                SetAutoTarget(*TargetCombat, AttackerID, kAutoTargetLockSeconds);
            }
            else if (TargetBuilding &&
                     AreEnemyPlayers(EM, TargetID, AttackerID) &&
                     CanAttackTarget(*TargetCombat, EM, AttackerID))
            {
                SetAutoTarget(*TargetCombat, AttackerID, kAutoTargetLockSeconds);
            }
        }
    }
    if (TargetUnit->HitPoints <= 0)
    {
        TargetUnit->HitPoints = 0;
        RecordUnitDeath(EM, TargetID, AttackerID, true);
        EM.DestroyEntity(TargetID);
        ClearCombatTargetForInvalidTarget(EM, AttackerID, TargetID);
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
    auto* AttackerUnit = EM.GetComponent<UnitComponent>(AttackerID);
    if (!Combat || !Trans || !Combat->PendingAttackFrame)
        return false;
    if (!AttackerUnit || AttackerUnit->HitPoints <= 0)
    {
        ClearCombatStateForInactiveAttacker(EM, AttackerID);
        return false;
    }

    const int TargetID = Combat->PendingAttackTargetEntityID;
    Core::Math::Vec2 TargetPos = Combat->PendingAttackTargetPos;
    if (TargetID >= 0)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
        if (!EM.IsAlive(TargetID) || !TargetUnit || TargetUnit->HitPoints <= 0 || !TargetTrans)
        {
            ClearCombatTargetForInvalidTarget(EM, AttackerID, TargetID);
            return false;
        }
        if (EM.GetComponent<ResourceComponent>(TargetID))
        {
            ClearCombatTargetForInvalidTarget(EM, AttackerID, TargetID);
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

        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (!Unit || Unit->HitPoints <= 0)
        {
            Status->Tick(DeltaTime);
            continue;
        }

        // Plague: drain HP to minimum 1
        float PlagueSeconds = ActiveStatusSeconds(*Status, StatusEffect::Plague, DeltaTime);
        if (PlagueSeconds > 0.0f)
        {
            const float DamagePerSecond = (float)std::max(1, Unit->MaxHitPoints / 40);
            Unit->PlagueDamageAccumulator += DamagePerSecond * PlagueSeconds;
            int Drain = (int)Unit->PlagueDamageAccumulator;
            if (Drain > 0)
            {
                Unit->PlagueDamageAccumulator -= (float)Drain;
                Unit->HitPoints = std::max(1, Unit->HitPoints - Drain);
            }
        }
        else
        {
            Unit->PlagueDamageAccumulator = 0.0f;
        }

        // Irradiate: AoE damage (handled per tick in BulletSystem Persist style)
        // For now, direct self-damage each second
        float IrradiateSeconds = ActiveStatusSeconds(*Status, StatusEffect::Irradiate, DeltaTime);
        if (IrradiateSeconds > 0.0f)
        {
            const float DamagePerSecond = (float)std::max(1, Unit->MaxHitPoints / 25);
            Unit->IrradiateDamageAccumulator += DamagePerSecond * IrradiateSeconds;
            int IrradDmg = (int)Unit->IrradiateDamageAccumulator;
            if (IrradDmg > 0)
            {
                Unit->IrradiateDamageAccumulator -= (float)IrradDmg;
                Unit->HitPoints = std::max(1, Unit->HitPoints - IrradDmg);
            }
        }
        else
        {
            Unit->IrradiateDamageAccumulator = 0.0f;
        }

        // EMP: zero shields (handled on application), energy drain
        if (Status->HasEffect(StatusEffect::EMP))
        {
            Unit->ShieldPoints = 0;
            Unit->EnergyPoints = 0;
            if (auto* Cloak = EM.GetComponent<CloakComponent>(ID);
                Cloak && Cloak->IsCloaked && !Cloak->IsPassiveCloak && !Cloak->IsBurrowed)
            {
                Cloak->IsCloaked = false;
            }
        }

        // Stasis: freeze unit (handled by disabling combat/movement in respective systems)
        Status->Tick(DeltaTime);
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
        if (Unit && Unit->MaxShieldPoints > 0 && Unit->ShieldPoints < Unit->MaxShieldPoints)
        {
            Unit->ShieldRegenAccumulator += DeltaTime;
            const int Regen = (int)(Unit->ShieldRegenAccumulator + 0.0001f);
            if (Regen > 0)
            {
                Unit->ShieldRegenAccumulator -= (float)Regen;
                if (Unit->ShieldRegenAccumulator < 0.0f)
                    Unit->ShieldRegenAccumulator = 0.0f;
                Unit->ShieldPoints = std::min(Unit->MaxShieldPoints,
                    Unit->ShieldPoints + Regen);
            }
        }
        else if (Unit && Unit->ShieldPoints >= Unit->MaxShieldPoints)
        {
            Unit->ShieldRegenAccumulator = 0.0f;
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
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);

    if (!Combat || !Trans) return;
    if (!Unit || Unit->HitPoints <= 0)
    {
        ClearCombatStateForInactiveAttacker(EM, EntityID);
        return;
    }

    // Check if unit is frozen by stasis/lockdown/maelstrom
    if (IsFrozenByStatus(EM, EntityID))
        return;

    // Reduce cooldown
    if (Combat->CooldownRemaining > 0.0f)
        Combat->CooldownRemaining -= DeltaTime;
    Combat->AutoTargetLockSeconds = std::max(0.0f, Combat->AutoTargetLockSeconds - DeltaTime);

    if (Combat->PendingAttackFrame)
    {
        Combat->PendingAttackElapsed += DeltaTime;
        if (Combat->PendingAttackElapsed >= 0.8f)
            ResolvePendingAttackFrame(EM, mBulletSystem, EntityID);
        return;
    }

    const float BaseRangePixels = EffectiveAttackRangePixels(EM, EntityID, *Combat);
    const bool HoldPosition = Cmd && Cmd->CurrentCommand == CommandType::HoldPosition;
    const bool ExplicitAttackOrder =
        Cmd && (Cmd->CurrentCommand == CommandType::Attack ||
                Cmd->CurrentCommand == CommandType::AttackMove);

    int TargetID = ExplicitAttackOrder ? Cmd->TargetEntityID : -1;

    // Validate target
    if (TargetID >= 0)
    {
        if (!IsActiveTargetValid(EM, *Combat, TargetID))
        {
            ClearCombatTargetForInvalidTarget(EM, EntityID, TargetID);
            TargetID = -1;
        }
    }

    if (Combat->AutoTargetEntityID >= 0 &&
        !IsActiveTargetValid(EM, *Combat, Combat->AutoTargetEntityID))
    {
        Combat->AutoTargetEntityID = -1;
        Combat->AutoTargetLockSeconds = 0.0f;
    }

    if (TargetID < 0 && Combat->AutoTargetEntityID >= 0)
    {
        TargetID = Combat->AutoTargetEntityID;
    }

    // Find new target for attack-move
    if (TargetID < 0 && Cmd && Cmd->CurrentCommand == CommandType::AttackMove)
    {
        const float SearchRangePixels = BaseRangePixels * 2.0f;
        TargetID = FindNearestEnemy(EM, EntityID, SearchRangePixels);
        if (TargetID >= 0)
        {
            const Core::Math::Vec2 AttackMoveDestination = Cmd->TargetPosition;
            Combat->TargetEntityID = TargetID;
            Cmd->TargetEntityID = TargetID;
            if (auto* TTrans = EM.GetComponent<TransformComponent>(TargetID))
                IssueMovePreservingCommand(EM, EntityID, TTrans->Position,
                                           CommandType::AttackMove, AttackMoveDestination, TargetID,
                                           TargetID);
        }
    }

    if (TargetID < 0)
    {
        const bool PassiveAutoAcquire =
            (HoldPosition && !IsWorkerUnit(Unit)) ||
            (!Cmd && !IsWorkerUnit(Unit)) ||
            (EM.GetComponent<BuildingComponent>(EntityID) != nullptr) ||
            ((Cmd->CurrentCommand == CommandType::Stop ||
              Cmd->CurrentCommand == CommandType::Follow ||
              Cmd->CurrentCommand == CommandType::Patrol) &&
             !IsWorkerUnit(Unit));
        if (PassiveAutoAcquire && Combat->AutoTargetLockSeconds <= 0.0f)
        {
            TargetID = FindNearestEnemy(EM, EntityID, BaseRangePixels);
            if (TargetID >= 0)
                SetAutoTarget(*Combat, TargetID, 0.35f);
        }
    }

    if (TargetID < 0)
    {
        Combat->TargetEntityID = -1;
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
    Combat->TargetEntityID = TargetID;

    auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
    if (!TargetTrans) return;

    // Check range
    const float RangePixels = EffectiveAttackRangePixels(EM, EntityID, *Combat, TargetID);
    float Dist = CombatEdgeDistance(EM, EntityID, TargetID);

    if (Dist > RangePixels)
    {
        if (HoldPosition || !CanChaseTargetTemporarily(Cmd, EM.GetComponent<BuildingComponent>(EntityID)))
            return;

        const CommandType PreserveCommand = Cmd ? Cmd->CurrentCommand : CommandType::Attack;
        const Core::Math::Vec2 PreserveTarget = Cmd ? Cmd->TargetPosition : TargetTrans->Position;
        const int PreserveTargetEntity = Cmd ? Cmd->TargetEntityID : TargetID;
        IssueMovePreservingCommand(EM, EntityID, TargetTrans->Position,
                                   PreserveCommand, PreserveTarget, PreserveTargetEntity, TargetID);
        return;
    }

    // In range - stop and fire
    auto* PathComp = EM.GetComponent<PathfindingComponent>(EntityID);
    if (PathComp) { PathComp->Path.clear(); PathComp->PathIndex = 0; }

    if (Combat->CooldownRemaining <= 0.0f)
    {
        Combat->CooldownRemaining =
            Combat->AttackCooldown * AttackCooldownMultiplier(EM, EntityID);

        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);

        const bool TargetIsAir = IsAirTarget(EM, TargetID);
        int Damage = PickWeaponDamage(*Combat, TargetUnit, TargetIsAir);
        if (Damage <= 0) return;

        int UpgradeBonus = UpgradeSystem::GetWeaponBonus(EM, EntityID);
        int TotalDamage = Damage + UpgradeBonus;

        if (Combat->WeaponBeh != WeaponBehavior::Melee ||
            !RollMissChance(*Combat, *Trans, *TargetTrans, RangePixels, EM))
            BeginOrResolveAttack(EM, EntityID, TargetID, *TargetTrans, TotalDamage, RangePixels);
    }
}

} // namespace Game::Systems
