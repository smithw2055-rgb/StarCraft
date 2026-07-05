#include "Game/Systems/BuildingSystem.h"

#include <cmath>
#include <utility>

#include "Core/Math/Vec2.h"
#include "Core/Utils/Logger.h"
#include "Game/Components.h"
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
    BuildingSystem::CollisionBoundsResolver gCollisionBoundsResolver;

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

    uint32_t ResolveImageID(int ChkUnitID)
    {
        return gImageResolver ? gImageResolver(ChkUnitID) : (uint32_t)ChkUnitID;
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

        auto Entities = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
        for (auto EntityID : Entities)
        {
            if (EntityID == IgnoreEntityID)
                continue;

            auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
            auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
            if (!Transform || !Building || Building->IsLifted)
                continue;

            const int ExistingDefID = ResolveDefID(EM, EntityID);
            if (ExistingDefID < 0)
                continue;

            const TileRect ExistingRect =
                BuildingTileRect(*gMap, Transform->Position, BuildingFootprintSizeForDef(ExistingDefID));
            if (RectsOverlap(CandidateRect, ExistingRect))
                return false;
        }

        return true;
    }

    Core::Math::Vec2 ComputeAddonPosition(const Core::ECS::EntityManager& EM, int ParentBuildingID)
    {
        auto* Transform =
            const_cast<Core::ECS::EntityManager&>(EM).GetComponent<TransformComponent>(ParentBuildingID);
        if (!Transform)
            return {};
        Core::Math::Vec2 Position = Transform->Position;
        Position.X += 64.0f;
        Position.Y += 16.0f;
        return Position;
    }

    Core::Math::Vec2 ResolveBuildPosition(const Core::ECS::EntityManager& EM,
                                          int BuilderID,
                                          int BuildingDefID,
                                          const BuildTarget& Target)
    {
        auto Def = Races::GetUnitDefByID(BuildingDefID);
        if (Def.IsAddon)
            return ComputeAddonPosition(EM, BuilderID);
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
        Render.ImageID = ResolveImageID(Def.ChkUnitID);
        Render.FrameIndex = 0;
        Render.TotalFrames = 1;
        Render.Layer = RenderLayer::Buildings;
        Render.Tint = UnderConstruction ? Color{255, 255, 255, 180} : Core::Colors::White();

        auto& Building = EM.AddComponent<BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.UnderConstruction = UnderConstruction;
        Building.BuilderUnitID = BuilderID;
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
        Render->ImageID = ResolveImageID(Def.ChkUnitID);
        Render->FrameIndex = 0;
        Render->TotalFrames = 1;
        Render->Layer = RenderLayer::Buildings;
        Render->Tint = Color{255, 255, 255, 180};

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

void BuildingSystem::SetCollisionBoundsResolver(CollisionBoundsResolver Resolver)
{
    gCollisionBoundsResolver = std::move(Resolver);
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
        if (!IsPlacementValid(const_cast<Core::ECS::EntityManager&>(EM), BuildingDefID, Target.Position, PlayerID))
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
        CommandSystem::IssueMove(EM, {BuilderID}, BuildPosition);

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

void BuildingSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f)
        return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
    for (auto ID : Entities)
        ProcessConstruction(EM, ID, DeltaTime);
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
            float Distance = Transform->Position.DistanceTo(BuilderTransform->Position);
            BuilderNearby = Distance < 48.0f;
        }
    }

    if (BuilderNearby)
    {
        Building->BuildProgress += DeltaTime;
        int MaxHP = Unit->MaxHitPoints;
        Unit->HitPoints = std::max(1, (int)(MaxHP * (Building->BuildProgress / Building->TotalBuildTime)));
    }

    if (Building->BuildProgress < Building->TotalBuildTime)
        return;

    Building->UnderConstruction = false;
    Building->BuildProgress = Building->TotalBuildTime;
    Unit->HitPoints = Unit->MaxHitPoints;
    Unit->ShieldPoints = Unit->MaxShieldPoints;
    Unit->EnergyPoints = Unit->MaxEnergyPoints;

    if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
        Render->Tint = Core::Colors::White();

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
    if (!Bldg || !Unit || !Bldg->CanLiftOff || Bldg->IsLifted) return false;
    if (Bldg->UnderConstruction) return false;

    Bldg->IsLifted = true;
    Unit->Speed = 2.0f; // Flying speed
    Unit->SupplyCost = 0; // Supply freed during flight
    if (auto* Render = EM.GetComponent<RenderComponent>(BuildingID))
        Render->Layer = RenderLayer::AirUnits;

    auto* PathPtr = EM.GetComponent<PathfindingComponent>(BuildingID);
    auto& Path = PathPtr ? *PathPtr : EM.AddComponent<PathfindingComponent>(BuildingID);
    Path.IsAir = true;
    Path.CurrentSpeed = 100.0f;

    Core::Utils::Logger::Info("BuildingSystem",
        "Building " + std::to_string(BuildingID) + " lifted off");
    return true;
}

bool BuildingSystem::LandBuilding(Core::ECS::EntityManager& EM, int BuildingID,
                                   Core::Math::Vec2 Position)
{
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    auto* Unit = EM.GetComponent<UnitComponent>(BuildingID);
    if (!Bldg || !Unit || !Bldg->IsLifted) return false;

    if (!IsFootprintPlacementValid(EM, Bldg->BuildingDefID, Position, BuildingID))
        return false;

    Bldg->IsLifted = false;
    Unit->Speed = 0.0f;
    Unit->SupplyCost = Game::Races::GetUnitDefByID(Bldg->BuildingDefID).SupplyCost;
    if (auto* Render = EM.GetComponent<RenderComponent>(BuildingID))
        Render->Layer = RenderLayer::Buildings;

    auto* Trans = EM.GetComponent<TransformComponent>(BuildingID);
    if (Trans) Trans->Position = Position;

    if (auto* Path = EM.GetComponent<PathfindingComponent>(BuildingID))
    {
        Path->IsAir = false;
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
    }

    Core::Utils::Logger::Info("BuildingSystem",
        "Building " + std::to_string(BuildingID) + " landed at (" +
        std::to_string((int)Position.X) + "," + std::to_string((int)Position.Y) + ")");
    return true;
}

} // namespace Game::Systems
