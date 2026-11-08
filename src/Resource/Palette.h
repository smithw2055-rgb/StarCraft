#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Resource/RawColor.h"

namespace Resource
{

class PalDecoder
{
public:
    static std::optional<RawPalette> DecodePal(const std::vector<uint8_t>& Data);
    static std::optional<RawPalette> DecodeWpe(const std::vector<uint8_t>& Data);

    static RawImage ApplyPalette(const std::vector<uint8_t>& PixelData,
                                 int Width, int Height,
                                 const RawPalette& Pal,
                                 RawColor TransparentColor = RawColor(0, 0, 0, 0));

    static RawPalette GetDefaultPalette();
};

} // namespace Resource

namespace Resource::Graphics
{

struct SokoColor
{
    uint8_t R = 0;
    uint8_t G = 0;
    uint8_t B = 0;
    uint8_t A = 255;
};

struct ColorPalette
{
    SokoColor Colors[256] = {};
};

std::optional<ColorPalette> DecodePal(const std::vector<uint8_t>& Data);
std::optional<ColorPalette> DecodeWpe(const std::vector<uint8_t>& Data);
ColorPalette GetDefaultPalette();

} // namespace Resource::Graphics

// === Inlined from src/Resource/Palette.cpp ===


#include "Core/Utils/Logger.h"

#include <cctype>
#include <cstring>

namespace Resource
{

namespace
{
    constexpr size_t kExpectedPalSize = 256 * 3 + 4;   // 768 + 4-byte footer
    constexpr size_t kExpectedWpeSize = 256 * 4;       // 1024 RGBA bytes
}

inline std::optional<RawPalette> PalDecoder::DecodePal(const std::vector<uint8_t>& Data)
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

inline std::optional<RawPalette> PalDecoder::DecodeWpe(const std::vector<uint8_t>& Data)
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
        Out.Colors[I].A = 255;
    }
    return Out;
}

inline RawImage PalDecoder::ApplyPalette(const std::vector<uint8_t>& PixelData,
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

inline RawPalette PalDecoder::GetDefaultPalette()
{
    RawPalette P = {};
    for (int I = 0; I < 256; ++I)
    {
        P.Colors[I] = RawColor((uint8_t)I, (uint8_t)I, (uint8_t)I, 255);
    }
    return P;
}

} // namespace Resource

namespace Resource::Graphics
{

inline int DetectPalStride(size_t DataSize)
{
    if (DataSize >= 1024) return 4;
    if (DataSize >= 768) return 3;
    return 3;
}

inline std::optional<ColorPalette> DecodePal(const std::vector<uint8_t>& Data)
{
    if (Data.size() < 768)
    {
        Core::Utils::Logger::Error("Palette", "Palette data too small: " + std::to_string(Data.size()));
        return std::nullopt;
    }
    int Stride = DetectPalStride(Data.size());
    ColorPalette Pal = {};
    for (int I = 0; I < 256; ++I)
    {
        int Offset = I * Stride;
        if (Offset + 2 >= (int)Data.size()) break;
        Pal.Colors[I].R = Data[Offset + 0];
        Pal.Colors[I].G = Data[Offset + 1];
        Pal.Colors[I].B = Data[Offset + 2];
        Pal.Colors[I].A = 255;
    }
    Pal.Colors[0].A = 0;
    return Pal;
}

inline std::optional<ColorPalette> DecodeWpe(const std::vector<uint8_t>& Data)
{
    if (Data.size() < 256 * 4)
    {
        Core::Utils::Logger::Error("Palette", "WPE data too small: " + std::to_string(Data.size()));
        return std::nullopt;
    }
    ColorPalette Pal = {};
    for (int I = 0; I < 256; ++I)
    {
        Pal.Colors[I].R = Data[I * 4 + 0];
        Pal.Colors[I].G = Data[I * 4 + 1];
        Pal.Colors[I].B = Data[I * 4 + 2];
        Pal.Colors[I].A = 255;
    }
    return Pal;
}

inline ColorPalette GetDefaultPalette()
{
    ColorPalette Pal = {};
    for (int I = 0; I < 256; ++I)
    {
        Pal.Colors[I].R = static_cast<uint8_t>((I * 7) % 256);
        Pal.Colors[I].G = static_cast<uint8_t>((I * 13) % 256);
        Pal.Colors[I].B = static_cast<uint8_t>((I * 23) % 256);
        Pal.Colors[I].A = 255;
    }
    Pal.Colors[0].A = 0;
    return Pal;
}

} // namespace Resource::Graphics
