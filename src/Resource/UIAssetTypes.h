#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resource/GpuImage.h"
namespace Resource
{

struct PcxRegion
{
    int X = 0, Y = 0, W = 0, H = 0;
};

struct PcxRegions
{
    std::vector<PcxRegion> Minimap;
    std::vector<PcxRegion> Portrait;
    std::vector<PcxRegion> Info;
    std::vector<PcxRegion> CmdButtons;
    std::vector<PcxRegion> Resources;
};

} // namespace Resource
#include "Resource/PortraitVideoDecoder.h"

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
    bool StreamingWebm = false;
    std::vector<std::filesystem::path> WebmPaths;
    WebmVideoDecoder WebmDecoder;
    size_t ActiveWebmPath = 0;
    PortraitFrame CurrentFrame;
    double NextFrameTimeSeconds = 0.0;
    std::vector<PortraitFrame> Frames;
    bool Valid() const { return StreamingWebm ? CurrentFrame.Image.Valid() : !Frames.empty(); }
};

struct UIAssetCacheState
{
    std::vector<GpuImage> Icons;
    std::vector<std::string> IconUris;
    std::vector<GpuImage> CommandIcons;
    std::vector<GpuImage> CommandIconMasks;
    std::vector<std::string> CommandIconUris;
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
