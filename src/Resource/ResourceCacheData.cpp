#include "Resource/ResourceCache.h"

#include <cstdint>
#include <limits>
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
    if (ChkUnitID < 0)
        return 0u;

    const uint32_t ImageID = mData.Loader.ResolveUnitImageID((uint32_t)ChkUnitID);
    if (ImageID != std::numeric_limits<uint32_t>::max())
        return ImageID;

    return (uint32_t)ChkUnitID;
}

uint32_t ResourceCache::ResolveSubunitImageID(int ChkUnitID)
{
    if (ChkUnitID < 0)
        return std::numeric_limits<uint32_t>::max();
    return mData.Loader.ResolveUnitSubunitImageID((uint32_t)ChkUnitID);
}

} // namespace Resource::Graphics
