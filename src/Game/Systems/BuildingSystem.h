#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

enum class BuildTargetType : uint8_t
{
    None,
    WorldPosition,
    TargetEntity
};

struct BuildTarget
{
    BuildTargetType Type = BuildTargetType::None;
    Core::Math::Vec2 Position;
    int TargetEntityID = -1;

    static BuildTarget FromPosition(Core::Math::Vec2 InPosition)
    {
        BuildTarget Target;
        Target.Type = BuildTargetType::WorldPosition;
        Target.Position = InPosition;
        return Target;
    }

    static BuildTarget FromEntity(int InTargetEntityID)
    {
        BuildTarget Target;
        Target.Type = BuildTargetType::TargetEntity;
        Target.TargetEntityID = InTargetEntityID;
        return Target;
    }
};

struct BuildOrder
{
    bool Active = false;
    int BuilderID = -1;
    int BuildingDefID = -1;
    Core::Math::Vec2 Position;
    int PlacedEntityID = -1;
    bool IsAddon = false;
    int ParentBuildingID = -1;
};

class BuildingSystem : public Core::ECS::ISystem
{
public:
    using ImageIDResolver = std::function<uint32_t(int)>;
    using CollisionBoundsResolver = std::function<Game::Components::UnitCollisionBounds(int)>;
    using AddonOffsetResolver = std::function<Core::Math::Vec2(int, int)>;

    BuildingSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "BuildingSystem"; }

    static void SetMap(const Game::Map::GameMap* Map);
    static void SetImageResolver(ImageIDResolver Resolver);
    static void SetConstructionImageResolver(ImageIDResolver Resolver);
    static void SetCollisionBoundsResolver(CollisionBoundsResolver Resolver);
    static void SetAddonOffsetResolver(AddonOffsetResolver Resolver);

    static bool CanBuild(const Core::ECS::EntityManager& EM,
                         int BuilderID,
                         int BuildingDefID,
                         const BuildTarget& Target = {},
                         std::string* OutReason = nullptr,
                         bool CheckPlacement = true);

    static bool CanBuild(const Core::ECS::EntityManager& EM,
                         int BuilderID,
                         int BuildingDefID,
                         Core::Math::Vec2 Position,
                         std::string* OutReason = nullptr,
                         bool CheckPlacement = true)
    {
        return CanBuild(EM, BuilderID, BuildingDefID,
                        BuildTarget::FromPosition(Position),
                        OutReason, CheckPlacement);
    }

    static bool StartBuild(Core::ECS::EntityManager& EM,
                           int BuilderID,
                           int BuildingDefID,
                           const BuildTarget& Target);

    static bool StartBuild(Core::ECS::EntityManager& EM,
                           int BuilderID,
                           int BuildingDefID,
                           Core::Math::Vec2 Position)
    {
        return StartBuild(EM, BuilderID, BuildingDefID,
                          BuildTarget::FromPosition(Position));
    }

    static int DebugSpawnCompletedBuilding(Core::ECS::EntityManager& EM,
                                           int BuildingDefID,
                                           Core::Math::Vec2 Position,
                                           int PlayerID,
                                           int ParentBuildingID = -1);

    static int DebugSpawnBuildingUnderConstruction(Core::ECS::EntityManager& EM,
                                                   int BuildingDefID,
                                                   Core::Math::Vec2 Position,
                                                   int PlayerID,
                                                   int BuilderID,
                                                   int ParentBuildingID = -1);

    static bool IsPlacementValid(Core::ECS::EntityManager& EM,
                                  int BuildingDefID,
                                  Core::Math::Vec2 Position,
                                  int BuilderPlayerID = 0);

    static bool LiftOffBuilding(Core::ECS::EntityManager& EM, int BuildingID);
    static bool LandBuilding(Core::ECS::EntityManager& EM, int BuildingID,
                             Core::Math::Vec2 Position);
    static void RestoreSpawnedAddonLinks(Core::ECS::EntityManager& EM);
    static void ProcessFlightTransition(Core::ECS::EntityManager& EM, int EntityID);

private:
    void ProcessConstruction(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime);

    Core::ECS::SystemManager& mSystemMgr;
    static const Game::Map::GameMap* sMap;
};

} // namespace Game::Systems
