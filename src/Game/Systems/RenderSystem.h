#pragma once

#include <vector>

#include "Core/Math/Rect.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Render/CameraController.h"
#include "Resource/ResourceCache.h"
#include "raylib.h"

#include "Game/Map/Map.h"

namespace UI { class SelectionManager; }
namespace Game::Systems { class FogOfWarSystem; }

namespace Game::Systems
{

class RenderSystem : public Core::ECS::ISystem
{
public:
    RenderSystem(Resource::ResourceCache& Cache, Core::ECS::SystemManager& SysMgr);

    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "RenderSystem"; }

    Render::CameraController& GetCamera() { return mCamera; }
    void SetMap(const Game::Map::GameMap* Map) { mMap = Map; }
    void SetTileset(const std::vector<Texture2D>* Textures,
                    const std::vector<uint16_t>* Cv5Data = nullptr) { mTilesetTextures = Textures; mCv5Data = Cv5Data; }
    void SetSelection(const std::vector<int>* Selected, bool IsDragging,
                      const Core::Math::Rect* DragRect);
    void SetFogOfWar(const FogOfWarSystem* Fog) { mFogOfWar = Fog; }
    void DrawBuildGhost(Core::Math::Vec2 Position, int BuildingDefID, bool Valid);

private:
    void DrawGrid();
    void DrawTerrain();
    void DrawEntities();
    void DrawSelection();
    void DrawFog();
    void DrawWalkability();

    Resource::ResourceCache& mResourceCache;
    Core::ECS::SystemManager& mSystemMgr;
    Render::CameraController mCamera;
    const Game::Map::GameMap* mMap = nullptr;
    const std::vector<Texture2D>* mTilesetTextures = nullptr;

    const std::vector<int>* mSelectedEntities = nullptr;
    bool mIsDragging = false;
    Core::Math::Rect mDragRect;

    const std::vector<uint16_t>* mCv5Data = nullptr;
    const FogOfWarSystem* mFogOfWar = nullptr;
    bool mShowWalkability = false;
};

} // namespace Game::Systems
