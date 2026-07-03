#include "Resource/ResourceCache.h"

#include <cstddef>
#include <string>
#include <vector>

namespace Resource::Graphics
{

const std::vector<GpuImage>* ResourceCache::GetUIIcons()
{
    return mUI.Icons.empty() ? nullptr : &mUI.Icons;
}

const std::vector<GpuImage>* ResourceCache::GetCommandIcons()
{
    return mUI.CommandIcons.empty() ? nullptr : &mUI.CommandIcons;
}

const std::vector<GpuImage>* ResourceCache::GetCommandIconMasks()
{
    return mUI.CommandIconMasks.empty() ? nullptr : &mUI.CommandIconMasks;
}

const std::vector<GpuImage>* ResourceCache::GetCommandButtonSkins(CommandButtonSkinSet SkinSet)
{
    const auto& Frames = mUI.CommandButtonSkins[static_cast<size_t>(SkinSet)];
    return Frames.empty() ? nullptr : &Frames;
}

const std::vector<GpuImage>* ResourceCache::GetWireframeFrames()
{
    return mUI.WireframeFrames.empty() ? nullptr : &mUI.WireframeFrames;
}

const ImageOpaqueBounds* ResourceCache::GetWireframeOpaqueBounds(int FrameIndex) const
{
    if (FrameIndex < 0 || FrameIndex >= (int)mUI.WireframeOpaqueBounds.size())
        return nullptr;
    return &mUI.WireframeOpaqueBounds[(size_t)FrameIndex];
}

const std::vector<GpuImage>* ResourceCache::GetCursorFrames(const std::string& CursorName)
{
    auto It = mUI.CursorFrames.find(CursorName);
    return It != mUI.CursorFrames.end() ? &It->second : nullptr;
}

GpuImage ResourceCache::GetHudAggIcons() { return mUI.HudAggIcons; }
GpuImage ResourceCache::GetHudTwireStrip() { return mUI.HudTwireStrip; }
GpuImage ResourceCache::GetHudTunitStrip() { return mUI.HudTunitStrip; }
GpuImage ResourceCache::GetHudHpBarEmptyStrip() { return mUI.HudHpBarEmptyStrip; }
GpuImage ResourceCache::GetHudHpBarStrip() { return mUI.HudHpBarStrip; }
GpuImage ResourceCache::GetUIConsoleLeft() { return mUI.ConsoleLeft; }
GpuImage ResourceCache::GetUIConsoleCenter() { return mUI.ConsoleCenter; }
GpuImage ResourceCache::GetUIConsoleRight() { return mUI.ConsoleRight; }
GpuImage ResourceCache::GetUIConsolePortraitFrame() { return mUI.ConsolePortraitFrame; }
GpuImage ResourceCache::GetUIConsoleApm() { return mUI.ConsoleApm; }
GpuImage ResourceCache::GetUIConsoleTime() { return mUI.ConsoleTime; }
GpuImage ResourceCache::GetUIConsole() { return mUI.Console; }
GpuImage ResourceCache::GetUIConsoleOver() { return mUI.ConsoleOver; }

} // namespace Resource::Graphics
