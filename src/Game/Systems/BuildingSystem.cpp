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

    void SetReason(std::string* OutReason, const std::string& Reason)
    {
        if (OutReason)
            *OutReason = Reason;
    }

    uint32_t ResolveImageID(int ChkUnitID)
    {
        return gImageResolver ? gImageResolver(ChkUnitID) : (uint32_t)ChkUnitID;
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
            return Game::Races::GetTerranDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
    }

    bool HasCompletedBuilding(const Core::ECS::EntityManager& EM, int PlayerID, int RequiredDefID)
    {
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
            if (ResolveDefID(EM, EntityID) == RequiredDefID)
                return true;
        }
        return false;
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
        Render.Tint = UnderConstruction ? Color{255, 255, 255, 180} : Render::Colors::White();

        auto& Building = EM.AddComponent<BuildingComponent>(EntityID);
        Building.IsBuilding = true;
        Building.UnderConstruction = UnderConstruction;
        Building.BuilderUnitID = BuilderID;
        Building.BuildingDefID = BuildingDefID;
        Building.TotalBuildTime = (float)Def.BuildTime;
        Building.CanLiftOff = Def.CanLiftOff;
        Building.IsAddon = Def.IsAddon;
        Building.BuildProgress = UnderConstruction ? 0.0f : (float)Def.BuildTime;

        auto& Unit = EM.AddComponent<UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.HitPoints = UnderConstruction ? 1 : Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.UnitRace = Def.RaceType;
        Unit.SizeType = Def.SizeType;
        Unit.Armor = Def.Armor;
        Unit.SightRange = Def.SightRange;

        auto& Player = EM.AddComponent<PlayerComponent>(EntityID);
        Player.PlayerID = PlayerID;

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
        Building->BuildProgress = 0.0f;

        Unit->ChkUnitID = Def.ChkUnitID;
        Unit->HitPoints = 1;
        Unit->MaxHitPoints = Def.HitPoints;
        Unit->UnitRace = Def.RaceType;
        Unit->SizeType = Def.SizeType;
        Unit->Armor = Def.Armor;
        Unit->SightRange = Def.SightRange;

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

    int TileX = 0;
    int TileY = 0;
    int HalfSize = Def.SizeType == UnitSize::Large ? 2 : 1;
    gMap->WorldToTile(Position, TileX, TileY);
    TileX -= HalfSize;
    TileY -= HalfSize;

    for (int DY = 0; DY <= HalfSize * 2; DY++)
    {
        for (int DX = 0; DX <= HalfSize * 2; DX++)
        {
            int TX = TileX + DX;
            int TY = TileY + DY;
            if (!gMap->IsInBounds(TX, TY) || !gMap->IsBuildable(TX, TY))
                return false;

            auto Entities = EM.GetEntitiesWith<TransformComponent, BuildingComponent>();
            for (auto EntityID : Entities)
            {
                auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
                auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
                if (!Transform || !Building)
                    continue;
                int BuildingTileX = 0;
                int BuildingTileY = 0;
                gMap->WorldToTile(Transform->Position, BuildingTileX, BuildingTileY);
                if (std::abs(TX - BuildingTileX) < 2 && std::abs(TY - BuildingTileY) < 2)
                    return false;
            }
        }
    }

    return true;
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

    for (int RequiredDefID : Def.RequiredBuildings)
    {
        if (!HasCompletedBuilding(EM, PlayerID, RequiredDefID))
        {
            SetReason(OutReason, "required building missing");
            return false;
        }
    }

    const int RefineryDefID = Game::Races::GetUnitDefID("Refinery");
    if (!Def.IsAddon && BuildingDefID == RefineryDefID)
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
        if (TargetDefID == RefineryDefID)
        {
            SetReason(OutReason, "geyser already has a refinery");
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
    if (BuildingDefID == Game::Races::GetUnitDefID("Refinery") &&
        Target.Type == BuildTargetType::TargetEntity)
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
    auto* BuilderTransform = EM.GetComponent<TransformComponent>(Building->BuilderUnitID);
    if (Building->IsAddon && Building->BuilderUnitID >= 0)
    {
        BuilderNearby = true;
    }
    else if (BuilderTransform)
    {
        float Distance = Transform->Position.DistanceTo(BuilderTransform->Position);
        BuilderNearby = Distance < 48.0f;
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

    if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
        Render->Tint = Render::Colors::White();

    int PlayerID = 0;
    if (auto* Player = EM.GetComponent<PlayerComponent>(EntityID))
        PlayerID = Player->PlayerID;

    auto Def = Races::GetUnitDefByID(Building->BuildingDefID);
    HarvestSystem::GetResources(PlayerID).SupplyMax += Def.SupplyCost;

    if (auto* BuilderCommand = EM.GetComponent<CommandComponent>(Building->BuilderUnitID))
        BuilderCommand->CurrentCommand = CommandType::Stop;

    Core::Utils::Logger::Info("BuildingSystem",
        "Construction complete for entity " + std::to_string(EntityID));
}

} // namespace Game::Systems
