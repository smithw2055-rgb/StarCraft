#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

#include <functional>
#include <optional>
#include <unordered_map>

namespace Game::Systems
{

class BulletSystem : public Core::ECS::ISystem
{
public:
    struct VisualSpec
    {
        uint32_t ImageID = UINT32_MAX;
        // Optional unit-sized visual that follows the projectile. Carrier
        // volleys use this to keep the Interceptor body visible while the
        // Pulse Cannon projectile retains its own authored IScript.
        uint32_t FlightProxyImageID = UINT32_MAX;
        float Speed = 0.0f;
        bool HasInitAnimation = false;
        bool HasImpactAnimation = false;
        // Raw weapons.dat BulletKind value.  Keeping the raw value here lets
        // the visual/runtime bridge preserve DAT behavior without coupling
        // the resource layer to gameplay component enums.
        int BulletType = -1;
        float LifetimeSeconds = 0.0f;
        float HeadingOffsetRadians = 0.0f;
        float ForwardOffset = 0.0f;
        float UpwardOffset = 0.0f;
        int ProjectileCount = 1;
        bool AppliesAcidSpores = false;
    };
    using VisualResolver = std::function<std::optional<VisualSpec>(int, bool)>;

    BulletSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "BulletSystem"; }
    static void SetVisualResolver(VisualResolver Resolver);

    // Resolve a projectile-root DoGroundDamage/DoMissileDamage opcode. This
    // is currently used by Subterranean Spines, whose damage advances with
    // the visible spike chain instead of being deferred to the line endpoint.
    bool ResolveIScriptDamageFrame(int BulletEntityID);

    int SpawnBullet(Core::ECS::EntityManager& EM,
                    const Components::CombatComponent& SourceCombat,
                    const Components::TransformComponent& SourceTrans,
                    const Components::PlayerComponent& SourcePlayer,
                    int SourceEntityID,
                    int BaseDamage,
                    int DamageUpgradeBonus,
                    float RangePixels,
                    int TargetEntityID,
                    const Core::Math::Vec2& TargetPos,
                    int HitCount = 1,
                    bool ForceMissAtLaunch = false);

private:
    void UpdateBullet(Core::ECS::EntityManager& EM, int BulletID, float DeltaTime);
    void BulletHit(Core::ECS::EntityManager& EM, int BulletID);
    void SpawnImpactVisual(Core::ECS::EntityManager& EM,
                           int BulletID,
                           const Core::Math::Vec2& ImpactPosition);
    void ApplySplashDamage(Core::ECS::EntityManager& EM,
                           const Core::Math::Vec2& Center,
                           float InnerRadius,
                           float MediumRadius,
                           float OuterRadius,
                           int BaseDamage,
                           Components::DamageType DamageKind,
                           int DamageUpgradeBonus,
                           int SourcePlayerID,
                           int SourceEntityID,
                           Components::SplashType Splash,
                           bool CanTargetGround,
                           bool CanTargetAir,
                           bool IsRangedAttack,
                           int HitCount,
                           int DirectHitID,
                           const Core::Math::Vec2& LineStart,
                           const Core::Math::Vec2& LineEnd);

    Core::ECS::SystemManager& mSystemMgr;
    std::unordered_map<int, bool> mNextHeadingOffsetClockwise;
    std::unordered_map<int, int> mNextNearTargetPositionIndex;
    std::unordered_map<int, int> mNextFlightProxyOffsetIndex;
    bool mSuppressProjectileExpansion = false;
    static VisualResolver sVisualResolver;
};

} // namespace Game::Systems
