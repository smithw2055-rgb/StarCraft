#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "Core/ECS/SystemManager.h"
#include "Engine/Gameplay/BuildingPlacementServer2D.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

class FogOfWarSystem;

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
    int PlayerID = -1;
    int BuildingDefID = -1;
    Core::Math::Vec2 Position;
    int PlacedEntityID = -1;
    bool IsAddon = false;
    int ParentBuildingID = -1;
    BuildTarget Target;
    int ApproachAttempt = 0;
    int ConsecutiveApproachFailures = 0;
    float ApproachSeconds = 0.0f;
    float TotalApproachSeconds = 0.0f;
    float NoProgressSeconds = 0.0f;
    float BestFootprintDistance = 0.0f;
    bool HasBestFootprintDistance = false;
    float PlacementBlockedSeconds = 0.0f;
    std::vector<int> AssistantBuilderIDs;
};

struct UnreachableBuildSiteEntry
{
    int PlayerID = -1;
    int BuildingDefID = -1;
    Core::Math::Vec2 Position;
    float RemainingSeconds = 0.0f;
};

// World-scoped Singleton Resource storing per-session pending build orders and site cooldowns.
struct BuildingOrderStore
{
    std::vector<BuildOrder> PendingBuildOrders;
    std::vector<UnreachableBuildSiteEntry> UnreachableBuildSites;
};

class BuildingSystem : public Core::ECS::ISystem
{
public:
    using ImageIDResolver = std::function<uint32_t(int)>;
    using CollisionBoundsResolver = std::function<Game::Components::UnitCollisionBounds(int)>;
    using AddonOffsetResolver = std::function<Core::Math::Vec2(int, int)>;

    using FogExploredChecker = std::function<bool(int TileX, int TileY, int PlayerID)>;

    BuildingSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "BuildingSystem"; }

    static void SetMap(const Game::Map::GameMap* Map);
    static void SetFogExploredChecker(FogExploredChecker Checker);

    template <typename TFow = FogOfWarSystem>
    static void SetFogOfWarSystem(const TFow* FOW)
    {
        if (FOW)
        {
            SetFogExploredChecker([FOW](int TileX, int TileY, int PlayerID) {
                return FOW->IsExplored(TileX, TileY, PlayerID);
            });
        }
        else
        {
            SetFogExploredChecker(nullptr);
        }
    }

    static void SetFogOfWarSystem(std::nullptr_t)
    {
        SetFogExploredChecker(nullptr);
    }

    static void ResetPendingBuildOrders();
    static void ResetPendingBuildOrders(Core::ECS::EntityManager& EM);
    static void SetImageResolver(ImageIDResolver Resolver);
    static void SetSubunitImageResolver(ImageIDResolver Resolver);
    static void SetConstructionImageResolver(ImageIDResolver Resolver);
    static uint32_t ResolveConstructionImageID(int ChkUnitID);
    static void SetCollisionBoundsResolver(CollisionBoundsResolver Resolver);
    static void SetAddonOffsetResolver(AddonOffsetResolver Resolver);

    struct BuildingFootprintTiles
    {
        int W = 3;
        int H = 2;
    };
    static inline BuildingFootprintTiles BuildingFootprintSizeForChk(int ChkUnitID)
    {
        switch (ChkUnitID)
        {
            // 4x3 (128x96)
            case 106: // Terran Command Center
            case 111: // Terran Barracks
            case 113: // Terran Factory
            case 114: // Terran Starport
            case 116: // Terran Science Facility
            case 119: // Norad II (Crashed)
            case 126: // Crashed Norad II
            case 130: // Infested Command Center
            case 131: // Zerg Hatchery
            case 132: // Zerg Lair
            case 133: // Zerg Hive
            case 147: // Overmind
            case 148: // Overmind (With Shell)
            case 151: // Cerebrate
            case 152: // Cerebrate Daggoth
            case 153: // Overmind (Without Shell)
            case 154: // Protoss Nexus
            case 160: // Protoss Gateway
            case 167: // Protoss Stargate
            case 174: // Protoss Temple
            case 175: // Xel'Naga Temple
                return {4, 3};

            // 4x2 (128x64)
            case 110: // Terran Refinery
            case 149: // Zerg Extractor
            case 157: // Protoss Assimilator
            case 188: // Vespene Geyser
                return {4, 2};

            // 3x2 (96x64)
            case 109: // Terran Supply Depot
            case 112: // Terran Academy
            case 122: // Terran Engineering Bay
            case 123: // Terran Armory
            case 125: // Terran Bunker
            case 127: // Ion Cannon
            case 134: // Zerg Nydus Canal
            case 135: // Zerg Hydralisk Den
            case 136: // Zerg Defiler Mound
            case 138: // Zerg Queen's Nest
            case 139: // Zerg Evolution Chamber
            case 140: // Zerg Ultralisk Cavern
            case 142: // Zerg Spawning Pool
            case 155: // Protoss Robotics Facility
            case 159: // Protoss Observatory
            case 163: // Protoss Citadel of Adun
            case 164: // Protoss Cybernetics Core
            case 165: // Protoss Templar Archives
            case 166: // Protoss Forge
            case 169: // Protoss Fleet Beacon
            case 170: // Protoss Arbiter Tribunal
            case 171: // Protoss Robotics Support Bay
            case 189: // Special Warp Gate
            case 190: // Special Psi Disrupter
                return {3, 2};

            // 2x2 (64x64)
            case 107: // Terran Comsat Station
            case 108: // Terran Nuclear Silo
            case 115: // Terran Control Tower
            case 117: // Terran Covert Ops
            case 118: // Terran Physics Lab
            case 120: // Terran Machine Shop
            case 124: // Terran Missile Turret
            case 137: // Zerg Greater Spire
            case 141: // Zerg Spire
            case 143: // Zerg Creep Colony
            case 144: // Zerg Spore Colony
            case 146: // Zerg Sunken Colony
            case 150: // Mature Chrysalis
            case 156: // Protoss Pylon
            case 162: // Protoss Photon Cannon
            case 172: // Protoss Shield Battery
            case 173: // Khaydarin Crystal Formation
            case 187: // Khaydarin Crystal Formation (Doodad)
            case 200: // Power Generator
            case 201: // Overmind Cocoon
                return {2, 2};

            // Special footprints
            case 186: // Ruins (3x3)
                return {3, 3};
            case 168: // Stasis Cell / Prison (5x3)
                return {5, 3};

            default:
                return {3, 2};
        }
    }
    static inline BuildingFootprintTiles BuildingFootprintSizeForDef(int BuildingDefID);

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
                           const BuildTarget& Target,
                           const std::vector<int>& AssistantBuilderIDs = {});
    static int CountPendingBuilds(const Core::ECS::EntityManager& EM, int PlayerID, int BuildingDefID);
    static int CountPendingBuilds(int PlayerID, int BuildingDefID);
    static bool IsBuilderCommitted(Core::ECS::EntityManager& EM, int BuilderID);
    static bool WasRecentlyUnreachable(const Core::ECS::EntityManager& EM,
                                       int PlayerID, int BuildingDefID,
                                       const Core::Math::Vec2& Position);
    static bool WasRecentlyUnreachable(int PlayerID, int BuildingDefID,
                                       const Core::Math::Vec2& Position);
    static bool HasPylonPowerAtPosition(const Core::ECS::EntityManager& EM,
                                        Core::Math::Vec2 Position,
                                        int PlayerID);

    static bool AssistConstruction(Core::ECS::EntityManager& EM,
                                   const std::vector<int>& BuilderIDs,
                                   int BuildingID);

    static bool StartBuild(Core::ECS::EntityManager& EM,
                           int BuilderID,
                           int BuildingDefID,
                           Core::Math::Vec2 Position,
                           const std::vector<int>& AssistantBuilderIDs = {})
    {
        return StartBuild(EM, BuilderID, BuildingDefID,
                          BuildTarget::FromPosition(Position),
                          AssistantBuilderIDs);
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
    static bool LandBuilding(Core::ECS::EntityManager& EM, int BuildingID);
    static bool LandBuilding(Core::ECS::EntityManager& EM, int BuildingID,
                             Core::Math::Vec2 Position);
    static bool CancelConstruction(Core::ECS::EntityManager& EM, int BuildingID);
    static void RestoreSpawnedAddonLinks(Core::ECS::EntityManager& EM);
    static void ProcessFlightTransition(Core::ECS::EntityManager& EM, int EntityID);
    static void AdvanceFlightHeightTransition(Core::ECS::EntityManager& EM,
                                              int EntityID,
                                              float DeltaTime);

    static void BindEnginePlacementServer(Engine::Gameplay::BuildingPlacementServer2D* Server) noexcept { sEnginePlacementServer = Server; }
    static Engine::Gameplay::BuildingPlacementServer2D* GetBoundEnginePlacementServer() noexcept { return sEnginePlacementServer; }

private:
    void ProcessPendingBuildOrders(Core::ECS::EntityManager& EM, float DeltaTime);
    void ProcessConstruction(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime);

    Core::ECS::SystemManager& mSystemMgr;
    static const Game::Map::GameMap* sMap;
    inline static Engine::Gameplay::BuildingPlacementServer2D* sEnginePlacementServer = nullptr;
};

inline BuildingSystem::BuildingFootprintTiles BuildingSystem::BuildingFootprintSizeForDef(int BuildingDefID)
{
    auto Def = Races::GetUnitDefByID(BuildingDefID);
    return BuildingFootprintSizeForChk(Def.ChkUnitID);
}

} // namespace Game::Systems
