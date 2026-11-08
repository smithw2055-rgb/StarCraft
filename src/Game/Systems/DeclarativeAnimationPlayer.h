#pragma once

// === Consolidated from src/Game/Events/GameEvents.h ===
#include <cstdint>
#include <string>

#include "Core/Audio/ModernAudioEventRouter.h"
#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"

namespace Game::Events {

// Track 2: audio_notify_track signal (aliased from Core::Audio::AnimAudioNotifyEvent)
using AnimAudioNotifyEvent = Core::Audio::AnimAudioNotifyEvent;

// Track 3: combat_notify_track signal
struct AnimCombatNotifyEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    std::string NotifyType; // "attackwith", "attackmelee", "domissiledmg", "useweapon", "gotorepeatattk", "castspell"
    int32_t WeaponSlotOrParam = 0;
    Core::Math::Vec2 WorldPos{0.0f, 0.0f};
};

// Track 4: socket_vfx_spawn_track signal
struct AnimSocketVfxSpawnEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    std::string SpawnMode;  // "imgol", "imgul", "sprol", "lowsprul", "imgoluselo"
    std::string TargetAsset;
    int16_t OffsetX = 0;
    int16_t OffsetY = 0;
};

// Track 5: root_motion_and_transform_track signal
struct AnimRootMotionEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    std::string Action;     // "move", "setvertpos", "sethorpos", "setfldirect", "setflspeed", "turncwise", "turnccwise"
    int32_t Param = 0;
};

// Track 6: fsm_signals_and_branches signal
struct AnimFsmSignalEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    std::string Signal;     // "nobrkcodestart", "nobrkcodeend", "sigorder", "orderdone", "randcondjmp"
    int32_t Param = 0;
};

} // namespace Game::Events

// === End src/Game/Events/GameEvents.h ===

#include <cstdint>
#include <string>
#include <unordered_map>

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Engine/Animation/AnimationServer2D.h"
#include "Resource/ModernAssetRegistry.h"

namespace Game::Systems {

struct DeclarativeAnimatorComponent {
    uint32_t IScriptID = 0;
    std::string CurrentClipName = "Init";
    uint32_t CurrentTick = 0;
    float TickAccumulator = 0.0f;
    int32_t CurrentFrameSet = 0;
    bool NoBreakSectionActive = false;
    bool Playing = true;
    bool DestroyOnClipEnd = false;
    uint32_t LoopsCompleted = 0;
};

// Godot AnimationTree / Unity Animator style 6-track declarative timeline player
// Phase 51: Connected directly to Engine::Animation::AnimationServer2D & ModernAssetRegistry
class DeclarativeAnimationPlayerSystem final : public Core::ECS::ISystem {
public:
    DeclarativeAnimationPlayerSystem(
        Core::ECS::SystemManager& SysMgr,
        const Resource::ModernAssetRegistry* Registry = nullptr,
        Engine::Animation::AnimationServer2D* AnimServer = nullptr)
        : mSystemMgr(SysMgr), mRegistry(Registry), mAnimServer(AnimServer) {}

    std::string_view GetName() const override { return "DeclarativeAnimationPlayerSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::Presentation; }

    void SetAssetRegistry(const Resource::ModernAssetRegistry* Registry) { mRegistry = Registry; }
    void SetAnimationServer(Engine::Animation::AnimationServer2D* Server) { mAnimServer = Server; }
    Engine::Animation::AnimationServer2D* GetAnimationServer() const noexcept { return mAnimServer; }

    bool TransitionToState(Core::ECS::EntityID Entity, const std::string& NewClipName, bool ForceInterrupt = false);
    void StepEntityTicks(Core::ECS::EntityID Entity, uint32_t TicksToAdvance);
    void Update(float DeltaTime) override;

private:
    const Resource::ModernAnimationClip* ResolveClip(uint32_t IScriptID, const std::string& ClipName) const
    {
        if (mAnimServer)
        {
            if (const auto* Clip = mAnimServer->FindClip(IScriptID, ClipName))
                return Clip;
        }
        if (mRegistry)
        {
            if (const auto* Ctrl = mRegistry->FindIScriptControllerByID(IScriptID))
            {
                auto It = Ctrl->Clips.find(ClipName);
                if (It != Ctrl->Clips.end())
                    return &It->second;
            }
        }
        return nullptr;
    }

    void EvaluateTickKeyframes(
        Core::ECS::EntityID Entity,
        DeclarativeAnimatorComponent& Anim,
        const Resource::ModernAnimationClip& Clip,
        uint32_t Tick);

    Core::ECS::SystemManager& mSystemMgr;
    const Resource::ModernAssetRegistry* mRegistry = nullptr;
    Engine::Animation::AnimationServer2D* mAnimServer = nullptr;
};

} // namespace Game::Systems

// === Inlined from DeclarativeAnimationPlayer.cpp ===

#include "Game/Components.h"

namespace Game::Systems {

inline bool DeclarativeAnimationPlayerSystem::TransitionToState(
    Core::ECS::EntityID Entity,
    const std::string& NewClipName,
    bool ForceInterrupt)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* Anim = EM.GetComponent<DeclarativeAnimatorComponent>(Entity);
    if (!Anim || (!mRegistry && !mAnimServer)) return false;

    if (Anim->NoBreakSectionActive && !ForceInterrupt && NewClipName != "Death") {
        return false;
    }

    const auto* ClipPtr = ResolveClip(Anim->IScriptID, NewClipName);
    if (!ClipPtr) return false;

    Anim->CurrentClipName = NewClipName;
    Anim->CurrentTick = 0;
    Anim->TickAccumulator = 0.0f;
    Anim->NoBreakSectionActive = false;
    Anim->Playing = true;
    Anim->DestroyOnClipEnd = ClipPtr->DestroysEntityOnEnd;
    Anim->LoopsCompleted = 0;

    EvaluateTickKeyframes(Entity, *Anim, *ClipPtr, 0);
    return true;
}

inline void DeclarativeAnimationPlayerSystem::StepEntityTicks(Core::ECS::EntityID Entity, uint32_t TicksToAdvance)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* Anim = EM.GetComponent<DeclarativeAnimatorComponent>(Entity);
    if (!Anim || !Anim->Playing || (!mRegistry && !mAnimServer)) return;

    const auto* ClipPtr = ResolveClip(Anim->IScriptID, Anim->CurrentClipName);
    if (!ClipPtr) return;
    const auto& Clip = *ClipPtr;

    if (Anim->CurrentTick == 0 && TicksToAdvance > 0 && Anim->TickAccumulator == 0.0f) {
        EvaluateTickKeyframes(Entity, *Anim, Clip, 0);
    }

    const uint32_t Duration = std::max<uint32_t>(1, Clip.DurationTicks24Fps);
    for (uint32_t Step = 0; Step < TicksToAdvance && Anim->Playing; ++Step) {
        ++Anim->CurrentTick;
        if (Anim->CurrentTick >= Duration) {
            if (Clip.IsLooping) {
                Anim->CurrentTick = 0;
                ++Anim->LoopsCompleted;
                EvaluateTickKeyframes(Entity, *Anim, Clip, 0);
            } else {
                Anim->Playing = false;
                if (Clip.DestroysEntityOnEnd) {
                    EM.DestroyEntity(Entity);
                }
                break;
            }
        } else {
            EvaluateTickKeyframes(Entity, *Anim, Clip, Anim->CurrentTick);
        }
    }
}

inline void DeclarativeAnimationPlayerSystem::Update(float DeltaTime)
{
    if (!mRegistry && !mAnimServer) return;
    constexpr float kSecPerTick = 1.0f / 24.0f;
    auto& EM = mSystemMgr.GetEntityManager();

    EM.ForEachEntityWith<DeclarativeAnimatorComponent>([&](Core::ECS::EntityID Entity) {
        auto* Anim = EM.GetComponent<DeclarativeAnimatorComponent>(Entity);
        if (!Anim || !Anim->Playing) return;

        Anim->TickAccumulator += DeltaTime;
        uint32_t Ticks = 0;
        while (Anim->TickAccumulator >= kSecPerTick) {
            Anim->TickAccumulator -= kSecPerTick;
            ++Ticks;
        }
        if (Ticks > 0) {
            StepEntityTicks(Entity, Ticks);
        }
    });
}

inline void DeclarativeAnimationPlayerSystem::EvaluateTickKeyframes(
    Core::ECS::EntityID Entity,
    DeclarativeAnimatorComponent& Anim,
    const Resource::ModernAnimationClip& Clip,
    uint32_t Tick)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto& Bus = EM.Events();
    Core::Math::Vec2 WorldPos{0.0f, 0.0f};
    if (const auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(Entity)) {
        WorldPos = Trans->Position;
    }

    // Track 1: sprite_frame_track
    for (const auto& Key : Clip.SpriteFrameTrack) {
        if (Key.Tick == Tick) {
            Anim.CurrentFrameSet = Key.FrameSetIndex;
            if (auto* RenderComp = EM.GetComponent<Game::Components::RenderComponent>(Entity)) {
                RenderComp->FrameIndex = Key.FrameSetIndex;
            }
        }
    }

    // Track 2: audio_notify_track -> EventBus
    for (const auto& Key : Clip.AudioNotifyTrack) {
        if (Key.Tick == Tick) {
            Bus.Publish(Game::Events::AnimAudioNotifyEvent{
                Entity,
                Anim.IScriptID,
                Clip.ClipName,
                Tick,
                Key.SoundID,
                Key.SoundIDMax,
                Key.AudioClip,
                WorldPos
            });
        }
    }

    // Track 3: combat_notify_track -> EventBus
    for (const auto& Key : Clip.CombatNotifyTrack) {
        if (Key.Tick == Tick) {
            Bus.Publish(Game::Events::AnimCombatNotifyEvent{
                Entity,
                Anim.IScriptID,
                Clip.ClipName,
                Tick,
                Key.Notify,
                Key.Param0,
                WorldPos
            });
        }
    }

    // Track 4: socket_vfx_spawn_track -> EventBus
    for (const auto& Key : Clip.SocketVfxSpawnTrack) {
        if (Key.Tick == Tick) {
            Bus.Publish(Game::Events::AnimSocketVfxSpawnEvent{
                Entity,
                Anim.IScriptID,
                Clip.ClipName,
                Tick,
                Key.SpawnMode,
                Key.TargetAsset,
                Key.OffsetOrLoSlot[0],
                Key.OffsetOrLoSlot[1]
            });
        }
    }

    // Track 5: root_motion_and_transform_track -> EventBus
    for (const auto& Key : Clip.RootMotionTrack) {
        if (Key.Tick == Tick) {
            Bus.Publish(Game::Events::AnimRootMotionEvent{
                Entity,
                Anim.IScriptID,
                Clip.ClipName,
                Tick,
                Key.Action,
                Key.Param0
            });
        }
    }

    // Track 6: fsm_signals_and_branches -> EventBus
    for (const auto& Key : Clip.FsmSignalTrack) {
        if (Key.Tick == Tick) {
            if (Key.SignalOrBranch == "nobrkcodestart") {
                Anim.NoBreakSectionActive = true;
            } else if (Key.SignalOrBranch == "nobrkcodeend") {
                Anim.NoBreakSectionActive = false;
            }
            Bus.Publish(Game::Events::AnimFsmSignalEvent{
                Entity,
                Anim.IScriptID,
                Clip.ClipName,
                Tick,
                Key.SignalOrBranch,
                Key.Param0
            });
        }
    }
}

} // namespace Game::Systems
