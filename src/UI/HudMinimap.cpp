#include "UI/Hud.h"

#include <cmath>

#include "Game/Components.h"
#include "Render/Draw2D.h"
#include "UI/HudLayout.h"
#include "UI/HudMinimapGeometry.h"

namespace Game::Client::UI
{

void Hud::SetMinimapState(float CamX, float CamY, float CamZoom,
                          int VpWidth, int VpHeight,
                          int MapW, int MapH,
                          const int*, const bool*, const bool*)
{
    mState.Minimap.CameraX = CamX;
    mState.Minimap.CameraY = CamY;
    mState.Minimap.CameraZoom = CamZoom;
    mState.Minimap.ViewportWidth = VpWidth;
    mState.Minimap.ViewportHeight = VpHeight;
    mState.Minimap.MapWidth = MapW;
    mState.Minimap.MapHeight = MapH;
}

void Hud::DrawMinimap(float, float, float, float) const
{
    // Minimap rendering is inline in Draw() for now because it needs the ECS overlay.
}

void Hud::DrawMinimapOverlay(Core::ECS::EntityManager& Entities,
                             const HudLayout& Layout,
                             int MapWidthTiles,
                             int MapHeightTiles) const
{
    auto& D = Render::g_Draw2D;
    D.DrawRectLines((float)Layout.MinimapRect.X - 1, (float)Layout.MinimapRect.Y - 1,
                    (float)Layout.MinimapRect.W + 2, (float)Layout.MinimapRect.H + 2,
                    Render::Color(100, 100, 120, 150), 1.0f);
    const RectI ContentRect = ResolveMinimapContentRect(
        Layout.MinimapRect, MapWidthTiles, MapHeightTiles);

    const float MapPxW = (float)(MapWidthTiles * 32);
    const float MapPxH = (float)(MapHeightTiles * 32);
    if (MapPxW <= 0.0f || MapPxH <= 0.0f)
        return;

    Entities.ForEachEntityWith<Game::Components::TransformComponent,
                               Game::Components::RenderComponent,
                               Game::Components::UnitComponent>([&](int EntityId)
    {
        auto* Transform = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Render = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityId);
        if (!Transform || !Render || !Render->Visible)
            return;

        const int PX = MinimapWorldToScreenX(ContentRect, Transform->Position.X, MapPxW);
        const int PY = MinimapWorldToScreenY(ContentRect, Transform->Position.Y, MapPxH);
        const int PlayerId = Player ? Player->PlayerID : 11;
        const Render::Color Dot = (PlayerId == 0) ? Render::Color(80, 200, 255, 255)
                                                  : Render::Color(220, 60, 60, 255);
        D.DrawRect((float)PX, (float)PY, 2.0f, 2.0f, Dot);
    });
}

} // namespace Game::Client::UI
