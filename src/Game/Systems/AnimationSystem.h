#pragma once

#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/IScriptVM.h"
#include <functional>
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
    Burrowed,
    Flying,
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
    AnimationSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    AnimationSystem(Core::ECS::SystemManager& SysMgr, Core::Audio::AudioSystem* Audio)
        : mSystemMgr(SysMgr), mAudio(Audio) {}
    AnimationSystem(Core::ECS::SystemManager& SysMgr, Core::Audio::AudioSystem* Audio, BulletSystem* BulletSys)
        : mSystemMgr(SysMgr), mAudio(Audio), mBulletSystem(BulletSys) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "AnimationSystem"; }

    static void RegisterUnitAnim(int ChkUnitID, const std::vector<AnimClip>& Clips);
    static bool HasIScriptAnimationForImage(int ImageID, int AnimID);
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
                                                  const Components::StatusEffectComponent* Status);
private:
    struct EntityScriptState
    {
        Game::IScriptVM VM;
        int ImageID = -1;
        int ScriptID = -1;
        int AnimID = -1;
        bool WasWorking = false;
        bool WasUnderConstruction = false;
        bool WasWalking = false;
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
    void UpdateImageTrees(float DeltaTime);
    void ClearImageTree(int EntityID);
    void MarkImageSubtreeHidden(Components::ImageTreeComponent& Tree, int ParentIndex);
    void AddImageTreeNode(int EntityID,
                          int ImageID,
                          int OffsetX,
                          int OffsetY,
                          bool DrawAbove,
                          bool Flipped,
                          float Lifetime,
                          bool UseSpriteID = false);
    void AddImageTreeNode(int EntityID,
                          int ParentIndex,
                          int ImageID,
                          Core::Math::Vec2 Offset,
                          bool DrawAbove,
                          bool Flipped,
                          float Lifetime,
                          bool UseSpriteID = false);
    void StartImageNodeScript(Components::ImageSubnode& Node);
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
    void ConsumeIScriptEvents(int EntityID,
                              EntityScriptState& State,
                              const Components::TransformComponent& Trans,
                              const Components::PlayerComponent* Player,
                              Components::CombatComponent* Combat);
    bool UpdateIScriptAnimation(int EntityID,
                                float DeltaTime,
                                AnimState DesiredState,
                                Components::RenderComponent& Render,
                                const Components::TransformComponent& Trans,
                                const Components::PathfindingComponent* Path,
                                const Components::PlayerComponent* Player,
                                Components::CombatComponent* Combat,
                                const Components::BuildingComponent* Bldg,
                                const Components::StatusEffectComponent* Status,
                                const Components::ProductionComponent* Prod);
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

    if (!IsBuilding)
    {
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
        if (!WasUnderConstruction && HasAnim(Game::IScriptAnims::Anim_SpecialState1))
            return Game::IScriptAnims::Anim_SpecialState1;
        if (ConstructionProgress >= 0.85f)
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

    if (WasUnderConstruction)
    {
        const int BuiltAnim =
            ResolvePlayableIScriptAnimation(Game::IScriptAnims::Anim_Built, HasAnim);
        if (BuiltAnim >= 0)
            return BuiltAnim;
    }
    if (IsWorking && HasAnim(Game::IScriptAnims::Anim_IsWorking))
        return Game::IScriptAnims::Anim_IsWorking;
    if (WasWorking && HasAnim(Game::IScriptAnims::Anim_WorkingToIdle))
        return Game::IScriptAnims::Anim_WorkingToIdle;
    if (HasAnim(Game::IScriptAnims::Anim_Init))
        return Game::IScriptAnims::Anim_Init;
    return -1;
}

inline bool AnimationSystem::ShouldClearImageTreeForAnimRestart(
    bool IsBuilding,
    bool ImageOrScriptChanged,
    int PreviousAnim,
    int TargetAnim,
    bool UnderConstruction)
{
    if (ImageOrScriptChanged)
        return true;

    if (TargetAnim == Game::IScriptAnims::Anim_GndAttkInit ||
        TargetAnim == Game::IScriptAnims::Anim_AirAttkInit)
        return true;

    if (UnderConstruction)
        return PreviousAnim != TargetAnim;

    return !IsBuilding && PreviousAnim != TargetAnim;
}

inline AnimState AnimationSystem::GetAnimStateFromComponents(
    const Components::CommandComponent* Cmd,
    const Components::CombatComponent* Combat,
    const Components::PathfindingComponent* Path,
    const Components::UnitComponent* Unit,
    const Components::BuildingComponent* Bldg,
    const Components::MorphComponent* Morph,
    const Components::StatusEffectComponent* Status)
{
    if (Unit && Unit->HitPoints <= 0) return AnimState::Dying;
    if (Morph && Morph->IsMorphing) return AnimState::Morphing;
    if (Bldg && Bldg->UnderConstruction) return AnimState::Constructing;
    if (Status && (Status->HasEffect(Components::StatusEffect::Stasis) ||
                   Status->HasEffect(Components::StatusEffect::Lockdown) ||
                   Status->HasEffect(Components::StatusEffect::Maelstrom)))
    {
        return AnimState::Idle;
    }
    if (Path)
    {
        if (!Path->Path.empty() && Path->PathIndex < (int)Path->Path.size())
            return AnimState::Walking;
        if (Path->CurrentSpeed > 0.1f || Path->CurrentVelocity.Length() > 0.1f)
            return AnimState::Walking;
    }
    if (Combat && Combat->CooldownRemaining > Combat->AttackCooldown * 0.5f)
        return AnimState::Attacking;
    if (Cmd && Cmd->CurrentCommand == Components::CommandType::Attack)
        return AnimState::Attacking;
    return AnimState::Idle;
}

} // namespace Game::Systems
