#pragma once

#include "Client/ClientRuntime.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameAudioRuntime.h"
#include "Main/GameNetworkActionRuntime.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void UpdateGameplaySimulationRuntime(
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    UI::Hud& Hud,
    float Dt)
{
    if (!Session.IsEcsInitialized())
        return;

    Session.ProcessNetworkFrame([&Session](const Game::Networking::SyncAction& Action) {
        ExecuteNetworkAction(Session, Action);
    });

    Session.UpdateSystems(Dt);
    Audio.UpdateForClientView(Client, Dt);

    if (Session.IsReplayRecording())
    {
        // Record dummy action per frame to keep the replay stream alive.
        // Real actions are recorded via RecordGameAction().
    }

    Session.Bwapi().OnFrame();

    SyncHudFromPlayerResources(Hud);

    Client.PruneSelection(Session.Entities());

    Audio.UpdateClientWorldEffects(Client, Session.Entities(), Dt);
}

inline void UpdateCampaignAndSimulationRuntime(
    CampaignRuntime& Campaign,
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    UI::Hud& Hud,
    float Dt)
{
    if (Campaign.IsActive())
    {
        Campaign.Controller().Update(Session.Entities(), Dt);
        Campaign.RecordVictoryProgressIfNeeded();
    }

    // Scenario-triggered pauses freeze the simulation but keep
    // CampaignController ticking so Wait/Unpause/transmission triggers can resume.
    if (!Campaign.IsScenarioPaused())
        UpdateGameplaySimulationRuntime(Session, Audio, Client, Hud, Dt);
}

} // namespace Game::Client::GameLoop
