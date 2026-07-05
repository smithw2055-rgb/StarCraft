#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

namespace Game::Systems
{

class BulletSystem;

class CombatSystem : public Core::ECS::ISystem
{
public:
    CombatSystem(Core::ECS::SystemManager& SysMgr, BulletSystem& BulletSys)
        : mSystemMgr(SysMgr), mBulletSystem(BulletSys) {}
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
                            int AttackerID, int Damage);
    static bool ResolvePendingAttackFrame(Core::ECS::EntityManager& EM,
                                          BulletSystem& BulletSys,
                                          int AttackerID);

private:
    bool BeginOrResolveAttack(Core::ECS::EntityManager& EM,
                              int EntityID,
                              int TargetID,
                              const Components::TransformComponent& TargetTrans,
                              int TotalDamage,
                              float RangePixels);
    void ProcessCombat(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime);
    int FindNearestEnemy(Core::ECS::EntityManager& EM, int EntityID, float MaxRange);
    bool AreEnemies(int A, int B, Core::ECS::EntityManager& EM);
    int PickWeaponDamage(const Components::CombatComponent& Combat,
                         const Components::UnitComponent* TargetUnit,
                         bool TargetIsAir) const;
    bool RollMissChance(const Components::CombatComponent& Combat,
                        const Components::TransformComponent& AttackerTrans,
                        const Components::TransformComponent& TargetTrans,
                        float RangePixels,
                        Core::ECS::EntityManager& EM) const;
    void UpdateStatusEffects(Core::ECS::EntityManager& EM, float DeltaTime);

    Core::ECS::SystemManager& mSystemMgr;
    BulletSystem& mBulletSystem;
};

} // namespace Game::Systems
