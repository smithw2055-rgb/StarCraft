#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"

namespace Engine::Input
{

// ============================================================================
// CameraAndSelectionServer2D (Phase 74):
// Referenced from Godot 4 Camera2D (drag margins, world limits, smoothing,
// canvas transform) & Unity DOTS / RTS Selection & ControlGroup Subsystem:
// - General-purpose 2D camera controller (viewport clamping, 8-way edge scroll,
//   keyboard pan, zoom, screen<->world conversion, minimap click projection)
// - Generational-handle-safe box selection & 10-slot control group manager.
// ============================================================================
struct CameraRigConfig2D
{
    int32_t ViewportWidthPx = 1280;
    int32_t ViewportHeightPx = 720;
    float WorldWidthPx = 4096.0f;
    float WorldHeightPx = 4096.0f;
    float EdgeMarginPx = 16.0f;
    float TopHudBlockedPx = 24.0f;
    float BottomHudBlockedPx = 160.0f;
    float PanSpeedPxPerSec = 750.0f;
    float MinZoom = 0.5f;
    float MaxZoom = 2.5f;
};

struct CameraAndSelectionTelemetry
{
    uint64_t CameraStepsEvaluated = 0;
    uint64_t BoxSelectionsExecuted = 0;
    uint64_t ControlGroupOperations = 0;
};

class CameraAndSelectionServer2D
{
public:
    static constexpr size_t kMaxControlGroups = 10;
    static constexpr size_t kDefaultMaxSelectionSize = 12;

    CameraAndSelectionServer2D() = default;

    void ConfigureCamera(const CameraRigConfig2D& Config) noexcept
    {
        mConfig = Config;
        ClampCameraCenter();
    }

    void SetWorldBoundsByTiles(int32_t WidthTiles, int32_t HeightTiles, float TileSizePx = 32.0f) noexcept
    {
        mConfig.WorldWidthPx = std::max(TileSizePx, static_cast<float>(WidthTiles) * TileSizePx);
        mConfig.WorldHeightPx = std::max(TileSizePx, static_cast<float>(HeightTiles) * TileSizePx);
        ClampCameraCenter();
    }

    void SetViewportSize(int32_t WidthPx, int32_t HeightPx) noexcept
    {
        mConfig.ViewportWidthPx = std::max(1, WidthPx);
        mConfig.ViewportHeightPx = std::max(1, HeightPx);
        ClampCameraCenter();
    }

    void SetCameraCenter(Core::Math::Vec2 CenterWorldPos) noexcept
    {
        mCameraCenter = CenterWorldPos;
        ClampCameraCenter();
    }

    Core::Math::Vec2 GetCameraCenter() const noexcept { return mCameraCenter; }

    void SetZoom(float Zoom) noexcept
    {
        mZoom = std::clamp(Zoom, mConfig.MinZoom, mConfig.MaxZoom);
        ClampCameraCenter();
    }

    float GetZoom() const noexcept { return mZoom; }

    Core::Math::Vec2 ComputeEdgeScrollDirection(int32_t MouseX, int32_t MouseY) const noexcept
    {
        const int32_t VW = mConfig.ViewportWidthPx;
        const int32_t VH = mConfig.ViewportHeightPx;
        if (MouseX < 0 || MouseY < 0 || MouseX >= VW || MouseY >= VH)
            return {0.0f, 0.0f};

        const int32_t Margin = static_cast<int32_t>(mConfig.EdgeMarginPx);
        const int32_t ActiveTop = std::max(0, static_cast<int32_t>(mConfig.TopHudBlockedPx));
        const int32_t ActiveBottom = std::max(ActiveTop, VH - static_cast<int32_t>(mConfig.BottomHudBlockedPx));

        float DX = 0.0f;
        float DY = 0.0f;
        if (MouseX < Margin)
            DX = -1.0f;
        else if (MouseX >= VW - Margin)
            DX = 1.0f;

        if ((MouseY >= ActiveTop && MouseY < ActiveTop + Margin) || MouseY <= 4)
            DY = -1.0f;
        else if ((MouseY > ActiveBottom - Margin && MouseY <= ActiveBottom + 8) || MouseY >= VH - 8)
            DY = 1.0f;

        const float Len = std::sqrt(DX * DX + DY * DY);
        if (Len > 1.0f)
        {
            DX /= Len;
            DY /= Len;
        }
        return {DX, DY};
    }

    Core::Math::Vec2 StepCameraEdgeAndKeyPan(int32_t MouseX,
                                             int32_t MouseY,
                                             Core::Math::Vec2 KeyDir,
                                             float DeltaSeconds) noexcept
    {
        ++mTelemetry.CameraStepsEvaluated;
        Core::Math::Vec2 Dir = ComputeEdgeScrollDirection(MouseX, MouseY);
        Dir.X += KeyDir.X;
        Dir.Y += KeyDir.Y;
        const float Len = std::sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y);
        if (Len > 1.0f)
        {
            Dir.X /= Len;
            Dir.Y /= Len;
        }

        const float EffectiveZoom = std::max(0.1f, mZoom);
        const float StepPx = (mConfig.PanSpeedPxPerSec / EffectiveZoom) * std::max(0.0f, DeltaSeconds);
        mCameraCenter.X += Dir.X * StepPx;
        mCameraCenter.Y += Dir.Y * StepPx;
        ClampCameraCenter();
        return mCameraCenter;
    }

    Core::Math::Vec2 ScreenToWorld(Core::Math::Vec2 ScreenPos) const noexcept
    {
        const float EffectiveZoom = std::max(0.1f, mZoom);
        const float HalfW = static_cast<float>(mConfig.ViewportWidthPx) * 0.5f / EffectiveZoom;
        const float HalfH = static_cast<float>(mConfig.ViewportHeightPx) * 0.5f / EffectiveZoom;
        return {
            (mCameraCenter.X - HalfW) + ScreenPos.X / EffectiveZoom,
            (mCameraCenter.Y - HalfH) + ScreenPos.Y / EffectiveZoom
        };
    }

    Core::Math::Vec2 WorldToScreen(Core::Math::Vec2 WorldPos) const noexcept
    {
        const float EffectiveZoom = std::max(0.1f, mZoom);
        const float HalfW = static_cast<float>(mConfig.ViewportWidthPx) * 0.5f / EffectiveZoom;
        const float HalfH = static_cast<float>(mConfig.ViewportHeightPx) * 0.5f / EffectiveZoom;
        return {
            (WorldPos.X - (mCameraCenter.X - HalfW)) * EffectiveZoom,
            (WorldPos.Y - (mCameraCenter.Y - HalfH)) * EffectiveZoom
        };
    }

    Core::Math::Vec2 CenterCameraFromNormalizedMinimap(float NormX, float NormY) noexcept
    {
        mCameraCenter.X = std::clamp(NormX, 0.0f, 1.0f) * mConfig.WorldWidthPx;
        mCameraCenter.Y = std::clamp(NormY, 0.0f, 1.0f) * mConfig.WorldHeightPx;
        ClampCameraCenter();
        return mCameraCenter;
    }

    // Selection & Generational Handle Management
    void SetMaxSelectionSize(size_t MaxSize) noexcept
    {
        mMaxSelectionSize = std::max<size_t>(1u, MaxSize);
    }

    void SetActiveSelection(const Core::ECS::EntityManager& EM,
                            const std::vector<Core::ECS::EntityID>& Entities)
    {
        ++mTelemetry.BoxSelectionsExecuted;
        mSelectedEntities.clear();
        mSelectedHandles.clear();
        for (Core::ECS::EntityID Id : Entities)
        {
            if (mSelectedEntities.size() >= mMaxSelectionSize)
                break;
            if (!EM.IsAlive(Id))
                continue;
            if (std::find(mSelectedEntities.begin(), mSelectedEntities.end(), Id) != mSelectedEntities.end())
                continue;
            mSelectedEntities.push_back(Id);
            mSelectedHandles.push_back(EM.GetEntityHandle(Id));
        }
    }

    void PruneDeadSelection(const Core::ECS::EntityManager& EM)
    {
        std::vector<Core::ECS::EntityID> AliveIds;
        std::vector<Core::ECS::EntityHandle> AliveHandles;
        for (size_t i = 0; i < mSelectedHandles.size(); ++i)
        {
            if (EM.IsAlive(mSelectedHandles[i]))
            {
                AliveIds.push_back(mSelectedHandles[i].Id);
                AliveHandles.push_back(mSelectedHandles[i]);
            }
        }
        mSelectedEntities = std::move(AliveIds);
        mSelectedHandles = std::move(AliveHandles);
    }

    bool SaveControlGroup(size_t GroupIndex, const Core::ECS::EntityManager& EM)
    {
        if (GroupIndex >= kMaxControlGroups)
            return false;
        ++mTelemetry.ControlGroupOperations;
        PruneDeadSelection(EM);
        mControlGroups[GroupIndex] = mSelectedHandles;
        return true;
    }

    std::vector<Core::ECS::EntityID> RecallControlGroup(size_t GroupIndex,
                                                        const Core::ECS::EntityManager& EM)
    {
        if (GroupIndex >= kMaxControlGroups)
            return {};
        ++mTelemetry.ControlGroupOperations;
        std::vector<Core::ECS::EntityID> Recalled;
        std::vector<Core::ECS::EntityHandle> ValidHandles;
        for (const auto& H : mControlGroups[GroupIndex])
        {
            if (EM.IsAlive(H))
            {
                Recalled.push_back(H.Id);
                ValidHandles.push_back(H);
            }
        }
        mControlGroups[GroupIndex] = std::move(ValidHandles);
        SetActiveSelection(EM, Recalled);
        return mSelectedEntities;
    }

    const std::vector<Core::ECS::EntityID>& GetSelectedEntities() const noexcept { return mSelectedEntities; }
    const std::vector<Core::ECS::EntityHandle>& GetSelectedHandles() const noexcept { return mSelectedHandles; }
    const CameraRigConfig2D& GetConfig() const noexcept { return mConfig; }
    const CameraAndSelectionTelemetry& GetTelemetry() const noexcept { return mTelemetry; }

private:
    void ClampCameraCenter() noexcept
    {
        const float EffectiveZoom = std::max(0.1f, mZoom);
        const float HalfW = static_cast<float>(mConfig.ViewportWidthPx) * 0.5f / EffectiveZoom;
        const float HalfH = static_cast<float>(mConfig.ViewportHeightPx) * 0.5f / EffectiveZoom;

        const float MinX = std::min(HalfW, mConfig.WorldWidthPx * 0.5f);
        const float MaxX = std::max(MinX, mConfig.WorldWidthPx - HalfW);
        const float MinY = std::min(HalfH, mConfig.WorldHeightPx * 0.5f);
        const float MaxY = std::max(MinY, mConfig.WorldHeightPx - HalfH);

        mCameraCenter.X = std::clamp(mCameraCenter.X, MinX, MaxX);
        mCameraCenter.Y = std::clamp(mCameraCenter.Y, MinY, MaxY);
    }

    CameraRigConfig2D mConfig{};
    Core::Math::Vec2 mCameraCenter{640.0f, 360.0f};
    float mZoom = 1.0f;
    size_t mMaxSelectionSize = kDefaultMaxSelectionSize;

    std::vector<Core::ECS::EntityID> mSelectedEntities;
    std::vector<Core::ECS::EntityHandle> mSelectedHandles;
    std::array<std::vector<Core::ECS::EntityHandle>, kMaxControlGroups> mControlGroups{};
    CameraAndSelectionTelemetry mTelemetry{};
};

} // namespace Engine::Input
