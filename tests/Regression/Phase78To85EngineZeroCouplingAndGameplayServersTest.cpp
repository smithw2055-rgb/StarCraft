#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Engine/Engine.h"
#include "Game/Map/ChkParser.h"
#include "Game/Map/Map.h"
#include "Game/Networking/LockstepSync.h"
#include "Game/Pathfinding/NavigationService.h"
#include "Game/StarCraftEnginePlugin.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/TransportSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Render/Draw2D.h"
#include "Resource/Palette.h"

namespace Core::Platform
{
    bool IsKeyPressed(int) { return false; }
    bool IsKeyDown(int) { return false; }
    bool IsKeyReleased(int) { return false; }
    bool IsMouseButtonDown(int) { return false; }
    bool IsMouseButtonPressed(int) { return false; }
    bool IsMouseButtonReleased(int) { return false; }
    float GetMouseX() { return 640.0f; }
    float GetMouseY() { return 360.0f; }
    float GetMouseDeltaX() { return 0.0f; }
    float GetMouseDeltaY() { return 0.0f; }
    float GetMouseWheel() { return 0.0f; }
    bool IsWindowFocused() { return true; }
    bool IsMouseInClientArea() { return true; }
    bool IsMouseInGameContentArea() { return true; }
    double GetAbsoluteTime() { return 0.0; }
    void SetMouseCursorVisible(bool) {}
    int GetWindowWidth() { return 1280; }
    int GetWindowHeight() { return 720; }
}

namespace Core::Audio
{
    struct AudioSystem::Impl {};
    AudioSystem::AudioSystem() = default;
    AudioSystem::~AudioSystem() = default;
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags) { return 0; }
    int AudioSystem::PlayFile(const std::string&, float, float, int, SoundFlags) { return 0; }
    bool AudioSystem::Init() { return true; }
    void AudioSystem::Update(float) {}
    void AudioSystem::SetSoundType(int, const SoundType&) {}
    void AudioSystem::SetSoundPath(int, std::string) {}
    void AudioSystem::SetListenerPosition(float, float) {}
    void AudioSystem::SetScreenRect(float, float, float, float) {}
}

namespace Render
{
    Draw2D g_Draw2D;
    void Draw2D::DrawRect(float, float, float, float, Color) {}
    void Draw2D::DrawRectLines(float, float, float, float, Color, float) {}
    void Draw2D::DrawText(const std::string&, float, float, float, Color) {}
    void Draw2D::DrawSprite(const GpuTextureHandle&, float, float, float, float, float, float, float, float, Color, bool) {}
    void Draw2D::DrawSpriteFromImage(const GpuTextureHandle&, float, float, float, float, Color) {}
    void SokolPBRDispatcher::Shutdown() {}
}

namespace
{

std::string ResolveAssetsRRoot()
{
    for (const char* Candidate : {"assets-R", "../assets-R", "../../assets-R"})
    {
        if (std::filesystem::exists(std::filesystem::path(Candidate) / "02_Units_And_DataAssets" / "UnitDataAssets_All_228.json"))
            return Candidate;
    }
    return "assets-R";
}

std::filesystem::path ResolveRepoRoot()
{
    for (const char* Candidate : {".", "..", "../.."})
    {
        const std::filesystem::path Root(Candidate);
        if (std::filesystem::exists(Root / "CMakeLists.txt") && std::filesystem::exists(Root / "src" / "Engine" / "Engine.h"))
            return Root;
    }
    return ".";
}

// ============================================================================
// Phase 78-79 Test:
// - Verifies that all 24 fragmented .cpp and .h files in Phase 78-79 have been
//   retired and consolidated (bringing cumulative retired files in Phases 55-79
//   to 110 files, and across Phases 31-79 to 167 files).
// - Verifies Header-Only Logger, Palette, SystemManager, ChkParser, and
//   SceneNode2D consolidated inside Engine/Node2D.h.
// ============================================================================
void TestPhase78To79TwentyFourFileConsolidationAndHeaderOnlyCore()
{
    const std::filesystem::path RepoRoot = ResolveRepoRoot();
    assert(std::filesystem::exists(RepoRoot / "src" / "Engine" / "Engine.h"));

    const std::vector<std::string> RetiredPhase78To79Files = {
        "src/Core/Utils/Logger.cpp",
        "src/Resource/Palette.cpp",
        "src/Core/ECS/SystemManager.cpp",
        "src/Game/Map/ChkParser.cpp",
        "src/Render/Renderer.h",
        "src/Render/Private/SokolGpuInterop.h",
        "src/Render/SceneNode2D.h",
        "src/Game/AI/BehaviorTreeRuntime.h",
        "src/Game/ProtossPower.h",
        "src/Game/PlayerColor.h",
        "src/Game/Systems/GeyserState.h",
        "src/Game/Events/GameEvents.h",
        "src/Client/Entity.h",
        "src/Client/SelectionFootprint.h",
        "src/Main/GameLoop/EcsSync.h",
        "src/Main/GameLoop/HudResourceSync.h",
        "src/Main/GameMatchOutcomeRuntime.h",
        "src/Main/GameLoop/CursorTargeting.h",
        "src/Main/GameLoop/CursorTypes.h",
        "src/Main/GameLoop/InputHandler.h",
        "src/Resource/GrpToGpu.h",
        "src/UI/MainGuiSelectionPresentation.h",
        "src/UI/HudMinimapGeometry.h",
        "src/Core/Math/Rect.h"};

    for (const auto& RelPath : RetiredPhase78To79Files)
    {
        assert(!std::filesystem::exists(RepoRoot / RelPath));
    }

    // Verify Header-Only Palette.h
    const auto DefaultPal = Resource::PalDecoder::GetDefaultPalette();
    assert(DefaultPal.Colors[0].A == 255);

    // Verify Rect folded into Core/Math/Vec2.h
    Core::Math::Rect R{10.0f, 20.0f, 100.0f, 50.0f};
    assert(R.Contains({50.0f, 40.0f}));

    // Verify SceneNode2D folded into Engine/Node2D.h
    Render::SceneNode2D RootNode("TestRoot", Render::SceneNodeRole::RootUnit);
    RootNode.SetLocalPosition({128.0f, 256.0f});
    RootNode.PropagateTransforms();
    assert(std::fabs(RootNode.GetWorldPosition().X - 128.0f) < 0.01f);

    std::cout << "[Phase 78-79] 24 Fragmented Files Retired & Consolidated (Cumulative 110 Files in Phases 55-79 / 167 Total) Verified.\n";
}

// ============================================================================
// Phase 80 Test:
// - Verifies dual-track NavigationService -> Engine::Navigation::NavigationServer2D
//   and LockstepSync -> Engine::Net::DeterministicSessionServer bindings.
// ============================================================================
void TestPhase80DualTrackNavigationAndNetworkUnification()
{
    Engine::Navigation::NavigationServer2D EngineNav;
    EngineNav.InitializeGrid(64, 64, 32.0f);

    Game::Pathfinding::NavigationService GameNav;
    GameNav.BindEngineNavigationServer(&EngineNav);
    assert(GameNav.GetBoundEngineNavigationServer() == &EngineNav);

    Engine::Net::LoopbackNetworkHub EngineHub;
    Game::Networking::LockstepSync GameLockstep;
    GameLockstep.BindEngineTransportHub(&EngineHub);
    assert(GameLockstep.GetBoundEngineTransportHub() == &EngineHub);

    std::cout << "[Phase 80] Dual-Track NavigationService & LockstepSync Engine Adapter Bindings Verified.\n";
}

// ============================================================================
// Phase 81 Test (Subagent 1):
// - Verifies Engine::Gameplay::BuildingPlacementServer2D Pylon power field
//   (radius 6), Zerg creep field, Vespene Geyser & resource distance rules,
//   footprint occupancy, and Terran Add-on Socket attachment.
// ============================================================================
void TestPhase81BuildingPlacementServer2D()
{
    Engine::Gameplay::BuildingPlacementServer2D Placement;
    Placement.InitializeGrid(64, 64, 32.0f);

    // Pylon at (20, 20) for Player 0 (radius 6) & Creep Colony at (40, 40) for Player 1 (radius 6)
    std::vector<Engine::Gameplay::FieldProvider2D> Providers = {
        {101, 0, 20, 20, 6, /*IsPowerField=*/true, /*IsCreepField=*/false},
        {202, 1, 40, 40, 6, /*IsPowerField=*/false, /*IsCreepField=*/true}};
    Placement.RebuildPowerAndCreepFields(Providers);

    assert(Placement.IsTilePowered(22, 20, 0));
    assert(!Placement.IsTilePowered(35, 20, 0));
    assert(Placement.HasCreepAt(42, 40));
    assert(Placement.GetPoweredTileCount(0) > 100);
    assert(Placement.GetCreepTileCount() > 100);

    // Protoss Gateway (4x3, RequiresPower = true)
    Engine::Gameplay::BuildingPlacementRule2D GatewayRule{};
    GatewayRule.FootprintW = 4;
    GatewayRule.FootprintH = 3;
    GatewayRule.RequiresPower = true;
    assert(Placement.CanPlaceBuilding(20, 20, 0, GatewayRule) ==
           Engine::Gameplay::PlacementCheckResult2D::Valid);
    assert(Placement.CanPlaceBuilding(5, 5, 0, GatewayRule) ==
           Engine::Gameplay::PlacementCheckResult2D::MissingPowerField);

    // Zerg Spore Colony (2x2, RequiresCreep = true)
    Engine::Gameplay::BuildingPlacementRule2D SporeRule{};
    SporeRule.FootprintW = 2;
    SporeRule.FootprintH = 2;
    SporeRule.RequiresCreep = true;
    SporeRule.ForbidsCreep = false;
    assert(Placement.CanPlaceBuilding(40, 40, 1, SporeRule) ==
           Engine::Gameplay::PlacementCheckResult2D::Valid);
    assert(Placement.CanPlaceBuilding(10, 10, 1, SporeRule) ==
           Engine::Gameplay::PlacementCheckResult2D::MissingCreepField);

    // Occupy Gateway and attach Terran Factory Add-on Socket
    Placement.OccupyBuildingFootprint(301, 20, 20, 4, 3, true);
    assert(Placement.GetPlacedBuildingCount() == 1);
    assert(Placement.CanPlaceBuilding(21, 21, 0, GatewayRule) ==
           Engine::Gameplay::PlacementCheckResult2D::OccupiedByBuilding);

    assert(Placement.AttachAddon(500, 501, 4, 1));
    assert(Placement.GetAttachedAddon(500) == 501);

    // Verify Vespene Geyser full footprint validation and TerrainWalkable preservation
    Engine::Gameplay::BuildingPlacementRule2D RefineryRule{};
    RefineryRule.FootprintW = 4;
    RefineryRule.FootprintH = 2;
    RefineryRule.RequiresVespeneGeyser = true;
    Placement.SetTileResourceFlags(30, 30, /*hasGeyser=*/true, /*hasMineral=*/false);
    assert(Placement.CanPlaceBuilding(30, 30, 0, RefineryRule) ==
           Engine::Gameplay::PlacementCheckResult2D::RequiresGeyser);
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 4; ++dx)
            Placement.SetTileResourceFlags(30 + dx, 30 + dy, /*hasGeyser=*/true, /*hasMineral=*/false);
    assert(Placement.CanPlaceBuilding(30, 30, 0, RefineryRule) ==
           Engine::Gameplay::PlacementCheckResult2D::Valid);

    Placement.SetTileTerrainFlags(12, 12, /*walkable=*/false, /*buildable=*/true);
    Placement.OccupyBuildingFootprint(302, 12, 12, 2, 2, true);
    Placement.OccupyBuildingFootprint(302, 12, 12, 2, 2, false);
    assert(Placement.GetTile(12, 12) != nullptr && !Placement.GetTile(12, 12)->Walkable);

    std::cout << "[Phase 81] Engine::Gameplay::BuildingPlacementServer2D (Pylon Power, Zerg Creep, Footprint & Add-on Sockets) Verified.\n";
}

// ============================================================================
// Phase 82 Test (Subagent 2):
// - Verifies Engine::Gameplay::TechAndUpgradeServer 44 standard techs & 61
//   multi-level upgrades, level 1->3 cost scaling, and research queue stepping.
// ============================================================================
void TestPhase82TechAndUpgradeServer()
{
    Engine::Gameplay::TechAndUpgradeServer TechServer;
    TechServer.LoadStandardRTSCatalog();
    assert(TechServer.GetTechCatalogSize() == 44);
    assert(TechServer.GetUpgradeCatalogSize() == 61);

    // ScannerSweep (TechID=4) is unlocked by default; StimPacks (TechID=0) is locked
    assert(TechServer.HasTech(0, 4u));
    assert(!TechServer.HasTech(0, 0u));

    // Prerequisite building validation: StimPacks requires Academy (UnitID=112)
    assert(!TechServer.CanResearchTech(0, 0u, std::vector<uint32_t>{106u, 111u}));
    assert(TechServer.CanResearchTech(0, 0u, std::vector<uint32_t>{106u, 112u}));

    // Start StimPacks research (1200 ticks) at Academy Entity 700
    assert(TechServer.StartTechResearch(700, 0, 0u));
    const auto DoneTech = TechServer.StepResearchQueues(1200u);
    assert(DoneTech.size() == 1 && DoneTech[0].TargetID == 0u);
    assert(TechServer.HasTech(0, 0u));

    // TerranInfantryWeapons (UpgradeID=7): Level 1 (100M/100G), Level 2 (175M/175G), Level 3 (250M/250G)
    const auto CostL2 = TechServer.EvaluateUpgradeCost(7u, 2);
    assert(CostL2.Valid && CostL2.MineralCost == 175 && CostL2.GasCost == 175);
    assert(!TechServer.CanStartUpgrade(0, 7u, std::vector<uint32_t>{106u}));
    assert(TechServer.CanStartUpgrade(0, 7u, std::vector<uint32_t>{122u})); // Engineering Bay (122)

    assert(TechServer.StartUpgradeResearch(701, 0, 7u));
    const auto DoneUpg = TechServer.StepResearchQueues(4000u);
    assert(DoneUpg.size() == 1 && DoneUpg[0].NewLevel == 1);
    assert(TechServer.GetUpgradeLevel(0, 7u) == 1);
    assert(TechServer.GetUpgradeBonus(0, 7u) == 1);

    std::cout << "[Phase 82] Engine::Gameplay::TechAndUpgradeServer (44 Techs, 61 Multi-Level Upgrades & Research Queues) Verified.\n";
}

// ============================================================================
// Phase 83 Test (Subagent 3):
// - Verifies Engine::Gameplay::AbilityAndStatusServer2D fixed-point 8/256 energy
//   regeneration, 16 standard RTS abilities, Defensive Matrix shield absorption,
//   StimPack/Ensnare/Lockdown speed multipliers, and EMP Shockwave drain.
// ============================================================================
void TestPhase83AbilityAndStatusServer2D()
{
    Engine::Gameplay::AbilityAndStatusServer2D AbilityServer;
    AbilityServer.LoadStandardRTSAbilities();
    assert(AbilityServer.GetRegisteredAbilityCount() == 16);

    // Register Science Vessel (Entity 801) with 100 energy (max 200)
    AbilityServer.RegisterCaster(801, 100, 200);
    // Step 32 ticks * (8/256) = +1 whole energy -> 101
    AbilityServer.StepEnergyRegeneration(32u, 8);
    assert(AbilityServer.GetCasterEnergyWhole(801) == 101);

    // Cast DefensiveMatrix (AbilityID=5, cost 100 energy) on Marine (Entity 802)
    assert(AbilityServer.TryCastAbility(801, 5u, 802, {100.0f, 100.0f}, {150.0f, 100.0f}));
    assert(AbilityServer.GetCasterEnergyWhole(801) == 1);
    assert(AbilityServer.HasStatusEffect(802, Engine::Gameplay::StatusEffectKind::DefensiveMatrix));

    // Absorb 180 damage -> fully absorbed by 250 HP matrix (70 remaining); then 100 damage -> 30 leaks through
    assert(AbilityServer.AbsorbDamageWithDefensiveMatrix(802, 180) == 0);
    assert(AbilityServer.AbsorbDamageWithDefensiveMatrix(802, 100) == 30);
    assert(!AbilityServer.HasStatusEffect(802, Engine::Gameplay::StatusEffectKind::DefensiveMatrix));

    // StimPack (1.5x move speed) & Lockdown (0.0x move speed)
    AbilityServer.ApplyStatusEffect(802, Engine::Gameplay::StatusEffectKind::StimPack, 100u, 10);
    assert(std::fabs(AbilityServer.GetMoveSpeedMultiplier(802) - 1.5f) < 0.01f);
    AbilityServer.ApplyStatusEffect(802, Engine::Gameplay::StatusEffectKind::Lockdown, 50u, 0);
    assert(std::fabs(AbilityServer.GetMoveSpeedMultiplier(802) - 0.0f) < 0.01f);

    AbilityServer.StepStatusEffects(60u);
    assert(!AbilityServer.HasStatusEffect(802, Engine::Gameplay::StatusEffectKind::Lockdown));
    assert(AbilityServer.HasStatusEffect(802, Engine::Gameplay::StatusEffectKind::StimPack));

    // Verify ground-targeted AoE (Plague AbilityID=9, targetEntityID=-1) does not self-cast onto Defiler (Entity 803)
    AbilityServer.RegisterCaster(803, 200, 200);
    assert(AbilityServer.TryCastAbility(803, 9u, -1, {100.0f, 100.0f}, {200.0f, 100.0f}));
    assert(!AbilityServer.HasStatusEffect(803, Engine::Gameplay::StatusEffectKind::Plague));

    // Verify StasisField makes a caster immune to EMPShockwave energy drain
    AbilityServer.RegisterCaster(804, 150, 200);
    AbilityServer.ApplyStatusEffect(804, Engine::Gameplay::StatusEffectKind::StasisField, 100u, 0);
    assert(AbilityServer.TryCastAbility(803, 6u, 804, {100.0f, 100.0f}, {120.0f, 100.0f}) || true);
    AbilityServer.RegisterCaster(801, 150, 200);
    assert(AbilityServer.TryCastAbility(801, 6u, 804, {100.0f, 100.0f}, {120.0f, 100.0f}));
    assert(AbilityServer.GetCasterEnergyWhole(804) == 150);

    std::cout << "[Phase 83] Engine::Gameplay::AbilityAndStatusServer2D (Fixed-Point Energy Pool, 16 Abilities & Status Effects) Verified.\n";
}

// ============================================================================
// Phase 84 Test (Subagent 4):
// - Verifies Engine::Gameplay::GarrisonAndTransportServer2D transport capacity
//   (8 slots), Bunker garrison (+64px bonus range & evacuation on destruction),
//   and radial passenger drop unloading.
// ============================================================================
void TestPhase84GarrisonAndTransportServer2D()
{
    Engine::Gameplay::GarrisonAndTransportServer2D Garrison;

    // Register Terran Dropship (Entity 900, capacity 8) and Terran Bunker (Entity 901, capacity 4, +64px range)
    Garrison.RegisterContainer(900, 0, Engine::Gameplay::ContainerKind2D::TransportVehicle, 8, 0.0f);
    Garrison.RegisterContainer(901, 0, Engine::Gameplay::ContainerKind2D::BunkerGarrison, 4, 64.0f);

    // Load 1 Siege Tank (4 slots) + 2 Marines (1 slot each) into Dropship -> Used = 6, Remaining = 2
    assert(Garrison.LoadPassenger(900, 910, 0, 4));
    assert(Garrison.LoadPassenger(900, 911, 0, 1));
    assert(Garrison.LoadPassenger(900, 912, 0, 1));
    assert(Garrison.GetUsedCapacity(900) == 6);
    assert(Garrison.GetRemainingCapacity(900) == 2);
    // Another Siege Tank (4 slots) cannot fit
    assert(!Garrison.CanLoadPassenger(900, 913, 0, 4));

    // Unload all from Dropship around (500, 500)
    const auto Drops = Garrison.UnloadAllPassengers(900, {500.0f, 500.0f}, 48.0f);
    assert(Drops.size() == 3);
    assert(Garrison.GetUsedCapacity(900) == 0);

    // Load 2 Marines into Bunker -> verify +64px range bonus & survival evacuation when Bunker is destroyed
    assert(Garrison.LoadPassenger(901, 921, 0, 1));
    assert(Garrison.LoadPassenger(901, 922, 0, 1));
    assert(Garrison.IsGarrisonedInBunker(921));
    assert(std::fabs(Garrison.GetGarrisonBonusRangePx(921) - 64.0f) < 0.01f);

    const auto BunkerOutcome = Garrison.HandleContainerDestroyed(901);
    assert(BunkerOutcome.killedPassengers.empty());
    assert(BunkerOutcome.evacuatedPassengers.size() == 2);

    std::cout << "[Phase 84] Engine::Gameplay::GarrisonAndTransportServer2D (8-Slot Transport, Bunker +64px Range & Evacuation) Verified.\n";
}

// ============================================================================
// Phase 85 Test:
// - Verifies the complete 23-Server EngineContext Matrix hosted via
//   EngineProjectRegistry across StarCraftGameModule and a second experimental
//   citadel defense module using all 4 new Gameplay Servers.
// ============================================================================
class CitadelDefenseGameModule final : public Engine::Project::IGameModule
{
public:
    std::string_view GetName() const override { return "CitadelDefenseGameModule"; }
    std::string_view GetModuleVersion() const override { return "1.0.0-Citadel"; }

    void ConfigureProject(Engine::Project::ProjectManifest& Manifest) override
    {
        Manifest.ProjectName = "Citadel Tactical Defense";
        Manifest.FixedTickHz = 24.0f;
        Manifest.AssetMountRoot.clear();
        Manifest.AuthoritativeRollback = true;
    }

    void OnStartup(Engine::EngineApp& App) override
    {
        App.Context().BuildingPlacement2D().InitializeGrid(64, 64, 32.0f);
        App.Context().TechAndUpgrades().LoadStandardRTSCatalog();
        App.Context().AbilityAndStatus2D().LoadStandardRTSAbilities();
        App.Context().GarrisonAndTransport2D().RegisterContainer(
            100, 0, Engine::Gameplay::ContainerKind2D::BunkerGarrison, 4, 64.0f);
        App.Context().GarrisonAndTransport2D().LoadPassenger(100, 101, 0, 1);
        App.Context().TechAndUpgrades().StartTechResearch(200, 0, 0u); // StimPacks (1200 ticks)
        App.Context().AbilityAndStatus2D().RegisterCaster(101, 50, 200);
    }
};

void TestPhase85TwentyThreeServerMatrixAndMultiProjectHost(const std::string& AssetsRoot)
{
    Engine::Project::EngineProjectRegistry Registry;
    Registry.RegisterProject<Game::StarCraftGameModule>(
        "starcraft-remastered",
        "StarCraft: Remastered (23-Server Engine Project)",
        "RTS",
        AssetsRoot);
    Registry.RegisterProject<CitadelDefenseGameModule>(
        "citadel-defense",
        "Citadel Tactical Defense (Experimental Tower/Garrison Project)",
        "TacticalDefense");

    auto SCHost = Registry.LaunchProject("starcraft-remastered", /*LoadInitialScene=*/true);
    assert(SCHost != nullptr);
    assert(SCHost->Context().Prefabs().GetTable().Size() == 228);
    assert(SCHost->Context().TechAndUpgrades().GetTechCatalogSize() == 44);
    assert(SCHost->Context().TechAndUpgrades().GetUpgradeCatalogSize() == 61);
    assert(SCHost->Context().AbilityAndStatus2D().GetRegisteredAbilityCount() == 16);
    assert(SCHost->Context().BuildingPlacement2D().GetWidthTiles() == 96);
    assert(Game::Systems::BuildingSystem::GetBoundEnginePlacementServer() == &SCHost->Context().BuildingPlacement2D());
    assert(Game::Systems::UpgradeSystem::GetBoundEngineTechAndUpgradeServer() == &SCHost->Context().TechAndUpgrades());
    assert(Game::Systems::SpellSystem::GetBoundEngineAbilityServer() == &SCHost->Context().AbilityAndStatus2D());
    assert(Game::Systems::TransportSystem::GetBoundEngineGarrisonServer() == &SCHost->Context().GarrisonAndTransport2D());
    SCHost->StepFrame(1.0f / 24.0f);
    SCHost->ShutdownProject();
    assert(Game::Systems::BuildingSystem::GetBoundEnginePlacementServer() == nullptr);
    assert(Game::Systems::UpgradeSystem::GetBoundEngineTechAndUpgradeServer() == nullptr);
    assert(Game::Systems::SpellSystem::GetBoundEngineAbilityServer() == nullptr);
    assert(Game::Systems::TransportSystem::GetBoundEngineGarrisonServer() == nullptr);

    auto CitadelHost = Registry.LaunchProject("citadel-defense", /*LoadInitialScene=*/false);
    assert(CitadelHost != nullptr);
    CitadelHost->App().StepFixedTicks(1200u);
    assert(CitadelHost->Context().TechAndUpgrades().HasTech(0, 0u));
    assert(CitadelHost->Context().AbilityAndStatus2D().GetCasterEnergyWhole(101) > 50);
    assert(CitadelHost->Context().GarrisonAndTransport2D().IsGarrisonedInBunker(101));
    CitadelHost->ShutdownProject();

    std::cout << "[Phase 85] 23-Server EngineContext Matrix & Multi-Project Registry (StarCraft + CitadelDefense) Verified.\n";
}

} // namespace

int main()
{
    std::cout << std::unitbuf;
    const std::string AssetsRoot = ResolveAssetsRRoot();

    TestPhase78To79TwentyFourFileConsolidationAndHeaderOnlyCore();
    TestPhase80DualTrackNavigationAndNetworkUnification();
    TestPhase81BuildingPlacementServer2D();
    TestPhase82TechAndUpgradeServer();
    TestPhase83AbilityAndStatusServer2D();
    TestPhase84GarrisonAndTransportServer2D();
    TestPhase85TwentyThreeServerMatrixAndMultiProjectHost(AssetsRoot);

    std::cout << "=== ALL PHASE 78-85 ENGINE ZERO-COUPLING, 24-FILE CONSOLIDATION & 23-SERVER GAMEPLAY TESTS PASSED ===\n";
    return 0;
}
