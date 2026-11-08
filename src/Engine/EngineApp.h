#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Audio/AudioBusMixer.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/ECS/SystemGroup.h"
#include "Core/ECS/SystemManager.h"
#include "Core/Input/InputActionMap.h"
#include "Core/Scene/SceneManager.h"
#include "Engine/AI/BehaviorTreeServer.h"
#include "Engine/Animation/AnimationServer2D.h"
#include "Engine/Audio/AudioServer2D.h"
#include "Engine/DataAssetRegistry.h"
#include "Engine/Debug/EngineTelemetryServer.h"
#include "Engine/ECS/GenericWorldSnapshot.h"
#include "Engine/ECS/WorldSerializer.h"
#include "Engine/EngineTime.h"
#include "Engine/Input/CommandRouter.h"
#include "Engine/Navigation/NavigationServer2D.h"
#include "Engine/Net/DeterministicSessionServer.h"
#include "Engine/Net/NetworkTransportServer.h"
#include "Engine/Node2D.h"
#include "Engine/Physics/PhysicsServer2D.h"
#include "Engine/Render/RenderGraph2D.h"
#include "Engine/Scene/PackedScene2D.h"
#include "Engine/Scene/PrefabRegistry.h"
#include "Engine/Scene/SceneLoaderServer.h"
#include "Engine/Scripting/ScenarioTriggerServer.h"
#include "Engine/Tilemap/TilemapServer2D.h"
#include "Engine/UI/UIServer2D.h"
#include "Engine/VFS/VirtualFileSystemServer.h"
#include "Render/RenderServer2D.h"
#include "Resource/AssetServer.h"

namespace Engine
{

class EngineApp;

// ============================================================================
// IPlugin: Modular Game / Subsystem Plugin Contract
// Referenced from Unity DOTS / Bevy Plugin & Godot EditorPlugin/MainLoop:
// - Allows decoupled engine extensions and game modules (e.g. StarCraftEnginePlugin)
//   to register ECS systems, WorldContext resources, input actions, and scenes
//   into a general-purpose EngineApp host.
// ============================================================================
class IPlugin
{
public:
    virtual ~IPlugin() = default;
    virtual std::string_view GetName() const = 0;

    virtual void OnRegister(EngineApp& /*App*/) {}
    virtual void OnStartup(EngineApp& /*App*/) {}
    virtual void OnFixedUpdate(EngineApp& /*App*/, float /*FixedDeltaSeconds*/) {}
    virtual void OnUpdate(EngineApp& /*App*/, float /*FrameDeltaSeconds*/) {}
    virtual void OnShutdown(EngineApp& /*App*/) {}
};

// ============================================================================
// EngineContext: Unified 15-Server Architecture Container (Phases 34–60)
// Referenced from Godot 4 Server Architecture (RenderingServer, AudioServer2D,
// AnimationServer2D, PhysicsServer2D, NavigationServer2D, UIServer2D,
// TilemapServer2D, VirtualFileSystemServer, ScenarioTriggerServer,
// SceneLoaderServer, ResourceLoader, InputMap, SceneTree) & Unity Subsystems:
// - Aggregates all core engine servers into a single non-global context owned
//   by EngineApp, eliminating scattered global state across modules.
// ============================================================================
class EngineContext
{
public:
    explicit EngineContext(float FixedStepSeconds = 1.0f / 24.0f,
                           int MaxSubStepsPerFrame = 4)
        : mTime(FixedStepSeconds, MaxSubStepsPerFrame)
        , mScenes()
    {
    }

    Core::Concurrency::JobSystem& Jobs() noexcept { return Core::Concurrency::JobSystem::Instance(); }

    Engine::VFS::VirtualFileSystemServer& VFS() noexcept { return mVFSServer; }
    const Engine::VFS::VirtualFileSystemServer& VFS() const noexcept { return mVFSServer; }

    Resource::AssetServer& Assets() noexcept { return mAssetServer; }
    const Resource::AssetServer& Assets() const noexcept { return mAssetServer; }

    ::Render::RenderServer2D& Rendering2D() noexcept { return mRenderServer2D; }
    const ::Render::RenderServer2D& Rendering2D() const noexcept { return mRenderServer2D; }

    Engine::Render::RenderGraph2D& RenderGraph() noexcept { return mRenderGraph2D; }
    const Engine::Render::RenderGraph2D& RenderGraph() const noexcept { return mRenderGraph2D; }

    Engine::Tilemap::TilemapServer2D& Tilemap2D() noexcept { return mTilemapServer2D; }
    const Engine::Tilemap::TilemapServer2D& Tilemap2D() const noexcept { return mTilemapServer2D; }

    Engine::Animation::AnimationServer2D& Animation2D() noexcept { return mAnimationServer2D; }
    const Engine::Animation::AnimationServer2D& Animation2D() const noexcept { return mAnimationServer2D; }

    Engine::Audio::AudioServer2D& Audio2D() noexcept { return mAudioServer2D; }
    const Engine::Audio::AudioServer2D& Audio2D() const noexcept { return mAudioServer2D; }

    Engine::Physics::PhysicsServer2D& Physics2D() noexcept { return mPhysicsServer2D; }
    const Engine::Physics::PhysicsServer2D& Physics2D() const noexcept { return mPhysicsServer2D; }

    Engine::Navigation::NavigationServer2D& Navigation2D() noexcept { return mNavigationServer2D; }
    const Engine::Navigation::NavigationServer2D& Navigation2D() const noexcept { return mNavigationServer2D; }

    Engine::AI::BehaviorTreeServer& BehaviorTrees() noexcept { return mBehaviorTreeServer; }
    const Engine::AI::BehaviorTreeServer& BehaviorTrees() const noexcept { return mBehaviorTreeServer; }

    Engine::Scripting::ScenarioTriggerServer& ScenarioTriggers() noexcept { return mScenarioTriggerServer; }
    const Engine::Scripting::ScenarioTriggerServer& ScenarioTriggers() const noexcept { return mScenarioTriggerServer; }

    Engine::UI::UIServer2D& UI2D() noexcept { return mUIServer2D; }
    const Engine::UI::UIServer2D& UI2D() const noexcept { return mUIServer2D; }

    Engine::Scene::PrefabRegistry& Prefabs() noexcept { return mPrefabRegistry; }
    const Engine::Scene::PrefabRegistry& Prefabs() const noexcept { return mPrefabRegistry; }

    Engine::Scene::SceneLoaderServer& SceneLoader() noexcept { return mSceneLoaderServer; }
    const Engine::Scene::SceneLoaderServer& SceneLoader() const noexcept { return mSceneLoaderServer; }

    Engine::Net::LoopbackNetworkHub& NetTransportHub() noexcept { return mNetTransportHub; }
    const Engine::Net::LoopbackNetworkHub& NetTransportHub() const noexcept { return mNetTransportHub; }

    Engine::Debug::EngineTelemetryServer& Telemetry() noexcept { return mTelemetryServer; }
    const Engine::Debug::EngineTelemetryServer& Telemetry() const noexcept { return mTelemetryServer; }

    Core::Audio::AudioBusMixer& AudioBuses() noexcept { return mAudioServer2D.BusMixer(); }
    const Core::Audio::AudioBusMixer& AudioBuses() const noexcept { return mAudioServer2D.BusMixer(); }

    Core::Input::InputActionMap& InputMap() noexcept { return mInputActionMap; }
    const Core::Input::InputActionMap& InputMap() const noexcept { return mInputActionMap; }

    Core::Scene::SceneManager& Scenes() noexcept { return mScenes; }
    const Core::Scene::SceneManager& Scenes() const noexcept { return mScenes; }

    SceneTree2D& SceneTree() noexcept { return mSceneTree2D; }
    const SceneTree2D& SceneTree() const noexcept { return mSceneTree2D; }

    TimeStepAccumulator& Time() noexcept { return mTime; }
    const TimeStepAccumulator& Time() const noexcept { return mTime; }

private:
    TimeStepAccumulator mTime;
    Engine::VFS::VirtualFileSystemServer mVFSServer;
    Resource::AssetServer mAssetServer;
    ::Render::RenderServer2D mRenderServer2D;
    Engine::Render::RenderGraph2D mRenderGraph2D;
    Engine::Tilemap::TilemapServer2D mTilemapServer2D;
    Engine::Animation::AnimationServer2D mAnimationServer2D;
    Engine::Audio::AudioServer2D mAudioServer2D;
    Engine::Physics::PhysicsServer2D mPhysicsServer2D;
    Engine::Navigation::NavigationServer2D mNavigationServer2D;
    Engine::AI::BehaviorTreeServer mBehaviorTreeServer;
    Engine::Scripting::ScenarioTriggerServer mScenarioTriggerServer;
    Engine::UI::UIServer2D mUIServer2D;
    Engine::Scene::PrefabRegistry mPrefabRegistry;
    Engine::Scene::SceneLoaderServer mSceneLoaderServer;
    Engine::Net::LoopbackNetworkHub mNetTransportHub;
    Engine::Debug::EngineTelemetryServer mTelemetryServer;
    Core::Input::InputActionMap mInputActionMap = Core::Input::InputActionMap::CreateDefaultRTSMap();
    Core::Scene::SceneManager mScenes;
    SceneTree2D mSceneTree2D;
};

// ============================================================================
// EngineApp: General-Purpose Modular Game Engine Host
// ============================================================================
class EngineApp
{
public:
    explicit EngineApp(float FixedStepSeconds = 1.0f / 24.0f,
                       int MaxSubStepsPerFrame = 4)
        : mContext(FixedStepSeconds, MaxSubStepsPerFrame)
    {
    }

    ~EngineApp()
    {
        if (mInitialized)
            Shutdown();
    }

    EngineApp(const EngineApp&) = delete;
    EngineApp& operator=(const EngineApp&) = delete;

    template <typename PluginT, typename... Args>
    PluginT& AddPlugin(Args&&... InArgs)
    {
        auto Plugin = std::make_unique<PluginT>(std::forward<Args>(InArgs)...);
        PluginT& Ref = *Plugin;
        Plugin->OnRegister(*this);
        mPlugins.push_back(std::move(Plugin));
        return Ref;
    }

    IPlugin* FindPlugin(std::string_view Name) const
    {
        for (const auto& P : mPlugins)
        {
            if (P && P->GetName() == Name)
                return P.get();
        }
        return nullptr;
    }

    void ConfigureFixedStep(float FixedStepSeconds, int MaxSubStepsPerFrame = 4) noexcept
    {
        mContext.Time().SetFixedStepSeconds(FixedStepSeconds);
        mContext.Time().SetMaxSubStepsPerFrame(MaxSubStepsPerFrame);
    }

    void Initialize()
    {
        if (mInitialized)
            return;
        mPrimaryWorld.GetEntityManager().ActivateWorld();
        for (auto& Plugin : mPlugins)
        {
            if (Plugin)
                Plugin->OnStartup(*this);
        }
        mInitialized = true;
    }

    void Startup() { Initialize(); }

    void Shutdown()
    {
        if (!mInitialized)
            return;
        for (auto It = mPlugins.rbegin(); It != mPlugins.rend(); ++It)
        {
            if (*It)
                (*It)->OnShutdown(*this);
        }
        mAuxiliaryWorlds.clear();
        mContext.SceneTree().ClearAndRecycleToPool();
        mInitialized = false;
    }

    int StepFrame(float WallDeltaSeconds, uint32_t GpuUploadBudgetBytes = 4u * 1024u * 1024u)
    {
        if (!mInitialized)
            Initialize();

        const int SubSteps = mContext.Time().BeginFrame(WallDeltaSeconds);
        const float FixedDt = mContext.Time().GetFixedStepSeconds();

        for (int Step = 0; Step < SubSteps; ++Step)
        {
            mContext.Time().ConsumeSubStep();
            ExecuteSingleFixedTick(FixedDt);
        }

        for (auto& Plugin : mPlugins)
        {
            if (Plugin)
                Plugin->OnUpdate(*this, WallDeltaSeconds);
        }

        mContext.Assets().FlushPendingGpuUploads(GpuUploadBudgetBytes);
        mContext.Tilemap2D().StepEnvironmentSimulation(WallDeltaSeconds);
        mContext.Audio2D().FlushFrame({0.0f, 0.0f}, 1600.0f, WallDeltaSeconds);
        mContext.SceneTree().PropagateAllTransforms();

        mPrimaryWorld.UpdatePresentationFrame(WallDeltaSeconds);
        return SubSteps;
    }

    void StepFixedTicks(uint32_t TickCount)
    {
        if (!mInitialized)
            Initialize();

        const float FixedDt = mContext.Time().GetFixedStepSeconds();
        for (uint32_t i = 0; i < TickCount; ++i)
        {
            mContext.Time().StepExactTicks(1u);
            ExecuteSingleFixedTick(FixedDt);
        }
    }

    Core::ECS::SystemManager& PrimaryWorld() noexcept { return mPrimaryWorld; }
    const Core::ECS::SystemManager& PrimaryWorld() const noexcept { return mPrimaryWorld; }

    Core::ECS::EntityManager& Entities() noexcept { return mPrimaryWorld.GetEntityManager(); }
    const Core::ECS::EntityManager& Entities() const noexcept { return mPrimaryWorld.GetEntityManager(); }

    Core::ECS::SystemManager& CreateOrGetAuxiliaryWorld(const std::string& WorldName)
    {
        auto It = mAuxiliaryWorlds.find(WorldName);
        if (It != mAuxiliaryWorlds.end())
            return *It->second;
        auto NewWorld = std::make_unique<Core::ECS::SystemManager>();
        Core::ECS::SystemManager& Ref = *NewWorld;
        mAuxiliaryWorlds[WorldName] = std::move(NewWorld);
        return Ref;
    }

    Core::ECS::SystemManager* FindAuxiliaryWorld(const std::string& WorldName) const
    {
        auto It = mAuxiliaryWorlds.find(WorldName);
        return It != mAuxiliaryWorlds.end() ? It->second.get() : nullptr;
    }

    size_t GetAuxiliaryWorldCount() const noexcept { return mAuxiliaryWorlds.size(); }

    EngineContext& Context() noexcept { return mContext; }
    const EngineContext& Context() const noexcept { return mContext; }

    ECS::GenericRollbackBuffer<64>& RollbackBuffer() noexcept { return mRollbackBuffer; }
    const ECS::GenericRollbackBuffer<64>& RollbackBuffer() const noexcept { return mRollbackBuffer; }

    void SetAutoRecordRollbackSnapshots(bool Enabled) noexcept { mAutoRecordRollback = Enabled; }
    bool GetAutoRecordRollbackSnapshots() const noexcept { return mAutoRecordRollback; }
    void SetAutoRecordSnapshots(bool Enabled) noexcept { mAutoRecordRollback = Enabled; }
    bool GetAutoRecordSnapshots() const noexcept { return mAutoRecordRollback; }

    bool IsInitialized() const noexcept { return mInitialized; }
    size_t GetPluginCount() const noexcept { return mPlugins.size(); }

private:
    void ExecuteSingleFixedTick(float FixedDt)
    {
        const uint32_t CurrentTick = static_cast<uint32_t>(mContext.Time().GetTickCount());
        if (mAutoRecordRollback)
        {
            mRollbackBuffer.RecordTick(mPrimaryWorld.GetEntityManager(), CurrentTick);
        }

        mContext.ScenarioTriggers().EvaluateTick(CurrentTick);

        for (auto& Plugin : mPlugins)
        {
            if (Plugin)
                Plugin->OnFixedUpdate(*this, FixedDt);
        }

        mPrimaryWorld.UpdateSimulationStep(FixedDt);
    }

    EngineContext mContext;
    Core::ECS::SystemManager mPrimaryWorld;
    std::unordered_map<std::string, std::unique_ptr<Core::ECS::SystemManager>> mAuxiliaryWorlds;
    ECS::GenericRollbackBuffer<64> mRollbackBuffer;
    std::vector<std::unique_ptr<IPlugin>> mPlugins;
    bool mInitialized = false;
    bool mAutoRecordRollback = false;
};

} // namespace Engine
