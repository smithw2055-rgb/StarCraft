#include "Render/ParticleSystem.h"
#include "Render/Draw2D.h"

#include <cstdlib>
#include <cmath>

namespace Render
{

void ParticleSystem::Init(int MaxParticles)
{
    mMaxParticles = MaxParticles;
    mParticles.resize(mMaxParticles);
    mNextSlot = 0;
}

void ParticleSystem::Shutdown()
{
    mParticles.clear();
}

int ParticleSystem::EmitOne(float X, float Y, float VX, float VY,
                            float Life, float Size, Color Tint)
{
    for (int Attempt = 0; Attempt < mMaxParticles; Attempt++)
    {
        int Idx = mNextSlot;
        mNextSlot = (mNextSlot + 1) % mMaxParticles;
        if (!mParticles[Idx].Active)
        {
            auto& P = mParticles[Idx];
            P.X = X; P.Y = Y;
            P.VX = VX; P.VY = VY;
            P.Life = Life; P.MaxLife = Life;
            P.Size = Size;
            P.Tint = Tint;
            P.Active = true;
            return Idx;
        }
    }
    return -1;
}

void ParticleSystem::EmitExplosion(float X, float Y, int Count)
{
    for (int I = 0; I < Count; I++)
    {
        float Angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
        float Speed = 20.0f + (float)(std::rand() % 60);
        float VX = std::cos(Angle) * Speed;
        float VY = std::sin(Angle) * Speed;
        float Life = 0.3f + (float)(std::rand() % 30) / 100.0f;
        float Size = 2.0f + (float)(std::rand() % 4);
        int R = 200 + (std::rand() % 55);
        int G = 100 + (std::rand() % 50);
        EmitOne(X, Y, VX, VY, Life, Size, Color((uint8_t)R, (uint8_t)G, 20, 220));
    }
}

void ParticleSystem::EmitDamageSpark(float X, float Y)
{
    for (int I = 0; I < 4; I++)
    {
        float Angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
        float Speed = 30.0f + (float)(std::rand() % 40);
        float VX = std::cos(Angle) * Speed;
        float VY = std::sin(Angle) * Speed;
        EmitOne(X, Y, VX, VY, 0.2f, 2.0f, Color(255, 220, 80, 255));
    }
}

void ParticleSystem::EmitBuildingComplete(float X, float Y)
{
    for (int I = 0; I < 15; I++)
    {
        float Angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
        float Speed = 15.0f + (float)(std::rand() % 30);
        float VX = std::cos(Angle) * Speed;
        float VY = std::sin(Angle) * Speed;
        float Life = 0.5f + (float)(std::rand() % 30) / 100.0f;
        int G = 180 + (std::rand() % 75);
        EmitOne(X, Y, VX, VY, Life, 3.0f, Color(60, (uint8_t)G, 60, 200));
    }
}

void ParticleSystem::EmitDeathBurst(float X, float Y, int Count)
{
    EmitExplosion(X, Y, Count);
    for (int I = 0; I < Count / 2; I++)
    {
        float Angle = (float)(std::rand() % 360) * 3.14159f / 180.0f;
        float Speed = 10.0f + (float)(std::rand() % 40);
        float VX = std::cos(Angle) * Speed;
        float VY = std::sin(Angle) * Speed;
        float Life = 0.6f + (float)(std::rand() % 40) / 100.0f;
        EmitOne(X, Y, VX, VY, Life, 6.0f, Color(100, 100, 100, 180));
    }
}

void ParticleSystem::Update(float DeltaTime)
{
    for (auto& P : mParticles)
    {
        if (!P.Active) continue;
        P.Life -= DeltaTime;
        if (P.Life <= 0.0f)
        {
            P.Active = false;
            continue;
        }
        float Fade = P.Life / P.MaxLife;
        P.X += P.VX * DeltaTime;
        P.Y += P.VY * DeltaTime;
        P.VX *= 0.95f;  // friction
        P.VY *= 0.95f;
        P.Tint.A = (uint8_t)(Fade * 255);
    }
}

void ParticleSystem::Render()
{
    auto& D = g_Draw2D;
    for (auto& P : mParticles)
    {
        if (!P.Active) continue;
        float HS = P.Size * 0.5f;
        D.DrawRect(P.X - HS, P.Y - HS, P.Size, P.Size, P.Tint);
    }
}

} // namespace Render