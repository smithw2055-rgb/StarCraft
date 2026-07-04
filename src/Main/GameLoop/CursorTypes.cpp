#include "CursorTypes.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"

#include <algorithm>

namespace Game::Client::GameLoop
{

const char* CursorName(GameCursorKind Kind)
{
    switch (Kind)
    {
        case GameCursorKind::Arrow: return "arrow";
        case GameCursorKind::Drag: return "drag";
        case GameCursorKind::Illegal: return "illegal";
        case GameCursorKind::TargetNeutral: return "targn";
        case GameCursorKind::TargetGreen: return "targg";
        case GameCursorKind::TargetRed: return "targr";
        case GameCursorKind::TargetYellow: return "targy";
        case GameCursorKind::MagnifierGreen: return "magg";
        case GameCursorKind::MagnifierRed: return "magr";
        case GameCursorKind::MagnifierYellow: return "magy";
        case GameCursorKind::ScrollUp: return "scrollu";
        case GameCursorKind::ScrollDown: return "scrolld";
        case GameCursorKind::ScrollLeft: return "scrolll";
        case GameCursorKind::ScrollRight: return "scrollr";
        case GameCursorKind::ScrollUpLeft: return "scrollul";
        case GameCursorKind::ScrollUpRight: return "scrollur";
        case GameCursorKind::ScrollDownLeft: return "scrolldl";
        case GameCursorKind::ScrollDownRight: return "scrolldr";
        case GameCursorKind::Time: return "time";
    }
    return "arrow";
}

float CursorAnimationFps(GameCursorKind Kind)
{
    switch (Kind)
    {
        case GameCursorKind::Arrow:
        case GameCursorKind::Drag:
        case GameCursorKind::Illegal:
            return 6.0f;
        case GameCursorKind::TargetNeutral:
        case GameCursorKind::TargetGreen:
        case GameCursorKind::TargetRed:
        case GameCursorKind::TargetYellow:
        case GameCursorKind::ScrollUp:
        case GameCursorKind::ScrollDown:
        case GameCursorKind::ScrollLeft:
        case GameCursorKind::ScrollRight:
        case GameCursorKind::ScrollUpLeft:
        case GameCursorKind::ScrollUpRight:
        case GameCursorKind::ScrollDownLeft:
        case GameCursorKind::ScrollDownRight:
            return 5.0f;
        case GameCursorKind::MagnifierGreen:
        case GameCursorKind::MagnifierRed:
        case GameCursorKind::MagnifierYellow:
            return 4.0f;
        case GameCursorKind::Time:
            return 0.0f;
    }
    return 0.0f;
}

void CursorHotspot(GameCursorKind Kind, float& OutX, float& OutY)
{
    switch (Kind)
    {
        case GameCursorKind::Arrow:
            OutX = 3.0f; OutY = 3.0f; return;
        case GameCursorKind::Drag:
            OutX = 8.0f; OutY = 8.0f; return;
        case GameCursorKind::Illegal:
            OutX = 8.0f; OutY = 8.0f; return;
        case GameCursorKind::TargetNeutral:
        case GameCursorKind::TargetGreen:
        case GameCursorKind::TargetRed:
        case GameCursorKind::TargetYellow:
            OutX = 9.0f; OutY = 8.0f; return;
        case GameCursorKind::MagnifierGreen:
        case GameCursorKind::MagnifierRed:
        case GameCursorKind::MagnifierYellow:
            OutX = 9.0f; OutY = 8.0f; return;
        case GameCursorKind::ScrollUp:
        case GameCursorKind::ScrollDown:
        case GameCursorKind::ScrollLeft:
        case GameCursorKind::ScrollRight:
        case GameCursorKind::ScrollUpLeft:
        case GameCursorKind::ScrollUpRight:
        case GameCursorKind::ScrollDownLeft:
        case GameCursorKind::ScrollDownRight:
            OutX = 7.0f; OutY = 8.0f; return;
        case GameCursorKind::Time:
            OutX = 11.0f; OutY = 5.0f; return;
        default:
            OutX = 8.0f; OutY = 8.0f; return;
    }
}

bool IsMineralUnit(int Id)
{
    return Id >= 176 && Id <= 178;
}

bool IsGeyserUnit(int Id)
{
    return Id == 188;
}

bool IsBuildingChkId(int Id)
{
    if ((Id >= 106 && Id <= 127) || (Id >= 130 && Id <= 152)) return true;
    if (Id >= 154 && Id <= 175) return true;
    if (Id >= 189 && Id <= 213) return true;
    return false;
}

bool IsOrganicBuilding(int Id)
{
    return Id >= 130 && Id <= 152;
}

bool RequiresPylonChkId(int Id)
{
    return Id >= 154 && Id <= 172 && Id != 157;
}

bool IsStartLocationUnit(int Id)
{
    return Id == 214;
}

bool IsRenderableChkUnit(int Id)
{
    switch (Id)
    {
        case 101: // Special_Map_Revealer
        case 191: // Unused_Zerg_Marker
        case 192: // Unused_Terran_Marker
        case 193: // Unused_Protoss_Marker
        case 214: // Special_Start_Location
            return false;
        default:
            return true;
    }
}

bool HasCombatSelection(Core::ECS::EntityManager& EM,
                        const std::vector<Core::ECS::EntityID>& EcsIds)
{
    for (int EntityId : EcsIds)
    {
        if (EM.GetComponent<Game::Components::CombatComponent>(EntityId))
            return true;
    }
    return false;
}

namespace
{
    bool IsAirTarget(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityId)
    {
        if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityId))
        {
            if (Path->IsAir)
                return true;
        }

        if (auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(EntityId))
            return Combat->MyElevation == Game::Components::Elevation::Air;

        return false;
    }
}

bool CanEntityAttackTarget(Core::ECS::EntityManager& EM,
                           Core::ECS::EntityID AttackerId,
                           Core::ECS::EntityID TargetId)
{
    auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(AttackerId);
    if (!Combat)
        return false;
    if (TargetId <= 0 || !EM.IsAlive(TargetId))
        return false;
    if (EM.GetComponent<Game::Components::ResourceComponent>(TargetId))
        return false;

    const bool TargetAir = IsAirTarget(EM, TargetId);
    if (TargetAir)
        return Combat->CanTargetAir && Combat->AirDamage > 0;
    return Combat->CanTargetGround && Combat->GroundDamage > 0;
}

bool HasAttackCapableSelection(Core::ECS::EntityManager& EM,
                               const std::vector<Core::ECS::EntityID>& EcsIds,
                               Core::ECS::EntityID TargetId)
{
    return std::any_of(EcsIds.begin(), EcsIds.end(), [&](Core::ECS::EntityID EntityId) {
        return CanEntityAttackTarget(EM, EntityId, TargetId);
    });
}

std::vector<Core::ECS::EntityID> FilterAttackersForTarget(
    Core::ECS::EntityManager& EM,
    const std::vector<Core::ECS::EntityID>& EcsIds,
    Core::ECS::EntityID TargetId)
{
    std::vector<Core::ECS::EntityID> Attackers;
    Attackers.reserve(EcsIds.size());
    for (Core::ECS::EntityID EntityId : EcsIds)
    {
        if (CanEntityAttackTarget(EM, EntityId, TargetId))
            Attackers.push_back(EntityId);
    }
    return Attackers;
}

std::vector<Core::ECS::EntityID> FilterHarvesters(Core::ECS::EntityManager& EM,
                                                   const std::vector<Core::ECS::EntityID>& EcsIds)
{
    std::vector<Core::ECS::EntityID> Harvesters;
    Harvesters.reserve(EcsIds.size());
    const int ScvDefId = Game::Races::GetUnitDefID("SCV");
    const int DroneDefId = Game::Races::GetUnitDefID("Drone");
    const int ProbeDefId = Game::Races::GetUnitDefID("Probe");
    for (int EntityId : EcsIds)
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId);
        if (!Unit) continue;
        const int UnitDefId = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        if (UnitDefId == ScvDefId || UnitDefId == DroneDefId || UnitDefId == ProbeDefId)
            Harvesters.push_back(EntityId);
    }
    return Harvesters;
}

} // namespace Game::Client::GameLoop
