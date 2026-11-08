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
