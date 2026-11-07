#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Engine
{

// ============================================================================
// TimeStepAccumulator: Deterministic Fixed-Step & Frame Interpolation Clock
// Referenced from Godot 4 Engine (physics_ticks_per_second / physics_interpolation_fraction)
// and Unity Time (fixedDeltaTime / maximumDeltaTime / timeScale):
// - Decouples deterministic fixed-rate simulation ticks (e.g. 24Hz RTS lockstep
//   or 60Hz physics) from variable-rate host rendering frames.
// - Prevents spiral-of-death frame stalls via MaxSubStepsPerFrame clamping.
// - Computes normalized InterpolationAlpha in [0.0, 1.0] for smooth high-Hz rendering.
// ============================================================================
class TimeStepAccumulator
{
public:
    explicit constexpr TimeStepAccumulator(float FixedStepSeconds = 1.0f / 24.0f,
                                           int MaxSubStepsPerFrame = 4)
        : mFixedStepSeconds(FixedStepSeconds > 1e-5f ? FixedStepSeconds : (1.0f / 24.0f))
        , mMaxSubStepsPerFrame(MaxSubStepsPerFrame > 1 ? MaxSubStepsPerFrame : 1)
    {
    }

    void Reset() noexcept
    {
        mAccumulatorSeconds = 0.0f;
        mInterpolationAlpha = 0.0f;
        mTotalSimulationSeconds = 0.0;
        mTotalWallSeconds = 0.0;
        mTickCount = 0;
        mFrameCount = 0;
        mLastFrameSubSteps = 0;
    }

    // Prepares the accumulator for a new host frame and returns the number of
    // fixed sub-steps scheduled to run on this frame without pre-incrementing mTickCount.
    int BeginFrame(float WallDeltaSeconds) noexcept
    {
        ++mFrameCount;
        const float ClampedWall = std::max(0.0f, WallDeltaSeconds);
        mTotalWallSeconds += static_cast<double>(ClampedWall);

        if (mPaused || mTimeScale <= 0.0f)
        {
            mLastFrameSubSteps = 0;
            mInterpolationAlpha = std::clamp(mAccumulatorSeconds / mFixedStepSeconds, 0.0f, 1.0f);
            return 0;
        }

        const float ScaledDelta = ClampedWall * mTimeScale;
        const float MaxAllowedAccum = mFixedStepSeconds * static_cast<float>(mMaxSubStepsPerFrame);
        mAccumulatorSeconds = std::min(mAccumulatorSeconds + ScaledDelta, MaxAllowedAccum);

        int Steps = 0;
        float TempAccum = mAccumulatorSeconds;
        constexpr float kEpsilon = 1e-6f;
        while (TempAccum + kEpsilon >= mFixedStepSeconds && Steps < mMaxSubStepsPerFrame)
        {
            TempAccum = std::max(0.0f, TempAccum - mFixedStepSeconds);
            ++Steps;
        }

        mLastFrameSubSteps = Steps;
        mInterpolationAlpha = std::clamp(TempAccum / mFixedStepSeconds, 0.0f, 1.0f);
        return Steps;
    }

    // Consumes a single scheduled fixed sub-step, advancing mTickCount by 1.
    uint64_t ConsumeSubStep() noexcept
    {
        mAccumulatorSeconds = std::max(0.0f, mAccumulatorSeconds - mFixedStepSeconds);
        mTotalSimulationSeconds += static_cast<double>(mFixedStepSeconds);
        ++mTickCount;
        mInterpolationAlpha = std::clamp(mAccumulatorSeconds / mFixedStepSeconds, 0.0f, 1.0f);
        return mTickCount;
    }

    // Advances the clock by WallDeltaSeconds and returns the number of fixed ticks
    // that should execute on this frame.
    int AdvanceFrame(float WallDeltaSeconds) noexcept
    {
        const int Steps = BeginFrame(WallDeltaSeconds);
        for (int i = 0; i < Steps; ++i)
        {
            ConsumeSubStep();
        }
        return Steps;
    }

    // Directly advances ExactTicks deterministic ticks (useful for headless tests / lockstep replay)
    void StepExactTicks(uint32_t ExactTicks) noexcept
    {
        for (uint32_t i = 0; i < ExactTicks; ++i)
        {
            mTotalSimulationSeconds += static_cast<double>(mFixedStepSeconds);
            ++mTickCount;
        }
        mLastFrameSubSteps = static_cast<int>(ExactTicks);
    }

    void SetFixedStepSeconds(float StepSeconds) noexcept
    {
        if (StepSeconds > 1e-5f)
            mFixedStepSeconds = StepSeconds;
    }

    float GetFixedStepSeconds() const noexcept { return mFixedStepSeconds; }
    float GetFixedTickRateHz() const noexcept { return 1.0f / mFixedStepSeconds; }

    void SetMaxSubStepsPerFrame(int MaxSubSteps) noexcept
    {
        mMaxSubStepsPerFrame = std::max(1, MaxSubSteps);
    }

    int GetMaxSubStepsPerFrame() const noexcept { return mMaxSubStepsPerFrame; }

    void SetTimeScale(float Scale) noexcept { mTimeScale = std::max(0.0f, Scale); }
    float GetTimeScale() const noexcept { return mTimeScale; }

    void SetPaused(bool Paused) noexcept { mPaused = Paused; }
    bool IsPaused() const noexcept { return mPaused; }

    float GetInterpolationAlpha() const noexcept { return mInterpolationAlpha; }
    float GetAccumulatorSeconds() const noexcept { return mAccumulatorSeconds; }
    uint64_t GetTickCount() const noexcept { return mTickCount; }
    uint64_t GetFrameCount() const noexcept { return mFrameCount; }
    int GetLastFrameSubSteps() const noexcept { return mLastFrameSubSteps; }
    double GetTotalSimulationSeconds() const noexcept { return mTotalSimulationSeconds; }
    double GetTotalWallSeconds() const noexcept { return mTotalWallSeconds; }

private:
    float mFixedStepSeconds = 1.0f / 24.0f;
    int mMaxSubStepsPerFrame = 4;
    float mTimeScale = 1.0f;
    bool mPaused = false;

    float mAccumulatorSeconds = 0.0f;
    float mInterpolationAlpha = 0.0f;
    int mLastFrameSubSteps = 0;
    uint64_t mTickCount = 0;
    uint64_t mFrameCount = 0;
    double mTotalSimulationSeconds = 0.0;
    double mTotalWallSeconds = 0.0;
};

} // namespace Engine
