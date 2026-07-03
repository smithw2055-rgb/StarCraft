#pragma once

#include "Resource/GameDataLoader.h"
#include "Resource/Palette.h"

namespace Resource::Graphics
{

struct DataAssetCacheState
{
    Resource::GameDataLoader Loader;
    ColorPalette ActivePalette;
};

} // namespace Resource::Graphics
