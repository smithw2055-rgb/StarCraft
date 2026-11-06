#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Events/GameEvents.h"
#include "Resource/ModernAssetRegistry.h"

namespace Core::Audio {

struct DispatchedAudioEventRecord {
    Core::ECS::EntityID SourceEntity = Core::ECS::InvalidEntity;
    uint32_t SoundID = 0;
    std::string ResolvedWavPath;
    uint8_t Priority = 0;
    uint8_t Flags = 0;
    uint16_t VolumePct = 0;
    Core::Math::Vec2 WorldPos{0.0f, 0.0f};
};

// Phase 3 Event-Driven Audio & SFX Catalog Router (Godot AudioStreamPlayer2D / Unity AudioMixer)
// Subscribes to AnimAudioNotifyEvent on the ECS EventBus, resolves the 1,144 SFX catalog
// entries from ModernAssetRegistry (Sound_Catalog_1144_SFX.json), configures AudioSystem
// metadata, and dispatches positional 2D audio playback.
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
            mSystemMgr.GetEventBus().Unsubscribe<Game::Events::AnimAudioNotifyEvent>(mSubscriptionID);
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
        if (mAudioBackend) {
            mAudioBackend->Update(DeltaTime);
        }
    }

    const std::vector<DispatchedAudioEventRecord>& GetDispatchedHistory() const { return mDispatchedHistory; }
    void ClearHistory() { mDispatchedHistory.clear(); }

private:
    void BindToEventBus() {
        mSubscriptionID = mSystemMgr.GetEventBus().Subscribe<Game::Events::AnimAudioNotifyEvent>(
            [this](const Game::Events::AnimAudioNotifyEvent& Ev) {
                OnAnimAudioNotify(Ev);
            });
    }

    void OnAnimAudioNotify(const Game::Events::AnimAudioNotifyEvent& Ev) {
        DispatchedAudioEventRecord Rec;
        Rec.SourceEntity = Ev.SourceEntity;
        Rec.SoundID = Ev.SoundID;
        Rec.ResolvedWavPath = Ev.AudioClipPath;
        Rec.WorldPos = Ev.WorldPos;

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
    Core::ECS::EventBus::SubscriptionID mSubscriptionID = 0;
    std::vector<DispatchedAudioEventRecord> mDispatchedHistory;
};

} // namespace Core::Audio
