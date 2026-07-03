#pragma once

namespace Game::Campaign { class CampaignController; }
namespace Resource::Graphics { class ArchiveAssetSourceView; }
namespace Resource::Graphics { class DataAssetCacheView; }
namespace Resource::Graphics { class UIAssetCacheView; }
namespace Game::Client::UI { class Hud; }

namespace Game::Client::GameLoop
{

void RenderCampaignOverlay(
    Game::Campaign::CampaignController& Campaign,
    UI::Hud& Hud,
    const Resource::Graphics::DataAssetCacheView& DataAssets,
    const Resource::Graphics::ArchiveAssetSourceView& ArchiveSource,
    const Resource::Graphics::UIAssetCacheView& UIAssets,
    int ViewportWidth,
    int ViewportHeight);

} // namespace Game::Client::GameLoop
