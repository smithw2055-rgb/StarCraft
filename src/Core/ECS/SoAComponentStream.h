#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/Concurrency/JobSystem.h"
#include "Core/ECS/EntityManager.h"

namespace Core::ECS
{

// ============================================================================
// KinematicsSoAStream: Structure-of-Arrays (SoA) Hot Component Stream
// Inspired by Unity DOTS Archetype Chunk SoA Layout & SIMD Data Streams:
// - Zero dependency on Game/Components.h (Phase 49 Engine Decoupling).
// - Extracts hot locomotion & transform fields (PosX, PosY, PrevPosX, PrevPosY,
//   VelX, VelY, Speed) into contiguous flat float arrays for auto-vectorization
//   (SSE/AVX2) and cache-line-friendly parallel batch integration.
// ============================================================================
class KinematicsSoAStream
{
public:
    void Reserve(size_t Capacity)
    {
        mEntityIds.reserve(Capacity);
        mPosX.reserve(Capacity);
        mPosY.reserve(Capacity);
        mPrevPosX.reserve(Capacity);
        mPrevPosY.reserve(Capacity);
        mVelX.reserve(Capacity);
        mVelY.reserve(Capacity);
        mMaxSpeed.reserve(Capacity);
    }

    void Clear()
    {
        mEntityIds.clear();
        mPosX.clear();
        mPosY.clear();
        mPrevPosX.clear();
        mPrevPosY.clear();
        mVelX.clear();
        mVelY.clear();
        mMaxSpeed.clear();
    }

    void PushEntityKinematics(EntityID Id,
                              float PosX,
                              float PosY,
                              float PrevPosX,
                              float PrevPosY,
                              float VelX,
                              float VelY,
                              float MaxSpeed = 0.0f)
    {
        mEntityIds.push_back(Id);
        mPosX.push_back(PosX);
        mPosY.push_back(PosY);
        mPrevPosX.push_back(PrevPosX);
        mPrevPosY.push_back(PrevPosY);
        mVelX.push_back(VelX);
        mVelY.push_back(VelY);
        mMaxSpeed.push_back(MaxSpeed);
    }

    size_t ExtractFromECS(const EntityManager& EM)
    {
        Clear();
        const auto& Hooks = EngineAdapterHooks::Instance();
        if (Hooks.ExtractKinematics)
        {
            Hooks.ExtractKinematics(
                EM,
                [this](EntityID Id,
                       float PX,
                       float PY,
                       float PrevX,
                       float PrevY,
                       float VX,
                       float VY,
                       float Speed)
                {
                    PushEntityKinematics(Id, PX, PY, PrevX, PrevY, VX, VY, Speed);
                });
        }
        return mEntityIds.size();
    }

    // Contiguous SIMD-friendly batch kinematics integration:
    // PrevPos = Pos; Pos = clamp(Pos + Vel * Dt, [MinX..MaxX], [MinY..MaxY])
    void IntegrateBatch(float Dt,
                        float MinX = 0.0f,
                        float MinY = 0.0f,
                        float MaxX = 8192.0f,
                        float MaxY = 8192.0f,
                        uint32_t MinParallelBatchSize = 256u)
    {
        const uint32_t Count = static_cast<uint32_t>(mEntityIds.size());
        if (Count == 0 || Dt <= 0.0f)
            return;

        float* __restrict PX = mPosX.data();
        float* __restrict PY = mPosY.data();
        float* __restrict PrevX = mPrevPosX.data();
        float* __restrict PrevY = mPrevPosY.data();
        const float* __restrict VX = mVelX.data();
        const float* __restrict VY = mVelY.data();

        Core::Concurrency::JobSystem::Instance().ParallelForRange(
            Count,
            MinParallelBatchSize,
            [=](uint32_t Begin, uint32_t End, uint32_t /*ThreadIdx*/)
            {
                for (uint32_t I = Begin; I < End; ++I)
                {
                    const float CurX = PX[I];
                    const float CurY = PY[I];
                    PrevX[I] = CurX;
                    PrevY[I] = CurY;
                    PX[I] = std::clamp(CurX + VX[I] * Dt, MinX, MaxX);
                    PY[I] = std::clamp(CurY + VY[I] * Dt, MinY, MaxY);
                }
            });
    }

    // Contiguous batch linear interpolation for smooth high-refresh-rate rendering:
    // OutPos = PrevPos + (Pos - PrevPos) * Alpha
    void ComputeInterpolatedPositions(float Alpha,
                                      std::vector<float>& OutInterpX,
                                      std::vector<float>& OutInterpY) const
    {
        const size_t Count = mEntityIds.size();
        OutInterpX.resize(Count);
        OutInterpY.resize(Count);
        if (Count == 0)
            return;

        const float ClampedAlpha = std::clamp(Alpha, 0.0f, 1.0f);
        const float* __restrict PX = mPosX.data();
        const float* __restrict PY = mPosY.data();
        const float* __restrict PrevX = mPrevPosX.data();
        const float* __restrict PrevY = mPrevPosY.data();
        float* __restrict OX = OutInterpX.data();
        float* __restrict OY = OutInterpY.data();

        for (size_t I = 0; I < Count; ++I)
        {
            OX[I] = PrevX[I] + (PX[I] - PrevX[I]) * ClampedAlpha;
            OY[I] = PrevY[I] + (PY[I] - PrevY[I]) * ClampedAlpha;
        }
    }

    void WriteBackToECS(EntityManager& EM) const
    {
        const auto& Hooks = EngineAdapterHooks::Instance();
        if (!Hooks.WriteBackKinematics)
            return;

        const size_t Count = mEntityIds.size();
        for (size_t I = 0; I < Count; ++I)
        {
            Hooks.WriteBackKinematics(EM, mEntityIds[I], mPosX[I], mPosY[I], mPrevPosX[I], mPrevPosY[I]);
        }
    }

    size_t Size() const { return mEntityIds.size(); }
    bool Empty() const { return mEntityIds.empty(); }

    const std::vector<EntityID>& GetEntityIds() const { return mEntityIds; }
    std::vector<float>& MutablePosX() { return mPosX; }
    std::vector<float>& MutablePosY() { return mPosY; }
    std::vector<float>& MutableVelX() { return mVelX; }
    std::vector<float>& MutableVelY() { return mVelY; }
    const std::vector<float>& GetPosX() const { return mPosX; }
    const std::vector<float>& GetPosY() const { return mPosY; }
    const std::vector<float>& GetPrevPosX() const { return mPrevPosX; }
    const std::vector<float>& GetPrevPosY() const { return mPrevPosY; }
    const std::vector<float>& GetVelX() const { return mVelX; }
    const std::vector<float>& GetVelY() const { return mVelY; }
    const std::vector<float>& GetMaxSpeed() const { return mMaxSpeed; }

private:
    std::vector<EntityID> mEntityIds;
    std::vector<float> mPosX;
    std::vector<float> mPosY;
    std::vector<float> mPrevPosX;
    std::vector<float> mPrevPosY;
    std::vector<float> mVelX;
    std::vector<float> mVelY;
    std::vector<float> mMaxSpeed;
};

} // namespace Core::ECS
