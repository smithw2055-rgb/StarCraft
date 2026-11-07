#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Core::Audio
{

enum class AudioBusId : uint8_t
{
    Master = 0,
    SFX    = 1,
    Voice  = 2,
    Music  = 3,
    UI     = 4,
    Count  = 5
};

struct AudioBusState
{
    float Volume = 1.0f;
    bool Muted = false;
    bool Solo = false;
};

struct SpatialSoundInstance
{
    uint32_t SoundID = 0;
    Core::Math::Vec2 Position{0.0f, 0.0f};
};

struct AudioMixerTelemetry
{
    uint64_t TotalRequestsEvaluated = 0;
    uint64_t CoalescedDuplicateCount = 0;
    uint64_t DuckedFramesCount = 0;
};

// ============================================================================
// AudioBusMixer: Hierarchical Audio Bus & Spatial Coalescing Coordinator
// Referenced from Godot 4 AudioServer Buses & Unity AudioMixer:
// - Manages Master, SFX, Voice, Music, and UI bus gain, mute, and solo states
// - Implements automatic Sidechain Voice Ducking: when a unit portrait or
//   briefing transmission plays on the Voice bus, SFX and Music buses smoothly
//   attenuate to mVoiceDuckingGain (e.g. 0.45x) and recover when finished.
// - Implements same-tick Spatial Sound Coalescing: deduplicates identical
//   SoundID requests emitted within mCoalesceRadiusPx on the same frame to
//   prevent multi-unit weapon fire clipping and channel starvation.
// ============================================================================
class AudioBusMixer
{
public:
    AudioBusMixer() = default;

    void SetBusVolume(AudioBusId Bus, float LinearVolume)
    {
        const size_t Idx = static_cast<size_t>(Bus);
        if (Idx < static_cast<size_t>(AudioBusId::Count))
            mBuses[Idx].Volume = std::clamp(LinearVolume, 0.0f, 1.0f);
    }

    float GetBusVolume(AudioBusId Bus) const
    {
        const size_t Idx = static_cast<size_t>(Bus);
        return Idx < static_cast<size_t>(AudioBusId::Count) ? mBuses[Idx].Volume : 0.0f;
    }

    void SetBusMuted(AudioBusId Bus, bool Muted)
    {
        const size_t Idx = static_cast<size_t>(Bus);
        if (Idx < static_cast<size_t>(AudioBusId::Count))
            mBuses[Idx].Muted = Muted;
    }

    bool IsBusMuted(AudioBusId Bus) const
    {
        const size_t Idx = static_cast<size_t>(Bus);
        return Idx < static_cast<size_t>(AudioBusId::Count) ? mBuses[Idx].Muted : true;
    }

    void SetBusSolo(AudioBusId Bus, bool Solo)
    {
        const size_t Idx = static_cast<size_t>(Bus);
        if (Idx < static_cast<size_t>(AudioBusId::Count))
            mBuses[Idx].Solo = Solo;
    }

    // Triggers sidechain voice ducking for DurationSeconds (e.g. unit portrait speech / campaign transmission)
    void TriggerVoiceTransmissionDucking(float DurationSeconds, float DuckGain = 0.45f)
    {
        mVoiceDuckRemainingSeconds = std::max(mVoiceDuckRemainingSeconds, DurationSeconds);
        mVoiceDuckTargetGain = std::clamp(DuckGain, 0.05f, 1.0f);
        mCurrentDuckGain = mVoiceDuckTargetGain;
    }

    bool IsVoiceDuckingActive() const
    {
        return mVoiceDuckRemainingSeconds > 0.0f || mCurrentDuckGain < 0.999f;
    }

    // Computes effective final linear gain [0.0 .. 1.0] for a target bus
    float ComputeEffectiveBusGain(AudioBusId Bus) const
    {
        if (mBuses[static_cast<size_t>(AudioBusId::Master)].Muted)
            return 0.0f;

        const size_t Idx = static_cast<size_t>(Bus);
        if (Idx >= static_cast<size_t>(AudioBusId::Count) || mBuses[Idx].Muted)
            return 0.0f;

        bool AnySolo = false;
        for (size_t i = 1; i < static_cast<size_t>(AudioBusId::Count); ++i)
        {
            if (mBuses[i].Solo)
            {
                AnySolo = true;
                break;
            }
        }
        if (AnySolo && Bus != AudioBusId::Master && !mBuses[Idx].Solo)
            return 0.0f;

        float Gain = mBuses[static_cast<size_t>(AudioBusId::Master)].Volume * mBuses[Idx].Volume;
        if (Bus == AudioBusId::SFX || Bus == AudioBusId::Music)
        {
            Gain *= mCurrentDuckGain;
        }
        return std::clamp(Gain, 0.0f, 1.0f);
    }

    // Evaluates whether a spatial sound request should play or be coalesced with an identical
    // sound already emitted within mCoalesceRadiusPx during the current frame.
    bool TryAcquireSpatialVoiceSlot(uint32_t SoundID, const Core::Math::Vec2& WorldPos)
    {
        ++mTelemetry.TotalRequestsEvaluated;
        if (SoundID == 0)
            return false;

        const float RadiusSq = mCoalesceRadiusPx * mCoalesceRadiusPx;
        for (const auto& Existing : mCurrentFrameSpatialSounds)
        {
            if (Existing.SoundID == SoundID)
            {
                const float Dx = Existing.Position.X - WorldPos.X;
                const float Dy = Existing.Position.Y - WorldPos.Y;
                if (Dx * Dx + Dy * Dy <= RadiusSq)
                {
                    ++mTelemetry.CoalescedDuplicateCount;
                    return false;
                }
            }
        }

        mCurrentFrameSpatialSounds.push_back({SoundID, WorldPos});
        return true;
    }

    void AdvanceFrame(float DeltaTime)
    {
        mCurrentFrameSpatialSounds.clear();
        if (mVoiceDuckRemainingSeconds > 0.0f)
        {
            ++mTelemetry.DuckedFramesCount;
            mVoiceDuckRemainingSeconds = std::max(0.0f, mVoiceDuckRemainingSeconds - std::max(0.0f, DeltaTime));
            mCurrentDuckGain = mVoiceDuckTargetGain;
        }
        else if (mCurrentDuckGain < 1.0f)
        {
            mCurrentDuckGain = std::min(1.0f, mCurrentDuckGain + std::max(0.0f, DeltaTime) * mDuckRecoveryRatePerSec);
        }
    }

    void SetCoalesceRadius(float RadiusPx) { mCoalesceRadiusPx = std::max(0.0f, RadiusPx); }
    float GetCoalesceRadius() const { return mCoalesceRadiusPx; }

    const AudioMixerTelemetry& GetTelemetry() const { return mTelemetry; }

private:
    std::array<AudioBusState, static_cast<size_t>(AudioBusId::Count)> mBuses{};
    float mVoiceDuckRemainingSeconds = 0.0f;
    float mVoiceDuckTargetGain = 0.45f;
    float mCurrentDuckGain = 1.0f;
    float mDuckRecoveryRatePerSec = 2.5f;
    float mCoalesceRadiusPx = 96.0f;
    std::vector<SpatialSoundInstance> mCurrentFrameSpatialSounds;
    AudioMixerTelemetry mTelemetry{};
};

} // namespace Core::Audio
