#pragma once

#include "raylib.h"
#include "Core/Math/Vec2.h"

namespace Render
{

class CameraController
{
public:
    CameraController();
    void Update(float DeltaTime);
    Camera2D GetCamera() const { return mCamera; }

    void SetPosition(const Core::Math::Vec2& Pos);
    void SetZoom(float Zoom);
    void MoveTo(const Core::Math::Vec2& Target);
    void ResetOffset();
    void SetMapBounds(int WidthTiles, int HeightTiles, int BottomPanelPixels = 0);

    float GetZoom() const { return mCamera.zoom; }

private:
    Camera2D mCamera = {};
    float mScrollSpeed = 800.0f;
    float mZoomSpeed = 0.1f;
    float mMinZoom = 0.3f;
    float mMaxZoom = 3.0f;
    int mMapWidth = 0;
    int mMapHeight = 0;
    int mBottomPanelHeight = 0;
};

} // namespace Render
