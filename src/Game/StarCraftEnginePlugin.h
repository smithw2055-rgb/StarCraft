#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Audio/ModernAudioEventRouter.h"
#include "Core/ECS/SystemGroup.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Engine/EngineApp.h"
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

// ============================================================================
// StarCraftEnginePlugin: StarCraft RTS Module Mounted on General-Purpose EngineApp
// Demonstrates how StarCraft: Remastered acts as an experimental flagship module
// hosted on top of the reusable Engine Foundation (Engine::EngineApp):
// - Loads or binds cloud-converted ModernAssetRegistry (assets-R)
// - Auto-populates Engine::Scene::PrefabRegistry (228 unit prefabs),
//   Engine::Animation::AnimationServer2D (398 controllers / 1,364 clips + 2D sockets),
//   Engine::Audio::AudioServer2D (1,144 SFX catalog),
//   Engine::UI::UIServer2D (HD UI & wireframe atlases), and
//   Engine::AI::BehaviorTreeServer (449 AIScript + BWScript behavior trees)
// - Synchronizes UnitPrototypeRegistry and SystemAssetBridge without legacy .dat files
// - Registers WorldContext rollback resources (PlayerResourceStore, WorldUpgradeResource)
// - Installs hierarchical SystemGroups across FixedSimulation, Presentation, and RenderExtract
// ============================================================================
class StarCraftEnginePlugin final : public Engine::IPlugin
{
public:
    explicit StarCraftEnginePlugin(std::string AssetsRPath = "assets-R",
                                   Game::Map::GameMap* InitialMap = nullptr)
        : mAssetsRPath(std::move(AssetsRPath))
        , mMap(InitialMap)
    {
    }

    std::string_view GetName() const override { return "StarCraftEnginePlugin"; }

    void OnRegister(Engine::EngineApp& App) override
    {
        // Register StarCraft world singleton resources into EngineApp's generic rollback buffer
        App.RollbackBuffer().RegisterWorldResource<Game::Systems::PlayerResourceStore>();
        App.RollbackBuffer().RegisterWorldResource<Core::ECS::WorldUpgradeResource>();
    }

    void OnStartup(Engine::EngineApp& App) override
    {
        auto& SM = App.PrimaryWorld();
        auto& EM = SM.GetEntityManager();

        // 1. Load cloud assets-R catalog if path is configured and bind to all Engine Servers
        if (!mAssetsRPath.empty())
        {
            if (mOwnedAssetRegistry.LoadFromAssetsRDirectory(mAssetsRPath))
            {
                mActiveRegistry = &mOwnedAssetRegistry;
            }
            App.Context().BehaviorTrees().LoadFromAssetsRDirectory(mAssetsRPath);
            App.Context().UI2D().ImportHDUIAtlasesFromManifest(
                mAssetsRPath + "/04_Terrain_Environment_And_Shaders/Terrain_CHK_And_HD_Effects_Manifest.json");
        }
        if (mActiveRegistry && mActiveRegistry->IsLoaded())
        {
            App.Context().Assets().BindModernRegistry(mActiveRegistry);
            App.Context().Prefabs().BuildUnitPrefabsFromModernRegistry(*mActiveRegistry);
            App.Context().Animation2D().ImportFromModernRegistry(*mActiveRegistry);
            App.Context().Audio2D().ImportFromModernRegistry(*mActiveRegistry);
            Game::Systems::SystemAssetBridge::BindModernAssetRegistry(*mActiveRegistry);
            SyncUnitPrototypesFromModernAssets(*mActiveRegistry);
        }

        // 2. Initialize authoritative world-scoped resources & NavigationServer2D
        if (!mMap)
        {
            mOwnedFallbackMap = std::make_unique<Game::Map::GameMap>();
            mOwnedFallbackMap->Generate(128, 128);
            mMap = mOwnedFallbackMap.get();
        }
        App.Context().Navigation2D().InitializeGrid(mMap->GetWidth(), mMap->GetHeight(), 32.0f);
        App.Context().Navigation2D().RebuildConnectedRegions(1u);

        auto& SimCtx = Game::SimulationContext::Get(EM);
        SimCtx.Map = mMap;
        SimCtx.MutableMap = mMap;
        SimCtx.LocalPlayerID = 0;

        Game::Systems::HarvestSystem::ResetResources(EM);
        Game::Systems::UpgradeSystem::ResetUpgrades(EM);

        // 3. Build Hierarchical FixedSimulation SystemGroups
        auto SpatialGroup = std::make_unique<Core::ECS::SystemGroup>(
            "SpatialAndEnvironmentGroup",
            Core::ECS::SystemPhase::FixedSimulation,
            &EM);
        SpatialGroup->AddSubSystemInstance(
            std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM), false);
        auto CmdSys = std::make_unique<Game::Systems::CommandSystem>();
        CmdSys->Init(&SM);
        SpatialGroup->AddSubSystemInstance(std::move(CmdSys), true);
        auto FogSys = std::make_unique<Game::Systems::FogOfWarSystem>(SM, mMap);
        SimCtx.FogOfWar = FogSys.get();
        SpatialGroup->AddSubSystemInstance(std::move(FogSys), false);
        mSpatialGroup = SpatialGroup.get();
        SM.RegisterSystemInPhase(Core::ECS::SystemPhase::FixedSimulation, std::move(SpatialGroup));

        auto EconomyGroup = std::make_unique<Core::ECS::SystemGroup>(
            "EconomyAndProductionGroup",
            Core::ECS::SystemPhase::FixedSimulation,
            &EM);
        EconomyGroup->AddSubSystemInstance(std::make_unique<Game::Systems::HarvestSystem>(SM), false);
        EconomyGroup->AddSubSystemInstance(std::make_unique<Game::Systems::BuildingSystem>(SM), true);
        EconomyGroup->AddSubSystemInstance(std::make_unique<Game::Systems::ProductionSystem>(SM), true);
        EconomyGroup->AddSubSystemInstance(std::make_unique<Game::Systems::UpgradeSystem>(SM), false);
        EconomyGroup->AddSubSystemInstance(std::make_unique<Game::Systems::ZergSystem>(SM, mMap), false);
        EconomyGroup->AddSubSystemInstance(std::make_unique<Game::Systems::ProtossSystem>(SM, mMap), false);
        mEconomyGroup = EconomyGroup.get();
        SM.RegisterSystemInPhase(Core::ECS::SystemPhase::FixedSimulation, std::move(EconomyGroup));

        auto CombatGroup = std::make_unique<Core::ECS::SystemGroup>(
            "LocomotionAndCombatGroup",
            Core::ECS::SystemPhase::FixedSimulation,
            &EM);
        CombatGroup->AddSubSystemInstance(std::make_unique<Game::Systems::MovementSystem>(SM, mMap), false);
        CombatGroup->AddSubSystemInstance(std::make_unique<Game::Systems::TransportSystem>(SM), true);
        auto BulletSys = std::make_unique<Game::Systems::BulletSystem>(SM);
        auto* BulletSysRaw = BulletSys.get();
        CombatGroup->AddSubSystemInstance(std::move(BulletSys), false);
        CombatGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::CombatSystem>(SM, *BulletSysRaw, mMap), true);
        CombatGroup->AddSubSystemInstance(std::make_unique<Game::Systems::SpellSystem>(SM), true);
        CombatGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::NukeAndSpiderMineSystem>(SM, *BulletSysRaw), false);
        mCombatGroup = CombatGroup.get();
        SM.RegisterSystemInPhase(Core::ECS::SystemPhase::FixedSimulation, std::move(CombatGroup));

        // 4. Presentation & RenderExtract Systems
        auto PresentationGroup = std::make_unique<Core::ECS::SystemGroup>(
            "PresentationAndAudioGroup",
            Core::ECS::SystemPhase::Presentation,
            &EM);
        PresentationGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::AnimationSystem>(SM, nullptr, BulletSysRaw), false);
        PresentationGroup->AddSubSystemInstance(
            std::make_unique<Game::Systems::DeclarativeAnimationPlayerSystem>(SM, mActiveRegistry), false);
        auto AudioRouter = std::make_unique<Core::Audio::ModernAudioEventRouterSystem>(SM, mActiveRegistry, nullptr);
        mAudioRouter = AudioRouter.get();
        PresentationGroup->AddSubSystemInstance(std::move(AudioRouter), false);
        SM.RegisterSystemInPhase(Core::ECS::SystemPhase::Presentation, std::move(PresentationGroup));

        auto RenderPipeline = std::make_unique<Render::ModernRenderPipelineSystem>(SM, mActiveRegistry);
        mRenderPipeline = RenderPipeline.get();
        SM.RegisterSystemInPhase(Core::ECS::SystemPhase::RenderExtract, std::move(RenderPipeline));
    }

    void OnShutdown(Engine::EngineApp& /*App*/) override
    {
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
    std::unique_ptr<Game::Map::GameMap> mOwnedFallbackMap;
    Game::Map::GameMap* mMap = nullptr;

    Core::ECS::SystemGroup* mSpatialGroup = nullptr;
    Core::ECS::SystemGroup* mEconomyGroup = nullptr;
    Core::ECS::SystemGroup* mCombatGroup = nullptr;
    Core::Audio::ModernAudioEventRouterSystem* mAudioRouter = nullptr;
    Render::ModernRenderPipelineSystem* mRenderPipeline = nullptr;
};

} // namespace Game
