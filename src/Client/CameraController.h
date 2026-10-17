#pragma once

#include "Core/Platform/Platform.h"
#include "Render/Camera2D.h"

namespace Game::Client
{

class CameraController
{
public:
    void Attach(Render::Camera2D& Camera) { mCamera = &Camera; }
    void Update(float DeltaSeconds);
    void SetBounds(int MapWidthTiles, int MapHeightTiles);
    void SetTopBlockedPx(float Pixels) { mTopBlockedPx = Pixels; }
    void SetBottomBlockedPx(float Pixels) { mBottomBlockedPx = Pixels; }

private:
    Render::Camera2D* mCamera = nullptr;
    int mMapWidth = 0;
    int mMapHeight = 0;

    float mEdgePanMarginPx = 16.0f;
    float mKeyboardPanSpeed = 750.0f;
    float mMousePanSpeed = 750.0f;
    float mTopBlockedPx = 0.0f;
    float mBottomBlockedPx = 0.0f;
};

} // namespace Game::Client
