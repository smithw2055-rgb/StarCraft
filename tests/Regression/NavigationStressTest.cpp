#include "Game/Systems/MovementSystem.h"

#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Systems/CommandSystem.h"
#include "Core/ECS/SystemManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Game::Systems
{
void CommandSystem::IssueMove(
    Core::ECS::EntityManager&,
    const std::vector<Core::ECS::EntityID>&,
    const Core::Math::Vec2&)
{
}

void CommandSystem::AdvanceQueue(
    Core::ECS::EntityManager&, Core::ECS::EntityID)
{
}
}

int main()
{
    using namespace Game::Components;
    using Clock = std::chrono::steady_clock;

    Core::ECS::SystemManager SystemManager;
    auto& Entities = SystemManager.GetEntityManager();
    Game::Map::GameMap Map;
    Map.Generate(128, 64);
    Game::Systems::MovementSystem::SetPathRequestCallback(nullptr);
    Game::Systems::MovementSystem::SetNavigationSyncCallback(nullptr);
    Game::Systems::MovementSystem Movement(SystemManager, &Map);

    constexpr int UnitCount = 800;
    constexpr int Columns = 40;
    constexpr float Spacing = 24.0f;
    for (int Index = 0; Index < UnitCount; ++Index)
    {
        const int Column = Index % Columns;
        const int Row = Index / Columns;
        const Core::Math::Vec2 Position{
            256.0f + (float)Column * Spacing,
            256.0f + (float)Row * Spacing};
        // Keep all 800 units active for the complete 480-tick run while
        // measuring the normal formation fast path.  Dense head-on/choke
        // behavior is covered by deterministic movement regressions; making
        // every row swap mirrored positions here turned the timing benchmark
        // into a permanent-overlap recovery benchmark instead.
        const Core::Math::Vec2 Target{
            Position.X + 2048.0f,
            Position.Y};

        const int EntityID = Entities.CreateEntity();
        Entities.AddComponent<TransformComponent>(EntityID).Position =
            Position;
        auto& Unit = Entities.AddComponent<UnitComponent>(EntityID);
        Unit.ChkUnitID = 0;
        Unit.HitPoints = 40;
        Unit.Speed = 4.0f;
        Unit.SizeType = UnitSize::Small;
        auto& Player = Entities.AddComponent<PlayerComponent>(EntityID);
        Player.PlayerID = Index & 1;
        auto& Command = Entities.AddComponent<CommandComponent>(EntityID);
        Command.CurrentCommand = CommandType::Move;
        Command.TargetPosition = Target;
        auto& Path = Entities.AddComponent<PathfindingComponent>(EntityID);
        Path.Path.push_back(Target);
        Path.RequestedDestination = Target;
        Path.HasCollisionBounds = true;
        Path.CollisionLeft = 6;
        Path.CollisionUp = 6;
        Path.CollisionRight = 6;
        Path.CollisionDown = 6;
        Path.CollisionRadius = 6;
        Path.NodeTolerance = 4.0f;
    }

    constexpr float TickSeconds = 1.0f / 24.0f;
    for (int Tick = 0; Tick < 240; ++Tick)
        Movement.Update(TickSeconds);

    std::vector<double> Milliseconds;
    Milliseconds.reserve(240);
    uint64_t NeighborPairs = 0;
    uint64_t RejectedCandidates = 0;
    uint64_t StuckEvents = 0;
    for (int Tick = 0; Tick < 240; ++Tick)
    {
        const auto Begin = Clock::now();
        Movement.Update(TickSeconds);
        const auto End = Clock::now();
        Milliseconds.push_back(
            std::chrono::duration<double, std::milli>(End - Begin).count());
        const auto& Stats = Movement.GetLastNavigationStats();
        NeighborPairs += Stats.NeighborPairs;
        RejectedCandidates += Stats.CandidatesRejected;
        StuckEvents += Stats.StuckEvents;
    }

    std::sort(Milliseconds.begin(), Milliseconds.end());
    const size_t P95Index = std::min(
        Milliseconds.size() - 1,
        (size_t)std::ceil((double)Milliseconds.size() * 0.95) - 1);
    double Total = 0.0;
    for (const double Value : Milliseconds)
        Total += Value;
    const double Average = Total / (double)Milliseconds.size();
    const double P95 = Milliseconds[P95Index];
    const double Maximum = Milliseconds.back();
    std::printf(
        "NavigationStressTest units=%d warmup=240 sample=240 "
        "average=%.3fms p95=%.3fms max=%.3fms "
        "neighbor_pairs=%llu rejected=%llu stuck=%llu\n",
        UnitCount, Average, P95, Maximum,
        (unsigned long long)NeighborPairs,
        (unsigned long long)RejectedCandidates,
        (unsigned long long)StuckEvents);
    return P95 < 8.0 ? 0 : 1;
}
