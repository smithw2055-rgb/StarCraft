#pragma once

#include "Client/ClientRuntime.h"
#include "Core/Audio/AudioSystem.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"

#include <string>
#include <utility>

namespace Game::Client::GameLoop
{

class GameAudioRuntime
{
public:
    void Initialize()
    {
        Audio.Init();
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
