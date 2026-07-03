#pragma once

#include "CursorTypes.h"

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
