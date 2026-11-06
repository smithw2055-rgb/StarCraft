#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Core::Scene
{

enum class SceneId : uint8_t
{
    None = 0,
    Boot,
    MainMenu,
    Gameplay,
    Replay,
    Custom
};

// Modern engine Scene interface inspired by Godot SceneTree Node lifecycle
// and Unity SceneManager. Separates variable-rate host frame updates (OnUpdate),
// deterministic fixed-rate simulation ticks (OnFixedUpdate, 24Hz), and
// interpolated presentation rendering (OnRender).
class IScene
{
public:
    virtual ~IScene() = default;

    virtual SceneId GetId() const = 0;
    virtual std::string_view GetName() const = 0;

    virtual void OnEnter() {}
    virtual void OnExit() {}

    // Called once per host frame before fixed-step simulation ticks.
    // Returns true if the scene consumed the frame and fixed-step should be skipped.
    virtual bool OnPreUpdate(float DeltaSeconds)
    {
        (void)DeltaSeconds;
        return false;
    }

    // Called at deterministic fixed intervals (e.g. 1/24s in StarCraft).
    virtual void OnFixedUpdate(float FixedDeltaSeconds)
    {
        (void)FixedDeltaSeconds;
    }

    // Called once per host frame after fixed-step ticks complete.
    virtual void OnPostUpdate(float DeltaSeconds, int FixedTicksExecuted)
    {
        (void)DeltaSeconds;
        (void)FixedTicksExecuted;
    }

    // Called on render frames with InterpolationAlpha in [0.0, 1.0].
    virtual void OnRender(float InterpolationAlpha)
    {
        (void)InterpolationAlpha;
    }

    virtual void OnEvent(const void* Event)
    {
        (void)Event;
    }
};

class SceneManager
{
public:
    using TransitionCallback = std::function<void(SceneId From, SceneId To)>;

    void RegisterScene(std::unique_ptr<IScene> Scene)
    {
        if (!Scene)
            return;
        const SceneId Id = Scene->GetId();
        mScenes[static_cast<uint8_t>(Id)] = std::move(Scene);
    }

    bool HasScene(SceneId Id) const
    {
        return mScenes.find(static_cast<uint8_t>(Id)) != mScenes.end();
    }

    IScene* GetScene(SceneId Id)
    {
        auto It = mScenes.find(static_cast<uint8_t>(Id));
        return It != mScenes.end() ? It->second.get() : nullptr;
    }

    const IScene* GetScene(SceneId Id) const
    {
        auto It = mScenes.find(static_cast<uint8_t>(Id));
        return It != mScenes.end() ? It->second.get() : nullptr;
    }

    IScene* GetActiveScene()
    {
        return GetScene(mActiveSceneId);
    }

    const IScene* GetActiveScene() const
    {
        return GetScene(mActiveSceneId);
    }

    SceneId GetActiveSceneId() const
    {
        return mActiveSceneId;
    }

    // Immediate scene transition with OnExit -> OnEnter lifecycle hooks.
    bool SwitchToImmediate(SceneId TargetId)
    {
        if (!HasScene(TargetId))
            return false;

        if (mActiveSceneId == TargetId && mEnteredActiveScene)
            return true;

        const SceneId PreviousId = mActiveSceneId;
        if (mEnteredActiveScene)
        {
            if (IScene* Current = GetScene(mActiveSceneId))
                Current->OnExit();
        }

        mActiveSceneId = TargetId;
        mPendingSceneId = SceneId::None;
        mEnteredActiveScene = true;
        ResetFixedStepAccumulator();

        if (IScene* Next = GetScene(mActiveSceneId))
            Next->OnEnter();

        if (mOnTransition && PreviousId != TargetId)
            mOnTransition(PreviousId, TargetId);

        return true;
    }

    // Queues a scene transition to be applied at the start of the next frame step.
    void RequestSwitchTo(SceneId TargetId)
    {
        mPendingSceneId = TargetId;
    }

    void ApplyPendingTransition()
    {
        if (mPendingSceneId != SceneId::None)
        {
            const SceneId Target = mPendingSceneId;
            mPendingSceneId = SceneId::None;
            SwitchToImmediate(Target);
        }
    }

    // Overlay stack (for modal pause menus, victory/defeat dialogs, briefing overlays)
    void PushOverlay(std::unique_ptr<IScene> OverlayScene)
    {
        if (!OverlayScene)
            return;
        OverlayScene->OnEnter();
        mOverlayStack.push_back(std::move(OverlayScene));
    }

    bool PopOverlay()
    {
        if (mOverlayStack.empty())
            return false;
        mOverlayStack.back()->OnExit();
        mOverlayStack.pop_back();
        return true;
    }

    void ClearOverlays()
    {
        while (!mOverlayStack.empty())
            PopOverlay();
    }

    bool HasOverlay() const
    {
        return !mOverlayStack.empty();
    }

    size_t GetOverlayCount() const
    {
        return mOverlayStack.size();
    }

    void ConfigureFixedStep(double FixedTickSeconds = 1.0 / 24.0, int DefaultMaxSubSteps = 2)
    {
        if (FixedTickSeconds > 0.0)
            mFixedTickSeconds = FixedTickSeconds;
        mDefaultMaxSubSteps = std::max(1, DefaultMaxSubSteps);
    }

    void ResetFixedStepAccumulator()
    {
        mAccumulatorSeconds = 0.0;
        mFixedTickCount = 0;
    }

    // Advances the active scene by HostDt with deterministic fixed-step sub-stepping.
    // Returns the number of authoritative fixed ticks executed during this host frame.
    int StepFrame(float HostDt, float TimeScale = 1.0f, int MaxSubStepsOverride = -1)
    {
        ApplyPendingTransition();

        IScene* Active = GetActiveScene();
        if (!Active)
            return 0;

        if (!mOverlayStack.empty())
        {
            if (mOverlayStack.back()->OnPreUpdate(HostDt))
                return 0;
        }

        if (Active->OnPreUpdate(HostDt))
            return 0;

        const double ScaledDt = static_cast<double>(std::max(0.0f, HostDt) * std::max(0.0f, TimeScale));
        const int MaxSteps = MaxSubStepsOverride > 0 ? MaxSubStepsOverride : mDefaultMaxSubSteps;

        mAccumulatorSeconds += ScaledDt;
        int StepsThisFrame = 0;
        while (mAccumulatorSeconds + 1.0e-12 >= mFixedTickSeconds &&
               StepsThisFrame < MaxSteps)
        {
            Active->OnFixedUpdate(static_cast<float>(mFixedTickSeconds));
            mAccumulatorSeconds -= mFixedTickSeconds;
            ++mFixedTickCount;
            ++StepsThisFrame;
        }

        Active->OnPostUpdate(HostDt, StepsThisFrame);
        return StepsThisFrame;
    }

    void RenderActiveScene()
    {
        IScene* Active = GetActiveScene();
        if (!Active)
            return;

        const float Alpha = GetInterpolationAlpha();
        Active->OnRender(Alpha);

        for (auto& Overlay : mOverlayStack)
            Overlay->OnRender(Alpha);
    }

    void DispatchEvent(const void* Event)
    {
        if (!mOverlayStack.empty())
        {
            mOverlayStack.back()->OnEvent(Event);
            return;
        }
        if (IScene* Active = GetActiveScene())
            Active->OnEvent(Event);
    }

    float GetInterpolationAlpha() const
    {
        if (mFixedTickSeconds <= 0.0)
            return 1.0f;
        return std::clamp(
            static_cast<float>(mAccumulatorSeconds / mFixedTickSeconds),
            0.0f,
            1.0f);
    }

    double GetAccumulatorSeconds() const { return mAccumulatorSeconds; }
    void SetAccumulatorSeconds(double Value) { mAccumulatorSeconds = Value; }
    uint64_t GetFixedTickCount() const { return mFixedTickCount; }
    void SetFixedTickCount(uint64_t Value) { mFixedTickCount = Value; }
    double GetFixedTickSeconds() const { return mFixedTickSeconds; }

    void SetTransitionCallback(TransitionCallback Callback)
    {
        mOnTransition = std::move(Callback);
    }

private:
    std::unordered_map<uint8_t, std::unique_ptr<IScene>> mScenes;
    std::vector<std::unique_ptr<IScene>> mOverlayStack;
    SceneId mActiveSceneId = SceneId::None;
    SceneId mPendingSceneId = SceneId::None;
    bool mEnteredActiveScene = false;
    double mFixedTickSeconds = 1.0 / 24.0;
    double mAccumulatorSeconds = 0.0;
    uint64_t mFixedTickCount = 0;
    int mDefaultMaxSubSteps = 2;
    TransitionCallback mOnTransition;
};

} // namespace Core::Scene
