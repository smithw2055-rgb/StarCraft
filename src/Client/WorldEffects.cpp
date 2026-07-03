#include "Client/WorldEffects.h"

#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Render/Color.h"
#include "Render/Draw2D.h"

#include <algorithm>

namespace Game::Client
{

void WorldEffects::Init(int MaxParticles)
{
    Particles_.Init(MaxParticles);
}

void WorldEffects::UpdateFromCombat(Core::ECS::EntityManager& Entities,
                                    Core::Audio::AudioSystem& Audio,
                                    float DeltaSeconds)
{
    SpawnProjectilesFromCombat(Entities, Projectiles_, Particles_, Audio);
    Particles_.Update(DeltaSeconds);
    UpdateProjectiles(Projectiles_, DeltaSeconds);

    for (auto& Ping : CampaignPings_)
        Ping.TimeRemaining -= DeltaSeconds;
    CampaignPings_.erase(std::remove_if(CampaignPings_.begin(), CampaignPings_.end(),
        [](const CampaignPing& Ping) { return Ping.TimeRemaining <= 0.0f; }),
        CampaignPings_.end());
}

void WorldEffects::Render(Render::Draw2D& Draw2D)
{
    Particles_.Render();
    RenderProjectiles(Projectiles_, Draw2D);

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

void WorldEffects::AddCampaignPing(Core::Math::Vec2 Position)
{
    CampaignPings_.push_back({ Position, 4.0f });
    if (CampaignPings_.size() > 8)
        CampaignPings_.erase(CampaignPings_.begin(), CampaignPings_.end() - 8);
}

} // namespace Game::Client
