#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

#include <functional>
#include <optional>

namespace Game::Systems
{

class BulletSystem : public Core::ECS::ISystem
{
public:
    struct VisualSpec
    {
        uint32_t ImageID = UINT32_MAX;
        float Speed = 0.0f;
    };
    using VisualResolver = std::function<std::optional<VisualSpec>(int, bool)>;

    BulletSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "BulletSystem"; }
    static void SetVisualResolver(VisualResolver Resolver);

    int SpawnBullet(Core::ECS::EntityManager& EM,
                    const Components::CombatComponent& SourceCombat,
                    const Components::TransformComponent& SourceTrans,
                    const Components::PlayerComponent& SourcePlayer,
                    int SourceEntityID,
                    int BaseDamage,
                    int DamageUpgradeBonus,
                    float RangePixels,
                    int TargetEntityID,
                    const Core::Math::Vec2& TargetPos);

private:
    void UpdateBullet(Core::ECS::EntityManager& EM, int BulletID, float DeltaTime);
    void BulletHit(Core::ECS::EntityManager& EM, int BulletID);
    void ApplySplashDamage(Core::ECS::EntityManager& EM,
                           const Core::Math::Vec2& Center,
                           float Radius,
                           int BaseDamage,
                           Components::DamageType DamageKind,
                           int DamageUpgradeBonus,
                           int SourcePlayerID,
                           int SourceEntityID,
                           Components::SplashType Splash,
                           Components::Elevation TargetElev,
                           bool IsRangedAttack);

    Core::ECS::SystemManager& mSystemMgr;
    static VisualResolver sVisualResolver;
};

} // namespace Game::Systems
