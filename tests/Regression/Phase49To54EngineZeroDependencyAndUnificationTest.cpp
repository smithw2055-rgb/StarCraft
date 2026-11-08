#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "Core/Audio/ModernAudioEventRouter.h"
#include "Core/Color.h"
#include "Core/ECS/SoAComponentStream.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Engine/Engine.h"
#include "Game/AI/AIBrain.h"
#include "Game/Components.h"
#include "Game/Pathfinding/NavigationWorldResource.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Races/RaceDef.h"
#include "Game/Races/UnitPrototypeRegistry.h"
#include "Game/StarCraftEnginePlugin.h"
#include "Game/Systems/DeclarativeAnimationPlayer.h"
#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"
#include "Render/ModernRenderPipeline.h"
#include "Resource/ModernAssetRegistry.h"
#include "Resource/Palette.h"
#include "Resource/RawColor.h"
#include "Resource/UIAssetCacheView.h"


namespace Core::Platform
{
    bool IsKeyPressed(int) { return false; }
    bool IsKeyDown(int) { return false; }
    bool IsKeyReleased(int) { return false; }
    int GetWindowWidth() { return 1280; }
    int GetWindowHeight() { return 720; }
}

namespace Core::Audio
{
    struct AudioSystem::Impl {};
    AudioSystem::AudioSystem() = default;
    AudioSystem::~AudioSystem() = default;
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags) { return 0; }
    void AudioSystem::Update(float) {}
    void AudioSystem::SetSoundType(int, const SoundType&) {}
    void AudioSystem::SetSoundPath(int, const std::string) {}
}

namespace Render
{
    void SokolPBRDispatcher::Shutdown() {}
}

namespace
{

void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::cerr << "[FAIL] " << Message << "\n";
        std::exit(1);
    }
}

std::string ResolveRepoRoot()
{
    namespace fs = std::filesystem;
    for (const char* Candidate : {".", "..", "../.."})
    {
        if (fs::exists(fs::path(Candidate) / "src" / "Engine" / "Engine.h"))
            return Candidate;
    }
    return ".";
}

std::string ResolveAssetsRRoot()
{
    namespace fs = std::filesystem;
    for (const char* Candidate : {
             "assets-R",
             "../assets-R",
             "b/assets-R",
             "../../assets-R"})
    {
        if (fs::exists(Candidate))
            return Candidate;
    }
    return "assets-R";
}

// ============================================================================
// Phase 49 Verification: Zero Upward #include Dependencies in Core/Engine/Render/Resource
// ============================================================================
void TestPhase49ZeroDependencyEngineFoundation(const std::string& RepoRoot)
{
    namespace fs = std::filesystem;

    auto HasUpwardGameInclude = [](const fs::path& FilePath) -> bool {
        std::ifstream In(FilePath);
        std::string Line;
        while (std::getline(In, Line))
        {
            const auto FirstNonSpace = Line.find_first_not_of(" \t");
            if (FirstNonSpace == std::string::npos || Line.compare(FirstNonSpace, 8, "#include") != 0)
                continue;
            if (Line.find("#include \"Game/") != std::string::npos ||
                Line.find("#include \"Main/") != std::string::npos ||
                Line.find("#include \"Client/") != std::string::npos ||
                Line.find("#include \"UI/") != std::string::npos)
            {
                return true;
            }
        }
        return false;
    };

    size_t CheckedFiles = 0;
    for (const char* LayerDir : {"src/Core", "src/Engine", "src/Render"})
    {
        const fs::path DirPath = fs::path(RepoRoot) / LayerDir;
        Require(fs::exists(DirPath), "Phase 49: Engine layer directory must exist");
        for (const auto& Entry : fs::recursive_directory_iterator(DirPath))
        {
            if (!Entry.is_regular_file())
                continue;
            const auto Ext = Entry.path().extension().string();
            if (Ext != ".h" && Ext != ".cpp")
                continue;
            ++CheckedFiles;
            Require(!HasUpwardGameInclude(Entry.path()),
                    ("Phase 49: Found upward Game/Main/Client/UI include in " + Entry.path().string()).c_str());
        }
    }

    for (const auto& Entry : fs::recursive_directory_iterator(fs::path(RepoRoot) / "src" / "Resource"))
    {
        if (!Entry.is_regular_file() || Entry.path().extension() != ".h")
            continue;
        ++CheckedFiles;
        Require(!HasUpwardGameInclude(Entry.path()),
                ("Phase 49: Found upward Game/Main/Client/UI include in Resource header " + Entry.path().string()).c_str());
    }
    Require(CheckedFiles >= 40, "Phase 49: Audited all Core, Engine, Render, and Resource headers");
    Require(!fs::exists(fs::path(RepoRoot) / "src/Game/ComponentBinarySerializers.h"),
            "Phase 49: ComponentBinarySerializers.h must be consolidated into Game/Components.h");

    // Verify EngineAdapterHooks end-to-end execution
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    auto& Upg = EM.Context().EmplaceResource<Core::ECS::WorldUpgradeResource>();
    Upg.PlayerUpgrades[0].Levels[static_cast<size_t>(Core::ECS::ResearchId::Terran_InfantryWeapons)] = 3;
    auto& Econ = EM.Context().EmplaceResource<Core::ECS::PlayerResourceStore>();
    Econ.Players[0].Minerals = 1250;

    const auto E1 = EM.CreateEntity();
    auto& T1 = EM.AddComponent<Game::Components::TransformComponent>(E1);
    T1.Position = {100.0f, 200.0f};
    auto& P1 = EM.AddComponent<Game::Components::PathfindingComponent>(E1);
    P1.CurrentVelocity = {24.0f, -12.0f};
    auto& U1 = EM.AddComponent<Game::Components::UnitComponent>(E1);
    U1.ChkUnitID = 0;
    U1.HitPoints = 40;
    U1.MaxHitPoints = 40;
    U1.Speed = 4.0f;
    auto& R1 = EM.AddComponent<Game::Components::RenderComponent>(E1);
    R1.ImageID = 214;
    R1.Visible = true;

    // 1. SoA Stream via EngineAdapterHooks
    Core::ECS::KinematicsSoAStream SoA;
    Require(SoA.ExtractFromECS(EM) == 1u, "Phase 49: KinematicsSoAStream extracts entity via EngineAdapterHooks");
    SoA.IntegrateBatch(1.0f);
    SoA.WriteBackToECS(EM);
    Require(T1.Position.X == 124.0f && T1.Position.Y == 188.0f && T1.HasPrevPosition,
            "Phase 49: KinematicsSoAStream writes integrated position back via EngineAdapterHooks");

    // 2. WorldSnapshotSerializer via EngineAdapterHooks
    const auto Snap = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(EM, 77u);
    T1.Position = {999.0f, 999.0f};
    Econ.Players[0].Minerals = 0;
    Require(Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(EM, 77u) != Snap.StateHashFNV1a,
            "Phase 49: WorldSnapshotSerializer hash detects component and economy mutations");
    Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(EM, Snap);
    Require(Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(EM, 77u) == Snap.StateHashFNV1a &&
                EM.GetResource<Core::ECS::PlayerResourceStore>()->Players[0].Minerals == 1250,
            "Phase 49: WorldSnapshotSerializer restores exact hash and resources");

    // 3. ModernRenderPipelineSystem via EngineAdapterHooks
    Render::ModernRenderPipelineSystem RenderSys(SM, nullptr);
    Require(RenderSys.ExtractFromECSWorld(EM) >= 1u,
            "Phase 49: ModernRenderPipelineSystem extracts render queue commands via EngineAdapterHooks");
}

// ============================================================================
// Phase 50 Verification: Unified Host Runtime (GameSession & Engine::EngineApp)
// ============================================================================
void TestPhase50UnifiedGameSessionAndEngineApp()
{
    Game::Client::GameLoop::GameSession Session;
    Core::Audio::AudioSystem Audio;

    Require(&Session.Systems() == &Session.EngineHost().PrimaryWorld(),
            "Phase 50: GameSession::Systems() is unified with EngineApp::PrimaryWorld()");
    Require(&Session.Entities() == &Session.EngineHost().Entities(),
            "Phase 50: GameSession::Entities() is unified with EngineApp::Entities()");

    Session.ConfigureSyntheticSkirmishMap();
    Session.MutableEcsMap().Generate(64, 64);
    Session.RebuildRegionsFromEcsMap();
    Require(Session.Navigation().GetWidth() == 64 &&
                Session.Navigation().GetHeight() == 64 &&
                Session.Navigation().GetConnectedRegionCount() >= 1u,
            "Phase 50: GameSession::RebuildRegionsFromEcsMap populates EngineContext::Navigation2D()");

    Game::Client::GameLoop::GameSessionBootstrap::RegisterGameplaySystems(Session, Audio);
    Require(Session.EngineHost().IsInitialized(),
            "Phase 50: RegisterGameplaySystems initializes hosted EngineApp");

    Session.RollbackRecordingEnabled = true;
    Session.SimulationTick = 5;
    Session.UpdateSimulationStep(1.0f / 24.0f);
    Session.UpdatePresentationFrame(1.0f / 60.0f);

    Require(Session.EngineHost().RollbackBuffer().GetTotalRecorded() == 1u &&
                Session.EngineContext().Time().GetTickCount() >= 1u,
            "Phase 50: GameSession::UpdateSimulationStep advances EngineApp time and GenericRollbackBuffer");
}

// ============================================================================
// Phase 51 Verification: Legacy AngelScript Retirement & AnimationServer2D Player
// ============================================================================
void TestPhase51AngelScriptRetirementAndAnimationServerPlayer(
    const std::string& RepoRoot,
    const Resource::ModernAssetRegistry& Registry)
{
    namespace fs = std::filesystem;
    Require(!fs::exists(fs::path(RepoRoot) / "src/Game/IScriptAngelScriptExporter.h") &&
                !fs::exists(fs::path(RepoRoot) / "src/Game/IScriptAngelScriptExporter.cpp") &&
                !fs::exists(fs::path(RepoRoot) / "src/Game/IScriptToAngelScript.cpp"),
            "Phase 51: Legacy AngelScript exporter files are permanently retired");

    Engine::Animation::AnimationServer2D AnimServer;
    const size_t ImportedControllers = AnimServer.ImportFromModernRegistry(Registry);
    Require(ImportedControllers >= 398u,
            "Phase 51: AnimationServer2D imported all 398 declarative IScript controllers");

    // Verify DeclarativeAnimationPlayerSystem works purely against AnimationServer2D (Registry = nullptr)
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();
    Game::Systems::DeclarativeAnimationPlayerSystem AnimPlayer(SM, nullptr, &AnimServer);

    const auto Entity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Entity).Position = {64.0f, 64.0f};
    EM.AddComponent<Game::Components::RenderComponent>(Entity);
    auto& AnimComp = EM.AddComponent<Game::Systems::DeclarativeAnimatorComponent>(Entity);
    AnimComp.IScriptID = Registry.GetIScriptControllers().front().IScriptID;

    const std::string FirstClipName = Registry.GetIScriptControllers().front().Clips.begin()->first;
    Require(AnimPlayer.TransitionToState(Entity, FirstClipName, true),
            "Phase 51: DeclarativeAnimationPlayerSystem transitions clip directly from AnimationServer2D");
    AnimPlayer.StepEntityTicks(Entity, 4u);
}

// ============================================================================
// Phase 52 Verification: Pathfinding Convergence (NavigationWorldResource -> NavigationServer2D)
// ============================================================================
void TestPhase52PathfindingAndNavigationConvergence(const std::string& RepoRoot)
{
    namespace fs = std::filesystem;
    Require(!fs::exists(fs::path(RepoRoot) / "src/Game/Pathfinding/RegionPathfinder.h") &&
                !fs::exists(fs::path(RepoRoot) / "src/Game/Pathfinding/NavigationSpatialHash.h"),
            "Phase 52: RegionPathfinder.h and NavigationSpatialHash.h are permanently retired");

    Core::ECS::EntityManager EM;
    Game::Map::GameMap Map;
    Map.Generate(32, 32);
    // Split Map into two disconnected islands with a vertical wall at x = 16
    for (int y = 0; y < 32; ++y)
    {
        auto T = Map.GetTile(16, y);
        T.Walkable = false;
        Map.SetTile(16, y, T);
    }

    auto& NavRes = Game::Pathfinding::NavigationWorldResource::GetOrSync(EM, &Map);
    Require(NavRes.RegionsBuilt && NavRes.NavServer.GetConnectedRegionCount() >= 2u,
            "Phase 52: NavigationWorldResource builds connected regions via Engine::Navigation::NavigationServer2D");
    Require(NavRes.NavServer.AreCellsConnected({4, 4}, {10, 10}),
            "Phase 52: Cells on same side of wall are connected in NavigationServer2D");
    Require(!NavRes.NavServer.AreCellsConnected({4, 4}, {24, 24}),
            "Phase 52: Cells across full barrier wall are disconnected in NavigationServer2D");
}

// ============================================================================
// Phase 53 Verification: Data-Driven AI & Catalog Unification
// ============================================================================
void TestPhase53DataDrivenAIAndCatalogUnification(
    const std::string& AssetsRRoot,
    const Resource::ModernAssetRegistry& Registry)
{
    size_t VerifiedWeapons = 0;
    const size_t SyncedUnits = Game::Races::SyncAndVerifyWithModernAssets(Registry, &VerifiedWeapons);
    Require(SyncedUnits > 0 && VerifiedWeapons > 0,
            "Phase 53: RaceDef::SyncAndVerifyWithModernAssets synchronized units and weapons");

    const auto* MarineProto = Game::Races::UnitPrototypeRegistry::Instance().FindByChkUnitID(0);
    Require(MarineProto != nullptr && MarineProto->HitPoints == 40,
            "Phase 53: UnitPrototypeRegistry automatically reflects synced ModernAssetRegistry definitions");

    Engine::AI::BehaviorTreeServer BTServer;
    const size_t LoadedTrees = BTServer.LoadFromAssetsRDirectory(AssetsRRoot);
    Require(LoadedTrees >= 449u,
            "Phase 53: Engine::AI::BehaviorTreeServer loaded 449 AIScript + BWScript trees");

    Core::ECS::SystemManager SM;
    Game::Systems::AIBrain Brain(SM, 1, 0, false, nullptr);
    const std::string FirstScriptID = BTServer.GetCatalog().GetAIScriptTrees().begin()->first;
    Require(Brain.BindBehaviorTreeFromServer(BTServer, FirstScriptID) && Brain.HasBoundBehaviorTree(),
            "Phase 53: AIBrain binds behavior tree directly from Engine::AI::BehaviorTreeServer");

    Engine::AI::Blackboard Board;
    Require(Brain.TickServerBehaviorTree(BTServer, FirstScriptID, Board) == Engine::AI::BTStatus::Success &&
                Board.GetInt("ExecutedNodeCount", 0) > 0 &&
                Board.GetInt("AIPlayerID", -1) == 1,
            "Phase 53: AIBrain ticks Engine::AI::BehaviorTreeServer with populated Blackboard");
}

// ============================================================================
// Phase 54 Verification: Resource, Color, Palette & View Type Deduplication
// ============================================================================
void TestPhase54ResourceColorAndPaletteDeduplication(const std::string& RepoRoot)
{
    namespace fs = std::filesystem;
    for (const char* RemovedHeader : {
             "src/Render/Color.h",
             "src/Resource/RgbaImage.h",
             "src/Resource/PalDecoder.h",
             "src/Resource/PalDecoder.cpp",
             "src/Resource/GpuImage.h",
             "src/Resource/ArchiveAssetSourceView.h",
             "src/Resource/DataAssetCacheView.h",
             "src/Resource/SpriteAssetCacheView.h"})
    {
        Require(!fs::exists(fs::path(RepoRoot) / RemovedHeader),
                ("Phase 54: Redundant file must be removed: " + std::string(RemovedHeader)).c_str());
    }

    const Render::Color C = Render::Colors::Cyan();
    Require(C == Core::Colors::Cyan(), "Phase 54: Render::Color and Core::Color are unified in Core/Color.h");

    std::vector<uint8_t> WpeBytes(1024, 128);
    auto RawPalOpt = Resource::PalDecoder::DecodeWpe(WpeBytes);
    auto GfxPalOpt = Resource::Graphics::DecodeWpe(WpeBytes);
    Require(RawPalOpt.has_value() && GfxPalOpt.has_value() &&
                RawPalOpt->Colors[10].R == 128 && GfxPalOpt->Colors[10].G == 128,
            "Phase 54: Unified Palette.h/.cpp provides both Resource::PalDecoder and Resource::Graphics::DecodeWpe");

    Resource::Graphics::ArchiveAssetSourceView ArchiveView(nullptr);
    Resource::Graphics::DataAssetCacheView DataView(nullptr);
    Resource::Graphics::SpriteAssetCacheView SpriteView(nullptr);
    Resource::Graphics::UIAssetCacheView UIView(nullptr);
    Require(!ArchiveView.HasCache() && !DataView.HasCache() && !SpriteView.HasCache() && !UIView.HasCache(),
            "Phase 54: Consolidated UIAssetCacheView.h provides all 4 cache view classes");
}

} // namespace

int main()
{
    const std::string RepoRoot = ResolveRepoRoot();
    const std::string AssetsRRoot = ResolveAssetsRRoot();

    Resource::ModernAssetRegistry Registry;
    Require(Registry.LoadFromAssetsRDirectory(AssetsRRoot),
            "Cloud assets-R ModernAssetRegistry loaded for Phase 49-54 verification");

    TestPhase49ZeroDependencyEngineFoundation(RepoRoot);
    std::cout << "[PASS] Phase 49: Zero-Dependency Engine Foundation (0 upward includes in Core/Engine/Render/Resource) verified.\n";

    TestPhase50UnifiedGameSessionAndEngineApp();
    std::cout << "[PASS] Phase 50: Unified Host Runtime (GameSession hosted on Engine::EngineApp) verified.\n";

    TestPhase51AngelScriptRetirementAndAnimationServerPlayer(RepoRoot, Registry);
    std::cout << "[PASS] Phase 51: Legacy AngelScript Exporter Retirement & AnimationServer2D Player verified.\n";

    TestPhase52PathfindingAndNavigationConvergence(RepoRoot);
    std::cout << "[PASS] Phase 52: Pathfinding & Navigation Convergence (NavigationWorldResource -> NavigationServer2D) verified.\n";

    TestPhase53DataDrivenAIAndCatalogUnification(AssetsRRoot, Registry);
    std::cout << "[PASS] Phase 53: Data-Driven AI & Catalog Unification (AIBrain + BehaviorTreeServer + UnitPrototypeRegistry) verified.\n";

    TestPhase54ResourceColorAndPaletteDeduplication(RepoRoot);
    std::cout << "[PASS] Phase 54: Resource, Color, Palette & CacheView Deduplication verified.\n";

    std::cout << "=== ALL PHASE 49-54 ENGINE ZERO-DEPENDENCY & UNIFICATION TESTS PASSED ===\n";
    return 0;
}
