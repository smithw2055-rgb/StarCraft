#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/DdsGrpDecoder.h"
#include "Resource/PcxToGpu.h"
#include "Resource/RgbaImageFileDecoder.h"
#include "Resource/UIAssetImageHelpers.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

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
