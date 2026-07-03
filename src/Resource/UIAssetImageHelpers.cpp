#include "Resource/UIAssetImageHelpers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "third_party/stb_image_resize.h"

namespace Resource::Graphics
{

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

std::optional<ColorPalette> LoadPcxPalette(const std::vector<uint8_t>& Data)
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

    ColorPalette Palette = {};
    for (int I = 0; I < 256; ++I)
    {
        Palette.Colors[I].R = PaletteData[I * 3 + 0];
        Palette.Colors[I].G = PaletteData[I * 3 + 1];
        Palette.Colors[I].B = PaletteData[I * 3 + 2];
        Palette.Colors[I].A = 255;
    }
    return Palette;
}

void ApplyTransparentBlack(std::vector<uint8_t>& Rgba)
{
    for (size_t I = 0; I + 3 < Rgba.size(); I += 4)
    {
        if (Rgba[I + 3] == 0)
            continue;
        if (Rgba[I + 0] <= 8 && Rgba[I + 1] <= 8 && Rgba[I + 2] <= 8)
            Rgba[I + 3] = 0;
    }
}

void ConvertOpaquePixelsToWhiteMask(std::vector<uint8_t>& Rgba)
{
    for (size_t Pixel = 0; Pixel + 3 < Rgba.size(); Pixel += 4)
    {
        if (Rgba[Pixel + 3] == 0)
            continue;
        Rgba[Pixel + 0] = 255;
        Rgba[Pixel + 1] = 255;
        Rgba[Pixel + 2] = 255;
    }
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

} // namespace Resource::Graphics
