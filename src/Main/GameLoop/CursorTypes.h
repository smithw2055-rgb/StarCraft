#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/Systems/GeyserState.h"

namespace Game::Client { enum class EntityKind : uint8_t; }

namespace Game::Client::GameLoop
{

struct ClickTargetContext
{
    Core::Math::Vec2 WorldPosition;
    int ResourceEntityID = -1;
    int AnyEntityID = -1;
    int EnemyEntityID = -1;
};

enum class GameCursorKind
{
    Arrow,
    Drag,
    Illegal,
    TargetNeutral,
    TargetGreen,
    TargetRed,
    TargetYellow,
    MagnifierGreen,
    MagnifierRed,
    MagnifierYellow,
    ScrollUp,
    ScrollDown,
    ScrollLeft,
    ScrollRight,
    ScrollUpLeft,
    ScrollUpRight,
    ScrollDownLeft,
    ScrollDownRight,
    Time
};

struct CursorRenderState
{
    GameCursorKind Kind = GameCursorKind::Arrow;
    bool UseGameCursor = false;
    float HotspotX = 0.0f;
    float HotspotY = 0.0f;
};

inline const char* CursorName(GameCursorKind Kind)
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

inline float CursorAnimationFps(GameCursorKind Kind)
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

inline void CursorHotspot(GameCursorKind Kind, float& OutX, float& OutY)
{
    (void)Kind;
    // Original cursor GRPs keep every frame on a 128x128 canvas; their opaque
    // pixels carry per-frame offsets around the canvas center. The hotspot is
    // therefore (64,64), not a coordinate relative to the cropped opaque
    // pixels. Small values such as (8,8) displaced the visible cursor roughly
    // fifty pixels down/right from the actual click and drag rectangle.
    OutX = 64.0f;
    OutY = 64.0f;
}

inline bool IsMineralUnit(int Id)
{
    return Id >= 176 && Id <= 178;
}

inline bool IsGeyserUnit(int Id)
{
    return Id == 188;
}

inline bool IsBuildingChkId(int Id)
{
    if ((Id >= 106 && Id <= 127) || (Id >= 130 && Id <= 152)) return true;
    if (Id >= 154 && Id <= 175) return true;
    if (Id >= 189 && Id <= 213) return true;
    return false;
}

inline bool IsOrganicBuilding(int Id)
{
    return Id >= 130 && Id <= 152;
}

inline bool RequiresPylonChkId(int Id)
{
    // Nexus and Assimilator are the two Protoss structures that may be
    // placed without an existing power field. A Pylon must also be exempt,
    // otherwise the first power field could never be established.
    return Id >= 154 && Id <= 172 &&
           Id != 154 && // Nexus
           Id != 156 && // Pylon
           Id != 157;   // Assimilator
}

inline bool IsStartLocationUnit(int Id)
{
    return Id == 214;
}

inline bool IsRenderableChkUnit(int Id)
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

inline bool HasCombatSelection(Core::ECS::EntityManager& EM,
                               const std::vector<Core::ECS::EntityID>& EcsIds)
{
    for (int EntityId : EcsIds)
    {
        if (EM.GetComponent<Game::Components::CombatComponent>(EntityId))
            return true;
    }
    return false;
}

namespace Detail
{
inline bool IsAirTarget(Core::ECS::EntityManager& EM, Core::ECS::EntityID EntityId)
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
} // namespace Detail

inline bool CanEntityAttackTarget(Core::ECS::EntityManager& EM,
                                  Core::ECS::EntityID AttackerId,
                                  Core::ECS::EntityID TargetId)
{
    auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(AttackerId);
    if (!Combat)
        return false;
    if (TargetId <= 0 || !EM.IsAlive(TargetId))
        return false;
    if (Game::Systems::IsProtectedResourceNode(EM, TargetId))
        return false;

    const bool TargetAir = Detail::IsAirTarget(EM, TargetId);
    if (TargetAir)
        return Combat->CanTargetAir && Combat->AirDamage > 0;
    return Combat->CanTargetGround && Combat->GroundDamage > 0;
}

inline bool HasAttackCapableSelection(Core::ECS::EntityManager& EM,
                                      const std::vector<Core::ECS::EntityID>& EcsIds,
                                      Core::ECS::EntityID TargetId)
{
    return std::any_of(EcsIds.begin(), EcsIds.end(), [&](Core::ECS::EntityID EntityId) {
        return CanEntityAttackTarget(EM, EntityId, TargetId);
    });
}

inline std::vector<Core::ECS::EntityID> FilterAttackersForTarget(
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

inline std::vector<Core::ECS::EntityID> FilterHarvesters(
    Core::ECS::EntityManager& EM,
    const std::vector<Core::ECS::EntityID>& EcsIds)
{
    std::vector<Core::ECS::EntityID> Harvesters;
    Harvesters.reserve(EcsIds.size());
    for (int EntityId : EcsIds)
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId);
        if (!Unit) continue;
        // CHK unit IDs: Terran SCV 7, Zerg Drone 41, Protoss Probe 64.
        const bool MatchesRawWorkerId =
            Unit->ChkUnitID == 7 || Unit->ChkUnitID == 41 || Unit->ChkUnitID == 64;
        if (MatchesRawWorkerId)
            Harvesters.push_back(EntityId);
    }
    return Harvesters;
}

} // namespace Game::Client::GameLoop
