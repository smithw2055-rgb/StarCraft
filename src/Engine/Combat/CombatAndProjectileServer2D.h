#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Math/Vec2.h"
#include "Engine/Physics/PhysicsServer2D.h"
#include "Resource/ModernAssetRegistry.h"

namespace Engine::Combat
{

// ============================================================================
// CombatAndProjectileServer2D (Phase 76):
// Referenced from Unity DOTS Damage/Ballistics Pipeline & Godot 4 2D Projectile
// Server:
// - Imports all 130 weapon definitions from cloud assets-R/Weapons_Catalog_130.json
// - Evaluates deterministic armor, plasma shield, and unit-size damage multipliers
//   (Normal, Explosive, Concussive, IgnoreArmor vs Small, Medium, Large)
// - Evaluates 3-tier radial splash damage (Inner 100%, Medium 50%, Outer 25%)
//   and steps pooled 2D ballistic projectiles.
// ============================================================================
enum class WeaponDamageKind : uint8_t
{
    Independent = 0,
    Explosive = 1,
    Concussive = 2,
    Normal = 3,
    IgnoreArmor = 4
};

enum class TargetSizeKind : uint8_t
{
    Independent = 0,
    Small = 1,
    Medium = 2,
    Large = 3
};

struct WeaponDefinition2D
{
    uint32_t WeaponID = 0;
    uint32_t ProjectileFlingyID = 0;
    int32_t DamageAmount = 0;
    int32_t DamageBonusPerUpgrade = 0;
    int32_t DamageFactor = 1;
    WeaponDamageKind DamageKind = WeaponDamageKind::Normal;
    uint32_t CooldownTicks = 15;
    float MinRangePx = 0.0f;
    float MaxRangePx = 128.0f;
    float SplashInnerPx = 0.0f;
    float SplashMediumPx = 0.0f;
    float SplashOuterPx = 0.0f;
};

struct DamageEvaluationResult
{
    int32_t ShieldDamageDealt = 0;
    int32_t HullDamageDealt = 0;
    int32_t RemainingShields = 0;
    int32_t RemainingHitPoints = 0;
    bool TargetKilled = false;
};

struct BallisticProjectile2D
{
    uint32_t ProjectileID = 0;
    uint32_t WeaponID = 0;
    int32_t SourceEntityID = -1;
    int32_t TargetEntityID = -1;
    int32_t OwnerPlayerID = 0;
    int32_t UpgradeLevel = 0;
    Core::Math::Vec2 Position{0.0f, 0.0f};
    Core::Math::Vec2 TargetPosition{0.0f, 0.0f};
    float SpeedPxPerSec = 480.0f;
    bool Active = true;
    bool ReachedImpact = false;
};

struct CombatServerTelemetry
{
    uint64_t DamageEvaluations = 0;
    uint64_t SplashEvaluations = 0;
    uint64_t ProjectilesSpawned = 0;
    uint64_t ProjectilesImpacted = 0;
};

class CombatAndProjectileServer2D
{
public:
    CombatAndProjectileServer2D() = default;

    static WeaponDamageKind ParseDamageKind(const std::string& Str) noexcept
    {
        if (Str == "Explosive") return WeaponDamageKind::Explosive;
        if (Str == "Concussive") return WeaponDamageKind::Concussive;
        if (Str == "Ignore_Armor" || Str == "IgnoreArmor") return WeaponDamageKind::IgnoreArmor;
        if (Str == "Independent") return WeaponDamageKind::Independent;
        return WeaponDamageKind::Normal;
    }

    // Multiplier in 256ths (256 = 100%, 192 = 75%, 128 = 50%, 64 = 25%)
    static constexpr int32_t GetSizeDamageMultiplier256(WeaponDamageKind Kind, TargetSizeKind Size) noexcept
    {
        if (Kind == WeaponDamageKind::Explosive)
        {
            if (Size == TargetSizeKind::Small) return 128;  // 50%
            if (Size == TargetSizeKind::Medium) return 192; // 75%
            return 256;                                     // 100% vs Large
        }
        if (Kind == WeaponDamageKind::Concussive)
        {
            if (Size == TargetSizeKind::Small) return 256;  // 100% vs Small
            if (Size == TargetSizeKind::Medium) return 128; // 50% vs Medium
            if (Size == TargetSizeKind::Large) return 64;   // 25% vs Large
        }
        return 256;
    }

    size_t ImportFromModernRegistry(const Resource::ModernAssetRegistry& Registry)
    {
        mWeapons.clear();
        for (const auto& W : Registry.GetWeapons())
        {
            WeaponDefinition2D Def{};
            Def.WeaponID = W.WeaponID;
            Def.ProjectileFlingyID = W.ProjectileFlingyID;
            Def.DamageAmount = static_cast<int32_t>(W.DamageAmount);
            Def.DamageBonusPerUpgrade = static_cast<int32_t>(W.DamageBonusPerUpgrade);
            Def.DamageFactor = std::max<int32_t>(1, static_cast<int32_t>(W.DamageFactor));
            Def.DamageKind = ParseDamageKind(W.DamageType);
            Def.CooldownTicks = std::max<uint32_t>(1u, W.CooldownTicks);
            Def.MinRangePx = static_cast<float>(W.MinRangePx);
            Def.MaxRangePx = static_cast<float>(W.MaxRangePx);
            Def.SplashInnerPx = static_cast<float>(W.SplashRadiiPx[0]);
            Def.SplashMediumPx = static_cast<float>(W.SplashRadiiPx[1]);
            Def.SplashOuterPx = static_cast<float>(W.SplashRadiiPx[2]);
            mWeapons[Def.WeaponID] = Def;
        }
        return mWeapons.size();
    }

    void RegisterWeapon(const WeaponDefinition2D& Def)
    {
        mWeapons[Def.WeaponID] = Def;
    }

    const WeaponDefinition2D* FindWeapon(uint32_t WeaponID) const
    {
        auto It = mWeapons.find(WeaponID);
        return It != mWeapons.end() ? &It->second : nullptr;
    }

    DamageEvaluationResult EvaluateWeaponDamage(uint32_t WeaponID,
                                                int32_t WeaponUpgradeLevel,
                                                int32_t TargetHitPoints,
                                                int32_t TargetShields,
                                                int32_t TargetArmor,
                                                int32_t TargetShieldUpgradeLevel,
                                                TargetSizeKind TargetSize,
                                                int32_t SplashTierPercent = 100) noexcept
    {
        ++mTelemetry.DamageEvaluations;
        DamageEvaluationResult Result{};
        Result.RemainingHitPoints = TargetHitPoints;
        Result.RemainingShields = TargetShields;

        const auto* W = FindWeapon(WeaponID);
        const int32_t BaseDmg = W ? (W->DamageAmount + WeaponUpgradeLevel * W->DamageBonusPerUpgrade) * W->DamageFactor : 6;
        const WeaponDamageKind Kind = W ? W->DamageKind : WeaponDamageKind::Normal;

        int32_t EffectiveDmg = (BaseDmg * std::clamp(SplashTierPercent, 0, 100)) / 100;
        if (EffectiveDmg <= 0)
            return Result;

        // 1. Plasma Shields absorb damage first (taking full damage before size multiplier, minus shield upgrades)
        int32_t RemainingAttack = EffectiveDmg;
        if (Result.RemainingShields > 0 && Kind != WeaponDamageKind::IgnoreArmor)
        {
            const int32_t ShieldReduced = std::max(1, RemainingAttack - TargetShieldUpgradeLevel);
            const int32_t Absorbed = std::min(Result.RemainingShields, ShieldReduced);
            Result.RemainingShields -= Absorbed;
            Result.ShieldDamageDealt = Absorbed;
            RemainingAttack = std::max(0, ShieldReduced - Absorbed);
        }

        // 2. Remaining damage applies target armor and size-vs-damage-type multiplier
        if (RemainingAttack > 0 && Result.RemainingHitPoints > 0)
        {
            const int32_t ArmorReduced = (Kind == WeaponDamageKind::IgnoreArmor)
                ? RemainingAttack
                : std::max(1, RemainingAttack - TargetArmor);
            const int32_t Mult256 = GetSizeDamageMultiplier256(Kind, TargetSize);
            const int32_t FinalHullDmg = std::max(1, (ArmorReduced * Mult256) / 256);

            Result.HullDamageDealt = std::min(Result.RemainingHitPoints, FinalHullDmg);
            Result.RemainingHitPoints -= Result.HullDamageDealt;
        }

        Result.TargetKilled = (Result.RemainingHitPoints <= 0);
        return Result;
    }

    // Evaluates 3-tier splash tier percentage (100 / 50 / 25 / 0) for a given distance from impact
    int32_t ResolveSplashTierPercent(uint32_t WeaponID, float DistancePx) const noexcept
    {
        const auto* W = FindWeapon(WeaponID);
        if (!W || W->SplashOuterPx <= 0.0f)
            return DistancePx <= 4.0f ? 100 : 0;
        if (DistancePx <= W->SplashInnerPx) return 100;
        if (DistancePx <= W->SplashMediumPx) return 50;
        if (DistancePx <= W->SplashOuterPx) return 25;
        return 0;
    }

    // Queries PhysicsServer2D for entities within outer splash radius and returns (EntityID, SplashTierPercent)
    std::vector<std::pair<Core::ECS::EntityID, int32_t>> QuerySplashTargets(
        uint32_t WeaponID,
        Core::Math::Vec2 ImpactPos,
        const Engine::Physics::PhysicsServer2D& Physics,
        uint32_t LayerMask = Engine::Physics::CollisionLayer::All)
    {
        ++mTelemetry.SplashEvaluations;
        std::vector<std::pair<Core::ECS::EntityID, int32_t>> Hits;
        const auto* W = FindWeapon(WeaponID);
        if (!W || W->SplashOuterPx <= 0.0f)
            return Hits;

        std::vector<Core::ECS::EntityID> Candidates;
        Physics.OverlapCircle(ImpactPos, W->SplashOuterPx, LayerMask, Candidates);
        for (Core::ECS::EntityID Id : Candidates)
        {
            const auto* Body = Physics.FindBody(Id);
            if (!Body)
                continue;
            const float Dist = ImpactPos.DistanceTo(Body->Position);
            const int32_t TierPct = ResolveSplashTierPercent(WeaponID, Dist);
            if (TierPct > 0)
                Hits.emplace_back(Id, TierPct);
        }
        return Hits;
    }

    uint32_t SpawnProjectile(uint32_t WeaponID,
                             int32_t SourceEntityID,
                             int32_t TargetEntityID,
                             int32_t OwnerPlayerID,
                             Core::Math::Vec2 StartPos,
                             Core::Math::Vec2 TargetPos,
                             float SpeedPxPerSec = 480.0f)
    {
        ++mTelemetry.ProjectilesSpawned;
        const uint32_t Id = ++mNextProjectileID;
        mProjectiles.push_back(BallisticProjectile2D{
            Id, WeaponID, SourceEntityID, TargetEntityID, OwnerPlayerID, 0,
            StartPos, TargetPos, std::max(32.0f, SpeedPxPerSec), true, false
        });
        return Id;
    }

    std::vector<BallisticProjectile2D> StepProjectiles(float DeltaSeconds)
    {
        std::vector<BallisticProjectile2D> Impacted;
        const float Dt = std::max(0.0f, DeltaSeconds);
        for (auto& Proj : mProjectiles)
        {
            if (!Proj.Active)
                continue;
            const float Dist = Proj.Position.DistanceTo(Proj.TargetPosition);
            const float Step = Proj.SpeedPxPerSec * Dt;
            if (Dist <= Step || Dist <= 4.0f)
            {
                Proj.Position = Proj.TargetPosition;
                Proj.Active = false;
                Proj.ReachedImpact = true;
                Impacted.push_back(Proj);
                ++mTelemetry.ProjectilesImpacted;
            }
            else
            {
                const Core::Math::Vec2 Dir = (Proj.TargetPosition - Proj.Position).Normalized();
                Proj.Position = Proj.Position + Dir * Step;
            }
        }
        mProjectiles.erase(
            std::remove_if(mProjectiles.begin(), mProjectiles.end(),
                           [](const BallisticProjectile2D& P) { return !P.Active; }),
            mProjectiles.end());
        return Impacted;
    }

    size_t GetWeaponCount() const noexcept { return mWeapons.size(); }
    size_t GetActiveProjectileCount() const noexcept { return mProjectiles.size(); }
    const CombatServerTelemetry& GetTelemetry() const noexcept { return mTelemetry; }

private:
    uint32_t mNextProjectileID = 0;
    std::unordered_map<uint32_t, WeaponDefinition2D> mWeapons;
    std::vector<BallisticProjectile2D> mProjectiles;
    CombatServerTelemetry mTelemetry{};
};

} // namespace Engine::Combat
