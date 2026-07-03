#pragma once

#include <string>
#include <utility>

#include "Core/Utils/Logger.h"
#include "Game/Campaign/CampaignController.h"
#include "Game/Campaign/CampaignProgressStore.h"
#include "Game/Trigger/TriggerVM.h"
#include "Main/CampaignAudio.h"

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
              typename CenterViewT,
              typename MinimapPingT,
              typename SetGamePausedT>
    void ConfigureTriggerRuntime(SetAllianceT&& SetAlliance,
                                 ResolveImageIdT&& ResolveImageID,
                                 CenterViewT&& CenterView,
                                 MinimapPingT&& MinimapPing,
                                 SetGamePausedT&& SetGamePaused)
    {
        auto* TriggerVM = Campaign.GetTriggerVM();
        if (!TriggerVM)
            return;

        Game::Trigger::TriggerRuntimeConfig TriggerConfig;
        TriggerConfig.SetAlliance = std::forward<SetAllianceT>(SetAlliance);
        TriggerConfig.ResolveImageID = std::forward<ResolveImageIdT>(ResolveImageID);
        TriggerConfig.CenterView = std::forward<CenterViewT>(CenterView);
        TriggerConfig.MinimapPing = std::forward<MinimapPingT>(MinimapPing);
        TriggerConfig.SetGamePaused = std::forward<SetGamePausedT>(SetGamePaused);
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
