#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

class ZergSystem : public Core::ECS::ISystem
{
public:
    using CollisionBoundsResolver = std::function<Game::Components::UnitCollisionBounds(int)>;

    ZergSystem(Core::ECS::SystemManager& SysMgr, Game::Map::GameMap* Map);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "ZergSystem"; }

    static bool HasCreep(int TileX, int TileY);
    static void SetMap(Game::Map::GameMap* Map);
    static void SetCollisionBoundsResolver(CollisionBoundsResolver Resolver);

    static void BurrowUnit(Core::ECS::EntityManager& EM, int EntityID);

    static bool CanMorphDroneIntoBuilding(Core::ECS::EntityManager& EM,
                                          int DroneID, int BuildingDefID,
                                          std::string* OutReason = nullptr);
    static bool StartMorphDroneIntoBuilding(Core::ECS::EntityManager& EM,
                                            int DroneID, int BuildingDefID,
                                            const Core::Math::Vec2& Position);

    static bool CanMorphLarvaIntoUnit(Core::ECS::EntityManager& EM,
                                      int LarvaID, int UnitDefID,
                                      std::string* OutReason = nullptr);
    static bool StartMorphLarvaIntoUnit(Core::ECS::EntityManager& EM,
                                        int LarvaID, int UnitDefID);

    static bool CanMorphBuilding(Core::ECS::EntityManager& EM,
                                 int BuildingID, int TargetDefID,
                                 std::string* OutReason = nullptr);
    static bool StartMorphBuilding(Core::ECS::EntityManager& EM,
                                   int BuildingID, int TargetDefID);

private:
    void SpreadCreep(Core::ECS::EntityManager& EM);
    void ProcessLarvae(Core::ECS::EntityManager& EM, float DeltaTime);
    void ProcessMorphs(Core::ECS::EntityManager& EM, float DeltaTime);
    void ProcessZergRegeneration(Core::ECS::EntityManager& EM, float DeltaTime);
    void CompleteMorph(Core::ECS::EntityManager& EM, int MorphEntityID);

    Core::ECS::SystemManager& mSystemMgr;
    static Game::Map::GameMap* sMap;

    std::vector<uint8_t> mCreepGrid;
    int mMapWidth = 0;
    int mMapHeight = 0;
    float mCreepTimer = 0.0f;
    float mCreepInterval = 1.0f;
    float mCreepRegressionTimer = 0.0f;
    bool mGridInitialized = false;
};

} // namespace Game::Systems
