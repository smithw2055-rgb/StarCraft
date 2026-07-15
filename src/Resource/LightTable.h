#pragma once

#include "Resource/Palette.h"
#include "Resource/RawColor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Resource::Graphics
{

struct IndexedPcx
{
    int Width = 0;
    int Height = 0;
    std::vector<uint8_t> Pixels;

    bool Valid() const
    {
        return Width > 0 && Height > 0 &&
               Pixels.size() == static_cast<size_t>(Width) * static_cast<size_t>(Height);
    }
};

inline IndexedPcx DecodeIndexedPcx(const std::vector<uint8_t>& Data)
{
    IndexedPcx Result;
    if (Data.size() < 128 || Data[0] != 10 || Data[3] != 8)
        return Result;

    auto ReadU16 = [&](size_t Offset) -> int
    {
        return static_cast<int>(Data[Offset]) |
               (static_cast<int>(Data[Offset + 1]) << 8);
    };
    const int XMin = ReadU16(4);
    const int YMin = ReadU16(6);
    const int XMax = ReadU16(8);
    const int YMax = ReadU16(10);
    const int Planes = Data[65];
    const int BytesPerLine = ReadU16(66);
    Result.Width = XMax - XMin + 1;
    Result.Height = YMax - YMin + 1;
    if (Result.Width <= 0 || Result.Height <= 0 || Result.Width > 4096 ||
        Result.Height > 4096 || Planes != 1 || BytesPerLine < Result.Width)
    {
        return {};
    }

    Result.Pixels.assign(
        static_cast<size_t>(Result.Width) * static_cast<size_t>(Result.Height), 0);
    size_t Source = 128;
    for (int Y = 0; Y < Result.Height; ++Y)
    {
        int X = 0;
        while (X < BytesPerLine && Source < Data.size())
        {
            const uint8_t Token = Data[Source++];
            int Count = 1;
            uint8_t Value = Token;
            if ((Token & 0xc0u) == 0xc0u)
            {
                Count = Token & 0x3fu;
                if (Source >= Data.size())
                    return {};
                Value = Data[Source++];
            }
            for (int I = 0; I < Count && X < BytesPerLine; ++I, ++X)
            {
                if (X < Result.Width)
                    Result.Pixels[static_cast<size_t>(Y) *
                                      static_cast<size_t>(Result.Width) +
                                  static_cast<size_t>(X)] = Value;
            }
        }
        if (X < BytesPerLine)
            return {};
    }
    return Result;
}

// Classic modifier-9 sprites do not contain colors. Each source pixel selects
// a row in a 256-column tileset lookup table, whose column is the color already
// present in the framebuffer. The GPU renderer cannot sample the framebuffer
// palette that way, so fit each row to the equivalent source-over RGBA color.
inline std::array<RawColor, 256> BuildLightTableRgba(
    const std::vector<uint8_t>& LookupIndices,
    int Width,
    int Height,
    const ColorPalette& TilesetPalette)
{
    std::array<RawColor, 256> Result{};
    for (auto& Color : Result)
        Color = RawColor(0, 0, 0, 0);

    if (Width < 256 || Height <= 0 ||
        LookupIndices.size() < static_cast<size_t>(Width) * static_cast<size_t>(Height))
    {
        return Result;
    }

    double MeanBackground[3] = {};
    for (int I = 0; I < 256; ++I)
    {
        MeanBackground[0] += TilesetPalette.Colors[I].R;
        MeanBackground[1] += TilesetPalette.Colors[I].G;
        MeanBackground[2] += TilesetPalette.Colors[I].B;
    }
    for (double& Channel : MeanBackground)
        Channel /= 256.0;

    double BackgroundVariance = 0.0;
    for (int I = 0; I < 256; ++I)
    {
        const double DeltaR = TilesetPalette.Colors[I].R - MeanBackground[0];
        const double DeltaG = TilesetPalette.Colors[I].G - MeanBackground[1];
        const double DeltaB = TilesetPalette.Colors[I].B - MeanBackground[2];
        BackgroundVariance += DeltaR * DeltaR + DeltaG * DeltaG + DeltaB * DeltaB;
    }
    if (BackgroundVariance <= 0.0)
        return Result;

    const int RowCount = std::min(Height, 255);
    for (int Row = 0; Row < RowCount; ++Row)
    {
        double MeanOutput[3] = {};
        for (int Column = 0; Column < 256; ++Column)
        {
            const uint8_t OutputIndex =
                LookupIndices[static_cast<size_t>(Row) * static_cast<size_t>(Width) +
                              static_cast<size_t>(Column)];
            MeanOutput[0] += TilesetPalette.Colors[OutputIndex].R;
            MeanOutput[1] += TilesetPalette.Colors[OutputIndex].G;
            MeanOutput[2] += TilesetPalette.Colors[OutputIndex].B;
        }
        for (double& Channel : MeanOutput)
            Channel /= 256.0;

        double Covariance = 0.0;
        for (int Column = 0; Column < 256; ++Column)
        {
            const uint8_t OutputIndex =
                LookupIndices[static_cast<size_t>(Row) * static_cast<size_t>(Width) +
                              static_cast<size_t>(Column)];
            Covariance +=
                (TilesetPalette.Colors[Column].R - MeanBackground[0]) *
                    (TilesetPalette.Colors[OutputIndex].R - MeanOutput[0]) +
                (TilesetPalette.Colors[Column].G - MeanBackground[1]) *
                    (TilesetPalette.Colors[OutputIndex].G - MeanOutput[1]) +
                (TilesetPalette.Colors[Column].B - MeanBackground[2]) *
                    (TilesetPalette.Colors[OutputIndex].B - MeanOutput[2]);
        }

        const double BackgroundWeight =
            std::clamp(Covariance / BackgroundVariance, 0.0, 1.0);
        const double Alpha = 1.0 - BackgroundWeight;
        if (Alpha < 0.01)
            continue;

        auto FittedChannel = [&](int Channel) -> uint8_t
        {
            const double Value =
                (MeanOutput[Channel] - BackgroundWeight * MeanBackground[Channel]) / Alpha;
            return static_cast<uint8_t>(std::lround(std::clamp(Value, 0.0, 255.0)));
        };

        // GRP value N selects PCX row N-1. Value zero is transparent.
        Result[static_cast<size_t>(Row + 1)] = RawColor(
            FittedChannel(0), FittedChannel(1), FittedChannel(2),
            static_cast<uint8_t>(std::lround(Alpha * 255.0)));
    }
    return Result;
}

} // namespace Resource::Graphics
