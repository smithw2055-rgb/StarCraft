#pragma once

#include "Render/Camera2D.h"
#include "Render/Draw2D.h"
#include "Render/RenderQueue2D.h"

#include <cstdint>
#include <vector>

namespace Render
{

// ============================================================================
// CanvasItemRID: Godot 4 RenderingServer style generational resource ID (RID)
// Allows retained-mode registration of 2D canvas items in RenderServer2D so
// static/semi-static sprites and UI elements only update dirty transforms/states
// instead of rebuilding full descriptors from scratch every frame.
// ============================================================================
struct CanvasItemRID
{
    uint32_t Index = UINT32_MAX;
    uint32_t Generation = 0;

    constexpr bool IsValid() const { return Index != UINT32_MAX && Generation != 0; }
    constexpr bool operator==(const CanvasItemRID& Other) const
    {
        return Index == Other.Index && Generation == Other.Generation;
    }
    constexpr bool operator!=(const CanvasItemRID& Other) const
    {
        return !(*this == Other);
    }
};

struct RetainedCanvasItem2D
{
    uint32_t Generation = 0;
    bool Alive = false;
    bool Visible = true;
    RenderLayer2D Layer = RenderLayer2D::GroundEntities;
    RenderSubBand2D SubBand = RenderSubBand2D::RootSprite;
    int ZOrder = 0;

    GpuTextureHandle Texture = {};
    float X = 0.0f;
    float Y = 0.0f;
    float W = 32.0f;
    float H = 32.0f;
    float U0 = 0.0f;
    float V0 = 0.0f;
    float U1 = 1.0f;
    float V1 = 1.0f;
    Color Tint = Color(255, 255, 255, 255);
    bool FlipX = false;
};

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
// - Supports retained-mode CanvasItemRID registration & automatic extraction
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

    // ------------------------------------------------------------------------
    // Godot RenderingServer style CanvasItemRID lifecycle & state mutations
    // ------------------------------------------------------------------------
    CanvasItemRID CanvasItemCreate()
    {
        uint32_t SlotIdx = UINT32_MAX;
        if (!mFreeCanvasItemSlots.empty())
        {
            SlotIdx = mFreeCanvasItemSlots.back();
            mFreeCanvasItemSlots.pop_back();
        }
        else
        {
            SlotIdx = static_cast<uint32_t>(mCanvasItems.size());
            mCanvasItems.emplace_back();
        }

        auto& Item = mCanvasItems[SlotIdx];
        if (Item.Generation == 0)
            Item.Generation = 1;
        Item.Alive = true;
        Item.Visible = true;
        ++mActiveCanvasItemCount;
        return CanvasItemRID{SlotIdx, Item.Generation};
    }

    bool CanvasItemFree(CanvasItemRID Rid)
    {
        auto* Item = ResolveCanvasItemMutable(Rid);
        if (!Item)
            return false;

        Item->Alive = false;
        Item->Visible = false;
        Item->Texture = {};
        ++Item->Generation;
        if (Item->Generation == 0)
            Item->Generation = 1;
        if (mActiveCanvasItemCount > 0)
            --mActiveCanvasItemCount;
        mFreeCanvasItemSlots.push_back(Rid.Index);
        return true;
    }

    bool IsCanvasItemValid(CanvasItemRID Rid) const
    {
        return ResolveCanvasItem(Rid) != nullptr;
    }

    bool CanvasItemSetTransform(CanvasItemRID Rid, float X, float Y, int ZOrder = 0)
    {
        auto* Item = ResolveCanvasItemMutable(Rid);
        if (!Item)
            return false;
        Item->X = X;
        Item->Y = Y;
        Item->ZOrder = ZOrder;
        return true;
    }

    bool CanvasItemSetSprite(CanvasItemRID Rid,
                             const GpuTextureHandle& Texture,
                             float W,
                             float H,
                             Color Tint = Color(255, 255, 255, 255),
                             RenderLayer2D Layer = RenderLayer2D::GroundEntities,
                             RenderSubBand2D SubBand = RenderSubBand2D::RootSprite,
                             bool FlipX = false)
    {
        auto* Item = ResolveCanvasItemMutable(Rid);
        if (!Item)
            return false;
        Item->Texture = Texture;
        Item->W = W;
        Item->H = H;
        Item->Tint = Tint;
        Item->Layer = Layer;
        Item->SubBand = SubBand;
        Item->FlipX = FlipX;
        return true;
    }

    bool CanvasItemSetVisible(CanvasItemRID Rid, bool Visible)
    {
        auto* Item = ResolveCanvasItemMutable(Rid);
        if (!Item)
            return false;
        Item->Visible = Visible;
        return true;
    }

    uint32_t ExtractRetainedCanvasItemsToWorldQueue()
    {
        uint32_t Submitted = 0;
        for (const auto& Item : mCanvasItems)
        {
            if (!Item.Alive || !Item.Visible || !Item.Texture.Valid())
                continue;
            const uint16_t DepthKey = SortKey2D::EncodeDepthKey(Item.Y + Item.H * 0.5f, Item.ZOrder);
            ScopedSortContext2D Scope(mWorldQueue, Item.Layer, Item.SubBand, DepthKey);
            mWorldQueue.SubmitSprite(
                Item.Texture,
                Item.X, Item.Y, Item.W, Item.H,
                Item.U0, Item.V0, Item.U1, Item.V1,
                Item.Tint, Item.FlipX);
            ++Submitted;
        }
        return Submitted;
    }

    size_t GetActiveCanvasItemCount() const { return mActiveCanvasItemCount; }

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
    RetainedCanvasItem2D* ResolveCanvasItemMutable(CanvasItemRID Rid)
    {
        if (!Rid.IsValid() || Rid.Index >= mCanvasItems.size())
            return nullptr;
        auto& Item = mCanvasItems[Rid.Index];
        if (!Item.Alive || Item.Generation != Rid.Generation)
            return nullptr;
        return &Item;
    }

    const RetainedCanvasItem2D* ResolveCanvasItem(CanvasItemRID Rid) const
    {
        if (!Rid.IsValid() || Rid.Index >= mCanvasItems.size())
            return nullptr;
        const auto& Item = mCanvasItems[Rid.Index];
        if (!Item.Alive || Item.Generation != Rid.Generation)
            return nullptr;
        return &Item;
    }

    RenderQueue2D mWorldQueue;
    RenderQueue2D mOverlayQueue;
    RenderQueue2D mScreenQueue;
    RenderFrameStats2D mLastFrameStats = {};
    std::vector<RetainedCanvasItem2D> mCanvasItems;
    std::vector<uint32_t> mFreeCanvasItemSlots;
    size_t mActiveCanvasItemCount = 0;
};

} // namespace Render
