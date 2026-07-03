#pragma once

#include <cstdint>
#include <string>

#include "Resource/ArchiveAssetSourceView.h"
#include "Resource/DataAssetCacheView.h"
#include "Resource/UIAssetTypes.h"

namespace Game::Client::GameLoop
{

std::string CampaignPortraitKeyFromUnitID(
    const Resource::Graphics::DataAssetCacheView& DataAssets,
    const Resource::Graphics::ArchiveAssetSourceView& ArchiveSource,
    uint32_t UnitID);
std::string CampaignPortraitKeyFromText(const std::string& Text);
std::string CampaignAdvisorPortraitKey(int PlayerID);
const Resource::Graphics::PortraitFrame* CampaignPortraitFrameAt(
    const Resource::Graphics::PortraitClip& Clip,
    double TimeSeconds);

} // namespace Game::Client::GameLoop
