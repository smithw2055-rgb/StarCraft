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

// === Inlined from src/Client/CameraController.cpp ===


#include <algorithm>
#include <cmath>

namespace Game::Client
{

inline void CameraController::SetBounds(int MapWidthTiles, int MapHeightTiles)
{
    mMapWidth = MapWidthTiles;
    mMapHeight = MapHeightTiles;
    if (mCamera)
    {
        mCamera->SetMapBounds(MapWidthTiles, MapHeightTiles);
    }
}

inline void CameraController::Update(float Dt)
{
    if (!mCamera) return;

    float Wheel = Core::Platform::GetMouseWheel();
    if (Wheel != 0.0f)
    {
        mCamera->ZoomAt(Core::Platform::GetMouseX(), Core::Platform::GetMouseY(), Wheel * 0.1f);
    }

    if (Core::Platform::IsKeyDown(Core::Platform::KeyCode::Equal) ||
        Core::Platform::IsKeyDown(Core::Platform::KeyCode::KpAdd))
        mCamera->ZoomAt(Core::Platform::GetMouseX(), Core::Platform::GetMouseY(), 0.05f);
    if (Core::Platform::IsKeyDown(Core::Platform::KeyCode::Minus) ||
        Core::Platform::IsKeyDown(Core::Platform::KeyCode::KpSubtract))
        mCamera->ZoomAt(Core::Platform::GetMouseX(), Core::Platform::GetMouseY(), -0.05f);

    if (Core::Platform::IsMouseButtonDown(2)) // 0 = LMB, 1 = RMB, 2 = MMB
    {
        float DX = Core::Platform::GetMouseDeltaX();
        float DY = Core::Platform::GetMouseDeltaY();
        mCamera->PanBy(-DX, -DY);
        mCamera->SetPosition(mCamera->GetTargetX(), mCamera->GetTargetY());
    }

    const float Zoom = mCamera->GetZoom();
    const float EffectiveZoom = Zoom > 0.1f ? Zoom : 1.0f;

    float KDX = 0.0f, KDY = 0.0f;
    if (Core::Platform::IsKeyDown(Core::Platform::KeyCode::Left)) KDX -= 1.0f;
    if (Core::Platform::IsKeyDown(Core::Platform::KeyCode::Right)) KDX += 1.0f;
    if (Core::Platform::IsKeyDown(Core::Platform::KeyCode::Up)) KDY -= 1.0f;
    if (Core::Platform::IsKeyDown(Core::Platform::KeyCode::Down)) KDY += 1.0f;
    if (KDX != 0.0f || KDY != 0.0f)
    {
        const float KLen = std::sqrt(KDX * KDX + KDY * KDY);
        if (KLen > 1.0f) { KDX /= KLen; KDY /= KLen; }
        const float PanStep = (mKeyboardPanSpeed / EffectiveZoom) * Dt;
        mCamera->PanBy(KDX * PanStep, KDY * PanStep);
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

    const int EdgeMargin = (int)mEdgePanMarginPx;

    if (Core::Platform::IsWindowFocused() &&
        Core::Platform::IsMouseInGameContentArea() &&
        !AnyMouseButtonDown &&
        MX >= 0 && MY >= 0 && MX < VW && MY < VH)
    {
        float EX = 0.0f, EY = 0.0f;
        if (MX < EdgeMargin)
            EX = -1.0f;
        else if (MX >= VW - EdgeMargin)
            EX = 1.0f;

        if ((MY >= ActiveTop && MY < ActiveTop + EdgeMargin) || (MY <= 4))
            EY = -1.0f;
        else if ((MY > ActiveBottom - EdgeMargin && MY <= ActiveBottom + 8) || (MY >= VH - 8))
            EY = 1.0f;

        if (EX != 0.0f || EY != 0.0f)
        {
            const float Length = std::sqrt(EX * EX + EY * EY);
            if (Length > 1.0f)
            {
                EX /= Length;
                EY /= Length;
            }
            const float PanStep = (mMousePanSpeed / EffectiveZoom) * Dt;
            mCamera->PanBy(EX * PanStep, EY * PanStep);
        }
    }

    mCamera->UpdateSmoothing(Dt);
}

} // namespace Game::Client
