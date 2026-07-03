#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resource/GpuImage.h"
#include "Resource/PcxRegion.h"

namespace Resource::Graphics
{

enum class CommandButtonSkinSet : uint8_t
{
    Terran = 0,
    Protoss,
    Zerg,
    Count
};

struct ImageOpaqueBounds
{
    int X = 0;
    int Y = 0;
    int W = 0;
    int H = 0;
    bool Valid() const { return W > 0 && H > 0; }
};


struct PortraitFrame
{
    GpuImage Image = {};
    float DurationSeconds = 1.0f / 12.0f;
};

struct PortraitClip
{
    int Width = 0;
    int Height = 0;
    std::vector<PortraitFrame> Frames;
    bool Valid() const { return !Frames.empty(); }
};

struct UIAssetCacheState
{
    std::vector<GpuImage> Icons;
    std::vector<GpuImage> CommandIcons;
    std::vector<GpuImage> CommandIconMasks;
    std::array<std::vector<GpuImage>, static_cast<size_t>(CommandButtonSkinSet::Count)> CommandButtonSkins;
    std::vector<GpuImage> WireframeFrames;
    std::vector<ImageOpaqueBounds> WireframeOpaqueBounds;
    std::unordered_map<std::string, std::vector<GpuImage>> CursorFrames;
    std::unordered_map<std::string, PortraitClip> PortraitClips;
    GpuImage HudAggIcons = {};
    GpuImage HudTwireStrip = {};
    GpuImage HudTunitStrip = {};
    GpuImage HudHpBarEmptyStrip = {};
    GpuImage HudHpBarStrip = {};
    GpuImage ConsoleLeft = {};
    GpuImage ConsoleCenter = {};
    GpuImage ConsoleRight = {};
    GpuImage ConsolePortraitFrame = {};
    GpuImage ConsoleApm = {};
    GpuImage ConsoleTime = {};
    GpuImage Blink = {};
    GpuImage Console = {};
    GpuImage ConsoleOver = {};
    PcxRegions Regions;
};

} // namespace Resource::Graphics