#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/Audio/AudioBusMixer.h"
#include "Core/Math/Vec2.h"
#include "Engine/DataAssetRegistry.h"
#include "Resource/ModernAssetRegistry.h"

namespace Engine::Audio
{

struct SpatialSoundRequest2D
{
    uint32_t SoundID = 0;
    Core::Math::Vec2 WorldPosition{0.0f, 0.0f};
    Core::Audio::AudioBusId Bus = Core::Audio::AudioBusId::SFX;
    float GainScale = 1.0f;
    uint32_t CoalescedCount = 1;
};

struct DispatchedAudioVoice2D
{
    uint32_t SoundID = 0;
    Core::Audio::AudioBusId Bus = Core::Audio::AudioBusId::SFX;
    uint8_t Priority = 100;
    float FinalGain = 1.0f;
    float StereoPan = 0.0f;
    uint32_t CoalescedCount = 1;
};

struct AudioServerTelemetry
{
    uint64_t TotalSoundsRegistered = 0;
    uint64_t TotalRequestsEnqueued = 0;
    uint64_t TotalRequestsCoalesced = 0;
    uint64_t TotalVoicesDispatched = 0;
};

// ============================================================================
// AudioServer2D (Phase 46):
// Referenced from Godot 4 AudioServer / AudioStreamPlayer2D & Unity AudioMixer:
// - Manages data-driven sound catalogs (1,144 SFX in cloud Sound_Catalog_1144_SFX.json),
//   same-tick spatial sound coalescing (preventing RTS mass-unit weapon fire clipping),
//   2D listener distance attenuation, stereo panning, and hierarchical bus mixing.
// ============================================================================
class AudioServer2D
{
public:
    void Clear()
    {
        mSoundTable.Clear();
        mPendingRequests.clear();
        mLastFlushedVoices.clear();
        mBusMixer = Core::Audio::AudioBusMixer{};
        mTelemetry = {};
    }

    Engine::AssetHandle<Resource::ModernSoundAsset> RegisterSound(
        uint32_t SoundID,
        std::string Name,
        Resource::ModernSoundAsset Asset)
    {
        ++mTelemetry.TotalSoundsRegistered;
        return mSoundTable.Register(SoundID, std::move(Name), std::move(Asset));
    }

    size_t ImportFromModernRegistry(const Resource::ModernAssetRegistry& Registry)
    {
        size_t Imported = 0;
        for (const auto& Snd : Registry.GetSounds())
        {
            const std::string Name = Snd.WavPath.empty()
                                         ? ("Sound_" + std::to_string(Snd.SoundID))
                                         : Snd.WavPath;
            RegisterSound(Snd.SoundID, Name, Snd);
            ++Imported;
        }
        return Imported;
    }

    const Resource::ModernSoundAsset* FindSoundByID(uint32_t SoundID) const noexcept
    {
        return mSoundTable.FindById(SoundID);
    }

    const Resource::ModernSoundAsset* FindSoundByPath(std::string_view WavPath) const noexcept
    {
        return mSoundTable.FindByName(WavPath);
    }

    void SetCoalesceRadiusPx(float RadiusPx) noexcept
    {
        mCoalesceRadiusPx = std::max(0.0f, RadiusPx);
        mBusMixer.SetCoalesceRadius(mCoalesceRadiusPx);
    }

    float GetCoalesceRadiusPx() const noexcept { return mCoalesceRadiusPx; }

    // Enqueues a 2D spatial sound request, coalescing same-tick identical SoundIDs
    // within mCoalesceRadiusPx to prevent RTS combat audio overload.
    bool EnqueueSpatialSound(uint32_t SoundID,
                             Core::Math::Vec2 WorldPosition,
                             Core::Audio::AudioBusId Bus = Core::Audio::AudioBusId::SFX,
                             float GainScale = 1.0f)
    {
        ++mTelemetry.TotalRequestsEnqueued;
        const float RadiusSq = mCoalesceRadiusPx * mCoalesceRadiusPx;

        for (auto& Existing : mPendingRequests)
        {
            if (Existing.SoundID == SoundID && Existing.Bus == Bus)
            {
                const float DX = Existing.WorldPosition.X - WorldPosition.X;
                const float DY = Existing.WorldPosition.Y - WorldPosition.Y;
                if (DX * DX + DY * DY <= RadiusSq)
                {
                    const float N = static_cast<float>(Existing.CoalescedCount);
                    Existing.WorldPosition.X = (Existing.WorldPosition.X * N + WorldPosition.X) / (N + 1.0f);
                    Existing.WorldPosition.Y = (Existing.WorldPosition.Y * N + WorldPosition.Y) / (N + 1.0f);
                    Existing.CoalescedCount += 1u;
                    Existing.GainScale = std::min(1.5f, Existing.GainScale + 0.12f * GainScale);
                    ++mTelemetry.TotalRequestsCoalesced;
                    return false;
                }
            }
        }

        SpatialSoundRequest2D Req{};
        Req.SoundID = SoundID;
        Req.WorldPosition = WorldPosition;
        Req.Bus = Bus;
        Req.GainScale = std::clamp(GainScale, 0.0f, 1.5f);
        Req.CoalescedCount = 1u;
        mPendingRequests.push_back(Req);
        return true;
    }

    // Evaluates 2D distance attenuation & stereo panning against ListenerCenter,
    // mixes against AudioBusMixer bus gain/ducking, and advances frame timers.
    size_t FlushFrame(Core::Math::Vec2 ListenerCenter,
                      float MaxAudibleDistancePx = 1600.0f,
                      float FrameDeltaSeconds = 1.0f / 60.0f)
    {
        const float SafeMaxDist = std::max(1.0f, MaxAudibleDistancePx);
        mLastFlushedVoices.clear();

        for (const auto& Req : mPendingRequests)
        {
            const float DX = Req.WorldPosition.X - ListenerCenter.X;
            const float DY = Req.WorldPosition.Y - ListenerCenter.Y;
            const float Dist = std::sqrt(DX * DX + DY * DY);
            if (Dist > SafeMaxDist)
                continue;

            const float BusGain = mBusMixer.ComputeEffectiveBusGain(Req.Bus);
            if (BusGain <= 0.0001f)
                continue;

            const float Attenuation = std::clamp(1.0f - (Dist / SafeMaxDist), 0.05f, 1.0f);
            const float StereoPan = std::clamp(DX / (SafeMaxDist * 0.5f), -1.0f, 1.0f);

            uint8_t Priority = 100u;
            float BaseVol = 1.0f;
            if (const auto* Asset = FindSoundByID(Req.SoundID))
            {
                Priority = std::max<uint8_t>(1u, Asset->Priority);
                if (Asset->VolumePct > 0)
                    BaseVol = std::clamp(static_cast<float>(Asset->VolumePct) / 100.0f, 0.1f, 1.0f);
            }

            if (Req.Bus == Core::Audio::AudioBusId::Voice)
            {
                mBusMixer.TriggerVoiceTransmissionDucking(0.5f, 0.45f);
            }

            DispatchedAudioVoice2D Voice{};
            Voice.SoundID = Req.SoundID;
            Voice.Bus = Req.Bus;
            Voice.Priority = Priority;
            Voice.FinalGain = std::clamp(BaseVol * Req.GainScale * Attenuation * BusGain, 0.0f, 1.5f);
            Voice.StereoPan = StereoPan;
            Voice.CoalescedCount = Req.CoalescedCount;
            mLastFlushedVoices.push_back(Voice);
            ++mTelemetry.TotalVoicesDispatched;
        }

        mPendingRequests.clear();
        mBusMixer.AdvanceFrame(FrameDeltaSeconds);
        return mLastFlushedVoices.size();
    }

    Core::Audio::AudioBusMixer& BusMixer() noexcept { return mBusMixer; }
    const Core::Audio::AudioBusMixer& BusMixer() const noexcept { return mBusMixer; }

    const Engine::DataAssetTable<Resource::ModernSoundAsset>& GetSoundTable() const noexcept
    {
        return mSoundTable;
    }

    const std::vector<DispatchedAudioVoice2D>& GetLastFlushedVoices() const noexcept
    {
        return mLastFlushedVoices;
    }

    size_t GetPendingRequestCount() const noexcept { return mPendingRequests.size(); }
    const AudioServerTelemetry& GetTelemetry() const noexcept { return mTelemetry; }

private:
    Engine::DataAssetTable<Resource::ModernSoundAsset> mSoundTable;
    Core::Audio::AudioBusMixer mBusMixer;
    std::vector<SpatialSoundRequest2D> mPendingRequests;
    std::vector<DispatchedAudioVoice2D> mLastFlushedVoices;
    float mCoalesceRadiusPx = 128.0f;
    AudioServerTelemetry mTelemetry{};
};

} // namespace Engine::Audio
