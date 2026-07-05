#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include <utility>

namespace Resource::Graphics
{

bool ResourceCache::LoadCommandButtonGraphics(const GrpDecodeOptions& CommandButtonOptions)
{
    bool LoadedAny = false;
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\cmdicons.grp", mUI.CommandIcons, CommandButtonOptions, nullptr, false, "command icons");
    ExportPreferredUIFramesForGui("sprites\\cmdbtns\\cmdicons.grp", mUI.CommandIconUris, CommandButtonOptions, false, "command icons");
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\cmdicons.grp", mUI.CommandIconMasks, CommandButtonOptions, nullptr, true, "command icon masks");
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\tcmdbtns.grp",
                                      mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Terran)],
                                      CommandButtonOptions,
                                      nullptr,
                                      false,
                                      "terran command button skins");
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\pcmdbtns.grp",
                                      mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Protoss)],
                                      CommandButtonOptions,
                                      nullptr,
                                      false,
                                      "protoss command button skins");
    LoadedAny |= LoadPreferredUIFrames("sprites\\cmdbtns\\zcmdbtns.grp",
                                      mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Zerg)],
                                      CommandButtonOptions,
                                      nullptr,
                                      false,
                                      "zerg command button skins");
    return LoadedAny;
}

bool ResourceCache::LoadUIIconGraphics()
{
    ExportPreferredUIFramesForGui("game\\icons.grp", mUI.IconUris, {}, false, "UI icons");

    bool LoadedHdIcons = false;
    if (mSources.UseExtractedAssets)
    {
        LoadedHdIcons = LoadDdsGrpUIFrames("game\\icons.dds.grp", mUI.Icons);
        if (LoadedHdIcons)
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded HD UI icons from game/icons.dds.grp: " + std::to_string(mUI.Icons.size()) + " frames");
        }
    }
    if (!LoadedHdIcons)
        return LoadUIGrpFrames("game\\icons.grp", mUI.Icons);
    return true;
}

bool ResourceCache::LoadWireframeGraphics()
{
    return LoadPreferredUIFrames("sprites\\wirefram\\wirefram.grp",
                                 mUI.WireframeFrames,
                                 {},
                                 &mUI.WireframeOpaqueBounds,
                                 false,
                                 "wireframe frames");
}

bool ResourceCache::LoadBlinkGraphics()
{
    bool LoadedHdBlink = false;
    if (mSources.UseExtractedAssets)
    {
        LoadedHdBlink = LoadDdsGrpUIFirstFrame("game\\blink.dds.grp", mUI.Blink);
        if (LoadedHdBlink)
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded HD blink frame from game/blink.dds.grp");
        }
    }
    if (LoadedHdBlink)
        return true;

    auto BlinkData = ReadUIFileWithUpperFallback("game\\blink.grp");
    if (!BlinkData)
        return false;

    auto SpriteOpt = DecodeGrpToRgba(*BlinkData, mData.ActivePalette);
    if (!SpriteOpt || SpriteOpt->Frames.empty())
        return false;

    mUI.Blink = UploadRgbaFull(SpriteOpt->Frames[0].Width,
                               SpriteOpt->Frames[0].Height,
                               SpriteOpt->Frames[0].Rgba.data());
    return mUI.Blink.Valid();
}

bool ResourceCache::LoadCursorGraphics(const GrpDecodeOptions& CursorOptions)
{
    constexpr const char* CursorNames[] =
    {
        "arrow", "drag", "illegal", "targn", "targg", "targr", "targy",
        "scrollu", "scrolld", "scrolll", "scrollr",
        "scrollul", "scrollur", "scrolldl", "scrolldr",
        "magg", "magr", "magy", "time"
    };

    bool LoadedAny = false;
    for (const char* CursorName : CursorNames)
    {
        std::vector<GpuImage> Frames;
        if (LoadUIGrpFrames(std::string("cursor\\") + CursorName + ".grp", Frames, CursorOptions, nullptr, false))
        {
            mUI.CursorFrames.emplace(CursorName, std::move(Frames));
            LoadedAny = true;
        }
    }
    return LoadedAny;
}

} // namespace Resource::Graphics
