#pragma once

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Client/Entity.h"
#include "Resource/SpriteAssetCacheView.h"
#include <vector>

namespace Game::Client
{

class EntityRenderer
{
public:
    void Init(Resource::Graphics::SpriteAssetCacheView SpriteAssets) { mSpriteAssets = SpriteAssets; }

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

private:
    void DrawSelectionMarker(const Entity& E);
    void DrawSelectionMarker(Core::ECS::EntityManager& Entities, int EntityId);
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

    Resource::Graphics::SpriteAssetCacheView mSpriteAssets;
    float mCameraX = 0.0f, mCameraY = 0.0f;
    float mViewW = 1280.0f, mViewH = 720.0f;
    float mZoom = 1.0f;
};

} // namespace Game::Client
