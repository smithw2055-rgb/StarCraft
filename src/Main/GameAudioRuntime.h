#pragma once

#include "Client/ClientRuntime.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"
#include "Resource/TblDecoder.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Game::Client::GameLoop
{

class GameAudioRuntime
{
public:
    void Initialize()
    {
        Audio.Init();
    }

    void ConfigureGameSounds(Game::Client::ClientRuntime& Client)
    {
        auto Source = Client.GetArchiveSource();
        Audio.SetFileLoader([Source](const std::string& Path) {
            auto Data = Source.ReadWithFallback(Path);
            return Data ? std::move(*Data) : std::vector<uint8_t>{};
        });

        const auto Dat = Source.ReadWithFallback("arr/sfxdata.dat");
        const auto Tbl = Source.ReadWithFallback("arr/sfxdata.tbl");
        if (!Dat || !Tbl)
        {
            Core::Utils::Logger::Warning("GameAudio", "sfxdata.dat/tbl not found in game archives");
            return;
        }

        constexpr size_t Count = 1144;
        constexpr size_t FilenameOffset = 0;
        constexpr size_t PriorityOffset = Count * 4;
        constexpr size_t FlagsOffset = PriorityOffset + Count;
        constexpr size_t RaceOffset = FlagsOffset + Count;
        constexpr size_t MinVolumeOffset = RaceOffset + Count * 2;
        if (Dat->size() < MinVolumeOffset + Count)
        {
            Core::Utils::Logger::Warning("GameAudio", "sfxdata.dat is truncated");
            return;
        }

        const auto Paths = Resource::TblDecoder::DecodeStrings(*Tbl);
        auto ReadU32 = [&](size_t Offset) -> uint32_t {
            return (uint32_t)(*Dat)[Offset] |
                   ((uint32_t)(*Dat)[Offset + 1] << 8) |
                   ((uint32_t)(*Dat)[Offset + 2] << 16) |
                   ((uint32_t)(*Dat)[Offset + 3] << 24);
        };
        size_t ConfiguredPaths = 0;
        for (size_t I = 0; I < Count; ++I)
        {
            const uint32_t PathIndex = ReadU32(FilenameOffset + I * 4);
            if (PathIndex < Paths.size() && !Paths[PathIndex].empty())
            {
                Audio.SetSoundPath((int)I, Paths[PathIndex]);
                ++ConfiguredPaths;
            }

            Core::Audio::SoundType Type;
            Type.Id = (uint32_t)I;
            Type.Priority = (*Dat)[PriorityOffset + I];
            Type.Flags = (*Dat)[FlagsOffset + I];
            Type.Race = (int)(*Dat)[RaceOffset + I * 2] |
                        ((int)(*Dat)[RaceOffset + I * 2 + 1] << 8);
            Type.MinVolume = (*Dat)[MinVolumeOffset + I];
            Audio.SetSoundType((int)I, Type);
        }
        Core::Utils::Logger::Info(
            "GameAudio",
            "Configured " + std::to_string(ConfiguredPaths) +
            " archive sound paths from 1144 sfxdata entries");
    }

    void UpdateForClientView(Game::Client::ClientRuntime& Client, float DeltaSeconds)
    {
        const Core::Math::Vec2 CameraPosition = Client.GetCameraPosition();
        Audio.SetListenerPosition(CameraPosition.X, CameraPosition.Y);

        float ScreenLeft = 0.0f;
        float ScreenTop = 0.0f;
        float WorldW = 0.0f;
        float WorldH = 0.0f;
        Client.GetVisibleWorldRect(ScreenLeft, ScreenTop, WorldW, WorldH);
        Audio.SetScreenRect(ScreenLeft, ScreenTop, WorldW, WorldH);

        Audio.Update(DeltaSeconds);
    }

    void UpdateClientWorldEffects(Game::Client::ClientRuntime& Client,
                                  Core::ECS::EntityManager& Entities,
                                  float DeltaSeconds)
    {
        Client.UpdateWorldEffects(Entities, Audio, DeltaSeconds);
    }

    void RegisterGameplaySystems(GameSession& Session)
    {
        GameSessionBootstrap::RegisterGameplaySystems(Session, Audio);
    }

    template <typename CameraPositionProviderT>
    void ConfigureCampaignMapAudio(CampaignRuntime& Campaign,
                                   const std::string& MapPath,
                                   CameraPositionProviderT&& CameraPositionProvider)
    {
        Campaign.SetMapAudioCallback(
            Audio,
            MapPath,
            std::forward<CameraPositionProviderT>(CameraPositionProvider));
    }

private:
    Core::Audio::AudioSystem Audio;
};

} // namespace Game::Client::GameLoop
