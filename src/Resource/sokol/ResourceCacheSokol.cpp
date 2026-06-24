#include "Resource/sokol/ResourceCacheSokol.h"

#include "Core/Utils/Logger.h"

#include "Resource/MpqArchive.h"
#include "Resource/CascArchive.h"
#include "Resource/FileSystemArchive.h"
#include "Resource/GameDetector.h"
#include "Resource/sokol/PcxToSokol.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "third_party/stb_image_resize.h"

#ifdef _WIN32
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <objbase.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace Resource::SokolBridge
{

namespace
{
    constexpr int kConsoleCompositeTargetWidth = 640;
    constexpr int kConsoleCompositeLeftWidth = 176;
    constexpr int kConsoleCompositeRightWidth = 216;
    constexpr size_t kDdsGrpHeaderSize = 20;
    constexpr size_t kDdsGrpFrameHeaderSize = 12;

    struct DecodedRgbaImage
    {
        int Width = 0;
        int Height = 0;
        std::vector<uint8_t> Rgba;
    };

    struct DdsGrpFrame
    {
        int Width = 0;
        int Height = 0;
        uint32_t ByteSize = 0;
        size_t DataOffset = 0;
    };

    struct ParsedDdsGrp
    {
        uint32_t DeclaredFileSize = 0;
        uint16_t FrameCount = 0;
        std::vector<DdsGrpFrame> Frames;
    };

    ImageOpaqueBounds ComputeOpaqueBounds(const uint8_t* Rgba, int Width, int Height)
    {
        if (!Rgba || Width <= 0 || Height <= 0)
            return {};

        int MinX = Width;
        int MinY = Height;
        int MaxX = -1;
        int MaxY = -1;
        for (int Y = 0; Y < Height; ++Y)
        {
            for (int X = 0; X < Width; ++X)
            {
                const size_t Offset = ((size_t)Y * (size_t)Width + (size_t)X) * 4u;
                if (Rgba[Offset + 3] == 0)
                    continue;

                MinX = std::min(MinX, X);
                MinY = std::min(MinY, Y);
                MaxX = std::max(MaxX, X);
                MaxY = std::max(MaxY, Y);
            }
        }

        if (MaxX < MinX || MaxY < MinY)
            return {};

        return { MinX, MinY, MaxX - MinX + 1, MaxY - MinY + 1 };
    }

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

#ifdef _WIN32
    std::wstring Utf8ToWide(const std::string& Value)
    {
        if (Value.empty())
            return {};

        const int Required = MultiByteToWideChar(CP_UTF8, 0, Value.c_str(), -1, nullptr, 0);
        if (Required <= 1)
            return {};

        std::wstring Wide((size_t)Required - 1u, L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, Value.c_str(), -1, Wide.data(), Required) <= 0)
            return {};
        return Wide;
    }
#endif

    void AddUniquePath(std::vector<std::string>& Paths,
                       std::unordered_set<std::string>& Seen,
                       const std::string& Candidate)
    {
        if (Candidate.empty())
            return;

        std::string Normalized = NormalizeLogicalPath(Candidate);
        std::string Key = ToLowerCopy(Normalized);
        if (Seen.insert(Key).second)
            Paths.push_back(Normalized);
    }

    std::string GetExecutableDir()
    {
#ifdef _WIN32
        char Buffer[MAX_PATH] = {};
        DWORD Length = GetModuleFileNameA(nullptr, Buffer, MAX_PATH);
        if (Length == 0 || Length >= MAX_PATH)
            return {};
        return std::filesystem::path(Buffer).parent_path().string();
#else
        return {};
#endif
    }

    std::optional<SokoPalette> LoadPcxPalette(const std::vector<uint8_t>& Data)
    {
        if (Data.size() < 769)
            return std::nullopt;

        const uint8_t* PaletteData = nullptr;
        for (size_t Offset = Data.size() - 769; Offset + 769 <= Data.size(); ++Offset)
        {
            if (Data[Offset] == 0x0C)
            {
                PaletteData = Data.data() + Offset + 1;
                break;
            }
        }
        if (!PaletteData)
            return std::nullopt;

        SokoPalette Palette = {};
        for (int I = 0; I < 256; ++I)
        {
            Palette.Colors[I].R = PaletteData[I * 3 + 0];
            Palette.Colors[I].G = PaletteData[I * 3 + 1];
            Palette.Colors[I].B = PaletteData[I * 3 + 2];
            Palette.Colors[I].A = 255;
        }
        return Palette;
    }

    uint16_t ReadLe16(const uint8_t* Data)
    {
        return (uint16_t)Data[0] | ((uint16_t)Data[1] << 8u);
    }

    uint32_t ReadLe32(const uint8_t* Data)
    {
        return (uint32_t)Data[0] |
               ((uint32_t)Data[1] << 8u) |
               ((uint32_t)Data[2] << 16u) |
               ((uint32_t)Data[3] << 24u);
    }

    std::optional<ParsedDdsGrp> ParseDdsGrp(const std::vector<uint8_t>& Data)
    {
        if (Data.size() < kDdsGrpHeaderSize)
            return std::nullopt;

        ParsedDdsGrp Parsed = {};
        Parsed.DeclaredFileSize = ReadLe32(Data.data() + 0);
        Parsed.FrameCount = ReadLe16(Data.data() + 4);
        const uint16_t FirstWidth = ReadLe16(Data.data() + 12);
        const uint16_t FirstHeight = ReadLe16(Data.data() + 14);
        uint32_t CurrentSize = ReadLe32(Data.data() + 16);

        if (Parsed.FrameCount == 0 || CurrentSize == 0)
            return std::nullopt;

        Parsed.Frames.reserve(Parsed.FrameCount);

        size_t Offset = kDdsGrpHeaderSize;
        int CurrentWidth = (int)FirstWidth;
        int CurrentHeight = (int)FirstHeight;
        for (uint16_t FrameIndex = 0; FrameIndex < Parsed.FrameCount; ++FrameIndex)
        {
            if (CurrentWidth <= 0 || CurrentHeight <= 0 ||
                CurrentSize < 4 ||
                Offset + (size_t)CurrentSize > Data.size() ||
                std::memcmp(Data.data() + Offset, "DDS ", 4) != 0)
            {
                return std::nullopt;
            }

            Parsed.Frames.push_back({ CurrentWidth, CurrentHeight, CurrentSize, Offset });
            Offset += (size_t)CurrentSize;

            if (FrameIndex + 1 >= Parsed.FrameCount)
                break;

            if (Offset + kDdsGrpFrameHeaderSize > Data.size())
                return std::nullopt;

            const uint8_t* FrameHeader = Data.data() + Offset;
            CurrentWidth = (int)ReadLe16(FrameHeader + 4);
            CurrentHeight = (int)ReadLe16(FrameHeader + 6);
            CurrentSize = ReadLe32(FrameHeader + 8);
            Offset += kDdsGrpFrameHeaderSize;
        }

        if (Parsed.DeclaredFileSize != 0 && Parsed.DeclaredFileSize != Data.size())
            return std::nullopt;

        if (Offset != Data.size())
            return std::nullopt;

        return Parsed;
    }

    std::optional<DecodedRgbaImage> ResizeRgbaImage(const DecodedRgbaImage& Source,
                                                    int TargetWidth,
                                                    int TargetHeight)
    {
        if (Source.Width <= 0 || Source.Height <= 0 ||
            TargetWidth <= 0 || TargetHeight <= 0 ||
            Source.Rgba.empty())
        {
            return std::nullopt;
        }

        DecodedRgbaImage Resized = {};
        Resized.Width = TargetWidth;
        Resized.Height = TargetHeight;
        Resized.Rgba.resize((size_t)TargetWidth * (size_t)TargetHeight * 4u);
        if (!stbir_resize_uint8(Source.Rgba.data(),
                                Source.Width, Source.Height, 0,
                                Resized.Rgba.data(),
                                TargetWidth, TargetHeight, 0,
                                4))
        {
            return std::nullopt;
        }
        return Resized;
    }

#ifdef _WIN32
    struct DecodedVideoFrame
    {
        DecodedRgbaImage Image;
        float DurationSeconds = 1.0f / 12.0f;
    };

    std::optional<std::vector<DecodedVideoFrame>> DecodeVideoFramesFromFile(const std::filesystem::path& Path)
    {
        using Microsoft::WRL::ComPtr;

        const std::wstring WidePath = Path.wstring();
        if (WidePath.empty())
            return std::nullopt;

        ComPtr<IMFAttributes> Attributes;
        if (FAILED(MFCreateAttributes(&Attributes, 2)))
            return std::nullopt;
        Attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        Attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);

        ComPtr<IMFSourceReader> Reader;
        HRESULT Hr = MFCreateSourceReaderFromURL(WidePath.c_str(), Attributes.Get(), &Reader);
        if (FAILED(Hr))
            return std::nullopt;

        ComPtr<IMFMediaType> MediaType;
        Hr = MFCreateMediaType(&MediaType);
        if (FAILED(Hr))
            return std::nullopt;
        Hr = MediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (FAILED(Hr))
            return std::nullopt;
        Hr = MediaType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (FAILED(Hr))
            return std::nullopt;
        Hr = Reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, MediaType.Get());
        if (FAILED(Hr))
            return std::nullopt;

        ComPtr<IMFMediaType> CurrentType;
        Hr = Reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &CurrentType);
        if (FAILED(Hr) || !CurrentType)
            return std::nullopt;

        UINT32 Width = 0;
        UINT32 Height = 0;
        Hr = MFGetAttributeSize(CurrentType.Get(), MF_MT_FRAME_SIZE, &Width, &Height);
        if (FAILED(Hr) || Width == 0 || Height == 0)
            return std::nullopt;

        LONG Stride = (LONG)Width * 4;
        Hr = MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, Width, &Stride);
        if (FAILED(Hr) || Stride == 0)
            Stride = (LONG)Width * 4;

        std::vector<DecodedVideoFrame> Frames;
        bool HavePendingFrame = false;
        DecodedVideoFrame PendingFrame = {};
        LONGLONG PendingTimestamp = 0;

        for (;;)
        {
            DWORD StreamIndex = 0;
            DWORD Flags = 0;
            LONGLONG Timestamp = 0;
            ComPtr<IMFSample> Sample;
            Hr = Reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                    0,
                                    &StreamIndex,
                                    &Flags,
                                    &Timestamp,
                                    &Sample);
            if (FAILED(Hr))
                return std::nullopt;

            if (Flags & MF_SOURCE_READERF_ENDOFSTREAM)
                break;

            if (!Sample)
                continue;

            ComPtr<IMFMediaBuffer> Buffer;
            Hr = Sample->ConvertToContiguousBuffer(&Buffer);
            if (FAILED(Hr) || !Buffer)
                continue;

            BYTE* Data = nullptr;
            DWORD CurrentLength = 0;
            Hr = Buffer->Lock(&Data, nullptr, &CurrentLength);
            if (FAILED(Hr) || !Data)
                continue;

            DecodedVideoFrame Current = {};
            Current.Image.Width = (int)Width;
            Current.Image.Height = (int)Height;
            Current.Image.Rgba.resize((size_t)Width * (size_t)Height * 4u);

            const int AbsStride = std::abs(Stride);
            for (UINT32 Y = 0; Y < Height; ++Y)
            {
                const BYTE* SrcRow = Data + (Stride >= 0
                    ? (size_t)Y * (size_t)AbsStride
                    : (size_t)(Height - 1u - Y) * (size_t)AbsStride);
                uint8_t* DstRow = Current.Image.Rgba.data() + (size_t)Y * (size_t)Width * 4u;
                for (UINT32 X = 0; X < Width; ++X)
                {
                    const size_t Src = (size_t)X * 4u;
                    const size_t Dst = (size_t)X * 4u;
                    DstRow[Dst + 0] = SrcRow[Src + 2];
                    DstRow[Dst + 1] = SrcRow[Src + 1];
                    DstRow[Dst + 2] = SrcRow[Src + 0];
                    DstRow[Dst + 3] = SrcRow[Src + 3];
                }
            }
            Buffer->Unlock();

            if (HavePendingFrame)
            {
                const float DeltaSeconds = (float)std::max<LONGLONG>(1, Timestamp - PendingTimestamp) / 10000000.0f;
                PendingFrame.DurationSeconds = std::clamp(DeltaSeconds, 1.0f / 30.0f, 0.25f);
                Frames.push_back(std::move(PendingFrame));
            }

            PendingTimestamp = Timestamp;
            PendingFrame = std::move(Current);
            HavePendingFrame = true;
        }

        if (HavePendingFrame)
            Frames.push_back(std::move(PendingFrame));

        if (Frames.empty())
            return std::nullopt;

        return Frames;
    }
#endif

    void AlphaBlendInto(std::vector<uint8_t>& Dest,
                        int DestWidth,
                        int DestHeight,
                        const DecodedRgbaImage& Source,
                        int DestX,
                        int DestY)
    {
        if (DestWidth <= 0 || DestHeight <= 0 || Source.Width <= 0 || Source.Height <= 0)
            return;

        for (int Y = 0; Y < Source.Height; ++Y)
        {
            const int OutY = DestY + Y;
            if (OutY < 0 || OutY >= DestHeight)
                continue;

            for (int X = 0; X < Source.Width; ++X)
            {
                const int OutX = DestX + X;
                if (OutX < 0 || OutX >= DestWidth)
                    continue;

                const size_t SrcOffset = ((size_t)Y * (size_t)Source.Width + (size_t)X) * 4u;
                const uint8_t SrcA = Source.Rgba[SrcOffset + 3];
                if (SrcA == 0)
                    continue;

                const size_t DstOffset = ((size_t)OutY * (size_t)DestWidth + (size_t)OutX) * 4u;
                if (SrcA == 255)
                {
                    Dest[DstOffset + 0] = Source.Rgba[SrcOffset + 0];
                    Dest[DstOffset + 1] = Source.Rgba[SrcOffset + 1];
                    Dest[DstOffset + 2] = Source.Rgba[SrcOffset + 2];
                    Dest[DstOffset + 3] = 255;
                    continue;
                }

                const float SrcAlpha = (float)SrcA / 255.0f;
                const float DstAlpha = (float)Dest[DstOffset + 3] / 255.0f;
                const float OutAlpha = SrcAlpha + DstAlpha * (1.0f - SrcAlpha);
                if (OutAlpha <= 0.0f)
                    continue;

                const auto BlendChannel = [&](size_t Channel) -> uint8_t
                {
                    const float SrcValue = (float)Source.Rgba[SrcOffset + Channel] / 255.0f;
                    const float DstValue = (float)Dest[DstOffset + Channel] / 255.0f;
                    const float OutValue =
                        (SrcValue * SrcAlpha + DstValue * DstAlpha * (1.0f - SrcAlpha)) / OutAlpha;
                    return (uint8_t)std::clamp((int)std::lround(OutValue * 255.0f), 0, 255);
                };

                Dest[DstOffset + 0] = BlendChannel(0);
                Dest[DstOffset + 1] = BlendChannel(1);
                Dest[DstOffset + 2] = BlendChannel(2);
                Dest[DstOffset + 3] = (uint8_t)std::clamp((int)std::lround(OutAlpha * 255.0f), 0, 255);
            }
        }
    }

#ifdef _WIN32
    std::optional<DecodedRgbaImage> LoadWicRgbaImageFromMemory(const uint8_t* Bytes, size_t ByteCount)
    {
        using Microsoft::WRL::ComPtr;

        if (!Bytes || ByteCount == 0 || ByteCount > (size_t)UINT32_MAX)
            return std::nullopt;

        const HRESULT InitHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool NeedUninitialize = SUCCEEDED(InitHr);
        if (FAILED(InitHr) && InitHr != RPC_E_CHANGED_MODE)
            return std::nullopt;

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
            return std::nullopt;
        }

        ComPtr<IWICStream> Stream;
        Hr = Factory->CreateStream(Stream.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        Hr = Stream->InitializeFromMemory(const_cast<BYTE*>(Bytes), (DWORD)ByteCount);
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        ComPtr<IWICBitmapDecoder> Decoder;
        Hr = Factory->CreateDecoderFromStream(Stream.Get(),
                                              nullptr,
                                              WICDecodeMetadataCacheOnLoad,
                                              Decoder.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        ComPtr<IWICBitmapFrameDecode> Frame;
        Hr = Decoder->GetFrame(0, Frame.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        UINT Width = 0;
        UINT Height = 0;
        Hr = Frame->GetSize(&Width, &Height);
        if (FAILED(Hr) || Width == 0 || Height == 0)
        {
            CleanupCom();
            return std::nullopt;
        }

        ComPtr<IWICFormatConverter> Converter;
        Hr = Factory->CreateFormatConverter(Converter.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        Hr = Converter->Initialize(Frame.Get(),
                                   GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone,
                                   nullptr,
                                   0.0f,
                                   WICBitmapPaletteTypeCustom);
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        DecodedRgbaImage Image = {};
        Image.Width = (int)Width;
        Image.Height = (int)Height;
        Image.Rgba.resize((size_t)Width * (size_t)Height * 4u);
        Hr = Converter->CopyPixels(nullptr,
                                   Width * 4u,
                                   (UINT)Image.Rgba.size(),
                                   Image.Rgba.data());
        CleanupCom();
        if (FAILED(Hr))
            return std::nullopt;

        return Image;
    }

    std::optional<DecodedRgbaImage> LoadWicRgbaImageFromFile(const std::filesystem::path& Path)
    {
        using Microsoft::WRL::ComPtr;

        const HRESULT InitHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool NeedUninitialize = SUCCEEDED(InitHr);
        if (FAILED(InitHr) && InitHr != RPC_E_CHANGED_MODE)
            return std::nullopt;

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
            return std::nullopt;
        }

        ComPtr<IWICBitmapDecoder> Decoder;
        Hr = Factory->CreateDecoderFromFilename(Path.wstring().c_str(),
                                                nullptr,
                                                GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad,
                                                Decoder.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        ComPtr<IWICBitmapFrameDecode> Frame;
        Hr = Decoder->GetFrame(0, Frame.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        UINT Width = 0;
        UINT Height = 0;
        Hr = Frame->GetSize(&Width, &Height);
        if (FAILED(Hr) || Width == 0 || Height == 0)
        {
            CleanupCom();
            return std::nullopt;
        }

        ComPtr<IWICFormatConverter> Converter;
        Hr = Factory->CreateFormatConverter(Converter.GetAddressOf());
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        Hr = Converter->Initialize(Frame.Get(),
                                   GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone,
                                   nullptr,
                                   0.0f,
                                   WICBitmapPaletteTypeCustom);
        if (FAILED(Hr))
        {
            CleanupCom();
            return std::nullopt;
        }

        DecodedRgbaImage Image = {};
        Image.Width = (int)Width;
        Image.Height = (int)Height;
        Image.Rgba.resize((size_t)Width * (size_t)Height * 4u);
        Hr = Converter->CopyPixels(nullptr,
                                   Width * 4u,
                                   (UINT)Image.Rgba.size(),
                                   Image.Rgba.data());
        CleanupCom();
        if (FAILED(Hr))
            return std::nullopt;

        return Image;
    }

    std::optional<std::vector<DecodedRgbaImage>> LoadDdsGrpRgbaFrames(const std::vector<uint8_t>& Data)
    {
        auto Parsed = ParseDdsGrp(Data);
        if (!Parsed)
            return std::nullopt;

        std::vector<DecodedRgbaImage> Frames;
        Frames.reserve(Parsed->Frames.size());
        for (const auto& Frame : Parsed->Frames)
        {
            auto ImageOpt = LoadWicRgbaImageFromMemory(Data.data() + Frame.DataOffset, Frame.ByteSize);
            if (!ImageOpt)
                return std::nullopt;
            Frames.push_back(std::move(*ImageOpt));
        }
        return Frames;
    }
#endif
}

ResourceCacheSokol::ResourceCacheSokol() = default;

ResourceCacheSokol::~ResourceCacheSokol()
{
    Shutdown();
}

void ResourceCacheSokol::DestroySpriteCache()
{
    for (auto& [ID, Frames] : mSpriteCache)
        for (auto& F : Frames) DestroyImage(F);
    mSpriteCache.clear();
}

void ResourceCacheSokol::DestroyTilesetCache()
{
    for (auto& [Name, Frames] : mTilesetImages)
        for (auto& F : Frames) DestroyImage(F);
    mTilesetImages.clear();
    mTilesetData.clear();
}

void ResourceCacheSokol::DestroyUICache()
{
    for (auto& I : mUIIcons) DestroyImage(I);
    mUIIcons.clear();
    for (auto& I : mCommandIcons) DestroyImage(I);
    mCommandIcons.clear();
    for (auto& I : mCommandIconMasks) DestroyImage(I);
    mCommandIconMasks.clear();
    for (auto& Frames : mCommandButtonSkins)
    {
        for (auto& I : Frames)
            DestroyImage(I);
        Frames.clear();
    }
    for (auto& I : mWireframeFrames) DestroyImage(I);
    mWireframeFrames.clear();
    mWireframeOpaqueBounds.clear();
    for (auto& [Name, Frames] : mCursorFrames)
        for (auto& Frame : Frames) DestroyImage(Frame);
    mCursorFrames.clear();
    for (auto& [Name, Clip] : mPortraitClips)
        for (auto& Frame : Clip.Frames)
            DestroyImage(Frame.Image);
    mPortraitClips.clear();
    DestroyImage(mHudAggIcons);
    DestroyImage(mHudTwireStrip);
    DestroyImage(mHudTunitStrip);
    DestroyImage(mHudHpBarEmptyStrip);
    DestroyImage(mHudHpBarStrip);
    DestroyImage(mUIConsoleLeft);
    DestroyImage(mUIConsoleCenter);
    DestroyImage(mUIConsoleRight);
    DestroyImage(mUIConsolePortraitFrame);
    DestroyImage(mUIConsoleApm);
    DestroyImage(mUIConsoleTime);
    DestroyImage(mUIBlink);
    DestroyImage(mUIConsole);
    DestroyImage(mUIConsoleOver);
}

void ResourceCacheSokol::Shutdown()
{
    DestroySpriteCache();
    DestroyTilesetCache();
    DestroyUICache();
#ifdef _WIN32
    if (mMediaFoundationInitialized)
    {
        MFShutdown();
        mMediaFoundationInitialized = false;
    }
#endif
    mArchive.reset();
    mArchive2.reset();
    mArchive3.reset();
    mAssetsRoot.clear();
    mUseExtractedAssets = false;
    mIsCasc = false;
    mIsInitialized = false;
}

bool ResourceCacheSokol::OpenMpqArchives(const std::string& InstallPath)
{
    auto TryOpen = [&](const std::string& Name, std::unique_ptr<IArchiveReader>& Out) -> bool
    {
        auto Reader = std::make_unique<MpqArchive>();
        std::string FullPath = InstallPath + "/" + Name;
        if (Reader->Open(FullPath)) { Out = std::move(Reader); return true; }
        FullPath = InstallPath + "\\" + Name;
        if (Reader->Open(FullPath)) { Out = std::move(Reader); return true; }
        return false;
    };

    TryOpen("Patch_rt.mpq", mArchive3);
    TryOpen("BrooDat.mpq", mArchive);
    TryOpen("StarDat.mpq", mArchive2);

    mListfilePath = InstallPath + "/(listfile)";

    auto AttachLf = [&](std::unique_ptr<IArchiveReader>& Arc)
    {
        if (Arc) static_cast<MpqArchive*>(Arc.get())->AddListFile(mListfilePath);
    };
    AttachLf(mArchive3);
    AttachLf(mArchive);
    AttachLf(mArchive2);

    if (!mArchive && mArchive2) mArchive = std::move(mArchive2);

    if (!mArchive)
    {
        Core::Utils::Logger::Warning("ResourceCacheSokol", "No game data at: " + InstallPath);
        return false;
    }
    Core::Utils::Logger::Info("ResourceCacheSokol", "Opened game MPQ");
    return true;
}

bool ResourceCacheSokol::OpenCascStorage(const std::string& StoragePath)
{
    auto Reader = std::make_unique<CascArchive>();
    if (Reader->Open(StoragePath))
    {
        mArchive = std::move(Reader);
        return true;
    }
    return false;
}

std::vector<std::string> ResourceCacheSokol::BuildExtractedAssetCandidates(const std::string& Path) const
{
    std::vector<std::string> Candidates;
    std::unordered_set<std::string> Seen;

    const std::string Normalized = NormalizeLogicalPath(Path);
    const std::string Lower = ToLowerCopy(Normalized);

    AddUniquePath(Candidates, Seen, Normalized);

    const size_t SlashPos = Lower.find('/');
    const bool HasSlash = SlashPos != std::string::npos;
    const size_t DotPos = Lower.rfind('.');
    const std::string Ext = (DotPos == std::string::npos) ? std::string() : Lower.substr(DotPos);

    if (!HasSlash && (Ext == ".dat" || Ext == ".tbl"))
        AddUniquePath(Candidates, Seen, "data/" + Lower);

    if (Lower.rfind("game/", 0) == 0)
    {
        const std::string Suffix = Lower.substr(5);
        AddUniquePath(Candidates, Seen, "textures/game/" + Suffix);
        AddUniquePath(Candidates, Seen, "textures/ui/" + Suffix);

        if (Lower == "game/icons.grp")
            AddUniquePath(Candidates, Seen, "game/icons.dds.grp");
    }

    if (Lower.rfind("sprites/", 0) == 0 || Lower.rfind("cursor/", 0) == 0)
        AddUniquePath(Candidates, Seen, "textures/" + Lower);

    if (Lower == "sprites/cmdbtns/cmdicons.grp")
    {
        AddUniquePath(Candidates, Seen, "unit/cmdicons/cmdicons.grp");
        AddUniquePath(Candidates, Seen, "unit/cmdicons/cmdicons.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/cmdicons.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "unit/cmdicons/cmdicons.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/ticon.pcx")
    {
        AddUniquePath(Candidates, Seen, "game/ticon.pcx");
    }
    else if (Lower == "sprites/cmdbtns/tcmdbtns.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/terran/cmdbtns.grp");
        AddUniquePath(Candidates, Seen, "game/consoles/terran/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/tcmdbtns.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/terran/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/pcmdbtns.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/protoss/cmdbtns.grp");
        AddUniquePath(Candidates, Seen, "game/consoles/protoss/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/pcmdbtns.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/protoss/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/zcmdbtns.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/zerg/cmdbtns.grp");
        AddUniquePath(Candidates, Seen, "game/consoles/zerg/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/cmdbtns/zcmdbtns.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "game/consoles/zerg/cmdbtns.dds.grp");
    }
    else if (Lower == "sprites/wirefram/wirefram.grp")
    {
        AddUniquePath(Candidates, Seen, "unit/wirefram/wirefram.grp");
        AddUniquePath(Candidates, Seen, "unit/wirefram/wirefram.dds.grp");
    }
    else if (Lower == "sprites/wirefram/wirefram.dds.grp")
    {
        AddUniquePath(Candidates, Seen, "unit/wirefram/wirefram.dds.grp");
    }

    if (Lower.rfind("tileset/", 0) == 0)
    {
        const std::string Suffix = Lower.substr(8);
        AddUniquePath(Candidates, Seen, "textures/tilesets/" + Suffix);
        if (Ext == ".vx4")
            AddUniquePath(Candidates, Seen, "tileset/" + Lower.substr(8) + "ex");
    }

    if (Lower.rfind("unit/", 0) == 0)
    {
        const std::string Suffix = Lower.substr(5);
        AddUniquePath(Candidates, Seen, "textures/sprites/" + Suffix);
    }

    if (Ext == ".grp" && Lower.rfind("game/", 0) != 0 && Lower.rfind("tileset/", 0) != 0)
        AddUniquePath(Candidates, Seen, "textures/sprites/" + Lower);

    return Candidates;
}

std::optional<std::vector<uint8_t>> ResourceCacheSokol::ReadExtractedAsset(const std::string& Path)
{
    if (!mArchive)
        return std::nullopt;

    const auto Candidates = BuildExtractedAssetCandidates(Path);
    for (const auto& Candidate : Candidates)
    {
        if (!mAssetsRoot.empty())
        {
            const auto FullPath = std::filesystem::path(mAssetsRoot) / std::filesystem::path(Candidate);
            if (!std::filesystem::exists(FullPath))
                continue;
        }

        auto Data = mArchive->ReadFile(Candidate);
        if (Data)
        {
            if (Candidate != NormalizeLogicalPath(Path))
            {
                Core::Utils::Logger::Info("ResourceCacheSokol",
                    "Extracted asset remap: " + Path + " -> " + Candidate);
            }
            return Data;
        }
    }

    return std::nullopt;
}

std::optional<std::vector<uint8_t>> ResourceCacheSokol::ReadFile(const std::string& Path)
{
    if (mUseExtractedAssets)
        return ReadExtractedAsset(Path);

    if (mIsCasc)
    {
        std::string Resolved = ResolveCascPath(Path);
        if (Resolved.empty())
        {
            if (mArchive)
            {
                auto D = mArchive->ReadFile(Path);
                if (D) return D;
            }
            return std::nullopt;
        }
        return mArchive->ReadFile(Resolved);
    }

    if (mArchive3)
    {
        auto D = mArchive3->ReadFile(Path);
        if (D) return D;
    }
    if (mArchive)
    {
        auto D = mArchive->ReadFile(Path);
        if (D) return D;
    }
    if (mArchive2)
    {
        auto D = mArchive2->ReadFile(Path);
        if (D) return D;
    }
    return std::nullopt;
}

std::vector<std::string> ResourceCacheSokol::DebugBuildExtractedAssetCandidates(const std::string& Path) const
{
    return BuildExtractedAssetCandidates(Path);
}

std::optional<std::vector<uint8_t>> ResourceCacheSokol::ReadWithFallback(const std::string& Path)
{
    if (mUseExtractedAssets)
        return ReadExtractedAsset(Path);

    auto TryOn = [&](IArchiveReader* Arc, const std::string& P) -> std::optional<std::vector<uint8_t>>
    {
        if (!Arc) return std::nullopt;
        auto D = Arc->ReadFile(P);
        if (D) return D;
        std::string Upper;
        Upper.reserve(P.size());
        for (char C : P) Upper += (char)std::toupper((unsigned char)C);
        D = Arc->ReadFile(Upper);
        if (D) return D;
        std::string Lower;
        Lower.reserve(P.size());
        for (char C : P) Lower += (char)std::tolower((unsigned char)C);
        D = Arc->ReadFile(Lower);
        if (D) return D;
        return std::nullopt;
    };

    // Try original path
    auto D = TryOn(mArchive3.get(), Path);
    if (D) return D;
    D = TryOn(mArchive.get(), Path);
    if (D) return D;
    D = TryOn(mArchive2.get(), Path);
    if (D) return D;

    // Try arr/ prefix
    D = TryOn(mArchive3.get(), "arr/" + Path);
    if (D) return D;
    D = TryOn(mArchive.get(), "arr/" + Path);
    if (D) return D;
    D = TryOn(mArchive2.get(), "arr/" + Path);
    if (D) return D;

    // Try data/ prefix
    D = TryOn(mArchive3.get(), "data/" + Path);
    if (D) return D;
    D = TryOn(mArchive.get(), "data/" + Path);
    if (D) return D;
    D = TryOn(mArchive2.get(), "data/" + Path);
    if (D) return D;

    return std::nullopt;
}

std::string ResourceCacheSokol::ResolveCascPath(const std::string& Path)
{
    if (!mArchive || !mArchive->IsOpen()) return Path;

    auto It = mCascPathCache.find(Path);
    if (It != mCascPathCache.end()) return It->second;

    // Generate candidate paths: original, separator-swapped, each with prefixes
    auto SwapSep = [](std::string S) -> std::string {
        for (auto& C : S) { if (C == '/') C = '\\'; else if (C == '\\') C = '/'; }
        return S;
    };
    std::string AltPath = SwapSep(Path);

    for (const auto& Prefix : mCascPrefixes)
    {
        for (const auto& P : { Path, AltPath })
        {
            std::string Candidate = Prefix + P;
            if (mArchive->HasFile(Candidate))
            {
                mCascPathCache[Path] = Candidate;
                return Candidate;
            }
        }
    }

    mCascPathCache[Path] = "";
    return "";
}

std::optional<SokoPalette> ResourceCacheSokol::ReadUnitPalette()
{
    if (mArchive)
    {
        auto D = mDataLoader.LoadUnitPalette(*mArchive);
        SokoPalette Out = {};
        for (int I = 0; I < 256; ++I)
        {
            Out.Colors[I].R = D.Colors[I].R;
            Out.Colors[I].G = D.Colors[I].G;
            Out.Colors[I].B = D.Colors[I].B;
            Out.Colors[I].A = D.Colors[I].A;
        }
        return Out;
    }
    return std::nullopt;
}

uint32_t ResourceCacheSokol::ResolveImageID(int ChkUnitID)
{
    return ChkUnitID >= 0 ? (uint32_t)ChkUnitID : 0u;
}

bool ResourceCacheSokol::Initialize(const std::string& GameInstallPath)
{
    Core::Utils::Logger::Info("ResourceCacheSokol", "Initializing with: " + GameInstallPath);

    // ── Check for extracted assets first ────────────────
    mIsInitialized = false;
    mIsCasc = false;
    mUseExtractedAssets = false;
    mAssetsRoot.clear();

#ifdef _WIN32
    if (!mMediaFoundationInitialized)
    {
        const HRESULT Hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (SUCCEEDED(Hr))
        {
            mMediaFoundationInitialized = true;
        }
        else
        {
            Core::Utils::Logger::Warning("ResourceCacheSokol",
                "Media Foundation startup failed; portrait webm decode will fall back");
        }
    }
#endif

    bool UsingExtractedAssets = false;
    std::vector<std::string> AssetCandidates;
    std::unordered_set<std::string> SeenAssets;
    AddUniquePath(AssetCandidates, SeenAssets, GameInstallPath + "/assets");
    AddUniquePath(AssetCandidates, SeenAssets, "./assets");

    const std::string ExeDir = GetExecutableDir();
    if (!ExeDir.empty())
    {
        std::filesystem::path Base(ExeDir);
        AddUniquePath(AssetCandidates, SeenAssets, (Base / "assets").string());
        AddUniquePath(AssetCandidates, SeenAssets, (Base / "../assets").lexically_normal().string());
        AddUniquePath(AssetCandidates, SeenAssets, (Base / "../../assets").lexically_normal().string());
        AddUniquePath(AssetCandidates, SeenAssets, (Base / "../../../assets").lexically_normal().string());
    }

    for (const auto& AssetsPath : AssetCandidates)
    {
        std::filesystem::path CandidatePath(AssetsPath);
        if (!std::filesystem::exists(CandidatePath) || !std::filesystem::is_directory(CandidatePath))
            continue;

        if (OpenAssetsDirectory(AssetsPath))
        {
            UsingExtractedAssets = true;
            Core::Utils::Logger::Info("ResourceCacheSokol",
                "Using extracted assets from: " + AssetsPath);
            break;
        }
    }

    if (!UsingExtractedAssets)
    {
        GameInstallInfo Info = GameDetector::CheckPath(GameInstallPath);

        if (Info.Type == ArchiveType::Casc)
        {
            if (!OpenCascStorage(Info.CascDataPath))
            {
                Core::Utils::Logger::Warning("ResourceCacheSokol",
                    "Failed to open CASC storage at: " + Info.CascDataPath);
                return false;
            }
            mIsCasc = true;
        }
        else
        {
            if (!OpenMpqArchives(GameInstallPath))
                return false;
            mIsCasc = false;
        }
    }

    if (mUseExtractedAssets)
    {
        auto LogProbe = [&](const std::string& ProbePath)
        {
            Core::Utils::Logger::Info("ResourceCacheSokol",
                std::string("Bootstrap probe ") + ProbePath +
                (ReadExtractedAsset(ProbePath) ? ": found" : ": missing"));
        };
        LogProbe("units.dat");
        LogProbe("images.tbl");
        LogProbe("game/consoles/terran/console_left.dds");
        LogProbe("Tileset/badlands.wpe");
    }

    auto LoadTable = [&](const std::string& Name, auto LoadFn)
    {
        std::optional<std::vector<uint8_t>> Data;
        if (mIsCasc)
        {
            Data = ReadFile(Name);
            if (!Data) Data = ReadFile("arr/" + Name);
            if (!Data) Data = ReadFile("arr\\" + Name);
            if (!Data) Data = ReadFile("data/" + Name);
            if (!Data) Data = ReadFile("data\\" + Name);
        }
        else Data = ReadWithFallback(Name);
        if (Data) LoadFn(*Data);
        else Core::Utils::Logger::Warning("ResourceCacheSokol", "Cannot load: " + Name);
    };

    LoadTable("units.dat",    [&](auto& D) { mDataLoader.LoadUnitDataRaw(D); });
    LoadTable("weapons.dat",  [&](auto& D) { mDataLoader.LoadWeaponDataRaw(D); });
    LoadTable("flingy.dat",   [&](auto& D) { mDataLoader.LoadFlingyDataRaw(D); });
    LoadTable("sprites.dat",  [&](auto& D) { mDataLoader.LoadSpriteDataRaw(D); });
    LoadTable("images.dat",   [&](auto& D) { mDataLoader.LoadImageDataRaw(D); });
    LoadTable("upgrades.dat", [&](auto& D) { mDataLoader.LoadUpgradeDataRaw(D); });
    LoadTable("techdata.dat", [&](auto& D) { mDataLoader.LoadTechDataRaw(D); });
    LoadTable("images.tbl",   [&](auto& D) { mDataLoader.LoadImageTblDataRaw(D); });
    mDataLoader.RebuildImageMappingFromTbl();

    auto UnitPal = ReadUnitPalette();
    if (UnitPal) mActivePalette = *UnitPal;
    else mActivePalette = GetDefaultSokolPalette();

    LoadUIGraphics();

    mIsInitialized = true;
    Core::Utils::Logger::Info("ResourceCacheSokol", "Initialization complete");
    return true;
}

const std::vector<SokolImage>* ResourceCacheSokol::GetSpriteFrames(uint32_t ImageID)
{
    auto It = mSpriteCache.find(ImageID);
    if (It != mSpriteCache.end()) return &It->second;

    std::string GrpPath = mDataLoader.ResolveGrpPath(ImageID);
    if (GrpPath.empty()) return nullptr;

    std::optional<std::vector<uint8_t>> GrpData;
    if (mIsCasc)
    {
        GrpData = ReadFile(GrpPath);
        if (!GrpData) GrpData = ReadFile("arr/" + GrpPath);
        if (!GrpData) GrpData = ReadFile("arr\\" + GrpPath);
        if (!GrpData) GrpData = ReadFile("data/" + GrpPath);
        if (!GrpData) GrpData = ReadFile("data\\" + GrpPath);
    }
    else
    {
        GrpData = ReadWithFallback(GrpPath);
        if (!GrpData) GrpData = ReadWithFallback("arr/" + GrpPath);
        if (!GrpData) GrpData = ReadWithFallback("data/" + GrpPath);
    }
    if (!GrpData)
    {
        Core::Utils::Logger::Warning("ResourceCacheSokol",
            "GRP not found: " + GrpPath + " (ImageID=" + std::to_string(ImageID) + ")");
        return nullptr;
    }

    auto SpriteOpt = DecodeGrpToRgba(*GrpData, mActivePalette);
    if (!SpriteOpt)
    {
        Core::Utils::Logger::Warning("ResourceCacheSokol",
            "Failed to decode GRP: " + GrpPath);
        return nullptr;
    }

    auto& Out = mSpriteCache[ImageID];
    Out.reserve(SpriteOpt->FrameCount());
    for (auto& F : SpriteOpt->Frames)
    {
        Out.push_back(UploadRgbaFull(F.Width, F.Height, F.Rgba.data()));
    }

    if (ImageID >= 100 && ImageID <= 103)
    {
        Core::Utils::Logger::Info("ResourceCacheSokol",
            "Resource GRP: ImageID=" + std::to_string(ImageID) +
            " -> " + GrpPath + " (" + std::to_string(Out.size()) + " frames)");
    }
    return &Out;
}

bool ResourceCacheSokol::SetTilesetPalette(const std::string& TilesetName)
{
    std::optional<std::vector<uint8_t>> WpeData;
    std::string WpePath = "Tileset/" + TilesetName + ".wpe";
    WpeData = ReadFile(WpePath);
    if (!WpeData)
    {
        std::string Upper;
        Upper.reserve(WpePath.size());
        for (char C : WpePath) Upper += (char)std::toupper((unsigned char)C);
        WpeData = ReadFile(Upper);
    }
    if (!WpeData)
    {
        Core::Utils::Logger::Warning("ResourceCacheSokol", "WPE not found for tileset: " + TilesetName);
        return false;
    }

    auto PalOpt = DecodeWpeSokol(*WpeData);
    if (!PalOpt) return false;
    mActivePalette = *PalOpt;

    DestroyTilesetCache();
    Core::Utils::Logger::Info("ResourceCacheSokol", "Set tileset palette: " + TilesetName);
    return true;
}

ResourceCacheSokol::TilesetData* ResourceCacheSokol::LoadTilesetData(const std::string& TilesetName)
{
    auto It = mTilesetData.find(TilesetName);
    if (It != mTilesetData.end()) return &It->second;

    auto TryRead = [&](const std::string& Base) -> std::optional<std::vector<uint8_t>>
    {
        auto D = ReadFile(Base);
        if (D) return D;
        std::string Upper;
        Upper.reserve(Base.size());
        for (char C : Base) Upper += (char)std::toupper((unsigned char)C);
        auto D2 = ReadFile(Upper);
        if (D2) return D2;
        std::string Lower;
        Lower.reserve(Base.size());
        for (char C : Base) Lower += (char)std::tolower((unsigned char)C);
        auto D3 = ReadFile(Lower);
        if (D3) return D3;
        Core::Utils::Logger::Warning("ResourceCacheSokol", "Tileset file not found: " + Base);
        return std::nullopt;
    };

    auto Vx4Data = TryRead("Tileset/" + TilesetName + ".vx4");
    if (!Vx4Data) return nullptr;
    auto Vr4Data = TryRead("Tileset/" + TilesetName + ".vr4");
    if (!Vr4Data) return nullptr;
    auto Cv5Data = TryRead("Tileset/" + TilesetName + ".cv5");

    TilesetData& Data = mTilesetData[TilesetName];
    auto Vx4Result = Vx4Decoder::Decode(*Vx4Data);
    Data.Vx4.clear();
    Data.Vx4.reserve(Vx4Result.MegaTileCount * 16);
    for (auto& M : Vx4Result.MegaTiles)
        for (int J = 0; J < 16; J++) Data.Vx4.push_back(M.MiniTileIDs[J]);
    Data.Vr4 = std::move(*Vr4Data);

    if (Cv5Data)
    {
        auto Cv5Entries = Cv5Decoder::Decode(*Cv5Data);
        Data.Cv5.clear();
        Data.Cv5.reserve(Cv5Entries.size() * 16);
        for (auto& E : Cv5Entries)
            for (int J = 0; J < 16; J++) Data.Cv5.push_back(E.MegaTileIndices[J]);
    }

    auto Vf4Data = TryRead("Tileset/" + TilesetName + ".vf4");
    if (Vf4Data)
    {
        auto Vf4Result = Vf4Decoder::Decode(*Vf4Data);
        Data.Vf4.clear();
        Data.Vf4.reserve(Vf4Result.Entries.size() * 16);
        for (auto& E : Vf4Result.Entries)
            for (int J = 0; J < 16; J++) Data.Vf4.push_back(E.Flags[J]);
    }
    else
    {
        size_t MiniTileCount = Data.Vr4.size() / 64;
        std::vector<bool> MiniWalkable(MiniTileCount, false);
        for (size_t M = 0; M < MiniTileCount; M++)
        {
            uint8_t Counts[256] = {0};
            int NonZero = 0;
            for (int P = 0; P < 64; P++)
            {
                uint8_t V = Data.Vr4[M * 64 + P];
                if (V != 0 && Counts[V] == 0) NonZero++;
                Counts[V]++;
            }
            MiniWalkable[M] = (NonZero >= 3);
        }
        size_t MegaTileCount = Data.Vx4.size() / 16;
        Data.Vf4.clear();
        Data.Vf4.reserve(MegaTileCount * 16);
        for (size_t M = 0; M < MegaTileCount; M++)
        {
            for (int J = 0; J < 16; J++)
            {
                uint16_t MiniIdx = Data.Vx4[M * 16 + J];
                uint16_t Vr4Entry = MiniIdx / 2;
                bool Walkable = (Vr4Entry < MiniWalkable.size()) ? MiniWalkable[Vr4Entry] : true;
                Data.Vf4.push_back(Walkable ? 1 : 0);
            }
        }
    }

    Data.Loaded = true;
    Core::Utils::Logger::Info("ResourceCacheSokol",
        "Loaded tileset " + TilesetName + ": VX4=" +
        std::to_string(Data.Vx4.size() / 16) + " entries, CV5=" +
        std::to_string(Data.Cv5.size() / 16) + " groups, VR4=" +
        std::to_string(Data.Vr4.size() / 64) + " tiles, VF4=" +
        std::to_string(Data.Vf4.size() / 16) + " entries");
    return &Data;
}

const ResourceCacheSokol::TilesetData* ResourceCacheSokol::GetTilesetData(const std::string& TilesetName)
{
    auto It = mTilesetData.find(TilesetName);
    if (It != mTilesetData.end()) return &It->second;
    return nullptr;
}

bool ResourceCacheSokol::BuildTilesetImages(const std::string& TilesetName)
{
    auto* Ts = LoadTilesetData(TilesetName);
    if (!Ts || !Ts->Loaded) return false;

    size_t MegatileCount = Ts->Vx4.size() / 16;
    auto& Images = mTilesetImages[TilesetName];
    Images.clear();
    Images.reserve(MegatileCount);

    auto TryRead = [&](const std::string& Base) -> std::optional<std::vector<uint8_t>>
    {
        auto D = ReadFile(Base);
        if (D) return D;
        std::string Upper;
        Upper.reserve(Base.size());
        for (char C : Base) Upper += (char)std::toupper((unsigned char)C);
        auto D2 = ReadFile(Upper);
        if (D2) return D2;
        std::string Lower;
        Lower.reserve(Base.size());
        for (char C : Base) Lower += (char)std::tolower((unsigned char)C);
        return ReadFile(Lower);
    };

    if (mUseExtractedAssets)
    {
        auto HdMegatileData = TryRead("Tileset/" + TilesetName + ".dds.vr4");
        if (HdMegatileData)
        {
#ifdef _WIN32
            auto HdFrames = LoadDdsGrpRgbaFrames(*HdMegatileData);
            if (HdFrames && !HdFrames->empty())
            {
                Images.reserve(HdFrames->size());
                for (const auto& Frame : *HdFrames)
                    Images.push_back(UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data()));

                Core::Utils::Logger::Info("ResourceCacheSokol",
                    "Loaded HD tileset megatiles from Tileset/" + TilesetName + ".dds.vr4: " +
                    std::to_string(Images.size()) + " frames");
                return !Images.empty();
            }
            Core::Utils::Logger::Warning("ResourceCacheSokol",
                "Failed to decode HD tileset megatiles from Tileset/" + TilesetName + ".dds.vr4");
#else
            Core::Utils::Logger::Info("ResourceCacheSokol",
                "HD tileset megatile decode is unavailable on this platform for Tileset/" +
                TilesetName + ".dds.vr4");
#endif
        }
        else
        {
            Core::Utils::Logger::Info("ResourceCacheSokol",
                "HD tileset megatile file not found for Tileset/" + TilesetName + ".dds.vr4");
        }
    }

    constexpr int MegaW = 32, MegaH = 32;
    constexpr int MiniW = 8, MiniH = 8;
    std::vector<uint8_t> Pixels(MegaW * MegaH * 4, 0);

    for (size_t M = 0; M < MegatileCount; M++)
    {
        std::memset(Pixels.data(), 0, Pixels.size());
        for (int Iy = 0; Iy < 4; Iy++)
        {
            for (int Ix = 0; Ix < 4; Ix++)
            {
                size_t Vx4Off = M * 16 + Iy * 4 + Ix;
                if (Vx4Off >= Ts->Vx4.size()) continue;
                uint16_t ImgIdx = Ts->Vx4[Vx4Off];
                size_t Vr4Entry = ImgIdx / 2;
                bool Inverted = (ImgIdx & 1) != 0;
                size_t Vr4Off = Vr4Entry * 64;
                if (Vr4Off + 64 > Ts->Vr4.size()) continue;

                for (int Py = 0; Py < MiniH; Py++)
                {
                    for (int Px = 0; Px < MiniW; Px++)
                    {
                        int SrcPx = Inverted ? (MiniW - 1 - Px) : Px;
                        uint8_t PalIdx = (uint8_t)Ts->Vr4[Vr4Off + Py * MiniW + SrcPx];
                        int DstX = Ix * MiniW + Px;
                        int DstY = Iy * MiniH + Py;
                        int DstOff = (DstY * MegaW + DstX) * 4;
                        const SokoColor& C = mActivePalette.Colors[PalIdx];
                        Pixels[DstOff + 0] = C.R;
                        Pixels[DstOff + 1] = C.G;
                        Pixels[DstOff + 2] = C.B;
                        Pixels[DstOff + 3] = C.A;
                    }
                }
            }
        }
        Images.push_back(UploadRgbaFull(MegaW, MegaH, Pixels.data()));
    }
    Core::Utils::Logger::Info("ResourceCacheSokol",
        "Built tileset " + TilesetName + ": " + std::to_string(Images.size()) + " megatiles");
    return true;
}

const std::vector<SokolImage>* ResourceCacheSokol::GetTilesetImages(const std::string& TilesetName)
{
    auto It = mTilesetImages.find(TilesetName);
    if (It != mTilesetImages.end()) return &It->second;
    if (BuildTilesetImages(TilesetName)) return &mTilesetImages[TilesetName];
    return nullptr;
}

bool ResourceCacheSokol::LoadUIGraphics()
{
    auto TryRead = [&](const std::string& P) -> std::optional<std::vector<uint8_t>>
    {
        auto D = ReadFile(P);
        if (D) return D;
        std::string Upper;
        Upper.reserve(P.size());
        for (char C : P) Upper += (char)std::toupper((unsigned char)C);
        return ReadFile(Upper);
    };

    auto FindExtractedAssetPath = [&](const std::string& LogicalPath) -> std::optional<std::filesystem::path>
    {
        if (!mUseExtractedAssets || mAssetsRoot.empty())
            return std::nullopt;

        const auto Candidates = BuildExtractedAssetCandidates(LogicalPath);
        for (const auto& Candidate : Candidates)
        {
            auto FullPath = std::filesystem::path(mAssetsRoot) / std::filesystem::path(Candidate);
            if (!std::filesystem::exists(FullPath))
                continue;

            if (Candidate != NormalizeLogicalPath(LogicalPath))
            {
                Core::Utils::Logger::Info("ResourceCacheSokol",
                    "Extracted asset remap: " + LogicalPath + " -> " + Candidate);
            }
            return FullPath;
        }
        return std::nullopt;
    };

    std::optional<SokoPalette> UiPalette;
    if (auto TIcon = TryRead("sprites\\cmdbtns\\ticon.pcx"))
        UiPalette = LoadPcxPalette(*TIcon);

    const SokoPalette* UiPalettePtr = UiPalette ? &*UiPalette : nullptr;

    auto UploadDecodedFrames = [&](const DecodedSprite& Sprite,
                                   std::vector<SokolImage>& Out,
                                   std::vector<ImageOpaqueBounds>* OutBounds = nullptr,
                                   bool AsMask = false) -> bool
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
            for (size_t Pixel = 0; Pixel + 3 < Mask.size(); Pixel += 4)
            {
                if (Mask[Pixel + 3] == 0)
                    continue;
                Mask[Pixel + 0] = 255;
                Mask[Pixel + 1] = 255;
                Mask[Pixel + 2] = 255;
            }
            Out.push_back(UploadRgbaFull(Frame.Width, Frame.Height, Mask.data()));
        }
        return !Out.empty();
    };

    auto LoadGrpFrames = [&](const std::string& Path,
                             std::vector<SokolImage>& Out,
                             const GrpDecodeOptions& Options = {},
                             std::vector<ImageOpaqueBounds>* OutBounds = nullptr,
                             bool AsMask = false) -> bool
    {
        auto GrpData = TryRead(Path);
        if (!GrpData)
            return false;

        auto SpriteOpt = DecodeGrpToRgba(*GrpData, mActivePalette, Options);
        if (!SpriteOpt)
            return false;

        return UploadDecodedFrames(*SpriteOpt, Out, OutBounds, AsMask);
    };

    auto LoadDdsGrpFrames = [&](const std::string& Path,
                                std::vector<SokolImage>& Out,
                                std::vector<ImageOpaqueBounds>* OutBounds = nullptr,
                                bool AsMask = false) -> bool
    {
#ifdef _WIN32
        auto DdsGrpData = TryRead(Path);
        if (!DdsGrpData)
            return false;

        auto Parsed = ParseDdsGrp(*DdsGrpData);
        if (!Parsed)
            return false;

        std::vector<SokolImage> Frames;
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
                Core::Utils::Logger::Warning("ResourceCacheSokol",
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
            for (size_t Pixel = 0; Pixel + 3 < Mask.size(); Pixel += 4)
            {
                if (Mask[Pixel + 3] == 0)
                    continue;
                Mask[Pixel + 0] = 255;
                Mask[Pixel + 1] = 255;
                Mask[Pixel + 2] = 255;
            }
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
    };

    auto LoadDdsGrpFirstFrame = [&](const std::string& Path,
                                    SokolImage& Out) -> bool
    {
#ifdef _WIN32
        auto DdsGrpData = TryRead(Path);
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
    };

    auto GetDdsGrpLogicalPath = [&](const std::string& ClassicPath) -> std::string
    {
        const std::string Lower = ToLowerCopy(ClassicPath);
        if (Lower.size() >= 4 && Lower.substr(Lower.size() - 4) == ".grp")
            return ClassicPath.substr(0, ClassicPath.size() - 4) + ".dds.grp";
        return ClassicPath;
    };

    auto LoadPreferredFrames = [&](const std::string& ClassicPath,
                                   std::vector<SokolImage>& Out,
                                   const GrpDecodeOptions& Options = {},
                                   std::vector<ImageOpaqueBounds>* OutBounds = nullptr,
                                   bool AsMask = false,
                                   const char* DebugLabel = nullptr) -> bool
    {
        if (mUseExtractedAssets)
        {
            std::vector<SokolImage> HdFrames;
            std::vector<ImageOpaqueBounds> HdBounds;
            const std::string DdsPath = GetDdsGrpLogicalPath(ClassicPath);
            if (LoadDdsGrpFrames(DdsPath, HdFrames, OutBounds ? &HdBounds : nullptr, AsMask))
            {
                if (DebugLabel)
                {
                    Core::Utils::Logger::Info("ResourceCacheSokol",
                        std::string("Loaded HD ") + DebugLabel + " from " + DdsPath +
                        ": " + std::to_string(HdFrames.size()) + " frames");
                }
                Out = std::move(HdFrames);
                if (OutBounds)
                    *OutBounds = std::move(HdBounds);
                return true;
            }
        }
        return LoadGrpFrames(ClassicPath, Out, Options, OutBounds, AsMask);
    };

    auto LoadPcxImage = [&](const std::string& Path,
                            SokolImage& Out,
                            bool TransparentBlack = false) -> bool
    {
        auto PcxData = TryRead(Path);
        if (!PcxData)
            return false;

        auto PcxOpt = DecodePcxToRgba(*PcxData);
        if (!PcxOpt)
            return false;

        if (TransparentBlack)
        {
            auto& Pixels = PcxOpt->Rgba;
            for (size_t I = 0; I + 3 < Pixels.size(); I += 4)
            {
                if (Pixels[I + 3] == 0)
                    continue;
                if (Pixels[I + 0] <= 8 && Pixels[I + 1] <= 8 && Pixels[I + 2] <= 8)
                    Pixels[I + 3] = 0;
            }
        }

        Out = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
        return Out.Valid();
    };

    auto LoadWicImage = [&](const std::string& LogicalPath,
                            SokolImage& Out,
                            bool TransparentBlack = false) -> bool
    {
#ifdef _WIN32
        auto FullPath = FindExtractedAssetPath(LogicalPath);
        if (!FullPath)
            return false;

        auto ImageOpt = LoadWicRgbaImageFromFile(*FullPath);
        if (!ImageOpt)
            return false;

        if (TransparentBlack)
        {
            auto& Pixels = ImageOpt->Rgba;
            for (size_t I = 0; I + 3 < Pixels.size(); I += 4)
            {
                if (Pixels[I + 3] == 0)
                    continue;
                if (Pixels[I + 0] <= 8 && Pixels[I + 1] <= 8 && Pixels[I + 2] <= 8)
                    Pixels[I + 3] = 0;
            }
        }

        Out = UploadRgbaFull(ImageOpt->Width, ImageOpt->Height, ImageOpt->Rgba.data());
        return Out.Valid();
#else
        (void)LogicalPath;
        (void)Out;
        (void)TransparentBlack;
        return false;
#endif
    };

    auto LoadNamedConsoleDds = [&](const std::string& FileName,
                                   SokolImage& Out,
                                   bool TransparentBlack = false) -> bool
    {
        if (!mUseExtractedAssets)
            return false;
        return LoadWicImage(std::string("game/consoles/terran/") + FileName, Out, TransparentBlack);
    };

    auto BuildConsoleCompositeFromDds = [&](SokolImage& Out) -> bool
    {
#ifdef _WIN32
        auto LeftPath = FindExtractedAssetPath("game/consoles/terran/console_left.dds");
        auto CenterPath = FindExtractedAssetPath("game/consoles/terran/console_center.dds");
        auto RightPath = FindExtractedAssetPath("game/consoles/terran/console_right.dds");
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
    };

    bool LoadedHdIcons = false;
    if (mUseExtractedAssets)
    {
        LoadedHdIcons = LoadDdsGrpFrames("game\\icons.dds.grp", mUIIcons);
        if (LoadedHdIcons)
        {
            Core::Utils::Logger::Info("ResourceCacheSokol",
                "Loaded HD UI icons from game/icons.dds.grp: " + std::to_string(mUIIcons.size()) + " frames");
        }
    }
    if (!LoadedHdIcons)
        LoadGrpFrames("game\\icons.grp", mUIIcons);

    GrpDecodeOptions CommandButtonOptions = {};
    CommandButtonOptions.PaletteOverride = UiPalettePtr;
    CommandButtonOptions.TransparentIndexZero = true;

    GrpDecodeOptions CursorOptions = CommandButtonOptions;
    CursorOptions.TransparentBlack = true;
    CursorOptions.TransparentRgb = RawColor(0, 0, 0, 0);

    LoadPreferredFrames("sprites\\cmdbtns\\cmdicons.grp", mCommandIcons, CommandButtonOptions, nullptr, false, "command icons");
    LoadPreferredFrames("sprites\\cmdbtns\\cmdicons.grp", mCommandIconMasks, CommandButtonOptions, nullptr, true, "command icon masks");
    LoadPreferredFrames("sprites\\cmdbtns\\tcmdbtns.grp",
                        mCommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Terran)],
                        CommandButtonOptions,
                        nullptr,
                        false,
                        "terran command button skins");
    LoadPreferredFrames("sprites\\cmdbtns\\pcmdbtns.grp",
                        mCommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Protoss)],
                        CommandButtonOptions,
                        nullptr,
                        false,
                        "protoss command button skins");
    LoadPreferredFrames("sprites\\cmdbtns\\zcmdbtns.grp",
                        mCommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Zerg)],
                        CommandButtonOptions,
                        nullptr,
                        false,
                        "zerg command button skins");
    LoadPreferredFrames("sprites\\wirefram\\wirefram.grp", mWireframeFrames, {}, &mWireframeOpaqueBounds, false, "wireframe frames");

    LoadPcxImage("game\\aggicons.pcx", mHudAggIcons, true);

    // The extracted asset set has clear HD replacements for the console shell and HP bar,
    // but not for the old twire/tunit decoration strips. In extracted-assets mode those
    // classic strips produce obvious distortion when stretched into the new FHD layout,
    // so prefer the HUD's rectangle fallback until we identify a true sliced replacement.
    if (!mUseExtractedAssets)
    {
        LoadPcxImage("game\\twire.pcx", mHudTwireStrip, true);
        LoadPcxImage("game\\tunit.pcx", mHudTunitStrip, true);
    }

    bool LoadedHdHpBar = false;
    if (mUseExtractedAssets)
    {
        const bool LoadedEmpty = LoadNamedConsoleDds("pbrempt.DDS", mHudHpBarEmptyStrip, true);
        const bool LoadedFull = LoadNamedConsoleDds("pbrfull.DDS", mHudHpBarStrip, true);
        LoadedHdHpBar = LoadedEmpty && LoadedFull;
        if (LoadedHdHpBar)
        {
            Core::Utils::Logger::Info("ResourceCacheSokol",
                "Loaded HD HP bar strips from game/consoles/terran/pbrempt.DDS and pbrfull.DDS");
        }
        else
        {
            Core::Utils::Logger::Info("ResourceCacheSokol",
                "HD HP bar DDS availability: empty=" + std::to_string((int)LoadedEmpty) +
                ", full=" + std::to_string((int)LoadedFull) + "; falling back to thpbar.pcx");
        }
    }
    if (!LoadedHdHpBar)
        LoadPcxImage("game\\thpbar.pcx", mHudHpBarStrip, true);

    bool LoadedHdBlink = false;
    if (mUseExtractedAssets)
    {
        LoadedHdBlink = LoadDdsGrpFirstFrame("game\\blink.dds.grp", mUIBlink);
        if (LoadedHdBlink)
        {
            Core::Utils::Logger::Info("ResourceCacheSokol",
                "Loaded HD blink frame from game/blink.dds.grp");
        }
    }
    if (!LoadedHdBlink)
    {
        auto BlinkData = TryRead("game\\blink.grp");
        if (BlinkData)
        {
            auto SpriteOpt = DecodeGrpToRgba(*BlinkData, mActivePalette);
            if (SpriteOpt && !SpriteOpt->Frames.empty())
                mUIBlink = UploadRgbaFull(SpriteOpt->Frames[0].Width,
                                          SpriteOpt->Frames[0].Height,
                                          SpriteOpt->Frames[0].Rgba.data());
        }
    }

    if (mUseExtractedAssets)
    {
        LoadNamedConsoleDds("console_left.DDS", mUIConsoleLeft);
        LoadNamedConsoleDds("console_center.DDS", mUIConsoleCenter);
        LoadNamedConsoleDds("console_right.DDS", mUIConsoleRight);
        LoadNamedConsoleDds("conover.DDS", mUIConsolePortraitFrame);
        LoadNamedConsoleDds("console_APM.DDS", mUIConsoleApm);
        LoadNamedConsoleDds("console_TIME.DDS", mUIConsoleTime);

        if (BuildConsoleCompositeFromDds(mUIConsole))
        {
            Core::Utils::Logger::Info("ResourceCacheSokol",
                "Built terran HUD console composite from extracted DDS slices");
        }
    }
    else
    {
        auto ConsoleData = TryRead("game\\nconsole.pcx");
        if (ConsoleData)
        {
            auto PcxOpt = DecodePcxToRgba(*ConsoleData);
            if (PcxOpt)
            {
                mUIConsole = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
                mUIRegions = PcxOpt->Regions;
            }
        }

        auto ConsoleOverData = TryRead("game\\tconsole.pcx");
        if (ConsoleOverData)
        {
            auto PcxOpt = DecodePcxToRgba(*ConsoleOverData);
            if (PcxOpt)
                mUIConsoleOver = UploadRgbaFull(PcxOpt->Width, PcxOpt->Height, PcxOpt->Rgba.data());
        }
    }

    constexpr const char* CursorNames[] =
    {
        "arrow", "drag", "illegal", "targn", "targg", "targr", "targy",
        "scrollu", "scrolld", "scrolll", "scrollr",
        "scrollul", "scrollur", "scrolldl", "scrolldr",
        "magg", "magr", "magy", "time"
    };
    for (const char* CursorName : CursorNames)
    {
        std::vector<SokolImage> Frames;
        if (LoadGrpFrames(std::string("cursor\\") + CursorName + ".grp", Frames, CursorOptions, nullptr, false))
            mCursorFrames.emplace(CursorName, std::move(Frames));
    }

    Core::Utils::Logger::Info("ResourceCacheSokol",
        "UI graphics: " + std::to_string(mUIIcons.size()) + " icons, " +
        std::to_string(mCommandIcons.size()) + " command icons, " +
        std::to_string(mCommandButtonSkins[static_cast<size_t>(CommandButtonSkinSet::Terran)].size()) +
        " terran button skins, " +
        std::to_string(mCursorFrames.size()) + " cursors" +
        (mUIBlink.Valid() ? ", blink" : "") +
        (mUIConsole.Valid() ? ", console" : "") +
        (mUIConsoleOver.Valid() ? ", conover" : ""));
    return true;
}

const std::vector<SokolImage>* ResourceCacheSokol::GetUIIcons()
{
    return mUIIcons.empty() ? nullptr : &mUIIcons;
}

const std::vector<SokolImage>* ResourceCacheSokol::GetCommandIcons()
{
    return mCommandIcons.empty() ? nullptr : &mCommandIcons;
}

const std::vector<SokolImage>* ResourceCacheSokol::GetCommandIconMasks()
{
    return mCommandIconMasks.empty() ? nullptr : &mCommandIconMasks;
}

const std::vector<SokolImage>* ResourceCacheSokol::GetCommandButtonSkins(CommandButtonSkinSet SkinSet)
{
    const auto& Frames = mCommandButtonSkins[static_cast<size_t>(SkinSet)];
    return Frames.empty() ? nullptr : &Frames;
}

const std::vector<SokolImage>* ResourceCacheSokol::GetWireframeFrames()
{
    return mWireframeFrames.empty() ? nullptr : &mWireframeFrames;
}

const ImageOpaqueBounds* ResourceCacheSokol::GetWireframeOpaqueBounds(int FrameIndex) const
{
    if (FrameIndex < 0 || FrameIndex >= (int)mWireframeOpaqueBounds.size())
        return nullptr;
    return &mWireframeOpaqueBounds[(size_t)FrameIndex];
}

const std::vector<SokolImage>* ResourceCacheSokol::GetCursorFrames(const std::string& CursorName)
{
    auto It = mCursorFrames.find(CursorName);
    return It != mCursorFrames.end() ? &It->second : nullptr;
}

const ResourceCacheSokol::PortraitClip* ResourceCacheSokol::GetPortraitClip(const std::string& PortraitKey)
{
    if (PortraitKey.empty())
        return nullptr;

    const std::string Key = ToLowerCopy(PortraitKey);
    if (auto It = mPortraitClips.find(Key); It != mPortraitClips.end())
        return It->second.Valid() ? &It->second : nullptr;

    PortraitClip Clip = {};
    if (!LoadPortraitClip(Key, Clip))
    {
        mPortraitClips.emplace(Key, PortraitClip{});
        return nullptr;
    }

    auto [It, Inserted] = mPortraitClips.emplace(Key, std::move(Clip));
    (void)Inserted;
    return It->second.Valid() ? &It->second : nullptr;
}

SokolImage ResourceCacheSokol::GetHudAggIcons() { return mHudAggIcons; }
SokolImage ResourceCacheSokol::GetHudTwireStrip() { return mHudTwireStrip; }
SokolImage ResourceCacheSokol::GetHudTunitStrip() { return mHudTunitStrip; }
SokolImage ResourceCacheSokol::GetHudHpBarEmptyStrip() { return mHudHpBarEmptyStrip; }
SokolImage ResourceCacheSokol::GetHudHpBarStrip() { return mHudHpBarStrip; }
SokolImage ResourceCacheSokol::GetUIConsoleLeft() { return mUIConsoleLeft; }
SokolImage ResourceCacheSokol::GetUIConsoleCenter() { return mUIConsoleCenter; }
SokolImage ResourceCacheSokol::GetUIConsoleRight() { return mUIConsoleRight; }
SokolImage ResourceCacheSokol::GetUIConsolePortraitFrame() { return mUIConsolePortraitFrame; }
SokolImage ResourceCacheSokol::GetUIConsoleApm() { return mUIConsoleApm; }
SokolImage ResourceCacheSokol::GetUIConsoleTime() { return mUIConsoleTime; }
SokolImage ResourceCacheSokol::GetUIConsole() { return mUIConsole; }
SokolImage ResourceCacheSokol::GetUIConsoleOver() { return mUIConsoleOver; }

bool ResourceCacheSokol::LoadPortraitClip(const std::string& PortraitKey, PortraitClip& OutClip)
{
#ifdef _WIN32
    if (!mUseExtractedAssets || mAssetsRoot.empty() || !mMediaFoundationInitialized)
        return false;

    std::filesystem::path PortraitDir = std::filesystem::path(mAssetsRoot) / "portrait" / PortraitKey;
    if (!std::filesystem::exists(PortraitDir) || !std::filesystem::is_directory(PortraitDir))
        return false;

    std::vector<std::filesystem::path> Clips;
    for (const auto& Entry : std::filesystem::directory_iterator(PortraitDir))
    {
        if (!Entry.is_regular_file())
            continue;

        const std::string FileName = ToLowerCopy(Entry.path().filename().string());
        if (Entry.path().extension() == ".webm" && FileName.find("fid") != std::string::npos)
            Clips.push_back(Entry.path());
    }
    if (Clips.empty())
        return false;

    std::sort(Clips.begin(), Clips.end(), [](const auto& A, const auto& B)
    {
        return ToLowerCopy(A.filename().string()) < ToLowerCopy(B.filename().string());
    });

    PortraitClip Clip = {};
    for (const auto& ClipPath : Clips)
    {
        auto FramesOpt = DecodeVideoFramesFromFile(ClipPath);
        if (!FramesOpt || FramesOpt->empty())
        {
            Core::Utils::Logger::Warning("ResourceCacheSokol",
                "Failed to decode portrait clip: " + ClipPath.string());
            continue;
        }

        for (auto& DecodedFrame : *FramesOpt)
        {
            PortraitFrame Frame = {};
            Frame.Image = UploadRgbaFull(DecodedFrame.Image.Width,
                                         DecodedFrame.Image.Height,
                                         DecodedFrame.Image.Rgba.data());
            Frame.DurationSeconds = DecodedFrame.DurationSeconds;
            if (!Frame.Image.Valid())
                continue;

            Clip.Width = std::max(Clip.Width, DecodedFrame.Image.Width);
            Clip.Height = std::max(Clip.Height, DecodedFrame.Image.Height);
            Clip.Frames.push_back(std::move(Frame));
        }
    }

    if (!Clip.Valid())
        return false;

    Core::Utils::Logger::Info("ResourceCacheSokol",
        "Loaded portrait clip " + PortraitKey + ": " + std::to_string(Clip.Frames.size()) + " frames");
    OutClip = std::move(Clip);
    return true;
#else
    (void)PortraitKey;
    (void)OutClip;
    return false;
#endif
}

bool ResourceCacheSokol::OpenAssetsDirectory(const std::string& AssetsPath)
{
    auto Reader = std::make_unique<FileSystemArchive>();
    if (Reader->Open(AssetsPath))
    {
        mArchive = std::move(Reader);
        mArchive2.reset();
        mArchive3.reset();
        mUseExtractedAssets = true;
        mAssetsRoot = NormalizeLogicalPath(AssetsPath);
        Core::Utils::Logger::Info("ResourceCacheSokol", "Opened assets directory: " + AssetsPath);
        return true;
    }
    return false;
}

} // namespace Resource::SokolBridge
