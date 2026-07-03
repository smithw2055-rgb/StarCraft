#pragma once

#include <vector>
#include <cstdint>

#include "Render/Color.h"

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
    void Init(int MaxParticles = 800);
    void Shutdown();
    void Update(float DeltaTime);
    void Render();

    // Emit particle bursts
    void EmitExplosion(float X, float Y, int Count = 12);
    void EmitDamageSpark(float X, float Y);
    void EmitBuildingComplete(float X, float Y);
    void EmitDeathBurst(float X, float Y, int Count = 20);

private:
    int EmitOne(float X, float Y, float VX, float VY, float Life, float Size, Color Tint);

    std::vector<Particle> mParticles;
    int mMaxParticles = 800;
    int mNextSlot = 0;
};

} // namespace Render