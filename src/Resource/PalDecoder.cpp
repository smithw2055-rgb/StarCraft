#include "Resource/PalDecoder.h"

#include "Core/Utils/Logger.h"

#include <cstring>

namespace Resource
{

namespace
{
    constexpr size_t kExpectedPalSize = 256 * 3 + 4;   // 768 + 4-byte footer
    constexpr size_t kExpectedWpeSize = 256 * 4;       // 1024 RGBA bytes
}

std::optional<RawPalette> PalDecoder::DecodePal(const std::vector<uint8_t>& Data)
{
    if (Data.size() < kExpectedPalSize)
    {
        Core::Utils::Logger::Error("PalDecoder", "Palette data too small: " + std::to_string(Data.size()));
        return std::nullopt;
    }

    RawPalette Out = {};
    for (int I = 0; I < 256; ++I)
    {
        Out.Colors[I].R = Data[I * 3 + 0];
        Out.Colors[I].G = Data[I * 3 + 1];
        Out.Colors[I].B = Data[I * 3 + 2];
        Out.Colors[I].A = 255;
    }
    return Out;
}

std::optional<RawPalette> PalDecoder::DecodeWpe(const std::vector<uint8_t>& Data)
{
    if (Data.size() < kExpectedWpeSize)
    {
        Core::Utils::Logger::Error("PalDecoder", "WPE data too small: " + std::to_string(Data.size()));
        return std::nullopt;
    }

    RawPalette Out = {};
    for (int I = 0; I < 256; ++I)
    {
        Out.Colors[I].R = Data[I * 4 + 0];
        Out.Colors[I].G = Data[I * 4 + 1];
        Out.Colors[I].B = Data[I * 4 + 2];
        Out.Colors[I].A = Data[I * 4 + 3];
    }
    return Out;
}

RawImage PalDecoder::ApplyPalette(const std::vector<uint8_t>& PixelData,
                                  int Width, int Height,
                                  const RawPalette& Pal,
                                  RawColor TransparentColor)
{
    RawImage Out;
    Out.Width = Width;
    Out.Height = Height;
    Out.Rgba.assign((size_t)Width * Height * 4, 0);

    int N = Width * Height;
    int Limit = (int)PixelData.size();
    if (Limit > N) Limit = N;
    for (int I = 0; I < Limit; ++I)
    {
        uint8_t Idx = PixelData[I];
        const RawColor& C = Pal.Colors[Idx];
        Out.Rgba[I * 4 + 0] = C.R;
        Out.Rgba[I * 4 + 1] = C.G;
        Out.Rgba[I * 4 + 2] = C.B;
        Out.Rgba[I * 4 + 3] = (C.R == TransparentColor.R &&
                               C.G == TransparentColor.G &&
                               C.B == TransparentColor.B)
                                  ? 0 : C.A;
    }
    return Out;
}

RawPalette PalDecoder::GetDefaultPalette()
{
    RawPalette P = {};
    for (int I = 0; I < 256; ++I)
    {
        P.Colors[I] = RawColor((uint8_t)I, (uint8_t)I, (uint8_t)I, 255);
    }
    return P;
}

} // namespace Resource
