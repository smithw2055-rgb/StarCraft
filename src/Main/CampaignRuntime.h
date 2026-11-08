#pragma once

#include <string>
#include <utility>

#include "Core/Utils/Logger.h"
#include "Game/Campaign/CampaignController.h"
#include "Game/Trigger/TriggerVM.h"
#include "Main/CampaignPresentation.h"

namespace Game::Client::GameLoop
{

struct CampaignRuntime
{
    template <typename EntityManagerT, typename TriggerDataT>
    bool InitializeCampaign(EntityManagerT& Entities, const TriggerDataT* TriggerData)
    {
        Active = Campaign.Init(Entities, TriggerData);
        return Active;
    }

    bool IsActive() const
    {
        return Active;
    }

    void Deactivate()
    {
        Active = false;
        ScenarioPaused = false;
        ClearProgressTarget();
    }

    Game::Campaign::CampaignController& Controller()
    {
        return Campaign;
    }

    const Game::Campaign::CampaignController& Controller() const
    {
        return Campaign;
    }

    bool HasTriggerRuntime() const
    {
        return Campaign.GetTriggerVM() != nullptr;
    }

    template <typename SoundCallbackT>
    void SetSoundCallback(SoundCallbackT&& SoundCallback)
    {
        Campaign.SetSoundCallback(std::forward<SoundCallbackT>(SoundCallback));
    }

    template <typename CameraPositionProviderT>
    void SetMapAudioCallback(Core::Audio::AudioSystem& Audio,
                             const std::string& MapPath,
                             CameraPositionProviderT&& CameraPositionProvider)
    {
        Campaign.SetSoundCallback(
            [&Audio, &MapPath, CameraPositionProvider = std::forward<CameraPositionProviderT>(CameraPositionProvider)]
            (const Game::Campaign::CampaignMessage& Message) {
                const auto Camera = CameraPositionProvider();
                PlayCampaignMessageAudio(Audio, MapPath, Message, Camera.X, Camera.Y);
            });
    }

    template <typename SetAllianceT,
              typename ResolveImageIdT,
              typename CanBurrowT,
              typename ResolveCollisionBoundsT,
              typename CenterViewT,
              typename MinimapPingT,
              typename SetGamePausedT,
              typename ResolveSubunitImageIdT = std::function<uint32_t(int)>>
    void ConfigureTriggerRuntime(SetAllianceT&& SetAlliance,
                                 ResolveImageIdT&& ResolveImageID,
                                 CanBurrowT&& CanBurrow,
                                 ResolveCollisionBoundsT&& ResolveCollisionBounds,
                                 CenterViewT&& CenterView,
                                 MinimapPingT&& MinimapPing,
                                 SetGamePausedT&& SetGamePaused,
                                 ResolveSubunitImageIdT&& ResolveSubunitImageID = {})
    {
        auto* TriggerVM = Campaign.GetTriggerVM();
        if (!TriggerVM)
            return;

        Game::Trigger::TriggerRuntimeConfig TriggerConfig;
        TriggerConfig.SetAlliance = std::forward<SetAllianceT>(SetAlliance);
        TriggerConfig.ResolveImageID = std::forward<ResolveImageIdT>(ResolveImageID);
        TriggerConfig.CanBurrow = std::forward<CanBurrowT>(CanBurrow);
        TriggerConfig.ResolveCollisionBounds = std::forward<ResolveCollisionBoundsT>(ResolveCollisionBounds);
        TriggerConfig.CenterView = std::forward<CenterViewT>(CenterView);
        TriggerConfig.MinimapPing = std::forward<MinimapPingT>(MinimapPing);
        TriggerConfig.SetGamePaused = std::forward<SetGamePausedT>(SetGamePaused);
        TriggerConfig.ResolveSubunitImageID = std::forward<ResolveSubunitImageIdT>(ResolveSubunitImageID);
        TriggerVM->SetRuntimeConfig(std::move(TriggerConfig));
    }

    void SetProgressTarget(bool Expansion, int Race, int Mission)
    {
        ProgressSet = Expansion ? 1 : 0;
        ProgressRace = Race;
        ProgressMission = Mission;
        ProgressRecorded = false;
    }

    void ClearProgressTarget()
    {
        ProgressSet = -1;
        ProgressRace = -1;
        ProgressMission = -1;
        ProgressRecorded = false;
    }

    bool HasPendingProgressRecord() const
    {
        return !ProgressRecorded && ProgressSet >= 0 && ProgressRace >= 0 && ProgressMission >= 0;
    }

    void RecordVictoryProgressIfNeeded()
    {
        if (!HasPendingProgressRecord())
            return;

        if (!IsActive() || Campaign.GetState() != Game::Campaign::CampaignState::Victory)
            return;

        const int UnlockedCount = GetUnlockedMissionCount();
        Game::Campaign::SaveCampaignProgressCount(GetProgressSet(), GetProgressRace(), UnlockedCount);
        MarkProgressRecorded();
        Core::Utils::Logger::Info("CampaignProgress",
            "Unlocked campaign set=" + std::to_string(GetProgressSet()) +
            " race=" + std::to_string(GetProgressRace()) +
            " count=" + std::to_string(UnlockedCount));
    }

    int GetProgressSet() const
    {
        return ProgressSet;
    }

    int GetProgressRace() const
    {
        return ProgressRace;
    }

    int GetUnlockedMissionCount() const
    {
        const int UnlockedCount = ProgressMission + 2;
        if (UnlockedCount < 1)
            return 1;
        if (UnlockedCount > 10)
            return 10;
        return UnlockedCount;
    }

    void MarkProgressRecorded()
    {
        ProgressRecorded = true;
    }

    void SetScenarioPaused(bool Paused)
    {
        ScenarioPaused = Paused;
    }

    bool IsScenarioPaused() const
    {
        return ScenarioPaused;
    }

    Game::Campaign::CampaignController Campaign;
    bool Active = false;
    bool ScenarioPaused = false;
    int ProgressSet = -1;
    int ProgressRace = -1;
    int ProgressMission = -1;
    bool ProgressRecorded = false;
};

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameAudioRuntime.h ===
#include "Client/ClientRuntime.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"

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

    Core::Audio::AudioSystem& GetAudioSystem() { return Audio; }
    const Core::Audio::AudioSystem& GetAudioSystem() const { return Audio; }

    int PlayFile(const std::string& Path, float X = 0.0f, float Y = 0.0f, int Priority = 20,
                 Core::Audio::SoundFlags Flags = Core::Audio::SoundFlags::PerSound)
    {
        return Audio.PlayFile(Path, X, Y, Priority, Flags);
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
            const std::string SoundPath =
                Core::Audio::ResolveArchiveSoundPath(Paths, PathIndex);
            if (!SoundPath.empty())
            {
                Audio.SetSoundPath((int)I, SoundPath);
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
