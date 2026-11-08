#include "Resource/GrpDecoder.h"
#include "Resource/Palette.h"
#include "Core/Utils/Logger.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace Resource
{
namespace
{
    bool ShouldLogGrpDecodeDetails()
    {
        const char* Value = std::getenv("STARCRAFT_DEBUG_GRP");
        return Value && Value[0] == '1';
    }
}

// GRP format:
//   6 bytes: frameCount(uint16), maxWidth(uint16), maxHeight(uint16)
//   frameCount × 8 bytes: image headers
//     byte 0: x_offset
//     byte 1: y_offset
//     byte 2: width
//     byte 3: height
//     bytes 4-7: uint32 offset to line offsets from file start
//   line offsets: height × uint16, each = byte offset from line_offset_base to scanline RLE data
//   RLE data per scanline

std::optional<GrpSprite> GrpDecoder::DecodeGrp(const std::vector<uint8_t>& GrpData)
{
    if (GrpData.size() < 6)
    {
        Core::Utils::Logger::Error("GrpDecoder", "GRP data too small for header");
        return std::nullopt;
    }

    GrpSprite Sprite;
    Sprite.FrameCount = GrpData[0] | (GrpData[1] << 8);
    Sprite.Width = GrpData[2] | (GrpData[3] << 8);
    Sprite.Height = GrpData[4] | (GrpData[5] << 8);

    // Image headers start at byte 6, each is 8 bytes:
    //   byte 0: x_offset (uint8)
    //   byte 1: y_offset (uint8)
    //   byte 2: width (uint8)
    //   byte 3: height (uint8)
    //   bytes 4-7: lineOffsetOffset (uint32 LE from file start)
    size_t ImageHeaderOff = 6;
    if (GrpData.size() < ImageHeaderOff + Sprite.FrameCount * 8)
    {
        Core::Utils::Logger::Error("GrpDecoder", "GRP data too small for image headers");
        return std::nullopt;
    }

    for (int I = 0; I < Sprite.FrameCount; ++I)
    {
        size_t HdrOff = ImageHeaderOff + I * 8;
        int FrameXOff = GrpData[HdrOff + 0];
        int FrameYOff = GrpData[HdrOff + 1];
        int FrameWidth = GrpData[HdrOff + 2];
        int FrameHeight = GrpData[HdrOff + 3];
        uint32_t LineOffBase = GrpData[HdrOff + 4] |
                               (GrpData[HdrOff + 5] << 8) |
                               (GrpData[HdrOff + 6] << 16) |
                               (GrpData[HdrOff + 7] << 24);

        if (FrameWidth == 0) FrameWidth = Sprite.Width;
        if (FrameHeight == 0) FrameHeight = Sprite.Height;
        if (LineOffBase == 0 || LineOffBase >= GrpData.size())
        {
            GrpFrameInfo Info;
            Info.Width = FrameWidth;
            Info.Height = FrameHeight;
            Info.XOffset = FrameXOff;
            Info.YOffset = FrameYOff;
            Info.Offset = (int)Sprite.RawFrameData.size();
            Info.Flip = false;
            Sprite.Frames.push_back(Info);
            Sprite.RawFrameData.resize(
                Sprite.RawFrameData.size() + FrameWidth * FrameHeight, 0);
            continue;
        }

        // Check line offset table bounds
        size_t LineOffsetTableEnd = LineOffBase + FrameHeight * 2;
        if (LineOffsetTableEnd > GrpData.size())
        {
            GrpFrameInfo Info;
            Info.Width = FrameWidth;
            Info.Height = FrameHeight;
            Info.XOffset = FrameXOff;
            Info.YOffset = FrameYOff;
            Info.Offset = (int)Sprite.RawFrameData.size();
            Info.Flip = false;
            Sprite.Frames.push_back(Info);
            Sprite.RawFrameData.resize(
                Sprite.RawFrameData.size() + FrameWidth * FrameHeight, 0);
            continue;
        }

        GrpFrameInfo Info;
        Info.Width = FrameWidth;
        Info.Height = FrameHeight;
        Info.XOffset = FrameXOff;
        Info.YOffset = FrameYOff;
        Info.Offset = (int)Sprite.RawFrameData.size();
        Info.Flip = false;
        Sprite.Frames.push_back(Info);

        // Pre-allocate exact space needed for this frame
        size_t ExpectedPixels = FrameWidth * FrameHeight;
        size_t FrameStartPos = Sprite.RawFrameData.size();
        Sprite.RawFrameData.resize(FrameStartPos + ExpectedPixels, 0);

        // Decode each scanline into pre-allocated space
        int TotalNonZero = 0;
        for (int Y = 0; Y < FrameHeight; ++Y)
        {
            size_t LineOffAddr = LineOffBase + Y * 2;
            if (LineOffAddr + 1 >= GrpData.size())
                continue;
            uint16_t LineOff = GrpData[LineOffAddr] | (GrpData[LineOffAddr + 1] << 8);
            size_t RleStart = LineOffBase + LineOff;

            if (RleStart >= GrpData.size())
                continue;

            // Decode RLE for this scanline
            int X = 0;
            size_t RlePos = RleStart;
            size_t LineStartIdx = FrameStartPos + Y * FrameWidth;
            while (X < FrameWidth && RlePos < GrpData.size())
            {
                uint8_t Cmd = GrpData[RlePos++];
                if (Cmd & 0x80)
                {
                    // Transparent run
                    int Count = Cmd & 0x7F;
                    if (Count == 0 && RlePos < GrpData.size())
                        Count = GrpData[RlePos++];
                    X += Count;
                }
                else if (Cmd & 0x40)
                {
                    // Repeated pixel run
                    int Count = Cmd & 0x3F;
                    if (Count == 0 && RlePos < GrpData.size())
                        Count = GrpData[RlePos++];
                    uint8_t Pixel = (RlePos < GrpData.size()) ? GrpData[RlePos++] : 0;
                    for (int J = 0; J < Count && X < FrameWidth; ++J)
                    {
                        Sprite.RawFrameData[LineStartIdx + X++] = Pixel;
                        if (Pixel != 0) TotalNonZero++;
                    }
                }
                else
                {
                    // Literal pixel run
                    int Count = Cmd;
                    if (Count == 0 && RlePos < GrpData.size())
                        Count = GrpData[RlePos++];
                    for (int J = 0; J < Count && X < FrameWidth && RlePos < GrpData.size(); ++J)
                    {
                        uint8_t Pixel = GrpData[RlePos++];
                        Sprite.RawFrameData[LineStartIdx + X++] = Pixel;
                        if (Pixel != 0) TotalNonZero++;
                    }
                }
            }
        }
        if (I == 0 && ShouldLogGrpDecodeDetails())
        {
            Core::Utils::Logger::Info("GrpDecoder", "Frame0 decode: " + 
                std::to_string(TotalNonZero) + "/" + std::to_string(FrameWidth * FrameHeight) + " non-zero");
            // Also log first few pixels
            std::string PixHex;
            for (int P = 0; P < 40 && P < (int)ExpectedPixels; ++P)
            {
                char Buf[4];
                sprintf(Buf, "%02X ", Sprite.RawFrameData[FrameStartPos + P]);
                PixHex += Buf;
            }
            Core::Utils::Logger::Info("GrpDecoder", "  First 40 pixels: " + PixHex);
            
            // Count unique non-zero values
            int UniqueVals[256] = {0};
            for (size_t P = FrameStartPos; P < FrameStartPos + ExpectedPixels; ++P)
            {
                if (P < Sprite.RawFrameData.size())
                    UniqueVals[Sprite.RawFrameData[P]]++;
            }
            std::string DistHex;
            int DistCount = 0;
            for (int V = 1; V < 256 && DistCount < 10; ++V)
            {
                if (UniqueVals[V] > 0)
                {
                    char Buf[16];
                    sprintf(Buf, "%d=%dx ", V, UniqueVals[V]);
                    DistHex += Buf;
                    DistCount++;
                }
            }
            Core::Utils::Logger::Info("GrpDecoder", "  Pixel distribution: " + DistHex);
        }
    }

    return Sprite;
}

std::optional<std::vector<RawImage>> GrpDecoder::DecodeToRawImages(
    const std::vector<uint8_t>& GrpData,
    const RawPalette& Pal)
{
    auto GrpOpt = DecodeGrp(GrpData);
    if (!GrpOpt)
        return std::nullopt;

    const GrpSprite& Sprite = *GrpOpt;
    std::vector<RawImage> Images;
    Images.reserve(Sprite.FrameCount);

    for (int I = 0; I < Sprite.FrameCount; ++I)
    {
        const GrpFrameInfo& Info = Sprite.Frames[I];
        int FramePixels = Info.Width * Info.Height;

        std::vector<uint8_t> FramePixelsData(
            Sprite.RawFrameData.begin() + Info.Offset,
            Sprite.RawFrameData.begin() + Info.Offset + FramePixels);

        RawImage Img = PalDecoder::ApplyPalette(FramePixelsData,
                                                 Info.Width, Info.Height, Pal);
        if (I == 0 && ShouldLogGrpDecodeDetails())
        {
            Core::Utils::Logger::Info("GrpDecoder",
                "Image0: " + std::to_string(Img.Width) + "x" + std::to_string(Img.Height));
        }
        Images.push_back(std::move(Img));
    }

    return Images;
}

} // namespace Resource


// === Consolidated from src/Resource/DdsGrpDecoder.cpp ===
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


// === Consolidated from src/Resource/GrpToGpu.cpp ===
#include "Resource/GrpToGpu.h"

#include "Core/Utils/Logger.h"

#include <cstring>

namespace Resource::Graphics
{

namespace
{
    bool IsTeamColorIndex(uint8_t Index, const GrpDecodeOptions& Options)
    {
        if (!Options.GenerateTeamColorMask || Options.TeamColorIndexCount == 0)
            return false;

        const int Start = Options.TeamColorIndexStart;
        const int End = Start + Options.TeamColorIndexCount;
        return Index >= Start && Index < End;
    }
}

std::optional<DecodedSprite> DecodeGrpToRgba(
    const std::vector<uint8_t>& GrpData,
    const ColorPalette& Pal,
    const GrpDecodeOptions& Options)
{
    auto GrpOpt = GrpDecoder::DecodeGrp(GrpData);
    if (!GrpOpt) return std::nullopt;

    const GrpSprite& Sprite = *GrpOpt;
    const ColorPalette& ActivePalette = Options.PaletteOverride ? *Options.PaletteOverride : Pal;
    DecodedSprite Out;
    Out.Frames.reserve(Sprite.FrameCount);
    for (int I = 0; I < Sprite.FrameCount; ++I)
    {
        const GrpFrameInfo& Info = Sprite.Frames[I];
        DecodedFrame F;
        F.Width = Sprite.Width > 0 ? Sprite.Width : Info.Width;
        F.Height = Sprite.Height > 0 ? Sprite.Height : Info.Height;
        F.OriginX = 0;
        F.OriginY = 0;
        F.Flip = Info.Flip;
        F.Rgba.resize(static_cast<size_t>(F.Width) * static_cast<size_t>(F.Height) * 4, 0);
        F.TeamColorMaskRgba.resize(static_cast<size_t>(F.Width) * static_cast<size_t>(F.Height) * 4, 0);
        bool HasTeamColorMask = false;

        int Px = Info.Width * Info.Height;
        for (int P = 0; P < Px; ++P)
        {
            const uint8_t SourceIndex = Sprite.RawFrameData[Info.Offset + P];
            uint8_t Idx = SourceIndex;
            const bool TeamColorPixel = IsTeamColorIndex(SourceIndex, Options);
            if (Options.UseIndexRemap && Options.IndexRemap[Idx] >= 0)
                Idx = static_cast<uint8_t>(Options.IndexRemap[Idx]);

            const SokoColor& C = ActivePalette.Colors[Idx];
            const bool TransparentByIndex = Options.TransparentIndexZero && Idx == 0;
            const bool TransparentByBlack = Options.TransparentBlack &&
                                            C.R == Options.TransparentRgb.R &&
                                            C.G == Options.TransparentRgb.G &&
                                            C.B == Options.TransparentRgb.B;
            const int LocalX = P % Info.Width;
            const int LocalY = P / Info.Width;
            const int OutX = Info.XOffset + LocalX;
            const int OutY = Info.YOffset + LocalY;
            if (OutX < 0 || OutY < 0 || OutX >= F.Width || OutY >= F.Height)
                continue;

            const size_t OutOffset = ((size_t)OutY * (size_t)F.Width + (size_t)OutX) * 4u;
            if (Options.DecodeLightTableMask)
            {
                if (Options.UseLightTableRgba)
                {
                    const RawColor& LightColor =
                        Options.LightTableRgba[SourceIndex];
                    F.Rgba[OutOffset + 0] = LightColor.R;
                    F.Rgba[OutOffset + 1] = LightColor.G;
                    F.Rgba[OutOffset + 2] = LightColor.B;
                    F.Rgba[OutOffset + 3] = LightColor.A;
                    continue;
                }
                const uint8_t Alpha = DecodeLightTableMaskAlpha(
                    SourceIndex, Options.LightTableIndexCount,
                    Options.LightTableMaxAlpha);
                const unsigned Strength = Options.LightTableIndexCount > 1
                    ? (static_cast<unsigned>(std::min<uint8_t>(
                           SourceIndex, Options.LightTableIndexCount) -
                       (SourceIndex > 0 ? 1u : 0u)) * 255u) /
                          static_cast<unsigned>(Options.LightTableIndexCount - 1)
                    : 0u;
                F.Rgba[OutOffset + 0] = static_cast<uint8_t>(
                    Options.LightTableTint.R +
                    ((255u - Options.LightTableTint.R) * Strength) / 255u);
                F.Rgba[OutOffset + 1] = static_cast<uint8_t>(
                    Options.LightTableTint.G +
                    ((255u - Options.LightTableTint.G) * Strength) / 255u);
                F.Rgba[OutOffset + 2] = static_cast<uint8_t>(
                    Options.LightTableTint.B +
                    ((255u - Options.LightTableTint.B) * Strength) / 255u);
                F.Rgba[OutOffset + 3] = Alpha;
            }
            else
            {
                F.Rgba[OutOffset + 0] = C.R;
                F.Rgba[OutOffset + 1] = C.G;
                F.Rgba[OutOffset + 2] = C.B;
                F.Rgba[OutOffset + 3] =
                    (TransparentByIndex || TransparentByBlack) ? 0 : C.A;
            }

            if (TeamColorPixel && F.Rgba[OutOffset + 3] != 0)
            {
                F.TeamColorMaskRgba[OutOffset + 0] = 255;
                F.TeamColorMaskRgba[OutOffset + 1] = 255;
                F.TeamColorMaskRgba[OutOffset + 2] = 255;
                F.TeamColorMaskRgba[OutOffset + 3] = 255;
                HasTeamColorMask = true;
            }
        }
        if (!HasTeamColorMask)
            F.TeamColorMaskRgba.clear();
        Out.Frames.push_back(std::move(F));
    }
    return Out;
}

} // namespace Resource::Graphics
