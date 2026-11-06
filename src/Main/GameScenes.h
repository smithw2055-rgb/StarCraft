#pragma once

#include <memory>
#include <string_view>

#include "Core/Scene/SceneManager.h"
#include "Main/AppRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameRuntimeState.h"
#include "Main/GameSimulationRuntime.h"
#include "Main/GameUiPresentation.h"
#include "Main/GameWorldPresentation.h"

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
