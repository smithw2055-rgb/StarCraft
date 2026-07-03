#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/PcxToGpu.h"
#include "Resource/RgbaImageFileDecoder.h"
#include "Resource/UIAssetImageHelpers.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace Resource::Graphics
{

namespace
{
    constexpr int kConsoleCompositeTargetWidth = 640;
    constexpr int kConsoleCompositeLeftWidth = 176;
    constexpr int kConsoleCompositeRightWidth = 216;
}

bool ResourceCache::LoadNamedConsoleDds(const std::string& FileName,
                                        GpuImage& Out,
                                        bool TransparentBlack)
{
    if (!mSources.UseExtractedAssets)
        return false;
    return LoadWicUIImage(std::string("game/consoles/terran/") + FileName, Out, TransparentBlack);
}

bool ResourceCache::BuildConsoleCompositeFromDds(GpuImage& Out)
{
#ifdef _WIN32
    auto LeftPath = FindExtractedUIAssetPath("game/consoles/terran/console_left.dds");
    auto CenterPath = FindExtractedUIAssetPath("game/consoles/terran/console_center.dds");
    auto RightPath = FindExtractedUIAssetPath("game/consoles/terran/console_right.dds");
    if (!LeftPath || !CenterPath || !RightPath)
        return false;

    auto LeftImage = LoadWicRgbaImageFromFile(*LeftPath);
    auto CenterImage = LoadWicRgbaImageFromFile(*CenterPath);
    auto RightImage = LoadWicRgbaImageFromFile(*RightPath);
    if (!LeftImage || !CenterImage || !RightImage)
        return false;

    constexpr int TargetWidth = kConsoleCompositeTargetWidth;
    constexpr int TargetLeftWidth = kConsoleCompositeLeftWidth;
    constexpr int TargetRightWidth = kConsoleCompositeRightWidth;
    constexpr int TargetCenterWidth = TargetWidth - TargetLeftWidth - TargetRightWidth;
    const int TargetHeight = std::max({LeftImage->Height, CenterImage->Height, RightImage->Height});
    if (TargetCenterWidth <= 0 || TargetHeight <= 0)
        return false;

    auto LeftScaled = ResizeRgbaImage(*LeftImage, TargetLeftWidth, TargetHeight);
    auto CenterScaled = ResizeRgbaImage(*CenterImage, TargetCenterWidth, TargetHeight);
    auto RightScaled = ResizeRgbaImage(*RightImage, TargetRightWidth, TargetHeight);
    if (!LeftScaled || !CenterScaled || !RightScaled)
        return false;

    std::vector<uint8_t> Pixels((size_t)TargetWidth * (size_t)TargetHeight * 4u, 0);
    AlphaBlendInto(Pixels, TargetWidth, TargetHeight, *LeftScaled, 0, 0);
    AlphaBlendInto(Pixels, TargetWidth, TargetHeight, *CenterScaled, TargetLeftWidth, 0);
    AlphaBlendInto(Pixels, TargetWidth, TargetHeight, *RightScaled, TargetLeftWidth + TargetCenterWidth, 0);

    Out = UploadRgbaFull(TargetWidth, TargetHeight, Pixels.data());
    return Out.Valid();
#else
    (void)Out;
    return false;
#endif
}

bool ResourceCache::LoadHdConsoleGraphics()
{
    if (!mSources.UseExtractedAssets)
        return false;

    LoadNamedConsoleDds("console_left.DDS", mUI.ConsoleLeft);
    LoadNamedConsoleDds("console_center.DDS", mUI.ConsoleCenter);
    LoadNamedConsoleDds("console_right.DDS", mUI.ConsoleRight);
    LoadNamedConsoleDds("conover.DDS", mUI.ConsolePortraitFrame);
    LoadNamedConsoleDds("console_APM.DDS", mUI.ConsoleApm);
    LoadNamedConsoleDds("console_TIME.DDS", mUI.ConsoleTime);

    if (BuildConsoleCompositeFromDds(mUI.Console))
    {
        Core::Utils::Logger::Info("ResourceCache",
            "Built terran HUD console composite from extracted DDS slices");
        return true;
    }
    return mUI.ConsoleLeft.Valid() || mUI.ConsoleCenter.Valid() || mUI.ConsoleRight.Valid();
}

bool ResourceCache::LoadClassicConsoleGraphics()
{
    bool LoadedAny = false;

    auto ConsoleData = ReadUIFileWithUpperFallback("game\\nconsole.pcx");
    if (ConsoleData)
    {
        auto PcxOpt = DecodePcxToRgba(*ConsoleData);
        if (PcxOpt)
        {
            mUI.Console = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
            mUI.Regions = PcxOpt->Regions;
            LoadedAny = LoadedAny || mUI.Console.Valid();
        }
    }

    auto ConsoleOverData = ReadUIFileWithUpperFallback("game\\tconsole.pcx");
    if (ConsoleOverData)
    {
        auto PcxOpt = DecodePcxToRgba(*ConsoleOverData);
        if (PcxOpt)
        {
            mUI.ConsoleOver = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
            LoadedAny = LoadedAny || mUI.ConsoleOver.Valid();
        }
    }

    return LoadedAny;
}

bool ResourceCache::LoadHudChromeGraphics()
{
    bool LoadedAny = false;
    LoadedAny |= LoadPcxUIImage("game\\aggicons.pcx", mUI.HudAggIcons, true);

    // The extracted asset set has clear HD replacements for the console shell and HP bar,
    // but not for the old twire/tunit decoration strips. In extracted-assets mode those
    // classic strips produce obvious distortion when stretched into the new FHD layout,
    // so prefer the HUD's rectangle fallback until we identify a true sliced replacement.
    if (!mSources.UseExtractedAssets)
    {
        LoadedAny |= LoadPcxUIImage("game\\twire.pcx", mUI.HudTwireStrip, true);
        LoadedAny |= LoadPcxUIImage("game\\tunit.pcx", mUI.HudTunitStrip, true);
    }

    bool LoadedHdHpBar = false;
    if (mSources.UseExtractedAssets)
    {
        const bool LoadedEmpty = LoadNamedConsoleDds("pbrempt.DDS", mUI.HudHpBarEmptyStrip, true);
        const bool LoadedFull = LoadNamedConsoleDds("pbrfull.DDS", mUI.HudHpBarStrip, true);
        LoadedHdHpBar = LoadedEmpty && LoadedFull;
        if (LoadedHdHpBar)
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded HD HP bar strips from game/consoles/terran/pbrempt.DDS and pbrfull.DDS");
        }
        else
        {
            Core::Utils::Logger::Info("ResourceCache",
                "HD HP bar DDS availability: empty=" + std::to_string((int)LoadedEmpty) +
                ", full=" + std::to_string((int)LoadedFull) + "; falling back to thpbar.pcx");
        }
    }
    if (!LoadedHdHpBar)
        LoadedAny |= LoadPcxUIImage("game\\thpbar.pcx", mUI.HudHpBarStrip, true);
    else
        LoadedAny = true;

    return LoadedAny;
}

} // namespace Resource::Graphics
