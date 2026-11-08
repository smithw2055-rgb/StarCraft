#pragma once

#include <memory>
#include <string_view>

#include "Core/Scene/SceneManager.h"
#include "Main/AppRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameRuntimeState.h"
// === Consolidated from src/Main/GameSimulationRuntime.h ===
#include "Client/ClientRuntime.h"
#include "Main/GameLoop/HudResourceSync.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameAudioRuntime.h"
// === Consolidated from src/Main/GameNetworkActionRuntime.h ===
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Game/Components.h"
#include "Game/Networking/LockstepSync.h"
#include "Game/Systems/CommandSystem.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop
{

inline bool HostSessionNetworkGame(GameSession& Session,
                                   const std::string& BindIP,
                                   int Port,
                                   int MaxClients)
{
    return Session.HostNetworkGame(BindIP, Port, MaxClients);
}

inline bool JoinSessionNetworkGame(GameSession& Session,
                                   const std::string& HostIP,
                                   int Port)
{
    return Session.JoinNetworkGame(HostIP, Port);
}

inline void StartSessionNetworkGame(GameSession& Session)
{
    Session.StartNetworkGameIfEnabled();
}

inline void RecordSessionLocalFrameAction(GameSession& Session,
                                          uint64_t FrameCount,
                                          uint8_t PlayerID,
                                          uint8_t ActionType,
                                          uint16_t P1,
                                          uint16_t P2,
                                          uint16_t P3,
                                          uint8_t P4)
{
    Session.RecordLocalFrameAction((uint32_t)FrameCount, PlayerID, ActionType, P1, P2, P3, P4);
}

inline float ReadNetworkActionFloat(const std::vector<uint8_t>& Data, size_t Offset)
{
    float Value = 0.0f;
    if (Offset + sizeof(Value) <= Data.size())
        std::memcpy(&Value, Data.data() + Offset, sizeof(Value));
    return Value;
}

inline void ExecuteNetworkAction(GameSession& Session, const Game::Networking::SyncAction& Action)
{
    if (Action.Data.empty())
        return;

    auto& EM = Session.Entities();
    const uint8_t CmdType = Action.Data[0];

    auto CollectControllableUnits = [&]() -> std::vector<int> {
        std::vector<int> Units;
        EM.ForEachEntityWith<Game::Components::PlayerComponent, Game::Components::UnitComponent>(
            [&](Core::ECS::EntityID Id) {
                const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Id);
                const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Id);
                if (Player && Player->PlayerID == Action.PlayerID && Unit && Unit->HitPoints > 0)
                {
                    if (!EM.GetComponent<Game::Components::BuildingComponent>(Id) &&
                        !EM.GetComponent<Game::Components::ResourceComponent>(Id) &&
                        EM.GetComponent<Game::Components::CommandComponent>(Id))
                    {
                        Units.push_back((int)Id);
                    }
                }
            });
        return Units;
    };

    switch (CmdType)
    {
    case 0:
    case 1:
    {
        float X = 0.0f;
        float Y = 0.0f;
        std::vector<int> Units;

        if (CmdType == 0 && Action.Data.size() >= 9)
        {
            X = ReadNetworkActionFloat(Action.Data, 1);
            Y = ReadNetworkActionFloat(Action.Data, 5);
            if (Action.Data.size() > 9)
            {
                for (size_t i = 9; i + sizeof(int) <= Action.Data.size(); i += sizeof(int))
                {
                    int Id = 0;
                    std::memcpy(&Id, Action.Data.data() + i, sizeof(Id));
                    if (EM.IsAlive(Id))
                        Units.push_back(Id);
                }
            }
        }
        else if (Action.Data.size() >= 5)
        {
            const uint16_t P1 = (uint16_t)Action.Data[1] | ((uint16_t)Action.Data[2] << 8);
            const uint16_t P2 = (uint16_t)Action.Data[3] | ((uint16_t)Action.Data[4] << 8);
            X = (float)P1;
            Y = (float)P2;
        }

        if (Units.empty())
            Units = CollectControllableUnits();

        if (!Units.empty())
            Game::Systems::CommandSystem::IssueMove(EM, Units, {X, Y});
        break;
    }
    default:
        break;
    }
}

} // namespace Game::Client::GameLoop

#include "Main/GameSession.h"
#include "UI/Hud.h"

#include "Main/GameMatchOutcomeRuntime.h"

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

    // Staged Dual-Phase Execution (Unity PlayerLoop / Godot SceneTree physics vs process split):
    // 1. Advance deterministic simulation phases (Input -> NetworkLockstep -> FixedSimulation -> TransformPropagation)
    //    and populate Session.KinematicsStream SoA buffers + optional RollbackStateManager ring buffer.
    Session.UpdateSimulationStep(Dt);

    // 2. Advance presentation & render extraction phases (Presentation -> RenderExtract)
    Session.UpdatePresentationFrame(Dt);

    Audio.UpdateForClientView(Client, Dt);

    if (Session.IsReplayRecording())
    {
        // Record dummy action per frame to keep the replay stream alive.
        // Real actions are recorded via RecordGameAction().
    }

    Session.Bwapi().OnFrame();

    SyncHudFromPlayerResources(Hud, Session.GetLocalPlayerID());

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

        const auto State = Campaign.Controller().GetState();
        if (State == Game::Campaign::CampaignState::Victory)
            Session.SetMatchOutcome(MatchOutcome::Victory);
        else if (State == Game::Campaign::CampaignState::Defeat)
            Session.SetMatchOutcome(MatchOutcome::Defeat);
        else if (State == Game::Campaign::CampaignState::Draw)
            Session.SetMatchOutcome(MatchOutcome::Draw);
    }
    else
    {
        UpdateMeleeMatchOutcome(Session, Dt);
    }

    // Scenario-triggered pauses freeze the simulation but keep
    // CampaignController ticking so Wait/Unpause/transmission triggers can resume.
    if (!Campaign.IsScenarioPaused())
        UpdateGameplaySimulationRuntime(Session, Audio, Client, Hud, Dt);
}

} // namespace Game::Client::GameLoop

#include "Main/GameUiPresentation.h"

namespace Game::Client::GameLoop
{

inline Core::Scene::SceneId ToSceneId(GameState State)
{
    return State == GameState::InGame
        ? Core::Scene::SceneId::Gameplay
        : Core::Scene::SceneId::MainMenu;
}

inline GameState ToGameState(Core::Scene::SceneId Id)
{
    return Id == Core::Scene::SceneId::Gameplay
        ? GameState::InGame
        : GameState::MainMenu;
}

class GlueMenuScene final : public Core::Scene::IScene
{
public:
    explicit GlueMenuScene(GameRuntimeState& Runtime)
        : mRuntime(Runtime)
    {
    }

    Core::Scene::SceneId GetId() const override { return Core::Scene::SceneId::MainMenu; }
    std::string_view GetName() const override { return "GlueMenuScene"; }

    bool OnPreUpdate(float /*DeltaSeconds*/) override
    {
        UpdateGuiPresentationForState(mRuntime.App.Gui, GameState::MainMenu);
        return true; // Main menu does not advance gameplay 24Hz ticks
    }

    void OnRender(float /*InterpolationAlpha*/) override
    {
        RenderGlueMenuPresentationLayer(mRuntime.App.Gui);
    }

    void OnEvent(const void* Event) override
    {
        HandleGuiPresentationEventForState(mRuntime.App.Gui, GameState::MainMenu, Event);
    }

private:
    GameRuntimeState& mRuntime;
};

class GameplayScene final : public Core::Scene::IScene
{
public:
    explicit GameplayScene(GameRuntimeState& Runtime)
        : mRuntime(Runtime)
    {
    }

    Core::Scene::SceneId GetId() const override { return Core::Scene::SceneId::Gameplay; }
    std::string_view GetName() const override { return "GameplayScene"; }

    void OnEnter() override
    {
        mRuntime.Session.Game.Entities().ActivateWorld();
    }

    void OnFixedUpdate(float FixedDeltaSeconds) override
    {
        mRuntime.Session.Game.SaveTransformTickState();
        UpdateCampaignAndSimulationRuntime(
            mRuntime.Session.Campaign,
            mRuntime.Session.Game,
            mRuntime.App.Audio,
            mRuntime.Client.Runtime,
            mRuntime.Client.Hud,
            FixedDeltaSeconds);
    }

    void OnPostUpdate(float /*DeltaSeconds*/, int FixedTicksExecuted) override
    {
        if (FixedTicksExecuted > 0 && mRuntime.Session.Game.IsMapLoaded())
        {
            mRuntime.Session.Game.SyncCreepPresentation();
        }
    }

    void OnRender(float InterpolationAlpha) override
    {
        RenderGameplayWorldFrame(mRuntime.Client.Runtime, mRuntime.Session.Game, InterpolationAlpha);
        RenderGameplayUiFrame(
            mRuntime.Client.Runtime,
            mRuntime.Session.Game,
            mRuntime.Session.Campaign,
            mRuntime.App.Menu,
            mRuntime.Client.Hud,
#if ENABLE_GUI_UI
            !(mRuntime.App.Gui.UseGui && mRuntime.App.Gui.MainGui.IsActive())
#else
            true
#endif
        );
        RenderGuiPresentationLayer(mRuntime.App.Gui);
        mRuntime.App.Runtime.UpdateFrameStats();
    }

    void OnEvent(const void* Event) override
    {
        HandleGuiPresentationEventForState(mRuntime.App.Gui, GameState::InGame, Event);
    }

private:
    GameRuntimeState& mRuntime;
};

inline Core::Scene::SceneManager& EnsureRuntimeScenesRegistered(GameRuntimeState& Runtime)
{
    auto& Scenes = Runtime.App.Runtime.Scenes;
    if (!Scenes.HasScene(Core::Scene::SceneId::MainMenu))
    {
        Scenes.ConfigureFixedStep(1.0 / 24.0, 2);
        Scenes.RegisterScene(std::make_unique<GlueMenuScene>(Runtime));
        Scenes.RegisterScene(std::make_unique<GameplayScene>(Runtime));
    }

    const Core::Scene::SceneId DesiredScene = ToSceneId(Runtime.App.Runtime.State);
    if (Scenes.GetActiveSceneId() != DesiredScene)
    {
        Scenes.SwitchToImmediate(DesiredScene);
    }
    return Scenes;
}

} // namespace Game::Client::GameLoop
