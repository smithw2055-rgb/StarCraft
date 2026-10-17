#pragma once

#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/IScriptVM.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace Core::Audio { class AudioSystem; }

namespace Game::Systems
{

class BulletSystem;

enum class AnimState : uint8_t
{
    Idle,
    Walking,
    Attacking,
    Dying,
    Constructing,
    Morphing,
    Burrowing,
    Burrowed,
    Unburrowing,
    LiftingOff,
    Flying,
    Landing,
    Count
};

struct AnimClip
{
    AnimState State = AnimState::Idle;
    int StartFrame = 0;
    int FrameCount = 1;
    float FrameDuration = 0.15f;
    bool Loop = true;
    bool Flipped = false;
};

class AnimationSystem : public Core::ECS::ISystem
{
public:
    using OverlayOffsetResolver = std::function<std::optional<Core::Math::Vec2>(
        uint32_t, size_t, size_t, size_t, bool)>;
    using WeaponImageResolver = std::function<std::optional<uint32_t>(int)>;
    using SpriteImageResolver = std::function<std::optional<uint32_t>(int)>;
    // Images.rel marks only shadows and glow overlays as static attachments.
    // Transition images created by Init must be allowed to finish and disappear.
    using ImageAttachmentPersistenceResolver = std::function<bool(uint32_t)>;

    AnimationSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    AnimationSystem(Core::ECS::SystemManager& SysMgr, Core::Audio::AudioSystem* Audio)
        : mSystemMgr(SysMgr), mAudio(Audio) {}
    AnimationSystem(Core::ECS::SystemManager& SysMgr, Core::Audio::AudioSystem* Audio, BulletSystem* BulletSys)
        : mSystemMgr(SysMgr), mAudio(Audio), mBulletSystem(BulletSys) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "AnimationSystem"; }

    static void RegisterUnitAnim(int ChkUnitID, const std::vector<AnimClip>& Clips);
    // Configure and eagerly validate the project-owned iscript tables before
    // entities are spawned. Absolute paths keep menu launches independent of
    // the process working directory (for example build/Release under VS).
    static bool InitializeIScriptAssets(const std::string& IScriptPath,
                                        const std::string& ImagesDatPath);
    static void SetOverlayOffsetResolver(OverlayOffsetResolver Resolver);
    static void SetWeaponImageResolver(WeaponImageResolver Resolver);
    static void SetSpriteImageResolver(SpriteImageResolver Resolver);
    static void SetImageAttachmentPersistenceResolver(
        ImageAttachmentPersistenceResolver Resolver);
    static bool HasIScriptAnimationForImage(int ImageID, int AnimID);
    static int ComputeDamageOverlaySteps(int HitPoints,
                                         int HitPointSubDamage,
                                         int MaxHitPoints,
                                         int DamageSlotCount);
    static int ResolvePlayableIScriptAnimation(int AnimID,
                                               const std::function<bool(int)>& HasAnim);
    static int SelectIScriptAnimationForState(AnimState DesiredState,
                                              bool IsBuilding,
                                              bool UnderConstruction,
                                              float ConstructionProgress,
                                              bool WasUnderConstruction,
                                              bool IsWorking,
                                              bool WasWorking,
                                              bool WasWalking,
                                              bool IsPendingAttack,
                                              const std::function<bool(int)>& HasAnim);
    static bool ShouldClearImageTreeForAnimRestart(bool IsBuilding,
                                                   bool ImageOrScriptChanged,
                                                   int PreviousAnim,
                                                   int TargetAnim,
                                                   bool UnderConstruction);
    static AnimState GetAnimStateFromComponents(const Components::CommandComponent* Cmd,
                                                  const Components::CombatComponent* Combat,
                                                  const Components::PathfindingComponent* Path,
                                                  const Components::UnitComponent* Unit,
                                                  const Components::BuildingComponent* Bldg,
                                                  const Components::MorphComponent* Morph,
                                                  const Components::BurrowComponent* Burrow,
                                                  const Components::StatusEffectComponent* Status);
private:
    struct EntityScriptState
    {
        Game::IScriptVM VM;
        int ImageID = -1;
        int ScriptID = -1;
        int AnimID = -1;
        bool WasWorking = false;
        bool WasMiningMinerals = false;
        bool WasConstructing = false;
        bool WasUnderConstruction = false;
        bool WasWalking = false;
        bool WasPendingAttack = false;
        bool HasPlayedBuilt = false;
        float StepAccumulator = 0.0f;
    };

    struct UnitAnimData
    {
        std::vector<AnimClip> Clips;
    };
    static std::unordered_map<int, UnitAnimData> sAnimDB;
    static Game::IScriptDatabase sIScriptDB;
    static bool sTriedLoadIScript;
    static std::string sIScriptPath;
    static std::string sImagesDatPath;
    static OverlayOffsetResolver sOverlayOffsetResolver;
    static WeaponImageResolver sWeaponImageResolver;
    static SpriteImageResolver sSpriteImageResolver;
    static ImageAttachmentPersistenceResolver sImageAttachmentPersistenceResolver;

    Core::ECS::SystemManager& mSystemMgr;
    Core::Audio::AudioSystem* mAudio = nullptr;
    BulletSystem* mBulletSystem = nullptr;
    std::unordered_map<int, EntityScriptState> mScriptStates;

    static void EnsureIScriptLoaded();
    static int DirectionalFrameIndex(int BaseFrame,
                                     int Direction,
                                     int TotalFrames,
                                     bool HasDirectionalFrames,
                                     bool* OutFlipped = nullptr);
    void UpdateScriptEffects(float DeltaTime);
    void UpdateShieldImpactOverlays();
    void UpdateImageTrees(float DeltaTime);
    void UpdateCarriedResourceOverlay(int EntityID,
                                      Components::ImageTreeComponent& Tree,
                                      const Components::UnitComponent* Unit,
                                      const Components::HarvestComponent* Harvest);
    void EnsureUnitSubunitNode(int EntityID,
                               const Components::RenderComponent& Render);
    void UpdateUnitSubunitAttackLifecycle(
        int EntityID,
        const Components::CombatComponent* Combat);
    void UpdateAddonConnectorOverlay(
        int EntityID,
        const Components::RenderComponent& Render);
    void UpdateDamageOverlays(int EntityID,
                              const Components::RenderComponent& Render,
                              const Components::UnitComponent* Unit,
                              const Components::BuildingComponent* Building);
    void UpdateCommandCenterProductionVisual(
        int EntityID,
        const Components::RenderComponent& Render,
        const Components::ProductionComponent* Production,
        float DeltaTime);
    void ClearImageTree(int EntityID, bool PreservePersistentAttachments = false);
    void MarkImageSubtreeHidden(Components::ImageTreeComponent& Tree, int ParentIndex);
    void AddImageTreeNode(int EntityID,
                          int ImageID,
                          int OffsetX,
                          int OffsetY,
                          bool DrawAbove,
                          bool Flipped,
                          float Lifetime,
                          bool UseSpriteID = false,
                          bool PersistentSpriteAttachment = false,
                          bool UseSpecialOffset = false);
    void AddImageTreeNode(int EntityID,
                          int ParentIndex,
                          int ImageID,
                          Core::Math::Vec2 Offset,
                          bool DrawAbove,
                          bool Flipped,
                          float Lifetime,
                          bool UseSpriteID = false,
                          bool PersistentSpriteAttachment = false,
                          bool UseSpecialOffset = false);
    void StartImageNodeScript(Components::ImageSubnode& Node);
    void RunImageTreeAnimation(int EntityID, int AnimID);
    void RunUnitSubunitAnimation(int EntityID, int AnimID);
    void CreateGasOverlay(int EntityID,
                          int ParentIndex,
                          int SourceImageID,
                          int SourceFrame,
                          bool SourceFlipped,
                          int OffsetIndex,
                          float Lifetime);
    void ConsumeImageNodeEvents(int EntityID,
                                int NodeIndex,
                                Core::Math::Vec2 NodeOffset,
                                float NodeLifetime,
                                const Components::TransformComponent& Trans,
                                const Components::PlayerComponent* Player,
                                Components::CombatComponent* Combat);
    void SpawnIScriptOverlay(int ParentEntityID,
                             const Components::TransformComponent& ParentTrans,
                             int ImageID,
                             int OffsetX,
                             int OffsetY,
                             float Lifetime,
                             bool FollowParent);
    bool SpawnIScriptSpriteEffect(
        int ParentEntityID,
        const Components::TransformComponent& ParentTrans,
        int SpriteID,
        Core::Math::Vec2 WorldOffset,
        bool DrawAbove,
        const Components::PlayerComponent* Player,
        int IScriptDirection = -1);
    void ConsumeIScriptEvents(int EntityID,
                              EntityScriptState& State,
                              const Components::TransformComponent& Trans,
                              const Components::PlayerComponent* Player,
                              Components::CombatComponent* Combat,
                              const Components::HarvestComponent* Harvest);
    bool SpawnHarvestWeaponVisual(int EntityID,
                                  const Components::TransformComponent& Trans,
                                  const Components::HarvestComponent* Harvest,
                                  int WeaponID,
                                  bool ParentFlipped);
    bool UpdateIScriptAnimation(int EntityID,
                                float DeltaTime,
                                AnimState DesiredState,
                                Components::RenderComponent& Render,
                                const Components::TransformComponent& Trans,
                                const Components::PathfindingComponent* Path,
                                const Components::PlayerComponent* Player,
                                Components::CombatComponent* Combat,
                                Components::BuildingComponent* Bldg,
                                const Components::StatusEffectComponent* Status,
                                const Components::ProductionComponent* Prod,
                                const Components::HarvestComponent* Harvest,
                                Components::BulletComponent* Bullet);
};

inline int AnimationSystem::ResolvePlayableIScriptAnimation(
    int AnimID,
    const std::function<bool(int)>& HasAnim)
{
    if (HasAnim(AnimID))
        return AnimID;

    if ((AnimID == Game::IScriptAnims::Anim_AlmostBuilt ||
         AnimID == Game::IScriptAnims::Anim_Built) &&
        HasAnim(Game::IScriptAnims::Anim_Init))
    {
        return Game::IScriptAnims::Anim_Init;
    }

    return -1;
}

inline int AnimationSystem::ComputeDamageOverlaySteps(
    int HitPoints,
    int HitPointSubDamage,
    int MaxHitPoints,
    int DamageSlotCount)
{
    if (MaxHitPoints <= 0 || DamageSlotCount <= 0)
        return 0;

    constexpr int kSubPointsPerPoint = 256;
    const int64_t MaxRaw = (int64_t)MaxHitPoints * kSubPointsPerPoint;
    const int64_t HitPointsRaw = std::clamp<int64_t>(
        (int64_t)HitPoints * kSubPointsPerPoint -
            std::clamp(HitPointSubDamage, 0, kSubPointsPerPoint - 1),
        0, MaxRaw);
    const int DamageStates = DamageSlotCount * 2;
    const int64_t TwoThirdsMaxRaw = MaxRaw - MaxRaw / 3;
    const int64_t HitPointsPerState =
        std::max<int64_t>(1, TwoThirdsMaxRaw / (DamageStates + 1));
    const int64_t Numerator =
        HitPointsRaw - TwoThirdsMaxRaw % HitPointsPerState -
        MaxRaw / 3 - 1;
    const int DamageState = std::clamp(
        Numerator > 0 ? (int)(Numerator / HitPointsPerState) : 0,
        0, DamageStates);
    return DamageStates - DamageState;
}

inline int AnimationSystem::SelectIScriptAnimationForState(
    AnimState DesiredState,
    bool IsBuilding,
    bool UnderConstruction,
    float ConstructionProgress,
    bool WasUnderConstruction,
    bool IsWorking,
    bool WasWorking,
    bool WasWalking,
    bool IsPendingAttack,
    const std::function<bool(int)>& HasAnim)
{
    if (DesiredState == AnimState::Dying && HasAnim(Game::IScriptAnims::Anim_Death))
        return Game::IScriptAnims::Anim_Death;

    if (IsPendingAttack && HasAnim(Game::IScriptAnims::Anim_GndAttkInit))
        return Game::IScriptAnims::Anim_GndAttkInit;
    if (IsPendingAttack && HasAnim(Game::IScriptAnims::Anim_AirAttkInit))
        return Game::IScriptAnims::Anim_AirAttkInit;

    if (IsBuilding && DesiredState == AnimState::LiftingOff &&
        HasAnim(Game::IScriptAnims::Anim_LiftOff))
    {
        return Game::IScriptAnims::Anim_LiftOff;
    }
    if (IsBuilding && DesiredState == AnimState::Landing &&
        HasAnim(Game::IScriptAnims::Anim_Landing))
    {
        return Game::IScriptAnims::Anim_Landing;
    }

    if (!IsBuilding)
    {
        if ((DesiredState == AnimState::Burrowing ||
             DesiredState == AnimState::Burrowed) &&
            HasAnim(Game::IScriptAnims::Anim_Burrow))
        {
            return Game::IScriptAnims::Anim_Burrow;
        }
        if (DesiredState == AnimState::Unburrowing &&
            HasAnim(Game::IScriptAnims::Anim_UnBurrow))
        {
            return Game::IScriptAnims::Anim_UnBurrow;
        }
        if (DesiredState == AnimState::Walking && HasAnim(Game::IScriptAnims::Anim_Walking))
            return Game::IScriptAnims::Anim_Walking;
        if (WasWalking && HasAnim(Game::IScriptAnims::Anim_WalkingToIdle))
            return Game::IScriptAnims::Anim_WalkingToIdle;
        if (DesiredState == AnimState::Idle && HasAnim(Game::IScriptAnims::Anim_Init))
            return Game::IScriptAnims::Anim_Init;
        return -1;
    }

    if (UnderConstruction)
    {
        // Terran construction graphics use Init for the bare foundation,
        // SpecialState1 after 1/5 HP, SpecialState2 after 2/5 HP, and the
        // finished graphic's AlmostBuilt sequence after BuildingSystem swaps
        // it at 3/5 HP. These thresholds also provide useful staged fallbacks
        // for the Zerg/Protoss construction scripts when entries are present.
        if (ConstructionProgress < 0.2f &&
            HasAnim(Game::IScriptAnims::Anim_Init))
        {
            return Game::IScriptAnims::Anim_Init;
        }
        if (ConstructionProgress < 0.4f &&
            HasAnim(Game::IScriptAnims::Anim_SpecialState1))
        {
            return Game::IScriptAnims::Anim_SpecialState1;
        }
        if (ConstructionProgress < 0.6f &&
            HasAnim(Game::IScriptAnims::Anim_SpecialState2))
        {
            return Game::IScriptAnims::Anim_SpecialState2;
        }
        if (ConstructionProgress >= 0.6f)
        {
            const int AlmostBuilt =
                ResolvePlayableIScriptAnimation(Game::IScriptAnims::Anim_AlmostBuilt, HasAnim);
            if (AlmostBuilt >= 0)
                return AlmostBuilt;
        }
        if (HasAnim(Game::IScriptAnims::Anim_SpecialState1))
            return Game::IScriptAnims::Anim_SpecialState1;
        if (HasAnim(Game::IScriptAnims::Anim_SpecialState2))
            return Game::IScriptAnims::Anim_SpecialState2;
        if (HasAnim(Game::IScriptAnims::Anim_Init))
            return Game::IScriptAnims::Anim_Init;
        return -1;
    }

    if (IsWorking && HasAnim(Game::IScriptAnims::Anim_IsWorking))
        return Game::IScriptAnims::Anim_IsWorking;
    if (WasWorking && HasAnim(Game::IScriptAnims::Anim_WorkingToIdle))
        return Game::IScriptAnims::Anim_WorkingToIdle;
    if (WasUnderConstruction)
    {
        const int BuiltAnim =
            ResolvePlayableIScriptAnimation(Game::IScriptAnims::Anim_Built, HasAnim);
        if (BuiltAnim >= 0)
            return BuiltAnim;
    }
    if (HasAnim(Game::IScriptAnims::Anim_Init))
        return Game::IScriptAnims::Anim_Init;
    return -1;
}

inline bool AnimationSystem::ShouldClearImageTreeForAnimRestart(
    bool IsBuilding,
    bool ImageOrScriptChanged,
    int /*PreviousAnim*/,
    int TargetAnim,
    bool /*UnderConstruction*/)
{
    // OpenBW's sprite_run_anim(Death) sends Death to every image already
    // attached to the sprite (turrets, shadows, flames, carried graphics).
    // Preserve that tree even on the freshly-created presentation entity.
    if (TargetAnim == Game::IScriptAnims::Anim_Death)
        return false;
    if (ImageOrScriptChanged)
        return true;

    // Mobile attack/movement effects remain transient runtime overlays. A
    // building sprite, however, owns a stable list of images: state changes
    // are propagated to those images by RunImageTreeAnimation().
    return !IsBuilding;
}

inline AnimState AnimationSystem::GetAnimStateFromComponents(
    const Components::CommandComponent* Cmd,
    const Components::CombatComponent* Combat,
    const Components::PathfindingComponent* Path,
    const Components::UnitComponent* Unit,
    const Components::BuildingComponent* Bldg,
    const Components::MorphComponent* Morph,
    const Components::BurrowComponent* Burrow,
    const Components::StatusEffectComponent* Status)
{
    if (Unit && Unit->HitPoints <= 0) return AnimState::Dying;
    // A Zerg building morph (Hatchery -> Lair -> Hive, Spire -> Greater
    // Spire) is still building construction visually.  Eggs also carry a
    // BuildingComponent, but have no building definition and must retain the
    // ordinary unit-morph animation.
    if (Bldg && Bldg->UnderConstruction && Bldg->BuildingDefID >= 0)
        return AnimState::Constructing;
    if (Morph && Morph->IsMorphing) return AnimState::Morphing;
    if (Bldg &&
        Bldg->FlightPhase == Components::BuildingFlightPhase::LiftingOff &&
        !Bldg->FlightHeightTransitionActive)
        return AnimState::LiftingOff;
    if (Bldg &&
        Bldg->FlightPhase == Components::BuildingFlightPhase::Landing &&
        !Bldg->FlightHeightTransitionActive)
        return AnimState::Landing;
    if (Burrow)
    {
        if (Burrow->Phase == Components::BurrowPhase::Burrowing)
            return AnimState::Burrowing;
        if (Burrow->Phase == Components::BurrowPhase::Unburrowing)
            return AnimState::Unburrowing;
        if (Burrow->IsBurrowed || Burrow->Phase == Components::BurrowPhase::Burrowed)
            return AnimState::Burrowed;
    }
    if (Status && (Status->HasEffect(Components::StatusEffect::Stasis) ||
                   Status->HasEffect(Components::StatusEffect::Lockdown) ||
                   Status->HasEffect(Components::StatusEffect::Maelstrom)))
    {
        return AnimState::Idle;
    }
    if (Bldg && Bldg->IsLifted) return AnimState::Flying;
    if (Path)
    {
        const bool IsExplicitlyStopped = Cmd &&
            (Cmd->CurrentCommand == Components::CommandType::Stop ||
             Cmd->CurrentCommand == Components::CommandType::HoldPosition);
        if (!IsExplicitlyStopped)
        {
            const bool IsActuallyMoving = Path->CurrentSpeed > 0.05f || Path->CurrentVelocity.Length() > 0.05f;
            const bool HasUnfinishedPath = !Path->Path.empty() && Path->PathIndex < (int)Path->Path.size();
            const bool IsStalledOrFailed = Path->HasRecentMoveFailure || Path->DestinationStallTicks > 0;
            if (IsActuallyMoving || (HasUnfinishedPath && !IsStalledOrFailed))
                return AnimState::Walking;
        }
    }
    // CombatSystem clears TargetEntityID as soon as the target becomes
    // invalid.  While a valid target or an authored pending shot remains, an
    // otherwise stationary unit must stay in its attack pose across the whole
    // cooldown.  Movement still wins above so AttackMove/chasing units walk.
    if (Combat &&
        (Combat->PendingAttackFrame || Combat->TargetEntityID >= 0))
        return AnimState::Attacking;
    if (Cmd && Cmd->CurrentCommand == Components::CommandType::Attack)
        return AnimState::Attacking;
    return AnimState::Idle;
}

} // namespace Game::Systems
