#include "Game/Systems/DeclarativeAnimationPlayer.h"
#include "Game/Components.h"

namespace Game::Systems {

bool DeclarativeAnimationPlayerSystem::TransitionToState(
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

void DeclarativeAnimationPlayerSystem::StepEntityTicks(Core::ECS::EntityID Entity, uint32_t TicksToAdvance)
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

void DeclarativeAnimationPlayerSystem::Update(float DeltaTime)
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

void DeclarativeAnimationPlayerSystem::EvaluateTickKeyframes(
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
