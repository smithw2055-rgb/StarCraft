#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/UIAssetImageHelpers.h"



namespace Resource::Graphics
{


bool ResourceCache::LoadUIGraphics()
{
    std::optional<ColorPalette> UiPalette;
    if (auto TIcon = ReadUIFileWithUpperFallback("sprites\\cmdbtns\\ticon.pcx"))
        UiPalette = LoadPcxPalette(*TIcon);

    const ColorPalette* UiPalettePtr = UiPalette ? &*UiPalette : nullptr;

    GrpDecodeOptions CommandButtonOptions = {};
    CommandButtonOptions.PaletteOverride = UiPalettePtr;
    CommandButtonOptions.TransparentIndexZero = true;

    GrpDecodeOptions CursorOptions = CommandButtonOptions;
    CursorOptions.TransparentBlack = true;
    CursorOptions.TransparentRgb = RawColor(0, 0, 0, 0);

    LoadUIIconGraphics();
    LoadCommandButtonGraphics(CommandButtonOptions);
    LoadWireframeGraphics();
    LoadHudChromeGraphics();
    LoadBlinkGraphics();

    if (mSources.UseExtractedAssets)
        LoadHdConsoleGraphics();
    else
        LoadClassicConsoleGraphics();

    LoadCursorGraphics(CursorOptions);

    Core::Utils::Logger::Info("ResourceCache",
        "UI graphics: " + std::to_string(mUI.Icons.size()) + " icons, " +
        std::to_string(mUI.CommandIcons.size()) + " command icons, " +
        std::to_string(mUI.CommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Terran)].size()) +
        " terran button skins, " +
        std::to_string(mUI.CursorFrames.size()) + " cursors" +
        (mUI.Blink.Valid() ? ", blink" : "") +
        (mUI.Console.Valid() ? ", console" : "") +
        (mUI.ConsoleOver.Valid() ? ", conover" : ""));
    return true;
}

} // namespace Resource::Graphics

































// === Consolidated from src/Resource/ResourceCacheUIConsole.cpp ===
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

bool ResourceCache::LoadNamedConsoleDdsForRace(const std::string& RaceName,
                                              const std::string& FileName,
                                              GpuImage& Out,
                                              bool TransparentBlack)
{
    if (!mSources.UseExtractedAssets)
        return false;
    return LoadWicUIImage(std::string("game/consoles/") + RaceName + "/" + FileName, Out, TransparentBlack);
}

bool ResourceCache::LoadNamedConsoleDds(const std::string& FileName,
                                        GpuImage& Out,
                                        bool TransparentBlack)
{
    return LoadNamedConsoleDdsForRace("terran", FileName, Out, TransparentBlack);
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

bool ResourceCache::LoadHdConsoleGraphicsForRace(const std::string& RaceName)
{
    if (!mSources.UseExtractedAssets)
        return false;

    LoadNamedConsoleDdsForRace(RaceName, "console_left.DDS", mUI.ConsoleLeft);
    LoadNamedConsoleDdsForRace(RaceName, "console_center.DDS", mUI.ConsoleCenter);
    LoadNamedConsoleDdsForRace(RaceName, "console_right.DDS", mUI.ConsoleRight);
    LoadNamedConsoleDdsForRace(RaceName, "conover.DDS", mUI.ConsolePortraitFrame);
    LoadNamedConsoleDdsForRace(RaceName, "console_APM.DDS", mUI.ConsoleApm);
    LoadNamedConsoleDdsForRace(RaceName, "console_TIME.DDS", mUI.ConsoleTime);

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


// === Consolidated from src/Resource/ResourceCacheUIAssets.cpp ===
#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"
#include "Resource/DdsGrpDecoder.h"
#include "Resource/GrpToGpu.h"
#include "Resource/UIAssetImageHelpers.h"

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
#ifdef _WIN32
    if (mSources.UseExtractedAssets)
    {
        const std::string DdsPath = GetDdsGrpLogicalPath("unit\\wirefram\\wirefram.grp");
        auto DdsGrpData = ReadUIFileWithUpperFallback(DdsPath);
        if (DdsGrpData)
        {
            auto Parsed = ParseDdsGrp(*DdsGrpData);
            if (Parsed && !Parsed->Frames.empty())
            {
                mUI.WireframeFrames.resize(Parsed->Frames.size());
                mUI.WireframeOpaqueBounds.resize(Parsed->Frames.size());
                Core::Utils::Logger::Info("ResourceCache",
                    "Initialized lazy HD wireframe slots: " +
                    std::to_string(mUI.WireframeFrames.size()) + " frames");
                return true;
            }
        }
    }
#endif
    return LoadPreferredUIFrames("unit\\wirefram\\wirefram.grp",
                                 mUI.WireframeFrames,
                                 {},
                                 &mUI.WireframeOpaqueBounds,
                                 false,
                                 "wireframe frames");
}

bool ResourceCache::EnsureWireframeFrame(int FrameIndex)
{
    if (FrameIndex < 0 || FrameIndex >= static_cast<int>(mUI.WireframeFrames.size()))
        return false;

    if (mUI.WireframeFrames[static_cast<size_t>(FrameIndex)].Valid())
        return true;

#ifdef _WIN32
    if (mSources.UseExtractedAssets)
    {
        const std::string DdsPath = GetDdsGrpLogicalPath("unit\\wirefram\\wirefram.grp");
        auto DdsGrpData = ReadUIFileWithUpperFallback(DdsPath);
        if (DdsGrpData)
        {
            auto Parsed = ParseDdsGrp(*DdsGrpData);
            if (Parsed && FrameIndex < static_cast<int>(Parsed->Frames.size()))
            {
                const auto& Frame = Parsed->Frames[static_cast<size_t>(FrameIndex)];
                auto ImageOpt = LoadWicRgbaImageFromMemory(
                    DdsGrpData->data() + Frame.DataOffset,
                    Frame.ByteSize);
                if (ImageOpt && ImageOpt->Width > 0 && ImageOpt->Height > 0)
                {
                    if (FrameIndex < static_cast<int>(mUI.WireframeOpaqueBounds.size()))
                    {
                        mUI.WireframeOpaqueBounds[static_cast<size_t>(FrameIndex)] =
                            ComputeOpaqueBounds(ImageOpt->Rgba.data(), ImageOpt->Width, ImageOpt->Height);
                    }
                    mUI.WireframeFrames[static_cast<size_t>(FrameIndex)] =
                        UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
                    return mUI.WireframeFrames[static_cast<size_t>(FrameIndex)].Valid();
                }
            }
        }
    }
#endif

    // Fallback: decode single classic GRP frame
    auto GrpData = ReadUIFileWithUpperFallback("unit\\wirefram\\wirefram.grp");
    if (!GrpData)
        return false;

    auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette, {});
    if (!SpriteOpt || FrameIndex >= static_cast<int>(SpriteOpt->Frames.size()))
        return false;

    const auto& Frame = SpriteOpt->Frames[static_cast<size_t>(FrameIndex)];
    if (FrameIndex < static_cast<int>(mUI.WireframeOpaqueBounds.size()))
    {
        mUI.WireframeOpaqueBounds[static_cast<size_t>(FrameIndex)] =
            ComputeOpaqueBounds(Frame.Rgba.data(), Frame.Width, Frame.Height);
    }
    mUI.WireframeFrames[static_cast<size_t>(FrameIndex)] =
        UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data());
    return mUI.WireframeFrames[static_cast<size_t>(FrameIndex)].Valid();
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


// === Consolidated from src/Resource/ResourceCacheUIFrames.cpp ===
#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/DdsGrpDecoder.h"
#include "Resource/PcxToGpu.h"
#include "Resource/RgbaImageFileDecoder.h"
#include "Resource/UIAssetImageHelpers.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <objbase.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace Resource::Graphics
{

namespace
{
    std::string NormalizeLogicalPath(const std::string& Path)
    {
        std::string Normalized = Path;
        for (char& C : Normalized)
        {
            if (C == '\\')
                C = '/';
        }
        return Normalized;
    }

    std::string ToLowerCopy(const std::string& Value)
    {
        std::string Lower = Value;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        return Lower;
    }

    std::string MakeGuiExportStem(const std::string& Path, bool Hd, bool AsMask)
    {
        std::string Stem = ToLowerCopy(NormalizeLogicalPath(Path));
        for (char& C : Stem)
        {
            if (!std::isalnum((unsigned char)C))
                C = '_';
        }
        return Stem + (Hd ? "_hd" : "_classic") + (AsMask ? "_mask" : "");
    }

#ifdef _WIN32
    std::filesystem::path GetGuiExportDirectory()
    {
        return std::filesystem::temp_directory_path() / "StarCraftGuiAssetCache";
    }

    bool WritePngRgba(const std::filesystem::path& Path, const DecodedRgbaImage& Image)
    {
        using Microsoft::WRL::ComPtr;

        if (Image.Width <= 0 || Image.Height <= 0 || Image.Rgba.empty())
            return false;

        const HRESULT InitHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool NeedUninitialize = SUCCEEDED(InitHr);
        if (FAILED(InitHr) && InitHr != RPC_E_CHANGED_MODE)
            return false;

        auto CleanupCom = [&]()
        {
            if (NeedUninitialize)
                CoUninitialize();
        };

        ComPtr<IWICImagingFactory> Factory;
        HRESULT Hr = CoCreateInstance(CLSID_WICImagingFactory,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(Factory.GetAddressOf()));
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        std::filesystem::create_directories(Path.parent_path());

        ComPtr<IWICStream> Stream;
        Hr = Factory->CreateStream(Stream.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Stream->InitializeFromFilename(Path.wstring().c_str(), GENERIC_WRITE);
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        ComPtr<IWICBitmapEncoder> Encoder;
        Hr = Factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, Encoder.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Encoder->Initialize(Stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        ComPtr<IWICBitmapFrameEncode> Frame;
        ComPtr<IPropertyBag2> Properties;
        Hr = Encoder->CreateNewFrame(Frame.GetAddressOf(), Properties.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Frame->Initialize(Properties.Get());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Frame->SetSize((UINT)Image.Width, (UINT)Image.Height);
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        WICPixelFormatGUID PixelFormat = GUID_WICPixelFormat32bppBGRA;
        Hr = Frame->SetPixelFormat(&PixelFormat);
        if (FAILED(Hr) || PixelFormat != GUID_WICPixelFormat32bppBGRA)
        {
            CleanupCom();
            return false;
        }

        std::vector<uint8_t> Bgra = Image.Rgba;
        for (size_t I = 0; I + 3 < Bgra.size(); I += 4)
            std::swap(Bgra[I + 0], Bgra[I + 2]);

        const UINT Stride = (UINT)Image.Width * 4u;
        Hr = Frame->WritePixels((UINT)Image.Height, Stride, (UINT)Bgra.size(), Bgra.data());
        if (FAILED(Hr))
        {
            CleanupCom();
            return false;
        }

        Hr = Frame->Commit();
        if (SUCCEEDED(Hr))
            Hr = Encoder->Commit();
        CleanupCom();
        return SUCCEEDED(Hr);
    }
#endif

} // namespace

std::optional<std::vector<uint8_t>> ResourceCache::ReadUIFileWithUpperFallback(const std::string& Path)
{
    auto Data = ReadFile(Path);
    if (Data)
        return Data;

    std::string Upper;
    Upper.reserve(Path.size());
    for (char C : Path)
        Upper += (char)std::toupper((unsigned char)C);
    return ReadFile(Upper);
}

std::optional<std::filesystem::path> ResourceCache::FindExtractedUIAssetPath(const std::string& LogicalPath) const
{
    if (!mSources.UseExtractedAssets || mSources.AssetsRoot.empty())
        return std::nullopt;

    const auto Candidates = BuildExtractedAssetCandidates(LogicalPath);
    for (const auto& Candidate : Candidates)
    {
        auto FullPath = std::filesystem::path(mSources.AssetsRoot) / std::filesystem::path(Candidate);
        if (!std::filesystem::exists(FullPath))
            continue;

        if (Candidate != NormalizeLogicalPath(LogicalPath))
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Extracted asset remap: " + LogicalPath + " -> " + Candidate);
        }
        return FullPath;
    }
    return std::nullopt;
}

bool ResourceCache::LoadWicUIImage(const std::string& LogicalPath,
                                   GpuImage& Out,
                                   bool TransparentBlack)
{
#ifdef _WIN32
    auto FullPath = FindExtractedUIAssetPath(LogicalPath);
    if (!FullPath)
        return false;

    auto ImageOpt = LoadWicRgbaImageFromFile(*FullPath);
    if (!ImageOpt)
        return false;

    if (TransparentBlack)
        ApplyTransparentBlack(ImageOpt->Rgba);

    Out = UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
    return Out.Valid();
#else
    (void)LogicalPath;
    (void)Out;
    (void)TransparentBlack;
    return false;
#endif
}

bool ResourceCache::UploadDecodedUIFrames(const DecodedSprite& Sprite,
                                          std::vector<GpuImage>& Out,
                                          std::vector<ImageOpaqueBounds>* OutBounds,
                                          bool AsMask)
{
    Out.reserve(Sprite.FrameCount());
    if (OutBounds)
        OutBounds->reserve(Sprite.FrameCount());
    for (const auto& Frame : Sprite.Frames)
    {
        if (OutBounds)
            OutBounds->push_back(ComputeOpaqueBounds(Frame.Rgba.data(), Frame.Width, Frame.Height));
        if (!AsMask)
        {
            Out.push_back(UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data()));
            continue;
        }

        std::vector<uint8_t> Mask = Frame.Rgba;
        ConvertOpaquePixelsToWhiteMask(Mask);
        Out.push_back(UploadRgbaFull(Frame.Width, Frame.Height, Mask.data()));
    }
    return !Out.empty();
}

bool ResourceCache::LoadUIGrpFrames(const std::string& Path,
                                    std::vector<GpuImage>& Out,
                                    const GrpDecodeOptions& Options,
                                    std::vector<ImageOpaqueBounds>* OutBounds,
                                    bool AsMask)
{
    auto GrpData = ReadUIFileWithUpperFallback(Path);
    if (!GrpData)
        return false;

    auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette, Options);
    if (!SpriteOpt)
        return false;

    return UploadDecodedUIFrames(*SpriteOpt, Out, OutBounds, AsMask);
}

bool ResourceCache::LoadDdsGrpUIFrames(const std::string& Path,
                                       std::vector<GpuImage>& Out,
                                       std::vector<ImageOpaqueBounds>* OutBounds,
                                       bool AsMask)
{
#ifdef _WIN32
    auto DdsGrpData = ReadUIFileWithUpperFallback(Path);
    if (!DdsGrpData)
        return false;

    auto Parsed = ParseDdsGrp(*DdsGrpData);
    if (!Parsed)
        return false;

    std::vector<GpuImage> Frames;
    Frames.reserve(Parsed->Frames.size());
    std::vector<ImageOpaqueBounds> Bounds;
    if (OutBounds)
        Bounds.reserve(Parsed->Frames.size());
    for (const auto& Frame : Parsed->Frames)
    {
        auto ImageOpt = LoadWicRgbaImageFromMemory(DdsGrpData->data() + Frame.DataOffset, Frame.ByteSize);
        if (!ImageOpt)
            return false;

        if (Frame.Width > 0 && Frame.Height > 0 &&
            (ImageOpt->Width != Frame.Width || ImageOpt->Height != Frame.Height))
        {
            Core::Utils::Logger::Warning("ResourceCache",
                "DDSGRP frame size mismatch in " + Path +
                ": header=" + std::to_string(Frame.Width) + "x" + std::to_string(Frame.Height) +
                ", decoded=" + std::to_string(ImageOpt->Width) + "x" + std::to_string(ImageOpt->Height));
        }

        if (OutBounds)
            Bounds.push_back(ComputeOpaqueBounds(ImageOpt->Rgba.data(), ImageOpt->Width, ImageOpt->Height));
        if (!AsMask)
        {
            Frames.push_back(UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data()));
            continue;
        }

        std::vector<uint8_t> Mask = ImageOpt->Rgba;
        ConvertOpaquePixelsToWhiteMask(Mask);
        Frames.push_back(UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, Mask.data()));
    }

    if (Frames.empty())
        return false;

    Out = std::move(Frames);
    if (OutBounds)
        *OutBounds = std::move(Bounds);
    return true;
#else
    (void)Path;
    (void)Out;
    (void)OutBounds;
    (void)AsMask;
    return false;
#endif
}

std::string ResourceCache::GetDdsGrpLogicalPath(const std::string& ClassicPath) const
{
    const std::string Lower = ToLowerCopy(ClassicPath);
    if (Lower.size() >= 4 && Lower.substr(Lower.size() - 4) == ".grp")
        return ClassicPath.substr(0, ClassicPath.size() - 4) + ".dds.grp";
    return ClassicPath;
}

bool ResourceCache::LoadPreferredUIFrames(const std::string& ClassicPath,
                                          std::vector<GpuImage>& Out,
                                          const GrpDecodeOptions& Options,
                                          std::vector<ImageOpaqueBounds>* OutBounds,
                                          bool AsMask,
                                          const char* DebugLabel)
{
    if (mSources.UseExtractedAssets)
    {
        std::vector<GpuImage> HdFrames;
        std::vector<ImageOpaqueBounds> HdBounds;
        const std::string DdsPath = GetDdsGrpLogicalPath(ClassicPath);
        if (LoadDdsGrpUIFrames(DdsPath, HdFrames, OutBounds ? &HdBounds : nullptr, AsMask))
        {
            if (DebugLabel)
            {
                Core::Utils::Logger::Info("ResourceCache",
                    std::string("Loaded HD ") + DebugLabel + " from " + DdsPath +
                    ": " + std::to_string(HdFrames.size()) + " frames");
            }
            Out = std::move(HdFrames);
            if (OutBounds)
                *OutBounds = std::move(HdBounds);
            return true;
        }
    }
    return LoadUIGrpFrames(ClassicPath, Out, Options, OutBounds, AsMask);
}

bool ResourceCache::ExportPreferredUIFramesForGui(const std::string& ClassicPath,
                                                  std::vector<std::string>& OutUris,
                                                  const GrpDecodeOptions& Options,
                                                  bool AsMask,
                                                  const char* DebugLabel)
{
#ifdef _WIN32
    const std::filesystem::path ExportDir = GetGuiExportDirectory();
    const bool CanUseHd = mSources.UseExtractedAssets;
    const std::string Stem = MakeGuiExportStem(ClassicPath, CanUseHd, AsMask);

    // Fast path: if exported PNGs already exist in temp cache, skip expensive decoding and encoding
    char TestName[128];
    std::snprintf(TestName, sizeof(TestName), "%s_%03zu.png", Stem.c_str(), (size_t)0);
    if (std::filesystem::exists(ExportDir / TestName))
    {
        std::vector<std::string> CachedUris;
        size_t Idx = 0;
        while (true)
        {
            char CurName[128];
            std::snprintf(CurName, sizeof(CurName), "%s_%03zu.png", Stem.c_str(), Idx);
            const std::filesystem::path CurPath = ExportDir / CurName;
            if (!std::filesystem::exists(CurPath))
                break;
            std::string Uri = CurPath.string();
            std::replace(Uri.begin(), Uri.end(), '\\', '/');
            CachedUris.push_back(std::move(Uri));
            ++Idx;
        }
        if (!CachedUris.empty())
        {
            OutUris = std::move(CachedUris);
            if (DebugLabel)
            {
                Core::Utils::Logger::Info("ResourceCache",
                    std::string("Using cached GUI ") + DebugLabel + " PNGs: " +
                    std::to_string(OutUris.size()) + " frames");
            }
            return true;
        }
    }

    std::vector<DecodedRgbaImage> Frames;
    bool LoadedHd = false;
    if (mSources.UseExtractedAssets)
    {
        const std::string DdsPath = GetDdsGrpLogicalPath(ClassicPath);
        auto DdsGrpData = ReadUIFileWithUpperFallback(DdsPath);
        if (DdsGrpData)
        {
            auto Parsed = ParseDdsGrp(*DdsGrpData);
            if (Parsed)
            {
                Frames.reserve(Parsed->Frames.size());
                for (const auto& Frame : Parsed->Frames)
                {
                    auto ImageOpt = LoadWicRgbaImageFromMemory(
                        DdsGrpData->data() + Frame.DataOffset,
                        Frame.ByteSize);
                    if (!ImageOpt)
                    {
                        Frames.clear();
                        break;
                    }
                    Frames.push_back(std::move(*ImageOpt));
                }
                LoadedHd = !Frames.empty();
            }
        }
    }

    if (Frames.empty())
    {
        auto GrpData = ReadUIFileWithUpperFallback(ClassicPath);
        if (!GrpData)
            return false;

        auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette, Options);
        if (!SpriteOpt)
            return false;

        Frames.reserve(SpriteOpt->Frames.size());
        for (const auto& Frame : SpriteOpt->Frames)
        {
            DecodedRgbaImage Image = {};
            Image.Width = Frame.Width;
            Image.Height = Frame.Height;
            Image.Rgba = Frame.Rgba;
            Frames.push_back(std::move(Image));
        }
    }

    if (Frames.empty())
        return false;

    std::vector<std::string> Uris;
    Uris.reserve(Frames.size());
    for (size_t I = 0; I < Frames.size(); ++I)
    {
        DecodedRgbaImage Image = std::move(Frames[I]);
        if (AsMask)
            ConvertOpaquePixelsToWhiteMask(Image.Rgba);

        char Name[128];
        std::snprintf(Name, sizeof(Name), "%s_%03zu.png", Stem.c_str(), I);
        const std::filesystem::path OutPath = ExportDir / Name;
        if (!std::filesystem::exists(OutPath) && !WritePngRgba(OutPath, Image))
            return false;

        std::string Uri = OutPath.string();
        std::replace(Uri.begin(), Uri.end(), '\\', '/');
        Uris.push_back(std::move(Uri));
    }

    OutUris = std::move(Uris);
    if (DebugLabel)
    {
        Core::Utils::Logger::Info("ResourceCache",
            std::string("Exported GUI ") + DebugLabel + " PNGs: " +
            std::to_string(OutUris.size()) + " frames");
    }
    return true;
#else
    (void)ClassicPath;
    (void)OutUris;
    (void)Options;
    (void)AsMask;
    (void)DebugLabel;
    return false;
#endif
}

bool ResourceCache::LoadDdsGrpUIFirstFrame(const std::string& Path, GpuImage& Out)
{
#ifdef _WIN32
    auto DdsGrpData = ReadUIFileWithUpperFallback(Path);
    if (!DdsGrpData)
        return false;

    auto Parsed = ParseDdsGrp(*DdsGrpData);
    if (!Parsed || Parsed->Frames.empty())
        return false;

    const auto& Frame = Parsed->Frames.front();
    auto ImageOpt = LoadWicRgbaImageFromMemory(DdsGrpData->data() + Frame.DataOffset, Frame.ByteSize);
    if (!ImageOpt)
        return false;

    Out = UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
    return Out.Valid();
#else
    (void)Path;
    (void)Out;
    return false;
#endif
}

bool ResourceCache::LoadPcxUIImage(const std::string& Path,
                                   GpuImage& Out,
                                   bool TransparentBlack)
{
    auto PcxData = ReadUIFileWithUpperFallback(Path);
    if (!PcxData)
        return false;

    auto PcxOpt = DecodePcxToRgba(*PcxData);
    if (!PcxOpt)
        return false;

    if (TransparentBlack)
        ApplyTransparentBlack(PcxOpt->Rgba);

    Out = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
    return Out.Valid();
}

} // namespace Resource::Graphics
