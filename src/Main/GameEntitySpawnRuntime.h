#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Main/GameLoop/CursorTypes.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop::GameEntitySpawnRuntime
{

inline constexpr int NormalizeChkPlayerID(uint8_t ChkOwner)
{
    // CHK UNIT owner is already a zero-based StarCraft player id (0..11).
    // Keep it as data here; callers that index arrays must bounds-check there.
    return (int)ChkOwner;
}

static_assert(NormalizeChkPlayerID(1) == 1,
              "CHK owner values are data ids, not one-based display labels.");

template <typename ResolveImageIdT, typename ResolveSubunitImageIdT, typename ResolveCollisionBoundsT>
inline void SpawnChkEntities(GameSession& Session,
                             ResolveImageIdT&& ResolveImageID,
                             ResolveSubunitImageIdT&& ResolveSubunitImageID,
                             ResolveCollisionBoundsT&& ResolveCollisionBounds)
{
    auto& EM = Session.Entities();
    for (const auto& UnitPlacement : Session.GetChkUnits())
    {
        if (!IsRenderableChkUnit(UnitPlacement.UnitID))
            continue;

        int EntityID = EM.CreateEntity();
        auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
        Trans.Position = {(float)UnitPlacement.X, (float)UnitPlacement.Y};

        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = UnitPlacement.UnitID;
        Unit.HitPoints = 60;
        Unit.MaxHitPoints = 60;
        Unit.Speed = IsMineralUnit(UnitPlacement.UnitID) || IsGeyserUnit(UnitPlacement.UnitID) ? 0.0f : 2.8f;
        Unit.SightRange = 8;

        const int UnitDefID = Game::Races::GetUnitDefIDByChkUnitID(UnitPlacement.UnitID);
        if (UnitDefID >= 0)
        {
            const auto Def = Game::Races::GetUnitDefByID(UnitDefID);
            Unit.UnitRace = Def.RaceType;
            Unit.SizeType = Def.SizeType;
            Unit.Armor = Def.Armor;
            Unit.Speed = Def.Speed;
            Unit.SightRange = Def.SightRange;
            Unit.SupplyCost = Def.SupplyCost;
            Unit.MaxHitPoints = Def.HitPoints;
            Unit.HitPoints = Def.HitPoints;
            if (UnitPlacement.HpPercent > 0 && UnitPlacement.HpPercent < 100)
                Unit.HitPoints = std::max(1, Unit.MaxHitPoints * (int)UnitPlacement.HpPercent / 100);
            Unit.ShieldPoints = Def.ShieldPoints;
            Unit.MaxShieldPoints = Def.ShieldPoints;
            Unit.EnergyPoints = Def.EnergyPoints;
            Unit.MaxEnergyPoints = Def.EnergyPoints;
        }
        else if (IsBuildingChkId(UnitPlacement.UnitID) ||
                 IsMineralUnit(UnitPlacement.UnitID) ||
                 IsGeyserUnit(UnitPlacement.UnitID))
        {
            Unit.SizeType = Game::Components::UnitSize::Large;
            Unit.MaxHitPoints = IsGeyserUnit(UnitPlacement.UnitID) ? 500 :
                IsMineralUnit(UnitPlacement.UnitID) ? 1 : 1000;
            Unit.HitPoints = Unit.MaxHitPoints;
        }

        auto& Player = EM.AddComponent<Game::Components::PlayerComponent>(EntityID);
        Player.PlayerID = NormalizeChkPlayerID(UnitPlacement.Owner);
        Game::Systems::UpgradeSystem::RefreshUnitEnergyCap(EM, EntityID);
        if (UnitPlacement.EnergyPercent > 0 && Unit.MaxEnergyPoints > 0)
        {
            Unit.EnergyPoints =
                Unit.MaxEnergyPoints * std::min(100, (int)UnitPlacement.EnergyPercent) / 100;
        }
        Unit.HangarCount = std::max(0, (int)UnitPlacement.HangarCount);

        auto& RenderComp = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        RenderComp.ImageID = (uint32_t)ResolveImageID(UnitPlacement.UnitID);
        RenderComp.SubunitImageID = (uint32_t)ResolveSubunitImageID(UnitPlacement.UnitID);
        RenderComp.FrameIndex = 0;
        RenderComp.TotalFrames = 1;
        RenderComp.Layer = IsBuildingChkId(UnitPlacement.UnitID) ?
            Game::Components::RenderLayer::Buildings :
            Game::Components::RenderLayer::Units;

        const bool CanBurrow = Game::Systems::ZergSystem::CanUnitBurrow(
            UnitPlacement.UnitID, RenderComp.ImageID);
        if (CanBurrow)
        {
            auto& Burrow = EM.AddComponent<Game::Components::BurrowComponent>(EntityID);
            Burrow.CanBurrow = true;
        }

        if ((UnitPlacement.ValidFlags & 0x10) != 0)
            Unit.IsInvincible = (UnitPlacement.StateFlags & 0x10) != 0;
        if ((UnitPlacement.ValidFlags & 0x08) != 0 &&
            (UnitPlacement.StateFlags & 0x08) != 0)
        {
            auto& Status = EM.AddComponent<Game::Components::StatusEffectComponent>(EntityID);
            Status.AddEffect(Game::Components::StatusEffect::Hallucination, 1000000.0f, -1);
        }
        if ((UnitPlacement.ValidFlags & 0x03) != 0)
        {
            auto& Cloak = EM.AddComponent<Game::Components::CloakComponent>(EntityID);
            Cloak.IsCloaked = (UnitPlacement.ValidFlags & 0x01) != 0 &&
                              (UnitPlacement.StateFlags & 0x01) != 0;
            Cloak.IsBurrowed = CanBurrow &&
                               (UnitPlacement.ValidFlags & 0x02) != 0 &&
                               (UnitPlacement.StateFlags & 0x02) != 0;
            if (Cloak.IsBurrowed)
            {
                auto* Burrow = EM.GetComponent<Game::Components::BurrowComponent>(EntityID);
                Burrow->IsBurrowed = true;
                Burrow->Phase = Game::Components::BurrowPhase::Burrowed;
                Cloak.IsCloaked = true;
            }
        }

        if (UnitDefID >= 0)
        {
            const auto Def = Game::Races::GetUnitDefByID(UnitDefID);
            if (Def.GroundDamage > 0 || Def.AirDamage > 0)
            {
                auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
                Game::Races::ApplyCombatDefinition(
                    Combat, Def, Game::Data::IsAirUnitType(Unit.ChkUnitID));
            }
        }

        if (IsMineralUnit(UnitPlacement.UnitID))
        {
            auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
            Res.Type = Game::Components::ResourceType::Minerals;
            Res.Amount = UnitPlacement.ResourceAmount > 0 ? (int)UnitPlacement.ResourceAmount : 1500;
            Res.MaxAmount = Res.Amount;
            auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
            Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(UnitPlacement.UnitID));
        }
        else if (IsGeyserUnit(UnitPlacement.UnitID))
        {
            auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
            Res.Type = Game::Components::ResourceType::VespeneGas;
            Res.Amount = UnitPlacement.ResourceAmount > 0 ? (int)UnitPlacement.ResourceAmount : 5000;
            Res.MaxAmount = Res.Amount;
            auto& Build = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
            Build.IsBuilding = true;
            Build.UnderConstruction = false;
            auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
            Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(UnitPlacement.UnitID));
        }
        else if (IsBuildingChkId(UnitPlacement.UnitID))
        {
            auto& Build = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
            Build.IsBuilding = true;
            Build.UnderConstruction = false;
            Build.IsOrganic = IsOrganicBuilding(UnitPlacement.UnitID);
            Build.RequiresPylon = RequiresPylonChkId(UnitPlacement.UnitID);
            Build.TotalBuildTime = 120.0f;
            Build.BuildProgress = Build.TotalBuildTime;
            Build.BuildingDefID = UnitDefID;
            if (UnitDefID >= 0)
            {
                const auto Def = Game::Races::GetUnitDefByID(UnitDefID);
                Build.CanLiftOff = Def.CanLiftOff;
                Build.IsAddon = Def.IsAddon;
            }
            if (UnitDefID == Game::Races::GetUnitDefID("Refinery"))
            {
                auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
                Res.Type = Game::Components::ResourceType::VespeneGas;
                Res.Amount = UnitPlacement.ResourceAmount > 0 ? (int)UnitPlacement.ResourceAmount : 5000;
                Res.MaxAmount = Res.Amount;
            }
            auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
            Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(UnitPlacement.UnitID));
        }
        else
        {
            auto& Cmd = EM.AddComponent<Game::Components::CommandComponent>(EntityID);
            Cmd.CurrentCommand = Game::Components::CommandType::Stop;
            auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
            Path.NodeTolerance = 8.0f;
            Path.IsAir = Game::Data::IsAirUnitType(Unit.ChkUnitID);
            Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(UnitPlacement.UnitID));
        }
    }
    Game::Systems::BuildingSystem::RestoreSpawnedAddonLinks(EM);
}

template <typename ResolveImageIdT, typename ResolveCollisionBoundsT>
inline int SpawnResourceEntity(GameSession& Session,
                               int ChkUnitId,
                               int WorldX,
                               int WorldY,
                               int Amount,
                               ResolveImageIdT&& ResolveImageID,
                               ResolveCollisionBoundsT&& ResolveCollisionBounds)
{
    auto& EM = Session.Entities();
    int EntityID = EM.CreateEntity();
    auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
    Trans.Position = {(float)WorldX, (float)WorldY};
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
    Unit.ChkUnitID = ChkUnitId;
    Unit.SizeType = Game::Components::UnitSize::Large;
    Unit.HitPoints = IsGeyserUnit(ChkUnitId) ? 500 : 1;
    Unit.MaxHitPoints = Unit.HitPoints;
    Unit.Speed = 0.0f;
    auto& Player = EM.AddComponent<Game::Components::PlayerComponent>(EntityID);
    Player.PlayerID = 11;
    auto& RenderComp = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
    RenderComp.ImageID = (uint32_t)ResolveImageID(ChkUnitId);
    RenderComp.TotalFrames = 1;
    RenderComp.Layer = Game::Components::RenderLayer::Units;
    auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
    Res.Type = IsGeyserUnit(ChkUnitId) ?
        Game::Components::ResourceType::VespeneGas :
        Game::Components::ResourceType::Minerals;
    Res.Amount = Amount;
    Res.MaxAmount = Amount;
    auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
    Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(ChkUnitId));
    if (IsGeyserUnit(ChkUnitId))
    {
        auto& Build = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Build.IsBuilding = true;
        Build.UnderConstruction = false;
    }
    return EntityID;
}

inline void SpawnTerranAuditFormation(GameSession& Session, Core::Math::Vec2 CameraPosition)
{
    auto& EM = Session.Entities();
    const auto& TerranDefs = Game::Races::GetTerranDefs();
    float BaseX = CameraPosition.X - 240.0f;
    float BaseY = CameraPosition.Y - 160.0f;
    int UnitCol = 0;
    int BuildingCol = 0;
    for (size_t I = 0; I < TerranDefs.size(); ++I)
    {
        const auto& Def = TerranDefs[I];
        if (Def.IsBuilding)
        {
            Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(EM, (int)I,
                {BaseX + (float)(BuildingCol % 6) * 96.0f,
                 BaseY + 180.0f + (float)(BuildingCol / 6) * 96.0f}, 0);
            ++BuildingCol;
        }
        else
        {
            Game::Systems::ProductionSystem::DebugSpawnUnit(EM, (int)I,
                {BaseX + (float)(UnitCol % 6) * 72.0f,
                 BaseY + (float)(UnitCol / 6) * 72.0f}, 0);
            ++UnitCol;
        }
    }
}

inline void SpawnTerranAuditFormationOnce(GameSession& Session, Core::Math::Vec2 CameraPosition)
{
    if (!Session.IsEcsInitialized() || Session.HasTerranAuditSpawned())
        return;

    SpawnTerranAuditFormation(Session, CameraPosition);
    Session.MarkTerranAuditSpawned();
}

} // namespace Game::Client::GameLoop::GameEntitySpawnRuntime
