#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CombatSystem.h"

#include <algorithm>
#include <cmath>

namespace Game::Systems
{

using namespace Game::Components;

BulletSystem::VisualResolver BulletSystem::sVisualResolver;

void BulletSystem::SetVisualResolver(VisualResolver Resolver)
{
    sVisualResolver = std::move(Resolver);
}

namespace
{
    static float SplashFalloff(float Distance, float MaxRadius)
    {
        if (MaxRadius <= 0.0f) return 1.0f;
        float Ratio = Distance / MaxRadius;
        if (Ratio > 1.0f) return 0.0f;
        if (Ratio <= (1.0f / 3.0f)) return 1.0f;
        if (Ratio <= (2.0f / 3.0f)) return 0.5f;
        return 0.25f;
    }

    static bool IsAirTarget(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
            if (Path->IsAir)
                return true;
        if (auto* Combat = EM.GetComponent<CombatComponent>(EntityID))
            return Combat->MyElevation == Elevation::Air;
        return false;
    }

    static DamageType WeaponDamageKindForTarget(Core::ECS::EntityManager& EM,
                                                const CombatComponent& SourceCombat,
                                                int TargetEntityID)
    {
        if (TargetEntityID >= 0 && IsAirTarget(EM, TargetEntityID))
            return SourceCombat.AirDmgType;
        return SourceCombat.GroundDmgType;
    }

    static bool IsRangedAttack(const CombatComponent& Combat)
    {
        return Combat.IsRanged || Combat.WeaponBeh != WeaponBehavior::Melee;
    }

    static bool IsProtectedByDarkSwarm(Core::ECS::EntityManager& EM, int TargetEntityID)
    {
        if (IsAirTarget(EM, TargetEntityID))
            return false;

        auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
        return Status && Status->HasEffect(StatusEffect::DarkSwarm);
    }

    static bool IsInvulnerableByStatus(Core::ECS::EntityManager& EM, int TargetEntityID)
    {
        if (auto* Unit = EM.GetComponent<UnitComponent>(TargetEntityID))
        {
            if (Unit->IsInvincible)
                return true;
        }

        auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
        return Status && Status->HasEffect(StatusEffect::Stasis);
    }

    static CombatComponent BulletDamageCombat(DamageType DamageKind, bool IsRangedAttack)
    {
        CombatComponent Combat;
        Combat.GroundDmgType = DamageKind;
        Combat.AirDmgType = DamageKind;
        Combat.WeaponBeh = IsRangedAttack ? WeaponBehavior::FlyDirect : WeaponBehavior::Melee;
        Combat.IsRanged = IsRangedAttack;
        Combat.CanTargetGround = true;
        Combat.CanTargetAir = true;
        return Combat;
    }
}

int BulletSystem::SpawnBullet(Core::ECS::EntityManager& EM,
                               const CombatComponent& SourceCombat,
                               const TransformComponent& SourceTrans,
                               const PlayerComponent& SourcePlayer,
                               int SourceEntityID,
                               int BaseDamage,
                               int DamageUpgradeBonus,
                               float RangePixels,
                               int TargetEntityID,
                               const Core::Math::Vec2& TargetPosIn)
{
    (void)RangePixels;

    if (BaseDamage <= 0) return -1;

    int BulletID = EM.CreateEntity();
    auto& BulletTrans = EM.AddComponent<TransformComponent>(BulletID);
    auto& BulletComp = EM.AddComponent<BulletComponent>(BulletID);
    auto& BulletPlayer = EM.AddComponent<PlayerComponent>(BulletID);

    BulletTrans.Position = SourceTrans.Position;
    BulletPlayer = SourcePlayer;
    BulletComp.SourceEntityID = SourceEntityID;
    BulletComp.Behavior = SourceCombat.WeaponBeh;
    BulletComp.Splash = SourceCombat.Splash;
    BulletComp.SplashRadius = SourceCombat.SplashRadius;
    BulletComp.BaseDamage = BaseDamage;
    BulletComp.DamageKind = WeaponDamageKindForTarget(EM, SourceCombat, TargetEntityID);
    BulletComp.DamageUpgradeBonus = DamageUpgradeBonus;
    BulletComp.IsRangedAttack = IsRangedAttack(SourceCombat);
    BulletComp.TargetEntityID = TargetEntityID;
    BulletComp.TargetPos = TargetPosIn;

    Core::Math::Vec2 TargetPos = TargetPosIn;
    if (TargetEntityID >= 0)
    {
        auto* TTarget = EM.GetComponent<TransformComponent>(TargetEntityID);
        if (TTarget) TargetPos = TTarget->Position;
    }

    Core::Math::Vec2 Dir = TargetPos - SourceTrans.Position;
    float Dist = Dir.Length();

    std::optional<VisualSpec> Visual;
    if (sVisualResolver)
    {
        if (const auto* SourceUnit = EM.GetComponent<UnitComponent>(SourceEntityID))
            Visual = sVisualResolver(SourceUnit->ChkUnitID, IsAirTarget(EM, TargetEntityID));
    }
    if (Visual && Visual->Speed > 0.0f)
        BulletComp.Speed = Visual->Speed;

    switch (BulletComp.Behavior)
    {
        case WeaponBehavior::Melee:
        {
            BulletComp.LifetimeRemaining = 0.0f;
            break;
        }
        case WeaponBehavior::AppearOnTarget:
        {
            BulletComp.LifetimeRemaining = 0.05f;
            break;
        }
        case WeaponBehavior::FlyDirect:
        case WeaponBehavior::Missile:
        {
            float BulletSpeed = (Dist > 0.0f) ? std::max(BulletComp.Speed, Dist / 0.5f) : BulletComp.Speed;
            BulletComp.Speed = BulletSpeed;
            if (Dist > 0.0f)
                BulletComp.Velocity = Dir.Normalized() * BulletSpeed;
            else
                BulletComp.Velocity = {BulletSpeed, 0.0f};
            BulletComp.MaxLifetime = (Dist > 0.0f) ? (Dist / BulletSpeed) + 0.2f : 0.5f;
            BulletComp.LifetimeRemaining = BulletComp.MaxLifetime;
            break;
        }
        case WeaponBehavior::Lob:
        {
            float LobSpeed = std::max(300.0f, Dist / 0.8f);
            BulletComp.Speed = LobSpeed;
            if (Dist > 0.0f)
                BulletComp.Velocity = Dir.Normalized() * LobSpeed;
            else
                BulletComp.Velocity = {LobSpeed, 0.0f};
            BulletComp.MaxLifetime = (Dist > 0.0f) ? (Dist / LobSpeed) : 0.5f;
            BulletComp.LifetimeRemaining = BulletComp.MaxLifetime;
            break;
        }
        case WeaponBehavior::Persist:
        {
            BulletComp.MaxLifetime = 2.0f;
            BulletComp.LifetimeRemaining = 2.0f;
            BulletComp.Velocity = {0.0f, 0.0f};
            break;
        }
    }

    if (Visual && Visual->ImageID != UINT32_MAX &&
        BulletComp.Behavior != WeaponBehavior::Melee)
    {
        auto& Render = EM.AddComponent<RenderComponent>(BulletID);
        Render.ImageID = Visual->ImageID;
        Render.TotalFrames = 1;
        Render.Layer = RenderLayer::Effects;

        auto& Direction = EM.AddComponent<PathfindingComponent>(BulletID);
        Direction.CurrentVelocity = BulletComp.Velocity;
        Direction.FacingAngle = std::atan2(BulletComp.Velocity.Y, BulletComp.Velocity.X);
        Direction.IsAir = true;
    }

    return BulletID;
}

void BulletSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Bullets = EM.GetEntitiesWith<TransformComponent, BulletComponent>();
    for (int ID : Bullets)
        UpdateBullet(EM, ID, DeltaTime);
}

void BulletSystem::UpdateBullet(Core::ECS::EntityManager& EM, int BulletID, float DeltaTime)
{
    auto* Bullet = EM.GetComponent<BulletComponent>(BulletID);
    auto* Trans = EM.GetComponent<TransformComponent>(BulletID);
    if (!Bullet || !Trans) return;

    Bullet->LifetimeRemaining -= DeltaTime;
    if (auto* Direction = EM.GetComponent<PathfindingComponent>(BulletID))
    {
        Direction->CurrentVelocity = Bullet->Velocity;
        if (Bullet->Velocity.Length() > 0.001f)
            Direction->FacingAngle = std::atan2(Bullet->Velocity.Y, Bullet->Velocity.X);
    }

    switch (Bullet->Behavior)
    {
        case WeaponBehavior::AppearOnTarget:
        {
            if (Bullet->LifetimeRemaining <= Bullet->MaxLifetime - 0.01f)
                BulletHit(EM, BulletID);
            return;
        }
        case WeaponBehavior::Persist:
        {
            Core::Math::Vec2 Center = Trans->Position;
            int SourcePlayerID = -1;
            if (auto* P = EM.GetComponent<PlayerComponent>(BulletID))
                SourcePlayerID = P->PlayerID;

            ApplySplashDamage(EM, Center, Bullet->SplashRadius * 32.0f,
                              Bullet->BaseDamage, Bullet->DamageKind,
                              Bullet->DamageUpgradeBonus, SourcePlayerID,
                              Bullet->SourceEntityID, Bullet->Splash,
                              Elevation::Both, Bullet->IsRangedAttack);
            if (Bullet->LifetimeRemaining <= 0.0f)
                EM.DestroyEntity(BulletID);
            return;
        }
        case WeaponBehavior::FlyDirect:
        case WeaponBehavior::Missile:
        case WeaponBehavior::Lob:
        {
            Core::Math::Vec2 TargetPos = Bullet->TargetPos;
            if (Bullet->Behavior == WeaponBehavior::Missile && Bullet->TargetEntityID >= 0)
            {
                auto* TTarget = EM.GetComponent<TransformComponent>(Bullet->TargetEntityID);
                if (TTarget)
                {
                    TargetPos = TTarget->Position;
                    Bullet->TargetPos = TargetPos;
                }
            }

            Core::Math::Vec2 ToTarget = TargetPos - Trans->Position;
            float Dist = ToTarget.Length();

            if (Dist < 16.0f || Bullet->LifetimeRemaining <= 0.0f)
            {
                BulletHit(EM, BulletID);
                return;
            }

            Core::Math::Vec2 Dir = ToTarget.Normalized();
            Trans->Position = Trans->Position + Dir * Bullet->Speed * DeltaTime;
            break;
        }
        default: break;
    }
}

void BulletSystem::BulletHit(Core::ECS::EntityManager& EM, int BulletID)
{
    auto* Bullet = EM.GetComponent<BulletComponent>(BulletID);
    auto* Trans = EM.GetComponent<TransformComponent>(BulletID);
    auto* BPlayer = EM.GetComponent<PlayerComponent>(BulletID);
    if (!Bullet || !Trans || Bullet->HasHit) return;

    Bullet->HasHit = true;

    int SourcePlayerID = BPlayer ? BPlayer->PlayerID : -1;

    Core::Math::Vec2 ImpactPos = Trans->Position;
    int DirectHitID = Bullet->TargetEntityID;

    if (DirectHitID >= 0)
    {
        auto* TTarget = EM.GetComponent<TransformComponent>(DirectHitID);
        if (TTarget) ImpactPos = TTarget->Position;
    }

    const bool HasSplash = Bullet->Splash != SplashType::None && Bullet->SplashRadius > 0.0f;

    if (DirectHitID >= 0 && !HasSplash)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(DirectHitID);
        if (TargetUnit && TargetUnit->HitPoints > 0 &&
            !EM.GetComponent<ResourceComponent>(DirectHitID))
        {
            if (!IsInvulnerableByStatus(EM, DirectHitID) &&
                (!Bullet->IsRangedAttack || !IsProtectedByDarkSwarm(EM, DirectHitID)))
            {
                const CombatComponent BulletCombat =
                    BulletDamageCombat(Bullet->DamageKind, Bullet->IsRangedAttack);
                CombatSystem::ApplyDamage(
                    EM, DirectHitID, BulletCombat, Bullet->SourceEntityID,
                    Bullet->BaseDamage + Bullet->DamageUpgradeBonus);
            }
        }
    }

    if (HasSplash)
    {
        float Radius = Bullet->SplashRadius * 32.0f;
        ApplySplashDamage(EM, ImpactPos, Radius,
                          Bullet->BaseDamage, Bullet->DamageKind,
                          Bullet->DamageUpgradeBonus, SourcePlayerID,
                          Bullet->SourceEntityID, Bullet->Splash,
                          Elevation::Both, Bullet->IsRangedAttack);
    }

    EM.DestroyEntity(BulletID);
}

void BulletSystem::ApplySplashDamage(Core::ECS::EntityManager& EM,
                                      const Core::Math::Vec2& Center,
                                      float Radius,
                                      int BaseDamage,
                                      DamageType DamageKind,
                                      int DamageUpgradeBonus,
                                      int SourcePlayerID,
                                      int SourceEntityID,
                                      SplashType Splash,
                                      Elevation TargetElev,
                                      bool IsRangedAttack)
{
    (void)Splash;
    (void)TargetElev;

    auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
    for (int UID : Units)
    {
        auto* UP = EM.GetComponent<PlayerComponent>(UID);
        auto* UT = EM.GetComponent<TransformComponent>(UID);
        auto* UU = EM.GetComponent<UnitComponent>(UID);
        if (!UP || !UT || !UU || UU->HitPoints <= 0) continue;
        if (EM.GetComponent<ResourceComponent>(UID)) continue;
        if (UP->PlayerID == SourcePlayerID) continue;
        if (IsInvulnerableByStatus(EM, UID)) continue;
        if (IsRangedAttack && IsProtectedByDarkSwarm(EM, UID)) continue;

        float Dist = UT->Position.DistanceTo(Center);
        if (Dist > Radius) continue;

        float Falloff = SplashFalloff(Dist, Radius);
        if (Falloff <= 0.0f) continue;

        int Damage = std::max(1, (int)((float)(BaseDamage + DamageUpgradeBonus) * Falloff));
        const CombatComponent SplashCombat = BulletDamageCombat(DamageKind, IsRangedAttack);
        CombatSystem::ApplyDamage(EM, UID, SplashCombat, SourceEntityID, Damage);
    }
}

} // namespace Game::Systems
