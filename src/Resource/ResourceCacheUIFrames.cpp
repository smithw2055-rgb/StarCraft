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

    const std::filesystem::path ExportDir = GetGuiExportDirectory();
    const std::string Stem = MakeGuiExportStem(ClassicPath, LoadedHd, AsMask);
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
        if (!WritePngRgba(OutPath, Image))
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
