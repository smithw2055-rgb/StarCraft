#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Audio/AudioSystem.h"
#include "Core/Audio/ModernAudioEventRouter.h"
#include "Core/ECS/SystemGroup.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Engine/ECS/CommonComponents2D.h"
#include "Engine/EngineApp.h"
#include "Engine/Project/EngineProjectHost.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Races/UnitPrototypeRegistry.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/DeclarativeAnimationPlayer.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/SystemAssetBridge.h"
#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Render/ModernRenderPipeline.h"
#include "Resource/ModernAssetRegistry.h"

namespace Game
{

struct InstalledGameplayPipelineHandles
{
    Core::ECS::SystemGroup* SpatialAndCommandGroup = nullptr;
    Core::ECS::SystemGroup* LocomotionAndEconomyGroup = nullptr;
    Core::ECS::SystemGroup* CombatAndRaceGroup = nullptr;
    Core::ECS::SystemGroup* PresentationAndAudioGroup = nullptr;
    Core::ECS::SystemGroup* RenderExtractionGroup = nullptr;
    Game::Systems::FogOfWarSystem* FogOfWar = nullptr;
    Game::Systems::BulletSystem* BulletSys = nullptr;
    Core::Audio::ModernAudioEventRouterSystem* AudioRouter = nullptr;
    Render::ModernRenderPipelineSystem* RenderPipeline = nullptr;
};

// ============================================================================
// StarCraftEnginePlugin / StarCraftGameModule (Phases 48–77):
// StarCraft RTS Experimental Game Module Mounted on General-Purpose EngineApp
// Demonstrates how StarCraft: Remastered acts as a decoupled game project module
// hosted on top of the reusable Engine Foundation (Engine::EngineApp & EngineProjectHost):
// - Mounts cloud assets-R onto Engine::VFS::VirtualFileSystemServer ("res://")
// - Loads or binds cloud-converted ModernAssetRegistry (assets-R)
// - Auto-populates all 19 Engine Foundation Servers:
//   * Engine::Scene::PrefabRegistry (228 unit prefabs)
//   * Engine::Animation::AnimationServer2D (398 controllers / 1,364 clips + 2D sockets)
//   * Engine::Audio::AudioServer2D (1,144 SFX catalog)
//   * Engine::Combat::CombatAndProjectileServer2D (130 weapons catalog & damage matrix)
//   * Engine::Gameplay::EconomyAndProductionServer (228 unit production costs & queues)
//   * Engine::Vision::VisionServer2D (multi-player elevation-aware FoW & detector grid)
//   * Engine::Input::CameraAndSelectionServer2D (camera rig & 10-group control groups)
//   * Engine::UI::UIServer2D (12 HD UI & wireframe atlases)
//   * Engine::Tilemap::TilemapServer2D (8 GPU Tileset LUTs + 395 foliage records + 294 parallax stars)
//   * Engine::Scripting::ScenarioTriggerServer (CHK scenario trigger metadata)
//   * Engine::AI::BehaviorTreeServer (449 AIScript + BWScript behavior trees)
//   * Engine::ECS::CapabilityTraitRegistry (228 unit capability bitmasks)
// - Synchronizes UnitPrototypeRegistry and SystemAssetBridge without legacy .dat files
// - Provides unified `InstallStandardGameplaySystemGroups` shared with `GameSessionBootstrap`
// ============================================================================
class StarCraftEnginePlugin final : public Engine::Project::IGameModule
{
public:
    explicit StarCraftEnginePlugin(std::string AssetsRPath = "assets-R",
                                   Game::Map::GameMap* InitialMap = nullptr)
        : mAssetsRPath(std::move(AssetsRPath))
        , mMap(InitialMap)
    {
    }

    std::string_view GetName() const override { return "StarCraftEnginePlugin"; }
    std::string_view GetModuleVersion() const override { return "3.0.0-Remastered-HD-Phase77"; }

    void ConfigureProject(Engine::Project::ProjectManifest& Manifest) override
    {
        Manifest.ProjectName = "StarCraft: Remastered (Experimental Engine Project)";
        Manifest.FixedTickHz = 24.0f;
        Manifest.MaxSubStepsPerFrame = 4;
        if (!mAssetsRPath.empty())
            Manifest.AssetMountRoot = mAssetsRPath;
        Manifest.StartupSceneUri = "res://04_Terrain_Environment_And_Shaders/Byways_Chk_Full_Scene.json";
        Manifest.AuthoritativeRollback = true;
    }

    void OnMountVFS(Engine::VFS::VirtualFileSystemServer& VFS,
                    const Engine::Project::ProjectManifest& Manifest) override
    {
        if (!Manifest.AssetMountRoot.empty() && VFS.GetMountCount() == 0)
        {
            VFS.MountDirectory("res://", Manifest.AssetMountRoot, /*Priority=*/0, /*ReadOnly=*/true);
        }
    }

    bool OnLoadInitialScene(Engine::EngineApp& App,
                            const Engine::Project::ProjectManifest& Manifest) override
    {
        const std::string ScenePath = App.Context().VFS().ResolvePhysicalPath(Manifest.StartupSceneUri);
        App.Context().ScenarioTriggers().ImportScenarioMetadataFromChkJsonFile(ScenePath);
        const std::string AssetsRoot = !Manifest.AssetMountRoot.empty() ? Manifest.AssetMountRoot : mAssetsRPath;
        const bool Loaded = App.Context().SceneLoader().LoadSceneFromAssetsR(
            AssetsRoot,
            App.Entities(),
            App.Context().SceneTree(),
            App.Context().Prefabs(),
            &App.Context().Navigation2D(),
            &App.Context().Physics2D());
        if (Loaded)
        {
            const auto& Scene = App.Context().SceneLoader().GetActiveScene();
            if (Scene.WidthTiles > 0 && Scene.HeightTiles > 0)
            {
                App.Context().Vision2D().InitializeGrid(Scene.WidthTiles, Scene.HeightTiles, 8, 32);
                App.Context().BuildingPlacement2D().InitializeGrid(Scene.WidthTiles, Scene.HeightTiles, 32.0f);
                App.Context().CameraAndSelection2D().SetWorldBoundsByTiles(
                    Scene.WidthTiles, Scene.HeightTiles, 32.0f);
            }
        }
        return Loaded;
    }

    void OnRegister(Engine::EngineApp& App) override
    {
        App.RollbackBuffer().RegisterWorldResource<Game::Systems::PlayerResourceStore>();
        App.RollbackBuffer().RegisterWorldResource<Core::ECS::WorldUpgradeResource>();
    }

    // Unified SystemGroup pipeline installer shared across EngineApp and GameSessionBootstrap
    static InstalledGameplayPipelineHandles InstallStandardGameplaySystemGroups(
        Core::ECS::SystemManager& SysMgr,
        Game::Map::GameMap* Map,
        Core::Audio::AudioSystem* LegacyAudio = nullptr,
        const Resource::ModernAssetRegistry* AssetRegistry = nullptr,
        Engine::Animation::AnimationServer2D* AnimServer = nullptr,
        int LocalPlayerID = 0,
        bool ObserverRevealAll = false)
    {
        InstalledGameplayPipelineHandles Out{};
        auto& World = SysMgr.GetEntityManager();

        // Group 1: Spatial & Command Ingestion Group (FixedSimulation Phase)
        auto SpatialAndCommandGroup = std::make_unique<Core::ECS::SystemGroup>(
            "SpatialAndCommandGroup", Core::ECS::SystemPhase::FixedSimulation, &World);
        SpatialAndCommandGroup->AddSubSystemInstance(
            std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SysMgr), false);
        auto CmdSys = std::make_unique<Game::Systems::CommandSystem>();
        CmdSys->Init(&SysMgr);
        SpatialAndCommandGroup->AddSubSystemInstance(std::move(CmdSys), true);
        Out.SpatialAndCommandGroup = SpatialAndCommandGroup.get();
        SysMgr.RegisterSystemInPhase(Core::ECS::SystemPhase::FixedSimulation, std::move(SpatialAndCommandGroup));

        // Group 2: Locomotion, Economy & Construction Group (FixedSimulation Phase)
        auto LocomotionAndEconomyGroup = std::make_unique<Core::ECS::SystemGroup>(
            "LocomotionAndEconomyGroup", Core::ECS::SystemPhase::FixedSimulation, &World);
        LocomotionAndEconomyGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::MovementSystem>(SysMgr, Map), false);
        LocomotionAndEconomyGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::HarvestSystem>(SysMgr), false);
        LocomotionAndEconomyGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::BuildingSystem>(SysMgr), true);
        LocomotionAndEconomyGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::ProductionSystem>(SysMgr), true);
        Out.LocomotionAndEconomyGroup = LocomotionAndEconomyGroup.get();
        SysMgr.RegisterSystemInPhase(Core::ECS::SystemPhase::FixedSimulation, std::move(LocomotionAndEconomyGroup));

        // Group 3: Combat, Projectiles, FogOfWar & Race Mechanics Group (FixedSimulation Phase)
        auto CombatAndRaceGroup = std::make_unique<Core::ECS::SystemGroup>(
            "CombatAndRaceGroup", Core::ECS::SystemPhase::FixedSimulation, &World);
        auto BulletSys = std::make_unique<Game::Systems::BulletSystem>(SysMgr);
        auto* BulletSysRaw = BulletSys.get();
        Out.BulletSys = BulletSysRaw;
        CombatAndRaceGroup->AddSubSystemInstance(std::move(BulletSys), false);
        CombatAndRaceGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::CombatSystem>(SysMgr, *BulletSysRaw, Map), true);
        CombatAndRaceGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::UpgradeSystem>(SysMgr), false);
        auto FogSys = std::make_unique<Game::Systems::FogOfWarSystem>(SysMgr, Map);
        FogSys->SetLocalPlayerID(LocalPlayerID);
        FogSys->SetObserverRevealAll(ObserverRevealAll);
        Out.FogOfWar = FogSys.get();
        CombatAndRaceGroup->AddSubSystemInstance(std::move(FogSys), false);
        CombatAndRaceGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::ZergSystem>(SysMgr, Map), false);
        CombatAndRaceGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::ProtossSystem>(SysMgr, Map), false);
        CombatAndRaceGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::NukeAndSpiderMineSystem>(SysMgr, *BulletSysRaw), false);
        CombatAndRaceGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::SpellSystem>(SysMgr), true);
        CombatAndRaceGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::AnimationSystem>(SysMgr, LegacyAudio, BulletSysRaw), false);
        CombatAndRaceGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::TransportSystem>(SysMgr), true);
        Out.CombatAndRaceGroup = CombatAndRaceGroup.get();
        SysMgr.RegisterSystemInPhase(Core::ECS::SystemPhase::FixedSimulation, std::move(CombatAndRaceGroup));

        // Group 4: Presentation & Audio Event Router Group (Presentation Phase)
        auto PresentationGroup = std::make_unique<Core::ECS::SystemGroup>(
            "PresentationAndAudioGroup", Core::ECS::SystemPhase::Presentation, &World);
        if (AnimServer || AssetRegistry)
        {
            PresentationGroup->AddSubSystemInstance(
                std::make_unique<Game::Systems::DeclarativeAnimationPlayerSystem>(
                    SysMgr, AssetRegistry, AnimServer),
                false);
        }
        auto AudioRouter = std::make_unique<Core::Audio::ModernAudioEventRouterSystem>(
            SysMgr, AssetRegistry, LegacyAudio);
        Out.AudioRouter = AudioRouter.get();
        PresentationGroup->AddSubSystemInstance(std::move(AudioRouter), false);
        Out.PresentationAndAudioGroup = PresentationGroup.get();
        SysMgr.RegisterSystemInPhase(Core::ECS::SystemPhase::Presentation, std::move(PresentationGroup));

        // Group 5: Modern Render Pipeline Extraction Group (RenderExtract Phase)
        auto RenderExtractGroup = std::make_unique<Core::ECS::SystemGroup>(
            "RenderExtractionGroup", Core::ECS::SystemPhase::RenderExtract, &World);
        auto RenderPipeline = std::make_unique<Render::ModernRenderPipelineSystem>(SysMgr, AssetRegistry);
        Out.RenderPipeline = RenderPipeline.get();
        RenderExtractGroup->AddSubSystemInstance(std::move(RenderPipeline), false);
        Out.RenderExtractionGroup = RenderExtractGroup.get();
        SysMgr.RegisterSystemInPhase(Core::ECS::SystemPhase::RenderExtract, std::move(RenderExtractGroup));

        return Out;
    }

    void OnStartup(Engine::EngineApp& App) override
    {
        auto& SM = App.PrimaryWorld();
        auto& EM = SM.GetEntityManager();

        // 1. Mount VFS and load cloud assets-R catalog into all 19 Engine Servers
        if (!mAssetsRPath.empty())
        {
            App.Context().VFS().MountDirectory("res://", mAssetsRPath, /*Priority=*/0, /*ReadOnly=*/true);
            if (mOwnedAssetRegistry.LoadFromAssetsRDirectory(mAssetsRPath))
            {
                mActiveRegistry = &mOwnedAssetRegistry;
            }
            App.Context().BehaviorTrees().LoadFromAssetsRDirectory(mAssetsRPath);
            App.Context().Tilemap2D().LoadFromAssetsRDirectory(mAssetsRPath);
            App.Context().UI2D().ImportHDUIAtlasesFromManifest(
                App.Context().VFS().ResolvePhysicalPath(
                    "res://04_Terrain_Environment_And_Shaders/Terrain_CHK_And_HD_Effects_Manifest.json"));
            App.Context().ScenarioTriggers().ImportScenarioMetadataFromChkJsonFile(
                App.Context().VFS().ResolvePhysicalPath(
                    "res://04_Terrain_Environment_And_Shaders/Byways_Chk_Full_Scene.json"));
        }
        if (mActiveRegistry && mActiveRegistry->IsLoaded())
        {
            App.Context().Assets().BindModernRegistry(mActiveRegistry);
            App.Context().Prefabs().BuildUnitPrefabsFromModernRegistry(*mActiveRegistry);
            App.Context().Animation2D().ImportFromModernRegistry(*mActiveRegistry);
            App.Context().Audio2D().ImportFromModernRegistry(*mActiveRegistry);
            App.Context().EconomyAndProduction().ImportFromModernRegistry(*mActiveRegistry);
            App.Context().Combat2D().ImportFromModernRegistry(*mActiveRegistry);
            mTraitRegistry.ImportFromModernRegistry(*mActiveRegistry);
            Game::Systems::SystemAssetBridge::BindModernAssetRegistry(*mActiveRegistry);
            SyncUnitPrototypesFromModernAssets(*mActiveRegistry);
        }

        // 2. Initialize authoritative world-scoped resources & NavigationServer2D / VisionServer2D
        if (!mMap)
        {
            mOwnedFallbackMap = std::make_unique<Game::Map::GameMap>();
            mOwnedFallbackMap->Generate(128, 128);
            mMap = mOwnedFallbackMap.get();
        }
        App.Context().Navigation2D().InitializeGrid(mMap->GetWidth(), mMap->GetHeight(), 32.0f);
        App.Context().Navigation2D().RebuildConnectedRegions(1u);
        App.Context().Vision2D().InitializeGrid(mMap->GetWidth(), mMap->GetHeight(), 8, 32);
        App.Context().BuildingPlacement2D().InitializeGrid(mMap->GetWidth(), mMap->GetHeight(), 32.0f);
        App.Context().TechAndUpgrades().LoadStandardRTSCatalog();
        App.Context().AbilityAndStatus2D().LoadStandardRTSAbilities();
        App.Context().CameraAndSelection2D().SetWorldBoundsByTiles(mMap->GetWidth(), mMap->GetHeight(), 32.0f);
        App.Context().EconomyAndProduction().ResetWallets(50, 0, 10);
        Game::Systems::BuildingSystem::BindEnginePlacementServer(&App.Context().BuildingPlacement2D());
        Game::Systems::UpgradeSystem::BindEngineTechAndUpgradeServer(&App.Context().TechAndUpgrades());
        Game::Systems::SpellSystem::BindEngineAbilityServer(&App.Context().AbilityAndStatus2D());
        Game::Systems::TransportSystem::BindEngineGarrisonServer(&App.Context().GarrisonAndTransport2D());

        auto& SimCtx = Game::SimulationContext::Get(EM);
        SimCtx.Map = mMap;
        SimCtx.MutableMap = mMap;
        SimCtx.LocalPlayerID = 0;

        Game::Systems::HarvestSystem::ResetResources(EM);
        Game::Systems::UpgradeSystem::ResetUpgrades(EM);

        // 3. Install Unified Hierarchical SystemGroups across FixedSimulation, Presentation, and RenderExtract
        const auto Handles = InstallStandardGameplaySystemGroups(
            SM,
            mMap,
            /*LegacyAudio=*/nullptr,
            mActiveRegistry,
            &App.Context().Animation2D(),
            /*LocalPlayerID=*/0,
            /*ObserverRevealAll=*/false);

        SimCtx.FogOfWar = Handles.FogOfWar;
        mSpatialGroup = Handles.SpatialAndCommandGroup;
        mEconomyGroup = Handles.LocomotionAndEconomyGroup;
        mCombatGroup = Handles.CombatAndRaceGroup;
        mAudioRouter = Handles.AudioRouter;
        mRenderPipeline = Handles.RenderPipeline;
    }

    void OnShutdown(Engine::EngineApp& /*App*/) override
    {
        Game::Systems::BuildingSystem::BindEnginePlacementServer(nullptr);
        Game::Systems::UpgradeSystem::BindEngineTechAndUpgradeServer(nullptr);
        Game::Systems::SpellSystem::BindEngineAbilityServer(nullptr);
        Game::Systems::TransportSystem::BindEngineGarrisonServer(nullptr);
        Game::Systems::SystemAssetBridge::UnbindAll();
    }

    static int SyncUnitPrototypesFromModernAssets(const Resource::ModernAssetRegistry& Registry)
    {
        if (!Registry.IsLoaded())
            return 0;

        std::vector<Game::Races::UnitDatSyncEntry> Entries;
        Entries.reserve(Registry.GetUnitCount());
        for (const auto& ModUnit : Registry.GetUnits())
        {
            Game::Races::UnitDatSyncEntry Entry{};
            Entry.ChkUnitID = static_cast<int>(ModUnit.UnitID);
            Entry.HitPoints = static_cast<int>(ModUnit.MaxHP);
            Entry.ShieldPoints = ModUnit.ShieldsEnabled ? static_cast<int>(ModUnit.MaxShields) : 0;

            if (ModUnit.HasGroundWeapon)
            {
                if (const auto* GW = Registry.FindWeaponByID(ModUnit.GroundWeaponID))
                {
                    Entry.GroundDamage = static_cast<int>(GW->DamageAmount);
                    Entry.GroundDamageBonus = static_cast<int>(GW->DamageBonusPerUpgrade);
                }
            }
            if (ModUnit.HasAirWeapon)
            {
                if (const auto* AW = Registry.FindWeaponByID(ModUnit.AirWeaponID))
                {
                    Entry.AirDamage = static_cast<int>(AW->DamageAmount);
                    Entry.AirDamageBonus = static_cast<int>(AW->DamageBonusPerUpgrade);
                }
            }
            Entries.push_back(Entry);
        }
        return Game::Races::UnitPrototypeRegistry::Instance().SyncFromDatEntries(Entries);
    }

    void SetExternalAssetRegistry(const Resource::ModernAssetRegistry* Registry)
    {
        mActiveRegistry = Registry;
    }

    const Resource::ModernAssetRegistry* GetAssetRegistry() const noexcept { return mActiveRegistry; }
    const Engine::ECS::CapabilityTraitRegistry& GetCapabilityTraits() const noexcept { return mTraitRegistry; }
    Render::ModernRenderPipelineSystem* GetRenderPipeline() const noexcept { return mRenderPipeline; }
    Core::Audio::ModernAudioEventRouterSystem* GetAudioRouter() const noexcept { return mAudioRouter; }
    Core::ECS::SystemGroup* GetSpatialGroup() const noexcept { return mSpatialGroup; }
    Core::ECS::SystemGroup* GetEconomyGroup() const noexcept { return mEconomyGroup; }
    Core::ECS::SystemGroup* GetCombatGroup() const noexcept { return mCombatGroup; }
    Game::Map::GameMap* GetMap() const noexcept { return mMap; }

private:
    std::string mAssetsRPath;
    Resource::ModernAssetRegistry mOwnedAssetRegistry;
    const Resource::ModernAssetRegistry* mActiveRegistry = nullptr;
    Engine::ECS::CapabilityTraitRegistry mTraitRegistry;
    std::unique_ptr<Game::Map::GameMap> mOwnedFallbackMap;
    Game::Map::GameMap* mMap = nullptr;

    Core::ECS::SystemGroup* mSpatialGroup = nullptr;
    Core::ECS::SystemGroup* mEconomyGroup = nullptr;
    Core::ECS::SystemGroup* mCombatGroup = nullptr;
    Core::Audio::ModernAudioEventRouterSystem* mAudioRouter = nullptr;
    Render::ModernRenderPipelineSystem* mRenderPipeline = nullptr;
};

using StarCraftGameModule = StarCraftEnginePlugin;

} // namespace Game
