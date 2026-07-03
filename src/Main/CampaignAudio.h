#pragma once

#include <string>

namespace Core::Audio { class AudioSystem; }
namespace Game::Campaign { struct CampaignMessage; }

namespace Game::Client::GameLoop
{

std::string ResolveCampaignSoundFile(const std::string& MapPath, std::string SoundPath);
void PlayCampaignMessageAudio(
    Core::Audio::AudioSystem& Audio,
    const std::string& MapPath,
    const Game::Campaign::CampaignMessage& Message,
    float FallbackX,
    float FallbackY);

} // namespace Game::Client::GameLoop
