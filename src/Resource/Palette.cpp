#include "Resource/Palette.h"

#include "Core/Utils/Logger.h"

#include <cctype>
#include <cstring>

namespace Resource::Graphics
{

static int DetectPalStride(size_t DataSize)
{
    if (DataSize >= 1024) return 4;
    if (DataSize >= 768) return 3;
    return 3;
}

std::optional<ColorPalette> DecodePal(const std::vector<uint8_t>& Data)
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

std::optional<ColorPalette> DecodeWpe(const std::vector<uint8_t>& Data)
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
        // SC:R tileset WPE files carry a 4th byte, but extracted tileset palettes
        // commonly store it as 0 for every entry. Treat WPE as an opaque terrain
        // palette and let sprite/UI decoders apply transparency on their own paths.
        Pal.Colors[I].A = 255;
    }
    return Pal;
}

ColorPalette GetDefaultPalette()
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
