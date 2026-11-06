#pragma once

#include "Render/Camera2D.h"
#include "Render/Draw2D.h"
#include "Render/RenderQueue2D.h"

#include <cstdint>

namespace Render
{

struct RenderFrameStats2D
{
    uint64_t FrameIndex = 0;
    RenderQueueStats WorldStats = {};
    RenderQueueStats OverlayStats = {};
    RenderQueueStats ScreenStats = {};

    uint32_t TotalRecordedCommands() const
    {
        return WorldStats.RecordedCommands +
               OverlayStats.RecordedCommands +
               ScreenStats.RecordedCommands;
    }

    uint32_t TotalCulledCommands() const
    {
        return WorldStats.CulledCommands +
               OverlayStats.CulledCommands +
               ScreenStats.CulledCommands;
    }

    uint32_t TotalSortedFlushes() const
    {
        return WorldStats.SortedFlushes +
               OverlayStats.SortedFlushes +
               ScreenStats.SortedFlushes;
    }

    uint32_t TotalStateChangesSaved() const
    {
        return WorldStats.StateChangesSaved +
               OverlayStats.StateChangesSaved +
               ScreenStats.StateChangesSaved;
    }
};

// ============================================================================
// RenderServer2D: Godot RenderingServer / Unity SRP style 2D rendering coordinator
// - Separates command submission (ECS EntityRenderer / UI) from GPU execution (Draw2D)
// - Manages World, Overlay, and Screen render queues with 64-bit SortKey sorting
// - Performs automatic camera frustum culling on world-space submissions
// ============================================================================
class RenderServer2D
{
public:
    RenderServer2D()
    {
        mWorldQueue.Reserve(4096);
        mOverlayQueue.Reserve(1024);
        mScreenQueue.Reserve(1024);
    }

    void BeginWorldFrame(float CamX,
                         float CamY,
                         float ViewW,
                         float ViewH,
                         float Zoom,
                         float CullMargin = 128.0f)
    {
        mWorldQueue.Clear();
        mOverlayQueue.Clear();
        if (Zoom > 0.0f && ViewW > 0.0f && ViewH > 0.0f)
        {
            const float HalfW = ViewW * 0.5f / Zoom;
            const float HalfH = ViewH * 0.5f / Zoom;
            mWorldQueue.SetCullBounds(
                CamX - HalfW - CullMargin,
                CamY - HalfH - CullMargin,
                CamX + HalfW + CullMargin,
                CamY + HalfH + CullMargin,
                true);
        }
        else
        {
            mWorldQueue.DisableCulling();
        }
        mOverlayQueue.DisableCulling();
    }

    void BeginScreenFrame()
    {
        mScreenQueue.Clear();
        mScreenQueue.DisableCulling();
    }

    void FlushWorldFrame(Draw2D& Target)
    {
        mLastFrameStats.WorldStats = mWorldQueue.ComputeStats();
        mWorldQueue.Playback(Target);

        mLastFrameStats.OverlayStats = mOverlayQueue.ComputeStats();
        mOverlayQueue.Playback(Target);

        ++mLastFrameStats.FrameIndex;
    }

    void FlushScreenFrame(Draw2D& Target)
    {
        mLastFrameStats.ScreenStats = mScreenQueue.ComputeStats();
        mScreenQueue.Playback(Target);
    }

    RenderQueue2D& GetWorldQueue() { return mWorldQueue; }
    const RenderQueue2D& GetWorldQueue() const { return mWorldQueue; }

    RenderQueue2D& GetOverlayQueue() { return mOverlayQueue; }
    const RenderQueue2D& GetOverlayQueue() const { return mOverlayQueue; }

    RenderQueue2D& GetScreenQueue() { return mScreenQueue; }
    const RenderQueue2D& GetScreenQueue() const { return mScreenQueue; }

    const RenderFrameStats2D& GetLastFrameStats() const { return mLastFrameStats; }

private:
    RenderQueue2D mWorldQueue;
    RenderQueue2D mOverlayQueue;
    RenderQueue2D mScreenQueue;
    RenderFrameStats2D mLastFrameStats = {};
};

} // namespace Render
