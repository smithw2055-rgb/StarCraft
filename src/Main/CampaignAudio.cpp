#include "Main/CampaignAudio.h"

#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"
#include "Game/Campaign/CampaignController.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace Game::Client::GameLoop
{

namespace
{

std::string ReplaceExtension(std::string Path, const char* Ext)
{
    const size_t Dot = Path.find_last_of('.');
    if (Dot == std::string::npos)
        return Path + Ext;
    Path.erase(Dot);
    Path += Ext;
    return Path;
}

} // namespace

std::string ResolveCampaignSoundFile(const std::string& MapPath, std::string SoundPath)
{
    if (SoundPath.empty())
        return {};

    std::replace(SoundPath.begin(), SoundPath.end(), '\\', '/');
    std::vector<std::filesystem::path> Bases;
    const std::filesystem::path MapBase = std::filesystem::path(MapPath).parent_path();
    if (!MapBase.empty())
    {
        Bases.push_back(MapBase);
        if (MapBase.filename() == "staredit")
            Bases.push_back(MapBase.parent_path());
    }
    Bases.push_back(std::filesystem::path("assets"));

    const std::string OggPath = ReplaceExtension(SoundPath, ".ogg");
    const std::string WavPath = ReplaceExtension(SoundPath, ".wav");
    const std::string Candidates[] = { SoundPath, OggPath, WavPath };
    for (const auto& Base : Bases)
    {
        for (const auto& Candidate : Candidates)
        {
            const std::filesystem::path Full = Base / std::filesystem::path(Candidate);
            if (std::filesystem::exists(Full))
                return Full.string();
        }
    }

    return {};
}

void PlayCampaignMessageAudio(
    Core::Audio::AudioSystem& Audio,
    const std::string& MapPath,
    const Game::Campaign::CampaignMessage& Message,
    float FallbackX,
    float FallbackY)
{
    std::string File = ResolveCampaignSoundFile(MapPath, Message.SoundPath);
    if (File.empty())
    {
        Core::Utils::Logger::Warning("CampaignAudio",
            "Unable to resolve campaign audio: " + Message.SoundPath);
        return;
    }

    Core::Math::Vec2 Pos = Message.Position;
    if (Pos.X == 0.0f && Pos.Y == 0.0f)
    {
        Pos.X = FallbackX;
        Pos.Y = FallbackY;
    }

    const int Channel = Audio.PlayFile(File, Pos.X, Pos.Y, 20,
        Core::Audio::SoundFlags::PerSound);
    if (Channel >= 0)
        Core::Utils::Logger::Info("CampaignAudio", "Playing " + File);
}

} // namespace Game::Client::GameLoop
