#pragma once

#include "Game/sokol/Entity.h"
#include "Resource/sokol/ResourceCacheSokol.h"

namespace Game::SokolDemo
{

class EntityRenderer
{
public:
    void Init(Resource::SokolBridge::ResourceCacheSokol& Cache) { mCache = &Cache; }

    void ResolveFrames(EntityManager& Entities);
    void Render(const EntityManager& Entities, const std::vector<int>& SelectedIds);
    void UpdateAnimation(EntityManager& Entities, float DeltaSeconds);

    void SetCamera(float CamX, float CamY, float ViewW, float ViewH, float Zoom)
    {
        mCameraX = CamX; mCameraY = CamY;
        mViewW = ViewW; mViewH = ViewH;
        mZoom = Zoom;
    }

private:
    void DrawSelectionMarker(const Entity& E);

    Resource::SokolBridge::ResourceCacheSokol* mCache = nullptr;
    float mCameraX = 0.0f, mCameraY = 0.0f;
    float mViewW = 1280.0f, mViewH = 720.0f;
    float mZoom = 1.0f;
};

} // namespace Game::SokolDemo
