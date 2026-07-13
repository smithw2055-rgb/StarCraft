#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/CombatSystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <utility>
#include <vector>

namespace Core::Audio
{
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags)
    {
        return -1;
    }
}

namespace Game::Systems
{
    bool CombatSystem::ResolvePendingAttackFrame(Core::ECS::EntityManager&,
                                                 BulletSystem&,
                                                 int)
    {
        return false;
    }
}

namespace
{
    constexpr int kDirectionalImage = 234;
    constexpr int kBattlecruiserImage = 218;
    constexpr int kBattlecruiserShadowImage = 219;
    constexpr int kGaussProjectileImage = 527;
    constexpr int kScvImage = 247;
    constexpr int kScvGlowImage = 249;
    constexpr int kScvSparkImage = 526;
    constexpr int kScvMiningWeapon = 14;
    constexpr int kGoliathChkUnit = 3;

    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    int FindDirectionalAttackImage()
    {
        Game::IScriptDatabase Database;
        Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
                "load iscript database for attack direction test");

        for (int ImageID = 0; ImageID < 999; ++ImageID)
        {
            const auto* ImageInfo = Database.FindImageInfo(ImageID);
            if (!ImageInfo || !ImageInfo->HasDirectionalFrames || ImageInfo->IScriptID < 0)
                continue;

            const auto* Script = Database.FindScript(ImageInfo->IScriptID);
            if (!Script ||
                Game::IScriptAnims::Anim_GndAttkInit >= (int)Script->AnimationPC.size() ||
                Game::IScriptAnims::Anim_Init >= (int)Script->AnimationPC.size() ||
                Script->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init] <= 0 ||
                Script->AnimationPC[(size_t)Game::IScriptAnims::Anim_GndAttkInit] <= 0)
            {
                continue;
            }

            return ImageID;
        }

        Require(false, "find directional image with ground attack iscript");
        return -1;
    }

    int FindBurrowImage()
    {
        Game::IScriptDatabase Database;
        Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
                "load iscript database for burrow transition test");

        for (int ImageID = 0; ImageID < 999; ++ImageID)
        {
            const auto* ImageInfo = Database.FindImageInfo(ImageID);
            if (!ImageInfo || ImageInfo->IScriptID < 0)
                continue;
            const auto* Script = Database.FindScript(ImageInfo->IScriptID);
            if (!Script)
                continue;
            const auto HasAnimation = [Script](int AnimationID) {
                return AnimationID >= 0 &&
                       AnimationID < (int)Script->AnimationPC.size() &&
                       Script->AnimationPC[(size_t)AnimationID] > 0;
            };
            if (HasAnimation(Game::IScriptAnims::Anim_Burrow) &&
                HasAnimation(Game::IScriptAnims::Anim_UnBurrow))
            {
                return ImageID;
            }
        }

        Require(false, "find project image with Burrow and UnBurrow iscript entries");
        return -1;
    }

    bool AnimationEmitsOrderSignal(const Game::IScriptDatabase& Database,
                                   const Game::IScriptProgram::Script& Script,
                                   int ImageID,
                                   int AnimationID,
                                   int SignalMask)
    {
        if (AnimationID < 0 || AnimationID >= (int)Script.AnimationPC.size() ||
            Script.AnimationPC[(size_t)AnimationID] <= 0)
        {
            return false;
        }
        Game::IScriptVM VM;
        VM.LoadScript(AnimationID, &Database.GetProgram().ProgramData,
                      Script.AnimationPC[(size_t)AnimationID]);
        VM.GetContext().ImageID = ImageID;
        for (int Step = 0; Step < 512 && VM.IsRunning(); ++Step)
        {
            VM.ExecuteFrame();
            for (const auto& Event : VM.ConsumeEvents())
                if (Event.Type == Game::IScriptEventType::OrderSignal &&
                    (Event.A & SignalMask) != 0)
                    return true;
        }
        return false;
    }

    int FindFlightTransitionImage()
    {
        Game::IScriptDatabase Database;
        Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
                "load iscript database for building flight transition test");
        for (int ImageID = 0; ImageID < 999; ++ImageID)
        {
            const auto* ImageInfo = Database.FindImageInfo(ImageID);
            const auto* Script = ImageInfo && ImageInfo->IScriptID >= 0
                ? Database.FindScript(ImageInfo->IScriptID)
                : nullptr;
            if (!Script)
                continue;
            if (AnimationEmitsOrderSignal(Database, *Script, ImageID,
                    Game::IScriptAnims::Anim_LiftOff, 0x10) &&
                AnimationEmitsOrderSignal(Database, *Script, ImageID,
                    Game::IScriptAnims::Anim_Landing, 0x10))
            {
                return ImageID;
            }
        }
        Require(false, "find project building image with signal-driven LiftOff and Landing entries");
        return -1;
    }

    int SpawnIdleDirectionalUnit(Core::ECS::EntityManager& EM,
                                 float FacingAngle,
                                 int TotalFrames = 256,
                                 int ImageID = kDirectionalImage)
    {
        const int EntityID = EM.CreateEntity();
        auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
        Trans.Position = {128.0f, 128.0f};

        auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.ImageID = (uint32_t)ImageID;
        Render.FrameIndex = 0;
        Render.TotalFrames = TotalFrames;

        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = kGoliathChkUnit;

        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
        Path.FacingAngle = FacingAngle;
        Path.CurrentSpeed = 0.0f;
        Path.CurrentVelocity = {0.0f, 0.0f};

        return EntityID;
    }

    int SpawnWalkingDirectionalUnit(Core::ECS::EntityManager& EM, float TargetX)
    {
        int EntityID = SpawnIdleDirectionalUnit(EM, 0.0f);
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        Require(Path && Trans, "walking test unit has transform and path components");
        Path->Path.clear();
        Path->Path.push_back({TargetX, Trans->Position.Y});
        Path->PathIndex = 0;
        Path->CurrentSpeed = 64.0f;
        Path->CurrentVelocity = {TargetX >= Trans->Position.X ? 64.0f : -64.0f, 0.0f};
        return EntityID;
    }

    int SpawnPendingAttackUnit(Core::ECS::EntityManager& EM,
                               int AttackImageID,
                               Core::Math::Vec2 TargetPos)
    {
        const int EntityID = EM.CreateEntity();
        auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
        Trans.Position = {128.0f, 128.0f};

        auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.ImageID = (uint32_t)AttackImageID;
        Render.FrameIndex = 0;
        Render.TotalFrames = 256;

        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = kGoliathChkUnit;

        auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
        Combat.PendingAttackFrame = true;
        Combat.PendingAttackTargetEntityID = -1;
        Combat.PendingAttackTargetPos = TargetPos;
        Combat.AttackCooldown = 1.0f;
        return EntityID;
    }
}

int main()
{
    const std::filesystem::path RepoRoot = std::filesystem::current_path();
    std::filesystem::current_path(RepoRoot / "build");
    const bool LoadedOutsideRepoRoot =
        Game::Systems::AnimationSystem::InitializeIScriptAssets(
            (RepoRoot / "assets" / "scripts" / "iscript.bin").string(),
            (RepoRoot / "assets" / "arr" / "images.dat").string());
    std::filesystem::current_path(RepoRoot);
    Require(LoadedOutsideRepoRoot,
            "absolute iscript asset paths work outside the repository working directory");
    Require(Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                kDirectionalImage, Game::IScriptAnims::Anim_Init),
            "goliath image has idle/init iscript");
    const int DirectionalAttackImage = FindDirectionalAttackImage();

    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();
    const int EastIdle = SpawnIdleDirectionalUnit(EM, 0.0f);
    const int WestIdle = SpawnIdleDirectionalUnit(EM, 3.14159265359f);

    Game::Systems::AnimationSystem Anim(SysMgr);

    int ScvWeaponVisualResolves = 0;
    Game::Systems::AnimationSystem::SetWeaponImageResolver(
        [&ScvWeaponVisualResolves](int WeaponID) -> std::optional<uint32_t> {
            Require(WeaponID == kScvMiningWeapon,
                "mineral mining resolves the Fusion Cutter visual");
            ++ScvWeaponVisualResolves;
            return (uint32_t)kScvSparkImage;
        });

    const int Mineral = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Mineral)
        .Position = {160.0f, 128.0f};
    auto& MineralResource = EM.AddComponent<Game::Components::ResourceComponent>(Mineral);
    MineralResource.Type = Game::Components::ResourceType::Minerals;
    MineralResource.Amount = 1500;

    const int MiningScv = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(MiningScv)
        .Position = {128.0f, 128.0f};
    auto& MiningScvRender = EM.AddComponent<Game::Components::RenderComponent>(MiningScv);
    MiningScvRender.ImageID = kScvImage;
    MiningScvRender.TotalFrames = 51;
    EM.AddComponent<Game::Components::UnitComponent>(MiningScv).ChkUnitID = 7;
    EM.AddComponent<Game::Components::PlayerComponent>(MiningScv).PlayerID = 0;
    EM.AddComponent<Game::Components::PathfindingComponent>(MiningScv).FacingAngle = 3.14159265359f;
    auto& MiningScvHarvest = EM.AddComponent<Game::Components::HarvestComponent>(MiningScv);
    MiningScvHarvest.State = Game::Components::HarvestState::Mining;
    MiningScvHarvest.TargetResourceID = Mineral;

    std::set<int> MiningScvFrames;
    std::set<int> MiningSparkFrames;
    bool SawSparkAtContactPoint = false;
    for (int Step = 0; Step < 96; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        MiningScvFrames.insert(MiningScvRender.FrameIndex);
        const auto* Tree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(MiningScv);
        if (!Tree)
            continue;
        for (const auto& Node : Tree->Nodes)
        {
            if (!Node.Visible || (int)Node.ImageID != kScvSparkImage)
                continue;
            MiningSparkFrames.insert(Node.FrameIndex);
            SawSparkAtContactPoint = SawSparkAtContactPoint ||
                (std::fabs(Node.CreationOffset.X - 20.0f) < 0.1f &&
                 std::fabs(Node.CreationOffset.Y) < 0.1f);
        }
    }
    Require(MiningScvFrames.size() > 1,
        "SCV mineral mining advances the real AlmostBuilt texture phases");
    Require(ScvWeaponVisualResolves >= 2,
        "SCV mineral mining repeatedly resolves Fusion Cutter sparks");
    Require(SawSparkAtContactPoint,
        "SCV mineral mining creates Fusion Cutter sparks twenty pixels toward the mineral");
    Require(MiningSparkFrames.size() > 1,
        "SCV Fusion Cutter spark advances through multiple project GRP frames");

    MiningScvHarvest.State = Game::Components::HarvestState::MovingToDepot;
    Anim.Update(1.0f / 24.0f);
    bool HasStaleMiningSpark = false;
    if (const auto* Tree = EM.GetComponent<Game::Components::ImageTreeComponent>(MiningScv))
    {
        for (const auto& Node : Tree->Nodes)
            HasStaleMiningSpark = HasStaleMiningSpark ||
                (Node.Visible && (int)Node.ImageID == kScvSparkImage);
    }
    Require(!HasStaleMiningSpark,
        "leaving mineral mining clears the transient Fusion Cutter spark");

    const int ImpactProbe = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(ImpactProbe)
        .Position = {176.0f, 96.0f};
    auto& ImpactRender = EM.AddComponent<Game::Components::RenderComponent>(ImpactProbe);
    ImpactRender.ImageID = kGaussProjectileImage;
    ImpactRender.TotalFrames = 1;
    ImpactRender.Layer = Game::Components::RenderLayer::Effects;
    EM.AddComponent<Game::Components::EffectComponent>(ImpactProbe).LifetimeRemaining = 1.0f;
    auto& ImpactUnit = EM.AddComponent<Game::Components::UnitComponent>(ImpactProbe);
    ImpactUnit.ChkUnitID = -1;
    ImpactUnit.HitPoints = 0;
    ImpactUnit.MaxHitPoints = 1;
    auto& ImpactPath = EM.AddComponent<Game::Components::PathfindingComponent>(ImpactProbe);
    ImpactPath.CurrentVelocity = {240.0f, 0.0f};
    ImpactPath.FacingAngle = 0.0f;
    std::set<std::pair<int, bool>> ImpactFrames;
    int ImpactOverlayCount = 0;
    for (int Step = 0; Step < 16 && EM.IsAlive(ImpactProbe); ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        const auto* CurrentRender =
            EM.GetComponent<Game::Components::RenderComponent>(ImpactProbe);
        if (CurrentRender)
            ImpactFrames.emplace(CurrentRender->FrameIndex, CurrentRender->Flipped);
        if (const auto* Tree =
                EM.GetComponent<Game::Components::ImageTreeComponent>(ImpactProbe))
        {
            for (const auto& Node : Tree->Nodes)
                if (Node.Visible)
                    ++ImpactOverlayCount;
        }
    }
    Require(ImpactFrames.size() > 1 || ImpactOverlayCount > 0,
            "real Gauss projectile Death iscript advances the transient impact visual");

    const int BurrowImageID = FindBurrowImage();
    const int BurrowProbe = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(BurrowProbe).Position = {192.0f, 192.0f};
    auto& BurrowProbeRender = EM.AddComponent<Game::Components::RenderComponent>(BurrowProbe);
    BurrowProbeRender.ImageID = (uint32_t)BurrowImageID;
    BurrowProbeRender.TotalFrames = 256;
    EM.AddComponent<Game::Components::UnitComponent>(BurrowProbe).ChkUnitID = 37;
    auto& BurrowProbeState = EM.AddComponent<Game::Components::BurrowComponent>(BurrowProbe);
    BurrowProbeState.CanBurrow = true;
    BurrowProbeState.Phase = Game::Components::BurrowPhase::Burrowing;

    for (int Step = 0;
         Step < 240 && BurrowProbeState.Phase != Game::Components::BurrowPhase::Burrowed;
         ++Step)
    {
        Anim.Update(1.0f / 24.0f);
    }
    auto* BurrowProbeCloak = EM.GetComponent<Game::Components::CloakComponent>(BurrowProbe);
    Require(BurrowProbeState.Phase == Game::Components::BurrowPhase::Burrowed &&
            BurrowProbeState.IsBurrowed && BurrowProbeCloak &&
            BurrowProbeCloak->IsBurrowed && BurrowProbeCloak->IsCloaked,
            "real project Burrow iscript signal completes the burrow state transition");

    BurrowProbeState.Phase = Game::Components::BurrowPhase::Unburrowing;
    for (int Step = 0;
         Step < 240 && BurrowProbeState.Phase != Game::Components::BurrowPhase::Surface;
         ++Step)
    {
        Anim.Update(1.0f / 24.0f);
    }
    Require(BurrowProbeState.Phase == Game::Components::BurrowPhase::Surface &&
            !BurrowProbeState.IsBurrowed && BurrowProbeCloak &&
            !BurrowProbeCloak->IsBurrowed && !BurrowProbeCloak->IsCloaked,
            "real project UnBurrow iscript signal completes the surface transition");

    const int FlightImageID = FindFlightTransitionImage();
    const int FlightProbe = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(FlightProbe).Position = {224.0f, 224.0f};
    auto& FlightRender = EM.AddComponent<Game::Components::RenderComponent>(FlightProbe);
    FlightRender.ImageID = (uint32_t)FlightImageID;
    FlightRender.TotalFrames = 256;
    EM.AddComponent<Game::Components::UnitComponent>(FlightProbe).ChkUnitID = 106;
    auto& FlightBuilding = EM.AddComponent<Game::Components::BuildingComponent>(FlightProbe);
    FlightBuilding.UnderConstruction = false;
    FlightBuilding.CanLiftOff = true;
    FlightBuilding.IsLifted = true;
    FlightBuilding.FlightPhase = Game::Components::BuildingFlightPhase::LiftingOff;
    for (int Step = 0; Step < 360 && !FlightBuilding.FlightAnimationComplete; ++Step)
        Anim.Update(1.0f / 24.0f);
    Require(FlightBuilding.FlightAnimationComplete,
            "real project LiftOff iscript emits the building flight completion signal");

    FlightBuilding.FlightAnimationComplete = false;
    FlightBuilding.FlightPhase = Game::Components::BuildingFlightPhase::Landing;
    for (int Step = 0; Step < 360 && !FlightBuilding.FlightAnimationComplete; ++Step)
        Anim.Update(1.0f / 24.0f);
    Require(FlightBuilding.FlightAnimationComplete,
            "real project Landing iscript emits the building flight completion signal");

    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(
        [FlightImageID](uint32_t ImageID, size_t LoType, size_t,
                        size_t OffsetIndex, bool Flipped)
            -> std::optional<Core::Math::Vec2> {
            if ((int)ImageID != FlightImageID || LoType != 1 || OffsetIndex != 0)
                return std::nullopt;
            return Core::Math::Vec2{Flipped ? -14.0f : 14.0f, -18.0f};
        });
    const int DamageProbe = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(DamageProbe)
        .Position = {288.0f, 224.0f};
    auto& DamageRender = EM.AddComponent<Game::Components::RenderComponent>(DamageProbe);
    DamageRender.ImageID = (uint32_t)FlightImageID;
    DamageRender.TotalFrames = 256;
    auto& DamageUnit = EM.AddComponent<Game::Components::UnitComponent>(DamageProbe);
    DamageUnit.ChkUnitID = 106;
    DamageUnit.MaxHitPoints = 1500;
    DamageUnit.HitPoints = 500;
    auto& DamageBuilding = EM.AddComponent<Game::Components::BuildingComponent>(DamageProbe);
    DamageBuilding.UnderConstruction = false;
    Anim.Update(1.0f / 24.0f);
    auto* DamageTree = EM.GetComponent<Game::Components::ImageTreeComponent>(DamageProbe);
    Game::Components::ImageSubnode* DamageNode = nullptr;
    if (DamageTree)
    {
        for (auto& Node : DamageTree->Nodes)
        {
            if (Node.Visible && Node.IsDamageOverlay)
            {
                DamageNode = &Node;
                break;
            }
        }
    }
    Require(DamageNode && DamageNode->ImageID == 472u &&
                DamageNode->OverlayOffsetType == 1 &&
                DamageNode->OverlayOffsetIndex == 0 &&
                DamageNode->Offset.X == 14.0f && DamageNode->Offset.Y == -18.0f,
            "damaged completed building attaches the project large-flame image at its damage LO slot");
    DamageUnit.HitPoints = DamageUnit.MaxHitPoints;
    Anim.Update(1.0f / 24.0f);
    bool HasVisibleDamageNode = false;
    DamageTree = EM.GetComponent<Game::Components::ImageTreeComponent>(DamageProbe);
    if (DamageTree)
    {
        for (const auto& Node : DamageTree->Nodes)
            HasVisibleDamageNode = HasVisibleDamageNode ||
                (Node.Visible && Node.IsDamageOverlay);
    }
    Require(!HasVisibleDamageNode,
            "repairing a building to full health removes its damage overlay");
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver({});

    const int BuildingProbe = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(BuildingProbe).Position = {256.0f, 256.0f};
    auto& BuildingProbeRender = EM.AddComponent<Game::Components::RenderComponent>(BuildingProbe);
    BuildingProbeRender.ImageID = 278;
    BuildingProbeRender.TotalFrames = 8;
    EM.AddComponent<Game::Components::UnitComponent>(BuildingProbe).ChkUnitID = 109;
    auto& BuildingProbeComponent = EM.AddComponent<Game::Components::BuildingComponent>(BuildingProbe);
    BuildingProbeComponent.UnderConstruction = false;
    int BuildingTreeMax = 0;
    int BuildingNodeFrameChanges = 0;
    int BuildingLastNodeFrame = -1;
    for (int ProbeStep = 0; ProbeStep < 240; ++ProbeStep)
    {
        Anim.Update(1.0f / 24.0f);
        auto* ProbeTree = EM.GetComponent<Game::Components::ImageTreeComponent>(BuildingProbe);
        if (!ProbeTree) continue;
        BuildingTreeMax = std::max(BuildingTreeMax, (int)ProbeTree->Nodes.size());
        for (const auto& ProbeNode : ProbeTree->Nodes)
        {
            if (!ProbeNode.Visible) continue;
            if (BuildingLastNodeFrame >= 0 && BuildingLastNodeFrame != ProbeNode.FrameIndex)
                ++BuildingNodeFrameChanges;
            BuildingLastNodeFrame = ProbeNode.FrameIndex;
            break;
        }
    }
    Require(BuildingTreeMax > 0,
            "real Supply Depot iscript creates its building overlay image");
    Require(BuildingNodeFrameChanges > 0,
            "real Supply Depot overlay advances through animated frames");
    Anim.Update(1.0f / 24.0f);

    const auto* EastRender = EM.GetComponent<Game::Components::RenderComponent>(EastIdle);
    const auto* WestRender = EM.GetComponent<Game::Components::RenderComponent>(WestIdle);
    Require(EastRender && WestRender, "idle directional units have render components");
    Require(EastRender->FrameIndex != WestRender->FrameIndex ||
                EastRender->Flipped != WestRender->Flipped,
            "idle units use facing direction for their standing frame");

    const int UnknownFrameCount = SpawnIdleDirectionalUnit(EM, 3.14159265359f, 1);
    Anim.Update(1.0f / 24.0f);
    auto* UnknownFrameRender = EM.GetComponent<Game::Components::RenderComponent>(UnknownFrameCount);
    Require(UnknownFrameRender && UnknownFrameRender->TotalFrames > 1,
            "iscript animation expands unknown render frame count before renderer backfills sprites");
    Require(UnknownFrameRender->FrameIndex > 0 || UnknownFrameRender->Flipped,
            "unknown frame count entity keeps the directional iscript frame instead of clamping to zero");

    auto* EastPath = EM.GetComponent<Game::Components::PathfindingComponent>(EastIdle);
    EastPath->FacingAngle = 3.14159265359f;
    EastPath->CurrentSpeed = 0.0f;
    EastPath->CurrentVelocity = {0.0f, 0.0f};
    const int BeforeFrame = EastRender->FrameIndex;
    const bool BeforeFlipped = EastRender->Flipped;
    Anim.Update(1.0f / 24.0f);

    Require(EastRender->FrameIndex != BeforeFrame || EastRender->Flipped != BeforeFlipped,
            "idle animation refreshes direction while the iscript is waiting");

    const int WalkEast = SpawnWalkingDirectionalUnit(EM, 192.0f);
    const int WalkWest = SpawnWalkingDirectionalUnit(EM, 64.0f);
    Anim.Update(1.0f / 24.0f);

    auto* WalkEastRender = EM.GetComponent<Game::Components::RenderComponent>(WalkEast);
    auto* WalkWestRender = EM.GetComponent<Game::Components::RenderComponent>(WalkWest);
    Require(WalkEastRender && WalkWestRender, "walking directional units have render components");
    Require(WalkEastRender->FrameIndex != WalkWestRender->FrameIndex ||
                WalkEastRender->Flipped != WalkWestRender->Flipped,
            "walking units use movement direction for their moving frame");
    std::set<int> WalkEastAnimationFrames;
    for (int Step = 0; Step < 48; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        WalkEastAnimationFrames.insert(WalkEastRender->FrameIndex);
    }
    Require(WalkEastAnimationFrames.size() > 1,
            "real Goliath walking iscript advances animation phases at one heading");

    const int Battlecruiser = SpawnIdleDirectionalUnit(
        EM, 0.0f, 256, kBattlecruiserImage);
    auto* BattlecruiserUnit = EM.GetComponent<Game::Components::UnitComponent>(Battlecruiser);
    auto* BattlecruiserPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(Battlecruiser);
    Require(BattlecruiserUnit && BattlecruiserPath,
        "Battlecruiser direction probe has unit and movement state");
    BattlecruiserUnit->ChkUnitID = 12;
    for (int I = 0; I < 8; ++I)
        Anim.Update(1.0f / 24.0f);

    auto* BattlecruiserTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(Battlecruiser);
    Require(BattlecruiserTree, "real Battlecruiser iscript creates an image tree");
    Game::Components::ImageSubnode* BattlecruiserShadow = nullptr;
    size_t BattlecruiserShadowIndex = 0;
    for (size_t I = 0; I < BattlecruiserTree->Nodes.size(); ++I)
    {
        auto& Node = BattlecruiserTree->Nodes[I];
        if (Node.Visible && (int)Node.ImageID == kBattlecruiserShadowImage)
        {
            BattlecruiserShadow = &Node;
            BattlecruiserShadowIndex = I;
            break;
        }
    }
    Require(BattlecruiserShadow && BattlecruiserShadow->InheritSpriteDirection &&
                BattlecruiserShadow->PersistentSpriteAttachment,
        "real Battlecruiser Init creates a persistent directional shadow image");
    for (const auto& Node : BattlecruiserTree->Nodes)
        Require(!Node.Visible || (int)Node.ImageID != 220,
            "idle Battlecruiser does not render its movement engine flame");
    const int EastShadowFrame = BattlecruiserShadow->FrameIndex;
    const bool EastShadowFlipped = BattlecruiserShadow->Flipped;

    BattlecruiserPath->Path = {{128.0f, 0.0f}};
    BattlecruiserPath->CurrentSpeed = 64.0f;
    BattlecruiserPath->CurrentVelocity = {0.0f, -64.0f};
    for (int I = 0; I < 2; ++I)
        Anim.Update(1.0f / 24.0f);
    Require(BattlecruiserShadowIndex < BattlecruiserTree->Nodes.size(),
        "Battlecruiser shadow keeps a stable image-tree slot while moving");
    BattlecruiserShadow = &BattlecruiserTree->Nodes[BattlecruiserShadowIndex];
    Require(BattlecruiserShadow->Visible &&
                (BattlecruiserShadow->FrameIndex != EastShadowFrame ||
                 BattlecruiserShadow->Flipped != EastShadowFlipped),
        "moving Battlecruiser shadow turns with the root sprite heading");
    Game::Components::ImageSubnode* BattlecruiserEngines = nullptr;
    for (auto& Node : BattlecruiserTree->Nodes)
    {
        if (Node.Visible && (int)Node.ImageID == 220)
        {
            BattlecruiserEngines = &Node;
            break;
        }
    }
    Require(BattlecruiserEngines && BattlecruiserEngines->InheritSpriteDirection,
        "walking Battlecruiser creates a directional movement engine flame");
    const auto& EngineContext = BattlecruiserEngines->VM.GetContext();
    Require((!EngineContext.HorPosSet ||
                BattlecruiserEngines->Offset.X == (float)EngineContext.HorPos) &&
            (!EngineContext.VertPosSet ||
                BattlecruiserEngines->Offset.Y == (float)EngineContext.VertPos),
        "engine flame applies its own iscript setpos offset instead of staying at the hull center");

    static const Core::Math::Vec2 EightDirections[8] = {
        {1.0f, 0.0f}, {0.7071f, 0.7071f}, {0.0f, 1.0f}, {-0.7071f, 0.7071f},
        {-1.0f, 0.0f}, {-0.7071f, -0.7071f}, {0.0f, -1.0f}, {0.7071f, -0.7071f}
    };
    std::vector<int> EightWayEntities;
    for (const auto& Direction : EightDirections)
    {
        const int EntityID = SpawnIdleDirectionalUnit(
            EM, std::atan2(Direction.Y, Direction.X), 51, 247);
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        Require(Path && Trans, "eight-way walking unit has movement components");
        Path->Path = {Trans->Position + Direction * 128.0f};
        Path->PathIndex = 0;
        Path->CurrentSpeed = 64.0f;
        Path->CurrentVelocity = Direction * 64.0f;
        EightWayEntities.push_back(EntityID);
    }
    bool SawScvWalkingAttachment = false;
    for (int Step = 0; Step < 48; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        const auto* EastTree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(EightWayEntities[0]);
        if (!EastTree)
            continue;
        for (const auto& Node : EastTree->Nodes)
        {
            if (Node.Visible && (int)Node.ImageID == kScvGlowImage)
                SawScvWalkingAttachment = true;
        }
    }
    Require(SawScvWalkingAttachment,
            "real SCV walking iscript keeps its movement attachment alive");
    std::set<std::pair<int, bool>> EightWaySignatures;
    std::set<std::pair<int, bool>> EightWayGlowSignatures;
    for (int EntityID : EightWayEntities)
    {
        const auto* Render = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
        Require(Render, "eight-way walking unit keeps its render component");
        EightWaySignatures.emplace(Render->FrameIndex, Render->Flipped);
        const auto* Tree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(EntityID);
        Require(Tree, "eight-way walking SCV keeps its image tree");
        bool FoundFollowingGlow = false;
        for (const auto& Node : Tree->Nodes)
        {
            if (!Node.Visible || (int)Node.ImageID != kScvGlowImage)
                continue;
            FoundFollowingGlow = true;
            Require(Node.InheritSpriteDirection,
                "SCV walking glow inherits the sprite direction");
            Require(Node.FrameIndex == Render->FrameIndex &&
                        Node.Flipped == Render->Flipped,
                "SCV walking glow keeps the main graphic direction and phase");
            EightWayGlowSignatures.emplace(Node.FrameIndex, Node.Flipped);
            break;
        }
        Require(FoundFollowingGlow,
            "each walking direction keeps a visible SCV glow attachment");
    }
    Require(EightWaySignatures.size() == 8,
            "real SCV walking iscript preserves all eight movement directions");
    Require(EightWayGlowSignatures.size() == 8,
            "SCV walking dynamic texture follows all eight movement directions");

    auto* ScvStopPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(EightWayEntities[0]);
    Require(ScvStopPath, "walking SCV keeps its movement state for stop transition");
    ScvStopPath->Path.clear();
    ScvStopPath->PathIndex = 0;
    ScvStopPath->CurrentSpeed = 0.0f;
    ScvStopPath->CurrentVelocity = {0.0f, 0.0f};
    for (int Step = 0; Step < 24; ++Step)
        Anim.Update(1.0f / 24.0f);
    bool HasStoppedScvGlow = false;
    if (const auto* Tree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(EightWayEntities[0]))
    {
        for (const auto& Node : Tree->Nodes)
            HasStoppedScvGlow = HasStoppedScvGlow ||
                (Node.Visible && (int)Node.ImageID == kScvGlowImage);
    }
    Require(!HasStoppedScvGlow,
        "SCV walking glow is removed after the unit settles to idle");

    auto* WalkEastPath = EM.GetComponent<Game::Components::PathfindingComponent>(WalkEast);
    Require(WalkEastPath, "walking unit still has path component");
    WalkEastPath->Path.clear();
    WalkEastPath->PathIndex = 0;
    WalkEastPath->CurrentSpeed = 0.0f;
    WalkEastPath->CurrentVelocity = {0.0f, 0.0f};
    for (int I = 0; I < 24; ++I)
        Anim.Update(1.0f / 24.0f);

    const int StoppedFrame = WalkEastRender->FrameIndex;
    const bool StoppedFlipped = WalkEastRender->Flipped;
    for (int I = 0; I < 4; ++I)
        Anim.Update(1.0f / 24.0f);
    Require(WalkEastRender->FrameIndex == StoppedFrame &&
                WalkEastRender->Flipped == StoppedFlipped,
            "stopped unit settles to a stable idle frame instead of continuing walking");

    const int AttackEast = SpawnPendingAttackUnit(EM, DirectionalAttackImage, {192.0f, 128.0f});
    const int AttackWest = SpawnPendingAttackUnit(EM, DirectionalAttackImage, {64.0f, 128.0f});
    Anim.Update(1.0f / 24.0f);

    auto* AttackEastRender = EM.GetComponent<Game::Components::RenderComponent>(AttackEast);
    auto* AttackWestRender = EM.GetComponent<Game::Components::RenderComponent>(AttackWest);
    Require(AttackEastRender && AttackWestRender,
            "pending attack units have render components");
    Require(AttackEastRender->FrameIndex != AttackWestRender->FrameIndex ||
                AttackEastRender->Flipped != AttackWestRender->Flipped,
            "pending attack animation uses target position direction when target entity is absent");

    const int FrozenIdleReference =
        SpawnIdleDirectionalUnit(EM, 0.0f, 256, DirectionalAttackImage);
    const int FrozenPendingAttack =
        SpawnPendingAttackUnit(EM, DirectionalAttackImage, {64.0f, 128.0f});
    EM.AddComponent<Game::Components::StatusEffectComponent>(FrozenPendingAttack)
        .AddEffect(Game::Components::StatusEffect::Stasis, 5.0f, -1);
    Anim.Update(1.0f / 24.0f);

    auto* FrozenIdleRender =
        EM.GetComponent<Game::Components::RenderComponent>(FrozenIdleReference);
    auto* FrozenAttackRender =
        EM.GetComponent<Game::Components::RenderComponent>(FrozenPendingAttack);
    Require(FrozenIdleRender && FrozenAttackRender,
            "frozen pending attack comparison units have render components");
    Require(FrozenAttackRender->FrameIndex == FrozenIdleRender->FrameIndex &&
                FrozenAttackRender->Flipped == FrozenIdleRender->Flipped,
            "frozen pending attack uses idle direction instead of attack target direction");

    const int ScriptedChildEntity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(ScriptedChildEntity)
        .Position = {160.0f, 160.0f};
    auto& Tree =
        EM.AddComponent<Game::Components::ImageTreeComponent>(ScriptedChildEntity);
    Tree.Nodes.emplace_back();
    auto& ChildNode = Tree.Nodes.back();
    ChildNode.ImageID = 500;
    ChildNode.Visible = true;
    ChildNode.AnimID = Game::IScriptAnims::Anim_Init;
    static const std::vector<int> EndProgram = {
        Game::IScriptOpcodes::Opc_PlayFram, 0,
        Game::IScriptOpcodes::Opc_End
    };
    ChildNode.VM.LoadScript(Game::IScriptAnims::Anim_Init, &EndProgram, 0);
    Anim.Update(1.0f / 24.0f);
    Require(!Tree.Nodes.empty() && !Tree.Nodes.front().Visible,
            "image tree child is hidden when its iscript reaches End");

    const int FollowMainEntity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(FollowMainEntity)
        .Position = {240.0f, 160.0f};
    auto& FollowMainRender =
        EM.AddComponent<Game::Components::RenderComponent>(FollowMainEntity);
    FollowMainRender.ImageID = 1000;
    FollowMainRender.FrameIndex = 37;
    FollowMainRender.TotalFrames = 128;
    FollowMainRender.Flipped = true;
    auto& FollowMainTree =
        EM.AddComponent<Game::Components::ImageTreeComponent>(FollowMainEntity);
    FollowMainTree.Nodes.emplace_back();
    auto& FollowMainNode = FollowMainTree.Nodes.back();
    FollowMainNode.ImageID = 1;
    FollowMainNode.Visible = true;
    FollowMainNode.AnimID = Game::IScriptAnims::Anim_Init;
    static const std::vector<int> FollowMainProgram = {
        Game::IScriptOpcodes::Opc_FollowMainGraphic,
        Game::IScriptOpcodes::Opc_Wait, 2,
        Game::IScriptOpcodes::Opc_End
    };
    FollowMainNode.VM.LoadScript(
        Game::IScriptAnims::Anim_Init, &FollowMainProgram, 0);
    Anim.Update(1.0f / 24.0f);
    Require(FollowMainNode.FollowsMainGraphic &&
                FollowMainNode.FrameIndex == FollowMainRender.FrameIndex &&
                FollowMainNode.Flipped == FollowMainRender.Flipped,
        "followmaingraphic child copies the rendered main frame and absolute flip");

    const int CompletedRefinery = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(CompletedRefinery)
        .Position = {320.0f, 160.0f};
    auto& RefineryRender =
        EM.AddComponent<Game::Components::RenderComponent>(CompletedRefinery);
    RefineryRender.ImageID = 307;
    RefineryRender.TotalFrames = 5;
    auto& RefineryUnit =
        EM.AddComponent<Game::Components::UnitComponent>(CompletedRefinery);
    RefineryUnit.ChkUnitID = 110;
    auto& RefineryBuilding =
        EM.AddComponent<Game::Components::BuildingComponent>(CompletedRefinery);
    RefineryBuilding.IsBuilding = true;
    RefineryBuilding.UnderConstruction = false;
    RefineryBuilding.BuildingDefID = 0;
    auto& RefineryResource =
        EM.AddComponent<Game::Components::ResourceComponent>(CompletedRefinery);
    RefineryResource.Type = Game::Components::ResourceType::VespeneGas;
    RefineryResource.Amount = 5000;
    Anim.Update(1.0f / 24.0f);
    Require(RefineryRender.FrameIndex == 0,
        "completed Refinery uses its Built frame instead of the fragmented Init frame");

    int ResolverCalls = 0;
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(
        [&ResolverCalls](uint32_t ImageID, size_t LoType, size_t Frame,
                         size_t OffsetIndex, bool Flipped)
            -> std::optional<Core::Math::Vec2> {
            Require(ImageID == 344, "gas overlay resolves offsets from the geyser main image");
            Require(LoType == 2, "gas overlay uses the special LO table");
            Require(Frame == 3, "gas overlay uses the current source image frame");
            Require(OffsetIndex == (ResolverCalls == 0 ? 2u : 1u),
                    "gas overlay passes the opcode smoke slot to the LO resolver");
            Require(Flipped == (ResolverCalls == 0),
                    "gas overlay preserves source horizontal flip state");
            ++ResolverCalls;
            return Core::Math::Vec2{Flipped ? -11.0f : 11.0f, -19.0f};
        });

    static const std::vector<int> ActiveGasProgram = {
        Game::IScriptOpcodes::Opc_PlayFram, 3,
        Game::IScriptOpcodes::Opc_CreateGasOverlays, 2,
        Game::IScriptOpcodes::Opc_Wait, 5,
        Game::IScriptOpcodes::Opc_End
    };
    const int ActiveGasEntity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(ActiveGasEntity);
    EM.AddComponent<Game::Components::ResourceComponent>(ActiveGasEntity).Amount = 5000;
    auto& ActiveGasTree = EM.AddComponent<Game::Components::ImageTreeComponent>(ActiveGasEntity);
    ActiveGasTree.Nodes.emplace_back();
    ActiveGasTree.Nodes[0].ImageID = 344;
    ActiveGasTree.Nodes[0].Flipped = true;
    ActiveGasTree.Nodes[0].Visible = true;
    ActiveGasTree.Nodes[0].VM.LoadScript(Game::IScriptAnims::Anim_Init, &ActiveGasProgram, 0);
    ActiveGasTree.Nodes[0].VM.GetContext().FlipState = true;

    static const std::vector<int> DepletedGasProgram = {
        Game::IScriptOpcodes::Opc_PlayFram, 3,
        Game::IScriptOpcodes::Opc_CreateGasOverlays, 1,
        Game::IScriptOpcodes::Opc_Wait, 5,
        Game::IScriptOpcodes::Opc_End
    };
    const int DepletedGasEntity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(DepletedGasEntity);
    EM.AddComponent<Game::Components::ResourceComponent>(DepletedGasEntity).Amount = 0;
    auto& DepletedGasTree =
        EM.AddComponent<Game::Components::ImageTreeComponent>(DepletedGasEntity);
    DepletedGasTree.Nodes.emplace_back();
    DepletedGasTree.Nodes[0].ImageID = 344;
    DepletedGasTree.Nodes[0].Visible = true;
    DepletedGasTree.Nodes[0].VM.LoadScript(
        Game::IScriptAnims::Anim_Init, &DepletedGasProgram, 0);

    Anim.Update(1.0f / 24.0f);
    Require(ResolverCalls == 2, "both gas resource states resolve LO offsets");
    Require(ActiveGasTree.Nodes.size() >= 2 &&
                ActiveGasTree.Nodes[1].ImageID == 432 &&
                ActiveGasTree.Nodes[1].ParentIndex == 0 &&
                ActiveGasTree.Nodes[1].Offset.X == -11.0f &&
                ActiveGasTree.Nodes[1].Offset.Y == -19.0f,
            "active geyser creates the selected smoke image at the flipped LO offset");
    Require(DepletedGasTree.Nodes.size() >= 2 &&
                DepletedGasTree.Nodes[1].ImageID == 436 &&
                DepletedGasTree.Nodes[1].ParentIndex == 0 &&
                DepletedGasTree.Nodes[1].Offset.X == 11.0f &&
                DepletedGasTree.Nodes[1].Offset.Y == -19.0f,
            "depleted geyser creates the selected overlay image at the LO offset");

    static const std::vector<int> FlightProgram = {
        Game::IScriptOpcodes::Opc_SetFlDirect, 3,
        Game::IScriptOpcodes::Opc_SetFlSpeed, 0x100,
        Game::IScriptOpcodes::Opc_Wait, 5,
        Game::IScriptOpcodes::Opc_End
    };
    const int FlightEntity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(FlightEntity);
    auto& FlightPath = EM.AddComponent<Game::Components::PathfindingComponent>(FlightEntity);
    auto& FlightTree = EM.AddComponent<Game::Components::ImageTreeComponent>(FlightEntity);
    FlightTree.Nodes.emplace_back();
    FlightTree.Nodes[0].ImageID = 500;
    FlightTree.Nodes[0].Visible = true;
    FlightTree.Nodes[0].VM.LoadScript(Game::IScriptAnims::Anim_Init, &FlightProgram, 0);
    Anim.Update(1.0f / 24.0f);
    Require(std::fabs(FlightPath.FacingAngle) < 0.0001f &&
                std::fabs(FlightPath.TargetSpeed) < 0.0001f,
            "iscript flight opcodes do not overwrite movement-system direction or speed");

    Game::Systems::AnimationSystem::SetWeaponImageResolver({});

    std::printf("AnimationRuntimeDirectionTest passed\n");
    return 0;
}
