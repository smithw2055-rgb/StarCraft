#pragma once

#include <cstddef>
#include <functional>

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
class FogOfWarSystem;

class CombatSystem : public Core::ECS::ISystem
{
public:
    using DetectorChecker = std::function<bool(int TileX, int TileY, int PlayerID)>;
    using AllianceChecker = std::function<bool(int PlayerA, int PlayerB)>;

    CombatSystem(Core::ECS::SystemManager& SysMgr, BulletSystem& BulletSys,
                 Game::Map::GameMap* Map = nullptr)
        : mSystemMgr(SysMgr), mBulletSystem(BulletSys), mMap(Map) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "CombatSystem"; }

    static void SetDetectorChecker(DetectorChecker Checker);
    static void SetAllianceChecker(AllianceChecker Checker);

    template <typename TFow = FogOfWarSystem>
    static void SetFogOfWarSystem(const TFow* FOW)
    {
        if (FOW)
        {
            SetDetectorChecker([FOW](int TileX, int TileY, int PlayerID) {
                return FOW->IsGameplayDetected(TileX, TileY, PlayerID);
            });
            SetAllianceChecker([FOW](int PlayerA, int PlayerB) {
                return FOW->AreAllied(PlayerA, PlayerB);
            });
        }
        else
        {
            SetDetectorChecker(nullptr);
            SetAllianceChecker(nullptr);
        }
    }

    static void SetFogOfWarSystem(std::nullptr_t)
    {
        SetDetectorChecker(nullptr);
        SetAllianceChecker(nullptr);
    }

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
    static int FindNearestEnemy(Core::ECS::EntityManager& EM, int EntityID, float MaxRange);
    static bool AreEnemies(int A, int B, Core::ECS::EntityManager& EM);

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
