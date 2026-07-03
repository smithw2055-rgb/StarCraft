#include "Resource/DdsGrpDecoder.h"

#include <cstring>
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
    constexpr size_t kDdsGrpHeaderSize = 20;
    constexpr size_t kDdsGrpFrameHeaderSize = 12;

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

std::optional<DecodedRgbaImage> LoadWicRgbaImageFromMemory(const uint8_t* Bytes, size_t ByteCount)
{
#ifdef _WIN32
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
#else
    (void)Bytes;
    (void)ByteCount;
    return std::nullopt;
#endif
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

} // namespace Resource::Graphics
