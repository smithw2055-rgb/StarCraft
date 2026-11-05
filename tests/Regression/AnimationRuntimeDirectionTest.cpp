#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BulletSystem.h"
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
    bool gResolvePendingAttackFrameResult = false;
    int gIScriptDamageFrameCalls = 0;

    void BulletSystem::Update(float)
    {
    }

    bool BulletSystem::ResolveIScriptDamageFrame(int BulletEntityID)
    {
        const auto* Bullet = mSystemMgr.GetEntityManager()
            .GetComponent<Game::Components::BulletComponent>(BulletEntityID);
        if (!Bullet || !Bullet->DamageDrivenByIScript)
            return false;
        ++gIScriptDamageFrameCalls;
        return true;
    }

    bool CombatSystem::ResolvePendingAttackFrame(Core::ECS::EntityManager& EM,
                                                 BulletSystem&,
                                                 int EntityID,
                                                 float /*ForwardOffsetPixels*/)
    {
        if (!gResolvePendingAttackFrameResult)
            return false;
        auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(EntityID);
        if (!Combat || !Combat->PendingAttackFrame)
            return false;
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackTargetEntityID = -1;
        Combat->PendingAttackDamage = 0;
        return true;
    }
}

namespace
{
    constexpr int kDirectionalImage = 234;
    constexpr int kBattlecruiserImage = 218;
    constexpr int kBattlecruiserShadowImage = 219;
    constexpr int kScienceVesselImage = 260;
    constexpr int kScienceVesselTransitionImage = 261;
    constexpr int kScienceVesselShadowImage = 262;
    constexpr int kWraithImage = 243;
    constexpr int kWraithShadowImage = 244;
    constexpr int kValkyrieImage = 939;
    constexpr int kValkyrieShadowImage = 940;
    constexpr int kMarineImage = 239;
    constexpr int kDragoonImage = 122;
    constexpr int kGaussProjectileImage = 527;
    constexpr int kScvImage = 247;
    constexpr int kScvGlowImage = 249;
    constexpr int kScvSparkImage = 526;
    constexpr int kTankBaseImage = 250;
    constexpr int kTankTurretImage = 251;
    constexpr int kTankMuzzleImage = 536;
    constexpr int kSiegeTankBaseImage = 253;
    constexpr int kSiegeTankTurretImage = 254;
    constexpr int kScvMiningWeapon = 14;
    constexpr int kGoliathChkUnit = 3;
    constexpr int kCommandCenterImage = 275;
    constexpr int kCommandCenterControlImage = 276;
    constexpr int kBarracksImage = 266;
    constexpr int kSupplyDepotImage = 278;
    constexpr int kSupplyDepotMachineryImage = 279;
    constexpr int kEngineeringBayImage = 322;
    constexpr int kEngineeringBayMachineryImage = 323;
    constexpr int kComsatStationImage = 271;
    constexpr int kComsatConnectorImage = 272;
    constexpr int kHydraliskImage = 29;
    constexpr int kHydraliskShadowImage = 30;
    constexpr int kHydraliskNeedleImage = 35;
    constexpr int kHydraliskNeedleSprite = 332;
    constexpr int kHydraliskNeedleHitImage = 504;
    constexpr int kLurkerImage = 921;
    constexpr int kLurkerShadowImage = 922;
    constexpr int kLurkerSpineImage = 961;
    constexpr int kLurkerSpineSprite = 511;
    constexpr int kLurkerSpineControllerImage = 985;

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

    struct TimedAnimationImage
    {
        int ImageID = -1;
        int Frames = 0;
    };

    TimedAnimationImage FindFiniteAnimationImage(int AnimationID,
                                                 int MinimumFrames)
    {
        Game::IScriptDatabase Database;
        Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
                "load iscript database for timed effect lifecycle test");
        for (int ImageID = 0; ImageID < 999; ++ImageID)
        {
            const auto* Info = Database.FindImageInfo(ImageID);
            const auto* Script = Info && Info->IScriptID >= 0
                ? Database.FindScript(Info->IScriptID)
                : nullptr;
            if (!Script || AnimationID < 0 ||
                AnimationID >= (int)Script->AnimationPC.size() ||
                Script->AnimationPC[(size_t)AnimationID] <= 0)
            {
                continue;
            }

            Game::IScriptVM VM;
            VM.LoadScript(AnimationID, &Database.GetProgram().ProgramData,
                          Script->AnimationPC[(size_t)AnimationID]);
            VM.GetContext().ImageID = ImageID;
            int Frames = 0;
            for (; Frames < 180 && VM.IsRunning(); ++Frames)
            {
                VM.ExecuteFrame();
                VM.ConsumeEvents();
            }
            if (!VM.IsRunning() && Frames >= MinimumFrames)
                return {ImageID, Frames};
        }
        Require(false, "find finite project iscript animation with requested duration");
        return {};
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

    struct DelayedProjectileLaunch
    {
        int ImageID = -1;
        int SignalFrame = 0;
    };

    struct AuthoredAttackTrace
    {
        std::vector<int> Frames;
        int AttackEventStep = -1;
    };

    AuthoredAttackTrace TraceRealGroundAttack(int ImageID, int AnimationID)
    {
        Game::IScriptDatabase Database;
        Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
            "load real unit attack iscript");
        const auto* ImageInfo = Database.FindImageInfo(ImageID);
        const auto* Script = ImageInfo && ImageInfo->IScriptID >= 0
            ? Database.FindScript(ImageInfo->IScriptID) : nullptr;
        Require(Script && AnimationID >= 0 &&
                AnimationID < (int)Script->AnimationPC.size() &&
                Script->AnimationPC[(size_t)AnimationID] > 0,
            "real unit exposes requested ground attack animation");

        Game::IScriptVM VM;
        VM.LoadScript(AnimationID, &Database.GetProgram().ProgramData,
                      Script->AnimationPC[(size_t)AnimationID]);
        auto& Context = VM.GetContext();
        Context.ImageID = ImageID;
        Context.HasDirectionalFrames = ImageInfo->HasDirectionalFrames;
        Context.CurrentDirection = 64; // east in Brood War's 256-direction space
        Context.FlDirect = 64;
        VM.RefreshDirectionalFrame();

        AuthoredAttackTrace Result;
        for (int Step = 0; Step < 128 && VM.IsRunning(); ++Step)
        {
            VM.ExecuteFrame();
            Result.Frames.push_back(VM.GetCurrentFrame());
            for (const auto& Event : VM.ConsumeEvents())
            {
                if (Event.Type == Game::IScriptEventType::Attack ||
                    Event.Type == Game::IScriptEventType::AttackMelee ||
                    Event.Type == Game::IScriptEventType::UseWeapon)
                {
                    Result.AttackEventStep = Step;
                }
            }
        }
        Require(!Result.Frames.empty() && Result.AttackEventStep >= 0 &&
                Result.AttackEventStep + 1 < (int)Result.Frames.size(),
            "real attack has authored recovery frames after its fire opcode");
        return Result;
    }

    DelayedProjectileLaunch FindDelayedProjectileLaunch()
    {
        Game::IScriptDatabase Database;
        Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
                "load iscript database for projectile Init launch gate test");

        // 526..600 is the canonical weapon-image band beginning with Fusion
        // Cutter/Gauss Rifle and containing the Terran, Zerg, and Protoss
        // projectile sprites used by weapons.dat.
        for (int ImageID = 526; ImageID <= 600; ++ImageID)
        {
            const auto* ImageInfo = Database.FindImageInfo(ImageID);
            const auto* Script = ImageInfo && ImageInfo->IScriptID >= 0
                ? Database.FindScript(ImageInfo->IScriptID)
                : nullptr;
            if (!Script ||
                Game::IScriptAnims::Anim_Init >=
                    (int)Script->AnimationPC.size() ||
                Script->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init] <= 0)
            {
                continue;
            }

            Game::IScriptVM VM;
            VM.LoadScript(Game::IScriptAnims::Anim_Init,
                          &Database.GetProgram().ProgramData,
                          Script->AnimationPC[
                              (size_t)Game::IScriptAnims::Anim_Init]);
            VM.GetContext().ImageID = ImageID;
            for (int Frame = 1; Frame <= 128 && VM.IsRunning(); ++Frame)
            {
                VM.ExecuteFrame();
                for (const auto& Event : VM.ConsumeEvents())
                {
                    if (Event.Type == Game::IScriptEventType::OrderSignal &&
                        (Event.A & 1) != 0 && Frame > 1)
                    {
                        return {ImageID, Frame};
                    }
                }
            }
        }

        Require(false,
            "find a real projectile Init that waits before sigorder 1");
        return {};
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

    Game::Systems::BulletSystem Bullet(SysMgr);
    Game::Systems::AnimationSystem Anim(SysMgr, nullptr, &Bullet);

    const auto DelayedProjectile = FindDelayedProjectileLaunch();
    const int LaunchGateBullet = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(LaunchGateBullet)
        .Position = {48.0f, 48.0f};
    auto& LaunchGateRender =
        EM.AddComponent<Game::Components::RenderComponent>(LaunchGateBullet);
    LaunchGateRender.ImageID = (uint32_t)DelayedProjectile.ImageID;
    LaunchGateRender.TotalFrames = 256;
    auto& LaunchGateState =
        EM.AddComponent<Game::Components::BulletComponent>(LaunchGateBullet);
    LaunchGateState.AwaitingLaunchSignal = true;
    LaunchGateState.Behavior = Game::Components::WeaponBehavior::FlyDirect;
    LaunchGateState.Velocity = {160.0f, 0.0f};
    auto& LaunchGatePath =
        EM.AddComponent<Game::Components::PathfindingComponent>(LaunchGateBullet);
    LaunchGatePath.CurrentVelocity = LaunchGateState.Velocity;
    for (int Frame = 1; Frame < DelayedProjectile.SignalFrame; ++Frame)
    {
        Anim.Update(1.0f / 24.0f);
        Require(LaunchGateState.AwaitingLaunchSignal,
            "projectile remains launch-gated across every pre-sigorder Init frame");
    }
    Anim.Update(1.0f / 24.0f);
    Require(!LaunchGateState.AwaitingLaunchSignal,
        "real delayed projectile Init sigorder 1 releases the launch gate");
    EM.DestroyEntity(LaunchGateBullet);

    Game::Systems::AnimationSystem::SetSpriteImageResolver(
        [](int SpriteID) -> std::optional<uint32_t> {
            if (SpriteID == kLurkerSpineSprite)
                return (uint32_t)kLurkerSpineImage;
            if (SpriteID == kHydraliskNeedleSprite)
                return (uint32_t)kHydraliskNeedleImage;
            return std::nullopt;
        });

    // Subterranean Spines is a moving, invisible bullet controller. Its real
    // Init entry releases movement with sigorder 1 and then authors one
    // independent sprite 511 spike every two simulation frames.
    const int LurkerSpineController = EM.CreateEntity();
    auto& LurkerSpineTrans =
        EM.AddComponent<Game::Components::TransformComponent>(LurkerSpineController);
    LurkerSpineTrans.Position = {32.0f, 256.0f};
    auto& LurkerSpineRender =
        EM.AddComponent<Game::Components::RenderComponent>(LurkerSpineController);
    LurkerSpineRender.ImageID = kLurkerSpineControllerImage;
    LurkerSpineRender.TotalFrames = 256;
    auto& LurkerSpineBullet =
        EM.AddComponent<Game::Components::BulletComponent>(LurkerSpineController);
    LurkerSpineBullet.AwaitingLaunchSignal = true;
    LurkerSpineBullet.Behavior = Game::Components::WeaponBehavior::FlyDirect;
    LurkerSpineBullet.Velocity = {450.0f, 0.0f};
    LurkerSpineBullet.DamageDrivenByIScript = true;
    Game::Systems::gIScriptDamageFrameCalls = 0;
    auto& LurkerSpinePath =
        EM.AddComponent<Game::Components::PathfindingComponent>(LurkerSpineController);
    LurkerSpinePath.CurrentVelocity = LurkerSpineBullet.Velocity;
    std::set<int> SeenLurkerSpines;
    std::vector<float> LurkerSpinePositions;
    for (int Frame = 0; Frame < 16; ++Frame)
    {
        if (!LurkerSpineBullet.AwaitingLaunchSignal)
            LurkerSpineTrans.Position = LurkerSpineTrans.Position +
                LurkerSpineBullet.Velocity * (1.0f / 24.0f);
        Anim.Update(1.0f / 24.0f);
        for (int EffectID :
             EM.GetEntitiesWith<Game::Components::EffectComponent,
                                Game::Components::TransformComponent,
                                Game::Components::RenderComponent>())
        {
            const auto* Effect =
                EM.GetComponent<Game::Components::EffectComponent>(EffectID);
            if (!Effect || Effect->ParentEntityID != LurkerSpineController ||
                !SeenLurkerSpines.insert(EffectID).second)
            {
                continue;
            }
            const auto* EffectTrans =
                EM.GetComponent<Game::Components::TransformComponent>(EffectID);
            const auto* EffectRender =
                EM.GetComponent<Game::Components::RenderComponent>(EffectID);
            Require(EffectTrans && EffectRender &&
                    EffectRender->ImageID == (uint32_t)kLurkerSpineImage &&
                    Effect->IScriptDirection == 64 && !Effect->FollowParent,
                "Lurker controller creates east-facing independent authored spine effects");
            LurkerSpinePositions.push_back(EffectTrans->Position.X);
        }
    }
    Require(LurkerSpinePositions.size() >= 4,
        "real Subterranean Spines Init emits a visible multi-segment attack chain");
    Require(Game::Systems::gIScriptDamageFrameCalls >= 4,
        "each authored Lurker dogrddamage event advances local damage with the spine chain");
    for (size_t I = 1; I < LurkerSpinePositions.size(); ++I)
        Require(LurkerSpinePositions[I] > LurkerSpinePositions[I - 1] + 20.0f,
            "successive Lurker spikes advance along the attack ray");
    for (int EffectID : SeenLurkerSpines)
        if (EM.IsAlive(EffectID))
            EM.DestroyEntity(EffectID);
    EM.DestroyEntity(LurkerSpineController);

    // Verify Lurker burrowed attack lifecycle never produces blood/splat overlays (e.g. Image 484).
    // Image 484 is Lurker's unit Death overlay, which must only be emitted when a real Lurker dies.
    constexpr int kZergDeathBloodImage = 484;
    for (int ExistingID : EM.GetEntitiesWith<Game::Components::RenderComponent>())
    {
        const auto* ExistingRender = EM.GetComponent<Game::Components::RenderComponent>(ExistingID);
        Require(!ExistingRender || ExistingRender->ImageID != (uint32_t)kZergDeathBloodImage,
            "Lurker spine controller and attack chain must not emit death blood splat overlays");
        if (const auto* Tree = EM.GetComponent<Game::Components::ImageTreeComponent>(ExistingID))
        {
            for (const auto& Node : Tree->Nodes)
            {
                Require(Node.ImageID != (uint32_t)kZergDeathBloodImage,
                    "Lurker spine attack nodes must never attach death blood splat overlays");
            }
        }
    }

    Game::Systems::AnimationSystem::SetSpriteImageResolver(
        [](int SpriteID) -> std::optional<uint32_t> {
            if (SpriteID == kLurkerSpineSprite)
                return (uint32_t)kLurkerSpineImage;
            if (SpriteID == kHydraliskNeedleSprite)
                return (uint32_t)kHydraliskNeedleImage;
            if (SpriteID == kZergDeathBloodImage)
                return (uint32_t)kZergDeathBloodImage;
            return std::nullopt;
        });

    // Now verify that a real Lurker unit death visual *only* emits blood overlay 484 upon actual Death.
    const int DyingLurker = EM.CreateEntity();
    auto& DyingLurkerTrans = EM.AddComponent<Game::Components::TransformComponent>(DyingLurker);
    DyingLurkerTrans.Position = {100.0f, 100.0f};
    auto& DyingLurkerRender = EM.AddComponent<Game::Components::RenderComponent>(DyingLurker);
    DyingLurkerRender.ImageID = kLurkerImage;
    DyingLurkerRender.TotalFrames = 256;
    auto& DyingLurkerEffect = EM.AddComponent<Game::Components::EffectComponent>(DyingLurker);
    DyingLurkerEffect.LifetimeRemaining = 8.0f;
    DyingLurkerEffect.InitialAnimation = Game::IScriptAnims::Anim_Death;
    DyingLurkerEffect.DestroyWhenAnimationCompletes = true;
    auto& DyingLurkerUnit = EM.AddComponent<Game::Components::UnitComponent>(DyingLurker);
    DyingLurkerUnit.ChkUnitID = 103;
    DyingLurkerUnit.HitPoints = 0;

    bool FoundBloodFromDeath = false;
    for (int Frame = 0; Frame < 24; ++Frame)
    {
        Anim.Update(1.0f / 24.0f);
        for (int AnyID : EM.GetEntitiesWith<Game::Components::RenderComponent>())
        {
            const auto* AnyRender = EM.GetComponent<Game::Components::RenderComponent>(AnyID);
            if (AnyRender && AnyRender->ImageID == (uint32_t)kZergDeathBloodImage)
                FoundBloodFromDeath = true;
        }
        if (const auto* Tree = EM.GetComponent<Game::Components::ImageTreeComponent>(DyingLurker))
        {
            for (const auto& Node : Tree->Nodes)
            {
                if (Node.ImageID == (uint32_t)kZergDeathBloodImage)
                    FoundBloodFromDeath = true;
            }
        }
        if (FoundBloodFromDeath)
            break;
    }
    Require(FoundBloodFromDeath,
        "Lurker unit correctly emits death blood splat only upon real unit annihilation");
    if (EM.IsAlive(DyingLurker))
        EM.DestroyEntity(DyingLurker);

    // Hydralisk Needle Spines are not a travelling bullet in Brood War.
    // GndAttkInit first emits sprite 332 through the attack LO table, then
    // weapon 38 creates image 504 directly on the target for its hit script.
    int HydraliskMuzzleLoFrame = -1;
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(
        [&HydraliskMuzzleLoFrame](uint32_t ImageID, size_t LoType,
           size_t SourceFrame, size_t OffsetIndex, bool)
            -> std::optional<Core::Math::Vec2> {
            if (ImageID == (uint32_t)kHydraliskImage &&
                LoType == 0 && OffsetIndex == 0)
            {
                HydraliskMuzzleLoFrame = (int)SourceFrame;
                return Core::Math::Vec2{12.0f, -6.0f};
            }
            return std::nullopt;
        });
    const int HydraliskAttack = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(HydraliskAttack)
        .Position = {96.0f, 304.0f};
    auto& HydraliskRender =
        EM.AddComponent<Game::Components::RenderComponent>(HydraliskAttack);
    HydraliskRender.ImageID = kHydraliskImage;
    HydraliskRender.TotalFrames = 256;
    auto& HydraliskUnit =
        EM.AddComponent<Game::Components::UnitComponent>(HydraliskAttack);
    HydraliskUnit.ChkUnitID = 38;
    HydraliskUnit.HitPoints = 80;
    HydraliskUnit.MaxHitPoints = 80;
    auto& HydraliskCombat =
        EM.AddComponent<Game::Components::CombatComponent>(HydraliskAttack);
    HydraliskCombat.PendingAttackFrame = true;
    HydraliskCombat.PendingAttackTargetPos = {224.0f, 304.0f};
    HydraliskCombat.AttackCooldown = 0.625f;
    int HydraliskNeedleEffect = -1;
    for (int Frame = 0; Frame < 24 && HydraliskNeedleEffect < 0; ++Frame)
    {
        Anim.Update(1.0f / 24.0f);
        for (int EffectID :
             EM.GetEntitiesWith<Game::Components::EffectComponent,
                                Game::Components::RenderComponent>())
        {
            const auto* Effect =
                EM.GetComponent<Game::Components::EffectComponent>(EffectID);
            const auto* EffectRender =
                EM.GetComponent<Game::Components::RenderComponent>(EffectID);
            if (Effect && EffectRender &&
                Effect->ParentEntityID == HydraliskAttack &&
                EffectRender->ImageID == (uint32_t)kHydraliskNeedleImage)
            {
                HydraliskNeedleEffect = EffectID;
                break;
            }
        }
    }
    const auto* HydraliskNeedle = HydraliskNeedleEffect >= 0
        ? EM.GetComponent<Game::Components::EffectComponent>(HydraliskNeedleEffect)
        : nullptr;
    const auto* HydraliskNeedleTrans = HydraliskNeedleEffect >= 0
        ? EM.GetComponent<Game::Components::TransformComponent>(HydraliskNeedleEffect)
        : nullptr;
    Require(HydraliskNeedle && HydraliskNeedleTrans &&
            HydraliskMuzzleLoFrame == 76 &&
            HydraliskNeedle->IScriptDirection == 64 &&
            std::fabs(HydraliskNeedleTrans->Position.X - 108.0f) < 0.001f &&
            std::fabs(HydraliskNeedleTrans->Position.Y - 298.0f) < 0.001f,
        "Hydralisk GndAttkInit uses the freshly-authored frame for its directional LO offset");
    if (HydraliskNeedleEffect >= 0 && EM.IsAlive(HydraliskNeedleEffect))
        EM.DestroyEntity(HydraliskNeedleEffect);
    EM.DestroyEntity(HydraliskAttack);

    const int HydraliskHitBullet = EM.CreateEntity();
    auto& HydraliskHitTrans =
        EM.AddComponent<Game::Components::TransformComponent>(HydraliskHitBullet);
    HydraliskHitTrans.Position = {208.0f, 304.0f};
    auto& HydraliskHitRender =
        EM.AddComponent<Game::Components::RenderComponent>(HydraliskHitBullet);
    HydraliskHitRender.ImageID = kHydraliskNeedleHitImage;
    HydraliskHitRender.TotalFrames = 256;
    auto& HydraliskHitBulletState =
        EM.AddComponent<Game::Components::BulletComponent>(HydraliskHitBullet);
    HydraliskHitBulletState.AwaitingLaunchSignal = true;
    HydraliskHitBulletState.Behavior =
        Game::Components::WeaponBehavior::AppearOnTarget;
    HydraliskHitBulletState.Velocity = {10.0f, 0.0f};
    auto& HydraliskHitPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(HydraliskHitBullet);
    HydraliskHitPath.CurrentVelocity = HydraliskHitBulletState.Velocity;
    for (int Frame = 0;
         Frame < 8 && HydraliskHitBulletState.AwaitingLaunchSignal; ++Frame)
    {
        Anim.Update(1.0f / 24.0f);
    }
    Require(!HydraliskHitBulletState.AwaitingLaunchSignal &&
            HydraliskHitTrans.Position.X == 208.0f &&
            HydraliskHitTrans.Position.Y == 304.0f,
        "Needle Spine Hit Init holds at the target until its sigorder 1 launch signal");
    const Core::Math::Vec2 HydraliskHitPosition = HydraliskHitTrans.Position;
    EM.DestroyEntity(HydraliskHitBullet);

    const int HydraliskHitDeath = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(HydraliskHitDeath)
        .Position = HydraliskHitPosition;
    auto& HydraliskHitDeathRender =
        EM.AddComponent<Game::Components::RenderComponent>(HydraliskHitDeath);
    HydraliskHitDeathRender.ImageID = kHydraliskNeedleHitImage;
    HydraliskHitDeathRender.TotalFrames = 256;
    auto& HydraliskHitDeathUnit =
        EM.AddComponent<Game::Components::UnitComponent>(HydraliskHitDeath);
    HydraliskHitDeathUnit.ChkUnitID = -1;
    HydraliskHitDeathUnit.HitPoints = 0;
    HydraliskHitDeathUnit.MaxHitPoints = 1;
    auto& HydraliskHitDeathEffect =
        EM.AddComponent<Game::Components::EffectComponent>(HydraliskHitDeath);
    HydraliskHitDeathEffect.LifetimeRemaining = 8.0f;
    HydraliskHitDeathEffect.InitialAnimation = Game::IScriptAnims::Anim_Death;
    HydraliskHitDeathEffect.DestroyWhenAnimationCompletes = true;
    Anim.Update(1.0f / 24.0f);
    Require(EM.IsAlive(HydraliskHitDeath),
        "Needle Spine Hit impact enters its authored Death presentation state");
    for (int Frame = 0; Frame < 32 && EM.IsAlive(HydraliskHitDeath); ++Frame)
        Anim.Update(1.0f / 24.0f);
    Require(!EM.IsAlive(HydraliskHitDeath),
        "Needle Spine Hit Death completes without leaving a static projectile remnant");

    Game::Systems::AnimationSystem::SetImageAttachmentPersistenceResolver(
        [](uint32_t ImageID) {
            return ImageID == kBattlecruiserShadowImage ||
                   ImageID == kScienceVesselShadowImage ||
                   ImageID == kWraithShadowImage ||
                   ImageID == kValkyrieShadowImage ||
                   ImageID == kLurkerShadowImage;
        });

    int ScvWeaponVisualResolves = 0;
    Game::Systems::AnimationSystem::SetWeaponImageResolver(
        [&ScvWeaponVisualResolves](int WeaponID) -> std::optional<uint32_t> {
            Require(WeaponID == kScvMiningWeapon,
                "mineral mining resolves the Fusion Cutter visual");
            ++ScvWeaponVisualResolves;
            return (uint32_t)kScvSparkImage;
        });

    // A node iscript may append an overlay while UpdateImageTrees is visiting
    // that source node. Force the append to reallocate so the regression proves
    // the source is reacquired by index after the event rather than accessed
    // through a dangling vector reference.
    static const std::vector<int> GrowingOverlayProgram = {
        Game::IScriptOpcodes::Opc_PlayFram, 0,
        Game::IScriptOpcodes::Opc_ImgOl, 501, 3, -2,
        Game::IScriptOpcodes::Opc_Wait, 2,
        Game::IScriptOpcodes::Opc_End
    };
    const int GrowingOverlayEntity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(GrowingOverlayEntity)
        .Position = {64.0f, 64.0f};
    auto& GrowingOverlayTree =
        EM.AddComponent<Game::Components::ImageTreeComponent>(GrowingOverlayEntity);
    GrowingOverlayTree.Nodes =
        std::vector<Game::Components::ImageSubnode>(1);
    {
        auto& GrowingOverlaySource = GrowingOverlayTree.Nodes.front();
        GrowingOverlaySource.ImageID = 500;
        GrowingOverlaySource.Visible = true;
        GrowingOverlaySource.AnimID = Game::IScriptAnims::Anim_Init;
        GrowingOverlaySource.VM.LoadScript(
            Game::IScriptAnims::Anim_Init, &GrowingOverlayProgram, 0);
    }
    const size_t GrowingCapacityBefore = GrowingOverlayTree.Nodes.capacity();
    Require(GrowingCapacityBefore == GrowingOverlayTree.Nodes.size(),
        "overlay growth regression starts with a tight image-node vector");
    Anim.Update(1.0f / 24.0f);
    Require(GrowingOverlayTree.Nodes.capacity() > GrowingCapacityBefore,
        "node-created overlay forces image-tree vector storage to grow");
    Require(GrowingOverlayTree.Nodes.size() == 2 &&
                GrowingOverlayTree.Nodes[0].Visible &&
                GrowingOverlayTree.Nodes[0].VM.IsRunning() &&
                GrowingOverlayTree.Nodes[1].Visible &&
                GrowingOverlayTree.Nodes[1].ImageID == 501u &&
                GrowingOverlayTree.Nodes[1].ParentIndex == 0,
        "source node remains valid after its overlay reallocates image-tree storage");
    EM.DestroyEntity(GrowingOverlayEntity);

    const auto FiniteInit = FindFiniteAnimationImage(
        Game::IScriptAnims::Anim_Init, 2);
    Game::Systems::AnimationSystem::SetSpriteImageResolver(
        [FiniteInit](int SpriteID) -> std::optional<uint32_t> {
            return (SpriteID == 777 || SpriteID == 778)
                ? std::optional<uint32_t>{(uint32_t)FiniteInit.ImageID}
                : std::nullopt;
        });
    static const std::vector<int> IndependentSpriteProgram = {
        Game::IScriptOpcodes::Opc_SprOl, 777, 3, -2,
        Game::IScriptOpcodes::Opc_LowSprUl, 778, -4, 1,
        Game::IScriptOpcodes::Opc_Wait, 2,
        Game::IScriptOpcodes::Opc_End
    };
    const int SpriteSource = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(SpriteSource)
        .Position = {64.0f, 64.0f};
    auto& SpriteSourceTree =
        EM.AddComponent<Game::Components::ImageTreeComponent>(SpriteSource);
    SpriteSourceTree.Nodes.resize(2);
    SpriteSourceTree.Nodes[0].Offset = {4.0f, 6.0f};
    SpriteSourceTree.Nodes[0].Visible = true;
    SpriteSourceTree.Nodes[1].ParentIndex = 0;
    SpriteSourceTree.Nodes[1].Offset = {7.0f, 8.0f};
    SpriteSourceTree.Nodes[1].Visible = true;
    SpriteSourceTree.Nodes[1].VM.LoadScript(
        Game::IScriptAnims::Anim_Init, &IndependentSpriteProgram, 0);
    Anim.Update(1.0f / 24.0f);
    std::vector<int> IndependentEffects;
    for (int EffectID : EM.GetEntitiesWith<Game::Components::EffectComponent,
                                           Game::Components::RenderComponent,
                                           Game::Components::UnitComponent>())
    {
        const auto* Effect =
            EM.GetComponent<Game::Components::EffectComponent>(EffectID);
        if (Effect && Effect->ParentEntityID == SpriteSource)
            IndependentEffects.push_back(EffectID);
    }
    Require(IndependentEffects.size() == 2,
        "sprol and lowsprul create independent effect entities");
    bool SawAboveSprite = false;
    bool SawBelowSprite = false;
    for (int EffectID : IndependentEffects)
    {
        const auto* Effect =
            EM.GetComponent<Game::Components::EffectComponent>(EffectID);
        const auto* EffectTransform =
            EM.GetComponent<Game::Components::TransformComponent>(EffectID);
        const auto* EffectRender =
            EM.GetComponent<Game::Components::RenderComponent>(EffectID);
        Require(Effect && EffectTransform && EffectRender &&
                EffectRender->ImageID == (uint32_t)FiniteInit.ImageID &&
                Effect->InitialAnimation == Game::IScriptAnims::Anim_Init &&
                Effect->DestroyWhenAnimationCompletes,
            "independent sprite resolves to its main image and owns an Init script lifecycle");
        if (Effect->DrawAbove)
        {
            SawAboveSprite = true;
            Require(EffectTransform->Position.X == 78.0f &&
                    EffectTransform->Position.Y == 76.0f,
                "sprol world position includes every parent-image offset");
        }
        else
        {
            SawBelowSprite = true;
            Require(EffectTransform->Position.X == 71.0f &&
                    EffectTransform->Position.Y == 79.0f,
                "lowsprul preserves its authored below-sprite offset");
        }
    }
    Require(SawAboveSprite && SawBelowSprite,
        "independent sprite effects preserve authored above/below ordering");
    EM.GetComponent<Game::Components::TransformComponent>(SpriteSource)
        ->Position = {160.0f, 160.0f};
    Anim.Update(1.0f / 24.0f);
    for (int EffectID : IndependentEffects)
    {
        const auto* Effect =
            EM.GetComponent<Game::Components::EffectComponent>(EffectID);
        const auto* EffectTransform =
            EM.GetComponent<Game::Components::TransformComponent>(EffectID);
        Require(Effect && EffectTransform && !Effect->FollowParent &&
                EffectTransform->Position.X < 100.0f,
            "thingy remains at its spawn position when the source moves");
        EM.DestroyEntity(EffectID);
    }
    EM.DestroyEntity(SpriteSource);

    const auto LongDeath = FindFiniteAnimationImage(
        Game::IScriptAnims::Anim_Death, 30);
    Game::Systems::AnimationSystem::SetSpriteImageResolver(
        [FiniteInit](int) -> std::optional<uint32_t> {
            return (uint32_t)FiniteInit.ImageID;
        });
    const int DeathEffect = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(DeathEffect)
        .Position = {80.0f, 96.0f};
    auto& DeathRender =
        EM.AddComponent<Game::Components::RenderComponent>(DeathEffect);
    DeathRender.ImageID = (uint32_t)LongDeath.ImageID;
    auto& DeathUnit =
        EM.AddComponent<Game::Components::UnitComponent>(DeathEffect);
    DeathUnit.HitPoints = 0;
    DeathUnit.MaxHitPoints = 1;
    auto& DeathLifecycle =
        EM.AddComponent<Game::Components::EffectComponent>(DeathEffect);
    DeathLifecycle.LifetimeRemaining = 8.0f;
    DeathLifecycle.InitialAnimation = Game::IScriptAnims::Anim_Death;
    DeathLifecycle.DestroyWhenAnimationCompletes = true;
    auto& DeathTree =
        EM.AddComponent<Game::Components::ImageTreeComponent>(DeathEffect);
    DeathTree.Nodes.emplace_back();
    DeathTree.Nodes.back().ImageID = (uint32_t)LongDeath.ImageID;
    DeathTree.Nodes.back().Visible = true;
    DeathTree.Nodes.back().PersistentSpriteAttachment = true;
    Anim.Update(1.0f / 24.0f);
    Require(DeathTree.Nodes.front().AnimID == Game::IScriptAnims::Anim_Death &&
            !DeathTree.Nodes.front().PersistentSpriteAttachment,
        "sprite_run_anim Death switches every copied child image out of its old animation");
    for (int Step = 1; Step < 25; ++Step)
        Anim.Update(1.0f / 24.0f);
    Require(EM.IsAlive(DeathEffect),
        "long authored Death IScript is not truncated by the former one-second timer");
    int DeathSteps = 25;
    while (EM.IsAlive(DeathEffect) && DeathSteps < 192)
    {
        Anim.Update(1.0f / 24.0f);
        ++DeathSteps;
    }
    Require(!EM.IsAlive(DeathEffect) && DeathSteps < 192,
        "death visual is removed when root and child scripts finish before the safety ceiling");
    Game::Systems::AnimationSystem::SetSpriteImageResolver({});

    int ShieldResolverCalls = 0;
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(
        [&ShieldResolverCalls](uint32_t ImageID, size_t LoType, size_t Frame,
           size_t OffsetIndex, bool Flipped)
            -> std::optional<Core::Math::Vec2> {
            if (ImageID == 600u && LoType == 5u && Frame == 3u &&
                OffsetIndex == 5u && !Flipped)
            {
                ++ShieldResolverCalls;
                return Core::Math::Vec2{9.0f, -11.0f};
            }
            return std::nullopt;
        });
    const int ShieldVisualTarget = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(ShieldVisualTarget)
        .Position = {112.0f, 112.0f};
    auto& ShieldVisualRender =
        EM.AddComponent<Game::Components::RenderComponent>(ShieldVisualTarget);
    ShieldVisualRender.ImageID = 600u;
    ShieldVisualRender.FrameIndex = 3;
    auto& ShieldVisualUnit =
        EM.AddComponent<Game::Components::UnitComponent>(ShieldVisualTarget);
    ShieldVisualUnit.HitPoints = ShieldVisualUnit.MaxHitPoints = 40;
    Game::Components::ShieldImpactRequest ShieldRequest;
    ShieldRequest.DirectionIndex = 5;
    ShieldRequest.SourceImageID = 600u;
    ShieldRequest.SourceFrame = 3;
    ShieldRequest.SourceFlipped = false;
    EM.AddComponent<Game::Components::ShieldImpactComponent>(ShieldVisualTarget)
        .Impacts.push_back(ShieldRequest);
    Anim.Update(1.0f / 24.0f);
    const auto* ShieldTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(ShieldVisualTarget);
    const Game::Components::ImageSubnode* ShieldNode = nullptr;
    if (ShieldTree)
    {
        for (const auto& Node : ShieldTree->Nodes)
            if (Node.Visible && Node.ImageID == 424u)
                ShieldNode = &Node;
    }
    Require(ShieldResolverCalls == 1 && ShieldNode &&
            ShieldNode->Offset.X == 9.0f &&
            ShieldNode->Offset.Y == -11.0f &&
            ShieldNode->OverlayOffsetType == 5 &&
            ShieldNode->OverlayOffsetIndex == 5 &&
            ShieldNode->VM.GetContext().CurrentDirection == 40,
        "shield damage spawns the directional authored overlay at the impact side");
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver({});

    int TankTurretOffsetCalls = 0;
    size_t TankTurretOffsetFrame = 0;
    bool TankTurretOffsetFlipped = false;
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(
        [&](uint32_t ImageID, size_t LoType, size_t SourceFrame,
            size_t OffsetIndex, bool Flipped)
            -> std::optional<Core::Math::Vec2> {
            if (ImageID != (uint32_t)kTankBaseImage ||
                LoType != 2 || OffsetIndex != 0)
            {
                return std::nullopt;
            }
            ++TankTurretOffsetCalls;
            TankTurretOffsetFrame = SourceFrame;
            TankTurretOffsetFlipped = Flipped;
            const float X = (float)(5 + SourceFrame);
            return Core::Math::Vec2{Flipped ? -X : X, -3.0f};
        });

    const int Tank = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Tank).Position =
        {96.0f, 224.0f};
    auto& TankRender = EM.AddComponent<Game::Components::RenderComponent>(Tank);
    TankRender.ImageID = kTankBaseImage;
    TankRender.SubunitImageID = kTankTurretImage;
    TankRender.TotalFrames = 256;
    EM.AddComponent<Game::Components::UnitComponent>(Tank).ChkUnitID = 5;
    auto& TankPath = EM.AddComponent<Game::Components::PathfindingComponent>(Tank);
    TankPath.FacingAngle = 0.0f;
    Anim.Update(1.0f / 24.0f);
    auto* TankTree = EM.GetComponent<Game::Components::ImageTreeComponent>(Tank);
    Game::Components::ImageSubnode* TankTurret = nullptr;
    if (TankTree)
    {
        for (auto& Node : TankTree->Nodes)
            if (Node.IsUnitSubunit)
                TankTurret = &Node;
    }
    Require(TankTurret && TankTurret->Visible &&
            TankTurret->ImageID == (uint32_t)kTankTurretImage &&
            TankTurret->VM.GetContext().HasDirectionalFrames,
        "Tank turret is an independently scripted directional subunit image");
    Require(TankTurretOffsetCalls > 0 &&
            TankTurretOffsetFrame == (size_t)TankRender.FrameIndex &&
            TankTurretOffsetFlipped == TankRender.Flipped &&
            TankTurret->OverlayOffsetType == 2 &&
            TankTurret->OverlayOffsetIndex == 0 &&
            TankTurret->Offset.X ==
                (TankRender.Flipped
                    ? -(float)(5 + TankRender.FrameIndex)
                    : (float)(5 + TankRender.FrameIndex)) &&
            TankTurret->Offset.Y == -3.0f,
        "Tank turret attaches to the current body frame's LO(2, 0) offset");
    const auto FindTankTurretNode = [&]()
        -> Game::Components::ImageSubnode*
    {
        auto* CurrentTree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(Tank);
        if (!CurrentTree)
            return nullptr;
        for (auto& Node : CurrentTree->Nodes)
            if (Node.IsUnitSubunit)
                return &Node;
        return nullptr;
    };
    const int EastTurretFrame = TankTurret->FrameIndex;
    const bool EastTurretFlip = TankTurret->Flipped;
    const int InitialTankTurretOffsetCalls = TankTurretOffsetCalls;
    TankPath.FacingAngle = 3.14159265359f;
    Anim.Update(1.0f / 24.0f);
    TankTree = EM.GetComponent<Game::Components::ImageTreeComponent>(Tank);
    TankTurret = nullptr;
    if (TankTree)
    {
        for (auto& Node : TankTree->Nodes)
            if (Node.IsUnitSubunit)
                TankTurret = &Node;
    }
    Require(TankTurret && (TankTurret->FrameIndex != EastTurretFrame ||
            TankTurret->Flipped != EastTurretFlip),
        "moving Tank turret follows chassis heading instead of reusing body animation frames");
    Require(TankTurret && TankTurretOffsetCalls > InitialTankTurretOffsetCalls &&
            TankTurretOffsetFrame == (size_t)TankRender.FrameIndex &&
            TankTurretOffsetFlipped == TankRender.Flipped &&
            TankTurret->Offset.X ==
                (TankRender.Flipped
                    ? -(float)(5 + TankRender.FrameIndex)
                    : (float)(5 + TankRender.FrameIndex)),
        "Tank turret LO attachment refreshes with the body frame and flip state");

    const int TankTarget = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(TankTarget).Position =
        {192.0f, 184.0f};
    EM.AddComponent<Game::Components::UnitComponent>(TankTarget);
    auto& TankCombat = EM.AddComponent<Game::Components::CombatComponent>(Tank);
    TankCombat.TargetEntityID = TankTarget;
    TankCombat.PendingAttackFrame = true;
    TankCombat.PendingAttackTargetEntityID = TankTarget;
    TankCombat.PendingAttackTargetPos = {192.0f, 184.0f};
    TankCombat.AttackCooldown = 1.0f;
    TankTree = EM.GetComponent<Game::Components::ImageTreeComponent>(Tank);
    std::vector<bool> TankNodesVisibleBeforeAttack;
    if (TankTree)
    {
        TankNodesVisibleBeforeAttack.reserve(TankTree->Nodes.size());
        for (const auto& Node : TankTree->Nodes)
            TankNodesVisibleBeforeAttack.push_back(Node.Visible);
    }
    bool SawTurretMuzzleOverlay = false;
    bool SawChassisMuzzleOverlay = false;
    bool SawMuzzleDirectionInheritedFromTurret = false;
    bool SawThirtyTwoWayTurretAim = false;
    for (int Step = 0; Step < 48; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        TankTree = EM.GetComponent<Game::Components::ImageTreeComponent>(Tank);
        if (!TankTree)
            continue;
        int TurretIndex = -1;
        for (size_t I = 0; I < TankTree->Nodes.size(); ++I)
            if (TankTree->Nodes[I].IsUnitSubunit)
                TurretIndex = (int)I;
        if (TurretIndex >= 0)
        {
            const int TurretDirection =
                TankTree->Nodes[(size_t)TurretIndex]
                    .VM.GetContext().CurrentDirection & 0xff;
            SawThirtyTwoWayTurretAim =
                SawThirtyTwoWayTurretAim || (TurretDirection % 32) != 0;
        }
        for (size_t I = 0; I < TankTree->Nodes.size(); ++I)
        {
            const auto& Node = TankTree->Nodes[I];
            if (!Node.Visible || Node.IsUnitSubunit || Node.PersistentSpriteAttachment ||
                Node.ImageID != (uint32_t)kTankMuzzleImage)
                continue;
            const bool WasVisibleBeforeAttack =
                I < TankNodesVisibleBeforeAttack.size() &&
                TankNodesVisibleBeforeAttack[I];
            if (WasVisibleBeforeAttack)
                continue;
            int Ancestor = Node.ParentIndex;
            bool HasTurretAncestor = false;
            while (Ancestor >= 0 && Ancestor < (int)TankTree->Nodes.size())
            {
                if (Ancestor == TurretIndex)
                {
                    HasTurretAncestor = true;
                    break;
                }
                Ancestor = TankTree->Nodes[(size_t)Ancestor].ParentIndex;
            }
            if (HasTurretAncestor)
            {
                SawTurretMuzzleOverlay = true;
                SawMuzzleDirectionInheritedFromTurret =
                    Node.InheritSpriteDirection && Node.ParentIndex >= 0 &&
                    Node.VM.GetContext().CurrentDirection ==
                        TankTree->Nodes[(size_t)Node.ParentIndex]
                            .VM.GetContext().CurrentDirection;
            }
            else
                SawChassisMuzzleOverlay = true;
        }
    }
    Require(SawTurretMuzzleOverlay && !SawChassisMuzzleOverlay,
        "Tank muzzle overlay is parented to the turret instead of the chassis root");
    Require(SawMuzzleDirectionInheritedFromTurret,
        "Tank muzzle directional frame inherits the turret heading on its first visible frame");
    Require(SawThirtyTwoWayTurretAim,
        "Tank turret preserves a 32-way diagonal aim instead of snapping to eight directions");

    // PendingAttackFrame stays asserted until the turret's attack opcode.
    // This test stub intentionally does not resolve it. Tank's authored Init
    // deliberately loops, so the per-node latch, not VM termination, is what
    // proves one pending request cannot restart the entry every frame.
    TankTurret = FindTankTurretNode();
    Require(TankTurret &&
            TankTurret->AnimID == Game::IScriptAnims::Anim_GndAttkInit &&
            TankTurret->AttackRequestLatched && TankTurret->VM.IsRunning(),
        "one Tank pending shot stays in its single latched GndAttkInit request");
    for (int Step = 0; Step < 4; ++Step)
        Anim.Update(1.0f / 24.0f);
    TankTurret = FindTankTurretNode();
    Require(TankTurret && TankTurret->AttackRequestLatched &&
            TankTurret->AnimID == Game::IScriptAnims::Anim_GndAttkInit,
        "latched Tank request does not become a duplicate Init request");

    TankCombat.PendingAttackFrame = false;
    TankCombat.PendingAttackDamage = 0;
    Anim.Update(1.0f / 24.0f);
    TankTurret = FindTankTurretNode();
    Require(TankTurret && !TankTurret->AttackRequestLatched &&
            TankTurret->AnimID == Game::IScriptAnims::Anim_GndAttkInit,
        "cleared pending edge rearms Tank turret while recovery continues");

    TankCombat.PendingAttackFrame = true;
    TankCombat.PendingAttackTargetEntityID = TankTarget;
    Game::Systems::gResolvePendingAttackFrameResult = true;
    Anim.Update(1.0f / 24.0f);
    TankTurret = FindTankTurretNode();
    Require(TankTurret &&
            TankTurret->AnimID == Game::IScriptAnims::Anim_GndAttkRpt,
        "next Tank shot maps matching attack pose to GndAttkRpt");

    for (int Guard = 0; TankCombat.PendingAttackFrame && Guard < 128; ++Guard)
        Anim.Update(1.0f / 24.0f);
    TankTurret = FindTankTurretNode();
    Require(TankTurret && !TankCombat.PendingAttackFrame &&
            !TankTurret->AttackRequestLatched,
        "Tank attack opcode immediately rearms the subunit request latch");
    Game::Systems::gResolvePendingAttackFrameResult = false;

    for (int Step = 0; Step < 8; ++Step)
        Anim.Update(1.0f / 24.0f);
    TankTurret = FindTankTurretNode();
    Require(TankTurret &&
            TankTurret->AnimID == Game::IScriptAnims::Anim_GndAttkRpt &&
            TankTurret->VM.IsRunning(),
        "Tank repeat script keeps authored recovery active with a live target");

    TankCombat.TargetEntityID = -1;
    TankCombat.AutoTargetEntityID = -1;
    TankCombat.PendingAttackTargetEntityID = -1;
    Anim.Update(1.0f / 24.0f);
    TankTurret = FindTankTurretNode();
    Require(TankTurret &&
            TankTurret->AnimID == Game::IScriptAnims::Anim_GndAttkToIdle,
        "Tank target loss enters the authored GndAttkToIdle transition");

    auto& TankSiege =
        EM.AddComponent<Game::Components::SiegeTransitionComponent>(Tank);
    TankSiege.Active = true;
    TankSiege.TargetSieged = true;
    TankSiege.TargetChkUnitID = 30;
    TankSiege.TargetSpeed = 0.0f;
    TankSiege.TargetImageID = kSiegeTankBaseImage;
    TankSiege.TargetSubunitImageID = kSiegeTankTurretImage;
    TankSiege.TargetCombat = TankCombat;
    std::set<int> SiegeDeployFrames;
    for (int Step = 0; Step < 180 && TankSiege.Active; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        if (TankSiege.Stage == 1)
            SiegeDeployFrames.insert(TankRender.FrameIndex);
    }
    Require(!TankSiege.Active &&
            EM.GetComponent<Game::Components::UnitComponent>(Tank)->ChkUnitID == 30 &&
            SiegeDeployFrames.size() > 1,
        "Tank waits for the authored sieged-hull Init animation before completing deployment");
    TankTree = EM.GetComponent<Game::Components::ImageTreeComponent>(Tank);
    TankTurret = nullptr;
    if (TankTree)
    {
        for (auto& Node : TankTree->Nodes)
            if (Node.IsUnitSubunit)
                TankTurret = &Node;
    }
    Require(TankTurret && TankTurret->Visible &&
            TankTurret->ImageID == (uint32_t)kSiegeTankTurretImage,
        "Siege mode replaces the mobile turret with the authored siege turret subunit");

    TankSiege = Game::Components::SiegeTransitionComponent{};
    TankSiege.Active = true;
    TankSiege.TargetSieged = false;
    TankSiege.TargetChkUnitID = 5;
    TankSiege.TargetSpeed = 2.0f;
    TankSiege.TargetImageID = kTankBaseImage;
    TankSiege.TargetSubunitImageID = kTankTurretImage;
    TankSiege.TargetCombat = TankCombat;
    std::set<int> UnsiegeFrames;
    for (int Step = 0; Step < 180 && TankSiege.Active; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        UnsiegeFrames.insert(TankRender.FrameIndex);
    }
    Require(!TankSiege.Active && TankRender.ImageID == (uint32_t)kTankBaseImage &&
            TankRender.SubunitImageID == (uint32_t)kTankTurretImage &&
            EM.GetComponent<Game::Components::UnitComponent>(Tank)->ChkUnitID == 5 &&
            UnsiegeFrames.size() > 1,
        "Tank keeps the sieged hull through authored SpecialState2 before restoring tank mode");
    EM.DestroyEntity(TankTarget);
    EM.DestroyEntity(Tank);
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver({});

    const int Mineral = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Mineral)
        .Position = {160.0f, 128.0f};
    auto& MineralResource = EM.AddComponent<Game::Components::ResourceComponent>(Mineral);
    MineralResource.Type = Game::Components::ResourceType::Minerals;
    MineralResource.Amount = 1500;
    MineralResource.MaxAmount = 1500;
    auto& MineralRender = EM.AddComponent<Game::Components::RenderComponent>(Mineral);
    MineralRender.ImageID = 347;
    MineralRender.TotalFrames = 4;
    auto& MineralTree = EM.AddComponent<Game::Components::ImageTreeComponent>(Mineral);
    MineralTree.Nodes.emplace_back().TotalFrames = 4;

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

    MineralResource.Amount = 1100;
    Anim.Update(1.0f / 24.0f);
    Require(MineralRender.Visible && MineralRender.FrameIndex == 1,
        "mineral texture advances to its first depleted frame below 75 percent");
    MineralResource.Amount = 700;
    Anim.Update(1.0f / 24.0f);
    Require(MineralRender.Visible && MineralRender.FrameIndex == 2,
        "mineral texture advances to its second depleted frame below 50 percent");
    MineralResource.Amount = 300;
    Anim.Update(1.0f / 24.0f);
    Require(MineralRender.Visible && MineralRender.FrameIndex == 3,
        "mineral texture uses its smallest authored frame below 25 percent");
    MineralResource.Amount = 0;
    MineralResource.IsDepleted = true;
    Anim.Update(1.0f / 24.0f);
    Require(!MineralRender.Visible && !MineralTree.Nodes.front().Visible,
        "fully depleted mineral hides both its root texture and image-tree layers");
    MineralResource.Amount = 1500;
    MineralResource.IsDepleted = false;

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

    const int ConstructionTarget = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(ConstructionTarget)
        .Position = {224.0f, 224.0f};
    auto& ConstructionBuilding =
        EM.AddComponent<Game::Components::BuildingComponent>(ConstructionTarget);
    ConstructionBuilding.UnderConstruction = true;

    const int ConstructingScv = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(ConstructingScv)
        .Position = {192.0f, 224.0f};
    auto& ConstructingScvRender =
        EM.AddComponent<Game::Components::RenderComponent>(ConstructingScv);
    ConstructingScvRender.ImageID = kScvImage;
    ConstructingScvRender.TotalFrames = 51;
    EM.AddComponent<Game::Components::UnitComponent>(ConstructingScv).ChkUnitID = 7;
    EM.AddComponent<Game::Components::PlayerComponent>(ConstructingScv).PlayerID = 0;
    EM.AddComponent<Game::Components::PathfindingComponent>(ConstructingScv)
        .FacingAngle = 0.0f;
    auto& ConstructionCommand =
        EM.AddComponent<Game::Components::CommandComponent>(ConstructingScv);
    ConstructionCommand.CurrentCommand = Game::Components::CommandType::Build;
    ConstructionCommand.TargetEntityID = ConstructionTarget;
    ConstructionBuilding.BuilderUnitID = ConstructingScv;

    const int WeaponResolvesBeforeConstruction = ScvWeaponVisualResolves;
    std::set<int> ConstructionScvFrames;
    bool SawConstructionSparkAtContactPoint = false;
    for (int Step = 0; Step < 96; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        ConstructionScvFrames.insert(ConstructingScvRender.FrameIndex);
        const auto* Tree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(ConstructingScv);
        if (!Tree)
            continue;
        for (const auto& Node : Tree->Nodes)
        {
            if (!Node.Visible || (int)Node.ImageID != kScvSparkImage)
                continue;
            SawConstructionSparkAtContactPoint =
                SawConstructionSparkAtContactPoint ||
                (std::fabs(Node.CreationOffset.X - 20.0f) < 0.1f &&
                 std::fabs(Node.CreationOffset.Y) < 0.1f);
        }
    }
    Require(ConstructionScvFrames.size() > 1,
        "constructing SCV advances the real AlmostBuilt work animation");
    Require(ScvWeaponVisualResolves >= WeaponResolvesBeforeConstruction + 2,
        "constructing SCV repeatedly emits Fusion Cutter sparks");
    Require(SawConstructionSparkAtContactPoint,
        "construction spark appears at the building contact side");

    ConstructionBuilding.UnderConstruction = false;
    ConstructionCommand.CurrentCommand = Game::Components::CommandType::Stop;
    ConstructionCommand.TargetEntityID = -1;
    Anim.Update(1.0f / 24.0f);
    bool HasStaleConstructionSpark = false;
    if (const auto* Tree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(ConstructingScv))
    {
        for (const auto& Node : Tree->Nodes)
            HasStaleConstructionSpark = HasStaleConstructionSpark ||
                (Node.Visible && (int)Node.ImageID == kScvSparkImage);
    }
    Require(!HasStaleConstructionSpark,
        "finishing construction clears the transient Fusion Cutter spark");

    int CarryOffsetResolverCalls = 0;
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(
        [&CarryOffsetResolverCalls](uint32_t ImageID, size_t LoType, size_t,
                                    size_t OffsetIndex, bool Flipped)
            -> std::optional<Core::Math::Vec2> {
            Require(ImageID == kScvImage,
                "carried mineral resolves its attachment from the SCV main image");
            Require(LoType == 2 && OffsetIndex == 0,
                "carried mineral uses OpenBW special LO offset type 2 index 0");
            ++CarryOffsetResolverCalls;
            return Core::Math::Vec2{Flipped ? -7.0f : 7.0f, -9.0f};
        });
    MiningScvHarvest.CarryingAmount = 8;
    MiningScvHarvest.CarryingType = Game::Components::ResourceType::Minerals;
    auto* MiningScvPath =
        EM.GetComponent<Game::Components::PathfindingComponent>(MiningScv);
    Require(MiningScvPath, "carrying SCV keeps direction state");
    MiningScvPath->CurrentVelocity = {64.0f, 0.0f};
    MiningScvPath->Path = {{256.0f, 128.0f}};
    MiningScvPath->PathIndex = 0;
    Anim.Update(1.0f / 24.0f);
    const auto* CarryTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(MiningScv);
    Require(CarryTree, "carrying SCV owns an image tree attachment");
    const Game::Components::ImageSubnode* CarryNode = nullptr;
    for (const auto& Node : CarryTree->Nodes)
    {
        if (Node.IsCarriedResourceOverlay)
        {
            CarryNode = &Node;
            break;
        }
    }
    Require(CarryNode && CarryNode->Visible && CarryNode->ImageID == 397u &&
                CarryNode->UseSpecialOffset && CarryNode->InheritSpriteDirection,
        "mineral chunk is a directional SCV sprite attachment with special offset");
    const std::pair<int, bool> EastCarrySignature{
        CarryNode->FrameIndex, CarryNode->Flipped};
    MiningScvPath->CurrentVelocity = {-64.0f, 0.0f};
    MiningScvPath->Path = {{0.0f, 128.0f}};
    for (int I = 0; I < 3; ++I)
        Anim.Update(1.0f / 24.0f);
    CarryTree = EM.GetComponent<Game::Components::ImageTreeComponent>(MiningScv);
    CarryNode = nullptr;
    for (const auto& Node : CarryTree->Nodes)
        if (Node.IsCarriedResourceOverlay)
            CarryNode = &Node;
    Require(CarryNode &&
                std::pair<int, bool>{CarryNode->FrameIndex, CarryNode->Flipped} !=
                    EastCarrySignature,
        "carried mineral rotates and flips with the SCV heading");
    Require(CarryOffsetResolverCalls >= 2,
        "carried mineral refreshes its two-arm LO attachment while direction changes");
    MiningScvHarvest.CarryingAmount = 0;
    MiningScvHarvest.CarryingType = Game::Components::ResourceType::None;
    Anim.Update(1.0f / 24.0f);
    CarryTree = EM.GetComponent<Game::Components::ImageTreeComponent>(MiningScv);
    CarryNode = nullptr;
    for (const auto& Node : CarryTree->Nodes)
        if (Node.IsCarriedResourceOverlay)
            CarryNode = &Node;
    Require(CarryNode && !CarryNode->Visible,
        "depositing resources removes the carried sprite attachment");
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver({});

    const auto SpawnBuilding = [&](int MainImageID)
    {
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID)
            .Position = {320.0f + (float)EntityID * 8.0f, 192.0f};
        auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.ImageID = (uint32_t)MainImageID;
        Render.TotalFrames = 1;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.HitPoints = 1000;
        Unit.MaxHitPoints = 1000;
        auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Building.UnderConstruction = false;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = 0;
        return EntityID;
    };

    const auto StartProduction = [&](int EntityID)
    {
        auto& Production =
            EM.AddComponent<Game::Components::ProductionComponent>(EntityID);
        Production.Queue.push_back(0);
        Production.CurrentTrainingUnitDefID = 0;
        Production.TrainingTimePerUnit = 20.0f;
        return &Production;
    };

    const int WorkingCommandCenter = SpawnBuilding(kCommandCenterImage);
    auto* CommandCenterProduction = StartProduction(WorkingCommandCenter);
    for (int Step = 0; Step < 4; ++Step)
        Anim.Update(1.0f / 24.0f);
    auto* CommandCenterTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(WorkingCommandCenter);
    const Game::Components::ImageSubnode* CommandCenterWorkingNode = nullptr;
    if (CommandCenterTree)
        for (const auto& Node : CommandCenterTree->Nodes)
            if ((int)Node.ImageID == kCommandCenterControlImage)
                CommandCenterWorkingNode = &Node;
    Require(CommandCenterWorkingNode && CommandCenterWorkingNode->Visible &&
                (int)CommandCenterWorkingNode->ImageID == kCommandCenterControlImage,
        "Command Center production exposes its canonical control-panel overlay");
    float MinCommandCenterOpacity = 1.0f;
    float MaxCommandCenterOpacity = 0.0f;
    for (int Step = 0; Step < 48; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        CommandCenterTree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(WorkingCommandCenter);
        if (!CommandCenterTree)
            continue;
        for (const auto& Node : CommandCenterTree->Nodes)
        {
            if ((int)Node.ImageID != kCommandCenterControlImage)
                continue;
            MinCommandCenterOpacity = std::min(MinCommandCenterOpacity, Node.Opacity);
            MaxCommandCenterOpacity = std::max(MaxCommandCenterOpacity, Node.Opacity);
        }
    }
    Require(MaxCommandCenterOpacity - MinCommandCenterOpacity > 0.35f,
        "Command Center production continuously pulses its control-panel image");
    CommandCenterProduction->Queue.clear();
    CommandCenterProduction->CurrentTrainingUnitDefID = -1;
    Anim.Update(1.0f / 24.0f);
    CommandCenterTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(WorkingCommandCenter);
    CommandCenterWorkingNode = nullptr;
    if (CommandCenterTree)
        for (const auto& Node : CommandCenterTree->Nodes)
            if ((int)Node.ImageID == kCommandCenterControlImage)
                CommandCenterWorkingNode = &Node;
    Require(CommandCenterWorkingNode &&
                CommandCenterWorkingNode->AnimID ==
                    Game::IScriptAnims::Anim_WorkingToIdle &&
                std::fabs(CommandCenterWorkingNode->Opacity - 1.0f) < 0.001f,
        "Command Center control panel receives WorkingToIdle when production stops");

    const int WorkingBarracks = SpawnBuilding(kBarracksImage);
    StartProduction(WorkingBarracks);
    int BarracksFrameChanges = 0;
    int PreviousBarracksFrame = -1;
    for (int Step = 0; Step < 96; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        const auto* Render =
            EM.GetComponent<Game::Components::RenderComponent>(WorkingBarracks);
        if (Render && PreviousBarracksFrame >= 0 &&
            Render->FrameIndex != PreviousBarracksFrame)
        {
            ++BarracksFrameChanges;
        }
        if (Render)
            PreviousBarracksFrame = Render->FrameIndex;
    }
    Require(BarracksFrameChanges > 8,
        "Barracks production advances the real 62-frame IsWorking root animation");

    const int SupplyDepot = SpawnBuilding(kSupplyDepotImage);
    int DepotMachineryFrameChanges = 0;
    int PreviousDepotMachineryFrame = -1;
    for (int Step = 0; Step < 96; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        const auto* Tree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(SupplyDepot);
        if (!Tree)
            continue;
        for (const auto& Node : Tree->Nodes)
        {
            if (Node.Visible && (int)Node.ImageID == kSupplyDepotMachineryImage)
            {
                if (PreviousDepotMachineryFrame >= 0 &&
                    Node.FrameIndex != PreviousDepotMachineryFrame)
                {
                    ++DepotMachineryFrameChanges;
                }
                PreviousDepotMachineryFrame = Node.FrameIndex;
            }
        }
    }
    Require(DepotMachineryFrameChanges > 8,
        "Supply Depot Built creates and advances its completed idle machinery image");

    const int EngineeringBay = SpawnBuilding(kEngineeringBayImage);
    auto* EngineeringBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(EngineeringBay);
    EngineeringBuilding->IsResearching = true;
    EngineeringBuilding->ResearchingID = 0;
    EngineeringBuilding->BuildTimeRemaining = 30.0f;
    int EngineeringMachineryFrameChanges = 0;
    int PreviousEngineeringMachineryFrame = -1;
    bool SawEngineeringWorking = false;
    for (int Step = 0; Step < 72; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        const auto* Tree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(EngineeringBay);
        if (!Tree)
            continue;
        for (const auto& Node : Tree->Nodes)
        {
            if (Node.Visible && (int)Node.ImageID == kEngineeringBayMachineryImage)
            {
                if (Node.AnimID != Game::IScriptAnims::Anim_IsWorking)
                    continue;
                SawEngineeringWorking = true;
                if (PreviousEngineeringMachineryFrame >= 0 &&
                    Node.FrameIndex != PreviousEngineeringMachineryFrame)
                {
                    ++EngineeringMachineryFrameChanges;
                }
                PreviousEngineeringMachineryFrame = Node.FrameIndex;
            }
        }
    }
    Require(SawEngineeringWorking && EngineeringMachineryFrameChanges > 8,
        "Engineering Bay research advances its child machinery animation");

    const int Comsat = SpawnBuilding(kComsatStationImage);
    auto* ComsatRender = EM.GetComponent<Game::Components::RenderComponent>(Comsat);
    ComsatRender->AddonConnectorImageID = kComsatConnectorImage;
    for (int Step = 0; Step < 4; ++Step)
        Anim.Update(1.0f / 24.0f);
    const auto* ComsatTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(Comsat);
    const Game::Components::ImageSubnode* Connector = nullptr;
    if (ComsatTree)
        for (const auto& Node : ComsatTree->Nodes)
            if (Node.IsAddonConnectorOverlay)
                Connector = &Node;
    Require(Connector && Connector->Visible && !Connector->DrawAbove &&
                (int)Connector->ImageID == kComsatConnectorImage,
        "Terran add-on connector is a persistent animated image below the body");
    auto* ComsatBuilding =
        EM.GetComponent<Game::Components::BuildingComponent>(Comsat);
    ComsatBuilding->FlightPhase =
        Game::Components::BuildingFlightPhase::LiftingOff;
    Anim.Update(1.0f / 24.0f);
    ComsatTree = EM.GetComponent<Game::Components::ImageTreeComponent>(Comsat);
    Connector = nullptr;
    if (ComsatTree)
        for (const auto& Node : ComsatTree->Nodes)
            if (Node.IsAddonConnectorOverlay)
                Connector = &Node;
    Require(Connector && Connector->AnimID == Game::IScriptAnims::Anim_LiftOff,
        "Terran add-on connector receives its own LiftOff animation");

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

    const int BurrowProbe = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(BurrowProbe).Position = {192.0f, 192.0f};
    auto& BurrowProbeRender = EM.AddComponent<Game::Components::RenderComponent>(BurrowProbe);
    BurrowProbeRender.ImageID = kLurkerImage;
    BurrowProbeRender.TotalFrames = 256;
    EM.AddComponent<Game::Components::UnitComponent>(BurrowProbe).ChkUnitID = 103;
    auto& BurrowProbeState = EM.AddComponent<Game::Components::BurrowComponent>(BurrowProbe);
    BurrowProbeState.CanBurrow = true;

    Game::IScriptDatabase BurrowImageDatabase;
    Require(BurrowImageDatabase.Load("assets/scripts/iscript.bin",
                                     "assets/arr/images.dat"),
        "load real images.dat flags for Lurker burrow visibility");
    const auto* LurkerShadowInfo =
        BurrowImageDatabase.FindImageInfo(kLurkerShadowImage);
    const auto* AlwaysVisibleInfo =
        BurrowImageDatabase.FindImageInfo(kLurkerSpineImage);
    const auto* TransientInfo =
        BurrowImageDatabase.FindImageInfo(kHydraliskShadowImage);
    Require(LurkerShadowInfo && !LurkerShadowInfo->AlwaysVisible &&
            AlwaysVisibleInfo && AlwaysVisibleInfo->AlwaysVisible &&
            TransientInfo && !TransientInfo->AlwaysVisible,
        "images.dat distinguishes ordinary shadows from always-visible layers");

    // Let the real Lurker Init create its persistent shadow before requesting
    // Burrow. This makes the regression cover restoration of the actual
    // attachment instead of only constructing a synthetic tree.
    Game::Components::ImageTreeComponent* BurrowProbeTree = nullptr;
    Game::Components::ImageSubnode* BurrowProbeShadow = nullptr;
    for (int Step = 0; Step < 32 && !BurrowProbeShadow; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        BurrowProbeTree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(BurrowProbe);
        if (!BurrowProbeTree)
            continue;
        for (auto& Node : BurrowProbeTree->Nodes)
            if (Node.ImageID == (uint32_t)kLurkerShadowImage)
                BurrowProbeShadow = &Node;
    }
    Require(BurrowProbeTree && BurrowProbeShadow &&
            BurrowProbeShadow->Visible &&
            BurrowProbeShadow->PersistentSpriteAttachment &&
            !BurrowProbeShadow->AlwaysVisible,
        "real Lurker Init creates a non-always-visible persistent shadow");

    BurrowProbeTree->Nodes.emplace_back();
    auto& AlwaysVisibleNode = BurrowProbeTree->Nodes.back();
    AlwaysVisibleNode.ImageID = kLurkerSpineImage;
    AlwaysVisibleNode.Visible = true;
    AlwaysVisibleNode.PersistentSpriteAttachment = true;
    AlwaysVisibleNode.AlwaysVisible = AlwaysVisibleInfo->AlwaysVisible;
    AlwaysVisibleNode.LifetimeRemaining = -1.0f;
    BurrowProbeTree->Nodes.emplace_back();
    auto& TransientBurrowNode = BurrowProbeTree->Nodes.back();
    TransientBurrowNode.ImageID = kHydraliskShadowImage;
    TransientBurrowNode.Visible = true;
    TransientBurrowNode.PersistentSpriteAttachment = false;
    TransientBurrowNode.AlwaysVisible = TransientInfo->AlwaysVisible;
    TransientBurrowNode.LifetimeRemaining = -1.0f;

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
            "real Lurker Burrow iscript signal completes the burrow state transition");
    Require(!BurrowProbeRender.Visible,
        "fully buried Lurker hides its root instead of leaving a ground remnant");
    auto FindBurrowNode = [&](uint32_t ImageID)
        -> Game::Components::ImageSubnode* {
        auto* Tree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(BurrowProbe);
        if (!Tree)
            return nullptr;
        for (auto& Node : Tree->Nodes)
            if (Node.ImageID == ImageID)
                return &Node;
        return nullptr;
    };
    BurrowProbeShadow = FindBurrowNode(kLurkerShadowImage);
    auto* BurrowAlwaysVisible = FindBurrowNode(kLurkerSpineImage);
    auto* BurrowTransient = FindBurrowNode(kHydraliskShadowImage);
    Require(BurrowProbeShadow && !BurrowProbeShadow->Visible &&
            BurrowProbeShadow->HiddenByBurrow,
        "fully buried Lurker hides and marks its persistent shadow");
    Require(BurrowAlwaysVisible && BurrowAlwaysVisible->Visible &&
            !BurrowAlwaysVisible->HiddenByBurrow,
        "fully buried Lurker preserves images.dat always-visible layers");
    Require(!BurrowTransient ||
            (!BurrowTransient->Visible && !BurrowTransient->HiddenByBurrow),
        "Burrow hides transient ground remnants without marking them for revival");

    for (int Step = 0; Step < 24; ++Step)
        Anim.Update(1.0f / 24.0f);
    Require(!BurrowProbeRender.Visible &&
            BurrowProbeState.Phase == Game::Components::BurrowPhase::Burrowed,
        "buried Lurker holds the invisible state without replaying Burrow dust");
    BurrowProbeShadow = FindBurrowNode(kLurkerShadowImage);
    BurrowAlwaysVisible = FindBurrowNode(kLurkerSpineImage);
    BurrowTransient = FindBurrowNode(kHydraliskShadowImage);
    Require(BurrowProbeShadow && !BurrowProbeShadow->Visible &&
            BurrowProbeShadow->HiddenByBurrow &&
            BurrowAlwaysVisible && BurrowAlwaysVisible->Visible &&
            (!BurrowTransient || !BurrowTransient->Visible),
        "buried hold preserves the visibility markers without replaying dust");

    BurrowProbeState.Phase = Game::Components::BurrowPhase::Unburrowing;
    Anim.Update(1.0f / 24.0f);
    BurrowProbeShadow = FindBurrowNode(kLurkerShadowImage);
    BurrowTransient = FindBurrowNode(kHydraliskShadowImage);
    Require(BurrowProbeRender.Visible && BurrowProbeShadow &&
            !BurrowProbeShadow->Visible &&
            BurrowProbeShadow->HiddenByBurrow &&
            (!BurrowTransient || !BurrowTransient->Visible),
        "UnBurrow exposes its authored root before restoring hidden attachments");
    for (int Step = 0;
         Step < 240 && BurrowProbeState.Phase != Game::Components::BurrowPhase::Surface;
         ++Step)
    {
        Anim.Update(1.0f / 24.0f);
    }
    Require(BurrowProbeState.Phase == Game::Components::BurrowPhase::Surface &&
            !BurrowProbeState.IsBurrowed && BurrowProbeCloak &&
            !BurrowProbeCloak->IsBurrowed && !BurrowProbeCloak->IsCloaked &&
            BurrowProbeRender.Visible,
            "real Lurker UnBurrow iscript signal restores the visible surface state");
    BurrowProbeShadow = FindBurrowNode(kLurkerShadowImage);
    BurrowAlwaysVisible = FindBurrowNode(kLurkerSpineImage);
    BurrowTransient = FindBurrowNode(kHydraliskShadowImage);
    Require(BurrowProbeShadow && BurrowProbeShadow->Visible &&
            !BurrowProbeShadow->HiddenByBurrow &&
            BurrowAlwaysVisible && BurrowAlwaysVisible->Visible &&
            (!BurrowTransient || !BurrowTransient->Visible),
        "surface Lurker restores its persistent shadow without reviving transient remnants");

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
    FlightBuilding.FlightPhase = Game::Components::BuildingFlightPhase::Airborne;
    const int AirborneRootFrame = FlightRender.FrameIndex;
    std::vector<int> LiftOffChildAnimations;
    if (const auto* FlightTree = EM.GetComponent<Game::Components::ImageTreeComponent>(FlightProbe))
        for (const auto& Node : FlightTree->Nodes)
            LiftOffChildAnimations.push_back(Node.AnimID);
    for (int Step = 0; Step < 96; ++Step)
        Anim.Update(1.0f / 24.0f);
    Require(FlightRender.FrameIndex == AirborneRootFrame,
        "airborne building holds the terminal LiftOff root frame instead of replaying Init");
    if (const auto* FlightTree = EM.GetComponent<Game::Components::ImageTreeComponent>(FlightProbe))
    {
        for (size_t I = 0; I < LiftOffChildAnimations.size() &&
                           I < FlightTree->Nodes.size(); ++I)
            if (LiftOffChildAnimations[I] == Game::IScriptAnims::Anim_LiftOff)
                Require(FlightTree->Nodes[I].AnimID != Game::IScriptAnims::Anim_Init,
                    "airborne landing gear is not restarted in the grounded Init animation");
    }

    FlightBuilding.FlightPhase = Game::Components::BuildingFlightPhase::Landing;
    for (int Step = 0; Step < 360 && !FlightBuilding.FlightAnimationComplete; ++Step)
        Anim.Update(1.0f / 24.0f);
    Require(FlightBuilding.FlightAnimationComplete,
            "real project Landing iscript emits the building flight completion signal");
    Require(std::fabs(FlightRender.VisualOffset.Y) < 0.001f,
            "Landing returns the building root image and landing gear to ground offset");

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
    DamageUnit.HitPoints = 1499;
    auto& DamageBuilding = EM.AddComponent<Game::Components::BuildingComponent>(DamageProbe);
    DamageBuilding.UnderConstruction = false;
    Anim.Update(1.0f / 24.0f);
    auto* DamageTree = EM.GetComponent<Game::Components::ImageTreeComponent>(DamageProbe);
    bool IgnitedAboveThreshold = false;
    if (DamageTree)
    {
        for (const auto& Node : DamageTree->Nodes)
            IgnitedAboveThreshold = IgnitedAboveThreshold ||
                (Node.Visible && Node.IsDamageOverlay);
    }
    Require(!IgnitedAboveThreshold,
        "one point of damage above the first damage_state threshold does not ignite a flame");
    DamageUnit.HitPoints = 500;
    Anim.Update(1.0f / 24.0f);
    DamageTree = EM.GetComponent<Game::Components::ImageTreeComponent>(DamageProbe);
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
    std::set<int> DamageFlameFrames;
    for (int Step = 0; Step < 48; ++Step)
    {
        Anim.Update(1.0f / 24.0f);
        DamageTree =
            EM.GetComponent<Game::Components::ImageTreeComponent>(DamageProbe);
        if (!DamageTree)
            continue;
        for (const auto& Node : DamageTree->Nodes)
            if (Node.Visible && Node.IsDamageOverlay)
                DamageFlameFrames.insert(Node.FrameIndex);
    }
    Require(DamageFlameFrames.size() > 1,
        "unchanged damage_state preserves and advances the flame IScript instead of restarting it every frame");
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
            if (!ProbeNode.Visible || ProbeNode.ImageID != 279u) continue;
            if (BuildingLastNodeFrame >= 0 && BuildingLastNodeFrame != ProbeNode.FrameIndex)
                ++BuildingNodeFrameChanges;
            BuildingLastNodeFrame = ProbeNode.FrameIndex;
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

    auto VerifyFighterShadowAcrossStates =
        [&](int ChkUnitID, int ImageID, int ShadowImageID)
    {
        const int Fighter = SpawnIdleDirectionalUnit(EM, 0.0f, 256, ImageID);
        auto* FighterUnit = EM.GetComponent<Game::Components::UnitComponent>(Fighter);
        auto* FighterPath = EM.GetComponent<Game::Components::PathfindingComponent>(Fighter);
        auto* FighterTransform = EM.GetComponent<Game::Components::TransformComponent>(Fighter);
        Require(FighterUnit && FighterPath && FighterTransform,
            "fighter shadow probe has unit, path, and transform state");
        FighterUnit->ChkUnitID = ChkUnitID;
        FighterPath->Path = {FighterTransform->Position + Core::Math::Vec2{128.0f, 0.0f}};
        FighterPath->CurrentSpeed = 64.0f;
        FighterPath->CurrentVelocity = {64.0f, 0.0f};
        for (int I = 0; I < 8; ++I)
            Anim.Update(1.0f / 24.0f);

        auto HasPersistentShadow = [&]()
        {
            const auto* Tree =
                EM.GetComponent<Game::Components::ImageTreeComponent>(Fighter);
            if (!Tree)
                return false;
            for (const auto& Node : Tree->Nodes)
                if (Node.Visible && (int)Node.ImageID == ShadowImageID &&
                    Node.PersistentSpriteAttachment)
                    return true;
            return false;
        };
        Require(HasPersistentShadow(),
            "moving Terran fighter keeps its Init-created ground shadow");

        FighterPath->Path.clear();
        FighterPath->CurrentSpeed = 0.0f;
        FighterPath->CurrentVelocity = {};
        auto& FighterCombat =
            EM.AddComponent<Game::Components::CombatComponent>(Fighter);
        FighterCombat.PendingAttackFrame = true;
        FighterCombat.PendingAttackTargetPos =
            FighterTransform->Position + Core::Math::Vec2{96.0f, 32.0f};
        FighterCombat.AttackCooldown = 1.0f;
        for (int I = 0; I < 3; ++I)
            Anim.Update(1.0f / 24.0f);
        Require(HasPersistentShadow(),
            "attacking Terran fighter keeps its ground shadow");
    };

    VerifyFighterShadowAcrossStates(8, kWraithImage, kWraithShadowImage);
    VerifyFighterShadowAcrossStates(58, kValkyrieImage, kValkyrieShadowImage);

    const int ScienceVessel = SpawnIdleDirectionalUnit(
        EM, 0.0f, 256, kScienceVesselImage);
    auto* ScienceVesselUnit = EM.GetComponent<Game::Components::UnitComponent>(ScienceVessel);
    Require(ScienceVesselUnit, "Science Vessel probe has a unit component");
    ScienceVesselUnit->ChkUnitID = 9;
    for (int I = 0; I < 8; ++I)
        Anim.Update(1.0f / 24.0f);

    const auto* ScienceVesselTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(ScienceVessel);
    Require(ScienceVesselTree, "real Science Vessel Init creates an image tree");
    const Game::Components::ImageSubnode* ScienceVesselTransition = nullptr;
    const Game::Components::ImageSubnode* ScienceVesselShadow = nullptr;
    for (const auto& Node : ScienceVesselTree->Nodes)
    {
        if ((int)Node.ImageID == kScienceVesselTransitionImage)
            ScienceVesselTransition = &Node;
        if ((int)Node.ImageID == kScienceVesselShadowImage)
            ScienceVesselShadow = &Node;
    }
    Require(ScienceVesselTransition &&
                !ScienceVesselTransition->PersistentSpriteAttachment,
        "Science Vessel Init keeps its transition image transient instead of pinning it on screen");
    Require(ScienceVesselShadow && ScienceVesselShadow->PersistentSpriteAttachment,
        "Science Vessel Init keeps its classified static shadow attached");
    Require(ScienceVesselTransition->VM.IsRunning(),
        "Science Vessel turret retains its directional iscript while idle");

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

    auto VerifyRealUnitAttackLifecycle =
        [&](int ChkUnitID, int ImageID)
    {
        const auto Initial = TraceRealGroundAttack(
            ImageID, Game::IScriptAnims::Anim_GndAttkInit);
        const auto Repeat = TraceRealGroundAttack(
            ImageID, Game::IScriptAnims::Anim_GndAttkRpt);

        const int TargetID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(TargetID)
            .Position = {448.0f, 320.0f};
        EM.AddComponent<Game::Components::UnitComponent>(TargetID);

        const int EntityID = EM.CreateEntity();
        auto& Trans =
            EM.AddComponent<Game::Components::TransformComponent>(EntityID);
        Trans.Position = {320.0f, 320.0f};
        auto& Render =
            EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.ImageID = (uint32_t)ImageID;
        Render.TotalFrames = 512;
        EM.AddComponent<Game::Components::UnitComponent>(EntityID).ChkUnitID =
            ChkUnitID;
        auto& Path =
            EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
        Path.FacingAngle = 0.0f;
        auto& Command =
            EM.AddComponent<Game::Components::CommandComponent>(EntityID);
        Command.CurrentCommand = Game::Components::CommandType::AttackMove;
        Command.TargetEntityID = TargetID;
        Command.TargetPosition = {640.0f, 320.0f};
        auto& Combat =
            EM.AddComponent<Game::Components::CombatComponent>(EntityID);
        Combat.AttackCooldown = 1.0f;
        Combat.CooldownRemaining = 0.0f;
        Combat.TargetEntityID = TargetID;
        Combat.PendingAttackTargetEntityID = TargetID;
        Combat.PendingAttackTargetPos = {448.0f, 320.0f};

        // Establish the real Init script before requesting the first shot.
        Anim.Update(1.0f / 24.0f);
        Combat.PendingAttackFrame = true;
        for (int Step = 0; Step <= Initial.AttackEventStep; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
            Require(Render.FrameIndex == Initial.Frames[(size_t)Step],
                "real unit follows authored pre-fire attack frames");
        }

        // The real CombatSystem clears this flag at the attack opcode. The
        // remainder of the body animation must continue from the same PC.
        Combat.PendingAttackFrame = false;
        for (int Step = Initial.AttackEventStep + 1;
             Step < (int)Initial.Frames.size(); ++Step)
        {
            Anim.Update(1.0f / 24.0f);
            Require(Render.FrameIndex == Initial.Frames[(size_t)Step],
                "real unit keeps authored recoil frames after firing");
        }
        const int HeldInitialFrame = Render.FrameIndex;
        Anim.Update(1.0f / 24.0f);
        Require(Render.FrameIndex == HeldInitialFrame,
            "completed attack holds its terminal pose until repeat or idle");

        // OpenBW requests GndAttkRpt for the next cooldown while the unit is
        // already in an attack pose; replaying GndAttkInit gives both units the
        // visibly wrong wind-up on every shot.
        Combat.PendingAttackFrame = true;
        Combat.PendingAttackTargetEntityID = TargetID;
        Anim.Update(1.0f / 24.0f);
        Require(Render.FrameIndex == Repeat.Frames.front(),
            "second shot begins at the real GndAttkRpt entry");
        Combat.PendingAttackFrame = false;
        for (size_t Step = 1; Step < Repeat.Frames.size(); ++Step)
        {
            Anim.Update(1.0f / 24.0f);
            Require(Render.FrameIndex == Repeat.Frames[Step],
                "repeat shot completes every authored recovery frame");
        }

        const int HeldRepeatFrame = Render.FrameIndex;
        Combat.TargetEntityID = -1;
        Combat.PendingAttackTargetEntityID = -1;
        Command.TargetEntityID = -1;
        Anim.Update(1.0f / 24.0f);
        Require(Render.FrameIndex != HeldRepeatFrame,
            "AttackMove target loss enters the real GndAttkToIdle transition");

        std::set<int> RecoveryFrames;
        for (int Step = 0; Step < 16; ++Step)
        {
            RecoveryFrames.insert(Render.FrameIndex);
            Anim.Update(1.0f / 24.0f);
        }
        Require(RecoveryFrames.size() >= 3,
            "attack-to-idle keeps advancing instead of freezing or restarting");

        EM.DestroyEntity(TargetID);
    };

    VerifyRealUnitAttackLifecycle(0, kMarineImage);
    VerifyRealUnitAttackLifecycle(66, kDragoonImage);

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

    const int RefineryConstruction = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(RefineryConstruction)
        .Position = {256.0f, 160.0f};
    auto& RefineryConstructionRender =
        EM.AddComponent<Game::Components::RenderComponent>(RefineryConstruction);
    RefineryConstructionRender.ImageID = 307;
    RefineryConstructionRender.TotalFrames = 5;
    auto& RefineryConstructionUnit =
        EM.AddComponent<Game::Components::UnitComponent>(RefineryConstruction);
    RefineryConstructionUnit.ChkUnitID = 110;
    auto& RefineryConstructionBuilding =
        EM.AddComponent<Game::Components::BuildingComponent>(RefineryConstruction);
    RefineryConstructionBuilding.IsBuilding = true;
    RefineryConstructionBuilding.UnderConstruction = true;
    RefineryConstructionBuilding.BuildingDefID = 0;
    RefineryConstructionBuilding.TotalBuildTime = 40.0f;
    RefineryConstructionBuilding.BuildProgress = 0.0f;
    auto& RefineryConstructionResource =
        EM.AddComponent<Game::Components::ResourceComponent>(RefineryConstruction);
    RefineryConstructionResource.Type = Game::Components::ResourceType::VespeneGas;
    RefineryConstructionResource.Amount = 5000;
    Anim.Update(1.0f / 24.0f);
    const auto* RefineryConstructionTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(RefineryConstruction);
    bool HasConstructionGasOverlay = false;
    if (RefineryConstructionTree)
        for (const auto& Node : RefineryConstructionTree->Nodes)
            HasConstructionGasOverlay = HasConstructionGasOverlay ||
                (Node.Visible && Node.ImageID == 308u);
    Require(RefineryConstructionRender.ImageID == 307u && HasConstructionGasOverlay,
        "initial Refinery construction uses its main image and creates the gas overlay");

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
    const auto* RefineryTree =
        EM.GetComponent<Game::Components::ImageTreeComponent>(CompletedRefinery);
    bool HasRefineryOverlay = false;
    if (RefineryTree)
        for (const auto& Node : RefineryTree->Nodes)
            HasRefineryOverlay = HasRefineryOverlay ||
                (Node.Visible && Node.ImageID == 308u);
    Require(HasRefineryOverlay,
        "completed Refinery runs Init once to create its gas overlay image");
    Anim.Update(1.0f / 24.0f);
    Require(RefineryRender.FrameIndex == 0,
        "completed Refinery advances from Init to its Built frame");

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

    // Verify IScript Turn opcodes (TurnCWise, Turn1CWise, TurnCCWise, TurnRand)
    static const std::vector<int> TurnProgram = {
        Game::IScriptOpcodes::Opc_TurnCWise, 2,   // +2 32-way steps (+16 in 0..255)
        Game::IScriptOpcodes::Opc_Wait, 1,
        Game::IScriptOpcodes::Opc_Turn1CWise,     // +1 32-way step (+8 in 0..255)
        Game::IScriptOpcodes::Opc_Wait, 1,
        Game::IScriptOpcodes::Opc_TurnCCWise, 1,  // -1 32-way step (-8 in 0..255)
        Game::IScriptOpcodes::Opc_Wait, 1,
        Game::IScriptOpcodes::Opc_End
    };
    Game::IScriptVM TurnVM;
    TurnVM.GetContext().HasDirectionalFrames = true;
    TurnVM.GetContext().BaseFrameIndex = 0;
    TurnVM.GetContext().CurrentDirection = 0;
    TurnVM.LoadScript(Game::IScriptAnims::Anim_Init, &TurnProgram, 0);
    TurnVM.ExecuteFrame();
    Require(TurnVM.GetContext().CurrentDirection == 16,
            "TurnCWise advances CurrentDirection by 2 steps (16 units)");
    TurnVM.ExecuteFrame();
    Require(TurnVM.GetContext().CurrentDirection == 24,
            "Turn1CWise advances CurrentDirection by 1 step (8 units)");
    TurnVM.ExecuteFrame();
    Require(TurnVM.GetContext().CurrentDirection == 16,
            "TurnCCWise reverses CurrentDirection by 1 step (8 units)");

    // Verify full 32-way directional frame resolution across 360 degrees
    std::set<std::pair<int, bool>> ThirtyTwoWaySignatures;
    const size_t BaseEntityCount = EM.GetEntitiesWith<Game::Components::RenderComponent>().size();
    for (int Step = 0; Step < 32; ++Step)
    {
        const float Angle = (float)Step * (6.28318530718f / 32.0f);
        const int EntityID = SpawnIdleDirectionalUnit(EM, Angle, 256, kDirectionalImage);
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
        Path->FacingAngle = Angle;
        Path->CurrentSpeed = 64.0f;
        Path->CurrentVelocity = {std::cos(Angle) * 64.0f, std::sin(Angle) * 64.0f};
    }
    Anim.Update(1.0f / 24.0f);
    const auto AllRenderEntities = EM.GetEntitiesWith<Game::Components::RenderComponent>();
    for (size_t Idx = BaseEntityCount; Idx < AllRenderEntities.size(); ++Idx)
    {
        const auto* Render = EM.GetComponent<Game::Components::RenderComponent>(AllRenderEntities[Idx]);
        if (Render)
            ThirtyTwoWaySignatures.emplace(Render->FrameIndex, Render->Flipped);
    }
    Require(ThirtyTwoWaySignatures.size() >= 17,
            "full 32-way directional coverage spans all 17 mirrored GRP frames");

    Game::Systems::AnimationSystem::SetOverlayOffsetResolver({});
    const int WalkingOffsetUnit = SpawnWalkingDirectionalUnit(EM, 512.0f);
    for (int Frame = 0; Frame < 24; ++Frame)
    {
        Anim.Update(1.0f / 24.0f);
    }
    const auto* WalkingRender = EM.GetComponent<Game::Components::RenderComponent>(WalkingOffsetUnit);
    Require(WalkingRender != nullptr, "walking unit still has RenderComponent");
    Require(std::fabs(WalkingRender->VisualOffset.X) < 0.0001f &&
                std::fabs(WalkingRender->VisualOffset.Y) < 0.0001f,
            "Opc_Move during walking does not corrupt Render.VisualOffset away from unit shadow");

    // 1. Verify Dragoon impact effect (Image 523 -> imgol 427) completes and destroys itself without restarting
    {
        const int ImpactEffect = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(ImpactEffect).Position = {640.0f, 640.0f};
        auto& DragoonImpactRender = EM.AddComponent<Game::Components::RenderComponent>(ImpactEffect);
        DragoonImpactRender.ImageID = 523u;
        DragoonImpactRender.TotalFrames = 32;
        auto& EffectComp = EM.AddComponent<Game::Components::EffectComponent>(ImpactEffect);
        EffectComp.InitialAnimation = Game::IScriptAnims::Anim_Death;
        EffectComp.LifetimeRemaining = 8.0f;
        EffectComp.DestroyWhenAnimationCompletes = true;
        for (int Step = 0; Step < 24; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
            if (!EM.IsAlive(ImpactEffect))
                break;
        }
        Require(!EM.IsAlive(ImpactEffect),
                "Dragoon projectile impact effect (Image 523 -> child 427) finishes and destroys itself within 24 ticks instead of looping for 8s");
    }

    // 2. Verify Dark Archon (Image 925) and Archon (Image 134) preserve inner body (926/135) and swirl (927/136) across walking/idle
    for (uint32_t OuterImage : {134u, 925u})
    {
        const uint32_t ExpectedBody = (OuterImage == 134u) ? 135u : 926u;
        const uint32_t ExpectedSwirl = (OuterImage == 134u) ? 136u : 927u;
        const int ArchonEntity = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(ArchonEntity).Position = {700.0f, 700.0f};
        auto& ArchonRender = EM.AddComponent<Game::Components::RenderComponent>(ArchonEntity);
        ArchonRender.ImageID = OuterImage;
        ArchonRender.TotalFrames = 64;
        auto& ArchonUnit = EM.AddComponent<Game::Components::UnitComponent>(ArchonEntity);
        ArchonUnit.ChkUnitID = (OuterImage == 134u) ? 68 : 63;
        ArchonUnit.HitPoints = 10;
        ArchonUnit.MaxHitPoints = 10;
        auto& ArchonPath = EM.AddComponent<Game::Components::PathfindingComponent>(ArchonEntity);
        ArchonPath.CurrentSpeed = 64.0f;
        ArchonPath.CurrentVelocity = {64.0f, 0.0f};
        ArchonPath.Path = {{764.0f, 700.0f}};
        for (int Step = 0; Step < 24; ++Step)
            Anim.Update(1.0f / 24.0f);
        ArchonPath.CurrentSpeed = 0.0f;
        ArchonPath.CurrentVelocity = {};
        ArchonPath.Path.clear();
        for (int Step = 0; Step < 6; ++Step)
            Anim.Update(1.0f / 24.0f);
        const auto* ArchonTree = EM.GetComponent<Game::Components::ImageTreeComponent>(ArchonEntity);
        bool FoundBody = false;
        bool FoundSwirl = false;
        if (ArchonTree)
        {
            for (const auto& Node : ArchonTree->Nodes)
            {
                if (!Node.Visible)
                    continue;
                if (Node.ImageID == ExpectedBody && Node.PersistentSpriteAttachment)
                    FoundBody = true;
                if (Node.ImageID == ExpectedSwirl && Node.PersistentSpriteAttachment)
                    FoundSwirl = true;
            }
        }
        Require(FoundBody && FoundSwirl,
                "Archon and Dark Archon retain visible persistent inner body and swirl overlays across state transitions");
    }

    // 3. Verify Zerg building morph cocoon (Image 102) stays on SpecialState2 at 70% progress and only enters AlmostBuilt in final 0.30s
    {
        const int ZergCocoon = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(ZergCocoon).Position = {800.0f, 800.0f};
        auto& CocoonRender = EM.AddComponent<Game::Components::RenderComponent>(ZergCocoon);
        CocoonRender.ImageID = 102u;
        CocoonRender.TotalFrames = 64;
        auto& CocoonUnit = EM.AddComponent<Game::Components::UnitComponent>(ZergCocoon);
        CocoonUnit.ChkUnitID = 142; // Spawning Pool
        CocoonUnit.UnitRace = Game::Components::Race::Zerg;
        CocoonUnit.HitPoints = 500;
        CocoonUnit.MaxHitPoints = 750;
        auto& CocoonBldg = EM.AddComponent<Game::Components::BuildingComponent>(ZergCocoon);
        CocoonBldg.BuildingDefID = 1;
        CocoonBldg.UnderConstruction = true;
        CocoonBldg.TotalBuildTime = 40.0f;
        CocoonBldg.BuildProgress = 28.0f; // 70% progress (12s remaining)
        for (int Step = 0; Step < 8; ++Step)
            Anim.Update(1.0f / 24.0f);
        Require(CocoonRender.FrameIndex < 28,
                "Zerg morph cocoon at 70% progress loops SpecialState2 pulsing frames instead of freezing on AlmostBuilt frame 30");

        // Now complete construction and switch to Spawning Pool (Image 57)
        CocoonBldg.BuildProgress = 40.0f;
        CocoonBldg.UnderConstruction = false;
        CocoonRender.ImageID = 57u;
        for (int Step = 0; Step < 12; ++Step)
            Anim.Update(1.0f / 24.0f);
        const auto* PoolTree = EM.GetComponent<Game::Components::ImageTreeComponent>(ZergCocoon);
        bool BurstStillVisible = false;
        if (PoolTree)
        {
            for (const auto& Node : PoolTree->Nodes)
            {
                if (Node.Visible && (Node.ImageID >= 107u && Node.ImageID <= 109u))
                    BurstStillVisible = true;
            }
        }
        Require(!BurstStillVisible,
                "Finished Zerg building slime burst overlay (107..109) finishes within 12 ticks and does not freeze");
    }

    // 4. Verify Terran Refinery construction -> completion and pre-completed Refinery hold Built frame 0 across ticks
    {
        Require(RefineryConstructionRender.FrameIndex == 1,
                "Refinery construction at 0% holds Init frame 1");
        RefineryConstructionBuilding.BuildProgress = 10.0f;
        Anim.Update(1.0f / 24.0f);
        Require(RefineryConstructionRender.FrameIndex == 2,
                "Refinery construction at 25% selects SpecialState1 frame 2");
        RefineryConstructionBuilding.BuildProgress = 18.0f;
        Anim.Update(1.0f / 24.0f);
        Require(RefineryConstructionRender.FrameIndex == 3,
                "Refinery construction at 45% selects SpecialState2 frame 3");
        RefineryConstructionBuilding.BuildProgress = 26.0f;
        Anim.Update(1.0f / 24.0f);
        Require(RefineryConstructionRender.FrameIndex == 4,
                "Refinery construction at 65% selects AlmostBuilt frame 4");
        RefineryConstructionBuilding.BuildProgress = 40.0f;
        RefineryConstructionBuilding.UnderConstruction = false;
        for (int Step = 0; Step < 24; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
            const auto* ConstructedTree =
                EM.GetComponent<Game::Components::ImageTreeComponent>(RefineryConstruction);
            bool ConstructedOverlayVisible = false;
            if (ConstructedTree)
            {
                for (const auto& Node : ConstructedTree->Nodes)
                {
                    ConstructedOverlayVisible = ConstructedOverlayVisible ||
                        (Node.Visible && Node.ImageID == 308u);
                }
            }
            Require(RefineryConstructionRender.FrameIndex == 0 && ConstructedOverlayVisible,
                    "completed Refinery from construction holds Built frame 0 and overlay 308 across subsequent ticks");
            const auto* PreCompletedTree =
                EM.GetComponent<Game::Components::ImageTreeComponent>(CompletedRefinery);
            bool PreCompletedOverlayVisible = false;
            if (PreCompletedTree)
            {
                for (const auto& Node : PreCompletedTree->Nodes)
                {
                    PreCompletedOverlayVisible = PreCompletedOverlayVisible ||
                        (Node.Visible && Node.ImageID == 308u);
                }
            }
            Require(RefineryRender.FrameIndex == 0 && PreCompletedOverlayVisible,
                    "pre-completed Refinery holds Built frame 0 and overlay 308 across subsequent ticks without reverting to Init frame 1");
        }
    }

    // 5. Verify Zerg Extractor construction -> completion across 60 ticks and CreateGasOverlays uses VM's current frame
    {
        std::vector<size_t> ExtractorGasOverlayFrames;
        Game::Systems::AnimationSystem::SetOverlayOffsetResolver(
            [&](uint32_t ImageID, size_t OverlayType, size_t Frame,
                size_t OverlayIndex, bool /*Flipped*/) -> std::optional<Core::Math::Vec2>
            {
                if (ImageID == 93u && OverlayType == 2u && OverlayIndex == 0u)
                {
                    ExtractorGasOverlayFrames.push_back(Frame);
                }
                return Core::Math::Vec2{0.0f, -16.0f};
            });

        const int Extractor = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Extractor).Position = {900.0f, 800.0f};
        auto& ExtractorResource = EM.AddComponent<Game::Components::ResourceComponent>(Extractor);
        ExtractorResource.Type = Game::Components::ResourceType::VespeneGas;
        ExtractorResource.Amount = 2500;
        auto& ExtractorRender = EM.AddComponent<Game::Components::RenderComponent>(Extractor);
        ExtractorRender.ImageID = 105u; // Zerg Extractor morph cocoon
        ExtractorRender.TotalFrames = 16;
        auto& ExtractorUnit = EM.AddComponent<Game::Components::UnitComponent>(Extractor);
        ExtractorUnit.ChkUnitID = 149; // Zerg Extractor
        ExtractorUnit.UnitRace = Game::Components::Race::Zerg;
        ExtractorUnit.HitPoints = 750;
        ExtractorUnit.MaxHitPoints = 750;
        auto& ExtractorBldg = EM.AddComponent<Game::Components::BuildingComponent>(Extractor);
        ExtractorBldg.BuildingDefID = 149;
        ExtractorBldg.UnderConstruction = true;
        ExtractorBldg.TotalBuildTime = 40.0f;
        ExtractorBldg.BuildProgress = 20.0f;
        for (int Step = 0; Step < 8; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
        }

        // Complete Extractor construction -> switches to Image 93 (Script 56)
        ExtractorBldg.BuildProgress = 40.0f;
        ExtractorBldg.UnderConstruction = false;
        ExtractorRender.ImageID = 93u;
        bool SawAnimatedPulseFrame = false;
        bool SawBurstOverlay = false;
        for (int Step = 0; Step < 60; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
            if (ExtractorRender.FrameIndex >= 1 && ExtractorRender.FrameIndex <= 3)
            {
                SawAnimatedPulseFrame = true;
            }
            const auto* ExtractorTree =
                EM.GetComponent<Game::Components::ImageTreeComponent>(Extractor);
            bool ShadowVisible = false;
            if (ExtractorTree)
            {
                for (const auto& Node : ExtractorTree->Nodes)
                {
                    if (!Node.Visible)
                        continue;
                    if (Node.ImageID == 94u)
                        ShadowVisible = true;
                    if (Node.ImageID == 109u)
                        SawBurstOverlay = true;
                }
            }
            Require(ShadowVisible,
                    "completed Zerg Extractor keeps shadow (Image 94) visible across 60 post-completion ticks");
        }
        Require(SawBurstOverlay && SawAnimatedPulseFrame,
                "completed Zerg Extractor plays AlmostBuilt burst (Image 109) and loops Built pulsing frames (1..3)");
        Require(!ExtractorGasOverlayFrames.empty(),
                "completed Zerg Extractor spawns gas overlays during 60 post-completion ticks");
        for (size_t GasFrame : ExtractorGasOverlayFrames)
        {
            Require(GasFrame == 3u,
                    "Zerg Extractor CreateGasOverlays resolves LO offset at peak pulse frame 3");
        }
        Game::Systems::AnimationSystem::SetOverlayOffsetResolver({});
    }

    // 6. Verify Protoss Assimilator Materializing -> WarpIn -> completion holds frame 0 and shadow (Image 160) across 60 ticks
    {
        const int Assimilator = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Assimilator).Position = {1000.0f, 800.0f};
        auto& AssimilatorResource = EM.AddComponent<Game::Components::ResourceComponent>(Assimilator);
        AssimilatorResource.Type = Game::Components::ResourceType::VespeneGas;
        AssimilatorResource.Amount = 2500;
        auto& AssimilatorRender = EM.AddComponent<Game::Components::RenderComponent>(Assimilator);
        AssimilatorRender.ImageID = 158u;
        AssimilatorRender.TotalFrames = 4;
        auto& AssimilatorUnit = EM.AddComponent<Game::Components::UnitComponent>(Assimilator);
        AssimilatorUnit.ChkUnitID = 157; // Protoss Assimilator
        AssimilatorUnit.UnitRace = Game::Components::Race::Protoss;
        AssimilatorUnit.HitPoints = 450;
        AssimilatorUnit.MaxHitPoints = 450;
        auto& AssimilatorBldg = EM.AddComponent<Game::Components::BuildingComponent>(Assimilator);
        AssimilatorBldg.BuildingDefID = 157;
        AssimilatorBldg.UnderConstruction = true;
        AssimilatorBldg.ProtossPhase = Game::Components::ProtossConstructionPhase::Materializing;
        AssimilatorBldg.TotalBuildTime = 40.0f;
        AssimilatorBldg.BuildProgress = 40.0f;

        for (int Step = 0; Step < 24 && !AssimilatorBldg.ProtossPhaseSignal; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
        }
        Require(AssimilatorBldg.ProtossPhaseSignal,
                "Protoss Assimilator Materializing script (Script 193) emits sigorder 1 completion signal");

        // Advance to WarpIn phase
        AssimilatorBldg.ProtossPhase = Game::Components::ProtossConstructionPhase::WarpIn;
        AssimilatorBldg.ProtossPhaseSignal = false;
        for (int Step = 0; Step < 6; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
        }

        // Complete construction
        AssimilatorBldg.UnderConstruction = false;
        AssimilatorBldg.ProtossPhase = Game::Components::ProtossConstructionPhase::None;

        for (int Step = 0; Step < 60; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
            const auto* AssimilatorTree =
                EM.GetComponent<Game::Components::ImageTreeComponent>(Assimilator);
            bool ShadowVisible = false;
            if (AssimilatorTree)
            {
                for (const auto& Node : AssimilatorTree->Nodes)
                {
                    if (Node.Visible && Node.ImageID == 160u)
                        ShadowVisible = true;
                }
            }
            Require(AssimilatorRender.FrameIndex == 0 && ShadowVisible,
                    "completed Protoss Assimilator holds frame 0 and shadow (Image 160) across 60 post-completion ticks");
        }
    }

    // 7. Verify Zerg Hatchery -> Lair morph resets HasPlayedBuilt and replays AlmostBuilt -> Built on completion
    {
        const int Hatchery = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(Hatchery).Position = {1100.0f, 800.0f};
        auto& HatcheryRender = EM.AddComponent<Game::Components::RenderComponent>(Hatchery);
        HatcheryRender.ImageID = 70u; // Hatchery
        HatcheryRender.TotalFrames = 8;
        auto& HatcheryUnit = EM.AddComponent<Game::Components::UnitComponent>(Hatchery);
        HatcheryUnit.ChkUnitID = 131; // Hatchery
        HatcheryUnit.UnitRace = Game::Components::Race::Zerg;
        HatcheryUnit.HitPoints = 1250;
        HatcheryUnit.MaxHitPoints = 1250;
        auto& HatcheryBldg = EM.AddComponent<Game::Components::BuildingComponent>(Hatchery);
        HatcheryBldg.BuildingDefID = 131;
        HatcheryBldg.UnderConstruction = false;
        for (int Step = 0; Step < 12; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
        }

        // Start morphing Hatchery into Lair (ChkUnitID 132, cocoon Image 104 -> final Image 74)
        HatcheryUnit.ChkUnitID = 132;
        HatcheryBldg.BuildingDefID = 132;
        HatcheryBldg.UnderConstruction = true;
        HatcheryBldg.TotalBuildTime = 40.0f;
        HatcheryBldg.BuildProgress = 10.0f;
        HatcheryRender.ImageID = 104u;
        for (int Step = 0; Step < 8; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
        }

        // Complete morph into Lair (Image 74)
        HatcheryBldg.BuildProgress = 40.0f;
        HatcheryBldg.UnderConstruction = false;
        HatcheryRender.ImageID = 74u;
        bool SawLairBurst = false;
        bool SawLairPulse = false;
        for (int Step = 0; Step < 60; ++Step)
        {
            Anim.Update(1.0f / 24.0f);
            if (HatcheryRender.FrameIndex >= 1 && HatcheryRender.FrameIndex <= 3)
            {
                SawLairPulse = true;
            }
            const auto* LairTree =
                EM.GetComponent<Game::Components::ImageTreeComponent>(Hatchery);
            bool LairShadowVisible = false;
            if (LairTree)
            {
                for (const auto& Node : LairTree->Nodes)
                {
                    if (!Node.Visible)
                        continue;
                    if (Node.ImageID == 75u)
                        LairShadowVisible = true;
                    if (Node.ImageID >= 107u && Node.ImageID <= 109u)
                        SawLairBurst = true;
                }
            }
            Require(LairShadowVisible,
                    "completed Zerg Lair after Hatchery morph keeps shadow (Image 75) visible across 60 ticks");
        }
        Require(SawLairBurst && SawLairPulse,
                "Zerg Hatchery -> Lair morph resets HasPlayedBuilt during morph and plays Lair AlmostBuilt burst + Built pulse loop");
    }

    Game::Systems::AnimationSystem::SetWeaponImageResolver({});

    std::printf("AnimationRuntimeDirectionTest passed\n");
    return 0;
}
