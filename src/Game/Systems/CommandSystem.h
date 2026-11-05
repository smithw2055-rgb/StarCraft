#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/Pathfinding/NavigationService.h"

#include <cstdint>
#include <functional>

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

class CommandSystem : public Core::ECS::ISystem
{
public:
    using ImageIDResolver = std::function<uint32_t(int)>;

    void Init(Core::ECS::SystemManager* SysMgr) { mSystemMgr = SysMgr; }
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "CommandSystem"; }

    static void SetMap(const Game::Map::GameMap* Map);
    static void SetImageResolver(ImageIDResolver Resolver);
    static void SetSubunitImageResolver(ImageIDResolver Resolver);
    static const Game::Pathfinding::NavigationPerformanceStats&
        GetNavigationStats();
    static void ResetNavigationStats();
    static void SyncNavigationState(
        Core::ECS::EntityManager& EM,
        const Game::Map::GameMap* Map);

    // Original move command (backward compat, now wraps IssueOrder)
    static void IssueMove(Core::ECS::EntityManager& EM,
                          const std::vector<Core::ECS::EntityID>& Entities,
                          const Core::Math::Vec2& Target);

    static void IssueMove(Core::ECS::EntityManager& EM,
                          const std::vector<Core::ECS::EntityID>& Entities,
                          const Core::Math::Vec2& Target,
                          int PathingIgnoreEntityID);

    static void IssueMove(Core::ECS::EntityManager& EM,
                          const std::vector<Core::ECS::EntityID>& Entities,
                          const Core::Math::Vec2& Target,
                          int PathingIgnoreEntityID,
                          bool Queued);

    // Explicit player commands must release only the affected worker from its
    // resource slot. Internal harvest/construction approach moves deliberately
    // bypass this through IssueMoveAndHasPath().
    static void InterruptHarvest(Core::ECS::EntityManager& EM,
                                 Core::ECS::EntityID EntityID);

    static bool IssueMoveAndHasPath(Core::ECS::EntityManager& EM,
                                    Core::ECS::EntityID EntityID,
                                    const Core::Math::Vec2& Target,
                                    int PathingIgnoreEntityID = -1);

    // Shared movement path request used both when an order is first issued and
    // when MovementSystem must continue or rebuild a route. Long ground moves
    // are deliberately staged so every local A* search stays bounded.
    static bool RequestMovementPath(Core::ECS::EntityManager& EM,
                                    Core::ECS::EntityID EntityID,
                                    const Game::Map::GameMap* Map,
                                    const Core::Math::Vec2& Target,
                                    int PathingIgnoreEntityID = -1,
                                    int AvoidDynamicEntityID = -1);

    static void IssueAttackMove(Core::ECS::EntityManager& EM,
                                 const std::vector<int>& Units,
                                 Core::Math::Vec2 Target);

    static void IssueAttackMove(Core::ECS::EntityManager& EM,
                                 const std::vector<int>& Units,
                                 Core::Math::Vec2 Target,
                                 bool Queued);

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

    static bool CanSetSiegeMode(Core::ECS::EntityManager& EM,
                                Core::ECS::EntityID EntityID,
                                bool Siege);
    static bool SetSiegeMode(Core::ECS::EntityManager& EM,
                             Core::ECS::EntityID EntityID,
                             bool Siege);

    static bool IsTerranRepairableUnit(const Components::UnitComponent& Unit);
    static bool IsRepairableTarget(Core::ECS::EntityManager& EM, int TargetEntityID);

    // Advance to next queued order (called when current completes)
    static void AdvanceQueue(Core::ECS::EntityManager& EM,
                             Core::ECS::EntityID EntityID);

private:
    Core::ECS::SystemManager* mSystemMgr = nullptr;
};

} // namespace Game::Systems
