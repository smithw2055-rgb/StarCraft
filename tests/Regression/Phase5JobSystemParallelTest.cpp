#include "Core/Concurrency/JobSystem.h"
#include "Core/Spatial/DeterministicSpatialGrid2D.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/Systems/FogOfWarSystem.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <numeric>
#include <vector>

namespace
{

uint64_t HashFloatBits(uint64_t H, float V)
{
    uint32_t Bits = 0;
    std::memcpy(&Bits, &V, sizeof(Bits));
    H ^= static_cast<uint64_t>(Bits);
    H *= 1099511628211ULL;
    return H;
}

void TestJobSystemParallelForAndNestedSafety()
{
    Core::Concurrency::JobSystem Jobs(4);
    assert(Jobs.GetWorkerCount() == 4u);

    constexpr uint32_t N = 10000;
    std::vector<uint32_t> Output(N, 0);

    Jobs.ParallelFor(N, 128, [&](uint32_t Idx)
    {
        // Also test nested ParallelFor safety inside a worker thread
        if (Idx == 0)
        {
            uint32_t InnerSum = 0;
            Jobs.ParallelFor(16, 4, [&](uint32_t J)
            {
                InnerSum += (J + 1);
            });
            assert(InnerSum == 136u);
        }
        Output[Idx] = Idx * 3u + 7u;
    });

    for (uint32_t I = 0; I < N; ++I)
    {
        assert(Output[I] == I * 3u + 7u);
    }

    const auto Stats = Jobs.GetStats();
    assert(Stats.TotalParallelForCalls >= 1u);
    assert(Stats.TotalItemsProcessed >= N);
}

void TestDeterministicMapReduceBitExactAcrossThreadCounts()
{
    constexpr uint32_t NumUnits = 4096;

    auto RunSimulationWithThreads = [&](uint32_t NumWorkers) -> uint64_t
    {
        Core::Concurrency::JobSystem Jobs(NumWorkers);

        struct BoidForce
        {
            float FX = 0.0f;
            float FY = 0.0f;
            uint64_t Hash = 14695981039346656037ULL;
        };

        const BoidForce Final = Jobs.ParallelDeterministicReduce<BoidForce>(
            NumUnits,
            64u,
            BoidForce{},
            [](uint32_t Begin, uint32_t End) -> BoidForce
            {
                BoidForce Local;
                for (uint32_t I = Begin; I < End; ++I)
                {
                    const float Angle = static_cast<float>(I) * 0.0174532925f;
                    const float VX = std::cos(Angle) * 1.37f;
                    const float VY = std::sin(Angle) * 2.41f;
                    Local.FX += VX;
                    Local.FY += VY;
                    Local.Hash = HashFloatBits(Local.Hash, VX);
                    Local.Hash = HashFloatBits(Local.Hash, VY);
                }
                return Local;
            },
            [](const BoidForce& Acc, const BoidForce& Chunk) -> BoidForce
            {
                BoidForce Out;
                Out.FX = Acc.FX + Chunk.FX;
                Out.FY = Acc.FY + Chunk.FY;
                Out.Hash = HashFloatBits(Acc.Hash ^ Chunk.Hash, Out.FX);
                Out.Hash = HashFloatBits(Out.Hash, Out.FY);
                return Out;
            });

        return Final.Hash;
    };

    const uint64_t Hash1 = RunSimulationWithThreads(1);
    const uint64_t Hash2 = RunSimulationWithThreads(2);
    const uint64_t Hash4 = RunSimulationWithThreads(4);
    const uint64_t Hash8 = RunSimulationWithThreads(8);

    assert(Hash1 == Hash2);
    assert(Hash2 == Hash4);
    assert(Hash4 == Hash8);
}

void TestSpatialGridCsrEquivalence()
{
    Core::Spatial::DeterministicSpatialGrid2D<64, 32, 256> Grid;

    // Insert 600 entities across normal cells and negative/far fallback coordinates
    for (int I = 0; I < 600; ++I)
    {
        const float X = (I % 50 == 0)
            ? -5000.0f + static_cast<float>(I)
            : static_cast<float>((I * 37) % 2048);
        const float Y = (I % 50 == 0)
            ? 30000.0f + static_cast<float>(I)
            : static_cast<float>((I * 53) % 2048);
        Grid.Insert(I + 1, {X, Y});
    }

    assert(!Grid.IsCSRFinalized());
    std::vector<int> BeforeCsrA;
    std::vector<int> BeforeCsrB;
    Grid.QueryInto(100.0f, 100.0f, 900.0f, 900.0f, BeforeCsrA);
    Grid.QueryInto(-6000.0f, 29000.0f, -4000.0f, 32000.0f, BeforeCsrB);

    Grid.FinalizeCSR();
    assert(Grid.IsCSRFinalized());

    std::vector<int> AfterCsrA;
    std::vector<int> AfterCsrB;
    Grid.QueryInto(100.0f, 100.0f, 900.0f, 900.0f, AfterCsrA);
    Grid.QueryInto(-6000.0f, 29000.0f, -4000.0f, 32000.0f, AfterCsrB);

    assert(!BeforeCsrA.empty());
    assert(!BeforeCsrB.empty());
    assert(BeforeCsrA == AfterCsrA);
    assert(BeforeCsrB == AfterCsrB);
}

void TestParallelFogOfWarSlices()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();

    Game::Systems::FogOfWarSystem FogInst(SysMgr, nullptr);
    auto* Fog = &FogInst;

    // Create units across 4 different players
    for (int P = 0; P < 4; ++P)
    {
        const int E = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(E).Position = {
            static_cast<float>((20 + P * 20) * 32),
            static_cast<float>((20 + P * 15) * 32)
        };
        auto& U = EM.AddComponent<Game::Components::UnitComponent>(E);
        U.HitPoints = 100;
        U.MaxHitPoints = 100;
        U.SightRange = 8;
        U.IsDetector = (P == 1);
        U.ChkUnitID = (P == 1) ? 102 : 0; // Player 1 has a Missile Turret (detector range 7, sight 8)
        EM.AddComponent<Game::Components::PlayerComponent>(E).PlayerID = P;
    }

    Fog->Update(1.0f / 24.0f);

    for (int P = 0; P < 4; ++P)
    {
        const int TX = 20 + P * 20;
        const int TY = 20 + P * 15;
        assert(Fog->IsGameplayVisible(TX, TY, P));
        assert(!Fog->IsGameplayVisible(TX + 40, TY + 40, P));
    }
    assert(Fog->IsGameplayDetected(40, 35, 1));
    assert(!Fog->IsGameplayDetected(20, 20, 0));
}

} // namespace

int main()
{
    TestJobSystemParallelForAndNestedSafety();
    TestDeterministicMapReduceBitExactAcrossThreadCounts();
    TestSpatialGridCsrEquivalence();
    TestParallelFogOfWarSlices();

    std::cout << "[Phase5JobSystemParallelTest] All JobSystem, CSR SpatialGrid & parallel FogOfWar tests passed.\n";
    return 0;
}
