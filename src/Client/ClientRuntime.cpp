#include "Client/ClientRuntime.h"

#include "Client/CameraController.h"
#include "Client/Entity.h"
#include "Client/EntityRenderer.h"
#include "Client/Selection.h"
#include "Client/SelectionOverlayPresentation.h"
#include "Client/TilesetRenderer.h"
#include "Client/WorldEffects.h"
#include "Core/ECS/EntityManager.h"
#include "Core/Platform/Platform.h"
#include "Main/GameLoop/CursorManager.h"
#include "Resource/ResourceCache.h"
#include "Render/Camera2D.h"
#include "Render/Draw2D.h"
#include "Render/Renderer.h"
#include "third_party/sokol/sokol_app.h"
#include "UI/Hud.h"

namespace Game::Client
{

ClientRuntime::ClientRuntime()
    : Cache(std::make_unique<Resource::Graphics::ResourceCache>())
    , Tilemap(std::make_unique<TilesetRenderer>())
    , EntityView(std::make_unique<EntityRenderer>())
    , CameraCtrl(std::make_unique<CameraController>())
    , Selector(std::make_unique<Selection>())
    , Cursor(std::make_unique<GameLoop::CursorManager>())
    , Camera(std::make_unique<Render::Camera2D>())
    , Effects(std::make_unique<WorldEffects>())
{
}

ClientRuntime::~ClientRuntime() = default;

Resource::Graphics::SpriteAssetCacheView ClientRuntime::GetSpriteAssets()
{
    return Cache->GetSpriteAssets();
}

Resource::Graphics::TilesetAssetCacheView ClientRuntime::GetTilesetAssets()
{
    return Cache->GetTilesetAssets();
}

Resource::Graphics::UIAssetCacheView ClientRuntime::GetUIAssets()
{
    return Cache->GetUIAssets();
}

Resource::Graphics::DataAssetCacheView ClientRuntime::GetDataAssets()
{
    return Cache->GetDataAssets();
}


Resource::Graphics::ArchiveAssetSourceView ClientRuntime::GetArchiveSource()
{
    return Cache->GetArchiveSource();
}
bool ClientRuntime::InitializeResources(const std::string& InstallPath)
{
    return Cache->Initialize(InstallPath);
}

void ClientRuntime::ShutdownResources()
{
    Cache->Shutdown();
}

void ClientRuntime::InitializeWorldEffects(int MaxParticles)
{
    Effects->Init(MaxParticles);
}




void ClientRuntime::InitializeWorldView(const GameMap& MapData,
                                        const std::string& TilesetName,
                                        float CameraX,
                                        float CameraY,
                                        int ViewportWidth,
                                        int ViewportHeight)
{
    Tilemap->Init(TilesetName, GetTilesetAssets());
    EntityView->Init(GetSpriteAssets());

    Camera->SetViewport(ViewportWidth, ViewportHeight);
    Camera->SetZoomBounds(0.75f, 2.25f);
    Camera->SetZoom(1.20f);
    Camera->SetPosition(CameraX, CameraY);
    CameraCtrl->Attach(*Camera);
    CameraCtrl->SetBounds(MapData.Width, MapData.Height);
    Tilemap->SetCamera(*Camera);
}

void ClientRuntime::UpdateCameraForViewport(int ViewportWidth,
                                            int ViewportHeight,
                                            float TopBlockedPx,
                                            float BottomBlockedPx,
                                            float DeltaSeconds)
{
    Camera->SetViewport(ViewportWidth, ViewportHeight);
    CameraCtrl->SetTopBlockedPx(TopBlockedPx);
    CameraCtrl->SetBottomBlockedPx(BottomBlockedPx);
    CameraCtrl->Update(DeltaSeconds);
}

void ClientRuntime::CenterCamera(float WorldX, float WorldY)
{
    Camera->SetPosition(WorldX, WorldY);
    Tilemap->SetCamera(*Camera);
}

int ClientRuntime::GetViewportWidth() const
{
    return Camera->GetViewportWidth();
}

int ClientRuntime::GetViewportHeight() const
{
    return Camera->GetViewportHeight();
}

Core::Math::Vec2 ClientRuntime::GetCameraPosition() const
{
    return { Camera->GetX(), Camera->GetY() };
}

Core::Math::Vec2 ClientRuntime::ScreenToWorld(float ScreenX, float ScreenY) const
{
    float WorldX = 0.0f;
    float WorldY = 0.0f;
    Camera->ScreenToWorld(ScreenX, ScreenY, WorldX, WorldY);
    return { WorldX, WorldY };
}

void ClientRuntime::GetVisibleWorldRect(float& X, float& Y, float& W, float& H) const
{
    W = (float)Camera->GetViewportWidth() / Camera->GetZoom();
    H = (float)Camera->GetViewportHeight() / Camera->GetZoom();
    X = Camera->GetX() - W * 0.5f;
    Y = Camera->GetY() - H * 0.5f;
}

GameLoop::ClickTargetContext ClientRuntime::ResolveClickTargetContext(
    Core::ECS::EntityManager& Entities,
    const std::vector<Core::ECS::EntityID>& SelectedIds,
    const Core::Math::Vec2& WorldPosition)
{
    return Cursor->ResolveClickTargetContext(Entities, SelectedIds, WorldPosition);
}

void ClientRuntime::UpdateSelection(Core::ECS::EntityManager& Entities, bool BlockSelection)
{
    if (BlockSelection)
        Selector->CancelPointerGesture();
    else
        Selector->Update(Entities, *Camera);
}

void ClientRuntime::CancelPointerGesture()
{
    Selector->CancelPointerGesture();
}

void ClientRuntime::PruneSelection(Core::ECS::EntityManager& Entities)
{
    Selector->PruneAgainst(Entities);
}

void ClientRuntime::HandleControlGroupHotkeys(Core::ECS::EntityManager& Entities)
{
    const bool CtrlDown = Core::Platform::IsKeyDown(341) || Core::Platform::IsKeyDown(345);
    const bool ShiftDown = Core::Platform::IsKeyDown(340) || Core::Platform::IsKeyDown(344);

    for (int K = 0; K < 10; K++)
    {
        const int KeyCode = (K == 9) ? SAPP_KEYCODE_0 : (SAPP_KEYCODE_1 + K);
        if (Core::Platform::IsKeyPressed(KeyCode))
        {
            if (CtrlDown && ShiftDown)
                Selector->AppendControlGroup(K, Entities);
            else if (CtrlDown)
                Selector->SaveControlGroup(K, Entities);
            else
                Selector->RecallControlGroup(K, Entities);
        }
    }
}

std::vector<Core::ECS::EntityID> ClientRuntime::CollectSelectedAliveIds(Core::ECS::EntityManager& Entities) const
{
    std::vector<Core::ECS::EntityID> AliveIds;
    for (Core::ECS::EntityID EntityId : Selector->GetSelected())
    {
        if (Entities.IsAlive(EntityId))
            AliveIds.push_back(EntityId);
    }
    return AliveIds;
}

void ClientRuntime::UpdateWorldEffects(Core::ECS::EntityManager& Entities,
                                       Core::Audio::AudioSystem& Audio,
                                       float DeltaSeconds)
{
    Effects->UpdateFromCombat(Entities, Audio, DeltaSeconds);
}

void ClientRuntime::AddCampaignPing(Core::Math::Vec2 Position)
{
    Effects->AddCampaignPing(Position);
}

const std::vector<int>& ClientRuntime::GetSelectedIds() const
{
    return Selector->GetSelected();
}

bool ClientRuntime::IsSelectionDragging() const
{
    return Selector->IsDragging();
}

void ClientRuntime::GetSelectionDragRectScreen(float& X, float& Y, float& W, float& H) const
{
    Selector->GetDragRectScreen(X, Y, W, H);
}

void ClientRuntime::BeginWorldFrame()
{
    Render::BeginFrame();
    Render::g_Draw2D.BeginFrame(*Camera);
}

void ClientRuntime::RenderWorld(const GameMap& MapData,
                                Core::ECS::EntityManager& Entities,
                                bool MapLoaded)
{
    if (!MapLoaded)
        return;

    Tilemap->Render(MapData);
    EntityView->SetCamera(Camera->GetX(), Camera->GetY(),
                          (float)Camera->GetViewportWidth(),
                           (float)Camera->GetViewportHeight(),
                           Camera->GetZoom());
    EntityView->Render(Entities, Selector->GetSelected());
}

void ClientRuntime::RenderWorldEffects(Render::Draw2D& Draw2D)
{
    Effects->Render(Draw2D);
}

void ClientRuntime::RenderSelectionDragOverlay()
{
    if (!Selector->IsDragging())
        return;

    float X = 0.0f;
    float Y = 0.0f;
    float W = 0.0f;
    float H = 0.0f;
    Selector->GetDragRectScreen(X, Y, W, H);
    RenderSelectionDragRect(X, Y, W, H);
}

void ClientRuntime::RenderCursor(int MouseX,
                                 int MouseY,
                                 const UI::Hud& Hud,
                                 Core::ECS::EntityManager* Entities,
                                 bool EcsInitialized,
                                 Render::Draw2D& Draw2D)
{
    Cursor->RenderCursor(MouseX, MouseY, *Camera, *Selector, Hud, Entities, EcsInitialized, GetUIAssets(), Draw2D);
}

void ClientRuntime::EndWorldFrame()
{
    Render::g_Draw2D.EndFrame();
}

} // namespace Game::Client
