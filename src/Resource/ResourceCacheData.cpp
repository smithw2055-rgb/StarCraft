#include "Resource/ResourceCache.h"

#include <cstdint>
#include <optional>

namespace Resource::Graphics
{

std::optional<ColorPalette> ResourceCache::ReadUnitPalette()
{
    if (mSources.Archive)
    {
        auto D = mData.Loader.LoadUnitPalette(*mSources.Archive);
        ColorPalette Out = {};
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

uint32_t ResourceCache::ResolveImageID(int ChkUnitID)
{
    return ChkUnitID >= 0 ? (uint32_t)ChkUnitID : 0u;
}

} // namespace Resource::Graphics
