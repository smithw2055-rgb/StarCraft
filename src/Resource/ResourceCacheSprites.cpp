#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/DdsGrpDecoder.h"
#include "Resource/GrpToGpu.h"
#include "Resource/LightTable.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Resource::Graphics
{

namespace
{
    std::string ToLowerCopy(const std::string& Value)
    {
        std::string Lower = Value;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        return Lower;
    }

    bool PreferClassicEffectSprite(const std::string& GrpPath)
    {
        std::string Lower = ToLowerCopy(GrpPath);
        std::replace(Lower.begin(), Lower.end(), '\\', '/');
        return Lower.rfind("thingy/", 0) == 0 ||
               Lower.rfind("bullet/", 0) == 0;
    }

    bool ShouldLogSpriteLoads()
    {
        const char* Value = std::getenv("STARCRAFT_DEBUG_SPRITE_LOADS");
        return Value && Value[0] == '1';
    }

    bool ForceClassicSprites()
    {
        const char* Value = std::getenv("STARCRAFT_FORCE_CLASSIC_SPRITES");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    bool HasVisiblePixels(const std::vector<uint8_t>& Rgba)
    {
        for (size_t I = 3; I < Rgba.size(); I += 4)
            if (Rgba[I] != 0)
                return true;
        return false;
    }

    RawColor LightTableTint(uint8_t ColorShift)
    {
        // images.dat color_shift selects ofire/gfire/bfire/bexpl/trans50/red/
        // green. Alpha still comes from the GRP's light-table row index.
        switch (ColorShift)
        {
            case 1: return RawColor(255, 128, 32, 255);
            case 2: return RawColor(80, 255, 96, 255);
            case 3: return RawColor(64, 144, 255, 255);
            case 4: return RawColor(128, 192, 255, 255);
            case 6: return RawColor(255, 72, 72, 255);
            case 7: return RawColor(72, 255, 96, 255);
            default: return RawColor(255, 255, 255, 255);
        }
    }

    uint8_t LightTableIndexCount(uint8_t ColorShift)
    {
        // All extracted tilesets use the same row counts for these lookup
        // tables: ofire, gfire, bfire, bexpl, trans50, red, green.
        switch (ColorShift)
        {
            case 1: return 63;
            case 2: return 32;
            case 3: return 40;
            case 4: return 63;
            case 5: return 255;
            case 6:
            case 7: return 1;
            default: return 63;
        }
    }

    const char* LightTableFileName(uint8_t ColorShift)
    {
        static constexpr const char* Names[] = {
            "ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green"
        };
        if (ColorShift < 1 || ColorShift > 7)
            return nullptr;
        return Names[ColorShift - 1];
    }

    std::string DescribeRgba(const std::vector<uint8_t>& Rgba)
    {
        int Visible = 0;
        int NonBlack = 0;
        for (size_t I = 0; I + 3 < Rgba.size(); I += 4)
        {
            if (Rgba[I + 3] != 0)
            {
                ++Visible;
                if (Rgba[I] != 0 || Rgba[I + 1] != 0 || Rgba[I + 2] != 0)
                    ++NonBlack;
            }
        }
        return "visible=" + std::to_string(Visible) +
               " nonblack=" + std::to_string(NonBlack) +
               " pixels=" + std::to_string(Rgba.size() / 4);
    }

    std::vector<uint8_t> ScaleMaskRgbaNearest(const DecodedFrame& Source, int Width, int Height)
    {
        if (Source.TeamColorMaskRgba.empty() ||
            Source.Width <= 0 || Source.Height <= 0 ||
            Width <= 0 || Height <= 0)
        {
            return {};
        }

        std::vector<uint8_t> Mask((size_t)Width * (size_t)Height * 4u, 0);
        for (int Y = 0; Y < Height; ++Y)
        {
            const int SrcY = std::min(Source.Height - 1, (Y * Source.Height) / Height);
            for (int X = 0; X < Width; ++X)
            {
                const int SrcX = std::min(Source.Width - 1, (X * Source.Width) / Width);
                const size_t Src = ((size_t)SrcY * (size_t)Source.Width + (size_t)SrcX) * 4u;
                const size_t Dst = ((size_t)Y * (size_t)Width + (size_t)X) * 4u;
                Mask[Dst + 0] = Source.TeamColorMaskRgba[Src + 0];
                Mask[Dst + 1] = Source.TeamColorMaskRgba[Src + 1];
                Mask[Dst + 2] = Source.TeamColorMaskRgba[Src + 2];
                Mask[Dst + 3] = Source.TeamColorMaskRgba[Src + 3];
            }
        }
        return Mask;
    }

    void AttachTeamColorMask(GpuImage& Frame, int Width, int Height, const std::vector<uint8_t>& MaskRgba)
    {
        if (MaskRgba.empty())
            return;

        GpuImage Mask = UploadRgbaFull(Width, Height, MaskRgba.data());
        Frame.TeamColorMask = Mask;
    }
}

const std::vector<GpuImage>* ResourceCache::GetSpriteFrames(uint32_t ImageID)
{
    auto It = mSprites.FramesByImageId.find(ImageID);
    if (It != mSprites.FramesByImageId.end()) return &It->second;

    std::string GrpPath = mData.Loader.ResolveGrpPath(ImageID);
    if (GrpPath.empty()) return nullptr;

    return LoadSpriteFramesForPath(ImageID, GrpPath);
}

const std::vector<GpuImage>* ResourceCache::GetDatImageFrames(uint32_t ImageID)
{
    constexpr uint32_t kDatImageCacheFlag = 0x80000000u;
    const uint32_t CacheKey = kDatImageCacheFlag | ImageID;
    auto It = mSprites.FramesByImageId.find(CacheKey);
    if (It != mSprites.FramesByImageId.end()) return &It->second;

    const auto* Relation = mData.Loader.FindImageRelationByID(ImageID);
    const bool IsShadowSprite = Relation && Relation->IsShadow();
    GrpDecodeOptions DecodeOptions;
    if (const auto* Image = mData.Loader.FindImageByID(ImageID);
        Image && Image->Modifier == 9)
    {
        DecodeOptions.DecodeLightTableMask = true;
        DecodeOptions.GenerateTeamColorMask = false;
        DecodeOptions.LightTableTint = LightTableTint(Image->ColorShift);
        DecodeOptions.LightTableIndexCount =
            LightTableIndexCount(Image->ColorShift);

        const char* TableName = LightTableFileName(Image->ColorShift);
        if (TableName && !mTilesets.ActiveName.empty())
        {
            auto Cached = mTilesets.LightTableRgbaByColorShift.find(Image->ColorShift);
            if (Cached != mTilesets.LightTableRgbaByColorShift.end())
            {
                DecodeOptions.LightTableRgba = Cached->second;
                DecodeOptions.UseLightTableRgba = true;
            }
            else
            {
                const std::string TilesetBase = "Tileset/" + mTilesets.ActiveName;
                auto TableData = ReadWithFallback(
                    TilesetBase + "/" + TableName + ".pcx");
                auto PaletteData = ReadWithFallback(TilesetBase + ".wpe");
                if (TableData && PaletteData)
                {
                    const IndexedPcx Table = DecodeIndexedPcx(*TableData);
                    const auto TilesetPalette = DecodeWpe(*PaletteData);
                    if (TilesetPalette && Table.Valid() && Table.Width == 256)
                    {
                        DecodeOptions.LightTableRgba = BuildLightTableRgba(
                            Table.Pixels, Table.Width, Table.Height, *TilesetPalette);
                        DecodeOptions.UseLightTableRgba = true;
                        mTilesets.LightTableRgbaByColorShift.emplace(
                            Image->ColorShift, DecodeOptions.LightTableRgba);
                    }
                }
            }
        }
    }

    std::string GrpPath = mData.Loader.ResolveImageGrpPath(ImageID);
    if (GrpPath.empty()) return nullptr;

    std::string NormalizedGrpPath = ToLowerCopy(GrpPath);
    std::replace(NormalizedGrpPath.begin(), NormalizedGrpPath.end(), '\\', '/');
    if (NormalizedGrpPath == "protoss/texture.grp")
    {
        DecodeOptions.GenerateTeamColorMask = false;
        DecodeOptions.TransparentIndexZero = false;
    }

    // images.dat also contains links to LO coordinate tables (for example the
    // Nexus large-shield table). They are reached by valid iscript next-image
    // opcodes but are not GRP bitmaps. Feeding their binary offsets into the
    // GRP decoder produced the translucent ring seen under Protoss bases.
    const std::string LowerGrpPath = ToLowerCopy(GrpPath);
    if (LowerGrpPath.size() < 4 ||
        LowerGrpPath.substr(LowerGrpPath.size() - 4) != ".grp")
    {
        return nullptr;
    }

    const std::vector<GpuImage>* Frames = LoadSpriteFramesForPath(
        CacheKey, GrpPath, "DAT image", DecodeOptions);
    if (IsShadowSprite && Frames)
    {
        auto CacheIt = mSprites.FramesByImageId.find(CacheKey);
        if (CacheIt != mSprites.FramesByImageId.end())
            for (auto& Frame : CacheIt->second)
                Frame.ShadowSprite = true;
    }
    return Frames;
}

const std::vector<GpuImage>* ResourceCache::GetSpriteOverlayFrames(uint32_t SpriteID)
{
    const auto* Sprite = mData.Loader.FindSpriteByID(SpriteID);
    if (!Sprite)
        return nullptr;
    return GetDatImageFrames(Sprite->ImageID);
}

const std::vector<GpuImage>* ResourceCache::LoadSpriteFramesForPath(uint32_t CacheKey,
                                                                    const std::string& GrpPath,
                                                                    const char* DebugLabel,
                                                                    const GrpDecodeOptions& Options)
{
    auto ReadClassicGrpData = [&]() -> std::optional<std::vector<uint8_t>>
    {
        std::optional<std::vector<uint8_t>> GrpData;
        if (mSources.IsCasc)
        {
            GrpData = ReadFile(GrpPath);
            if (!GrpData) GrpData = ReadFile("unit/" + GrpPath);
            if (!GrpData) GrpData = ReadFile("unit\\" + GrpPath);
            if (!GrpData) GrpData = ReadFile("arr/" + GrpPath);
            if (!GrpData) GrpData = ReadFile("arr\\" + GrpPath);
            if (!GrpData) GrpData = ReadFile("data/" + GrpPath);
            if (!GrpData) GrpData = ReadFile("data\\" + GrpPath);
        }
        else
        {
            GrpData = ReadWithFallback(GrpPath);
            if (!GrpData) GrpData = ReadWithFallback("unit/" + GrpPath);
            if (!GrpData) GrpData = ReadWithFallback("arr/" + GrpPath);
            if (!GrpData) GrpData = ReadWithFallback("data/" + GrpPath);
        }
        return GrpData;
    };

    if (!ForceClassicSprites() &&
        !PreferClassicEffectSprite(GrpPath) &&
        GrpPath.size() > 4 &&
        ToLowerCopy(GrpPath.substr(GrpPath.size() - 4)) == ".grp")
    {
        const std::string HdPath = GrpPath.substr(0, GrpPath.size() - 4) + ".dds.grp";
        auto HdData = ReadFile(HdPath);
        if (HdData)
        {
            auto HdFrames = LoadDdsGrpRgbaFrames(*HdData);
            if (HdFrames && !HdFrames->empty())
            {
                bool HdHasTransparentOnlyFrame = false;
                for (const auto& FrameData : *HdFrames)
                {
                    if (!HasVisiblePixels(FrameData.Rgba))
                    {
                        HdHasTransparentOnlyFrame = true;
                        break;
                    }
                }

                if (HdHasTransparentOnlyFrame)
                {
                    Core::Utils::Logger::Warning("ResourceCache",
                        "HD unit sprite contains a fully transparent frame: " + HdPath +
                        "; falling back to classic GRP");
                }
                else
                {
                std::optional<DecodedSprite> ClassicSprite;
                if (auto ClassicGrpData = ReadClassicGrpData())
                    ClassicSprite = DecodeGrpToRgba(
                        *ClassicGrpData, mData.ActivePalette, Options);

                auto& Out = mSprites.FramesByImageId[CacheKey];
                Out.reserve(HdFrames->size());
                for (size_t I = 0; I < HdFrames->size(); ++I)
                {
                    const auto& FrameData = (*HdFrames)[I];
                    GpuImage Frame = UploadRgbaFull(FrameData.Width, FrameData.Height, FrameData.Rgba.data());
                    if (ClassicSprite && I < ClassicSprite->Frames.size())
                    {
                        const auto Mask = ScaleMaskRgbaNearest(
                            ClassicSprite->Frames[I], FrameData.Width, FrameData.Height);
                        AttachTeamColorMask(Frame, FrameData.Width, FrameData.Height, Mask);
                    }
                    Out.push_back(Frame);
                }

                    if (ShouldLogSpriteLoads())
                    {
                        Core::Utils::Logger::Info("ResourceCache",
                            std::string("Loaded HD unit sprite: ") +
                            (DebugLabel ? DebugLabel : "ImageID") + "=" + std::to_string(CacheKey) +
                            " -> " + HdPath + " (" + std::to_string(Out.size()) + " frames)");
                    }
                    return &Out;
                }
            }

            Core::Utils::Logger::Warning("ResourceCache",
                "Failed to decode HD unit sprite: " + HdPath +
                "; falling back to classic GRP");
        }
    }

    std::optional<std::vector<uint8_t>> GrpData = ReadClassicGrpData();
    if (!GrpData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "GRP not found: " + GrpPath + " (cacheKey=" + std::to_string(CacheKey) + ")");
        return nullptr;
    }

    auto SpriteOpt = DecodeGrpToRgba(*GrpData, mData.ActivePalette, Options);
    if (!SpriteOpt)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to decode GRP: " + GrpPath);
        return nullptr;
    }

    auto& Out = mSprites.FramesByImageId[CacheKey];
    Out.reserve(SpriteOpt->FrameCount());
    for (auto& F : SpriteOpt->Frames)
    {
        GpuImage Frame = UploadRgbaFull(F.Width, F.Height, F.Rgba.data());
        AttachTeamColorMask(Frame, F.Width, F.Height, F.TeamColorMaskRgba);
        Out.push_back(Frame);
    }

    if (ShouldLogSpriteLoads() && (DebugLabel || (CacheKey >= 100 && CacheKey <= 103)))
    {
        const std::string FirstFrameStats = SpriteOpt->Frames.empty()
            ? std::string("empty")
            : DescribeRgba(SpriteOpt->Frames.front().Rgba);
        Core::Utils::Logger::Info("ResourceCache",
            "Resource GRP: " + std::string(DebugLabel ? DebugLabel : "ImageID") +
            "=" + std::to_string(CacheKey) +
            " -> " + GrpPath + " (" + std::to_string(Out.size()) + " frames, " +
            FirstFrameStats + ")");
    }
    return &Out;
}

} // namespace Resource::Graphics
