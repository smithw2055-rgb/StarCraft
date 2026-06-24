#include "Render/CameraController.h"

namespace Render
{

CameraController::CameraController()
{
    mCamera.target = {0.0f, 0.0f};
    mCamera.offset = {0.0f, 0.0f};
    mCamera.rotation = 0.0f;
    mCamera.zoom = 1.0f;
}

void CameraController::ResetOffset()
{
    mCamera.offset = {GetScreenWidth() * 0.5f, GetScreenHeight() * 0.5f};
}

void CameraController::SetMapBounds(int WidthTiles, int HeightTiles, int BottomPanelPixels)
{
    mMapWidth = WidthTiles;
    mMapHeight = HeightTiles;
    mBottomPanelHeight = BottomPanelPixels;
}

void CameraController::Update(float DeltaTime)
{
    // Keyboard scroll
    Core::Math::Vec2 MoveDir;

    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))     MoveDir.Y -= 1.0f;
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))   MoveDir.Y += 1.0f;
    if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))   MoveDir.X -= 1.0f;
    if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT))  MoveDir.X += 1.0f;

    // Middle mouse drag
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE))
    {
        Vector2 Delta = GetMouseDelta();
        mCamera.target.x -= Delta.x / mCamera.zoom;
        mCamera.target.y -= Delta.y / mCamera.zoom;
    }

    if (MoveDir.LengthSq() > 0.0f)
    {
        MoveDir = MoveDir.Normalized();
        mCamera.target.x += MoveDir.X * mScrollSpeed * DeltaTime / mCamera.zoom;
        mCamera.target.y += MoveDir.Y * mScrollSpeed * DeltaTime / mCamera.zoom;
    }

    if (mMapWidth > 0 && mMapHeight > 0)
    {
        float MinX = 0;
        float MaxX = mMapWidth * 32.0f;
        float MinY = 0;
        float MaxY = mMapHeight * 32.0f;

        float SW = (float)GetScreenWidth();
        float SH = (float)GetScreenHeight();
        float HH = SH * 0.5f;

        // Camera center should be within these bounds
        float CMinX = MinX + SW * 0.5f / mCamera.zoom;
        float CMaxX = MaxX - SW * 0.5f / mCamera.zoom;
        float CMinY = MinY + HH / mCamera.zoom;
        float CMaxY = MaxY - HH / mCamera.zoom + (float)mBottomPanelHeight / mCamera.zoom;

        if (CMinX < CMaxX)
        {
            if (mCamera.target.x < CMinX) mCamera.target.x = CMinX;
            if (mCamera.target.x > CMaxX) mCamera.target.x = CMaxX;
        }
        if (CMinY < CMaxY)
        {
            if (mCamera.target.y < CMinY) mCamera.target.y = CMinY;
            if (mCamera.target.y > CMaxY) mCamera.target.y = CMaxY;
        }
    }

    // Zoom with mouse wheel
    float Wheel = GetMouseWheelMove();
    if (Wheel != 0.0f)
    {
        mCamera.zoom += Wheel * mZoomSpeed * mCamera.zoom;
        if (mCamera.zoom < mMinZoom) mCamera.zoom = mMinZoom;
        if (mCamera.zoom > mMaxZoom) mCamera.zoom = mMaxZoom;
    }
}

void CameraController::SetPosition(const Core::Math::Vec2& Pos)
{
    mCamera.target = {Pos.X, Pos.Y};
}

void CameraController::SetZoom(float Zoom)
{
    mCamera.zoom = Zoom;
}

void CameraController::MoveTo(const Core::Math::Vec2& Target)
{
    mCamera.target = {Target.X, Target.Y};
}

} // namespace Render
