#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

namespace Game::Map
{
class GameMap;
}

namespace Game::Systems
{

class BulletSystem;

class CombatSystem : public Core::ECS::ISystem
{
public:
    CombatSystem(Core::ECS::SystemManager& SysMgr, BulletSystem& BulletSys,
                 Game::Map::GameMap* Map = nullptr)
        : mSystemMgr(SysMgr), mBulletSystem(BulletSys), mMap(Map) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "CombatSystem"; }

    static void IssueAttack(Core::ECS::EntityManager& EM,
                             const std::vector<int>& Attackers,
                             int TargetEntityID);
    static void IssueAttackMove(Core::ECS::EntityManager& EM,
                                 const std::vector<int>& Units,
                                 Core::Math::Vec2 Target);

    static void ApplyDamage(Core::ECS::EntityManager& EM, int TargetID,
                            const Game::Components::CombatComponent& Attacker,
                            int AttackerID, int Damage, int HitCount = 1);
    static void ApplyDamageRaw(Core::ECS::EntityManager& EM, int TargetID,
                               const Game::Components::CombatComponent& Attacker,
                               int AttackerID, int DamageRaw, int HitCount = 1,
                               bool RespectDarkSwarm = true);
    static bool ResolvePendingAttackFrame(Core::ECS::EntityManager& EM,
                                          BulletSystem& BulletSys,
                                          int AttackerID,
                                          float ForwardOffsetPixels = 0.0f);

private:
    bool BeginOrResolveAttack(Core::ECS::EntityManager& EM,
                              int EntityID,
                              int TargetID,
                              const Components::TransformComponent& TargetTrans,
                              int BaseDamage,
                              int UpgradeBonus,
                              int HitCount,
                              float RangePixels,
                              bool MissedAtLaunch);
    void ProcessCombat(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime);
    int FindNearestEnemy(Core::ECS::EntityManager& EM, int EntityID, float MaxRange);
    bool AreEnemies(int A, int B, Core::ECS::EntityManager& EM);
    int PickWeaponDamage(const Components::CombatComponent& Combat,
                         const Components::UnitComponent* TargetUnit,
                         bool TargetIsAir) const;
    int PickWeaponHitCount(const Components::CombatComponent& Combat,
                           const Components::UnitComponent* AttackerUnit,
                           Core::ECS::EntityManager& EM,
                           int AttackerID,
                           bool TargetIsAir) const;
    float PickWeaponCooldown(const Components::CombatComponent& Combat,
                             bool TargetIsAir) const;
    bool RollMissChance(const Components::CombatComponent& Combat,
                        const Components::TransformComponent& AttackerTrans,
                        const Components::TransformComponent& TargetTrans,
                        float RangePixels,
                        Core::ECS::EntityManager& EM,
                        int AttackerID,
                        int TargetID);
    void UpdateStatusEffects(Core::ECS::EntityManager& EM, float DeltaTime);

    Core::ECS::SystemManager& mSystemMgr;
    BulletSystem& mBulletSystem;
    Game::Map::GameMap* mMap = nullptr;
    uint32_t mMissRngState = 0;
};

} // namespace Game::Systems
