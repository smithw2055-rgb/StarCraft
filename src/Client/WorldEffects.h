#pragma once

#include "Core/Audio/AudioSystem.h"
#include "Core/Color.h"
#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Render/Draw2D.h"
#include "Render/ParticleSystem.h"

#include <algorithm>
#include <vector>

namespace Game::Client
{

struct CampaignPing
{
    Core::Math::Vec2 Position;
    float TimeRemaining = 0.0f;
};

class WorldEffects
{
public:
    void Init(int MaxParticles)
    {
        Particles_.Init(MaxParticles);
    }

    void UpdateFromCombat(Core::ECS::EntityManager& Entities,
                          Core::Audio::AudioSystem& Audio,
                          float DeltaSeconds)
    {
        (void)Entities;
        (void)Audio;
        Particles_.Update(DeltaSeconds);

        for (auto& Ping : CampaignPings_)
            Ping.TimeRemaining -= DeltaSeconds;
        CampaignPings_.erase(std::remove_if(CampaignPings_.begin(), CampaignPings_.end(),
            [](const CampaignPing& Ping) { return Ping.TimeRemaining <= 0.0f; }),
            CampaignPings_.end());
    }

    void Render(Render::Draw2D& Draw2D)
    {
        Particles_.Render();

        for (const auto& Ping : CampaignPings_)
        {
            const float T = std::clamp(Ping.TimeRemaining / 4.0f, 0.0f, 1.0f);
            const float Radius = 16.0f + (1.0f - T) * 26.0f;
            const Render::Color Color(80, 210, 255, (uint8_t)(80 + 150 * T));
            Draw2D.DrawRectLines(Ping.Position.X - Radius,
                                 Ping.Position.Y - Radius,
                                 Radius * 2.0f,
                                 Radius * 2.0f,
                                 Color,
                                 2.0f);
            Draw2D.DrawRect(Ping.Position.X - 2.0f,
                            Ping.Position.Y - Radius,
                            4.0f,
                            Radius * 2.0f,
                            Color);
            Draw2D.DrawRect(Ping.Position.X - Radius,
                            Ping.Position.Y - 2.0f,
                            Radius * 2.0f,
                            4.0f,
                            Color);
        }
    }

    void AddCampaignPing(Core::Math::Vec2 Position)
    {
        CampaignPings_.push_back({ Position, 4.0f });
        if (CampaignPings_.size() > 8)
            CampaignPings_.erase(CampaignPings_.begin(), CampaignPings_.end() - 8);
    }

    const std::vector<CampaignPing>& GetCampaignPings() const { return CampaignPings_; }

private:
    Render::ParticleSystem Particles_;
    std::vector<CampaignPing> CampaignPings_;
};

} // namespace Game::Client
