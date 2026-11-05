#pragma once

#include "third_party/HandmadeMath.h"

namespace Render
{

// 2D camera: orthogonal projection, world-units = screen pixels (with zoom).
// Y axis: top-left origin like screen, matches RTS convention.
class Camera2D
{
public:
    Camera2D() = default;

    void SetViewport(int Width, int Height)
    {
        mViewportW = Width;
        mViewportH = Height;
        ClampTargetToBounds();
        mX = mTargetX;
        mY = mTargetY;
    }
    int GetViewportWidth() const { return mViewportW; }
    int GetViewportHeight() const { return mViewportH; }

    void SetPosition(float X, float Y)
    {
        mTargetX = X;
        mTargetY = Y;
        ClampTargetToBounds();
        mX = mTargetX;
        mY = mTargetY;
    }
    void SetTarget(float X, float Y)
    {
        mTargetX = X;
        mTargetY = Y;
        ClampTargetToBounds();
    }
    void TranslateTarget(float DX, float DY)
    {
        mTargetX += DX;
        mTargetY += DY;
        ClampTargetToBounds();
    }
    void PanBy(float DX, float DY)
    {
        mTargetX += DX;
        mTargetY += DY;
        ClampTargetToBounds();
    }

    void ZoomAt(float ScreenX, float ScreenY, float Delta)
    {
        float WX, WY;
        ScreenToWorld(ScreenX, ScreenY, WX, WY);
        AdjustZoom(Delta);
        if (mTargetZoom > 0.0001f)
        {
            const float HalfW = (mViewportW * 0.5f) / mTargetZoom;
            const float HalfH = (mViewportH * 0.5f) / mTargetZoom;
            mTargetX = WX + HalfW - ScreenX / mTargetZoom;
            mTargetY = WY + HalfH - ScreenY / mTargetZoom;
            ClampTargetToBounds();
        }
    }

    float GetX() const { return mX; }
    float GetY() const { return mY; }
    float GetTargetX() const { return mTargetX; }
    float GetTargetY() const { return mTargetY; }

    void SetZoom(float Zoom)
    {
        mZoom = Zoom;
        mTargetZoom = Zoom;
        ClampTargetToBounds();
        mX = mTargetX;
        mY = mTargetY;
    }
    void AdjustZoom(float Delta)
    {
        mTargetZoom += Delta;
        if (mTargetZoom < mMinZoom) mTargetZoom = mMinZoom;
        if (mTargetZoom > mMaxZoom) mTargetZoom = mMaxZoom;
        ClampTargetToBounds();
    }
    float GetZoom() const { return mZoom; }

    void SetZoomBounds(float Min, float Max) { mMinZoom = Min; mMaxZoom = Max; }
    void SetPanSpeed(float PxPerSec) { mPanSpeed = PxPerSec; }
    void SetSmoothSpeed(float S) { mSmoothSpeed = S; }

    void SetMapBounds(int MapWidthTiles, int MapHeightTiles, int TileSize = 32)
    {
        mMapMinX = 0;
        mMapMinY = 0;
        mMapMaxX = static_cast<float>(MapWidthTiles * TileSize);
        mMapMaxY = static_cast<float>(MapHeightTiles * TileSize);
        mHasBounds = true;
        ClampTargetToBounds();
        mX = mTargetX;
        mY = mTargetY;
    }
    void SetMapBoundsPx(float MinX, float MinY, float MaxX, float MaxY)
    {
        mMapMinX = MinX; mMapMinY = MinY; mMapMaxX = MaxX; mMapMaxY = MaxY;
        mHasBounds = true;
        ClampTargetToBounds();
        mX = mTargetX;
        mY = mTargetY;
    }
    void ClearMapBounds() { mHasBounds = false; }

    // Update (smooth interp) - alias for UpdateSmoothing
    void Update(float DeltaTime) { UpdateSmoothing(DeltaTime); }
    void UpdateSmoothing(float DeltaTime)
    {
        if (DeltaTime <= 0.0f) return;
        float T = DeltaTime * mSmoothSpeed;
        if (T > 1.0f) T = 1.0f;
        mX += (mTargetX - mX) * T;
        mY += (mTargetY - mY) * T;
        mZoom += (mTargetZoom - mZoom) * T;
        if (mHasBounds) ClampToBounds();
    }

    // Screen pixel position -> world (top-left origin in world)
    void ScreenToWorld(float ScreenX, float ScreenY, float& WorldX, float& WorldY) const
    {
        float HalfW = (mViewportW * 0.5f) / mZoom;
        float HalfH = (mViewportH * 0.5f) / mZoom;
        WorldX = (mX - HalfW) + ScreenX / mZoom;
        WorldY = (mY - HalfH) + ScreenY / mZoom;
    }

    // World -> screen pixel
    void WorldToScreen(float WorldX, float WorldY, float& ScreenX, float& ScreenY) const
    {
        float HalfW = (mViewportW * 0.5f) / mZoom;
        float HalfH = (mViewportH * 0.5f) / mZoom;
        ScreenX = (WorldX - (mX - HalfW)) * mZoom;
        ScreenY = (WorldY - (mY - HalfH)) * mZoom;
    }

    // Build an orthogonal projection where 1 world unit = 1/mZoom screen pixels
    HMM_Mat4 GetProjectionMatrix() const
    {
        float HalfW = (mViewportW * 0.5f) / mZoom;
        float HalfH = (mViewportH * 0.5f) / mZoom;
        float Left = mX - HalfW;
        float Right = mX + HalfW;
        float Top = mY - HalfH;
        float Bottom = mY + HalfH;
        return HMM_Orthographic_RH_NO(Left, Right, Bottom, Top, -1000.0f, 1000.0f);
    }

private:
    void ClampTargetToBounds()
    {
        if (!mHasBounds)
            return;

        float HalfW = (mViewportW * 0.5f) / mTargetZoom;
        float HalfH = (mViewportH * 0.5f) / mTargetZoom;
        float MinX = mMapMinX + HalfW;
        float MaxX = mMapMaxX - HalfW;
        float MinY = mMapMinY + HalfH;
        float MaxY = mMapMaxY - HalfH;
        if (MaxX < MinX) { mTargetX = (mMapMinX + mMapMaxX) * 0.5f; }
        else { if (mTargetX < MinX) mTargetX = MinX; if (mTargetX > MaxX) mTargetX = MaxX; }
        if (MaxY < MinY) { mTargetY = (mMapMinY + mMapMaxY) * 0.5f; }
        else { if (mTargetY < MinY) mTargetY = MinY; if (mTargetY > MaxY) mTargetY = MaxY; }
    }

    void ClampToBounds()
    {
        ClampTargetToBounds();
    }

    int mViewportW = 1280;
    int mViewportH = 720;

    float mX = 0.0f;
    float mY = 0.0f;
    float mTargetX = 0.0f;
    float mTargetY = 0.0f;

    float mZoom = 1.0f;
    float mTargetZoom = 1.0f;
    float mMinZoom = 0.25f;
    float mMaxZoom = 4.0f;
    float mPanSpeed = 1200.0f;
    float mSmoothSpeed = 24.0f;

    bool mHasBounds = false;
    float mMapMinX = 0.0f;
    float mMapMinY = 0.0f;
    float mMapMaxX = 0.0f;
    float mMapMaxY = 0.0f;
};

} // namespace Render
