#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "Core/Color.h"
#include "Render/Draw2D.h"

namespace Render
{

struct Particle
{
    float X = 0.0f, Y = 0.0f;
    float VX = 0.0f, VY = 0.0f;
    float Life = 0.0f;
    float MaxLife = 1.0f;
    float Size = 4.0f;
    Color Tint = Colors::White();
    bool Active = false;
};

class ParticleSystem
{
public:
    void Init(int MaxParticles = 800)
    {
        mMaxParticles = MaxParticles;
        mParticles.resize(mMaxParticles);
        mNextSlot = 0;
    }

    void Shutdown()
    {
        mParticles.clear();
    }

    void Update(float DeltaTime)
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
            P.VX *= 0.95f;
            P.VY *= 0.95f;
            P.Tint.A = (uint8_t)(Fade * 255);
        }
    }

    void Render()
    {
        auto& D = g_Draw2D;
        for (auto& P : mParticles)
        {
            if (!P.Active) continue;
            float HS = P.Size * 0.5f;
            D.DrawRect(P.X - HS, P.Y - HS, P.Size, P.Size, P.Tint);
        }
    }

    void EmitExplosion(float X, float Y, int Count = 12)
    {
        for (int I = 0; I < Count; I++)
        {
            float Angle = (float)(FastRand() % 360) * 3.14159f / 180.0f;
            float Speed = 20.0f + (float)(FastRand() % 60);
            float VX = std::cos(Angle) * Speed;
            float VY = std::sin(Angle) * Speed;
            float Life = 0.3f + (float)(FastRand() % 30) / 100.0f;
            float Size = 2.0f + (float)(FastRand() % 4);
            int R = 200 + (FastRand() % 55);
            int G = 100 + (FastRand() % 50);
            EmitOne(X, Y, VX, VY, Life, Size, Color((uint8_t)R, (uint8_t)G, 20, 220));
        }
    }

    void EmitDamageSpark(float X, float Y)
    {
        for (int I = 0; I < 4; I++)
        {
            float Angle = (float)(FastRand() % 360) * 3.14159f / 180.0f;
            float Speed = 30.0f + (float)(FastRand() % 40);
            float VX = std::cos(Angle) * Speed;
            float VY = std::sin(Angle) * Speed;
            EmitOne(X, Y, VX, VY, 0.2f, 2.0f, Color(255, 220, 80, 255));
        }
    }

    void EmitBuildingComplete(float X, float Y)
    {
        for (int I = 0; I < 15; I++)
        {
            float Angle = (float)(FastRand() % 360) * 3.14159f / 180.0f;
            float Speed = 15.0f + (float)(FastRand() % 30);
            float VX = std::cos(Angle) * Speed;
            float VY = std::sin(Angle) * Speed;
            float Life = 0.5f + (float)(FastRand() % 30) / 100.0f;
            int G = 180 + (FastRand() % 75);
            EmitOne(X, Y, VX, VY, Life, 3.0f, Color(60, (uint8_t)G, 60, 200));
        }
    }

    void EmitDeathBurst(float X, float Y, int Count = 20)
    {
        EmitExplosion(X, Y, Count);
        for (int I = 0; I < Count / 2; I++)
        {
            float Angle = (float)(FastRand() % 360) * 3.14159f / 180.0f;
            float Speed = 10.0f + (float)(FastRand() % 40);
            float VX = std::cos(Angle) * Speed;
            float VY = std::sin(Angle) * Speed;
            float Life = 0.6f + (float)(FastRand() % 40) / 100.0f;
            EmitOne(X, Y, VX, VY, Life, 6.0f, Color(100, 100, 100, 180));
        }
    }

private:
    static uint32_t FastRand()
    {
        static uint32_t sParticleRand = 0x87654321u;
        sParticleRand = sParticleRand * 1664525u + 1013904223u;
        return (sParticleRand >> 16) & 0x7fffu;
    }

    int EmitOne(float X, float Y, float VX, float VY, float Life, float Size, Color Tint)
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

    std::vector<Particle> mParticles;
    int mMaxParticles = 800;
    int mNextSlot = 0;
};

} // namespace Render
