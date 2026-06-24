#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

namespace Game::Systems
{

class CombatSystem : public Core::ECS::ISystem
{
public:
    CombatSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "CombatSystem"; }

    static void IssueAttack(Core::ECS::EntityManager& EM,
                             const std::vector<int>& Attackers,
                             int TargetEntityID);
    static void IssueAttackMove(Core::ECS::EntityManager& EM,
                                 const std::vector<int>& Units,
                                 Core::Math::Vec2 Target);

private:
    void ProcessCombat(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime);
    int FindNearestEnemy(Core::ECS::EntityManager& EM, int EntityID, float MaxRange);
    bool AreEnemies(int A, int B, Core::ECS::EntityManager& EM);
    void ApplyDamage(Core::ECS::EntityManager& EM, int TargetID,
                     const Game::Components::CombatComponent& Attacker,
                     int AttackerID);

    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
