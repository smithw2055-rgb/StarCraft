#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Audio/AudioBusMixer.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Resource/ModernAssetRegistry.h"

namespace Core::Audio {

// Engine-level Audio Events (Phase 49 Decoupling):
// Re-exported into Game::Events by GameEvents.h and SimulationContext.h
struct AnimAudioNotifyEvent {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t IScriptID = 0;
    std::string ClipName;
    uint32_t Tick = 0;
    uint32_t SoundID = 0;
    uint32_t SoundIDMax = 0;
    std::string AudioClipPath;
    Core::Math::Vec2 WorldPos{0.0f, 0.0f};
};

struct SoundRequestEvent {
    int SoundID = -1;
    float WorldX = 0.0f;
    float WorldY = 0.0f;
    int Priority = 20;
};

struct DispatchedAudioEventRecord {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t SoundID = 0;
    std::string ResolvedWavPath;
    uint8_t Priority = 0;
    uint8_t Flags = 0;
    uint16_t VolumePct = 0;
    float EffectiveBusGain = 1.0f;
    Core::Math::Vec2 WorldPos{0.0f, 0.0f};
};

// Phase 3, Phase 13, Phase 32 & Phase 49 Event-Driven Audio & SFX Catalog Router
// Zero dependency on Game/ headers.
class ModernAudioEventRouterSystem final : public Core::ECS::ISystem {
public:
    ModernAudioEventRouterSystem(
        Core::ECS::SystemManager& SysMgr,
        const Resource::ModernAssetRegistry* Registry = nullptr,
        AudioSystem* AudioBackend = nullptr)
        : mSystemMgr(SysMgr), mRegistry(Registry), mAudioBackend(AudioBackend)
    {
        BindToEventBus();
    }

    ~ModernAudioEventRouterSystem() override {
        if (mSubscriptionID != 0) {
            mSystemMgr.GetEventBus().Unsubscribe<AnimAudioNotifyEvent>(mSubscriptionID);
        }
        if (mSoundReqSubscriptionID != 0) {
            mSystemMgr.GetEventBus().Unsubscribe<SoundRequestEvent>(mSoundReqSubscriptionID);
        }
    }

    std::string_view GetName() const override { return "ModernAudioEventRouterSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::Presentation; }

    void SetAssetRegistry(const Resource::ModernAssetRegistry* Registry) {
        mRegistry = Registry;
        SyncCatalogToAudioBackend();
    }

    void SetAudioBackend(AudioSystem* Backend) {
        mAudioBackend = Backend;
        SyncCatalogToAudioBackend();
    }

    AudioBusMixer& GetBusMixer() { return mBusMixer; }
    const AudioBusMixer& GetBusMixer() const { return mBusMixer; }

    size_t SyncCatalogToAudioBackend() {
        if (!mRegistry || !mAudioBackend) return 0;
        size_t Synced = 0;
        for (const auto& Sfx : mRegistry->GetSounds()) {
            SoundType Meta{};
            Meta.Id = Sfx.SoundID;
            Meta.Priority = Sfx.Priority;
            Meta.Flags = Sfx.Flags;
            Meta.MinVolume = Sfx.VolumePct;
            mAudioBackend->SetSoundType(static_cast<int>(Sfx.SoundID), Meta);
            mAudioBackend->SetSoundPath(static_cast<int>(Sfx.SoundID), Sfx.WavPath);
            ++Synced;
        }
        return Synced;
    }

    void Update(float DeltaTime) override {
        mBusMixer.AdvanceFrame(DeltaTime);
        if (mAudioBackend) {
            mAudioBackend->Update(DeltaTime);
        }
    }

    const std::vector<DispatchedAudioEventRecord>& GetDispatchedHistory() const { return mDispatchedHistory; }
    void ClearHistory() { mDispatchedHistory.clear(); }

private:
    void BindToEventBus() {
        mSubscriptionID = mSystemMgr.GetEventBus().Subscribe<AnimAudioNotifyEvent>(
            [this](const AnimAudioNotifyEvent& Ev) {
                OnAnimAudioNotify(Ev);
            });
        mSoundReqSubscriptionID = mSystemMgr.GetEventBus().Subscribe<SoundRequestEvent>(
            [this](const SoundRequestEvent& Ev) {
                if (Ev.SoundID <= 0)
                    return;
                AnimAudioNotifyEvent Mapped{};
                Mapped.SourceEntity = Core::ECS::InvalidEntity;
                Mapped.SoundID = static_cast<uint32_t>(Ev.SoundID);
                Mapped.WorldPos = Core::Math::Vec2{Ev.WorldX, Ev.WorldY};
                OnAnimAudioNotify(Mapped);
            });
    }

    void OnAnimAudioNotify(const AnimAudioNotifyEvent& Ev) {
        if (!mBusMixer.TryAcquireSpatialVoiceSlot(Ev.SoundID, Ev.WorldPos)) {
            return;
        }

        const float BusGain = mBusMixer.ComputeEffectiveBusGain(AudioBusId::SFX);
        if (BusGain <= 0.0f) {
            return;
        }

        DispatchedAudioEventRecord Rec;
        Rec.SourceEntity = Ev.SourceEntity;
        Rec.SoundID = Ev.SoundID;
        Rec.ResolvedWavPath = Ev.AudioClipPath;
        Rec.WorldPos = Ev.WorldPos;
        Rec.EffectiveBusGain = BusGain;

        if (mRegistry) {
            if (const auto* SfxMeta = mRegistry->FindSoundByID(Ev.SoundID)) {
                Rec.Priority = SfxMeta->Priority;
                Rec.Flags = SfxMeta->Flags;
                Rec.VolumePct = SfxMeta->VolumePct;
                if (Rec.ResolvedWavPath.empty()) {
                    Rec.ResolvedWavPath = SfxMeta->WavPath;
                }
            }
        }

        if (mAudioBackend && Ev.SoundID > 0) {
            mAudioBackend->PlaySound(
                static_cast<int>(Ev.SoundID),
                Ev.WorldPos.X,
                Ev.WorldPos.Y,
                Rec.Priority,
                -1,
                static_cast<SoundFlags>(Rec.Flags));
        }

        mDispatchedHistory.push_back(std::move(Rec));
    }

    Core::ECS::SystemManager& mSystemMgr;
    const Resource::ModernAssetRegistry* mRegistry = nullptr;
    AudioSystem* mAudioBackend = nullptr;
    AudioBusMixer mBusMixer;
    Core::ECS::EventBus::SubscriptionID mSubscriptionID = 0;
    Core::ECS::EventBus::SubscriptionID mSoundReqSubscriptionID = 0;
    std::vector<DispatchedAudioEventRecord> mDispatchedHistory;
};

} // namespace Core::Audio

namespace Game::Events {
using AnimAudioNotifyEvent = Core::Audio::AnimAudioNotifyEvent;
using SoundRequestEvent = Core::Audio::SoundRequestEvent;
} // namespace Game::Events
