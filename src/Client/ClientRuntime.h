#pragma once

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Main/GameLoop/CursorManager.h"
#include "Resource/UIAssetCacheView.h"

#include <memory>
#include <string>
#include <vector>

namespace Core::ECS { class EntityManager; }
namespace Core::Audio { class AudioSystem; }
namespace Render { class Camera2D; class Draw2D; class SokolPBRDispatcher; class RenderQueue2D; }
namespace Resource { class ModernAssetRegistry; }
namespace Resource::Graphics { class ResourceCache; }
namespace Game::Client::UI { class Hud; }
namespace Game::Client::GameLoop { class CursorManager; }

namespace Game::Client
{

struct CampaignPing;
class WorldEffects;
class TilesetRenderer;
class EntityRenderer;
class CameraController;
class Selection;
struct GameMap;

struct ClientRuntime
{
    ClientRuntime();
    ~ClientRuntime();
    ClientRuntime(const ClientRuntime&) = delete;
    ClientRuntime& operator=(const ClientRuntime&) = delete;
    ClientRuntime(ClientRuntime&&) = delete;
    ClientRuntime& operator=(ClientRuntime&&) = delete;

    Resource::Graphics::SpriteAssetCacheView GetSpriteAssets();
    Resource::Graphics::TilesetAssetCacheView GetTilesetAssets();
    Resource::Graphics::UIAssetCacheView GetUIAssets();
    Resource::Graphics::DataAssetCacheView GetDataAssets();
    Resource::Graphics::ArchiveAssetSourceView GetArchiveSource();
    bool InitializeResources(const std::string& InstallPath);
    void ShutdownResources();
    void InitializeWorldEffects(int MaxParticles);
    void InitializeWorldView(const GameMap& MapData,
                             const std::string& TilesetName,
                             float CameraX,
                             float CameraY,
                             int ViewportWidth,
                             int ViewportHeight);
    void UpdateCameraForViewport(int ViewportWidth,
                                 int ViewportHeight,
                                 float TopBlockedPx,
                                 float BottomBlockedPx,
                                 float DeltaSeconds);
    void CenterCamera(float WorldX, float WorldY);
    int GetViewportWidth() const;
    int GetViewportHeight() const;
    Core::Math::Vec2 GetCameraPosition() const;
    Core::Math::Vec2 ScreenToWorld(float ScreenX, float ScreenY) const;
    Core::Math::Vec2 WorldToScreen(float WorldX, float WorldY) const;
    void GetVisibleWorldRect(float& X, float& Y, float& W, float& H) const;
    GameLoop::ClickTargetContext ResolveClickTargetContext(Core::ECS::EntityManager& Entities,
                                                           const std::vector<Core::ECS::EntityID>& SelectedIds,
                                                           const Core::Math::Vec2& WorldPosition);
    void UpdateSelection(Core::ECS::EntityManager& Entities, bool BlockSelection);
    void SetLocalPlayerId(int PlayerId);
    int GetLocalPlayerId() const;
    bool SetConsoleRace(const std::string& RaceName);
    void CancelPointerGesture();
    void ResetSessionState();
    void PruneSelection(Core::ECS::EntityManager& Entities);
    void HandleControlGroupHotkeys(Core::ECS::EntityManager& Entities);
    std::vector<Core::ECS::EntityID> CollectSelectedAliveIds(Core::ECS::EntityManager& Entities) const;
    void UpdateWorldEffects(Core::ECS::EntityManager& Entities,
                            Core::Audio::AudioSystem& Audio,
                            float DeltaSeconds);
    void AddCampaignPing(Core::Math::Vec2 Position);
    const std::vector<CampaignPing>& GetCampaignPings() const;
    const std::vector<int>& GetSelectedIds() const;
    void SetSelectedIdsForDemo(std::vector<int> SelectedIds);
    bool IsSelectionDragging() const;
    void GetSelectionDragRectScreen(float& X, float& Y, float& W, float& H) const;
    void BeginWorldFrame();
    void RenderWorld(const GameMap& MapData,
                     Core::ECS::EntityManager& Entities,
                     bool MapLoaded,
                     float InterpAlpha = 1.0f);
    // Phase 4: Direct Sokol GPU 3-Channel PBR Pipeline on ClientRuntime
    size_t InitializeModernPBRPipeline(const Resource::ModernAssetRegistry& Registry);
    void RenderModernPBRQueue(const Render::RenderQueue2D& Queue);
    void RenderWorldEffects(Render::Draw2D& Draw2D);
    void RenderSelectionDragOverlay();
    void RenderCursor(int MouseX,
                      int MouseY,
                      const UI::Hud& Hud,
                      Core::ECS::EntityManager* Entities,
                      bool EcsInitialized,
                      Render::Draw2D& Draw2D);
    void EndWorldFrame();

private:
    // Keep heavyweight implementation owners out of this public header. Expose
    // resources through narrow view accessors and keep cursor/runtime helpers
    // private to the implementation file.
    std::unique_ptr<Resource::Graphics::ResourceCache> Cache;
    std::unique_ptr<TilesetRenderer> Tilemap;
    std::unique_ptr<EntityRenderer> EntityView;
    std::unique_ptr<CameraController> CameraCtrl;
    std::unique_ptr<Selection> Selector;
    std::unique_ptr<GameLoop::CursorManager> Cursor;
    std::unique_ptr<Render::Camera2D> Camera;
    std::unique_ptr<WorldEffects> Effects;
    std::unique_ptr<Render::SokolPBRDispatcher> PBRDispatcher;
};

} // namespace Game::Client
