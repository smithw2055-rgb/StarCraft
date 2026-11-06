#pragma once

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Client/Entity.h"
#include "Render/RenderServer2D.h"
#include "Resource/DataAssetCacheView.h"
#include "Resource/SpriteAssetCacheView.h"
#include <algorithm>
#include <vector>

namespace Render { class Draw2D; }

namespace Game::Client
{

inline float BuildingFlightBodyOffsetY(
    const Game::Components::BuildingComponent* Building)
{
    if (!Building || !Building->IsLifted)
        return 0.0f;
    return -std::max(0.0f, Building->FlightVisualHeight);
}

class EntityRenderer
{
public:
    void Init(Resource::Graphics::SpriteAssetCacheView SpriteAssets,
              Resource::Graphics::DataAssetCacheView DataAssets)
    {
        mSpriteAssets = SpriteAssets;
        mDataAssets = DataAssets;
    }

    void ResolveFrames(EntityManager& Entities);
    void Render(const EntityManager& Entities, const std::vector<int>& SelectedIds);
    void Render(Core::ECS::EntityManager& Entities, const std::vector<int>& SelectedIds);
    void UpdateAnimation(EntityManager& Entities, float DeltaSeconds);

    void SetCamera(float CamX, float CamY, float ViewW, float ViewH, float Zoom)
    {
        mCameraX = CamX; mCameraY = CamY;
        mViewW = ViewW; mViewH = ViewH;
        mZoom = Zoom;
    }

    void SetInterpolationAlpha(float Alpha)
    {
        mInterpolationAlpha = Alpha;
    }

    void SetUseRenderQueue(bool Enabled) { mUseRenderQueue = Enabled; }
    bool GetUseRenderQueue() const { return mUseRenderQueue; }

    Render::RenderServer2D& GetRenderServer() { return mRenderServer; }
    const Render::RenderServer2D& GetRenderServer() const { return mRenderServer; }
    const Render::RenderFrameStats2D& GetLastRenderStats() const
    {
        return mRenderServer.GetLastFrameStats();
    }

private:
    void DrawSelectionMarker(const Entity& E);
    void DrawSelectionMarker(Core::ECS::EntityManager& Entities, int EntityId);
    void DrawRallyIndicator(Render::Draw2D& Draw, Core::ECS::EntityManager& Entities, int BuildingEntityId) const;
    void DrawImageNode(const Entity& E, const EntityImageNode& Node, float OffsetX, float OffsetY) const;
    void DrawImageNodeTree(const Entity& E,
                           const std::vector<EntityImageNode>& Nodes,
                           int NodeIndex,
                           float ParentOffsetX = 0.0f,
                           float ParentOffsetY = 0.0f,
                           int Depth = 0) const;
    void DrawImageNode(Core::ECS::EntityManager& Entities,
                       int EntityId,
                       const Game::Components::ImageSubnode& Node,
                       float OffsetX,
                       float OffsetY) const;
    void DrawImageNodeTree(Core::ECS::EntityManager& Entities,
                           int EntityId,
                           const std::vector<Game::Components::ImageSubnode>& Nodes,
                           int NodeIndex,
                           float ParentOffsetX = 0.0f,
                           float ParentOffsetY = 0.0f,
                           int Depth = 0) const;
    float ResolveVisualRadius(Core::ECS::EntityManager& Entities, int EntityId) const;
    void AccumulateImageNodeVisualRadius(const Game::Components::RenderComponent& RenderComp,
                                         const std::vector<Game::Components::ImageSubnode>& Nodes,
                                         int NodeIndex,
                                         float ParentOffsetX,
                                         float ParentOffsetY,
                                         int Depth,
                                         float& Radius) const;

    Resource::Graphics::SpriteAssetCacheView mSpriteAssets;
    Resource::Graphics::DataAssetCacheView mDataAssets;
    mutable Render::RenderServer2D mRenderServer;
    bool mUseRenderQueue = true;
    float mCameraX = 0.0f, mCameraY = 0.0f;
    float mViewW = 1280.0f, mViewH = 720.0f;
    float mZoom = 1.0f;
    float mInterpolationAlpha = 1.0f;
};

} // namespace Game::Client
