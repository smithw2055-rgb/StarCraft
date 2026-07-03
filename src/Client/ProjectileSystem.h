#pragma once

#include <cstdint>
#include <vector>
#include "Core/ECS/EntityManager.h"

namespace Core::Math { struct Vec2; }
namespace Render { class ParticleSystem; class Draw2D; }
namespace Core::Audio { class AudioSystem; }

namespace Game::Client
{

struct Projectile
{
    float SX = 0.0f, SY = 0.0f, TX = 0.0f, TY = 0.0f;
    float Timer = 0.0f;
    float Duration = 0.3f;
    bool Active = false;
};

void SpawnProjectilesFromCombat(Core::ECS::EntityManager& EM,
                                 std::vector<Projectile>& Projectiles,
                                 Render::ParticleSystem& Particles,
                                 Core::Audio::AudioSystem& Audio);

void UpdateProjectiles(std::vector<Projectile>& Projectiles, float Dt);

void RenderProjectiles(const std::vector<Projectile>& Projectiles,
                        class Render::Draw2D& Draw2D);

} // namespace Game::Client
