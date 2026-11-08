#include "Game/Systems/BuildingSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

#include "Core/Math/Vec2.h"
#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/HarvestSystem.h"

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    const Game::Map::GameMap* gMap = nullptr;
    BuildingSystem::FogExploredChecker gFogExploredChecker;
    BuildingSystem::ImageIDResolver gImageResolver;
    BuildingSystem::ImageIDResolver gSubunitImageResolver;
    BuildingSystem::ImageIDResolver gConstructionImageResolver;
    BuildingSystem::CollisionBoundsResolver gCollisionBoundsResolver;
    BuildingSystem::AddonOffsetResolver gAddonOffsetResolver;
    BuildingOrderStore gFallbackBuildingOrders;

    BuildingOrderStore& ResolveOrderStore(Core::ECS::EntityManager& EM)
    {
        if (auto* Store = EM.GetResource<BuildingOrderStore>())
            return *Store;
        return gFallbackBuildingOrders;
    }

    const BuildingOrderStore& ResolveOrderStore(const Core::ECS::EntityManager& EM)
    {
        if (const auto* Store = EM.GetResource<BuildingOrderStore>())
            return *Store;
        return gFallbackBuildingOrders;
    }

    BuildingOrderStore& ResolveActiveOrFallbackOrderStore()
    {
        if (auto* ActiveWorld = Core::ECS::EntityManager::GetActiveWorld())
        {
            if (auto* Store = ActiveWorld->GetResource<BuildingOrderStore>())
                return *Store;
        }
        return gFallbackBuildingOrders;
    }
    constexpr uint32_t ProtossWarpAnchorImageID = 211;
    constexpr int MaxPendingBuildApproachFailures = 8;
    constexpr float MaxPendingBuildApproachSeconds = 180.0f;
    constexpr float UnreachableBuildSiteCooldownSeconds = 90.0f;
    constexpr float UnreachableBuilderCooldownSeconds = 30.0f;

    using BuildingFootprintTiles = Game::Systems::BuildingSystem::BuildingFootprintTiles;

    struct TileRect
    {
        int Left = 0;
        int Top = 0;
        int Right = 0;
        int Bottom = 0;
    };

    void SetReason(std::string* OutReason, const std::string& Reason)
    {
        if (OutReason)
            *OutReason = Reason;
    }

    bool DebugConstructionEnabled()
    {
        static const bool Enabled = std::getenv("STARCRAFT_DEBUG_CONSTRUCTION") != nullptr;
        return Enabled;
    }

    uint32_t ResolveImageID(int ChkUnitID)
    {
        return gImageResolver ? gImageResolver(ChkUnitID) : (uint32_t)ChkUnitID;
    }

    uint32_t ResolveSubunitImageID(int ChkUnitID)
    {
        return gSubunitImageResolver
            ? gSubunitImageResolver(ChkUnitID)
            : std::numeric_limits<uint32_t>::max();
    }

    uint32_t ResolveConstructionImageID(int ChkUnitID)
    {
        if (gConstructionImageResolver)
        {
            const uint32_t ImageID = gConstructionImageResolver(ChkUnitID);
            if (ImageID != std::numeric_limits<uint32_t>::max())
                return ImageID;
        }
        return ResolveImageID(ChkUnitID);
    }

    UnitCollisionBounds ResolveCollisionBounds(int ChkUnitID)
    {
        return gCollisionBoundsResolver ? gCollisionBoundsResolver(ChkUnitID) : UnitCollisionBounds{};
    }

    uint32_t ResolveAddonConnectorImageID(int ChkUnitID)
    {
        // These are the explicit connector images in the canonical images.dat
        // sequence.  They are not units.dat subunits, so the regular entity
        // spawn path cannot discover them from the add-on unit alone.
        switch (ChkUnitID)
        {
            case 107: return 272; // Comsat Station Connector
            case 108: return 313; // Nuclear Silo Connector
            case 115: return 282; // Control Tower Connector
            case 117: return 289; // Covert Ops Connector
            case 118: return 302; // Physics Lab Connector
            case 120: return 294; // Machine Shop Connector
            default: return std::numeric_limits<uint32_t>::max();
        }
    }

    void SetAddonConnectorVisual(Core::ECS::EntityManager& EM,
                                 int AddonID,
                                 bool IsConnected)
    {
        auto* Render = EM.GetComponent<RenderComponent>(AddonID);
        auto* Unit = EM.GetComponent<UnitComponent>(AddonID);
        if (!Render)
            return;

        Render->AddonConnectorImageID = IsConnected && Unit
            ? ResolveAddonConnectorImageID(Unit->ChkUnitID)
            : std::numeric_limits<uint32_t>::max();
    }

    inline BuildingFootprintTiles BuildingFootprintSizeForChk(int ChkUnitID)
    {
        return BuildingSystem::BuildingFootprintSizeForChk(ChkUnitID);
    }

    inline BuildingFootprintTiles BuildingFootprintSizeForDef(int BuildingDefID)
    {
        return BuildingSystem::BuildingFootprintSizeForDef(BuildingDefID);
    }

    TileRect BuildingTileRect(const Game::Map::GameMap& Map,
                              Core::Math::Vec2 Position,
                              BuildingFootprintTiles Footprint)
    {
        int CenterX = 0;
        int CenterY = 0;
        Map.WorldToTile(Position, CenterX, CenterY);
        const int Left = CenterX - Footprint.W / 2;
        const int Top = CenterY - Footprint.H / 2;
        return {Left, Top, Left + Footprint.W - 1, Top + Footprint.H - 1};
    }

    bool HasCreepAcrossFootprint(int BuildingDefID, const Core::Math::Vec2& Position)
    {
        if (!gMap)
            return false;
        const TileRect Tiles = BuildingTileRect(
            *gMap, Position, BuildingFootprintSizeForDef(BuildingDefID));
        for (int Y = Tiles.Top; Y <= Tiles.Bottom; ++Y)
        {
            for (int X = Tiles.Left; X <= Tiles.Right; ++X)
            {
                if (!gMap->IsInBounds(X, Y) || !gMap->GetTile(X, Y).HasCreep)
                    return false;
            }
        }
        return true;
    }

    bool RectsOverlap(const TileRect& A, const TileRect& B)
    {
        return !(A.Right < B.Left || A.Left > B.Right ||
                 A.Bottom < B.Top || A.Top > B.Bottom);
    }

    bool ConflictsWithPendingBuild(int BuildingDefID,
                                   const BuildTarget& Target,
                                   int IgnoreBuilderID = -1)
    {
        for (const auto& Order : ResolveActiveOrFallbackOrderStore().PendingBuildOrders)
        {
            if (!Order.Active || Order.BuilderID == IgnoreBuilderID)
                continue;
            if (Target.Type == BuildTargetType::TargetEntity &&
                Order.Target.Type == BuildTargetType::TargetEntity &&
                Target.TargetEntityID == Order.Target.TargetEntityID)
            {
                return true;
            }
            if (!gMap || Target.Type != BuildTargetType::WorldPosition ||
                Order.Target.Type != BuildTargetType::WorldPosition)
            {
                continue;
            }
            const TileRect Requested = BuildingTileRect(
                *gMap, Target.Position, BuildingFootprintSizeForDef(BuildingDefID));
            const TileRect Pending = BuildingTileRect(
                *gMap, Order.Position,
                BuildingFootprintSizeForDef(Order.BuildingDefID));
            if (RectsOverlap(Requested, Pending))
                return true;
        }
        return false;
    }

    TileRect BoundsTileRect(const Game::Map::GameMap& Map,
                            Core::Math::Vec2 Position,
                            float Left,
                            float Up,
                            float Right,
                            float Down)
    {
        int MinX = 0;
        int MinY = 0;
        int MaxX = 0;
        int MaxY = 0;
        Map.WorldToTile({Position.X - Left, Position.Y - Up}, MinX, MinY);
        Map.WorldToTile({Position.X + Right, Position.Y + Down}, MaxX, MaxY);
        return {std::min(MinX, MaxX), std::min(MinY, MaxY),
                std::max(MinX, MaxX), std::max(MinY, MaxY)};
    }

    float CollisionRadiusFromBounds(const PathfindingComponent* Path, float Fallback)
    {
        if (!Path)
            return Fallback;
        if (!Path->HasCollisionBounds)
            return Path->CollisionRadius > 0 ? (float)Path->CollisionRadius : Fallback;

        const int Width = Path->CollisionLeft + Path->CollisionRight + 1;
        const int Height = Path->CollisionUp + Path->CollisionDown + 1;
        return (float)std::max(4, std::max(Width, Height) / 2);
    }

    bool IsMoveSettled(const PathfindingComponent* Path)
    {
        if (!Path)
            return true;
        if (Path->Path.empty())
            return true;
        return Path->PathIndex >= (int)Path->Path.size();
    }

    void AddUniqueCandidate(std::vector<Core::Math::Vec2>& Candidates,
                            const Core::Math::Vec2& Candidate)
    {
        for (const auto& Existing : Candidates)
            if ((Existing - Candidate).LengthSq() < 1.0f)
                return;
        Candidates.push_back(Candidate);
    }

    bool IsRecentFailedMoveTarget(const PathfindingComponent* Path,
                                  const Core::Math::Vec2& Candidate)
    {
        return Path && Path->HasRecentMoveFailure &&
               (Path->RecentFailedMoveTarget - Candidate).LengthSq() <= 16.0f * 16.0f;
    }

    void BuildConstructionApproachCandidates(std::vector<Core::Math::Vec2>& Candidates,
                                             Core::ECS::EntityManager& EM,
                                             int BuilderID,
                                             int BuildingID,
                                             Core::Math::Vec2 BuildPosition,
                                             int BuildingDefID)
    {
        auto* BuilderTrans = EM.GetComponent<TransformComponent>(BuilderID);
        float Left = 48.0f;
        float Right = 48.0f;
        float Up = 32.0f;
        float Down = 32.0f;
        if (const auto* BuildingPath = EM.GetComponent<PathfindingComponent>(BuildingID);
            BuildingPath && BuildingPath->HasCollisionBounds)
        {
            Left = (float)BuildingPath->CollisionLeft;
            Right = (float)BuildingPath->CollisionRight;
            Up = (float)BuildingPath->CollisionUp;
            Down = (float)BuildingPath->CollisionDown;
        }
        else
        {
            const auto Footprint = BuildingFootprintSizeForDef(BuildingDefID);
            Left = Right = (float)Footprint.W * 16.0f;
            Up = Down = (float)Footprint.H * 16.0f;
        }

        const float BuilderRadius = CollisionRadiusFromBounds(
            EM.GetComponent<PathfindingComponent>(BuilderID), 16.0f);
        const float Padding = BuilderRadius + 4.0f;
        Core::Math::Vec2 Delta = BuilderTrans ? BuilderTrans->Position - BuildPosition
                                              : Core::Math::Vec2{0.0f, 1.0f};
        if (Delta.Length() < 0.001f)
            Delta = {0.0f, 1.0f};
        const float ClampedX = std::clamp(Delta.X, -Left, Right);
        const float ClampedY = std::clamp(Delta.Y, -Up, Down);

        const auto BuildingDef = Races::GetUnitDefByID(BuildingDefID);
        if (BuildingDef.Name == "Refinery" ||
            BuildingDef.Name == "Extractor" ||
            BuildingDef.Name == "Assimilator")
        {
            // All three gas-building sprites author their worker threshold at
            // bottom-center.  Approaching the claimed geyser from an arbitrary
            // nearest side can trap the builder against the enlarged refinery
            // collision bounds on real melee mineral layouts.
            AddUniqueCandidate(
                Candidates,
                {BuildPosition.X, BuildPosition.Y + Down + Padding});
        }

        auto AddSide = [&](int Side)
        {
            Core::Math::Vec2 Candidate = BuildPosition;
            switch (Side)
            {
                case 0:
                    Candidate.X -= Left + Padding;
                    Candidate.Y += ClampedY;
                    break;
                case 1:
                    Candidate.X += Right + Padding;
                    Candidate.Y += ClampedY;
                    break;
                case 2:
                    Candidate.X += ClampedX;
                    Candidate.Y -= Up + Padding;
                    break;
                default:
                    Candidate.X += ClampedX;
                    Candidate.Y += Down + Padding;
                    break;
            }
            AddUniqueCandidate(Candidates, Candidate);
        };

        const float HorizontalScale = Delta.X < 0.0f ? std::max(Left, 1.0f) : std::max(Right, 1.0f);
        const float VerticalScale = Delta.Y < 0.0f ? std::max(Up, 1.0f) : std::max(Down, 1.0f);
        const int PrimarySide =
            std::abs(Delta.X) / HorizontalScale >= std::abs(Delta.Y) / VerticalScale
                ? (Delta.X < 0.0f ? 0 : 1)
                : (Delta.Y < 0.0f ? 2 : 3);

        AddSide(PrimarySide);
        AddSide(0);
        AddSide(1);
        AddSide(2);
        AddSide(3);
        AddUniqueCandidate(Candidates, {BuildPosition.X - Left - Padding, BuildPosition.Y - Up - Padding});
        AddUniqueCandidate(Candidates, {BuildPosition.X + Right + Padding, BuildPosition.Y - Up - Padding});
        AddUniqueCandidate(Candidates, {BuildPosition.X - Left - Padding, BuildPosition.Y + Down + Padding});
        AddUniqueCandidate(Candidates, {BuildPosition.X + Right + Padding, BuildPosition.Y + Down + Padding});
    }

    Core::Math::Vec2 ConstructionApproachPosition(Core::ECS::EntityManager& EM,
                                                  int BuilderID,
                                                  int BuildingID,
                                                  Core::Math::Vec2 BuildPosition,
                                                  int BuildingDefID)
    {
        Core::Math::Vec2 Direction{0.0f, 1.0f};
        if (auto* BuilderTrans = EM.GetComponent<TransformComponent>(BuilderID))
        {
            Direction = BuilderTrans->Position - BuildPosition;
            if (Direction.Length() < 0.001f)
                Direction = {0.0f, 1.0f};
        }

        const auto* BuildingPath = EM.GetComponent<PathfindingComponent>(BuildingID);
        float Left = 48.0f;
        float Right = 48.0f;
        float Up = 32.0f;
        float Down = 32.0f;
        if (BuildingPath && BuildingPath->HasCollisionBounds)
        {
            Left = (float)BuildingPath->CollisionLeft;
            Right = (float)BuildingPath->CollisionRight;
            Up = (float)BuildingPath->CollisionUp;
            Down = (float)BuildingPath->CollisionDown;
        }
        else
        {
            const auto Footprint = BuildingFootprintSizeForDef(BuildingDefID);
            Left = Right = (float)Footprint.W * 16.0f;
            Up = Down = (float)Footprint.H * 16.0f;
        }

        const float Padding = std::max(0.0f, CollisionRadiusFromBounds(
            EM.GetComponent<PathfindingComponent>(BuilderID), 16.0f) - 1.0f);
        Core::Math::Vec2 Offset;
        if (std::fabs(Direction.X) >= std::fabs(Direction.Y))
        {
            Offset.X = Direction.X >= 0.0f ? Right + Padding : -Left - Padding;
            Offset.Y = std::clamp(Direction.Y, -Up, Down);
        }
        else
        {
            Offset.X = std::clamp(Direction.X, -Left, Right);
            Offset.Y = Direction.Y >= 0.0f ? Down + Padding : -Up - Padding;
        }
        return BuildPosition + Offset;
    }

    void IssueConstructionMove(Core::ECS::EntityManager& EM,
                               int BuilderID,
                               int BuildingID,
                               Core::Math::Vec2 BuildPosition,
                               int BuildingDefID,
                               int CandidateOffset = 0)
    {
        const auto PreserveConstructionOrder =
            [&](const Core::Math::Vec2& ApproachPosition)
        {
            auto* Command = EM.GetComponent<CommandComponent>(BuilderID);
            if (!Command)
                return;

            // The path is calculated through the regular Move machinery, but
            // ownership of its completion belongs to BuildingSystem.  Keep the
            // rich order aligned with the legacy command so MovementSystem does
            // not finish the approach as an ordinary move before the worker can
            // start construction/morphing.
            Command->CancelAll();
            Order ConstructionOrder;
            ConstructionOrder.Type = OrderType::Build;
            ConstructionOrder.TargetPosition = BuildPosition;
            ConstructionOrder.TargetEntityID = BuildingID;
            ConstructionOrder.PathingIgnoreEntityID = BuildingID;
            Command->QueueOrder(ConstructionOrder);
            if (auto* Path =
                    EM.GetComponent<PathfindingComponent>(BuilderID))
            {
                // The rich Build order keeps the authored footprint as its
                // semantic target. Movement must continue toward the legal
                // edge candidate that was actually searched, not repath to
                // the blocked building center every tick.
                Path->RequestedDestination = ApproachPosition;
                // The structure is static. BuildingSystem owns its live
                // footprint and retry policy; tracking the structure entity
                // here makes MovementSystem replace the legal edge target with
                // the blocked building center on every refresh.
                Path->RequestedTargetEntityID = -1;
                Path->RequestedIgnoreEntityID = BuildingID;
                Path->LastRepathTime = 0.0f;
            }
        };

        std::vector<Core::Math::Vec2> Candidates;
        BuildConstructionApproachCandidates(
            Candidates, EM, BuilderID, BuildingID, BuildPosition, BuildingDefID);

        auto IsCandidateWalkable = [&](const Core::Math::Vec2& Pos) -> bool
        {
            if (!gMap)
                return true;
            const auto& NavGrid = gMap->GetNavigationGrid();
            const int CellX = Game::Pathfinding::NavigationGrid::WorldPixelToCell((int)std::floor(Pos.X));
            const int CellY = Game::Pathfinding::NavigationGrid::WorldPixelToCell((int)std::floor(Pos.Y));
            return NavGrid.IsBaseWalkable(CellX, CellY);
        };

        const size_t CandidateCount = Candidates.size();
        const size_t StartIndex = CandidateCount > 0
            ? (size_t)std::max(0, CandidateOffset) % CandidateCount : 0;
        for (size_t Attempt = 0; Attempt < CandidateCount; ++Attempt)
        {
            const auto& Candidate = Candidates[(StartIndex + Attempt) % CandidateCount];
            if (!IsCandidateWalkable(Candidate))
                continue;
            if (IsRecentFailedMoveTarget(
                    EM.GetComponent<PathfindingComponent>(BuilderID), Candidate))
            {
                continue;
            }
            if (CommandSystem::IssueMoveAndHasPath(
                    EM, BuilderID, Candidate, BuildingID))
            {
                PreserveConstructionOrder(Candidate);
                return;
            }
        }

        const Core::Math::Vec2 FallbackApproach =
            ConstructionApproachPosition(
                EM, BuilderID, BuildingID, BuildPosition, BuildingDefID);
        CommandSystem::IssueMove(
            EM,
            {BuilderID},
            FallbackApproach,
            BuildingID);
        PreserveConstructionOrder(FallbackApproach);
    }

    float DistanceToConstructionFootprint(Core::ECS::EntityManager& EM,
                                          Core::Math::Vec2 WorkerPosition,
                                          int BuildingID,
                                          Core::Math::Vec2 BuildingPosition,
                                          int BuildingDefID)
    {
        const auto* BuildingPath = EM.GetComponent<PathfindingComponent>(BuildingID);
        float Left = 48.0f;
        float Right = 48.0f;
        float Up = 32.0f;
        float Down = 32.0f;
        if (BuildingPath && BuildingPath->HasCollisionBounds)
        {
            Left = (float)BuildingPath->CollisionLeft;
            Right = (float)BuildingPath->CollisionRight;
            Up = (float)BuildingPath->CollisionUp;
            Down = (float)BuildingPath->CollisionDown;
        }
        else
        {
            const auto Footprint = BuildingFootprintSizeForDef(BuildingDefID);
            Left = Right = (float)Footprint.W * 16.0f;
            Up = Down = (float)Footprint.H * 16.0f;
        }

        const Core::Math::Vec2 Delta = WorkerPosition - BuildingPosition;
        const float NearestX = std::clamp(Delta.X, -Left, Right);
        const float NearestY = std::clamp(Delta.Y, -Up, Down);
        const Core::Math::Vec2 Nearest{
            BuildingPosition.X + NearestX,
            BuildingPosition.Y + NearestY
        };
        return WorkerPosition.DistanceTo(Nearest);
    }

    void ApplyUnitDefinition(UnitComponent& Unit,
                             const Races::UnitDef& Def,
                             bool UnderConstruction)
    {
        Unit.ChkUnitID = Def.ChkUnitID;
        // Brood War creates an incomplete building at one tenth of its final
        // vitality, then grows the remaining nine tenths over the authored
        // construction time. This matters especially for a Drone that has
        // just been consumed: the new Zerg structure must be targetable, but
        // must not begin as an artificial one-hit building.
        Unit.HitPoints = UnderConstruction
            ? std::max(1, Def.HitPoints / 10)
            : Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.UnitRace = Def.RaceType;
        Unit.SizeType = Def.SizeType;
        Unit.ShieldPoints = UnderConstruction
            ? Def.ShieldPoints / 10
            : Def.ShieldPoints;
        Unit.MaxShieldPoints = Def.ShieldPoints;
        Unit.EnergyPoints = UnderConstruction ? 0 : Def.EnergyPoints;
        Unit.MaxEnergyPoints = Def.EnergyPoints;
        Unit.Armor = Def.Armor;
        Unit.SightRange = Def.SightRange;
        Unit.SupplyCost = Def.SupplyCost;
        Unit.IsDetector = !UnderConstruction && Races::IsDetectorChkUnitID(Unit.ChkUnitID);
    }

    int ResolveDefID(const Core::ECS::EntityManager& EM, int EntityID)
    {
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
        if (const auto* Building = MutableEM.GetComponent<BuildingComponent>(EntityID))
        {
            if (Building->BuildingDefID >= 0)
                return Building->BuildingDefID;
        }
        if (const auto* Unit = MutableEM.GetComponent<UnitComponent>(EntityID))
            return Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
    }

    int NormalizeRequiredDefIDForRace(int RequiredDefID, Race RaceType)
    {
        const auto Candidate = Races::GetUnitDefByID(RequiredDefID);
        if (Candidate.Name != "Unknown" && Candidate.RaceType == RaceType)
            return RequiredDefID;

        if (RaceType == Race::Zerg)
            return (int)Races::GetTerranDefs().size() + RequiredDefID;
        if (RaceType == Race::Protoss)
            return (int)Races::GetTerranDefs().size() +
                   (int)Races::GetZergDefs().size() +
                   RequiredDefID;
        return RequiredDefID;
    }

    bool HasCompletedBuilding(const Core::ECS::EntityManager& EM,
                              int PlayerID,
                              int RequiredDefID,
                              Race RequiredRace)
    {
        const int NormalizedRequiredDefID =
            NormalizeRequiredDefIDForRace(RequiredDefID, RequiredRace);
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
        return MutableEM.AnyEntityWith<BuildingComponent, PlayerComponent>(
            [&](int EntityID)
            {
                auto* Building = MutableEM.GetComponent<BuildingComponent>(EntityID);
                auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
                if (!Building || !Player || Player->PlayerID != PlayerID || Building->UnderConstruction)
                    return false;
                return Races::BuildingDefSatisfiesRequirement(
                    ResolveDefID(EM, EntityID), NormalizedRequiredDefID);
            });
    }

    bool IsBuilderAssignedToActiveConstruction(Core::ECS::EntityManager& EM, int BuilderID)
    {
        for (const auto& Order : ResolveOrderStore(EM).PendingBuildOrders)
            if (Order.Active && Order.BuilderID == BuilderID)
                return true;
        const auto& Buildings = EM.GetEntitiesWithCached<BuildingComponent, UnitComponent>();
        for (int EntityID : Buildings)
        {
            auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
            auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            if (!Building || !Unit)
                continue;
            if (!Building->UnderConstruction || !Building->HasBuilderUnit(BuilderID))
                continue;
            if (Unit->HitPoints > 0)
                return true;
        }
        return false;
    }

    bool IsGasBuildingDefID(int BuildingDefID)
    {
        return BuildingDefID == Game::Races::GetUnitDefID("Refinery") ||
               BuildingDefID == Game::Races::GetUnitDefID("Extractor") ||
               BuildingDefID == Game::Races::GetUnitDefID("Assimilator");
    }

    const Game::Map::GameMap* ResolveBuildingMap(const Core::ECS::EntityManager& EM)
    {
        if (const auto* Ctx = Game::SimulationContext::TryGet(EM))
        {
            if (Ctx->Map)
                return Ctx->Map;
        }
        return gMap;
    }

    bool InternalHasPylonPowerAtPosition(const Core::ECS::EntityManager& EM,
                                         Core::Math::Vec2 Position,
                                         int PlayerID)
    {
        const Game::Map::GameMap* ActiveMap = ResolveBuildingMap(EM);
        if (!ActiveMap)
            return false;

        int TargetTileX = 0;
        int TargetTileY = 0;
        ActiveMap->WorldToTile(Position, TargetTileX, TargetTileY);

        const int PylonDefID = Game::Races::GetUnitDefID("Pylon");
        const int PylonChkID = Game::Races::GetUnitDefByID(PylonDefID).ChkUnitID;
        bool Powered = false;
        EM.ForEachComponent<TransformComponent, BuildingComponent, UnitComponent, PlayerComponent>(
            [&](Core::ECS::EntityID,
                const TransformComponent& Transform,
                const BuildingComponent& Building,
                const UnitComponent& Unit,
                const PlayerComponent& Player)
            {
                if (Powered)
                    return;
                if (Player.PlayerID != PlayerID || Building.UnderConstruction || Unit.HitPoints <= 0)
                    return;
                if (Unit.ChkUnitID != PylonChkID)
                    return;

                int PylonTileX = 0;
                int PylonTileY = 0;
                ActiveMap->WorldToTile(Transform.Position, PylonTileX, PylonTileY);
                const int Dx = TargetTileX - PylonTileX;
                const int Dy = TargetTileY - PylonTileY;
                if (Game::ProtossPower::ContainsTileOffset(Dx, Dy))
                    Powered = true;
            });

        return Powered;
    }

    bool IsFootprintPlacementValid(Core::ECS::EntityManager& EM,
                                   int BuildingDefID,
                                   Core::Math::Vec2 Position,
                                   int IgnoreEntityID = -1,
                                   int BuilderPlayerID = -1,
                                   const std::vector<int>* AdditionalIgnoredIDs = nullptr)
    {
        if (!gMap)
            return false;

        const TileRect CandidateRect =
            BuildingTileRect(*gMap, Position, BuildingFootprintSizeForDef(BuildingDefID));

        for (int TY = CandidateRect.Top; TY <= CandidateRect.Bottom; ++TY)
        {
            for (int TX = CandidateRect.Left; TX <= CandidateRect.Right; ++TX)
            {
                if (!gMap->IsInBounds(TX, TY) || !gMap->IsBuildable(TX, TY))
                    return false;
                if (gFogExploredChecker && BuilderPlayerID >= 0 &&
                    !gFogExploredChecker(TX, TY, BuilderPlayerID))
                {
                    return false;
                }
            }
        }

        bool PlacementBlocked = false;
        EM.ForEachEntityWith<TransformComponent, UnitComponent>(
            [&](int EntityID)
            {
                if (PlacementBlocked)
                    return;
                if (EntityID == IgnoreEntityID)
                    return;
                if (AdditionalIgnoredIDs &&
                    std::find(AdditionalIgnoredIDs->begin(), AdditionalIgnoredIDs->end(), EntityID) !=
                        AdditionalIgnoredIDs->end())
                {
                    return;
                }

                auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
                auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
                if (!Transform || !Unit || Unit->HitPoints <= 0)
                    return;

                constexpr float kMaxCheckDist = 256.0f;
                if (std::abs(Transform->Position.X - Position.X) > kMaxCheckDist ||
                    std::abs(Transform->Position.Y - Position.Y) > kMaxCheckDist)
                {
                    return;
                }

                auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
                if (Building && Building->IsLifted &&
                    Building->FlightPhase != BuildingFlightPhase::Landing)
                    return;
                if (Building && Building->IsAddon)
                {
                    const int ExistingAddonDefID = ResolveDefID(EM, EntityID);
                    auto* ExistingPlayer = EM.GetComponent<PlayerComponent>(EntityID);
                    if (ExistingAddonDefID >= 0 && ExistingPlayer)
                    {
                        const auto ExistingAddonDef = Races::GetUnitDefByID(ExistingAddonDefID);
                        const Core::Math::Vec2 AddonOffset = gAddonOffsetResolver
                            ? gAddonOffsetResolver(BuildingDefID, ExistingAddonDefID)
                            : Core::Math::Vec2{96.0f, 16.0f};
                        const Core::Math::Vec2 ExpectedAddonPosition = Position + AddonOffset;
                        if ((ExistingPlayer->PlayerID == 11 ||
                             ExistingPlayer->PlayerID == BuilderPlayerID) &&
                            (Building->BuilderUnitID < 0 ||
                             !EM.IsAlive(Building->BuilderUnitID)) &&
                            ExistingAddonDef.AddonOnBuildingDefID == BuildingDefID &&
                            (Transform->Position - ExpectedAddonPosition).LengthSq() <= 16.0f * 16.0f)
                        {
                            return;
                        }
                    }
                }
                if (!Building && Game::Data::IsAirUnitType(Unit->ChkUnitID))
                    return;

                TileRect ExistingRect{};
                bool HasExistingRect = false;
                const int ExistingDefID = ResolveDefID(EM, EntityID);
                if (Building && ExistingDefID >= 0)
                {
                    ExistingRect = BuildingTileRect(
                        *gMap, Transform->Position, BuildingFootprintSizeForDef(ExistingDefID));
                    HasExistingRect = true;
                }
                else if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
                         Path && Path->HasCollisionBounds)
                {
                    ExistingRect = BoundsTileRect(
                        *gMap,
                        Transform->Position,
                        (float)Path->CollisionLeft,
                        (float)Path->CollisionUp,
                        (float)Path->CollisionRight + 1.0f,
                        (float)Path->CollisionDown + 1.0f);
                    HasExistingRect = true;
                }
                else if (EM.GetComponent<ResourceComponent>(EntityID))
                {
                    ExistingRect = BoundsTileRect(*gMap, Transform->Position, 32.0f, 20.0f, 32.0f, 20.0f);
                    HasExistingRect = true;
                }
                else
                {
                    const float Radius = Unit->SizeType == UnitSize::Large
                        ? 20.0f
                        : (Unit->SizeType == UnitSize::Small ? 12.0f : 16.0f);
                    ExistingRect = BoundsTileRect(*gMap, Transform->Position, Radius, Radius, Radius, Radius);
                    HasExistingRect = true;
                }

                if (HasExistingRect && RectsOverlap(CandidateRect, ExistingRect))
                    PlacementBlocked = true;
            });

        if (PlacementBlocked)
            return false;

        return true;
    }

    Core::Math::Vec2 ComputeAddonPosition(const Core::ECS::EntityManager& EM,
                                          int ParentBuildingID,
                                          int AddonDefID)
    {
        auto* Transform =
            const_cast<Core::ECS::EntityManager&>(EM).GetComponent<TransformComponent>(ParentBuildingID);
        if (!Transform)
            return {};
        Core::Math::Vec2 Position = Transform->Position;
        const int ParentDefID = ResolveDefID(EM, ParentBuildingID);
        const Core::Math::Vec2 Offset = gAddonOffsetResolver
            ? gAddonOffsetResolver(ParentDefID, AddonDefID)
            : Core::Math::Vec2{96.0f, 16.0f};
        Position += Offset;
        return Position;
    }

    bool IsCompatibleAddon(const Core::ECS::EntityManager& EM,
                           int ParentBuildingID,
                           int AddonID)
    {
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
        const auto* Parent = MutableEM.GetComponent<BuildingComponent>(ParentBuildingID);
        const auto* Addon = MutableEM.GetComponent<BuildingComponent>(AddonID);
        if (!Parent || !Addon || Parent->IsAddon || !Addon->IsAddon || Addon->UnderConstruction)
            return false;

        const int ParentDefID = ResolveDefID(EM, ParentBuildingID);
        const int AddonDefID = ResolveDefID(EM, AddonID);
        if (ParentDefID < 0 || AddonDefID < 0)
            return false;
        return Races::GetUnitDefByID(AddonDefID).AddonOnBuildingDefID == ParentDefID;
    }

    int FindCompatibleAddonAtLanding(Core::ECS::EntityManager& EM,
                                     int ParentBuildingID,
                                     int RequiredOwner)
    {
        int BestAddonID = -1;
        float BestDistanceSq = 16.0f * 16.0f;
        for (int AddonID : EM.GetEntitiesWithCached<TransformComponent, BuildingComponent, PlayerComponent>())
        {
            if (AddonID == ParentBuildingID || !IsCompatibleAddon(EM, ParentBuildingID, AddonID))
                continue;
            auto* Addon = EM.GetComponent<BuildingComponent>(AddonID);
            auto* AddonPlayer = EM.GetComponent<PlayerComponent>(AddonID);
            auto* AddonTransform = EM.GetComponent<TransformComponent>(AddonID);
            if (!Addon || !AddonPlayer || !AddonTransform ||
                (AddonPlayer->PlayerID != RequiredOwner && AddonPlayer->PlayerID != 11) ||
                (Addon->BuilderUnitID >= 0 && EM.IsAlive(Addon->BuilderUnitID)))
            {
                continue;
            }
            const Core::Math::Vec2 ExpectedPosition =
                ComputeAddonPosition(EM, ParentBuildingID, ResolveDefID(EM, AddonID));
            const float DistanceSq = (AddonTransform->Position - ExpectedPosition).LengthSq();
            if (DistanceSq <= BestDistanceSq)
            {
                BestDistanceSq = DistanceSq;
                BestAddonID = AddonID;
            }
        }
        return BestAddonID;
    }

    void DetachAddon(Core::ECS::EntityManager& EM, int ParentBuildingID)
    {
        auto* Parent = EM.GetComponent<BuildingComponent>(ParentBuildingID);
        if (!Parent || Parent->AddonID < 0)
            return;

        const int AddonID = Parent->AddonID;
        Parent->AddonID = -1;
        if (!EM.IsAlive(AddonID))
            return;
        SetAddonConnectorVisual(EM, AddonID, false);
        auto* Addon = EM.GetComponent<BuildingComponent>(AddonID);
        if (!Addon || !Addon->IsAddon)
            return;

        Addon->BuilderUnitID = -1;
        Addon->FlightPhase = BuildingFlightPhase::LiftingOff;
        Addon->FlightAnimationComplete = false;
        // A detached add-on remains owned by its player. Reassigning it to a
        // neutral/hostile slot makes friendly units attack it and prevents a
        // compatible parent from reconnecting after landing.
    }

    void ConnectAddon(Core::ECS::EntityManager& EM,
                      int ParentBuildingID,
                      int AddonID)
    {
        auto* Parent = EM.GetComponent<BuildingComponent>(ParentBuildingID);
        auto* Addon = EM.GetComponent<BuildingComponent>(AddonID);
        auto* ParentPlayer = EM.GetComponent<PlayerComponent>(ParentBuildingID);
        auto* AddonPlayer = EM.GetComponent<PlayerComponent>(AddonID);
        if (!Parent || !Addon || !ParentPlayer || !AddonPlayer)
            return;

        Parent->AddonID = AddonID;
        Addon->BuilderUnitID = ParentBuildingID;
        AddonPlayer->PlayerID = ParentPlayer->PlayerID;
        Addon->FlightPhase = BuildingFlightPhase::Landing;
        Addon->FlightAnimationComplete = false;
        SetAddonConnectorVisual(EM, AddonID, !Addon->UnderConstruction);
    }

    Core::Math::Vec2 ResolveBuildPosition(const Core::ECS::EntityManager& EM,
                                          int BuilderID,
                                          int BuildingDefID,
                                          const BuildTarget& Target)
    {
        auto Def = Races::GetUnitDefByID(BuildingDefID);
        if (Def.IsAddon)
            return ComputeAddonPosition(EM, BuilderID, BuildingDefID);
        if (Target.Type == BuildTargetType::TargetEntity)
        {
            if (auto* Transform =
                    const_cast<Core::ECS::EntityManager&>(EM).GetComponent<TransformComponent>(Target.TargetEntityID))
            {
                return Transform->Position;
            }
        }
        return Target.Position;
    }

    int CreateBuildingEntity(Core::ECS::EntityManager& EM,
                             int BuildingDefID,
                             Core::Math::Vec2 Position,
                             int PlayerID,
                             bool UnderConstruction,
                             int BuilderID,
                             int ParentBuildingID)
    {
        auto Def = Races::GetUnitDefByID(BuildingDefID);
        int EntityID = EM.CreateEntity();

        auto& Transform = EM.AddComponent<TransformComponent>(EntityID);
        Transform.Position = Position;

        auto& Render = EM.AddComponent<RenderComponent>(EntityID);
        Render.ImageID = UnderConstruction
            ? ResolveConstructionImageID(Def.ChkUnitID)
            : ResolveImageID(Def.ChkUnitID);
        Render.SubunitImageID = UnderConstruction
            ? std::numeric_limits<uint32_t>::max()
            : ResolveSubunitImageID(Def.ChkUnitID);
        Render.FrameIndex = 0;
        Render.TotalFrames = 1;
        Render.Layer = RenderLayer::Buildings;
        // Construction GRPs already encode their skeletal/warp transparency.
        // Applying entity-wide alpha makes the whole building look ghosted.
        Render.Tint = Core::Colors::White();

        auto& Building = EM.AddComponent<BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.UnderConstruction = UnderConstruction;
        Building.BuilderUnitID = Def.IsAddon && ParentBuildingID > 0
            ? ParentBuildingID
            : BuilderID;
        Building.BuildingDefID = BuildingDefID;
        Building.TotalBuildTime = (float)Def.BuildTime;
        Building.CanLiftOff = Def.CanLiftOff;
        Building.IsAddon = Def.IsAddon;
        Building.IsOrganic = Def.RaceType == Components::Race::Zerg;
        Building.RequiresCreep = Def.RequiresCreep;
        Building.RequiresPylon = Def.RequiresPylon;
        Building.HasPylonPower = !Def.RequiresPylon ||
            InternalHasPylonPowerAtPosition(EM, Position, PlayerID);
        Building.BuildProgress = UnderConstruction ? 0.0f : (float)Def.BuildTime;

        auto& Unit = EM.AddComponent<UnitComponent>(EntityID);
        ApplyUnitDefinition(Unit, Def, UnderConstruction);
        if (UnderConstruction && Unit.UnitRace == Components::Race::Protoss)
        {
            Building.ProtossPhase = ProtossConstructionPhase::Rift;
            Render.ImageID = ProtossWarpAnchorImageID;
        }

        auto& Player = EM.AddComponent<PlayerComponent>(EntityID);
        Player.PlayerID = PlayerID;

        auto& Path = EM.AddComponent<PathfindingComponent>(EntityID);
        UnitCollisionBounds Bounds = ResolveCollisionBounds(Def.ChkUnitID);
        if (!Bounds.IsValid)
        {
            const auto Footprint = BuildingFootprintSizeForChk(Def.ChkUnitID);
            Bounds.IsValid = true;
            Bounds.Left = Footprint.W * 16 - 1;
            Bounds.Right = Footprint.W * 16 - 1;
            Bounds.Up = Footprint.H * 16 - 1;
            Bounds.Down = Footprint.H * 16 - 1;
        }
        ApplyCollisionBounds(Path, Bounds);

        if (!UnderConstruction && (Def.GroundDamage > 0 || Def.AirDamage > 0))
        {
            auto& Combat = EM.AddComponent<CombatComponent>(EntityID);
            Races::ApplyCombatDefinition(
                Combat, Def, Game::Data::IsAirUnitType(Unit.ChkUnitID));
        }

        if (Def.ChkUnitID == 125)
        {
            EM.AddComponent<LoadedUnitsComponent>(EntityID);
        }

        if (UnderConstruction)
        {
            auto& Command = EM.AddComponent<CommandComponent>(EntityID);
            Command.CurrentCommand = CommandType::Build;
        }

        if (ParentBuildingID > 0 && EM.IsAlive(ParentBuildingID))
        {
            if (auto* Parent = EM.GetComponent<BuildingComponent>(ParentBuildingID))
            {
                Parent->AddonID = EntityID;
                SetAddonConnectorVisual(EM, EntityID, Def.IsAddon && !UnderConstruction);
            }
        }

        return EntityID;
    }

    int ClaimRefineryTarget(Core::ECS::EntityManager& EM,
                            int BuilderID,
                            int BuildingDefID,
                            int TargetEntityID,
                            int PlayerID)
    {
        auto* Transform = EM.GetComponent<TransformComponent>(TargetEntityID);
        auto* Render = EM.GetComponent<RenderComponent>(TargetEntityID);
        auto* Building = EM.GetComponent<BuildingComponent>(TargetEntityID);
        auto* Unit = EM.GetComponent<UnitComponent>(TargetEntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(TargetEntityID);
        auto* Resource = EM.GetComponent<ResourceComponent>(TargetEntityID);
        if (!Transform || !Render || !Building || !Unit || !Player || !Resource)
            return -1;

        auto* OriginalPath = EM.GetComponent<PathfindingComponent>(TargetEntityID);
        auto* Snapshot = EM.GetComponent<ClaimedGeyserComponent>(TargetEntityID);
        if (!Snapshot)
            Snapshot = &EM.AddComponent<ClaimedGeyserComponent>(TargetEntityID);
        Snapshot->IsClaimed = true;
        Snapshot->OriginalUnit = *Unit;
        Snapshot->OriginalPlayer = *Player;
        Snapshot->OriginalRender = *Render;
        Snapshot->OriginalBuilding = *Building;
        Snapshot->HadPathfinding = OriginalPath != nullptr;
        if (OriginalPath)
            Snapshot->OriginalPathfinding = *OriginalPath;

        auto Def = Races::GetUnitDefByID(BuildingDefID);
        Render->ImageID = ResolveConstructionImageID(Def.ChkUnitID);
        Render->FrameIndex = 0;
        Render->TotalFrames = 1;
        Render->Layer = RenderLayer::Buildings;
        Render->Tint = Core::Colors::White();

        Building->IsBuilding = true;
        Building->UnderConstruction = true;
        Building->BuilderUnitID = BuilderID;
        Building->BuildingDefID = BuildingDefID;
        Building->TotalBuildTime = (float)Def.BuildTime;
        Building->CanLiftOff = Def.CanLiftOff;
        Building->IsAddon = false;
        Building->RequiresCreep = Def.RequiresCreep;
        Building->RequiresPylon = Def.RequiresPylon;
        Building->HasPylonPower = !Def.RequiresPylon ||
            InternalHasPylonPowerAtPosition(EM, Transform->Position, PlayerID);
        Building->BuildProgress = 0.0f;

        ApplyUnitDefinition(*Unit, Def, true);
        if (Unit->UnitRace == Components::Race::Protoss)
        {
            Building->ProtossPhase = ProtossConstructionPhase::Rift;
            Render->ImageID = ProtossWarpAnchorImageID;
        }
        UnitCollisionBounds RefineryBounds = ResolveCollisionBounds(Def.ChkUnitID);
        if (!RefineryBounds.IsValid)
        {
            const auto Footprint = BuildingFootprintSizeForChk(Def.ChkUnitID);
            RefineryBounds.IsValid = true;
            RefineryBounds.Left = Footprint.W * 16 - 1;
            RefineryBounds.Right = Footprint.W * 16 - 1;
            RefineryBounds.Up = Footprint.H * 16 - 1;
            RefineryBounds.Down = Footprint.H * 16 - 1;
        }
        if (auto* Path = EM.GetComponent<PathfindingComponent>(TargetEntityID))
            ApplyCollisionBounds(*Path, RefineryBounds);
        else
        {
            auto& NewPath = EM.AddComponent<PathfindingComponent>(TargetEntityID);
            ApplyCollisionBounds(NewPath, RefineryBounds);
        }

        Player->PlayerID = PlayerID;
        if (auto* ExistingCommand = EM.GetComponent<CommandComponent>(TargetEntityID))
        {
            ExistingCommand->CurrentCommand = CommandType::Build;
        }
        else
        {
            auto& Command = EM.AddComponent<CommandComponent>(TargetEntityID);
            Command.CurrentCommand = CommandType::Build;
        }

        Resource->Type = ResourceType::VespeneGas;
        return TargetEntityID;
    }
}

const Game::Map::GameMap* BuildingSystem::sMap = nullptr;

void BuildingSystem::SetMap(const Game::Map::GameMap* Map)
{
    sMap = Map;
    gMap = Map;
}

void BuildingSystem::SetFogExploredChecker(FogExploredChecker Checker)
{
    gFogExploredChecker = std::move(Checker);
}

void BuildingSystem::ResetPendingBuildOrders(Core::ECS::EntityManager& EM)
{
    EM.SetResource<BuildingOrderStore>();
    EM.ActivateWorld();
}

void BuildingSystem::ResetPendingBuildOrders()
{
    gFallbackBuildingOrders.PendingBuildOrders.clear();
    gFallbackBuildingOrders.UnreachableBuildSites.clear();
    if (auto* ActiveWorld = Core::ECS::EntityManager::GetActiveWorld())
    {
        if (auto* Store = ActiveWorld->GetResource<BuildingOrderStore>())
        {
            Store->PendingBuildOrders.clear();
            Store->UnreachableBuildSites.clear();
        }
    }
}

int BuildingSystem::CountPendingBuilds(const Core::ECS::EntityManager& EM, int PlayerID, int BuildingDefID)
{
    const auto& Orders = ResolveOrderStore(EM).PendingBuildOrders;
    return (int)std::count_if(
        Orders.begin(), Orders.end(),
        [PlayerID, BuildingDefID](const BuildOrder& Order)
        {
            return Order.Active && Order.PlayerID == PlayerID &&
                   Order.BuildingDefID == BuildingDefID;
        });
}

int BuildingSystem::CountPendingBuilds(int PlayerID, int BuildingDefID)
{
    const auto& Orders = ResolveActiveOrFallbackOrderStore().PendingBuildOrders;
    return (int)std::count_if(
        Orders.begin(), Orders.end(),
        [PlayerID, BuildingDefID](const BuildOrder& Order)
        {
            return Order.Active && Order.PlayerID == PlayerID &&
                   Order.BuildingDefID == BuildingDefID;
        });
}

bool BuildingSystem::WasRecentlyUnreachable(
    const Core::ECS::EntityManager& EM, int PlayerID, int BuildingDefID, const Core::Math::Vec2& Position)
{
    constexpr float SameSiteRadius = 48.0f;
    const auto& Sites = ResolveOrderStore(EM).UnreachableBuildSites;
    return std::any_of(
        Sites.begin(), Sites.end(),
        [PlayerID, BuildingDefID, &Position](const UnreachableBuildSiteEntry& Site)
        {
            return Site.RemainingSeconds > 0.0f &&
                   Site.PlayerID == PlayerID &&
                   Site.BuildingDefID == BuildingDefID &&
                   Site.Position.DistanceTo(Position) <= SameSiteRadius;
        });
}

bool BuildingSystem::WasRecentlyUnreachable(
    int PlayerID, int BuildingDefID, const Core::Math::Vec2& Position)
{
    constexpr float SameSiteRadius = 48.0f;
    const auto& Sites = ResolveActiveOrFallbackOrderStore().UnreachableBuildSites;
    return std::any_of(
        Sites.begin(), Sites.end(),
        [PlayerID, BuildingDefID, &Position](const UnreachableBuildSiteEntry& Site)
        {
            return Site.RemainingSeconds > 0.0f &&
                   Site.PlayerID == PlayerID &&
                   Site.BuildingDefID == BuildingDefID &&
                   Site.Position.DistanceTo(Position) <= SameSiteRadius;
        });
}

bool BuildingSystem::IsBuilderCommitted(Core::ECS::EntityManager& EM, int BuilderID)
{
    return IsBuilderAssignedToActiveConstruction(EM, BuilderID);
}

void BuildingSystem::SetImageResolver(ImageIDResolver Resolver)
{
    gImageResolver = std::move(Resolver);
}

void BuildingSystem::SetSubunitImageResolver(ImageIDResolver Resolver)
{
    gSubunitImageResolver = std::move(Resolver);
}

void BuildingSystem::SetConstructionImageResolver(ImageIDResolver Resolver)
{
    gConstructionImageResolver = std::move(Resolver);
}

uint32_t BuildingSystem::ResolveConstructionImageID(int ChkUnitID)
{
    return Game::Systems::ResolveConstructionImageID(ChkUnitID);
}

void BuildingSystem::SetCollisionBoundsResolver(CollisionBoundsResolver Resolver)
{
    gCollisionBoundsResolver = std::move(Resolver);
}

void BuildingSystem::SetAddonOffsetResolver(AddonOffsetResolver Resolver)
{
    gAddonOffsetResolver = std::move(Resolver);
}

bool BuildingSystem::HasPylonPowerAtPosition(const Core::ECS::EntityManager& EM,
                                             Core::Math::Vec2 Position,
                                             int PlayerID)
{
    return InternalHasPylonPowerAtPosition(EM, Position, PlayerID);
}

bool BuildingSystem::IsPlacementValid(Core::ECS::EntityManager& EM,
                                      int BuildingDefID,
                                      Core::Math::Vec2 Position,
                                      int BuilderPlayerID)
{
    if (!gMap)
        return false;

    auto Def = Races::GetUnitDefByID(BuildingDefID);
    if (!Def.IsBuilding)
        return false;

    auto& Resources = HarvestSystem::GetResources(EM, BuilderPlayerID);
    if (Resources.Minerals < Def.MineralCost || Def.GasCost > Resources.VespeneGas)
        return false;
    if (Def.RequiresPylon && !InternalHasPylonPowerAtPosition(EM, Position, BuilderPlayerID))
        return false;
    if (Def.RequiresCreep && !HasCreepAcrossFootprint(BuildingDefID, Position))
        return false;
    if (ConflictsWithPendingBuild(
            BuildingDefID, BuildTarget::FromPosition(Position)))
        return false;
    return IsFootprintPlacementValid(EM, BuildingDefID, Position, -1, BuilderPlayerID);
}

bool BuildingSystem::CanBuild(const Core::ECS::EntityManager& EM,
                              int BuilderID,
                              int BuildingDefID,
                              const BuildTarget& Target,
                              std::string* OutReason,
                              bool CheckPlacement)
{
    auto Def = Races::GetUnitDefByID(BuildingDefID);
    if (!Def.IsBuilding)
    {
        SetReason(OutReason, "target definition is not a building");
        return false;
    }

    auto* BuilderTransform =
        const_cast<Core::ECS::EntityManager&>(EM).GetComponent<TransformComponent>(BuilderID);
    if (!BuilderTransform)
    {
        SetReason(OutReason, "builder has no transform");
        return false;
    }

    auto* BuilderUnit =
        const_cast<Core::ECS::EntityManager&>(EM).GetComponent<UnitComponent>(BuilderID);
    if (!BuilderUnit || BuilderUnit->HitPoints <= 0)
    {
        SetReason(OutReason, "builder is not alive");
        return false;
    }

    auto* BuilderPlayer =
        const_cast<Core::ECS::EntityManager&>(EM).GetComponent<PlayerComponent>(BuilderID);
    int PlayerID = BuilderPlayer ? BuilderPlayer->PlayerID : 0;
    auto& Resources = HarvestSystem::GetResources(EM, PlayerID);
    if (Resources.Minerals < Def.MineralCost || Resources.VespeneGas < Def.GasCost)
    {
        SetReason(OutReason, "not enough resources");
        return false;
    }

    int BuilderDefID = ResolveDefID(EM, BuilderID);
    auto* BuilderBuilding =
        const_cast<Core::ECS::EntityManager&>(EM).GetComponent<BuildingComponent>(BuilderID);
    if (Def.IsAddon)
    {
        if (!BuilderBuilding || BuilderBuilding->UnderConstruction)
        {
            SetReason(OutReason, "add-on parent is not ready");
            return false;
        }
        if (BuilderBuilding->IsLifted)
        {
            SetReason(OutReason, "add-on parent is lifted");
            return false;
        }
        if (BuilderDefID != Def.AddonOnBuildingDefID)
        {
            SetReason(OutReason, "wrong parent building for add-on");
            return false;
        }
        if (BuilderBuilding->AddonID >= 0)
        {
            auto* AddonUnit =
                const_cast<Core::ECS::EntityManager&>(EM).GetComponent<UnitComponent>(BuilderBuilding->AddonID);
            if (EM.IsAlive(BuilderBuilding->AddonID) && AddonUnit && AddonUnit->HitPoints > 0)
            {
                SetReason(OutReason, "parent already has an add-on");
                return false;
            }
            else
            {
                BuilderBuilding->AddonID = -1;
            }
        }
        if (CheckPlacement)
        {
            const Core::Math::Vec2 AddonPosition = ComputeAddonPosition(EM, BuilderID, BuildingDefID);
            if (ConflictsWithPendingBuild(BuildingDefID, BuildTarget::FromPosition(AddonPosition), BuilderID))
            {
                SetReason(OutReason, "placement reserved by pending build");
                return false;
            }
            if (!IsFootprintPlacementValid(
                    const_cast<Core::ECS::EntityManager&>(EM), BuildingDefID, AddonPosition, BuilderID, PlayerID))
            {
                SetReason(OutReason, "invalid placement");
                return false;
            }
        }
    }
    else if (!Def.IsAddon &&
             IsBuilderAssignedToActiveConstruction(
                 const_cast<Core::ECS::EntityManager&>(EM), BuilderID))
    {
        SetReason(OutReason, "builder is already assigned to another building");
        return false;
    }
    else if (Def.RaceType == Race::Terran && BuilderDefID != Races::GetUnitDefID("SCV"))
    {
        SetReason(OutReason, "only SCV can place Terran buildings");
        return false;
    }
    else if (Def.RaceType == Race::Zerg && BuilderDefID != Races::GetUnitDefID("Drone"))
    {
        SetReason(OutReason, "only Drone can place Zerg buildings");
        return false;
    }
    else if (Def.RaceType == Race::Protoss && BuilderDefID != Races::GetUnitDefID("Probe"))
    {
        SetReason(OutReason, "only Probe can place Protoss buildings");
        return false;
    }

    for (int RequiredDefID : Def.RequiredBuildings)
    {
        if (!HasCompletedBuilding(EM, PlayerID, RequiredDefID, Def.RaceType))
        {
            SetReason(OutReason, "required building missing");
            return false;
        }
    }

    if (!Def.IsAddon && IsGasBuildingDefID(BuildingDefID))
    {
        if (!CheckPlacement)
            return true;
        if (Target.Type != BuildTargetType::TargetEntity)
        {
            SetReason(OutReason, "refinery must target a geyser");
            return false;
        }

        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
        auto* TargetResource = MutableEM.GetComponent<ResourceComponent>(Target.TargetEntityID);
        auto* TargetBuilding = MutableEM.GetComponent<BuildingComponent>(Target.TargetEntityID);
        auto* TargetUnit = MutableEM.GetComponent<UnitComponent>(Target.TargetEntityID);
        if (!TargetResource || TargetResource->Type != ResourceType::VespeneGas)
        {
            SetReason(OutReason, "target is not a vespene geyser");
            return false;
        }
        if (!TargetBuilding || !TargetUnit)
        {
            SetReason(OutReason, "target cannot host a refinery");
            return false;
        }

        const int TargetDefID = ResolveDefID(EM, Target.TargetEntityID);
        if (IsGasBuildingDefID(TargetDefID))
        {
            SetReason(OutReason, "geyser already has a gas building");
            return false;
        }
        if (ConflictsWithPendingBuild(BuildingDefID, Target, BuilderID))
        {
            SetReason(OutReason, "placement reserved by pending build");
            return false;
        }
        return true;
    }

    if (!Def.IsAddon && CheckPlacement)
    {
        if (Target.Type != BuildTargetType::WorldPosition)
        {
            SetReason(OutReason, "building requires a map placement");
            return false;
        }
        if (Def.RequiresPylon && !InternalHasPylonPowerAtPosition(EM, Target.Position, PlayerID))
        {
            SetReason(OutReason, "requires Pylon power");
            return false;
        }
        if (Def.RequiresCreep && !HasCreepAcrossFootprint(BuildingDefID, Target.Position))
        {
            SetReason(OutReason, "requires creep across building footprint");
            return false;
        }
        if (ConflictsWithPendingBuild(BuildingDefID, Target, BuilderID))
        {
            SetReason(OutReason, "placement reserved by pending build");
            return false;
        }
        if (!IsFootprintPlacementValid(
                const_cast<Core::ECS::EntityManager&>(EM), BuildingDefID, Target.Position, BuilderID, PlayerID))
        {
            SetReason(OutReason, "invalid placement");
            return false;
        }
    }

    return true;
}

bool BuildingSystem::StartBuild(Core::ECS::EntityManager& EM,
                                int BuilderID,
                                int BuildingDefID,
                                const BuildTarget& Target,
                                const std::vector<int>& AssistantBuilderIDs)
{
    std::string Reason;
    if (!CanBuild(EM, BuilderID, BuildingDefID, Target, &Reason))
    {
        Core::Utils::Logger::Debug("BuildingSystem", "Cannot build: " + Reason);
        return false;
    }

    auto Def = Races::GetUnitDefByID(BuildingDefID);
    const Core::Math::Vec2 BuildPosition = ResolveBuildPosition(EM, BuilderID, BuildingDefID, Target);

    auto* BuilderPlayer = EM.GetComponent<PlayerComponent>(BuilderID);
    int PlayerID = BuilderPlayer ? BuilderPlayer->PlayerID : 0;
    auto& Resources = HarvestSystem::GetResources(EM, PlayerID);
    Resources.Minerals -= Def.MineralCost;
    Resources.VespeneGas -= Def.GasCost;

    if (!Def.IsAddon)
    {
        BuildOrder Order;
        Order.Active = true;
        Order.BuilderID = BuilderID;
        Order.PlayerID = PlayerID;
        Order.BuildingDefID = BuildingDefID;
        Order.Position = BuildPosition;
        Order.Target = Target;
        Order.AssistantBuilderIDs = AssistantBuilderIDs;
        ResolveOrderStore(EM).PendingBuildOrders.push_back(Order);

        const int TargetEntityID = Target.Type == BuildTargetType::TargetEntity
            ? Target.TargetEntityID : -1;
        CommandSystem::InterruptHarvest(EM, BuilderID);
        IssueConstructionMove(
            EM, BuilderID, TargetEntityID, BuildPosition, BuildingDefID);
        if (auto* Command = EM.GetComponent<CommandComponent>(BuilderID))
        {
            Command->CurrentCommand = CommandType::Build;
            Command->TargetEntityID = TargetEntityID;
            Command->TargetPosition = BuildPosition;
        }

        for (size_t I = 0; I < AssistantBuilderIDs.size(); ++I)
        {
            int AsstID = AssistantBuilderIDs[I];
            if (AsstID == BuilderID || !EM.IsAlive(AsstID))
                continue;
            CommandSystem::InterruptHarvest(EM, AsstID);
            IssueConstructionMove(
                EM, AsstID, -1, BuildPosition, BuildingDefID, (int)(I + 1) * 2);
            if (auto* Command = EM.GetComponent<CommandComponent>(AsstID))
            {
                Command->CurrentCommand = CommandType::Build;
                Command->TargetEntityID = -1;
                Command->TargetPosition = BuildPosition;
            }
        }

        Core::Utils::Logger::Info(
            "BuildingSystem",
            std::string(Def.RaceType == Components::Race::Protoss
                            ? "Probe moving to warp "
                            : Def.RaceType == Components::Race::Zerg
                                ? "Drone moving to morph "
                                : "SCV moving to build ") +
            Def.Name + " at (" +
            std::to_string((int)BuildPosition.X) + "," +
            std::to_string((int)BuildPosition.Y) + ")");
        return true;
    }

    const int ParentBuildingID = BuilderID;
    const int EntityID = CreateBuildingEntity(
        EM, BuildingDefID, BuildPosition, PlayerID, true, BuilderID, ParentBuildingID);
    if (EntityID >= 0)
    {
        CommandSystem::InterruptHarvest(EM, BuilderID);
        Core::Utils::Logger::Info(
            "BuildingSystem",
            "Started addon: " + Def.Name + " at (" +
            std::to_string((int)BuildPosition.X) + "," +
            std::to_string((int)BuildPosition.Y) + ")");
        return true;
    }
    return false;
}

bool BuildingSystem::AssistConstruction(Core::ECS::EntityManager& EM,
                                        const std::vector<int>& BuilderIDs,
                                        int BuildingID)
{
    auto* Building = EM.GetComponent<BuildingComponent>(BuildingID);
    auto* BuildingUnit = EM.GetComponent<UnitComponent>(BuildingID);
    auto* BuildingPlayer = EM.GetComponent<PlayerComponent>(BuildingID);
    auto* BuildingTransform = EM.GetComponent<TransformComponent>(BuildingID);
    if (!Building || !BuildingUnit || !BuildingPlayer || !BuildingTransform ||
        !Building->UnderConstruction || Building->IsAddon ||
        BuildingUnit->UnitRace != Components::Race::Terran)
    {
        return false;
    }

    bool AssignedAny = false;
    for (int BuilderID : BuilderIDs)
    {
        auto* BuilderUnit = EM.GetComponent<UnitComponent>(BuilderID);
        auto* BuilderPlayer = EM.GetComponent<PlayerComponent>(BuilderID);
        if (!BuilderUnit || !BuilderPlayer || BuilderUnit->HitPoints <= 0 ||
            BuilderUnit->ChkUnitID != 7 ||
            BuilderPlayer->PlayerID != BuildingPlayer->PlayerID)
        {
            continue;
        }

        if (!Building->HasBuilderUnit(BuilderID))
        {
            if (IsBuilderAssignedToActiveConstruction(EM, BuilderID))
                continue;
            Building->AssistantBuilderUnitIDs.push_back(BuilderID);
        }

        CommandSystem::InterruptHarvest(EM, BuilderID);
        int AssistantIndex = 0;
        for (size_t A = 0; A < Building->AssistantBuilderUnitIDs.size(); ++A)
        {
            if (Building->AssistantBuilderUnitIDs[A] == BuilderID)
            {
                AssistantIndex = (int)A;
                break;
            }
        }
        const int CandidateOffset = (AssistantIndex + 1) * 2;
        IssueConstructionMove(
            EM, BuilderID, BuildingID, BuildingTransform->Position,
            Building->BuildingDefID, CandidateOffset);
        AssignedAny = true;
    }

    return AssignedAny;
}

int BuildingSystem::DebugSpawnCompletedBuilding(Core::ECS::EntityManager& EM,
                                                int BuildingDefID,
                                                Core::Math::Vec2 Position,
                                                int PlayerID,
                                                int ParentBuildingID)
{
    return CreateBuildingEntity(EM, BuildingDefID, Position, PlayerID, false, -1, ParentBuildingID);
}

int BuildingSystem::DebugSpawnBuildingUnderConstruction(Core::ECS::EntityManager& EM,
                                                        int BuildingDefID,
                                                        Core::Math::Vec2 Position,
                                                        int PlayerID,
                                                        int BuilderID,
                                                        int ParentBuildingID)
{
    return CreateBuildingEntity(EM, BuildingDefID, Position, PlayerID, true, BuilderID, ParentBuildingID);
}

void BuildingSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f)
        return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto& UnreachableSites = ResolveOrderStore(EM).UnreachableBuildSites;
    for (auto& Site : UnreachableSites)
        Site.RemainingSeconds -= DeltaTime;
    UnreachableSites.erase(
        std::remove_if(
            UnreachableSites.begin(), UnreachableSites.end(),
            [](const UnreachableBuildSiteEntry& Site)
            {
                return Site.RemainingSeconds <= 0.0f;
            }),
        UnreachableSites.end());
    ProcessPendingBuildOrders(EM, DeltaTime);
    auto Entities = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
    for (auto ID : Entities)
    {
        ProcessFlightTransition(EM, ID);
        AdvanceFlightHeightTransition(EM, ID, DeltaTime);
        ProcessConstruction(EM, ID, DeltaTime);
    }
}

void BuildingSystem::ProcessPendingBuildOrders(Core::ECS::EntityManager& EM,
                                               float DeltaTime)
{
    auto& OrderStore = ResolveOrderStore(EM);
    auto& PendingOrders = OrderStore.PendingBuildOrders;
    auto& UnreachableSites = OrderStore.UnreachableBuildSites;
    for (auto& Order : PendingOrders)
    {
        if (!Order.Active)
            continue;

        const auto Def = Races::GetUnitDefByID(Order.BuildingDefID);
        auto RefundAndCancel = [&](bool ReleaseBuilderState = true)
        {
            if (Order.PlayerID >= 0)
            {
                auto& Resources = HarvestSystem::GetResources(EM, Order.PlayerID);
                Resources.Minerals += Def.MineralCost;
                Resources.VespeneGas += Def.GasCost;
            }
            if (ReleaseBuilderState && EM.IsAlive(Order.BuilderID))
            {
                if (auto* Command = EM.GetComponent<CommandComponent>(Order.BuilderID))
                {
                    Command->CancelAll();
                    Command->TargetEntityID = -1;
                }
                if (auto* Path = EM.GetComponent<PathfindingComponent>(Order.BuilderID))
                {
                    Path->Path.clear();
                    Path->PathIndex = 0;
                    Path->CurrentSpeed = 0.0f;
                    Path->TargetSpeed = 0.0f;
                    Path->CurrentVelocity = {0.0f, 0.0f};
                    Path->RequestedTargetEntityID = -1;
                }
            }
            if (ReleaseBuilderState)
            {
                for (int AsstID : Order.AssistantBuilderIDs)
                {
                    if (EM.IsAlive(AsstID))
                    {
                        if (auto* Command = EM.GetComponent<CommandComponent>(AsstID))
                        {
                            if (Command->CurrentCommand == CommandType::Build)
                            {
                                Command->CancelAll();
                                Command->TargetEntityID = -1;
                            }
                        }
                        if (auto* Path = EM.GetComponent<PathfindingComponent>(AsstID))
                        {
                            Path->Path.clear();
                            Path->PathIndex = 0;
                            Path->CurrentSpeed = 0.0f;
                            Path->TargetSpeed = 0.0f;
                            Path->CurrentVelocity = {0.0f, 0.0f};
                            Path->RequestedTargetEntityID = -1;
                        }
                    }
                }
            }
            Order.Active = false;
        };

        auto* BuilderTransform = EM.GetComponent<TransformComponent>(Order.BuilderID);
        auto* BuilderUnit = EM.GetComponent<UnitComponent>(Order.BuilderID);
        auto* BuilderPlayer = EM.GetComponent<PlayerComponent>(Order.BuilderID);
        if (!EM.IsAlive(Order.BuilderID) || !BuilderTransform || !BuilderUnit ||
            BuilderUnit->HitPoints <= 0)
        {
            Core::Utils::Logger::Info(
                "BuildingSystem", "Cancelled pending " + Def.Name +
                ": builder unavailable");
            RefundAndCancel();
            continue;
        }

        if (!BuilderPlayer || BuilderPlayer->PlayerID != Order.PlayerID)
        {
            Core::Utils::Logger::Info(
                "BuildingSystem", "Cancelled pending " + Def.Name +
                ": builder ownership changed");
            const auto* BuilderCommand =
                EM.GetComponent<CommandComponent>(Order.BuilderID);
            // Clear only the stale construction approach. If the ownership
            // transfer already installed a newer command, preserve it.
            RefundAndCancel(
                !BuilderCommand ||
                BuilderCommand->CurrentCommand == CommandType::Build);
            continue;
        }

        if (const auto* BuilderCommand =
                EM.GetComponent<CommandComponent>(Order.BuilderID);
            BuilderCommand &&
            BuilderCommand->CurrentCommand != CommandType::Build)
        {
            Core::Utils::Logger::Info(
                "BuildingSystem", "Cancelled pending " + Def.Name +
                ": builder received a replacement command");
            // Preserve the user's newer Move/Stop/Harvest order and path.
            RefundAndCancel(false);
            continue;
        }

        const int TargetEntityID = Order.Target.Type == BuildTargetType::TargetEntity
            ? Order.Target.TargetEntityID : -1;
        const float Distance = DistanceToConstructionFootprint(
            EM, BuilderTransform->Position, TargetEntityID, Order.Position,
            Order.BuildingDefID);
        const float BuilderRadius = CollisionRadiusFromBounds(
            EM.GetComponent<PathfindingComponent>(Order.BuilderID), 16.0f);
        const float MaxApproachDistance = IsGasBuildingDefID(Order.BuildingDefID)
            ? BuilderRadius + 12.0f
            : BuilderRadius + 18.0f;
        if (Distance > MaxApproachDistance)
        {
            Order.ApproachSeconds += DeltaTime;
            Order.TotalApproachSeconds += DeltaTime;

            auto* Path = EM.GetComponent<PathfindingComponent>(Order.BuilderID);
            const bool IsActuallyMoving = Path &&
                (Path->CurrentSpeed > 0.1f || Path->CurrentVelocity.LengthSq() > 0.01f);

            if (!Order.HasBestFootprintDistance ||
                Distance < Order.BestFootprintDistance - 2.0f ||
                IsActuallyMoving)
            {
                if (Distance < Order.BestFootprintDistance || !Order.HasBestFootprintDistance)
                    Order.BestFootprintDistance = Distance;
                Order.HasBestFootprintDistance = true;
                Order.NoProgressSeconds = 0.0f;
                Order.ConsecutiveApproachFailures = 0;
            }
            else
            {
                Order.NoProgressSeconds += DeltaTime;
            }

            const bool MoveSettled = IsMoveSettled(Path);
            const bool SettledRetryReady =
                MoveSettled && Order.ApproachSeconds >= 2.0f;
            const bool MadeNoProgress = Order.NoProgressSeconds >= 4.0f;
            const bool RefreshApproach = Order.ApproachSeconds >= 10.0f;
            if (SettledRetryReady || MadeNoProgress || RefreshApproach)
            {
                ++Order.ApproachAttempt;
                if (SettledRetryReady || MadeNoProgress)
                    ++Order.ConsecutiveApproachFailures;

                if (Order.ConsecutiveApproachFailures >=
                        MaxPendingBuildApproachFailures ||
                    Order.TotalApproachSeconds >=
                        MaxPendingBuildApproachSeconds)
                {
                    UnreachableSites.push_back({
                        Order.PlayerID,
                        Order.BuildingDefID,
                        Order.Position,
                        UnreachableBuildSiteCooldownSeconds});
                    if (auto* FailedPath =
                            EM.GetComponent<PathfindingComponent>(Order.BuilderID))
                    {
                        FailedPath->HasRecentMoveFailure = true;
                        FailedPath->RecentFailedMoveTarget = Order.Position;
                        FailedPath->RecentMoveFailureCooldown =
                            UnreachableBuilderCooldownSeconds;
                    }
                    Core::Utils::Logger::Info(
                        "BuildingSystem", "Cancelled pending " + Def.Name +
                        ": builder could not reach footprint after " +
                        std::to_string(Order.ApproachAttempt) + " attempts");
                    RefundAndCancel();
                    continue;
                }

                IssueConstructionMove(
                    EM, Order.BuilderID, TargetEntityID, Order.Position,
                    Order.BuildingDefID, Order.ApproachAttempt);
                if (auto* Command = EM.GetComponent<CommandComponent>(Order.BuilderID))
                {
                    Command->CurrentCommand = CommandType::Build;
                    Command->TargetEntityID = TargetEntityID;
                    Command->TargetPosition = Order.Position;
                }
                Order.ApproachSeconds = 0.0f;
                Order.NoProgressSeconds = 0.0f;
                Order.BestFootprintDistance = Distance;
                Order.HasBestFootprintDistance = true;
            }
            continue;
        }

        int EntityID = -1;
        if (IsGasBuildingDefID(Order.BuildingDefID) &&
            Order.Target.Type == BuildTargetType::TargetEntity)
        {
            const int TargetID = Order.Target.TargetEntityID;
            const auto* TargetResource =
                EM.GetComponent<ResourceComponent>(TargetID);
            const auto* TargetBuilding =
                EM.GetComponent<BuildingComponent>(TargetID);
            const auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
            const auto* Claimed =
                EM.GetComponent<ClaimedGeyserComponent>(TargetID);
            const int TargetDefID = ResolveDefID(EM, TargetID);
            const bool GeyserStillAvailable =
                EM.IsAlive(TargetID) && TargetResource && TargetBuilding &&
                TargetUnit && TargetResource->Type == ResourceType::VespeneGas &&
                !IsGasBuildingDefID(TargetDefID) &&
                (!Claimed || !Claimed->IsClaimed);
            if (GeyserStillAvailable)
            {
                EntityID = ClaimRefineryTarget(
                    EM, Order.BuilderID, Order.BuildingDefID,
                    TargetID, Order.PlayerID);
            }
        }
        else
        {
            const bool HasRequiredPower =
                !Def.RequiresPylon ||
                InternalHasPylonPowerAtPosition(
                    EM, Order.Position, Order.PlayerID);
            const bool HasRequiredCreep =
                !Def.RequiresCreep ||
                HasCreepAcrossFootprint(
                    Order.BuildingDefID, Order.Position);
            if (HasRequiredPower && HasRequiredCreep &&
                IsFootprintPlacementValid(
                    EM, Order.BuildingDefID, Order.Position,
                    Order.BuilderID, -1, &Order.AssistantBuilderIDs))
            {
                EntityID = CreateBuildingEntity(
                    EM, Order.BuildingDefID, Order.Position, Order.PlayerID,
                    true, Order.BuilderID, -1);
                if (EntityID >= 0 && !Order.AssistantBuilderIDs.empty())
                {
                    AssistConstruction(EM, Order.AssistantBuilderIDs, EntityID);
                }
            }
        }

        if (EntityID < 0)
        {
            Order.PlacementBlockedSeconds += DeltaTime;
            if (Order.PlacementBlockedSeconds < 5.0f)
                continue;
            Core::Utils::Logger::Info(
                "BuildingSystem", "Cancelled pending " + Def.Name +
                ": placement no longer valid");
            RefundAndCancel();
            continue;
        }

        if (Def.RaceType == Components::Race::Protoss)
        {
            if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
                Building->BuilderUnitID = -1;
            if (auto* Command = EM.GetComponent<CommandComponent>(Order.BuilderID))
            {
                Command->CancelAll();
                Command->TargetEntityID = -1;
            }
            if (auto* Path = EM.GetComponent<PathfindingComponent>(Order.BuilderID))
            {
                Path->Path.clear();
                Path->PathIndex = 0;
                Path->CurrentSpeed = 0.0f;
                Path->TargetSpeed = 0.0f;
                Path->CurrentVelocity = {0.0f, 0.0f};
                Path->RequestedTargetEntityID = -1;
            }
        }
        else if (Def.RaceType == Components::Race::Zerg)
        {
            // A Drone is the seed of a Zerg structure. Keep it as a normal,
            // targetable moving unit until it actually reaches the requested
            // footprint; only a successful placement consumes it and begins
            // the autonomous morph.
            if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
                Building->BuilderUnitID = -1;
            auto& Resources = HarvestSystem::GetResources(EM, Order.PlayerID);
            Resources.SupplyUsed = std::max(
                0, Resources.SupplyUsed - std::max(0, BuilderUnit->SupplyCost));
            EM.DestroyEntity(Order.BuilderID);
        }
        else if (auto* Command = EM.GetComponent<CommandComponent>(Order.BuilderID))
        {
            Command->CurrentCommand = CommandType::Build;
            Command->TargetEntityID = EntityID;
            Command->TargetPosition = Order.Position;
            if (auto* Path = EM.GetComponent<PathfindingComponent>(Order.BuilderID))
            {
                Path->RequestedIgnoreEntityID = EntityID;
            }
        }
        Order.PlacedEntityID = EntityID;
        Order.Active = false;
        Core::Utils::Logger::Info(
            "BuildingSystem",
            std::string(Def.RaceType == Components::Race::Protoss
                            ? "Started warp-in after Probe arrival: "
                            : Def.RaceType == Components::Race::Zerg
                                ? "Started morph after Drone arrival: "
                                : "Started building after SCV arrival: ") +
            Def.Name + " at (" + std::to_string((int)Order.Position.X) +
            "," + std::to_string((int)Order.Position.Y) + ")");
    }

    PendingOrders.erase(
        std::remove_if(
            PendingOrders.begin(), PendingOrders.end(),
            [](const BuildOrder& Order) { return !Order.Active; }),
        PendingOrders.end());
}

void BuildingSystem::ProcessFlightTransition(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
    if (!Building || !Building->FlightAnimationComplete ||
        Building->FlightHeightTransitionActive)
        return;

    Building->FlightAnimationComplete = false;
    if (Building->IsAddon)
    {
        Building->FlightPhase = BuildingFlightPhase::Grounded;
        return;
    }

    if (Building->FlightPhase == BuildingFlightPhase::LiftingOff)
    {
        Building->FlightHeightTransitionActive = true;
        Building->FlightHeightTransitionElapsed = 0.0f;
        Building->FlightVisualHeight = 0.0f;
        Core::Utils::Logger::Info("BuildingSystem",
            "Building " + std::to_string(EntityID) +
                " retracted landing gear; beginning body rise");
        return;
    }

    if (Building->FlightPhase != BuildingFlightPhase::Landing)
        return;

    Building->IsLifted = false;
    Building->FlightPhase = BuildingFlightPhase::Grounded;
    Building->FlightHeightTransitionActive = false;
    Building->FlightHeightTransitionElapsed = 0.0f;
    Building->FlightVisualHeight = 0.0f;
    if (auto* Unit = EM.GetComponent<UnitComponent>(EntityID))
    {
        Unit->Speed = 0.0f;
        Unit->SupplyCost = Races::GetUnitDefByID(Building->BuildingDefID).SupplyCost;
    }
    if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
        Render->Layer = RenderLayer::Buildings;
    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
    {
        Path->IsAir = false;
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->TargetSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
    }

    const auto* ParentPlayer = EM.GetComponent<PlayerComponent>(EntityID);
    const int AddonID = ParentPlayer
        ? FindCompatibleAddonAtLanding(EM, EntityID, ParentPlayer->PlayerID)
        : -1;
    if (AddonID >= 0)
        ConnectAddon(EM, EntityID, AddonID);

    const int ResumedHarvesters =
        HarvestSystem::ResumeNearbyHarvestersForDepot(EM, EntityID);

    Core::Utils::Logger::Info("BuildingSystem",
        "Building " + std::to_string(EntityID) +
        " completed landing animation; resumed harvesters=" +
        std::to_string(ResumedHarvesters));
}

void BuildingSystem::AdvanceFlightHeightTransition(
    Core::ECS::EntityManager& EM,
    int EntityID,
    float DeltaTime)
{
    auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
    if (!Building || !Building->FlightHeightTransitionActive)
        return;

    constexpr float kFlightHeight = 24.0f;
    constexpr float kHeightTransitionSeconds = 0.5f;
    Building->FlightHeightTransitionElapsed += std::max(0.0f, DeltaTime);
    const float Linear = std::clamp(
        Building->FlightHeightTransitionElapsed / kHeightTransitionSeconds,
        0.0f, 1.0f);
    const float Smooth = Linear * Linear * (3.0f - 2.0f * Linear);

    if (Building->FlightPhase == BuildingFlightPhase::LiftingOff)
    {
        Building->FlightVisualHeight = kFlightHeight * Smooth;
        if (Linear < 1.0f)
            return;

        Building->FlightVisualHeight = kFlightHeight;
        Building->FlightHeightTransitionActive = false;
        Building->FlightHeightTransitionElapsed = 0.0f;
        Building->FlightPhase = BuildingFlightPhase::Airborne;
        if (auto* Unit = EM.GetComponent<UnitComponent>(EntityID))
            Unit->Speed = 2.0f;
        Core::Utils::Logger::Info("BuildingSystem",
            "Building " + std::to_string(EntityID) +
                " completed body rise and is airborne");
        return;
    }

    if (Building->FlightPhase != BuildingFlightPhase::Landing)
    {
        Building->FlightHeightTransitionActive = false;
        Building->FlightHeightTransitionElapsed = 0.0f;
        return;
    }

    Building->FlightVisualHeight = kFlightHeight * (1.0f - Smooth);
    if (Linear < 1.0f)
        return;

    Building->FlightVisualHeight = 0.0f;
    Building->FlightHeightTransitionActive = false;
    Building->FlightHeightTransitionElapsed = 0.0f;
    Building->FlightAnimationComplete = false;
    Core::Utils::Logger::Info("BuildingSystem",
        "Building " + std::to_string(EntityID) +
            " completed body descent; deploying landing gear");
}

void BuildingSystem::ProcessConstruction(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime)
{
    auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
    auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Building || !Building->UnderConstruction || !Transform || !Unit)
        return;
    if (const auto* Morph = EM.GetComponent<MorphComponent>(EntityID);
        Morph && Morph->IsMorphing)
    {
        return;
    }

    bool ProtossReadyToComplete = false;
    if (Unit->UnitRace == Components::Race::Protoss)
    {
        if (Building->ProtossPhase == ProtossConstructionPhase::None)
            Building->ProtossPhase = ProtossConstructionPhase::Rift;

        if (Building->ProtossPhase != ProtossConstructionPhase::Rift)
            Building->ProtossPhaseElapsed += DeltaTime;

        switch (Building->ProtossPhase)
        {
            case ProtossConstructionPhase::Rift:
                break;
            case ProtossConstructionPhase::RiftClosing:
                if (!Building->ProtossPhaseSignal &&
                    Building->ProtossPhaseElapsed < 0.35f)
                    return;
                Building->ProtossPhaseSignal = false;
                Building->ProtossPhase = ProtossConstructionPhase::Materializing;
                Building->ProtossPhaseElapsed = 0.0f;
                if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
                {
                    Render->ImageID = ResolveImageID(Unit->ChkUnitID);
                    Render->FrameIndex = 0;
                    Render->TotalFrames = 1;
                    Render->UseProtossWarpTexture = true;
                    Render->ProtossWarpTextureFrame = 0;
                }
                return;
            case ProtossConstructionPhase::Materializing:
                if (!Building->ProtossPhaseSignal &&
                    Building->ProtossPhaseElapsed < 2.5f)
                    return;
                Building->ProtossPhaseSignal = false;
                Building->ProtossPhase = ProtossConstructionPhase::WarpIn;
                Building->ProtossPhaseElapsed = 0.0f;
                if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
                    Render->UseProtossWarpTexture = false;
                return;
            case ProtossConstructionPhase::WarpIn:
                if (!Building->ProtossPhaseSignal &&
                    Building->ProtossPhaseElapsed < 2.0f)
                    return;
                Building->ProtossPhaseSignal = false;
                Building->ProtossPhase = ProtossConstructionPhase::None;
                Building->ProtossPhaseElapsed = 0.0f;
                ProtossReadyToComplete = true;
                break;
            case ProtossConstructionPhase::None:
                break;
        }
    }

    bool BuilderNearby = false;
    bool IsZergOrProtoss = (Unit->UnitRace == Components::Race::Zerg ||
                             Unit->UnitRace == Components::Race::Protoss);

    // Zerg/Protoss buildings self-construct; no builder proximity required
    if (IsZergOrProtoss)
    {
        BuilderNearby = true;
    }
    else
    {
        auto* BuilderTransform = EM.GetComponent<TransformComponent>(Building->BuilderUnitID);
        auto* BuilderUnit = EM.GetComponent<UnitComponent>(Building->BuilderUnitID);
        if (Building->IsAddon && Building->BuilderUnitID >= 0)
        {
            auto* ParentBuilding = EM.GetComponent<BuildingComponent>(Building->BuilderUnitID);
            BuilderNearby =
                ParentBuilding &&
                !ParentBuilding->UnderConstruction &&
                BuilderUnit &&
                BuilderUnit->HitPoints > 0;
        }
        else if (BuilderTransform && BuilderUnit && BuilderUnit->HitPoints > 0)
        {
            auto* BuilderCommand =
                EM.GetComponent<CommandComponent>(Building->BuilderUnitID);
            if (BuilderCommand &&
                (BuilderCommand->CurrentCommand != CommandType::Build ||
                 BuilderCommand->TargetEntityID != EntityID))
            {
                Building->BuilderUnitID = -1;
                BuilderNearby = false;
            }
            else
            {
                const float Distance = DistanceToConstructionFootprint(
                    EM,
                    BuilderTransform->Position,
                    EntityID,
                    Transform->Position,
                    Building->BuildingDefID);
                const float BuilderRadius = CollisionRadiusFromBounds(
                    EM.GetComponent<PathfindingComponent>(Building->BuilderUnitID), 16.0f);
                // Construction approach points lie immediately outside the
                // building collision box. Leave enough contact tolerance for DAT
                // asymmetry, integer waypoint rounding, and the final movement
                // step; the previous two-pixel margin could strand an SCV beside
                // a Command Center forever.
                BuilderNearby = Distance <= BuilderRadius + 18.0f;
                if (BuilderNearby)
                {
                    if (BuilderCommand)
                    {
                        BuilderCommand->CurrentCommand = CommandType::Build;
                        BuilderCommand->TargetEntityID = EntityID;
                    }

                auto* BuilderPath =
                    EM.GetComponent<PathfindingComponent>(Building->BuilderUnitID);
                if (BuilderPath)
                {
                    BuilderPath->RequestedIgnoreEntityID = EntityID;
                    constexpr float kWeldCycleSeconds = 2.4f;
                    constexpr float kWeldStationarySeconds = 1.65f;
                    const int CycleIndex = (int)(Building->BuildProgress / kWeldCycleSeconds);
                    const float SubPhase = std::fmod(Building->BuildProgress, kWeldCycleSeconds);

                    if (SubPhase < kWeldStationarySeconds)
                    {
                        BuilderPath->Path.clear();
                        BuilderPath->PathIndex = 0;
                        BuilderPath->CurrentSpeed = 0.0f;
                        BuilderPath->TargetSpeed = 0.0f;
                        BuilderPath->CurrentVelocity = {0.0f, 0.0f};
                        BuilderPath->RequestedTargetEntityID = -1;
                        BuilderPath->RequestedDestination = BuilderTransform->Position;
                        BuilderPath->StallTimer = 0.0f;
                        BuilderPath->StallTicks = 0;
                        BuilderPath->HasLastWaypointDistance = false;

                        const Core::Math::Vec2 ToBuilding = Transform->Position - BuilderTransform->Position;
                        if (ToBuilding.LengthSq() > 4.0f)
                        {
                            const float Angle = std::atan2(ToBuilding.Y, ToBuilding.X);
                            BuilderTransform->Rotation = Angle;
                            BuilderPath->FacingAngle = Angle;
                        }
                    }
                    else if (IsMoveSettled(BuilderPath) && BuilderPath->LastRepathTime >= 0.6f)
                    {
                        BuilderPath->LastRepathTime = 0.0f;
                        float HalfW = 40.0f, HalfH = 28.0f;
                        if (const auto* BldgPath = EM.GetComponent<PathfindingComponent>(EntityID);
                            BldgPath && BldgPath->HasCollisionBounds)
                        {
                            HalfW = (float)std::max(16, BldgPath->CollisionLeft) + BuilderRadius + 2.0f;
                            HalfH = (float)std::max(16, BldgPath->CollisionUp) + BuilderRadius + 2.0f;
                        }
                        static const float kShuttleOffsets[8][2] = {
                            {-0.95f, -0.70f}, { 0.15f, -0.95f}, { 0.95f, -0.65f}, { 0.90f,  0.15f},
                            { 0.90f,  0.75f}, {-0.15f,  0.95f}, {-0.95f,  0.70f}, {-0.90f, -0.10f}
                        };
                        const int Slot = (CycleIndex * 5 + EntityID * 3 + 1) & 7;
                        const float RadiusScale = ((CycleIndex + EntityID) & 1) ? 0.82f : 1.0f;
                        Core::Math::Vec2 NextWeldPos{
                            Transform->Position.X + HalfW * kShuttleOffsets[Slot][0] * RadiusScale,
                            Transform->Position.Y + HalfH * kShuttleOffsets[Slot][1] * RadiusScale
                        };
                        if (gMap)
                        {
                            NextWeldPos.X = std::clamp(NextWeldPos.X, 16.0f, (float)(gMap->GetWidth() * 32 - 16));
                            NextWeldPos.Y = std::clamp(NextWeldPos.Y, 16.0f, (float)(gMap->GetHeight() * 32 - 16));

                            const auto& NavGrid = gMap->GetNavigationGrid();
                            const int BR = (int)std::ceil(BuilderRadius);
                            const Game::Pathfinding::NavigationFootprint BuilderFootprint{BR, BR, BR, BR};
                            if (!NavGrid.CanOccupyWorld(NextWeldPos, BuilderFootprint, EntityID, -1) ||
                                !NavGrid.HasLineOfSightWorld(BuilderTransform->Position, NextWeldPos, BuilderFootprint, EntityID, -1))
                            {
                                NextWeldPos = BuilderTransform->Position;
                            }
                        }
                        if (NextWeldPos.DistanceTo(BuilderTransform->Position) > 4.0f)
                        {
                            BuilderPath->Path = { NextWeldPos };
                            const Core::Math::Vec2 MoveDir = NextWeldPos - BuilderTransform->Position;
                            const float MoveAngle = std::atan2(MoveDir.Y, MoveDir.X);
                            BuilderTransform->Rotation = MoveAngle;
                            BuilderPath->FacingAngle = MoveAngle;
                        }
                        else
                        {
                            BuilderPath->Path.clear();
                        }
                        BuilderPath->PathIndex = 0;
                        BuilderPath->RequestedDestination = NextWeldPos;
                        BuilderPath->RequestedIgnoreEntityID = EntityID;
                        BuilderPath->TargetSpeed = Game::Tuning::ToWorldMoveSpeed(BuilderUnit ? BuilderUnit->Speed : 2.5f);
                        BuilderPath->StallTimer = 0.0f;
                        BuilderPath->StallTicks = 0;
                        BuilderPath->HasLastWaypointDistance = false;
                    }
                }
            }
            else
            {
                auto* BuilderPath =
                    EM.GetComponent<PathfindingComponent>(Building->BuilderUnitID);
                if (IsMoveSettled(BuilderPath) && (!BuilderPath || BuilderPath->LastRepathTime >= 0.5f))
                {
                    if (BuilderPath)
                        BuilderPath->LastRepathTime = 0.0f;
                    IssueConstructionMove(
                        EM,
                        Building->BuilderUnitID,
                        EntityID,
                        Transform->Position,
                        Building->BuildingDefID);
                }
                if (DebugConstructionEnabled())
                {
                    static int DebugConstructionLogs = 120;
                    if (DebugConstructionLogs > 0)
                    {
                        --DebugConstructionLogs;
                        BuilderCommand =
                            EM.GetComponent<CommandComponent>(Building->BuilderUnitID);
                        BuilderPath =
                            EM.GetComponent<PathfindingComponent>(Building->BuilderUnitID);
                        Core::Utils::Logger::Info(
                            "BuildingSystem",
                            "construction debug building=" + std::to_string(EntityID) +
                                " builder=" + std::to_string(Building->BuilderUnitID) +
                                " nearby=" + std::to_string(BuilderNearby ? 1 : 0) +
                                " dist=" + std::to_string((int)Distance) +
                                " progress=" + std::to_string((int)Building->BuildProgress) +
                                " builderPos=" +
                                std::to_string((int)BuilderTransform->Position.X) + "," +
                                std::to_string((int)BuilderTransform->Position.Y) +
                                " cmd=" +
                                std::to_string(BuilderCommand ? (int)BuilderCommand->CurrentCommand : -1) +
                                " pathSize=" +
                                std::to_string(BuilderPath ? (int)BuilderPath->Path.size() : -1) +
                                " pathIndex=" +
                                std::to_string(BuilderPath ? BuilderPath->PathIndex : -1));
                    }
                }
            }
        }
    }
}

    int NearbyBuilderCount = BuilderNearby ? 1 : 0;
    if (Unit->UnitRace == Components::Race::Terran && !Building->IsAddon)
    {
        for (size_t AsstIdx = 0; AsstIdx < Building->AssistantBuilderUnitIDs.size(); ++AsstIdx)
        {
            int AssistantID = Building->AssistantBuilderUnitIDs[AsstIdx];
            if (AssistantID == Building->BuilderUnitID)
                continue;

            auto* AssistantCommand = EM.GetComponent<CommandComponent>(AssistantID);
            if (AssistantCommand && (AssistantCommand->CurrentCommand != CommandType::Build ||
                                     AssistantCommand->TargetEntityID != EntityID))
            {
                Building->AssistantBuilderUnitIDs.erase(Building->AssistantBuilderUnitIDs.begin() + AsstIdx);
                --AsstIdx;
                continue;
            }

            auto* AssistantTransform = EM.GetComponent<TransformComponent>(AssistantID);
            auto* AssistantUnit = EM.GetComponent<UnitComponent>(AssistantID);
            if (!AssistantTransform || !AssistantUnit || AssistantUnit->HitPoints <= 0)
                continue;

            const float Distance = DistanceToConstructionFootprint(
                EM, AssistantTransform->Position, EntityID,
                Transform->Position, Building->BuildingDefID);
            const float AssistantRadius = CollisionRadiusFromBounds(
                EM.GetComponent<PathfindingComponent>(AssistantID), 16.0f);
            const bool AssistantNearby = Distance <= AssistantRadius + 16.0f;

            // Stagger each assistant SCV's cycle phase and perimeter offset
            // so they do not collide or contest the same spot.
            // Stagger each assistant SCV's cycle phase so they weld and micro-adjust smoothly.
            constexpr float kLocalCycle = 4.5f;
            const float AssistantProgressTime = Building->BuildProgress + (float)(AsstIdx + 1) * 1.1f;
            const float AssistantSubPhase = std::fmod(AssistantProgressTime, kLocalCycle);
            const int AssistantCandidateOffset = (int)(AsstIdx + 1) * 2;

            if (AssistantNearby)
            {
                if (AssistantCommand)
                {
                    AssistantCommand->CurrentCommand = CommandType::Build;
                    AssistantCommand->TargetEntityID = EntityID;
                }
                const Core::Math::Vec2 ToBuilding = Transform->Position - AssistantTransform->Position;
                if (ToBuilding.LengthSq() > 0.01f)
                {
                    AssistantTransform->Rotation = std::atan2(ToBuilding.Y, ToBuilding.X);
                }
                auto* AssistantPath = EM.GetComponent<PathfindingComponent>(AssistantID);
                if (AssistantPath)
                {
                    if (AssistantSubPhase < 3.6f)
                    {
                        AssistantPath->Path.clear();
                        AssistantPath->PathIndex = 0;
                        AssistantPath->CurrentSpeed = 0.0f;
                        AssistantPath->TargetSpeed = 0.0f;
                        AssistantPath->CurrentVelocity = {0.0f, 0.0f};
                        AssistantPath->RequestedTargetEntityID = -1;
                        AssistantPath->RequestedDestination = AssistantTransform->Position;
                        AssistantPath->RequestedIgnoreEntityID = EntityID;
                    }
                    else
                    {
                        if (IsMoveSettled(AssistantPath) && AssistantPath->LastRepathTime >= 1.0f)
                        {
                            AssistantPath->LastRepathTime = 0.0f;
                            Core::Math::Vec2 Tangent{-ToBuilding.Y, ToBuilding.X};
                            if (Tangent.LengthSq() > 0.01f)
                                Tangent = Tangent.Normalized();
                            const float ShiftAmount = (std::fmod(AssistantProgressTime, 9.0f) < 4.5f ? 14.0f : -14.0f);
                            const Core::Math::Vec2 TargetPos = AssistantTransform->Position + Tangent * ShiftAmount;
                            CommandSystem::IssueMove(EM, {AssistantID}, TargetPos, EntityID);
                        }
                    }
                }
                ++NearbyBuilderCount;
            }
            else
            {
                auto* AssistantPath = EM.GetComponent<PathfindingComponent>(AssistantID);
                if (IsMoveSettled(AssistantPath) && (!AssistantPath || AssistantPath->LastRepathTime >= 0.5f))
                {
                    if (AssistantPath)
                        AssistantPath->LastRepathTime = 0.0f;
                    IssueConstructionMove(
                        EM, AssistantID, EntityID, Transform->Position,
                        Building->BuildingDefID, AssistantCandidateOffset);
                }
            }
        }
        BuilderNearby = NearbyBuilderCount > 0;
    }

    if (BuilderNearby)
    {
        const float PreviousProgress = std::clamp(
            Building->BuildProgress, 0.0f, Building->TotalBuildTime);
        Building->BuildProgress = std::min(
            Building->TotalBuildTime,
            PreviousProgress +
                DeltaTime * (float)std::max(1, NearbyBuilderCount));
        const float ProgressDelta = Building->TotalBuildTime > 0.0f
            ? (Building->BuildProgress - PreviousProgress) /
                  Building->TotalBuildTime
            : 1.0f;
        const int MaxHP = Unit->MaxHitPoints;
        const int InitialHP = std::max(1, MaxHP / 10);
        Building->ConstructionHitPointAccumulator +=
            (float)(MaxHP - InitialHP) * ProgressDelta;
        const int HitPointGain =
            (int)Building->ConstructionHitPointAccumulator;
        Building->ConstructionHitPointAccumulator -= (float)HitPointGain;
        Unit->HitPoints = std::clamp(
            Unit->HitPoints + HitPointGain, 1, MaxHP);
        const int MaxShields = Unit->MaxShieldPoints;
        const int InitialShields = MaxShields / 10;
        Building->ConstructionShieldAccumulator +=
            (float)(MaxShields - InitialShields) * ProgressDelta;
        const int ShieldGain = (int)Building->ConstructionShieldAccumulator;
        Building->ConstructionShieldAccumulator -= (float)ShieldGain;
        Unit->ShieldPoints = std::clamp(
            Unit->ShieldPoints + ShieldGain,
            0, MaxShields);
    }

    // OpenBW's Terran IncompleteBuilding order replaces the generic
    // construction GRP with the finished building graphic after 3/5 HP, then
    // runs AlmostBuilt on that graphic for the remaining construction time.
    // Zerg buildings remain on their pulsating morph cocoon until completion,
    // playing the 6-tick cocoon pop right before morph finishes and spawning
    // the slime burst overlay when switching to the finished structure graphic.
    const bool ShouldSwitchVisual =
        (Unit->UnitRace == Components::Race::Terran &&
         Building->TotalBuildTime > 0.0f &&
         Building->BuildProgress >= Building->TotalBuildTime * 0.6f);

    if (ShouldSwitchVisual)
    {
        if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
        {
            const auto* Morph = EM.GetComponent<MorphComponent>(EntityID);
            const int FinishedChk = (Morph && Morph->IsMorphing)
                ? Game::Races::GetUnitDefByID(Morph->MorphTargetDefID).ChkUnitID
                : Unit->ChkUnitID;
            const uint32_t FinishedImageID = ResolveImageID(FinishedChk);
            if (Render->ImageID != FinishedImageID)
            {
                Render->ImageID = FinishedImageID;
                Render->SubunitImageID = ResolveSubunitImageID(FinishedChk);
                Render->FrameIndex = 0;
                Render->TotalFrames = 1;
                if (DebugConstructionEnabled())
                {
                    Core::Utils::Logger::Info(
                        "BuildingSystem",
                        "construction visual switched building=" +
                            std::to_string(EntityID) +
                            " image=" + std::to_string(FinishedImageID) +
                            " progress=" +
                            std::to_string((int)Building->BuildProgress));
                }
            }
            Render->Tint = Core::Colors::White();
        }
    }

    if (Building->BuildProgress < Building->TotalBuildTime)
        return;

    if (Unit->UnitRace == Components::Race::Protoss && !ProtossReadyToComplete)
    {
        Building->BuildProgress = Building->TotalBuildTime;
        Building->ProtossPhase = ProtossConstructionPhase::RiftClosing;
        Building->ProtossPhaseSignal = false;
        Building->ProtossPhaseElapsed = 0.0f;
        return;
    }

    // Round the final fractional construction point to the nearest whole
    // point. Floating-point progress can leave a tiny positive residue even
    // when the full integer vitality budget was already granted; treating any
    // residue as +1 would heal one point of combat damage on completion.
    if (Building->ConstructionHitPointAccumulator >= 0.5f)
    {
        Unit->HitPoints = std::min(Unit->MaxHitPoints, Unit->HitPoints + 1);
    }
    Building->ConstructionHitPointAccumulator = 0.0f;
    if (Building->ConstructionShieldAccumulator >= 0.5f)
    {
        Unit->ShieldPoints = std::min(Unit->MaxShieldPoints,
                                      Unit->ShieldPoints + 1);
    }
    Building->ConstructionShieldAccumulator = 0.0f;

    Building->UnderConstruction = false;
    Building->BuildProgress = Building->TotalBuildTime;
    Unit->EnergyPoints = Unit->MaxEnergyPoints;
    Unit->IsDetector = Races::IsDetectorChkUnitID(Unit->ChkUnitID);

    const auto Def = Races::GetUnitDefByID(Building->BuildingDefID);
    if (!EM.GetComponent<CombatComponent>(EntityID) &&
        (Def.GroundDamage > 0 || Def.AirDamage > 0))
    {
        auto& Combat = EM.AddComponent<CombatComponent>(EntityID);
        Races::ApplyCombatDefinition(
            Combat, Def, Game::Data::IsAirUnitType(Unit->ChkUnitID));
    }
    if (auto* Cmd = EM.GetComponent<CommandComponent>(EntityID))
    {
        if (Cmd->CurrentCommand == CommandType::Build)
            Cmd->CurrentCommand = CommandType::Stop;
    }

    if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
    {
        Render->ImageID = ResolveImageID(Unit->ChkUnitID);
        Render->SubunitImageID = ResolveSubunitImageID(Unit->ChkUnitID);
        Render->FrameIndex = 0;
        Render->TotalFrames = 1;
        Render->Tint = Core::Colors::White();
        Render->UseProtossWarpTexture = false;
        Render->ProtossWarpTextureFrame = 0;
    }

    if (Building->IsAddon && Building->BuilderUnitID >= 0)
        SetAddonConnectorVisual(EM, EntityID, true);

    int PlayerID = 0;
    if (auto* Player = EM.GetComponent<PlayerComponent>(EntityID))
        PlayerID = Player->PlayerID;

    HarvestSystem::GetResources(EM, PlayerID).SupplyMax +=
        Races::GetSupplyProvidedByDefID(Building->BuildingDefID);
    EM.Events().Emit(Game::Events::BuildingCompletedEvent{
        EntityID, PlayerID, Building->BuildingDefID});

    std::vector<int> CompletedBuilders{Building->BuilderUnitID};
    CompletedBuilders.insert(
        CompletedBuilders.end(),
        Building->AssistantBuilderUnitIDs.begin(),
        Building->AssistantBuilderUnitIDs.end());

    auto UnstuckBuilderFromBuilding = [&](int BuilderID, int BuildingID)
    {
        if (BuilderID < 0) return;
        auto* BuilderTrans = EM.GetComponent<TransformComponent>(BuilderID);
        auto* BuildingTrans = EM.GetComponent<TransformComponent>(BuildingID);
        auto* BuildingComp = EM.GetComponent<BuildingComponent>(BuildingID);
        if (!BuilderTrans || !BuildingTrans || !BuildingComp) return;

        const auto* BuildingPath = EM.GetComponent<PathfindingComponent>(BuildingID);
        float HalfW = 48.0f;
        float HalfH = 32.0f;
        if (BuildingPath && BuildingPath->HasCollisionBounds)
        {
            HalfW = (float)std::max(BuildingPath->CollisionLeft, BuildingPath->CollisionRight);
            HalfH = (float)std::max(BuildingPath->CollisionUp, BuildingPath->CollisionDown);
        }
        else
        {
            const auto Footprint = BuildingFootprintSizeForDef(BuildingComp->BuildingDefID);
            HalfW = (float)Footprint.W * 16.0f;
            HalfH = (float)Footprint.H * 16.0f;
        }

        const float BuilderRadius = CollisionRadiusFromBounds(
            EM.GetComponent<PathfindingComponent>(BuilderID), 16.0f);
        const float SafeOutset = BuilderRadius + 14.0f;

        Core::Math::Vec2 Offset = BuilderTrans->Position - BuildingTrans->Position;
        if (Offset.Length() < 1.0f)
            Offset = { 0.0f, 1.0f };
        Offset = Offset.Normalized();

        Core::Math::Vec2 SafePos = BuilderTrans->Position;
        if (std::abs(Offset.X) * HalfH >= std::abs(Offset.Y) * HalfW)
        {
            SafePos.X = BuildingTrans->Position.X + (Offset.X >= 0.0f ? (HalfW + SafeOutset) : (-HalfW - SafeOutset));
            SafePos.Y = std::clamp(BuilderTrans->Position.Y, BuildingTrans->Position.Y - HalfH, BuildingTrans->Position.Y + HalfH);
        }
        else
        {
            SafePos.X = std::clamp(BuilderTrans->Position.X, BuildingTrans->Position.X - HalfW, BuildingTrans->Position.X + HalfW);
            SafePos.Y = BuildingTrans->Position.Y + (Offset.Y >= 0.0f ? (HalfH + SafeOutset) : (-HalfH - SafeOutset));
        }

        if (gMap)
        {
            int TileX = 0, TileY = 0;
            gMap->WorldToTile(SafePos, TileX, TileY);
            if (!gMap->IsWalkable(TileX, TileY))
            {
                for (int R = 1; R <= 3; ++R)
                {
                    bool Found = false;
                    for (int Dy = -R; Dy <= R && !Found; ++Dy)
                    {
                        for (int Dx = -R; Dx <= R && !Found; ++Dx)
                        {
                            if (gMap->IsWalkable(TileX + Dx, TileY + Dy))
                            {
                                SafePos = gMap->TileToWorld(TileX + Dx, TileY + Dy);
                                Found = true;
                            }
                        }
                    }
                    if (Found) break;
                }
            }
        }

        BuilderTrans->Position = SafePos;

        if (auto* Path = EM.GetComponent<PathfindingComponent>(BuilderID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->TargetSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
            Path->BlockedByEntityID = -1;
            Path->HasRecentMoveFailure = false;
            Path->RecentMoveFailureTicks = 0;
            Path->RequestedTargetEntityID = -1;
            Path->RequestedDestination = SafePos;
            Path->RequestedIgnoreEntityID = -1;
        }
    };

    for (int BuilderID : CompletedBuilders)
    {
        UnstuckBuilderFromBuilding(BuilderID, EntityID);
        if (auto* BuilderCommand = EM.GetComponent<CommandComponent>(BuilderID))
        {
            BuilderCommand->CancelAll();
            BuilderCommand->TargetEntityID = -1;
        }
        // In original StarCraft, builders stay idle after completing a non-gas structure.
    }
    if (IsGasBuildingDefID(Building->BuildingDefID) &&
        Building->BuilderUnitID >= 0 &&
        HarvestSystem::IssueHarvest(
            EM, {Building->BuilderUnitID}, EntityID))
    {
        Core::Utils::Logger::Info(
            "BuildingSystem",
            "Builder " + std::to_string(Building->BuilderUnitID) +
                " started gas harvesting after completing refinery " +
                std::to_string(EntityID));
    }
    Building->AssistantBuilderUnitIDs.clear();

    Core::Utils::Logger::Info("BuildingSystem",
        "Construction complete for entity " + std::to_string(EntityID));
}

bool BuildingSystem::LiftOffBuilding(Core::ECS::EntityManager& EM, int BuildingID)
{
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    auto* Unit = EM.GetComponent<UnitComponent>(BuildingID);
    if (!Bldg || !Unit || !Bldg->CanLiftOff || Bldg->IsLifted ||
        Bldg->FlightPhase != BuildingFlightPhase::Grounded)
    {
        return false;
    }
    if (Bldg->UnderConstruction) return false;
    if (Bldg->IsResearching) return false;
    if (auto* Prod = EM.GetComponent<ProductionComponent>(BuildingID))
    {
        if (!Prod->Queue.empty() || Prod->CurrentTrainingUnitDefID >= 0 || Prod->TrainingProgress > 0.0f)
            return false;
    }

    DetachAddon(EM, BuildingID);
    Bldg->IsLifted = true;
    Bldg->FlightPhase = BuildingFlightPhase::LiftingOff;
    Bldg->FlightAnimationComplete = false;
    Bldg->FlightHeightTransitionActive = false;
    Bldg->FlightHeightTransitionElapsed = 0.0f;
    Bldg->FlightVisualHeight = 0.0f;
    Unit->Speed = 0.0f;
    Unit->SupplyCost = 0; // Supply freed during flight
    if (auto* Render = EM.GetComponent<RenderComponent>(BuildingID))
        Render->Layer = RenderLayer::AirUnits;

    if (auto* Command = EM.GetComponent<CommandComponent>(BuildingID))
    {
        Command->CancelAll();
        Command->CommandQueue.clear();
        Command->CurrentCommand = CommandType::Stop;
        Command->TargetEntityID = -1;
    }

    auto* PathPtr = EM.GetComponent<PathfindingComponent>(BuildingID);
    auto& Path = PathPtr ? *PathPtr : EM.AddComponent<PathfindingComponent>(BuildingID);
    Path.IsAir = true;
    Path.Path.clear();
    Path.PathIndex = 0;
    Path.CurrentSpeed = 0.0f;
    Path.TargetSpeed = 0.0f;
    Path.CurrentVelocity = {0.0f, 0.0f};
    if (const auto* Transform = EM.GetComponent<TransformComponent>(BuildingID))
        Path.RequestedDestination = Transform->Position;
    Path.RequestedTargetEntityID = -1;
    Path.RequestedIgnoreEntityID = -1;
    Path.HasRecentMoveFailure = false;

    Core::Utils::Logger::Info("BuildingSystem",
        "Building " + std::to_string(BuildingID) + " lifted off");
    return true;
}

bool BuildingSystem::LandBuilding(Core::ECS::EntityManager& EM, int BuildingID,
                                   Core::Math::Vec2 Position)
{
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    auto* Unit = EM.GetComponent<UnitComponent>(BuildingID);
    if (!Bldg || !Unit || !Bldg->IsLifted ||
        Bldg->FlightPhase != BuildingFlightPhase::Airborne)
    {
        return false;
    }

    auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    const int PlayerID = Player ? Player->PlayerID : -1;

    if (!IsFootprintPlacementValid(EM, Bldg->BuildingDefID, Position, BuildingID, PlayerID))
        return false;

    Bldg->FlightPhase = BuildingFlightPhase::Landing;
    Bldg->FlightAnimationComplete = false;
    Bldg->FlightHeightTransitionActive = true;
    Bldg->FlightHeightTransitionElapsed = 0.0f;
    Bldg->FlightVisualHeight = 24.0f;
    Unit->Speed = 0.0f;

    auto* Trans = EM.GetComponent<TransformComponent>(BuildingID);
    if (Trans) Trans->Position = Position;

    if (auto* Path = EM.GetComponent<PathfindingComponent>(BuildingID))
    {
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->TargetSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
    }

    Core::Utils::Logger::Info("BuildingSystem",
        "Building " + std::to_string(BuildingID) + " began landing at (" +
        std::to_string((int)Position.X) + "," + std::to_string((int)Position.Y) + ")");
    return true;
}

bool BuildingSystem::LandBuilding(Core::ECS::EntityManager& EM, int BuildingID)
{
    const auto* Transform = EM.GetComponent<TransformComponent>(BuildingID);
    return Transform && LandBuilding(EM, BuildingID, Transform->Position);
}

static void SpawnCancelledZergDrone(Core::ECS::EntityManager& EM,
                                    const Core::Math::Vec2& SpawnPos,
                                    int PlayerID)
{
    const int DroneDefID = Races::GetUnitDefIDByChkUnitID(41);
    auto DroneDef = Races::GetUnitDefByID(DroneDefID);
    if (DroneDefID < 0 || DroneDef.ChkUnitID != 41)
        return;

    int DroneID = EM.CreateEntity();
    auto& DTrans = EM.AddComponent<TransformComponent>(DroneID);
    DTrans.Position = SpawnPos;
    auto& DRender = EM.AddComponent<RenderComponent>(DroneID);
    DRender.ImageID = gImageResolver ? gImageResolver(DroneDef.ChkUnitID) : 0;
    DRender.FrameIndex = 0;
    DRender.TotalFrames = 1;
    DRender.Tint = Core::Colors::White();
    DRender.Layer = RenderLayer::Units;
    auto& DUnit = EM.AddComponent<UnitComponent>(DroneID);
    DUnit.ChkUnitID = DroneDef.ChkUnitID;
    DUnit.UnitRace = DroneDef.RaceType;
    DUnit.SizeType = DroneDef.SizeType;
    DUnit.HitPoints = DroneDef.HitPoints;
    DUnit.MaxHitPoints = DroneDef.HitPoints;
    DUnit.Armor = DroneDef.Armor;
    DUnit.Speed = DroneDef.Speed;
    DUnit.SightRange = DroneDef.SightRange;
    DUnit.SupplyCost = DroneDef.SupplyCost;
    auto& DPlayer = EM.AddComponent<PlayerComponent>(DroneID);
    DPlayer.PlayerID = PlayerID;
    auto& DCmd = EM.AddComponent<CommandComponent>(DroneID);
    DCmd.CurrentCommand = CommandType::Stop;
    auto& DPath = EM.AddComponent<PathfindingComponent>(DroneID);
    DPath.NodeTolerance = 8.0f;
    DPath.IsAir = false;
    ApplyCollisionBounds(DPath, ResolveCollisionBounds(DroneDef.ChkUnitID));
    auto& DCombat = EM.AddComponent<CombatComponent>(DroneID);
    Races::ApplyCombatDefinition(DCombat, DroneDef, false);
    auto& DBurrow = EM.AddComponent<BurrowComponent>(DroneID);
    DBurrow.CanBurrow = true;
    EM.AddComponent<HarvestComponent>(DroneID);
    auto& Res = HarvestSystem::GetResources(EM, PlayerID);
    Res.SupplyUsed += DroneDef.SupplyCost;
}

bool BuildingSystem::CancelConstruction(Core::ECS::EntityManager& EM, int BuildingID)
{
    auto* Building = EM.GetComponent<BuildingComponent>(BuildingID);
    if (!Building || !Building->UnderConstruction)
        return false;

    auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const auto Def = Races::GetUnitDefByID(Building->BuildingDefID);
    auto& Resources = HarvestSystem::GetResources(EM, PlayerID);

    // If this was an add-on, detach from parent
    if (Building->IsAddon && Building->BuilderUnitID >= 0)
    {
        if (auto* Parent = EM.GetComponent<BuildingComponent>(Building->BuilderUnitID))
        {
            if (Parent->AddonID == BuildingID)
                Parent->AddonID = -1;
        }
    }

    // If an assigned worker was constructing (e.g. SCV), release the worker
    if (!Building->IsAddon && Building->BuilderUnitID >= 0 && EM.IsAlive(Building->BuilderUnitID))
    {
        if (auto* BuilderCmd = EM.GetComponent<CommandComponent>(Building->BuilderUnitID))
        {
            BuilderCmd->CancelAll();
            BuilderCmd->CurrentCommand = CommandType::Stop;
            BuilderCmd->TargetEntityID = -1;
        }
    }

    // Release all assistant builders assigned to this building
    for (int AsstID : Building->AssistantBuilderUnitIDs)
    {
        if (AsstID >= 0 && EM.IsAlive(AsstID))
        {
            if (auto* AsstCmd = EM.GetComponent<CommandComponent>(AsstID))
            {
                AsstCmd->CancelAll();
                AsstCmd->CurrentCommand = CommandType::Stop;
                AsstCmd->TargetEntityID = -1;
            }
        }
    }
    Building->AssistantBuilderUnitIDs.clear();

    auto* Trans = EM.GetComponent<TransformComponent>(BuildingID);
    const Core::Math::Vec2 SpawnPos = Trans ? Trans->Position : Core::Math::Vec2{};

    // If this was a refinery on a geyser, restore the original geyser
    if (auto* Claimed = EM.GetComponent<ClaimedGeyserComponent>(BuildingID); Claimed && Claimed->IsClaimed)
    {
        Resources.Minerals += (Def.MineralCost * 3) / 4;
        Resources.VespeneGas += (Def.GasCost * 3) / 4;

        if (auto* Unit = EM.GetComponent<UnitComponent>(BuildingID)) *Unit = Claimed->OriginalUnit;
        if (auto* Render = EM.GetComponent<RenderComponent>(BuildingID)) *Render = Claimed->OriginalRender;
        if (auto* TargetBuilding = EM.GetComponent<BuildingComponent>(BuildingID)) *TargetBuilding = Claimed->OriginalBuilding;
        if (auto* TargetPlayer = EM.GetComponent<PlayerComponent>(BuildingID)) *TargetPlayer = Claimed->OriginalPlayer;
        if (auto* TargetResource = EM.GetComponent<ResourceComponent>(BuildingID)) TargetResource->Type = ResourceType::VespeneGas;
        if (Claimed->HadPathfinding)
        {
            auto* Path = EM.GetComponent<PathfindingComponent>(BuildingID);
            if (!Path)
                Path = &EM.AddComponent<PathfindingComponent>(BuildingID);
            *Path = Claimed->OriginalPathfinding;
        }
        else
        {
            EM.RemoveComponent<PathfindingComponent>(BuildingID);
        }
        if (auto* Tree = EM.GetComponent<ImageTreeComponent>(BuildingID))
            *Tree = ImageTreeComponent{};
        if (auto* Command = EM.GetComponent<CommandComponent>(BuildingID))
        {
            Command->CancelAll();
            Command->TargetEntityID = -1;
        }
        Claimed->IsClaimed = false;
        EM.RemoveComponent<ClaimedGeyserComponent>(BuildingID);

        if (Def.RaceType == Components::Race::Zerg)
        {
            SpawnCancelledZergDrone(EM, SpawnPos, PlayerID);
        }
        return true;
    }

    // In SC1, cancelling an upgrading Zerg structure reverts it without destroying the base building,
    // whereas cancelling a Zerg Egg/Cocoon/Lurker Egg refunds 100% of the morph cost.
    if (auto* Morph = EM.GetComponent<MorphComponent>(BuildingID); Morph && Morph->IsMorphing)
    {
        const auto MorphTargetDef = Races::GetUnitDefByID(Morph->MorphTargetDefID);
        if (Building->BuildingDefID < 0)
        {
            Resources.Minerals += MorphTargetDef.MineralCost;
            Resources.VespeneGas += MorphTargetDef.GasCost;
            if (MorphTargetDef.SupplyCost > 0)
            {
                int RefundSupply = MorphTargetDef.SupplyCost;
                if (const auto* EggUnit = EM.GetComponent<UnitComponent>(BuildingID))
                {
                    if (EggUnit->ChkUnitID == 59 || EggUnit->ChkUnitID == 97)
                        RefundSupply = std::max(0, MorphTargetDef.SupplyCost - 4);
                }
                Resources.SupplyUsed = std::max(0, Resources.SupplyUsed - RefundSupply);
            }
            EM.DestroyEntity(BuildingID);
            return true;
        }

        Resources.Minerals += (MorphTargetDef.MineralCost * 3) / 4;
        Resources.VespeneGas += (MorphTargetDef.GasCost * 3) / 4;

        Building->UnderConstruction = false;
        Building->BuildProgress = 0.0f;
        Building->BuildTimeRemaining = 0.0f;
        if (auto* Render = EM.GetComponent<RenderComponent>(BuildingID))
        {
            Render->ImageID = gImageResolver ? gImageResolver(Def.ChkUnitID) : 0;
            Render->FrameIndex = 0;
        }
        EM.RemoveComponent<MorphComponent>(BuildingID);
        Core::Utils::Logger::Info("BuildingSystem",
            "Cancelled morphing upgrade of " + Def.Name + " (id=" + std::to_string(BuildingID) + ")");
        return true;
    }

    // Normal building cancellation (refund 75% of base building cost)
    Resources.Minerals += (Def.MineralCost * 3) / 4;
    Resources.VespeneGas += (Def.GasCost * 3) / 4;

    // In SC1, cancelling a morphing Zerg structure respawns the Drone that morphed into it
    if (Def.RaceType == Components::Race::Zerg && !Building->IsAddon)
    {
        SpawnCancelledZergDrone(EM, SpawnPos, PlayerID);
    }

    Core::Utils::Logger::Info("BuildingSystem",
        "Cancelled construction of " + Def.Name + " (id=" + std::to_string(BuildingID) + ")");
    EM.DestroyEntity(BuildingID);
    return true;
}

void BuildingSystem::RestoreSpawnedAddonLinks(Core::ECS::EntityManager& EM)
{
    for (int ParentID : EM.GetEntitiesWith<TransformComponent, BuildingComponent, PlayerComponent>())
    {
        auto* Parent = EM.GetComponent<BuildingComponent>(ParentID);
        auto* ParentPlayer = EM.GetComponent<PlayerComponent>(ParentID);
        if (!Parent || !ParentPlayer || Parent->IsAddon || Parent->UnderConstruction ||
            (Parent->AddonID >= 0 && EM.IsAlive(Parent->AddonID)))
        {
            continue;
        }

        const int AddonID = FindCompatibleAddonAtLanding(
            EM, ParentID, ParentPlayer->PlayerID);
        if (AddonID < 0)
            continue;
        auto* Addon = EM.GetComponent<BuildingComponent>(AddonID);
        if (!Addon)
            continue;
        Parent->AddonID = AddonID;
        Addon->BuilderUnitID = ParentID;
        Addon->FlightPhase = BuildingFlightPhase::Grounded;
        Addon->FlightAnimationComplete = false;
        SetAddonConnectorVisual(EM, AddonID, true);
    }
}

} // namespace Game::Systems
