#include "Client/CameraController.h"

#include "third_party/sokol/sokol_app.h"

#include <algorithm>
#include <cmath>

namespace Game::Client
{

void CameraController::SetBounds(int MapWidthTiles, int MapHeightTiles)
{
    mMapWidth = MapWidthTiles;
    mMapHeight = MapHeightTiles;
    if (mCamera)
    {
        mCamera->SetMapBounds(MapWidthTiles, MapHeightTiles);
    }
}

void CameraController::Update(float Dt)
{
    if (!mCamera) return;

    float Wheel = Core::Platform::GetMouseWheel();
    if (Wheel != 0.0f)
    {
        mCamera->ZoomAt(Core::Platform::GetMouseX(), Core::Platform::GetMouseY(), Wheel * 0.1f);
    }

    if (Core::Platform::IsKeyDown(SAPP_KEYCODE_EQUAL) || Core::Platform::IsKeyDown(SAPP_KEYCODE_KP_ADD))
        mCamera->ZoomAt(Core::Platform::GetMouseX(), Core::Platform::GetMouseY(), 0.05f);
    if (Core::Platform::IsKeyDown(SAPP_KEYCODE_MINUS) || Core::Platform::IsKeyDown(SAPP_KEYCODE_KP_SUBTRACT))
        mCamera->ZoomAt(Core::Platform::GetMouseX(), Core::Platform::GetMouseY(), -0.05f);

    if (Core::Platform::IsMouseButtonDown(2)) // 0 = LMB, 1 = RMB, 2 = MMB
    {
        float DX = Core::Platform::GetMouseDeltaX();
        float DY = Core::Platform::GetMouseDeltaY();
        mCamera->PanBy(-DX, -DY);
    }

    float KDX = 0.0f, KDY = 0.0f;
    if (Core::Platform::IsKeyDown(SAPP_KEYCODE_LEFT)) KDX -= 1.0f;
    if (Core::Platform::IsKeyDown(SAPP_KEYCODE_RIGHT)) KDX += 1.0f;
    if (Core::Platform::IsKeyDown(SAPP_KEYCODE_UP)) KDY -= 1.0f;
    if (Core::Platform::IsKeyDown(SAPP_KEYCODE_DOWN)) KDY += 1.0f;
    if (KDX != 0.0f || KDY != 0.0f)
    {
        mCamera->PanBy(KDX * mKeyboardPanSpeed * Dt, KDY * mKeyboardPanSpeed * Dt);
    }

    int MX = (int)Core::Platform::GetMouseX();
    int MY = (int)Core::Platform::GetMouseY();
    int VW = mCamera->GetViewportWidth();
    int VH = mCamera->GetViewportHeight();
    int ActiveTop = std::max(0, (int)mTopBlockedPx);
    int ActiveBottom = VH - (int)mBottomBlockedPx;
    if (ActiveBottom < ActiveTop)
        ActiveBottom = ActiveTop;

    const bool AnyMouseButtonDown =
        Core::Platform::IsMouseButtonDown(0) ||
        Core::Platform::IsMouseButtonDown(1) ||
        Core::Platform::IsMouseButtonDown(2);

    if (Core::Platform::IsWindowFocused() &&
        Core::Platform::IsMouseInGameContentArea() &&
        !AnyMouseButtonDown &&
        MX >= 0 && MY >= ActiveTop && MX < VW && MY < ActiveBottom)
    {
        float EX = 0.0f, EY = 0.0f;
        if (MX < (int)mEdgePanMarginPx) EX = -1.0f;
        else if (MX > VW - (int)mEdgePanMarginPx) EX = 1.0f;
        if (MY < ActiveTop + (int)mEdgePanMarginPx) EY = -1.0f;
        else if (MY > ActiveBottom - (int)mEdgePanMarginPx) EY = 1.0f;
        if (EX != 0.0f || EY != 0.0f)
        {
            const float Length = std::sqrt(EX * EX + EY * EY);
            if (Length > 1.0f)
            {
                EX /= Length;
                EY /= Length;
            }
            mCamera->PanBy(EX * mMousePanSpeed * Dt, EY * mMousePanSpeed * Dt);
        }
    }

    mCamera->UpdateSmoothing(Dt);
}

} // namespace Game::Client
