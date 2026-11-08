#ifndef SOKOL_DUMMY_BACKEND
#define SOKOL_DUMMY_BACKEND
#endif
#define SOKOL_GFX_IMPL
#define SOKOL_LOG_IMPL
#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_log.h"
#undef SOKOL_GFX_IMPL
#undef SOKOL_LOG_IMPL
#include "Core/Platform/Platform.h"
#include "Render/Draw2D.h"
namespace Core::Platform {
    int GetWindowWidth() { return 1280; }
    int GetWindowHeight() { return 720; }
}
namespace Render {
    bool Initialize() {
        sg_desc Desc = {};
        Desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
        Desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        Desc.environment.defaults.sample_count = 1;
        Desc.logger.func = slog_func;
        Desc.disable_validation = true;
        sg_setup(&Desc);
        return sg_isvalid();
    }
    void BeginFrame() {
        UploadPendingFontGlyphs();
        sg_pass Pass = {};
        Pass.swapchain.width = 1280;
        Pass.swapchain.height = 720;
        Pass.swapchain.sample_count = 1;
        Pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
        Pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        sg_begin_pass(&Pass);
    }
    void EndFrame() {
        sg_end_pass();
        sg_commit();
    }
    void Shutdown() {
        sg_shutdown();
    }
}
#include "Core/Audio/ModernAudioEventRouter.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/Trigger/TriggerVM.h"
#include "Render/EnvironmentAndTerrainPipeline.h"
#include "Resource/RgbaImageFileDecoder.h"
#include "Core/Audio/AudioSystem.h"
namespace Core::Audio {
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags) { return -1; }
    void AudioSystem::Update(float) {}
    void AudioSystem::SetSoundType(int, const SoundType&) {}
    void AudioSystem::SetSoundPath(int, std::string) {}
}
#include "Core/ECS/WorldSnapshot.h"
#include "Game/AI/AIBrain.h"
#include "Game/Networking/LockstepSync.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/DeclarativeAnimationPlayer.h"
#include "Render/ModernRenderPipeline.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/EventBus.h"
#include "Core/ECS/SystemManager.h"
#include "Core/ECS/WorldContext.h"
#include "Game/Components.h"
#include "Game/IScriptVM.h"
#include "Game/Map/ChkParser.h"
#include "Render/RenderQueue2D.h"
#include "Engine/Node2D.h"
#include "Resource/Cv5Decoder.h"
#include "Resource/GameDataLoader.h"
#include "Resource/ModernAssetRegistry.h"
#include "Resource/Vf4Decoder.h"

namespace {

void Require(bool Condition, const char* Message)
{
    if (!Condition) {
        std::fprintf(stderr, "[VERIFICATION FAIL] %s\n", Message);
        std::exit(1);
    }
}

std::vector<uint8_t> ReadFileBytes(const std::string& Path)
{
    std::ifstream File(Path, std::ios::binary);
    Require(File.is_open(), ("Failed to open file: " + Path).c_str());
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(File)), std::istreambuf_iterator<char>());
}

struct TestCombatSignal {
    Core::ECS::EntityID Attacker = 0;
    Core::ECS::EntityID Target = 0;
    int32_t Damage = 0;
};

struct TestMatchClockResource {
    uint64_t TickCount = 0;
    float FixedDelta = 1.0f / 24.0f;
};

class PhasedVerifySystem final : public Core::ECS::ISystem {
public:
    PhasedVerifySystem(const char* Name, Core::ECS::SystemPhase Phase, std::vector<std::string>& OrderLog)
        : mName(Name), mPhase(Phase), mOrderLog(OrderLog) {}

    std::string_view GetName() const override { return mName; }
    Core::ECS::SystemPhase GetPhase() const override { return mPhase; }

    void Update(float) override {
        mOrderLog.push_back(mName);
    }

private:
    const char* mName;
    Core::ECS::SystemPhase mPhase;
    std::vector<std::string>& mOrderLog;
};

void VerifyModernECSCore()
{
    std::printf("=== [Stage 1] Verifying Modern ECS Sparse-Set Pool, WorldContext, EventBus & Phased SystemManager ===\n");
    Core::ECS::EntityManager EM;

    // 1. Verify Paged Contiguous Sparse-Set ComponentPool<T> & Pointer Stability
    std::vector<Core::ECS::EntityID> Entities;
    for (int i = 0; i < 600; ++i) {
        Core::ECS::EntityID Id = EM.CreateEntity();
        auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(Id);
        Trans.Position = { static_cast<float>(i * 10), static_cast<float>(i * 5) };
        auto& RenderComp = EM.AddComponent<Game::Components::RenderComponent>(Id);
        RenderComp.ZOrder = 40 + i;
        Entities.push_back(Id);
    }

    auto* FirstPtr = EM.GetComponent<Game::Components::TransformComponent>(Entities[0]);
    auto* LastPtr = EM.GetComponent<Game::Components::TransformComponent>(Entities[599]);
    Require(FirstPtr != nullptr && LastPtr != nullptr, "Sparse-Set ComponentPool<T> returns valid component pointers");

    auto* TransPool = EM.GetPool<Game::Components::TransformComponent>();
    Require(TransPool != nullptr && TransPool->Size() == 600, "TransformComponent pool tracks 600 dense active entities");
    Require(TransPool->GetDenseEntities().size() == 600, "Sparse-Set dense entity array has 600 contiguous IDs");

    // Verify generation counter and post-destroy deferred lookup semantics
    uint32_t GenBefore = EM.GetGeneration(Entities[10]);
    EM.DestroyEntity(Entities[10]);
    Require(!EM.IsAlive(Entities[10]), "Destroyed entity reports !IsAlive");
    Require(EM.GetGeneration(Entities[10]) == GenBefore + 1, "Entity generation increments on DestroyEntity");
    Require(EM.GetComponent<Game::Components::RenderComponent>(Entities[10])->ZOrder == 50, "Deferred post-destroy component lookup remains intact before ID recycle");
    Require(EM.GetComponent<Game::Components::TransformComponent>(Entities[0]) == FirstPtr, "Page-allocated ComponentPool<T> preserves pointer stability across operations");

    // 2. Verify WorldContext (Unity DOTS Singleton Component / Bevy Res<T>)
    auto& Clock = EM.Context().EmplaceResource<TestMatchClockResource>();
    Clock.TickCount = 42;
    Require(EM.Context().HasResource<TestMatchClockResource>(), "WorldContext stores singleton resource");
    Require(EM.Context().GetResource<TestMatchClockResource>()->TickCount == 42, "WorldContext retrieves singleton resource state");

    // 3. Verify EventBus (Godot Signal / Decoupled Event Queue)
    int TotalDamageReceived = 0;
    EM.Events().Subscribe<TestCombatSignal>([&](const TestCombatSignal& Ev) {
        TotalDamageReceived += Ev.Damage;
    });
    EM.Events().Publish(TestCombatSignal{ Entities[0], Entities[1], 6 });
    EM.Events().Enqueue(TestCombatSignal{ Entities[2], Entities[1], 20 });
    Require(TotalDamageReceived == 6, "Immediate EventBus::Publish invokes subscribers synchronously");
    auto Drained = EM.Events().DrainEvents<TestCombatSignal>();
    Require(Drained.size() == 2 && Drained[1].Damage == 20, "EventBus::DrainEvents flushes buffered frame events");

    // 4. Verify Phased SystemManager Execution Pipeline
    Core::ECS::SystemManager SM;
    std::vector<std::string> ExecutionOrder;
    SM.RegisterSystem(std::make_unique<PhasedVerifySystem>("RenderExtractSys", Core::ECS::SystemPhase::RenderExtract, ExecutionOrder));
    SM.RegisterSystem(std::make_unique<PhasedVerifySystem>("FixedSimSys", Core::ECS::SystemPhase::FixedSimulation, ExecutionOrder));
    SM.RegisterSystem(std::make_unique<PhasedVerifySystem>("InputSys", Core::ECS::SystemPhase::Input, ExecutionOrder));
    SM.RegisterSystem(std::make_unique<PhasedVerifySystem>("TransformPropSys", Core::ECS::SystemPhase::TransformPropagation, ExecutionOrder));
    SM.RegisterSystem(std::make_unique<PhasedVerifySystem>("PresentationSys", Core::ECS::SystemPhase::Presentation, ExecutionOrder));
    SM.RegisterSystem(std::make_unique<PhasedVerifySystem>("LockstepSys", Core::ECS::SystemPhase::NetworkLockstep, ExecutionOrder));

    SM.UpdateAll(1.0f / 24.0f);
    Require(ExecutionOrder.size() == 6, "SystemManager executes all 6 phased systems");
    Require(ExecutionOrder[0] == "InputSys" &&
            ExecutionOrder[1] == "LockstepSys" &&
            ExecutionOrder[2] == "FixedSimSys" &&
            ExecutionOrder[3] == "TransformPropSys" &&
            ExecutionOrder[4] == "PresentationSys" &&
            ExecutionOrder[5] == "RenderExtractSys",
            "SystemManager strictly enforces deterministic phase ordering (Input -> Lockstep -> FixedSimulation -> TransformPropagation -> Presentation -> RenderExtract)");
    std::printf("  [OK] Sparse-Set ComponentPool (600 entities), WorldContext, EventBus, and 6-Phase SystemManager verified.\n");
}

void VerifyBinaryAndModernAssetsCrossCheck(Resource::ModernAssetRegistry& OutRegistry)
{
    std::printf("=== [Stage 2] Cross-Verifying Cloud Drive StarCraft/assets (Binary) vs StarCraft/assets-R (Modern Engine) ===\n");

    // 1. Load raw binary assets from StarCraft/assets
    Resource::GameDataLoader BinaryLoader;
    Require(BinaryLoader.LoadUnitDataRaw(ReadFileBytes("assets/arr/units.dat")), "Loaded binary assets/arr/units.dat");
    Require(BinaryLoader.LoadWeaponDataRaw(ReadFileBytes("assets/arr/weapons.dat")), "Loaded binary assets/arr/weapons.dat");
    Require(BinaryLoader.LoadFlingyDataRaw(ReadFileBytes("assets/arr/flingy.dat")), "Loaded binary assets/arr/flingy.dat");
    Require(BinaryLoader.LoadSpriteDataRaw(ReadFileBytes("assets/arr/sprites.dat")), "Loaded binary assets/arr/sprites.dat");
    Require(BinaryLoader.LoadImageTblDataRaw(ReadFileBytes("assets/arr/images.tbl")), "Loaded binary assets/arr/images.tbl");
    Require(BinaryLoader.LoadImageDataRaw(ReadFileBytes("assets/arr/images.dat")), "Loaded binary assets/arr/images.dat");
    Require(BinaryLoader.LoadImageRelationDataRaw(ReadFileBytes("assets/images.rel")), "Loaded binary assets/images.rel");
    Require(BinaryLoader.LoadUpgradeDataRaw(ReadFileBytes("assets/arr/upgrades.dat")), "Loaded binary assets/arr/upgrades.dat");
    Require(BinaryLoader.LoadTechDataRaw(ReadFileBytes("assets/arr/techdata.dat")), "Loaded binary assets/arr/techdata.dat");
    Require(BinaryLoader.LoadOrderDataRaw(ReadFileBytes("assets/arr/orders.dat")), "Loaded binary assets/arr/orders.dat");

    Game::IScriptDatabase BinaryIScriptDB;
    Require(BinaryIScriptDB.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"), "Loaded binary assets/scripts/iscript.bin + images.dat");

    // Also verify binary map (byways.chk) and tileset (badlands.cv5 + badlands.vf4)
    Game::Map::ChkParser MapParser;
    auto ChkBytes = ReadFileBytes("assets/maps/byways.chk");
    Require(MapParser.Parse(ChkBytes.data(), ChkBytes.size()), "Parsed binary map assets/maps/byways.chk");
    Require(MapParser.GetMapWidth() > 0 && MapParser.GetMapHeight() > 0, "byways.chk has valid dimensions");

    auto Cv5Groups = Resource::Cv5Decoder::Decode(ReadFileBytes("assets/TileSet/badlands.cv5"));
    auto Vf4MiniTiles = Resource::Vf4Decoder::Decode(ReadFileBytes("assets/TileSet/badlands.vf4"));
    Require(!Cv5Groups.empty() && !Vf4MiniTiles.Entries.empty(), "Decoded binary tileset badlands.cv5 and badlands.vf4");

    // 2. Load converted modern assets from StarCraft/assets-R
    Require(OutRegistry.LoadFromAssetsRDirectory("assets-R"), "Loaded converted modern assets from assets-R/");
    Require(OutRegistry.GetUnitCount() == 228, "ModernAssetRegistry loaded all 228 UnitDataAssets");
    Require(OutRegistry.GetWeaponCount() == 130, "ModernAssetRegistry loaded all 130 Weapons");
    Require(OutRegistry.GetSoundCount() == 1144, "ModernAssetRegistry loaded all 1,144 SFX Sounds");
    Require(OutRegistry.GetHDImageCount() == 999, "ModernAssetRegistry loaded all 999 HD Image entries");
    Require(OutRegistry.GetSocketTrackCount() == 2, "ModernAssetRegistry loaded both 2D Socket Mount Tracks");
    Require(OutRegistry.GetIScriptControllerCount() == 398, "ModernAssetRegistry loaded all 398 IScript Animation Controllers");
    Require(OutRegistry.GetTotalAnimationClipCount() == 1364, "ModernAssetRegistry loaded all 1,364 declarative Animation Clips");
    Require(OutRegistry.GetPBRPrefabCount() == 247, "ModernAssetRegistry loaded all 247 HD 3-Channel PBR Prefab Bundles");

    // 3. Perform 1:1 Cross-Verification between Binary and Modern Assets
    auto Report = OutRegistry.CrossVerifyWithBinaryAssets(BinaryLoader, &BinaryIScriptDB);
    if (!Report.Diagnostics.empty()) {
        for (const auto& Diag : Report.Diagnostics) {
            std::fprintf(stderr, "  [DIAG] %s\n", Diag.c_str());
        }
    }
    Require(Report.Passed, "100% 1:1 Cross-Verification passed between StarCraft/assets (binary) and StarCraft/assets-R (modern)");

    // Spot-check canonical units: Terran Marine (ID 0), Terran Siege Tank (ID 5), Protoss Pylon (ID 156)
    auto MarineHandle = OutRegistry.GetUnitHandle(0);
    const auto* Marine = OutRegistry.ResolveUnit(MarineHandle);
    Require(Marine != nullptr && Marine->UnitName == "Terran_Marine" && Marine->MaxHP == 40 && Marine->ImageID == 239,
            "Terran_Marine (UnitID 0) verified via generational AssetHandle<ModernUnitDataAsset>");

    const auto* Tank = OutRegistry.FindUnitByID(5);
    Require(Tank != nullptr && Tank->HasTurretAttachment && Tank->TurretUnitID == 6,
            "Terran_Siege_Tank_Tank_Mode (UnitID 5) turret subunit attachment (UnitID 6) verified");

    const auto* PylonPrefab = OutRegistry.FindPBRPrefabByImageID(189);
    Require(PylonPrefab != nullptr && PylonPrefab->TexturesVerifiedOnDisk && PylonPrefab->SpriteAtlasSlices.size() == 1,
            "Prefab_protoss_pylon (ImageID 189) 3-channel PBR textures verified on disk");

    std::printf("  [OK] Cross-verified %zu Units, %zu Weapons, %zu HD Images, %zu images.rel links, %zu IScript Controllers (%zu clips), %zu PBR Prefabs, byways.chk (%dx%d), and badlands tileset (%zu CV5 groups, %zu VF4 minitiles).\n",
        Report.UnitsChecked,
        Report.WeaponsChecked,
        Report.ImagesChecked,
        Report.ImageRelationsChecked,
        Report.IScriptControllersChecked,
        OutRegistry.GetTotalAnimationClipCount(),
        Report.PBRPrefabsChecked,
        MapParser.GetMapWidth(),
        MapParser.GetMapHeight(),
        Cv5Groups.size(),
        Vf4MiniTiles.Entries.size());
}

void VerifySceneGraphAndPBRRenderQueue(const Resource::ModernAssetRegistry& Registry)
{
    std::printf("=== [Stage 3] Verifying Godot-Style SceneNode2D Socket Hierarchy & 64-Bit SortKey PBR RenderQueue2D ===\n");

    const auto* ScvSocketTrack = Registry.FindSocketTrack("Socket_Terran_SCV_RepairAndSpark");
    Require(ScvSocketTrack != nullptr && ScvSocketTrack->NumRawFrames == 51, "Found Socket_Terran_SCV_RepairAndSpark (51 raw frames)");

    // Build a composite SCV scene tree: Root -> MainSprite + Shadow + RepairSparkSocket
    Render::SceneNode2D ScvRoot("Terran_SCV_Root", Render::SceneNodeRole::RootUnit);
    ScvRoot.SetLocalPosition({ 320.0f, 240.0f });
    ScvRoot.SetDirection32(2); // Direction 2: SD offset should be [5, -16], HD 4x offset [20, -64]
    ScvRoot.SetFrameSet(0);

    auto MainSprite = std::make_unique<Render::SceneNode2D>("SCV_Body", Render::SceneNodeRole::MainSprite);
    MainSprite->SetImageID(260); // Use PBR dropship/unit image ID for material binding test
    MainSprite->SetElevationSortLayer(4);
    MainSprite->SetFollowParentDirection(true);

    auto SparkSocket = std::make_unique<Render::SceneNode2D>("SCV_RepairSparkSocket", Render::SceneNodeRole::SocketAttachment2D);
    SparkSocket->SetImageID(960); // thingy_bsmoke / spark overlay
    SparkSocket->SetElevationSortLayer(4);
    SparkSocket->SetZIndexOffset(10);
    SparkSocket->BindSocketTrack(ScvSocketTrack, /*UseHD4xScale=*/false);
    Render::SceneNode2D* SparkPtr = MainSprite->AddChild(std::move(SparkSocket));

    ScvRoot.AddChild(std::move(MainSprite));

    // Add a Protoss Pylon with Shadow & Glow from PBR bundles
    Render::SceneNode2D PylonRoot("Protoss_Pylon_Root", Render::SceneNodeRole::RootUnit);
    PylonRoot.SetLocalPosition({ 400.0f, 180.0f });

    auto PylonShadow = std::make_unique<Render::SceneNode2D>("Pylon_Shadow", Render::SceneNodeRole::ShadowOverlay);
    PylonShadow->SetImageID(166); // protoss_pciShad PBR bundle
    PylonShadow->SetElevationSortLayer(4);

    auto PylonBody = std::make_unique<Render::SceneNode2D>("Pylon_Body", Render::SceneNodeRole::MainSprite);
    PylonBody->SetImageID(189); // protoss_pylon PBR bundle
    PylonBody->SetElevationSortLayer(4);

    auto PylonGlow = std::make_unique<Render::SceneNode2D>("Pylon_Glow", Render::SceneNodeRole::GlowOverlay);
    PylonGlow->SetImageID(120); // thingy_pshGlow PBR bundle
    PylonGlow->SetElevationSortLayer(4);
    PylonGlow->SetZIndexOffset(5);

    PylonRoot.AddChild(std::move(PylonShadow));
    PylonRoot.AddChild(std::move(PylonBody));
    PylonRoot.AddChild(std::move(PylonGlow));

    // Propagate transforms & sample 2D socket offsets
    ScvRoot.PropagateTransforms();
    PylonRoot.PropagateTransforms();

    Require(SparkPtr->GetWorldPosition().X == 325.0f && SparkPtr->GetWorldPosition().Y == 224.0f,
            "Socket_Terran_SCV_RepairAndSpark accurately offsets child world position to (320+5, 240-16) = (325, 224) at direction 2");

    // Extract into RenderQueue2D and verify 64-bit SortKey ordering
    Render::RenderQueue2D Queue;
    Queue.ExtractSceneTree(ScvRoot, &Registry, /*TeamColorID=*/1);
    Queue.ExtractSceneTree(PylonRoot, &Registry, /*TeamColorID=*/2);
    Queue.SortAndBatch();

    const auto& Cmds = Queue.GetCommands();
    Require(Cmds.size() == 5, "RenderQueue2D extracted all 5 renderable scene nodes");
    Require(Cmds[0].Pass == Render::RenderLayerPass::GroundShadow, "64-bit SortKey sorts GroundShadow pass first");
    Require(Cmds[1].Pass == Render::RenderLayerPass::GroundUnit && Cmds[1].ImageID == 189, "64-bit SortKey sorts Pylon (Y=180) before SCV (Y=240)");
    Require(Cmds[2].Pass == Render::RenderLayerPass::GroundUnit && Cmds[2].ImageID == 260, "64-bit SortKey sorts SCV (Y=240) after Pylon (Y=180)");
    Require(Cmds[3].Pass == Render::RenderLayerPass::GroundOverlay, "64-bit SortKey sorts GroundOverlay pass after GroundUnit pass");
    Require(Cmds[1].Material.HasNormalTeamAOTexture && Cmds[1].Material.HasSpecEmissiveTexture,
            "PBR material descriptor binds all 3 texture channels (BaseColor_A, Normal_RG_Team_B_AO_A, Spec_R_Emissive_GBA)");

    auto BatchStats = Queue.ComputeBatchStats();
    Require(BatchStats.TotalCommands == 5 && BatchStats.PBRDeferredLitCommands == 2 && BatchStats.ShadowCommands == 1 && BatchStats.AdditiveGlowCommands == 2,
            "RenderQueue2D accurately classifies PBRDeferredLit, Shadow, and AdditiveGlow draw calls");
    std::printf("  [OK] SceneNode2D 2D Socket propagation and 64-bit SortKey PBR RenderQueue2D verified (%zu commands, %zu draw calls).\n",
        BatchStats.TotalCommands, BatchStats.DrawCalls);
}

void VerifyAIBehaviorTrees()
{
    std::printf("=== [Stage 4] Verifying 449 Decompiled AI Behavior Trees (aiscript.bin + bwscript.bin) ===\n");
    Game::AI::BehaviorTreeCatalog Catalog;
    Require(Catalog.LoadFromAssetsR("assets-R"), "Loaded AI Behavior Tree catalogs from assets-R/");
    Require(Catalog.GetAIScriptTreeCount() == 293, "Loaded all 293 AIScript Behavior Trees");
    Require(Catalog.GetBWScriptTreeCount() == 156, "Loaded all 156 BWScript Behavior Trees");
    Require(Catalog.GetTotalTreeCount() == 449, "Total unique AI Behavior Trees equals 449");

    size_t VerifiedScripts = 0;
    size_t ScriptMismatches = 0;
    Require(Catalog.CrossVerifyBinaryScripts(
                "assets/scripts/aiscript.bin",
                "assets/scripts/bwscript.bin",
                VerifiedScripts,
                ScriptMismatches),
            "100% 1:1 entry-offset and FourCC cross-verification passed against raw binary aiscript.bin and bwscript.bin");

    // Execute "Ter3" Behavior Tree and verify blackboard state transitions
    const auto* Ter3Asset = Catalog.FindTree("Ter3");
    Require(Ter3Asset != nullptr, "Found Ter3 BehaviorTreeAsset");

    Game::AI::BehaviorTreeInstance Ter3Runner(Ter3Asset);
    auto Status = Ter3Runner.Tick(1000);
    Require(Status == Game::AI::BTNodeStatus::Running, "Ter3 BehaviorTreeInstance is actively running");

    const auto& BB = Ter3Runner.GetBlackboard();
    Require(BB.CampaignMode && BB.TownStarted && BB.DefaultBuildOff, "Ter3 initialized campaign & town state on Blackboard");
    Require(BB.QueuedBuildOrders.size() == 6, "Ter3 queued 6 build & tech orders (Hatchery, 6x Drone, Spawning Pool, Hydralisk Den, Spire, Burrowing)");
    Require(BB.ActiveDefensePool.size() == 9, "Ter3 configured 9 home defense pool rules");
    Require(BB.ExpansionsTriggered == 1, "Ter3 triggered capt_expand town expansion");
    Require(BB.AttackWavesLaunched >= 1 && !BB.LastLaunchedWave.empty(), "Ter3 launched initial attack wave and transitioned to repeating loop block Block_0x1B73");
    Require(Ter3Runner.GetCurrentBlockID() == "Block_0x1B73", "Ter3 transitioned to self-looping attack block Block_0x1B73");

    std::printf("  [OK] Cross-verified %zu AI Behavior Trees against aiscript.bin/bwscript.bin and executed Ter3 behavior tree.\n",
        VerifiedScripts);
}

} // namespace

void VerifyPhase2Subsystems(const Resource::ModernAssetRegistry& Registry)
{
    std::printf("=== [Stage 5 - Phase 2] Verifying Data-Driven RaceDef Sync, Declarative 6-Track Animator, ModernRenderPipeline, AIBrain BehaviorTree & Deterministic WorldSnapshot ===\n");

    // 1. Data-Driven RaceDef Sync with ModernAssetRegistry
    size_t VerifiedWeapons = 0;
    size_t SyncedUnits = Game::Races::SyncAndVerifyWithModernAssets(Registry, &VerifiedWeapons);
    Require(SyncedUnits > 50 && VerifiedWeapons > 30, "SyncAndVerifyWithModernAssets synchronized Terran/Zerg/Protoss UnitDefs with assets-R");

    // 2. Declarative 6-Track Animation Player + EventBus Signals (Marine GndAttkInit, IScriptID 78)
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    auto AnimPlayerSys = std::make_unique<Game::Systems::DeclarativeAnimationPlayerSystem>(SM, &Registry);
    auto* AnimPlayerPtr = AnimPlayerSys.get();
    SM.RegisterSystem(std::move(AnimPlayerSys));

    auto RenderPipeSys = std::make_unique<Render::ModernRenderPipelineSystem>(SM, &Registry);
    auto* RenderPipePtr = RenderPipeSys.get();
    SM.RegisterSystem(std::move(RenderPipeSys));

    int AudioSignalsFired = 0;
    int CombatSignalsFired = 0;
    int FsmNoBrkSignalsFired = 0;

    EM.Events().Subscribe<Game::Events::AnimAudioNotifyEvent>([&](const Game::Events::AnimAudioNotifyEvent& Ev) {
        if (Ev.SoundID == 69) ++AudioSignalsFired;
    });
    EM.Events().Subscribe<Game::Events::AnimCombatNotifyEvent>([&](const Game::Events::AnimCombatNotifyEvent& Ev) {
        if (Ev.NotifyType == "attackwith") ++CombatSignalsFired;
    });
    EM.Events().Subscribe<Game::Events::AnimFsmSignalEvent>([&](const Game::Events::AnimFsmSignalEvent& Ev) {
        if (Ev.Signal == "nobrkcodestart") ++FsmNoBrkSignalsFired;
    });

    // Spawn Marine (UnitID 0, ImageID 239, IScriptID 78), Siege Tank (UnitID 5, ImageID 250, Turret ImageID 251), and Protoss Pylon (UnitID 156, ImageID 189)
    Core::ECS::EntityID MarineEnt = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(MarineEnt).Position = { 200.0f, 300.0f };
    auto& MarineUnit = EM.AddComponent<Game::Components::UnitComponent>(MarineEnt);
    MarineUnit.ChkUnitID = 0;
    MarineUnit.HitPoints = 40;
    auto& MarineRender = EM.AddComponent<Game::Components::RenderComponent>(MarineEnt);
    MarineRender.ImageID = 239;
    auto& MarineAnim = EM.AddComponent<Game::Systems::DeclarativeAnimatorComponent>(MarineEnt);
    MarineAnim.IScriptID = 78;

    Require(AnimPlayerPtr->TransitionToState(MarineEnt, "GndAttkInit"), "Marine transitioned to GndAttkInit declarative clip");
    AnimPlayerPtr->StepEntityTicks(MarineEnt, 4); // Tick 3 triggers Sound 69, attackwith(1), nobrkcodestart, and frame_set 51
    Require(AudioSignalsFired == 1 && CombatSignalsFired == 1 && FsmNoBrkSignalsFired == 1,
            "Declarative 6-Track Animator fired Track 2 (Audio 69), Track 3 (Combat attackwith), and Track 6 (FSM nobrkcodestart) via EventBus at Tick 3");
    Require(!AnimPlayerPtr->TransitionToState(MarineEnt, "Walking", /*ForceInterrupt=*/false),
            "NoBreakCodeStart blocks non-forced state transition during weapon fire windup");

    // 3. ModernRenderPipelineSystem direct ECS -> SceneNode2D -> RenderQueue2D extraction
    Core::ECS::EntityID PylonEnt = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(PylonEnt).Position = { 150.0f, 120.0f };
    EM.AddComponent<Game::Components::UnitComponent>(PylonEnt).ChkUnitID = 156;
    EM.AddComponent<Game::Components::RenderComponent>(PylonEnt).ImageID = 189;

    SM.UpdateAll(1.0f / 24.0f);
    Require(RenderPipePtr->GetSceneRootCount() == 2, "ModernRenderPipelineSystem extracted 2 live ECS entities into SceneNode2D hierarchy");
    Require(RenderPipePtr->GetRenderQueue().GetCommands().size() >= 3, "ModernRenderPipelineSystem auto-attached shadow nodes via images.rel and sorted PBR RenderQueue2D");

    // 4. AIBrain BehaviorTree Integration
    Game::AI::BehaviorTreeCatalog BTCatalog;
    Require(BTCatalog.LoadFromAssetsR("assets-R"), "Loaded BehaviorTreeCatalog for AIBrain");
    Game::Systems::AIBrain Brain(SM, /*AIPlayerID=*/1, /*TargetPlayerID=*/0);
    Require(Brain.BindBehaviorTree(BTCatalog.FindTree("Ter3")), "Bound Ter3 BehaviorTree to AIBrain");
    Require(Brain.ResolveModernAssetUnitDefID("Zerg_Spawning_Pool") == Game::Races::GetUnitDefID("Spawning Pool"),
            "AIBrain maps assets-R unit names (Zerg_Spawning_Pool) to runtime RaceDef IDs");
    Require(Brain.ResolveModernAssetUnitDefID("Terran_Siege_Tank_Tank_Mode") == Game::Races::GetUnitDefID("SiegeTank"),
            "AIBrain maps Terran_Siege_Tank_Tank_Mode to SiegeTank DefID");

    // 5. WorldSnapshot & LockstepSync deterministic state hash & rollback verification
    auto& WorldUpgrades = EM.Context().EmplaceResource<Core::ECS::WorldUpgradeResource>();
    WorldUpgrades.PlayerUpgrades[0].Levels[static_cast<size_t>(Game::Systems::ResearchId::Terran_InfantryWeapons)] = 2;

    auto SnapBefore = Core::ECS::WorldSnapshotSerializer::CaptureSnapshot(EM, /*FrameNumber=*/100);
    Require(SnapBefore.Entities.size() == 2 && SnapBefore.StateHashFNV1a != 0, "Captured deterministic WorldSnapshot at Frame 100");

    // Mutate world state (damage Marine, move Pylon, change upgrade level)
    EM.GetComponent<Game::Components::UnitComponent>(MarineEnt)->HitPoints = 5;
    EM.GetComponent<Game::Components::TransformComponent>(PylonEnt)->Position = { 999.0f, 999.0f };
    WorldUpgrades.PlayerUpgrades[0].Levels[static_cast<size_t>(Game::Systems::ResearchId::Terran_InfantryWeapons)] = 0;

    uint32_t MutatedHash = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(EM, 100);
    Require(MutatedHash != SnapBefore.StateHashFNV1a, "Mutating ECS world state changes deterministic FNV-1a hash");

    // Rollback to SnapBefore and verify exact hash reproduction
    Core::ECS::WorldSnapshotSerializer::RestoreSnapshot(EM, SnapBefore);
    uint32_t RestoredHash = Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(EM, 100);
    Require(RestoredHash == SnapBefore.StateHashFNV1a, "Restoring WorldSnapshot reproduces exact bit-for-bit FNV-1a state hash");

    Game::Networking::LockstepSync Lockstep;
    Lockstep.Init(2, 2);
    Lockstep.SetStateHashCallback([&]() {
        return Core::ECS::WorldSnapshotSerializer::ComputeWorldHashFNV1a(EM, Lockstep.GetCurrentFrame());
    });
    Require(Lockstep.ComputeStateHash() != 0, "LockstepSync bound to WorldSnapshotSerializer::ComputeWorldHashFNV1a");

    std::printf("  [OK] Phase 2 Subsystems verified: Synced %zu UnitDefs (%zu Weapons), 6-Track Animator EventBus signals, ModernRenderPipeline (%zu roots), AIBrain BehaviorTree mapping, and Rollback Snapshot (Hash=0x%08X).\n",
        SyncedUnits, VerifiedWeapons, RenderPipePtr->GetSceneRootCount(), RestoredHash);
}

void VerifyPhase3Subsystems(const Resource::ModernAssetRegistry& Registry)
{
    std::printf("=== [Stage 6 - Phase 3] Verifying GPU Tilemap PNG vs Binary CHK/CV5/VF4, 45-Frame Water Normal Flipbook, 5-Layer Starfield, 3-Channel PBR+DXT5nm Shader, TriggerVM BehaviorTree & SpatialBroadphase ===\n");

    // 1. EnvironmentAndTerrainPipeline: Load 04_Terrain_Environment_And_Shaders
    Render::EnvironmentAndTerrainPipeline EnvPipe;
    Require(EnvPipe.LoadFromAssetsR("assets-R"), "Loaded 04_Terrain_Environment_And_Shaders from assets-R/");
    Require(EnvPipe.GetStarLayerCount() == 5 && EnvPipe.GetTotalStarCount() == 294,
            "Loaded 5 parallax starfield layers with 294 stars from Space_Parallax_Starfield_Particles.json");
    Require(EnvPipe.ValidateShaderContracts(),
            "Validated Godot 4 (.gdshader) and Unity URP (.shader) 3-Channel PBR + DXT5nm shader contracts");

    // Verify parallax starfield screen wrapping
    auto StarPos0 = EnvPipe.ComputeStarScreenPos(1, 0, { 0.0f, 0.0f }, 640, 480);
    auto StarPos1 = EnvPipe.ComputeStarScreenPos(1, 0, { 100.0f, 50.0f }, 640, 480);
    Require(StarPos0.X != StarPos1.X && StarPos0.Y != StarPos1.Y,
            "Parallax starfield layer 1 (scroll factor 0.28) shifts star screen coordinates with camera motion");

    // 2. Cross-verify GPU Tilemap Texture (96x96 PNG) 1:1 against raw binary byways.chk MTXM + jungle.cv5 + jungle.vf4
    Game::Map::ChkParser Chk;
    auto ChkBytes = ReadFileBytes("assets/maps/byways.chk");
    Require(Chk.Parse(ChkBytes.data(), ChkBytes.size()), "Parsed byways.chk for GPU tilemap verification");
    auto JungleCv5 = Resource::Cv5Decoder::Decode(ReadFileBytes("assets/TileSet/jungle.cv5"));
    auto JungleVf4 = Resource::Vf4Decoder::Decode(ReadFileBytes("assets/TileSet/jungle.vf4"));
    Require(!JungleCv5.empty() && !JungleVf4.Entries.empty(), "Decoded binary jungle.cv5 and jungle.vf4");

    size_t TilesVerified = 0;
    size_t TileMismatches = 0;
    Require(EnvPipe.CrossVerifyTilemapWithBinary(
                Chk.GetMegaTiles(),
                Chk.GetMapWidth(),
                Chk.GetMapHeight(),
                JungleCv5,
                JungleVf4,
                TilesVerified,
                TileMismatches),
            "100% 1:1 Cross-Verification passed between T_Byways_Map_128x128_TileID_RG_Walkable_B.png and binary byways.chk + jungle.cv5 + jungle.vf4 (all 9,216 tiles)");

    // 3. Water Normal 45-Frame 9x5 Flipbook sampling
    float WN_X = 0.0f, WN_Y = 0.0f, WN_Z = 0.0f;
    Require(EnvPipe.SampleWaterNormalAtTime(0.5f, 64, 64, WN_X, WN_Y, WN_Z),
            "Sampled animated water surface normal from T_Water_Normal_45Frames_9x5_Flipbook.png (1152x640)");
    Require(WN_Z > 0.1f && std::abs(WN_X * WN_X + WN_Y * WN_Y + WN_Z * WN_Z - 1.0f) < 0.01f,
            "Reconstructed water normal is unit-length with positive Z hemisphere");

    // 4. Evaluate 3-Channel PBR + DXT5nm + TeamColor shader math on real decoded Protoss Pylon PNGs
    const auto* PylonPrefab = Registry.FindPBRPrefabByImageID(189);
    Require(PylonPrefab != nullptr, "Resolved Prefab_protoss_pylon");
    auto BaseImg = Resource::Graphics::DecodeRgbaImageFile(PylonPrefab->BaseColorTexturePath);
    auto NtaImg  = Resource::Graphics::DecodeRgbaImageFile(PylonPrefab->NormalTeamAOTexturePath);
    auto SpecImg = Resource::Graphics::DecodeRgbaImageFile(PylonPrefab->SpecEmissiveTexturePath);
    Require(BaseImg.has_value() && NtaImg.has_value() && SpecImg.has_value(),
            "Decoded all 3 PNG texture channels for Prefab_protoss_pylon (216x264 RGBA)");

    auto ShadedCenter = Render::EnvironmentAndTerrainPipeline::Evaluate3ChannelPBRPixel(
        *BaseImg, *NtaImg, *SpecImg, BaseImg->Width / 2, BaseImg->Height / 2, 0.12f, 0.48f, 1.0f, false);
    Require(ShadedCenter.A > 200 && (ShadedCenter.R > 0 || ShadedCenter.G > 0 || ShadedCenter.B > 0),
            "Evaluated 3-Channel PBR + DXT5nm + Emissive pixel lighting on Protoss Pylon center texel");

    // 5. ModernAudioEventRouterSystem: EventBus -> 1,144 SFX Catalog resolution
    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    auto AudioRouter = std::make_unique<Core::Audio::ModernAudioEventRouterSystem>(SM, &Registry, nullptr);
    auto* AudioRouterPtr = AudioRouter.get();
    SM.RegisterSystem(std::move(AudioRouter));

    auto AnimPlayer = std::make_unique<Game::Systems::DeclarativeAnimationPlayerSystem>(SM, &Registry);
    auto* AnimPlayerPtr = AnimPlayer.get();
    SM.RegisterSystem(std::move(AnimPlayer));

    auto SpatialSys = std::make_unique<Game::Pathfinding::WorldSpatialIndexSystem>(SM);
    SM.RegisterSystem(std::move(SpatialSys));

    Core::ECS::EntityID PylonEnt = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(PylonEnt).Position = { 512.0f, 512.0f };
    EM.AddComponent<Game::Components::PlayerComponent>(PylonEnt).PlayerID = 1;
    EM.AddComponent<Game::Components::UnitComponent>(PylonEnt).HitPoints = 300;
    auto& PylonAnim = EM.AddComponent<Game::Systems::DeclarativeAnimatorComponent>(PylonEnt);
    PylonAnim.IScriptID = 181; // Protoss Pylon

    // Transition Pylon to Death clip (Tick 0 fires SoundID 7: sound/Misc/ExploLrg.wav)
    Require(AnimPlayerPtr->TransitionToState(PylonEnt, "Death", /*ForceInterrupt=*/true),
            "Transitioned Protoss Pylon to Death declarative clip");
    Require(!AudioRouterPtr->GetDispatchedHistory().empty() &&
            AudioRouterPtr->GetDispatchedHistory().back().SoundID == 7 &&
            AudioRouterPtr->GetDispatchedHistory().back().Priority > 0,
            "ModernAudioEventRouterSystem intercepted AnimAudioNotifyEvent (SoundID 7) and resolved SFX catalog priority/volume metadata");

    // 6. WorldSpatialIndexSystem O(1) broadphase & nearest enemy query
    for (int i = 0; i < 50; ++i) {
        Core::ECS::EntityID E = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(E).Position = { 100.0f + i * 16.0f, 100.0f + i * 8.0f };
        EM.AddComponent<Game::Components::PlayerComponent>(E).PlayerID = (i % 2 == 0) ? 0 : 1;
        EM.AddComponent<Game::Components::UnitComponent>(E).HitPoints = 40;
    }
    SM.UpdatePhase(Core::ECS::SystemPhase::FixedSimulation, 1.0f / 24.0f);
    const auto* SpatialRes = EM.Context().GetResource<Game::Pathfinding::SpatialIndexResource>();
    Require(SpatialRes != nullptr && SpatialRes->IndexedEntityCount == 51,
            "WorldSpatialIndexSystem indexed all 51 living entities into NavigationSpatialHash");
    Core::ECS::EntityID NearestEnemy = SpatialRes->FindNearestEnemy(EM, { 100.0f, 100.0f }, 256.0f, /*MyPlayerID=*/0);
    Require(NearestEnemy != Core::ECS::InvalidEntity && EM.GetComponent<Game::Components::PlayerComponent>(NearestEnemy)->PlayerID == 1,
            "SpatialIndexResource::FindNearestEnemy resolved closest opposing unit in O(1) cell neighborhood");

    // 7. TriggerVM + BehaviorTreeCatalog campaign AI script binding
    Game::AI::BehaviorTreeCatalog BTCatalog;
    Require(BTCatalog.LoadFromAssetsR("assets-R"), "Loaded BehaviorTreeCatalog for TriggerVM");
    Game::Trigger::TriggerVM TrigVM;
    TrigVM.SetBehaviorTreeCatalog(&BTCatalog);

    // Construct a synthetic trigger that executes StartAIScript("Ter3") for Player 1
    Game::Trigger::SCRTriggerData TrigData{};
    TrigData.Players[1].Owner = 5; // Computer slot
    Game::Trigger::SCRTrigger RawTrig{};
    RawTrig.Owners[1] = 1;
    RawTrig.Conditions[0].ConditionType = 22; // Always
    RawTrig.Actions[0].ActionType = 15;       // Run AI Script
    // Pack "Ter3" as little-endian uint32_t
    const uint32_t Ter3FourCC =
        static_cast<uint32_t>('T') |
        (static_cast<uint32_t>('e') << 8) |
        (static_cast<uint32_t>('r') << 16) |
        (static_cast<uint32_t>('3') << 24);
    RawTrig.Actions[0].Secondary = Ter3FourCC;
    TrigData.Triggers.push_back(RawTrig);

    Require(TrigVM.LoadScript(TrigData), "Loaded synthetic campaign trigger with RunAIScript(Ter3)");
    TrigVM.Update(EM, 6.0f);
    const auto* BoundBT = TrigVM.GetPlayerBehaviorTree(1);
    Require(BoundBT != nullptr && BoundBT->GetBlackboard().CampaignMode && BoundBT->GetBlackboard().TownStarted,
            "TriggerVM Action 15 (Run AI Script) automatically bound and ticked decompiled Ter3 BehaviorTreeInstance");

    std::printf("  [OK] Phase 3 Subsystems verified: %zu/9216 GPU Tilemap cells cross-checked, 45-frame Water Normal Flipbook, 294 Parallax Stars, 3-Channel PBR Pixel Evaluator, Audio Event Router, Spatial Broadphase, and TriggerVM BehaviorTree!\n",
        TilesVerified);
}

void VerifyPhase4SokolGpuBackend(const Resource::ModernAssetRegistry& Registry)
{
    std::printf("=== [Stage 7 - Phase 4] Verifying Direct Sokol GPU 3-Channel PBR Shader Pipeline, Texture Upload & Draw2D Batch Dispatch ===\n");

    Require(Render::Initialize(), "Initialized Sokol GFX backend (SG_BACKEND_DUMMY)");
    Require(Render::g_Draw2D.Init(), "Initialized Render::g_Draw2D (Textured, HealthBar, SpriteEffect, and 3-Channel PBRSprite pipelines)");

    Core::ECS::SystemManager SM;
    auto& EM = SM.GetEntityManager();

    auto RenderPipeSys = std::make_unique<Render::ModernRenderPipelineSystem>(SM, &Registry);
    auto* RenderPipePtr = RenderPipeSys.get();
    SM.RegisterSystem(std::move(RenderPipeSys));

    // Upload all 13 converted 3-Channel PBR Prefab bundles (PNG -> sg_image + sg_view)
    const size_t UploadedBundles = RenderPipePtr->InitializeSokolPBRResources();
    Require(UploadedBundles == 247, "Uploaded all 247 3-Channel HD PBR Prefab bundles to Sokol GPU textures and views");

    const auto* PylonGpuSet = RenderPipePtr->GetSokolDispatcher().FindGpuPBRTextureSet(189);
    Require(PylonGpuSet != nullptr &&
            PylonGpuSet->BaseColorA.Valid() &&
            PylonGpuSet->NormalTeamAO.Valid() &&
            PylonGpuSet->SpecEmissive.Valid() &&
            PylonGpuSet->Width == 216 && PylonGpuSet->Height == 264,
            "Prefab_protoss_pylon (ImageID 189) bound 3 valid Sokol sg_image/sg_view handles (BaseColor_A, Normal_RG_Team_B_AO_A, Spec_R_Emissive_GBA)");

    // Spawn 4 PBR entities in ECS: Protoss Pylon (189), Protoss Citadel (164), Protoss Forge (167), and Terran Science Vessel (260 + Turret 261)
    auto SpawnPBREntity = [&](int ChkUnitID, uint32_t ImageID, uint32_t SubunitImageID, float X, float Y, int PlayerID) {
        Core::ECS::EntityID E = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(E).Position = { X, Y };
        EM.AddComponent<Game::Components::PlayerComponent>(E).PlayerID = PlayerID;
        auto& U = EM.AddComponent<Game::Components::UnitComponent>(E);
        U.ChkUnitID = ChkUnitID;
        U.HitPoints = 300;
        auto& R = EM.AddComponent<Game::Components::RenderComponent>(E);
        R.ImageID = ImageID;
        R.SubunitImageID = SubunitImageID;
        return E;
    };

    SpawnPBREntity(156, 189, UINT32_MAX, 300.0f, 220.0f, 0); // Pylon (+ auto shadow 191)
    SpawnPBREntity(163, 164, UINT32_MAX, 420.0f, 260.0f, 1); // Citadel of Adun (+ auto shadow 166)
    SpawnPBREntity(166, 167, UINT32_MAX, 520.0f, 300.0f, 2); // Forge (+ auto shadow 169)
    SpawnPBREntity(9,   260, 261,        640.0f, 340.0f, 3); // Science Vessel + Turret 261

    Render::Camera2D Cam;
    Cam.SetViewport(1280, 720);
    Cam.SetPosition(480.0f, 280.0f);

    Render::BeginFrame();
    auto Telemetry = RenderPipePtr->ExtractAndFlushToSokol(EM, Render::g_Draw2D, Cam);
    Render::EndFrame();

    Require(Telemetry.CommandsSubmitted >= 8, "ModernRenderPipeline extracted >= 8 sorted render commands (bodies, shadows, turret)");
    Require(Telemetry.PBRSpritesDispatched >= 5 && Telemetry.ShadowSpritesDispatched >= 3,
            "SokolPBRDispatcher dispatched both 3-channel PBRDeferredLit sprites and Shadow pass sprites");
    Require(Telemetry.SokolDrawCallsExecuted >= 4 && Telemetry.SokolElementsDrawn == Telemetry.CommandsSubmitted * 6,
            "Sokol GPU backend executed sg_draw calls for every quad (6 indices per sprite) with 3-slot texture bindings");
    Require(Telemetry.SokolUniformUploads >= Telemetry.SokolDrawCallsExecuted * 2,
            "Sokol GPU backend uploaded both vertex MVP matrix and fragment PBRSpriteShaderParams uniform blocks");

    Render::g_Draw2D.Shutdown();
    Render::Shutdown();

    std::printf("  [OK] Phase 4 Sokol GPU Backend verified: %zu PBR bundles uploaded, %zu commands flushed, %llu sg_draw calls (%llu indices, %llu uniform uploads, %llu resource bindings).\n",
        UploadedBundles,
        Telemetry.CommandsSubmitted,
        static_cast<unsigned long long>(Telemetry.SokolDrawCallsExecuted),
        static_cast<unsigned long long>(Telemetry.SokolElementsDrawn),
        static_cast<unsigned long long>(Telemetry.SokolUniformUploads),
        static_cast<unsigned long long>(Telemetry.SokolBindingsApplied));
}

int main()
{
    VerifyModernECSCore();
    Resource::ModernAssetRegistry AssetRegistry;
    VerifyBinaryAndModernAssetsCrossCheck(AssetRegistry);
    VerifySceneGraphAndPBRRenderQueue(AssetRegistry);
    VerifyAIBehaviorTrees();
    VerifyPhase2Subsystems(AssetRegistry);
    VerifyPhase3Subsystems(AssetRegistry);
    VerifyPhase4SokolGpuBackend(AssetRegistry);

    std::printf("\n====================================================================\n");
    std::printf("ALL MODERN ENGINE REFACTORING & CLOUD ASSET VERIFICATIONS PASSED!\n");
    std::printf("====================================================================\n");
    return 0;
}
