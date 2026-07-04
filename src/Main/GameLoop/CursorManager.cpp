#include "CursorManager.h"
#include "CursorTargeting.h"
#include "EcsSync.h"
#include "Core/Platform/Platform.h"
#include "Render/Camera2D.h"
#include "Render/Draw2D.h"
#include "Render/Color.h"
#include "Client/Entity.h"
#include "Client/EntityRenderer.h"
#include "Client/Selection.h"
#include "UI/Hud.h"
#include "Resource/UIAssetCacheView.h"
#include "Game/Components.h"
#include "Core/Math/Vec2.h"
#include <cmath>
#include <limits>

namespace Game::Client::GameLoop
{

bool CursorManager::IsMapInteractionPoint(int ScreenX, int ScreenY,
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

GameCursorKind CursorManager::ResolveScrollCursorKind(int MouseX, int MouseY,
                                                       int ViewportWidth, int ViewportHeight,
                                                       const Game::Client::UI::Hud& Hud)
{
    constexpr int EdgeMargin = 8;
    const int TopBlocked = Hud.GetResourceBarHeight();
    const int BottomBlocked = Hud.GetHudHeight(ViewportWidth, ViewportHeight);
    const int ActiveBottom = ViewportHeight - BottomBlocked;

    const bool AnyMouseButtonDown =
        Core::Platform::IsMouseButtonDown(0) ||
        Core::Platform::IsMouseButtonDown(1) ||
        Core::Platform::IsMouseButtonDown(2);

    if (!Core::Platform::IsWindowFocused() ||
        !Core::Platform::IsMouseInClientArea() ||
        AnyMouseButtonDown ||
        MouseX < 0 || MouseX >= ViewportWidth ||
        MouseY < TopBlocked || MouseY >= ActiveBottom)
    {
        return GameCursorKind::Arrow;
    }

    const bool Left = MouseX < EdgeMargin;
    const bool Right = MouseX >= ViewportWidth - EdgeMargin;
    const bool Up = MouseY < TopBlocked + EdgeMargin;
    const bool Down = MouseY >= ActiveBottom - EdgeMargin;

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

ClickTargetContext CursorManager::ResolveClickTargetContext(
    Core::ECS::EntityManager& EM,
    const std::vector<Core::ECS::EntityID>& SelectedIds,
    const Core::Math::Vec2& WorldPosition)
{
    ClickTargetContext Context;
    Context.WorldPosition = WorldPosition;

    float BestResourceScore = 1.0f;
    for (int EntityId : EM.GetEntitiesWith<
        Game::Components::TransformComponent,
        Game::Components::ResourceComponent>())
    {
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityId);
        if (!Transform || !Resource || Resource->IsDepleted) continue;

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

    int MyPlayerId = 0;
    if (!SelectedIds.empty())
    {
        if (auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(SelectedIds.front()))
            MyPlayerId = Player->PlayerID;
    }

    float BestEntityScore = std::numeric_limits<float>::max();
    for (int EntityId : EM.GetEntitiesWith<
        Game::Components::TransformComponent,
        Game::Components::UnitComponent,
        Game::Components::PlayerComponent>())
    {
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityId);
        if (!Transform || !Unit || !Player || Unit->HitPoints <= 0) continue;
        if (EM.GetComponent<Game::Components::ResourceComponent>(EntityId)) continue;

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

CursorRenderState CursorManager::ResolveCursorState(int MouseX, int MouseY,
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
        Core::Platform::IsMouseInClientArea() &&
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

void CursorManager::RenderCursor(int MouseX, int MouseY,
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
