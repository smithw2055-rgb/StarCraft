#pragma once

namespace Game::Tuning
{

inline constexpr float kWorldMoveSpeedScale = 32.0f;

inline float ToWorldMoveSpeed(float SimulationSpeed)
{
    return SimulationSpeed * kWorldMoveSpeedScale;
}

} // namespace Game::Tuning
