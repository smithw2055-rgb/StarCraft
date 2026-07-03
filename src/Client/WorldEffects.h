#pragma once

#include "Client/ProjectileSystem.h"
#include "Core/Math/Vec2.h"
#include "Render/ParticleSystem.h"

#include <vector>

namespace Core::Audio { class AudioSystem; }
namespace Core::ECS { class EntityManager; }
namespace Render { class Draw2D; }

namespace Game::Client
{

class WorldEffects
{
public:
    void Init(int MaxParticles);
    void UpdateFromCombat(Core::ECS::EntityManager& Entities,
                          Core::Audio::AudioSystem& Audio,
                          float DeltaSeconds);
    void Render(Render::Draw2D& Draw2D);
    void AddCampaignPing(Core::Math::Vec2 Position);

private:
    struct CampaignPing
    {
        Core::Math::Vec2 Position;
        float TimeRemaining = 0.0f;
    };

    std::vector<Projectile> Projectiles_;
    Render::ParticleSystem Particles_;
    std::vector<CampaignPing> CampaignPings_;
};

} // namespace Game::Client
