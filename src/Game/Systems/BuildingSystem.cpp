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
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    const Game::Map::GameMap* gMap = nullptr;
    BuildingSystem::ImageIDResolver gImageResolver;
    BuildingSystem::ImageIDResolver gConstructionImageResolver;
    BuildingSystem::CollisionBoundsResolver gCollisionBoundsResolver;
    BuildingSystem::AddonOffsetResolver gAddonOffsetResolver;

    struct BuildingFootprintTiles
    {
        int W = 3;
        int H = 2;
    };

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

    BuildingFootprintTiles BuildingFootprintSizeForChk(int ChkUnitID)
    {
        switch (ChkUnitID)
        {
            case 106: // Terran Command Center
            case 111: // Terran Barracks
            case 113: // Terran Factory
            case 114: // Terran Starport
            case 116: // Terran Science Facility
            case 131: // Zerg Hatchery
            case 132: // Zerg Lair
            case 133: // Zerg Hive
            case 154: // Protoss Nexus
            case 160: // Protoss Gateway
            case 167: // Protoss Stargate
                return {4, 3};
            case 109: // Terran Supply Depot
            case 112: // Terran Academy
            case 122: // Terran Engineering Bay
            case 123: // Terran Armory
            case 125: // Terran Bunker
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
                return {3, 2};
            case 110: // Terran Refinery
            case 149: // Zerg Extractor
            case 157: // Protoss Assimilator
                return {4, 2};
            case 107: // Terran Comsat Station
            case 108: // Terran Nuclear Silo
            case 115: // Terran Control Tower
            case 117: // Terran Covert Ops
            case 118: // Terran Physics Lab
            case 120: // Terran Machine Shop
            case 124: // Terran Missile Turret
            case 137: // Zerg Greater Spire
            case 141: // Zerg Spire
            case 156: // Protoss Pylon
            case 162: // Protoss Photon Cannon
            case 172: // Protoss Shield Battery
                return {2, 2};
            default:
                return {3, 2};
        }
    }

    BuildingFootprintTiles BuildingFootprintSizeForDef(int BuildingDefID)
    {
        auto Def = Races::GetUnitDefByID(BuildingDefID);
        return BuildingFootprintSizeForChk(Def.ChkUnitID);
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

    bool RectsOverlap(const TileRect& A, const TileRect& B)
    {
        return !(A.Right < B.Left || A.Left > B.Right ||
                 A.Bottom < B.Top || A.Top > B.Bottom);
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

        const float Padding = std::max(0.0f, CollisionRadiusFromBounds(
            EM.GetComponent<PathfindingComponent>(BuilderID), 16.0f) - 1.0f);
        Core::Math::Vec2 Delta = BuilderTrans ? BuilderTrans->Position - BuildPosition
                                              : Core::Math::Vec2{0.0f, 1.0f};
        if (Delta.Length() < 0.001f)
            Delta = {0.0f, 1.0f};
        const float ClampedX = std::clamp(Delta.X, -Left, Right);
        const float ClampedY = std::clamp(Delta.Y, -Up, Down);

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
                               int BuildingDefID)
    {
        std::vector<Core::Math::Vec2> Candidates;
        BuildConstructionApproachCandidates(
            Candidates, EM, BuilderID, BuildingID, BuildPosition, BuildingDefID);

        for (const auto& Candidate : Candidates)
        {
            if (IsRecentFailedMoveTarget(
                    EM.GetComponent<PathfindingComponent>(BuilderID), Candidate))
            {
                continue;
            }
            if (CommandSystem::IssueMoveAndHasPath(EM, BuilderID, Candidate))
                return;
        }

        if (auto* Path = EM.GetComponent<PathfindingComponent>(BuilderID))
            Path->HasRecentMoveFailure = false;
        for (const auto& Candidate : Candidates)
        {
            if (CommandSystem::IssueMoveAndHasPath(EM, BuilderID, Candidate))
                return;
        }

        CommandSystem::IssueMove(
            EM,
            {BuilderID},
            ConstructionApproachPosition(EM, BuilderID, BuildingID, BuildPosition, BuildingDefID));
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
        Unit.HitPoints = UnderConstruction ? 1 : Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.UnitRace = Def.RaceType;
        Unit.SizeType = Def.SizeType;
        Unit.ShieldPoints = UnderConstruction ? 0 : Def.ShieldPoints;
        Unit.MaxShieldPoints = Def.ShieldPoints;
        Unit.EnergyPoints = UnderConstruction ? 0 : Def.EnergyPoints;
        Unit.MaxEnergyPoints = Def.EnergyPoints;
        Unit.Armor = Def.Armor;
        Unit.SightRange = Def.SightRange;
        Unit.SupplyCost = Def.SupplyCost;
        Unit.IsDetector = Races::IsDetectorChkUnitID(Unit.ChkUnitID);
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
        auto EntityIds =
            const_cast<Core::ECS::EntityManager&>(EM)
                .GetEntitiesWith<BuildingComponent, PlayerComponent>();
        for (int EntityID : EntityIds)
        {
            auto* Building =
                const_cast<Core::ECS::EntityManager&>(EM).GetComponent<BuildingComponent>(EntityID);
            auto* Player =
                const_cast<Core::ECS::EntityManager&>(EM).GetComponent<PlayerComponent>(EntityID);
            if (!Building || !Player || Player->PlayerID != PlayerID || Building->UnderConstruction)
                continue;
            if (ResolveDefID(EM, EntityID) == NormalizedRequiredDefID)
                return true;
        }
        return false;
    }

    bool IsBuilderAssignedToActiveConstruction(Core::ECS::EntityManager& EM, int BuilderID)
    {
        auto Buildings = EM.GetEntitiesWith<BuildingComponent, UnitComponent>();
        for (int EntityID : Buildings)
        {
            auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
            auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            if (!Building || !Unit)
                continue;
            if (!Building->UnderConstruction || Building->BuilderUnitID != BuilderID)
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

    bool HasPylonPowerAtPosition(const Core::ECS::EntityManager& EM,
                                 Core::Math::Vec2 Position,
                                 int PlayerID)
    {
        if (!gMap)
            return false;

        int TargetTileX = 0;
        int TargetTileY = 0;
        gMap->WorldToTile(Position, TargetTileX, TargetTileY);

        const int PylonDefID = Game::Races::GetUnitDefID("Pylon");
        const int PylonChkID = Game::Races::GetUnitDefByID(PylonDefID).ChkUnitID;
        auto Entities =
            const_cast<Core::ECS::EntityManager&>(EM)
                .GetEntitiesWith<TransformComponent, BuildingComponent, UnitComponent, PlayerComponent>();
        for (int EntityID : Entities)
        {
            auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
            auto* Transform = MutableEM.GetComponent<TransformComponent>(EntityID);
            auto* Building = MutableEM.GetComponent<BuildingComponent>(EntityID);
            auto* Unit = MutableEM.GetComponent<UnitComponent>(EntityID);
            auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
            if (!Transform || !Building || !Unit || !Player)
                continue;
            if (Player->PlayerID != PlayerID || Building->UnderConstruction || Unit->HitPoints <= 0)
                continue;
            if (Unit->ChkUnitID != PylonChkID)
                continue;

            int PylonTileX = 0;
            int PylonTileY = 0;
            gMap->WorldToTile(Transform->Position, PylonTileX, PylonTileY);
            const int Dy = std::abs(TargetTileY - PylonTileY);
            if (Dy > 3)
                continue;
            const int MaxDx = 4 - (Dy > 2 ? Dy - 2 : 0);
            if (std::abs(TargetTileX - PylonTileX) <= MaxDx)
                return true;
        }

        return false;
    }

    bool IsFootprintPlacementValid(Core::ECS::EntityManager& EM,
                                   int BuildingDefID,
                                   Core::Math::Vec2 Position,
                                   int IgnoreEntityID = -1)
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
            }
        }

        auto Entities = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
        for (auto EntityID : Entities)
        {
            if (EntityID == IgnoreEntityID)
                continue;

            auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
            auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            if (!Transform || !Unit || Unit->HitPoints <= 0)
                continue;

            auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
            if (Building && Building->IsLifted &&
                Building->FlightPhase != BuildingFlightPhase::Landing)
                continue;
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
                    if (ExistingPlayer->PlayerID == 11 &&
                        ExistingAddonDef.AddonOnBuildingDefID == BuildingDefID &&
                        (Transform->Position - ExpectedAddonPosition).LengthSq() <= 16.0f * 16.0f)
                    {
                        continue;
                    }
                }
            }
            if (!Building && Game::Data::IsAirUnitType(Unit->ChkUnitID))
                continue;

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
                return false;
        }

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
        for (int AddonID : EM.GetEntitiesWith<TransformComponent, BuildingComponent, PlayerComponent>())
        {
            if (AddonID == ParentBuildingID || !IsCompatibleAddon(EM, ParentBuildingID, AddonID))
                continue;
            auto* Addon = EM.GetComponent<BuildingComponent>(AddonID);
            auto* AddonPlayer = EM.GetComponent<PlayerComponent>(AddonID);
            auto* AddonTransform = EM.GetComponent<TransformComponent>(AddonID);
            if (!Addon || !AddonPlayer || !AddonTransform ||
                AddonPlayer->PlayerID != RequiredOwner ||
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
        auto* Addon = EM.GetComponent<BuildingComponent>(AddonID);
        if (!Addon || !Addon->IsAddon)
            return;

        Addon->BuilderUnitID = -1;
        Addon->FlightPhase = BuildingFlightPhase::LiftingOff;
        Addon->FlightAnimationComplete = false;
        if (auto* AddonPlayer = EM.GetComponent<PlayerComponent>(AddonID))
            AddonPlayer->PlayerID = 11;
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
        Building.RequiresCreep = Def.RequiresCreep;
        Building.RequiresPylon = Def.RequiresPylon;
        Building.HasPylonPower = !Def.RequiresPylon ||
            HasPylonPowerAtPosition(EM, Position, PlayerID);
        Building.BuildProgress = UnderConstruction ? 0.0f : (float)Def.BuildTime;

        auto& Unit = EM.AddComponent<UnitComponent>(EntityID);
        ApplyUnitDefinition(Unit, Def, UnderConstruction);

        auto& Player = EM.AddComponent<PlayerComponent>(EntityID);
        Player.PlayerID = PlayerID;

        auto& Path = EM.AddComponent<PathfindingComponent>(EntityID);
        ApplyCollisionBounds(Path, ResolveCollisionBounds(Def.ChkUnitID));

        if (!UnderConstruction && (Def.GroundDamage > 0 || Def.AirDamage > 0))
        {
            auto& Combat = EM.AddComponent<CombatComponent>(EntityID);
            Races::ApplyCombatDefinition(
                Combat, Def, Game::Data::IsAirUnitType(Unit.ChkUnitID));
        }

        if (UnderConstruction)
        {
            auto& Command = EM.AddComponent<CommandComponent>(EntityID);
            Command.CurrentCommand = CommandType::Build;
        }

        if (ParentBuildingID > 0 && EM.IsAlive(ParentBuildingID))
        {
            if (auto* Parent = EM.GetComponent<BuildingComponent>(ParentBuildingID))
                Parent->AddonID = EntityID;
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
            HasPylonPowerAtPosition(EM, Transform->Position, PlayerID);
        Building->BuildProgress = 0.0f;

        ApplyUnitDefinition(*Unit, Def, true);
        if (auto* Path = EM.GetComponent<PathfindingComponent>(TargetEntityID))
            ApplyCollisionBounds(*Path, ResolveCollisionBounds(Def.ChkUnitID));
        else
        {
            auto& NewPath = EM.AddComponent<PathfindingComponent>(TargetEntityID);
            ApplyCollisionBounds(NewPath, ResolveCollisionBounds(Def.ChkUnitID));
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

void BuildingSystem::SetImageResolver(ImageIDResolver Resolver)
{
    gImageResolver = std::move(Resolver);
}

void BuildingSystem::SetConstructionImageResolver(ImageIDResolver Resolver)
{
    gConstructionImageResolver = std::move(Resolver);
}

void BuildingSystem::SetCollisionBoundsResolver(CollisionBoundsResolver Resolver)
{
    gCollisionBoundsResolver = std::move(Resolver);
}

void BuildingSystem::SetAddonOffsetResolver(AddonOffsetResolver Resolver)
{
    gAddonOffsetResolver = std::move(Resolver);
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

    auto& Resources = HarvestSystem::GetResources(BuilderPlayerID);
    if (Resources.Minerals < Def.MineralCost || Def.GasCost > Resources.VespeneGas)
        return false;
    if (Def.RequiresPylon && !HasPylonPowerAtPosition(EM, Position, BuilderPlayerID))
        return false;
    return IsFootprintPlacementValid(EM, BuildingDefID, Position);
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
    auto& Resources = HarvestSystem::GetResources(PlayerID);
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
        if (BuilderBuilding->AddonID >= 0 && EM.IsAlive(BuilderBuilding->AddonID))
        {
            SetReason(OutReason, "parent already has an add-on");
            return false;
        }
    }
    else if (Def.RaceType == Race::Terran && BuilderDefID != Races::GetUnitDefID("SCV"))
    {
        SetReason(OutReason, "only SCV can place Terran buildings");
        return false;
    }
    else if (Def.RaceType == Race::Terran &&
             !Def.IsAddon &&
             IsBuilderAssignedToActiveConstruction(
                 const_cast<Core::ECS::EntityManager&>(EM), BuilderID))
    {
        SetReason(OutReason, "SCV is already constructing another building");
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
        return true;
    }

    if (!Def.IsAddon && CheckPlacement)
    {
        if (Target.Type != BuildTargetType::WorldPosition)
        {
            SetReason(OutReason, "building requires a map placement");
            return false;
        }
        if (Def.RequiresPylon && !HasPylonPowerAtPosition(EM, Target.Position, PlayerID))
        {
            SetReason(OutReason, "requires Pylon power");
            return false;
        }
        if (!IsFootprintPlacementValid(
                const_cast<Core::ECS::EntityManager&>(EM), BuildingDefID, Target.Position, BuilderID))
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
                                const BuildTarget& Target)
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
    auto& Resources = HarvestSystem::GetResources(PlayerID);
    Resources.Minerals -= Def.MineralCost;
    Resources.VespeneGas -= Def.GasCost;

    int EntityID = -1;
    if (IsGasBuildingDefID(BuildingDefID) && Target.Type == BuildTargetType::TargetEntity)
    {
        EntityID = ClaimRefineryTarget(EM, BuilderID, BuildingDefID, Target.TargetEntityID, PlayerID);
    }
    else
    {
        int ParentBuildingID = Def.IsAddon ? BuilderID : -1;
        EntityID = CreateBuildingEntity(EM, BuildingDefID, BuildPosition, PlayerID, true, BuilderID, ParentBuildingID);
    }

    if (!Def.IsAddon)
    {
        if (EntityID >= 0)
            IssueConstructionMove(EM, BuilderID, EntityID, BuildPosition, BuildingDefID);
        else
            CommandSystem::IssueMove(EM, {BuilderID}, BuildPosition);
    }

    Core::Utils::Logger::Info(
        "BuildingSystem",
        "Started building: " + Def.Name + " at (" +
        std::to_string((int)BuildPosition.X) + "," +
        std::to_string((int)BuildPosition.Y) + ")");
    return EntityID != -1;
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
    auto Entities = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
    for (auto ID : Entities)
    {
        ProcessFlightTransition(EM, ID);
        ProcessConstruction(EM, ID, DeltaTime);
    }
}

void BuildingSystem::ProcessFlightTransition(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
    if (!Building || !Building->FlightAnimationComplete)
        return;

    Building->FlightAnimationComplete = false;
    if (Building->IsAddon)
    {
        Building->FlightPhase = BuildingFlightPhase::Grounded;
        return;
    }

    if (Building->FlightPhase == BuildingFlightPhase::LiftingOff)
    {
        Building->FlightPhase = BuildingFlightPhase::Airborne;
        if (auto* Unit = EM.GetComponent<UnitComponent>(EntityID))
            Unit->Speed = 2.0f;
        Core::Utils::Logger::Info("BuildingSystem",
            "Building " + std::to_string(EntityID) + " completed lift-off animation");
        return;
    }

    if (Building->FlightPhase != BuildingFlightPhase::Landing)
        return;

    Building->IsLifted = false;
    Building->FlightPhase = BuildingFlightPhase::Grounded;
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

    const int AddonID = FindCompatibleAddonAtLanding(EM, EntityID, 11);
    if (AddonID >= 0)
        ConnectAddon(EM, EntityID, AddonID);

    Core::Utils::Logger::Info("BuildingSystem",
        "Building " + std::to_string(EntityID) + " completed landing animation");
}

void BuildingSystem::ProcessConstruction(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime)
{
    auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
    auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Building || !Building->UnderConstruction || !Transform || !Unit)
        return;

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
            const float Distance = DistanceToConstructionFootprint(
                EM,
                BuilderTransform->Position,
                EntityID,
                Transform->Position,
                Building->BuildingDefID);
            const float BuilderRadius = CollisionRadiusFromBounds(
                EM.GetComponent<PathfindingComponent>(Building->BuilderUnitID), 16.0f);
            BuilderNearby = Distance <= BuilderRadius + 2.0f;
            if (BuilderNearby)
            {
                if (auto* BuilderCommand =
                        EM.GetComponent<CommandComponent>(Building->BuilderUnitID))
                {
                    BuilderCommand->CurrentCommand = CommandType::Build;
                    BuilderCommand->TargetEntityID = EntityID;
                }
                // Reaching the footprint completes the approach order. Leaving
                // its last waypoint active lets MovementSystem carry the SCV
                // through the narrow build-distance threshold, producing an
                // endless 12/13-pixel approach loop with zero build progress.
                if (auto* BuilderPath =
                        EM.GetComponent<PathfindingComponent>(Building->BuilderUnitID))
                {
                    BuilderPath->Path.clear();
                    BuilderPath->PathIndex = 0;
                    BuilderPath->CurrentSpeed = 0.0f;
                    BuilderPath->TargetSpeed = 0.0f;
                    BuilderPath->CurrentVelocity = {0.0f, 0.0f};
                    BuilderPath->RequestedTargetEntityID = EntityID;
                    BuilderPath->RequestedDestination = Transform->Position;
                }
            }
            else
            {
                auto* BuilderPath =
                    EM.GetComponent<PathfindingComponent>(Building->BuilderUnitID);
                if (IsMoveSettled(BuilderPath))
                {
                    IssueConstructionMove(
                        EM,
                        Building->BuilderUnitID,
                        EntityID,
                        Transform->Position,
                        Building->BuildingDefID);
                }
            }
            if (DebugConstructionEnabled())
            {
                static int DebugConstructionLogs = 120;
                if (DebugConstructionLogs > 0)
                {
                    --DebugConstructionLogs;
                    auto* BuilderCommand =
                        EM.GetComponent<CommandComponent>(Building->BuilderUnitID);
                    auto* BuilderPath =
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

    if (BuilderNearby)
    {
        Building->BuildProgress += DeltaTime;
        int MaxHP = Unit->MaxHitPoints;
        Unit->HitPoints = std::max(1, (int)(MaxHP * (Building->BuildProgress / Building->TotalBuildTime)));
    }

    // OpenBW's Terran IncompleteBuilding order replaces the generic
    // construction GRP with the finished building graphic after 3/5 HP, then
    // runs AlmostBuilt on that graphic for the remaining construction time.
    if (Unit->UnitRace == Components::Race::Terran &&
        Building->TotalBuildTime > 0.0f &&
        Building->BuildProgress >= Building->TotalBuildTime * 0.6f)
    {
        if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
        {
            const uint32_t FinishedImageID = ResolveImageID(Unit->ChkUnitID);
            if (Render->ImageID != FinishedImageID)
            {
                Render->ImageID = FinishedImageID;
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

    Building->UnderConstruction = false;
    Building->BuildProgress = Building->TotalBuildTime;
    Unit->HitPoints = Unit->MaxHitPoints;
    Unit->ShieldPoints = Unit->MaxShieldPoints;
    Unit->EnergyPoints = Unit->MaxEnergyPoints;

    if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
    {
        Render->ImageID = ResolveImageID(Unit->ChkUnitID);
        Render->FrameIndex = 0;
        Render->TotalFrames = 1;
        Render->Tint = Core::Colors::White();
    }

    int PlayerID = 0;
    if (auto* Player = EM.GetComponent<PlayerComponent>(EntityID))
        PlayerID = Player->PlayerID;

    HarvestSystem::GetResources(PlayerID).SupplyMax +=
        Races::GetSupplyProvidedByDefID(Building->BuildingDefID);

    if (auto* BuilderCommand = EM.GetComponent<CommandComponent>(Building->BuilderUnitID))
        BuilderCommand->CurrentCommand = CommandType::Stop;

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

    DetachAddon(EM, BuildingID);
    Bldg->IsLifted = true;
    Bldg->FlightPhase = BuildingFlightPhase::LiftingOff;
    Bldg->FlightAnimationComplete = false;
    Unit->Speed = 0.0f;
    Unit->SupplyCost = 0; // Supply freed during flight
    if (auto* Render = EM.GetComponent<RenderComponent>(BuildingID))
        Render->Layer = RenderLayer::AirUnits;

    auto* PathPtr = EM.GetComponent<PathfindingComponent>(BuildingID);
    auto& Path = PathPtr ? *PathPtr : EM.AddComponent<PathfindingComponent>(BuildingID);
    Path.IsAir = true;
    Path.Path.clear();
    Path.PathIndex = 0;
    Path.CurrentSpeed = 0.0f;
    Path.TargetSpeed = 0.0f;
    Path.CurrentVelocity = {0.0f, 0.0f};

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

    if (!IsFootprintPlacementValid(EM, Bldg->BuildingDefID, Position, BuildingID))
        return false;

    Bldg->FlightPhase = BuildingFlightPhase::Landing;
    Bldg->FlightAnimationComplete = false;
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
    }
}

} // namespace Game::Systems
