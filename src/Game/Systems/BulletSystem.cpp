#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cmath>

namespace Game::Systems
{

using namespace Game::Components;
using Core::Utils::Logger;

namespace
{
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

    static float SplashFalloff(float Distance, float MaxRadius)
    {
        if (MaxRadius <= 0.0f) return 1.0f;
        float Ratio = Distance / MaxRadius;
        if (Ratio >= 1.0f) return 0.0f;
        if (Ratio <= 0.0f) return 1.0f;
        return 1.0f - Ratio * 0.5f;
    }

    static bool AreEnemies(int PlayerA, int PlayerB)
    {
        return PlayerA != PlayerB;
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
    BulletComp.DamageKind = SourceCombat.GroundDmgType;
    BulletComp.DamageUpgradeBonus = DamageUpgradeBonus;
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

            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int UID : Units)
            {
                auto* UP = EM.GetComponent<PlayerComponent>(UID);
                auto* UT = EM.GetComponent<TransformComponent>(UID);
                auto* UU = EM.GetComponent<UnitComponent>(UID);
                if (!UP || !UT || !UU || UU->HitPoints <= 0) continue;
                if (UP->PlayerID == SourcePlayerID) continue;

                float D = UT->Position.DistanceTo(Center);
                if (D <= Bullet->SplashRadius * 32.0f || D <= 96.0f)
                {
                    ApplySplashDamage(EM, Center, Bullet->SplashRadius * 32.0f,
                                      Bullet->BaseDamage, Bullet->DamageKind,
                                      Bullet->DamageUpgradeBonus, SourcePlayerID,
                                      Bullet->SourceEntityID, Bullet->Splash,
                                      Elevation::Both);
                }
            }
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

    if (DirectHitID >= 0)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(DirectHitID);
        if (TargetUnit && TargetUnit->HitPoints > 0)
        {
            float SizeMod = GetSizeDamageModifier(Bullet->DamageKind, TargetUnit->SizeType);
            int Damage = std::max(1, (int)((float)(Bullet->BaseDamage + Bullet->DamageUpgradeBonus) * SizeMod));

            if (TargetUnit->ShieldPoints > 0)
            {
                if (Damage <= TargetUnit->ShieldPoints)
                {
                    TargetUnit->ShieldPoints -= Damage;
                    Damage = 0;
                }
                else
                {
                    Damage -= TargetUnit->ShieldPoints;
                    TargetUnit->ShieldPoints = 0;
                }
            }

            if (Damage > 0)
            {
                int FinalArmor = TargetUnit->Armor + UpgradeSystem::GetArmorBonus(EM, DirectHitID);
                Damage = std::max(1, Damage - FinalArmor);
                TargetUnit->HitPoints -= Damage;
            }

            if (TargetUnit->HitPoints <= 0)
            {
                TargetUnit->HitPoints = 0;
                RecordUnitDeath(EM, DirectHitID, Bullet->SourceEntityID, true);
                EM.DestroyEntity(DirectHitID);
                Logger::Info("BulletSystem",
                    "Entity " + std::to_string(DirectHitID) + " destroyed by bullet " +
                    std::to_string(BulletID));
            }
        }
    }

    if (Bullet->Splash != SplashType::None && Bullet->SplashRadius > 0.0f)
    {
        float Radius = Bullet->SplashRadius * 32.0f;
        ApplySplashDamage(EM, ImpactPos, Radius,
                          Bullet->BaseDamage, Bullet->DamageKind,
                          Bullet->DamageUpgradeBonus, SourcePlayerID,
                          Bullet->SourceEntityID, Bullet->Splash,
                          Elevation::Both);
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
                                      Elevation TargetElev)
{
    auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
    for (int UID : Units)
    {
        auto* UP = EM.GetComponent<PlayerComponent>(UID);
        auto* UT = EM.GetComponent<TransformComponent>(UID);
        auto* UU = EM.GetComponent<UnitComponent>(UID);
        if (!UP || !UT || !UU || UU->HitPoints <= 0) continue;
        if (UP->PlayerID == SourcePlayerID) continue;

        float Dist = UT->Position.DistanceTo(Center);
        if (Dist > Radius) continue;

        float Falloff = SplashFalloff(Dist, Radius);
        if (Falloff <= 0.0f) continue;

        float SizeMod = GetSizeDamageModifier(DamageKind, UU->SizeType);
        int Damage = std::max(1, (int)((float)(BaseDamage + DamageUpgradeBonus) * SizeMod * Falloff * 0.5f));

        if (UU->ShieldPoints > 0)
        {
            if (Damage <= UU->ShieldPoints)
            {
                UU->ShieldPoints -= Damage;
                Damage = 0;
            }
            else
            {
                Damage -= UU->ShieldPoints;
                UU->ShieldPoints = 0;
            }
        }

        if (Damage > 0)
        {
            int FinalArmor = UU->Armor + UpgradeSystem::GetArmorBonus(EM, UID);
            Damage = std::max(1, Damage - FinalArmor);
            UU->HitPoints -= Damage;
        }

        if (UU->HitPoints <= 0)
        {
            UU->HitPoints = 0;
            RecordUnitDeath(EM, UID, SourceEntityID, true);
            EM.DestroyEntity(UID);
        }
    }
}

} // namespace Game::Systems
