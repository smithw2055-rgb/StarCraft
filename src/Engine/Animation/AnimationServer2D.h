#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Math/Vec2.h"
#include "Engine/DataAssetRegistry.h"
#include "Resource/ModernAssetRegistry.h"

namespace Engine::Animation
{

enum class NotifyKind2D : uint8_t
{
    AudioTrigger = 0,
    CombatTrigger = 1,
    SocketVfxSpawn = 2,
    RootMotion = 3,
    FsmSignal = 4
};

struct AnimationNotifyEvent2D
{
    NotifyKind2D Kind = NotifyKind2D::AudioTrigger;
    uint32_t Tick = 0;
    uint32_t SoundID = 0;
    int32_t Param0 = 0;
    int16_t OffsetX = 0;
    int16_t OffsetY = 0;
    std::string ActionOrSignal;
    std::string TargetAsset;
};

struct Animator2DState
{
    uint32_t ControllerID = 0;
    std::string CurrentClipName = "Init";
    uint32_t CurrentTick = 0;
    int32_t CurrentFrameSet = 0;
    int32_t VerticalOffsetPx = 0;
    bool NonInterruptibleSection = false;
    bool Playing = true;
    bool Finished = false;
    uint32_t CompletedLoops = 0;
};

struct AnimationServerTelemetry
{
    uint64_t ControllersRegistered = 0;
    uint64_t ClipsRegistered = 0;
    uint64_t SocketTracksRegistered = 0;
    uint64_t AnimatorStepsEvaluated = 0;
    uint64_t NotifyEventsEmitted = 0;
    uint64_t StateTransitionsExecuted = 0;
};

// ============================================================================
// AnimationServer2D (Phase 46 & Phase 61):
// Referenced from Godot 4 AnimationPlayer / AnimationTree & Unity DOTS Timeline:
// - Manages declarative 6-track 2D animation state machines (SpriteFrame,
//   AudioNotify, CombatNotify, SocketVfxSpawn, RootMotion, FsmSignal) and
//   17-way mirrored 2D Socket mount tracks.
// - Directly imports all 398 IScript controllers (1,364 declarative clips) and
//   2D Socket tracks from cloud assets-R ModernAssetRegistry.
// - Provides high-level declarative state evaluation (`EvaluateUnitLocomotionAndCombatState`)
//   for locomotion/combat/harvest/death transitions without legacy bytecode polling.
// ============================================================================
class AnimationServer2D
{
public:
    void Clear()
    {
        mControllers.Clear();
        mSocketTracks.Clear();
        mTelemetry = {};
    }

    Engine::AssetHandle<Resource::ModernIScriptControllerAsset> RegisterController(
        uint32_t ControllerID,
        std::string Name,
        Resource::ModernIScriptControllerAsset Asset)
    {
        ++mTelemetry.ControllersRegistered;
        mTelemetry.ClipsRegistered += Asset.Clips.size();
        return mControllers.Register(ControllerID, std::move(Name), std::move(Asset));
    }

    Engine::AssetHandle<Resource::ModernSocket2DTrack> RegisterSocketTrack(
        uint32_t TrackID,
        std::string Name,
        Resource::ModernSocket2DTrack Track)
    {
        Track.RebuildLookupIndex();
        ++mTelemetry.SocketTracksRegistered;
        return mSocketTracks.Register(TrackID, std::move(Name), std::move(Track));
    }

    size_t ImportFromModernRegistry(const Resource::ModernAssetRegistry& Registry)
    {
        size_t ImportedControllers = 0;
        for (const auto& Ctrl : Registry.GetIScriptControllers())
        {
            const std::string Name = Ctrl.ControllerName.empty()
                                         ? ("IScript_" + std::to_string(Ctrl.IScriptID))
                                         : Ctrl.ControllerName;
            RegisterController(Ctrl.IScriptID, Name, Ctrl);
            ++ImportedControllers;
        }

        uint32_t NextTrackID = 1;
        for (const auto& Track : Registry.GetSocketTracks())
        {
            RegisterSocketTrack(NextTrackID++, Track.SocketAssetName, Track);
        }
        return ImportedControllers;
    }

    const Resource::ModernIScriptControllerAsset* FindControllerByID(uint32_t ControllerID) const noexcept
    {
        return mControllers.FindById(ControllerID);
    }

    const Resource::ModernIScriptControllerAsset* FindControllerByName(std::string_view Name) const noexcept
    {
        return mControllers.FindByName(Name);
    }

    const Resource::ModernAnimationClip* FindClip(uint32_t ControllerID, const std::string& ClipName) const
    {
        const auto* Ctrl = FindControllerByID(ControllerID);
        if (!Ctrl)
            return nullptr;
        auto It = Ctrl->Clips.find(ClipName);
        return It != Ctrl->Clips.end() ? &It->second : nullptr;
    }

    // Attempts to transition Animator2DState to a new clip, respecting nobrkcodestart..nobrkcodeend
    // unless ForceInterrupt is true or transitioning to "Death".
    bool RequestStateTransition(Animator2DState& State,
                                std::string_view TargetClipName,
                                bool ForceInterrupt = false) const
    {
        if (State.NonInterruptibleSection && !ForceInterrupt && TargetClipName != "Death")
            return false;

        const std::string ClipStr(TargetClipName);
        const auto* Clip = FindClip(State.ControllerID, ClipStr);
        if (!Clip)
            return false;

        if (State.CurrentClipName != ClipStr || State.Finished)
        {
            State.CurrentClipName = ClipStr;
            State.CurrentTick = 0;
            State.NonInterruptibleSection = false;
            State.Playing = true;
            State.Finished = false;
            ++mTelemetry.StateTransitionsExecuted;

            for (const auto& FrameKey : Clip->SpriteFrameTrack)
            {
                if (FrameKey.Tick == 0)
                    State.CurrentFrameSet = FrameKey.FrameSetIndex;
            }
        }
        return true;
    }

    // Phase 61 Declarative State Machine Evaluator:
    // Automatically resolves desired clip ("Death", "AirAttkInit", "GndAttkInit",
    // "IsWorking", "Walking", or "Init") based on entity locomotion/combat flags,
    // transitions if allowed by nobrkcodestart..nobrkcodeend, and steps all 6 tracks.
    bool EvaluateUnitLocomotionAndCombatState(
        Animator2DState& State,
        float SpeedPxPerSec,
        bool IsAttacking,
        bool IsAirTarget,
        bool IsWorking,
        bool IsDead,
        uint32_t DeltaTicks,
        std::vector<AnimationNotifyEvent2D>& OutEvents) const
    {
        std::string DesiredClip = "Init";
        if (IsDead)
        {
            DesiredClip = "Death";
        }
        else if (IsAttacking)
        {
            if (IsAirTarget && FindClip(State.ControllerID, "AirAttkInit") != nullptr)
                DesiredClip = "AirAttkInit";
            else if (FindClip(State.ControllerID, "GndAttkInit") != nullptr)
                DesiredClip = "GndAttkInit";
            else if (FindClip(State.ControllerID, "AirAttkInit") != nullptr)
                DesiredClip = "AirAttkInit";
        }
        else if (IsWorking && FindClip(State.ControllerID, "IsWorking") != nullptr)
        {
            DesiredClip = "IsWorking";
        }
        else if (SpeedPxPerSec > 0.5f && FindClip(State.ControllerID, "Walking") != nullptr)
        {
            DesiredClip = "Walking";
        }

        if (State.CurrentClipName != DesiredClip)
        {
            RequestStateTransition(State, DesiredClip, /*ForceInterrupt=*/IsDead);
        }
        return StepAnimator(State, DeltaTicks, OutEvents);
    }

    // Steps an Animator2DState by DeltaTicks along all 6 declarative tracks
    bool StepAnimator(Animator2DState& State,
                      uint32_t DeltaTicks,
                      std::vector<AnimationNotifyEvent2D>& OutEvents) const
    {
        if (!State.Playing || State.Finished)
            return false;

        const auto* Clip = FindClip(State.ControllerID, State.CurrentClipName);
        if (!Clip)
            return false;

        ++mTelemetry.AnimatorStepsEvaluated;
        const uint32_t Duration = std::max<uint32_t>(1u, Clip->DurationTicks24Fps);
        const uint32_t StartTick = State.CurrentTick;
        const uint32_t EndTick = StartTick + DeltaTicks;

        auto EvaluateTickRange = [&](uint32_t FromInclusive, uint32_t ToInclusive) {
            for (const auto& FK : Clip->SpriteFrameTrack)
            {
                if (FK.Tick >= FromInclusive && FK.Tick <= ToInclusive)
                    State.CurrentFrameSet = FK.FrameSetIndex;
            }
            for (const auto& AK : Clip->AudioNotifyTrack)
            {
                if (AK.Tick >= FromInclusive && AK.Tick <= ToInclusive)
                {
                    AnimationNotifyEvent2D Ev{};
                    Ev.Kind = NotifyKind2D::AudioTrigger;
                    Ev.Tick = AK.Tick;
                    Ev.SoundID = AK.SoundID;
                    Ev.ActionOrSignal = AK.Type;
                    Ev.TargetAsset = AK.AudioClip;
                    OutEvents.push_back(std::move(Ev));
                    ++mTelemetry.NotifyEventsEmitted;
                }
            }
            for (const auto& CK : Clip->CombatNotifyTrack)
            {
                if (CK.Tick >= FromInclusive && CK.Tick <= ToInclusive)
                {
                    AnimationNotifyEvent2D Ev{};
                    Ev.Kind = NotifyKind2D::CombatTrigger;
                    Ev.Tick = CK.Tick;
                    Ev.Param0 = CK.Param0;
                    Ev.ActionOrSignal = CK.Notify;
                    OutEvents.push_back(std::move(Ev));
                    ++mTelemetry.NotifyEventsEmitted;
                }
            }
            for (const auto& VK : Clip->SocketVfxSpawnTrack)
            {
                if (VK.Tick >= FromInclusive && VK.Tick <= ToInclusive)
                {
                    AnimationNotifyEvent2D Ev{};
                    Ev.Kind = NotifyKind2D::SocketVfxSpawn;
                    Ev.Tick = VK.Tick;
                    Ev.OffsetX = VK.OffsetOrLoSlot[0];
                    Ev.OffsetY = VK.OffsetOrLoSlot[1];
                    Ev.ActionOrSignal = VK.SpawnMode;
                    Ev.TargetAsset = VK.TargetAsset;
                    OutEvents.push_back(std::move(Ev));
                    ++mTelemetry.NotifyEventsEmitted;
                }
            }
            for (const auto& RK : Clip->RootMotionTrack)
            {
                if (RK.Tick >= FromInclusive && RK.Tick <= ToInclusive)
                {
                    if (RK.Action == "setvertpos")
                        State.VerticalOffsetPx = RK.Param0;
                    AnimationNotifyEvent2D Ev{};
                    Ev.Kind = NotifyKind2D::RootMotion;
                    Ev.Tick = RK.Tick;
                    Ev.Param0 = RK.Param0;
                    Ev.ActionOrSignal = RK.Action;
                    OutEvents.push_back(std::move(Ev));
                    ++mTelemetry.NotifyEventsEmitted;
                }
            }
            for (const auto& SK : Clip->FsmSignalTrack)
            {
                if (SK.Tick >= FromInclusive && SK.Tick <= ToInclusive)
                {
                    if (SK.SignalOrBranch == "nobrkcodestart")
                        State.NonInterruptibleSection = true;
                    else if (SK.SignalOrBranch == "nobrkcodeend")
                        State.NonInterruptibleSection = false;

                    AnimationNotifyEvent2D Ev{};
                    Ev.Kind = NotifyKind2D::FsmSignal;
                    Ev.Tick = SK.Tick;
                    Ev.Param0 = SK.Param0;
                    Ev.ActionOrSignal = SK.SignalOrBranch;
                    OutEvents.push_back(std::move(Ev));
                    ++mTelemetry.NotifyEventsEmitted;
                }
            }
        };

        const uint32_t FirstWindowStart = (StartTick == 0 && DeltaTicks > 0) ? 0u : (StartTick + 1u);
        if (EndTick < Duration)
        {
            if (FirstWindowStart <= EndTick)
                EvaluateTickRange(FirstWindowStart, EndTick);
            State.CurrentTick = EndTick;
        }
        else
        {
            if (FirstWindowStart < Duration)
                EvaluateTickRange(FirstWindowStart, Duration - 1u);

            if (Clip->IsLooping)
            {
                const uint32_t Loops = EndTick / Duration;
                State.CompletedLoops += Loops;
                for (uint32_t L = 1; L < Loops; ++L)
                {
                    EvaluateTickRange(0u, Duration - 1u);
                }
                State.CurrentTick = EndTick % Duration;
                EvaluateTickRange(0u, State.CurrentTick);
            }
            else
            {
                State.CurrentTick = Duration;
                State.Finished = true;
                State.Playing = false;
                State.NonInterruptibleSection = false;
            }
        }
        return true;
    }

    // Samples 2D Socket HD 4x pixel offset with automatic 32-direction symmetry & horizontal flip
    bool SampleSocketOffsetHD(std::string_view SocketTrackName,
                              int32_t FrameSet,
                              uint8_t Direction32,
                              Core::Math::Vec2& OutOffsetHD) const
    {
        const auto* Track = mSocketTracks.FindByName(SocketTrackName);
        if (!Track)
            return false;

        const uint8_t ClampedDir = static_cast<uint8_t>(Direction32 & 31u);
        const bool Flipped = Track->Directional17Way && (ClampedDir > 16u);
        const int32_t Dir17 = Flipped ? static_cast<int32_t>(32u - ClampedDir)
                                      : static_cast<int32_t>(ClampedDir);

        int16_t SDX = 0, SDY = 0, HDX = 0, HDY = 0;
        if (!Track->SampleOffset(FrameSet, Dir17, SDX, SDY, HDX, HDY))
            return false;

        OutOffsetHD.X = Flipped ? -static_cast<float>(HDX) : static_cast<float>(HDX);
        OutOffsetHD.Y = static_cast<float>(HDY);
        return true;
    }

    const Engine::DataAssetTable<Resource::ModernIScriptControllerAsset>& GetControllerTable() const noexcept
    {
        return mControllers;
    }

    const Engine::DataAssetTable<Resource::ModernSocket2DTrack>& GetSocketTrackTable() const noexcept
    {
        return mSocketTracks;
    }

    const AnimationServerTelemetry& GetTelemetry() const noexcept { return mTelemetry; }

private:
    Engine::DataAssetTable<Resource::ModernIScriptControllerAsset> mControllers;
    Engine::DataAssetTable<Resource::ModernSocket2DTrack> mSocketTracks;
    mutable AnimationServerTelemetry mTelemetry{};
};

} // namespace Engine::Animation
