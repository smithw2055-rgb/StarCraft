#pragma once

#include <cstdint>
#include <string>

namespace Core::Audio { class AudioSystem; }
namespace Game::Campaign { class CampaignController; struct CampaignMessage; }
namespace Resource::Graphics { class ArchiveAssetSourceView; }
namespace Resource::Graphics { class DataAssetCacheView; }
namespace Resource::Graphics { class UIAssetCacheView; }
namespace Game::Client::UI { class Hud; }

namespace Game::Client::GameLoop
{

enum class MatchOutcome : uint8_t
{
    Playing = 0,
    Victory = 1,
    Defeat = 2,
    Draw = 3
};

std::string ResolveCampaignSoundFile(const std::string& MapPath, std::string SoundPath);
void PlayCampaignMessageAudio(
    Core::Audio::AudioSystem& Audio,
    const std::string& MapPath,
    const Game::Campaign::CampaignMessage& Message,
    float FallbackX,
    float FallbackY);

void RenderCampaignOverlay(
    Game::Campaign::CampaignController& Campaign,
    UI::Hud& Hud,
    const Resource::Graphics::DataAssetCacheView& DataAssets,
    const Resource::Graphics::ArchiveAssetSourceView& ArchiveSource,
    const Resource::Graphics::UIAssetCacheView& UIAssets,
    int ViewportWidth,
    int ViewportHeight);

void RenderMatchGameOverOverlay(
    MatchOutcome Outcome,
    int ViewportWidth,
    int ViewportHeight,
    float ElapsedSeconds);

} // namespace Game::Client::GameLoop
