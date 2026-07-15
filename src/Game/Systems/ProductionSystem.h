#pragma once

#include <functional>

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

class ProductionSystem : public Core::ECS::ISystem
{
public:
    using ImageIDResolver = std::function<uint32_t(int)>;
    using CollisionBoundsResolver = std::function<Game::Components::UnitCollisionBounds(int)>;

    ProductionSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "ProductionSystem"; }

    static void SetImageResolver(ImageIDResolver Resolver);
    static void SetSubunitImageResolver(ImageIDResolver Resolver);
    static void SetCollisionBoundsResolver(CollisionBoundsResolver Resolver);
    static void SetMap(const Game::Map::GameMap* Map);

    static Core::Math::Vec2 FindSpawnPosition(
        Core::ECS::EntityManager& EM,
        int ProducerID,
        int UnitChkID,
        int IgnoreEntityID = -1);

    static bool CanTrain(const Core::ECS::EntityManager& EM,
                         int BuildingID, int UnitDefID,
                         std::string* OutReason = nullptr);

    static bool StartTrain(Core::ECS::EntityManager& EM,
                           int BuildingID, int UnitDefID);

    static bool CancelTrain(Core::ECS::EntityManager& EM,
                            int BuildingID, int QueueIndex = -1);

    static int DebugSpawnUnit(Core::ECS::EntityManager& EM,
                              int UnitDefID,
                              Core::Math::Vec2 Position,
                              int PlayerID);

private:
    void ProcessTraining(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime);

    static int SpawnUnit(Core::ECS::EntityManager& EM,
                         int BuildingID, int UnitDefID);

    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
