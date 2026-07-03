#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

class CommandSystem : public Core::ECS::ISystem
{
public:
    void Init(Core::ECS::SystemManager* SysMgr) { mSystemMgr = SysMgr; }
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "CommandSystem"; }

    static void SetMap(const Game::Map::GameMap* Map);

    // Original move command (backward compat, now wraps IssueOrder)
    static void IssueMove(Core::ECS::EntityManager& EM,
                          const std::vector<Core::ECS::EntityID>& Entities,
                          const Core::Math::Vec2& Target);

    static void IssueAttackMove(Core::ECS::EntityManager& EM,
                                 const std::vector<int>& Units,
                                 Core::Math::Vec2 Target);

    // New unified order interface
    static void IssueOrder(Core::ECS::EntityManager& EM,
                           Core::ECS::EntityID EntityID,
                           const Components::Order& InOrder,
                           bool Queued = false);

    static void IssueOrder(Core::ECS::EntityManager& EM,
                           const std::vector<Core::ECS::EntityID>& Entities,
                           const Components::Order& InOrder,
                           bool Queued = false);

    // Queue management
    static void QueueOrder(Core::ECS::EntityManager& EM,
                           Core::ECS::EntityID EntityID,
                           const Components::Order& InOrder);

    static void CancelOrder(Core::ECS::EntityManager& EM,
                            Core::ECS::EntityID EntityID,
                            int OrderIndex);

    static void CancelAll(Core::ECS::EntityManager& EM,
                          Core::ECS::EntityID EntityID);

    static void SetRallyPoint(Core::ECS::EntityManager& EM,
                              Core::ECS::EntityID EntityID,
                              const Core::Math::Vec2& Point);

    // Advance to next queued order (called when current completes)
    static void AdvanceQueue(Core::ECS::EntityManager& EM,
                             Core::ECS::EntityID EntityID);

private:
    Core::ECS::SystemManager* mSystemMgr = nullptr;
};

} // namespace Game::Systems
