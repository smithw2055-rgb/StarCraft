#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"

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

    // Issue a move command to a set of entities
    static void IssueMove(Core::ECS::EntityManager& EM, const std::vector<Core::ECS::EntityID>& Entities,
                          const Core::Math::Vec2& Target);

private:
    Core::ECS::SystemManager* mSystemMgr = nullptr;
};

} // namespace Game::Systems
