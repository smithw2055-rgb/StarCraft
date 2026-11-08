#include "Render/SokolPBRDispatcher.h"
#include "Client/ClientRuntime.h"

#include "Client/CameraController.h"
#include "Client/EntityRenderer.h"
#include "Client/Selection.h"
#include "Core/Color.h"
#include "Client/TilesetRenderer.h"
#include "Client/WorldEffects.h"
#include "Core/ECS/EntityManager.h"
#include "Core/Platform/Platform.h"
#include "Core/Utils/InputDebug.h"
#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Main/GameLoop/CursorManager.h"
#include "Resource/ResourceCache.h"
#include "Render/Camera2D.h"
#include "Render/Draw2D.h"
#include "third_party/sokol/sokol_app.h"
#include "UI/Hud.h"

#include <string>
#include <sstream>
#include <cstdlib>
#include <utility>
#include <vector>

namespace Game::Client
{
namespace
{
    bool SelectionTraceEnabled()
    {
        static const bool Enabled =
            std::getenv("STARCRAFT_DEBUG_SELECTION") != nullptr;
        return Enabled;
    }

    std::string SelectionEntityName(Core::ECS::EntityManager& Entities, int EntityID)
    {
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityID);
        if (!Unit)
            return "Unknown";

        const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        if (DefID < 0)
            return "CHK_" + std::to_string(Unit->ChkUnitID);

        const auto Def = Game::Races::GetUnitDefByID(DefID);
        return Def.Name.empty() ? ("CHK_" + std::to_string(Unit->ChkUnitID)) : Def.Name;
    }

    const char* SelectionEntityKind(Core::ECS::EntityManager& Entities, int EntityID)
    {
        if (Entities.GetComponent<Game::Components::ResourceComponent>(EntityID))
            return "resource";
        if (Entities.GetComponent<Game::Components::BuildingComponent>(EntityID))
            return "building";
        if (Entities.GetComponent<Game::Components::UnitComponent>(EntityID))
            return "unit";
        return "entity";
    }

    void LogSelectionTrace(const char* Source,
                           Core::ECS::EntityManager& Entities,
                           const std::vector<int>& SelectedIds)
    {
        if (!SelectionTraceEnabled())
            return;

        if (SelectedIds.empty())
        {
            Core::Utils::Logger::Info(
                "SelectionTrace",
                std::string(Source) + " selected none");
            return;
        }

        for (int EntityID : SelectedIds)
        {
            if (!Entities.IsAlive(EntityID))
                continue;

            auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (!Unit)
                continue;

            auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityID);
            auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityID);
            Core::Utils::Logger::Info(
                "SelectionTrace",
                std::string(Source) + " selected id=" + std::to_string(EntityID) +
                    " kind=" + SelectionEntityKind(Entities, EntityID) +
                    " name=" + SelectionEntityName(Entities, EntityID) +
                    " chk=" + std::to_string(Unit->ChkUnitID) +
                    " player=" + std::to_string(Player ? Player->PlayerID : -1) +
                    " pos=" + std::to_string(Trans ? (int)Trans->Position.X : 0) +
                    "," + std::to_string(Trans ? (int)Trans->Position.Y : 0));
        }
    }
}

ClientRuntime::ClientRuntime()
    : Cache(std::make_unique<Resource::Graphics::ResourceCache>())
    , Tilemap(std::make_unique<TilesetRenderer>())
    , EntityView(std::make_unique<EntityRenderer>())
    , CameraCtrl(std::make_unique<CameraController>())
    , Selector(std::make_unique<Selection>())
    , Cursor(std::make_unique<GameLoop::CursorManager>())
    , Camera(std::make_unique<Render::Camera2D>())
    , Effects(std::make_unique<WorldEffects>())
    , PBRDispatcher(std::make_unique<Render::SokolPBRDispatcher>())
{
    Core::Utils::Logger::Info(
        "ClientRuntime",
        "Constructed this=" + std::to_string((uintptr_t)this) +
            " cache=" + std::to_string((uintptr_t)Cache.get()));
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
    Core::Utils::Logger::Info(
        "ClientRuntime",
        "InitializeResources this=" + std::to_string((uintptr_t)this) +
            " cache=" + std::to_string((uintptr_t)Cache.get()));
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
    Tilemap->Init(TilesetName, GetTilesetAssets(), &MapData.MegaTileIds);
    EntityView->Init(GetSpriteAssets(), GetDataAssets());
    Selector->Init(GetDataAssets());

    Camera->SetViewport(ViewportWidth, ViewportHeight);
    Camera->SetZoomBounds(0.75f, 2.25f);
    Camera->SetZoom(1.20f);
    CameraCtrl->Attach(*Camera);
    CameraCtrl->SetBounds(MapData.Width, MapData.Height);
    Camera->SetPosition(CameraX, CameraY);
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
    if (Core::Utils::InputDebugEnabled())
    {
        std::ostringstream Stream;
        Stream << "screen=" << ScreenX << "," << ScreenY
               << " viewport=" << Camera->GetViewportWidth() << "x" << Camera->GetViewportHeight()
               << " camera=" << Camera->GetX() << "," << Camera->GetY()
               << " zoom=" << Camera->GetZoom()
               << " world=" << WorldX << "," << WorldY;
        Core::Utils::InputDebugLog("InputScreenToWorld", Stream.str());
    }
    return { WorldX, WorldY };
}

Core::Math::Vec2 ClientRuntime::WorldToScreen(float WorldX, float WorldY) const
{
    float ScreenX = 0.0f;
    float ScreenY = 0.0f;
    Camera->WorldToScreen(WorldX, WorldY, ScreenX, ScreenY);
    return { ScreenX, ScreenY };
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
    {
        Selector->CancelPointerGesture();
        return;
    }
    else
    {
        const std::vector<int> Before = Selector->GetSelected();
        Selector->Update(Entities, *Camera);
        const std::vector<int>& After = Selector->GetSelected();
        if (Before != After)
            LogSelectionTrace("manual", Entities, After);
    }
}

void ClientRuntime::SetLocalPlayerId(int PlayerId)
{
    Selector->SetLocalPlayerId(PlayerId);
}

int ClientRuntime::GetLocalPlayerId() const
{
    return Selector->GetLocalPlayerId();
}

bool ClientRuntime::SetConsoleRace(const std::string& RaceName)
{
    if (Cache)
    {
        return Cache->LoadHdConsoleGraphicsForRace(RaceName);
    }
    return false;
}

void ClientRuntime::CancelPointerGesture()
{
    Selector->CancelPointerGesture();
}

void ClientRuntime::ResetSessionState()
{
    Selector->ResetSessionState();
}

void ClientRuntime::PruneSelection(Core::ECS::EntityManager& Entities)
{
    Selector->PruneAgainst(Entities);
}

void ClientRuntime::HandleControlGroupHotkeys(Core::ECS::EntityManager& Entities)
{
    const bool CtrlDown = Core::Platform::IsControlDown();
    const bool ShiftDown = Core::Platform::IsShiftDown();

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

const std::vector<CampaignPing>& ClientRuntime::GetCampaignPings() const
{
    return Effects->GetCampaignPings();
}

const std::vector<int>& ClientRuntime::GetSelectedIds() const
{
    return Selector->GetSelected();
}

void ClientRuntime::SetSelectedIdsForDemo(std::vector<int> SelectedIds)
{
    Selector->SetSelectedForDebug(std::move(SelectedIds));
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
                                bool MapLoaded,
                                float InterpAlpha)
{
    if (!MapLoaded)
        return;

    Tilemap->Render(MapData);
    EntityView->SetCamera(Camera->GetX(), Camera->GetY(),
                          (float)Camera->GetViewportWidth(),
                           (float)Camera->GetViewportHeight(),
                           Camera->GetZoom());
    EntityView->SetInterpolationAlpha(InterpAlpha);
    EntityView->Render(Entities, Selector->GetSelected());
}

void ClientRuntime::RenderWorldEffects(Render::Draw2D& Draw2D)
{
    Effects->Render(Draw2D);
}

void ClientRuntime::RenderSelectionDragOverlay()
{
    if (!Selector->IsDragging() || !Core::Platform::IsMouseButtonDown(0))
        return;

    float X = 0.0f;
    float Y = 0.0f;
    float W = 0.0f;
    float H = 0.0f;
    Selector->GetDragRectScreen(X, Y, W, H);
    Render::g_Draw2D.DrawRect(X, Y, W, H, Render::Color(80, 200, 80, 24));
    Render::g_Draw2D.DrawRectLines(X, Y, W, H, Render::Color(80, 220, 80, 220), 1.0f);
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

size_t ClientRuntime::InitializeModernPBRPipeline(const Resource::ModernAssetRegistry& Registry)
{
    if (!PBRDispatcher) {
        PBRDispatcher = std::make_unique<Render::SokolPBRDispatcher>();
    }
    return PBRDispatcher->UploadPBRPrefabsToGpu(Registry);
}

void ClientRuntime::RenderModernPBRQueue(const Render::RenderQueue2D& Queue)
{
    if (!PBRDispatcher || !Camera) return;
    PBRDispatcher->FlushQueueToSokol(Queue, Render::g_Draw2D, *Camera);
}

} // namespace Game::Client
