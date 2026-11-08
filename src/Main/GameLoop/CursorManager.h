#pragma once

// === Consolidated from src/Main/GameLoop/EcsSync.h ===
#include <cstdint>
#include <vector>
#include "Client/Selection.h"
#include "Core/ECS/EntityManager.h"

namespace Game::Client::GameLoop
{

inline std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(
    Core::ECS::EntityManager& EM,
    const std::vector<int>& SelectedIds)
{
    std::vector<Core::ECS::EntityID> EcsIds;
    EcsIds.reserve(SelectedIds.size());
    for (int SelectedId : SelectedIds)
    {
        if (EM.IsAlive(SelectedId))
            EcsIds.push_back(SelectedId);
    }
    return EcsIds;
}

inline std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(
    Core::ECS::EntityManager& EM,
    const Game::Client::Selection& Selector)
{
    return CollectSelectedAliveEcsIds(EM, Selector.GetSelected());
}

} // namespace Game::Client::GameLoop



// === Consolidated from src/Main/GameLoop/CursorTypes.h ===
#include <algorithm>
#include <cstdint>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/Systems/HarvestSystem.h"

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

// === Consolidated from src/Main/GameLoop/CursorTargeting.h ===
#include <algorithm>
#include <limits>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"


namespace Game::Client::GameLoop
{

struct ResourceClickFootprint
{
    float HalfW = 32.0f;
    float HalfH = 24.0f;
};

inline ResourceClickFootprint ResourceClickFootprintFor(
    Game::Components::ResourceType Type,
    int ChkUnitID)
{
    if (Type == Game::Components::ResourceType::VespeneGas || IsGeyserUnit(ChkUnitID))
        return {70.0f, 48.0f};

    if (Type == Game::Components::ResourceType::Minerals || IsMineralUnit(ChkUnitID))
        return {50.0f, 34.0f};

    return {32.0f, 24.0f};
}

inline float ResourceClickScore(const Core::Math::Vec2& ResourceCenter,
                                const Core::Math::Vec2& ClickPosition,
                                Game::Components::ResourceType Type,
                                int ChkUnitID)
{
    const ResourceClickFootprint Footprint = ResourceClickFootprintFor(Type, ChkUnitID);
    const float DX = ClickPosition.X - ResourceCenter.X;
    const float DY = ClickPosition.Y - ResourceCenter.Y;
    const float NX = DX / std::max(1.0f, Footprint.HalfW);
    const float NY = DY / std::max(1.0f, Footprint.HalfH);
    // Resource sprites fill a rectangular command footprint. The old ellipse
    // rejected visible corner pixels, so a right click there degraded to a
    // normal move and grouped workers piled up at the mineral edge.
    const float RectScore = std::max(NX * NX, NY * NY);
    if (RectScore > 1.0f)
        return std::numeric_limits<float>::max();
    return RectScore;
}

inline float EntityClickScore(Core::ECS::EntityManager& EM,
                              Core::ECS::EntityID EntityID,
                              const Core::Math::Vec2& ClickPosition)
{
    auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
    auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
    if (!Transform || !Unit || Unit->HitPoints <= 0)
        return std::numeric_limits<float>::max();

    float Left = -16.0f;
    float Top = -16.0f;
    float Right = 16.0f;
    float Bottom = 16.0f;

    if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
        Path && Path->HasCollisionBounds)
    {
        Left = -(float)Path->CollisionLeft;
        Top = -(float)Path->CollisionUp;
        Right = (float)Path->CollisionRight + 1.0f;
        Bottom = (float)Path->CollisionDown + 1.0f;
    }
    else
    {
        switch (Unit->SizeType)
        {
            case Game::Components::UnitSize::Small:
                Left = Top = -14.0f;
                Right = Bottom = 14.0f;
                break;
            case Game::Components::UnitSize::Medium:
                Left = Top = -20.0f;
                Right = Bottom = 20.0f;
                break;
            case Game::Components::UnitSize::Large:
                Left = Top = -28.0f;
                Right = Bottom = 28.0f;
                break;
        }
    }

    const float WorldLeft = Transform->Position.X + Left;
    const float WorldTop = Transform->Position.Y + Top;
    const float WorldRight = Transform->Position.X + Right;
    const float WorldBottom = Transform->Position.Y + Bottom;
    if (ClickPosition.X < WorldLeft || ClickPosition.X > WorldRight ||
        ClickPosition.Y < WorldTop || ClickPosition.Y > WorldBottom)
    {
        return std::numeric_limits<float>::max();
    }

    const float CenterX = (WorldLeft + WorldRight) * 0.5f;
    const float CenterY = (WorldTop + WorldBottom) * 0.5f;
    const float DX = ClickPosition.X - CenterX;
    const float DY = ClickPosition.Y - CenterY;
    const float AreaPenalty = (WorldRight - WorldLeft) * (WorldBottom - WorldTop) * 0.00025f;
    return DX * DX + DY * DY + AreaPenalty;
}

} // namespace Game::Client::GameLoop


namespace Render { class Camera2D; class Draw2D; }
namespace Game::Client { class CameraController; class Selection; }
namespace Game::Client::UI { class Hud; }
namespace Resource::Graphics { class UIAssetCacheView; }

namespace Game::Client::GameLoop
{

class CursorManager
{
public:
    CursorManager() = default;

    GameCursorKind ResolveScrollCursorKind(int MouseX, int MouseY,
                                            int ViewportWidth, int ViewportHeight,
                                            const Game::Client::UI::Hud& Hud);

    ClickTargetContext ResolveClickTargetContext(Core::ECS::EntityManager& EM,
                                                  const std::vector<Core::ECS::EntityID>& SelectedIds,
                                                  const Core::Math::Vec2& WorldPosition);

    CursorRenderState ResolveCursorState(int MouseX, int MouseY,
                                          bool EcsInitialized,
                                          Core::ECS::EntityManager* EM,
                                          const Render::Camera2D& Camera,
                                          const Selection& Selector,
                                          const Game::Client::UI::Hud& Hud);

    void RenderCursor(int MouseX, int MouseY,
                       const Render::Camera2D& Camera,
                       const Selection& Selector,
                       const Game::Client::UI::Hud& Hud,
                       Core::ECS::EntityManager* EM,
                       bool EcsInitialized,
                       const Resource::Graphics::UIAssetCacheView& UIAssets,
                       Render::Draw2D& Draw2D);

private:
    bool IsMapInteractionPoint(int ScreenX, int ScreenY,
                                int ViewportWidth, int ViewportHeight,
                                const Game::Client::UI::Hud& Hud,
                                int MapWidth, int MapHeight);
};

} // namespace Game::Client::GameLoop

// === Inlined from src/Main/GameLoop/CursorManager.cpp ===
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"


#include "Core/Platform/Platform.h"
#include "Render/Camera2D.h"
#include "Render/Draw2D.h"
#include "Core/Color.h"
#include "Client/EntityRenderer.h"
#include "Client/Selection.h"
#include "UI/Hud.h"
#include "Resource/UIAssetCacheView.h"
#include "Game/Components.h"
#include "Game/Systems/HarvestSystem.h"
#include "Core/Math/Vec2.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Game::Client::GameLoop
{

inline bool CursorManager::IsMapInteractionPoint(int ScreenX, int ScreenY,
                                           int ViewportWidth, int ViewportHeight,
                                           const Game::Client::UI::Hud& Hud,
                                           int MapWidth, int MapHeight)
{
    if (ScreenX < 0 || ScreenX >= ViewportWidth || ScreenY < 0 || ScreenY >= ViewportHeight)
        return false;
    if (Hud.IsPointInHudBand(ScreenX, ScreenY, ViewportWidth, ViewportHeight))
        return false;

    float DummyWX = 0.0f;
    float DummyWY = 0.0f;
    if (Hud.CheckMinimapClick(ScreenX, ScreenY, ViewportWidth, ViewportHeight,
                               MapWidth, MapHeight, DummyWX, DummyWY))
    {
        return false;
    }

    return Hud.CheckCommandPanelClick(ScreenX, ScreenY, ViewportWidth, ViewportHeight) < 0;
}

inline GameCursorKind CursorManager::ResolveScrollCursorKind(int MouseX, int MouseY,
                                                       int ViewportWidth, int ViewportHeight,
                                                       const Game::Client::UI::Hud& Hud)
{
    constexpr int EdgeMargin = 16;
    const int TopBlocked = Hud.GetResourceBarHeight();
    const int BottomBlocked = Hud.GetHudHeight(ViewportWidth, ViewportHeight);
    const int ActiveBottom = ViewportHeight - BottomBlocked;

    const bool AnyMouseButtonDown =
        Core::Platform::IsMouseButtonDown(0) ||
        Core::Platform::IsMouseButtonDown(1) ||
        Core::Platform::IsMouseButtonDown(2);

    if (!Core::Platform::IsWindowFocused() ||
        !Core::Platform::IsMouseInGameContentArea() ||
        AnyMouseButtonDown ||
        MouseX < 0 || MouseX >= ViewportWidth ||
        MouseY < 0 || MouseY >= ViewportHeight)
    {
        return GameCursorKind::Arrow;
    }

    const bool Left = MouseX < EdgeMargin;
    const bool Right = MouseX >= ViewportWidth - EdgeMargin;
    const bool Up = (MouseY >= TopBlocked && MouseY < TopBlocked + EdgeMargin) || (MouseY <= 4);
    const bool Down = (MouseY > ActiveBottom - EdgeMargin && MouseY <= ActiveBottom + 8) || (MouseY >= ViewportHeight - 8);

    if (Up && Left) return GameCursorKind::ScrollUpLeft;
    if (Up && Right) return GameCursorKind::ScrollUpRight;
    if (Down && Left) return GameCursorKind::ScrollDownLeft;
    if (Down && Right) return GameCursorKind::ScrollDownRight;
    if (Up) return GameCursorKind::ScrollUp;
    if (Down) return GameCursorKind::ScrollDown;
    if (Left) return GameCursorKind::ScrollLeft;
    if (Right) return GameCursorKind::ScrollRight;
    return GameCursorKind::Arrow;
}

inline ClickTargetContext CursorManager::ResolveClickTargetContext(
    Core::ECS::EntityManager& EM,
    const std::vector<Core::ECS::EntityID>& SelectedIds,
    const Core::Math::Vec2& WorldPosition)
{
    ClickTargetContext Context;
    Context.WorldPosition = WorldPosition;

    int MyPlayerId = 0;
    if (!SelectedIds.empty())
    {
        if (auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(SelectedIds.front()))
            MyPlayerId = Player->PlayerID;
    }

    float BestResourceScore = 1.0f;
    for (int EntityId : EM.GetEntitiesWithCached<
        Game::Components::TransformComponent,
        Game::Components::ResourceComponent>())
    {
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityId);
        if (!Transform || !Resource || Resource->IsDepleted) continue;
        if (Game::Systems::IsAttackableClaimedGeyser(EM, EntityId))
        {
            const auto* Player =
                EM.GetComponent<Game::Components::PlayerComponent>(EntityId);
            if (Player && Player->PlayerID != MyPlayerId)
                continue;
        }

        int ChkUnitID = 0;
        if (auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId))
            ChkUnitID = Unit->ChkUnitID;

        const float Score =
            ResourceClickScore(Transform->Position, WorldPosition, Resource->Type, ChkUnitID);
        if (Score < BestResourceScore)
        {
            BestResourceScore = Score;
            Context.ResourceEntityID = EntityId;
        }
    }

    float BestEntityScore = std::numeric_limits<float>::max();
    static thread_local std::vector<int> sCursorHoverCandidates;
    auto& SpatialRes = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
    SpatialRes.AllUnitsHash.QueryInto(WorldPosition.X - 256.0f, WorldPosition.Y - 256.0f, WorldPosition.X + 256.0f, WorldPosition.Y + 256.0f, sCursorHoverCandidates);
    std::sort(sCursorHoverCandidates.begin(), sCursorHoverCandidates.end());
    for (int EntityId : sCursorHoverCandidates)
    {
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityId);
        if (!Transform || !Unit || !Player || Unit->HitPoints <= 0) continue;
        if (EM.GetComponent<Game::Components::ResourceComponent>(EntityId) &&
            !Game::Systems::IsAttackableClaimedGeyser(EM, EntityId))
        {
            continue;
        }

        const float Score = EntityClickScore(EM, EntityId, WorldPosition);
        if (Score < BestEntityScore)
        {
            BestEntityScore = Score;
            Context.AnyEntityID = EntityId;
            Context.EnemyEntityID = (Player->PlayerID != MyPlayerId) ? EntityId : -1;
        }
    }

    return Context;
}

inline CursorRenderState CursorManager::ResolveCursorState(int MouseX, int MouseY,
                                                     bool EcsInitialized,
                                                     Core::ECS::EntityManager* EM,
                                                     const Render::Camera2D& Camera,
                                                     const Selection& Selector,
                                                     const Game::Client::UI::Hud& Hud)
{
    CursorRenderState State;
    const int ViewportWidth = Camera.GetViewportWidth();
    const int ViewportHeight = Camera.GetViewportHeight();
    State.UseGameCursor =
        Core::Platform::IsWindowFocused() &&
        Core::Platform::IsMouseInGameContentArea() &&
        MouseX >= 0 && MouseX < ViewportWidth &&
        MouseY >= 0 && MouseY < ViewportHeight;

    if (!State.UseGameCursor)
    {
        CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
        return State;
    }

    if (Selector.IsDragging())
    {
        State.Kind = GameCursorKind::Drag;
        CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
        return State;
    }

    // Use dummy map dims for map interaction point (handled by GameLoop context)
    const bool MapPoint = true; // simplified - actual check done in GameController
    const bool AttackKeyDown = Core::Platform::IsKeyDown(65);

    if (Hud.HasPendingTarget())
    {
        if (!MapPoint)
        {
            State.Kind = GameCursorKind::Illegal;
            CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
            return State;
        }

        float WorldX = 0.0f;
        float WorldY = 0.0f;
        Camera.ScreenToWorld((float)MouseX, (float)MouseY, WorldX, WorldY);

        bool Legal = true;
        ClickTargetContext TargetContext;
        if (EcsInitialized && EM)
        {
            auto EcsIds = CollectSelectedAliveEcsIds(*EM, Selector);
            TargetContext = ResolveClickTargetContext(*EM, EcsIds, {WorldX, WorldY});
        }

        switch (Hud.GetPendingTargetMode())
        {
            case Game::Client::UI::PendingTargetMode::BuildPlacement:
                State.Kind = GameCursorKind::TargetYellow;
                break;
            case Game::Client::UI::PendingTargetMode::BuildRefinery:
                Legal = TargetContext.ResourceEntityID >= 0;
                State.Kind = Legal ? GameCursorKind::TargetYellow : GameCursorKind::Illegal;
                break;
            case Game::Client::UI::PendingTargetMode::Harvest:
                Legal = TargetContext.ResourceEntityID >= 0;
                State.Kind = Legal ? GameCursorKind::MagnifierGreen : GameCursorKind::Illegal;
                break;
            case Game::Client::UI::PendingTargetMode::Move:
                State.Kind = GameCursorKind::TargetGreen;
                break;
            case Game::Client::UI::PendingTargetMode::AttackMove:
                State.Kind = GameCursorKind::TargetRed;
                break;
            case Game::Client::UI::PendingTargetMode::Patrol:
                State.Kind = GameCursorKind::TargetNeutral;
                break;
            case Game::Client::UI::PendingTargetMode::Follow:
                Legal = TargetContext.AnyEntityID >= 0;
                State.Kind = Legal ? GameCursorKind::TargetNeutral : GameCursorKind::Illegal;
                break;
            case Game::Client::UI::PendingTargetMode::Land:
                State.Kind = GameCursorKind::TargetYellow;
                break;
            case Game::Client::UI::PendingTargetMode::Rally:
                State.Kind = GameCursorKind::TargetGreen;
                break;
            case Game::Client::UI::PendingTargetMode::Scan:
                State.Kind = GameCursorKind::TargetYellow;
                break;
            case Game::Client::UI::PendingTargetMode::Load:
                Legal = TargetContext.AnyEntityID >= 0;
                State.Kind = Legal ? GameCursorKind::TargetNeutral : GameCursorKind::Illegal;
                break;
            default:
                break;
        }

        CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
        return State;
    }

    if (EcsInitialized && EM && MapPoint)
    {
        auto EcsIds = CollectSelectedAliveEcsIds(*EM, Selector);
        if (!EcsIds.empty())
        {
            float WorldX = 0.0f;
            float WorldY = 0.0f;
            Camera.ScreenToWorld((float)MouseX, (float)MouseY, WorldX, WorldY);
            auto TargetContext = ResolveClickTargetContext(*EM, EcsIds, {WorldX, WorldY});

            if (AttackKeyDown ||
                (TargetContext.EnemyEntityID >= 0 &&
                 HasAttackCapableSelection(*EM, EcsIds, TargetContext.EnemyEntityID)))
            {
                State.Kind = GameCursorKind::TargetRed;
                CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
                return State;
            }

            if (TargetContext.ResourceEntityID >= 0)
            {
                auto Harvesters = FilterHarvesters(*EM, EcsIds);
                if (!Harvesters.empty())
                {
                    State.Kind = GameCursorKind::MagnifierGreen;
                    CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
                    return State;
                }
            }
        }
    }

    const GameCursorKind ScrollKind = ResolveScrollCursorKind(
        MouseX, MouseY, ViewportWidth, ViewportHeight, Hud);
    State.Kind = (ScrollKind != GameCursorKind::Arrow) ? ScrollKind : GameCursorKind::Arrow;
    CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
    return State;
}

inline void CursorManager::RenderCursor(int MouseX, int MouseY,
                                  const Render::Camera2D& Camera,
                                  const Selection& Selector,
                                  const Game::Client::UI::Hud& Hud,
                                  Core::ECS::EntityManager* EM,
                                  bool EcsInitialized,
                                  const Resource::Graphics::UIAssetCacheView& UIAssets,
                                  Render::Draw2D& Draw2D)
{
    CursorRenderState CursorState = ResolveCursorState(
        MouseX, MouseY, EcsInitialized, EM, Camera, Selector, Hud);

    Core::Platform::SetMouseCursorVisible(!CursorState.UseGameCursor);
    if (CursorState.UseGameCursor)
    {
        if (const auto* Frames = UIAssets.GetCursorFrames(CursorName(CursorState.Kind));
            Frames && !Frames->empty())
        {
            size_t FrameIndex = 0;
            const float CursorFps = CursorAnimationFps(CursorState.Kind);
            if (Frames->size() > 1 && CursorFps > 0.0f)
            {
                FrameIndex = (size_t)(Core::Platform::GetAbsoluteTime() * CursorFps) % Frames->size();
            }
            const auto& Frame = (*Frames)[FrameIndex];
            if (Frame.Valid())
            {
                Draw2D.DrawSpriteFromImage(
                    Frame,
                    (float)MouseX - CursorState.HotspotX,
                    (float)MouseY - CursorState.HotspotY,
                    (float)Frame.Width,
                    (float)Frame.Height,
                    Render::Colors::White());
            }
        }
    }
}

} // namespace Game::Client::GameLoop
