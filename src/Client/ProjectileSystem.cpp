#include "Client/ProjectileSystem.h"
#include "Game/Components.h"
#include "Core/Audio/AudioSystem.h"
#include "Render/ParticleSystem.h"
#include "Render/Draw2D.h"
#include "Render/Color.h"
#include "Core/Math/Vec2.h"
#include <cmath>

namespace Game::Client
{

void SpawnProjectilesFromCombat(Core::ECS::EntityManager& EM,
                                 std::vector<Projectile>& Projectiles,
                                 Render::ParticleSystem& Particles,
                                 Core::Audio::AudioSystem& Audio)
{
    auto CombatEnts = EM.GetEntitiesWith<
        Game::Components::TransformComponent,
        Game::Components::CombatComponent>();

    for (int AttackerId : CombatEnts)
    {
        auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(AttackerId);
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(AttackerId);
        if (!Combat || !Transform || Combat->TargetEntityID < 0)
            continue;

        if (Combat->CooldownRemaining > 0.0f &&
            std::abs(Combat->CooldownRemaining - Combat->AttackCooldown) < 0.001f)
        {
            auto* TargetTransform = EM.GetComponent<Game::Components::TransformComponent>(
                Combat->TargetEntityID);
            if (!TargetTransform) continue;

            Projectile P;
            P.SX = Transform->Position.X;
            P.SY = Transform->Position.Y;
            P.TX = TargetTransform->Position.X;
            P.TY = TargetTransform->Position.Y;
            P.Timer = 0.0f;
            P.Duration = 0.3f;
            P.Active = true;
            Projectiles.push_back(P);

            Particles.EmitDamageSpark(TargetTransform->Position.X, TargetTransform->Position.Y);
            Audio.PlaySound((int)Core::Audio::SoundEffect::WeaponFire,
                            Transform->Position.X, Transform->Position.Y);
        }
    }
}

void UpdateProjectiles(std::vector<Projectile>& Projectiles, float Dt)
{
    for (auto It = Projectiles.begin(); It != Projectiles.end(); )
    {
        It->Timer += Dt;
        if (It->Timer >= It->Duration)
            It = Projectiles.erase(It);
        else
            ++It;
    }
}

void RenderProjectiles(const std::vector<Projectile>& Projectiles,
                        Render::Draw2D& Draw2D)
{
    for (auto& P : Projectiles)
    {
        if (!P.Active) continue;
        float T = P.Timer / P.Duration;
        float CX = P.SX + (P.TX - P.SX) * T;
        float CY = P.SY + (P.TY - P.SY) * T;
        float Radius = 3.0f;
        Draw2D.DrawRect(CX - Radius, CY - Radius, Radius * 2, Radius * 2,
            Render::Color(255, 200, 50, 220));
    }
}

} // namespace Game::Client
