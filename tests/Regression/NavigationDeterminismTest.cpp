#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/MovementSystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

namespace
{
using namespace Game::Components;

void Require(bool Condition, const char* Message)
{
    if (Condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message);
    std::exit(1);
}

void Mix(uint64_t& Hash, uint64_t Value)
{
    Hash ^= Value;
    Hash *= 1099511628211ULL;
}

int CreateUnit(Core::ECS::EntityManager& Entities,
               Core::Math::Vec2 Position,
               Core::Math::Vec2 Target,
               int PlayerID)
{
    const int EntityID = Entities.CreateEntity();
    Entities.AddComponent<TransformComponent>(EntityID).Position =
        Position;
    auto& Unit = Entities.AddComponent<UnitComponent>(EntityID);
    Unit.ChkUnitID = 0;
    Unit.HitPoints = 40;
    Unit.Speed = 4.0f;
    Unit.SizeType = UnitSize::Small;
    Entities.AddComponent<PlayerComponent>(EntityID).PlayerID =
        PlayerID;
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
    return EntityID;
}

uint64_t StateHash(Core::ECS::EntityManager& Entities,
                   uint64_t SimulationTick)
{
    uint64_t Hash = 1469598103934665603ULL;
    Mix(Hash, SimulationTick);
    for (const int EntityID :
         Entities.GetEntitiesWith<
             TransformComponent, PathfindingComponent>())
    {
        const auto* Path =
            Entities.GetComponent<PathfindingComponent>(EntityID);
        Mix(Hash, (uint64_t)EntityID);
        Mix(Hash, (uint32_t)Path->FixedPosition.X.raw_value);
        Mix(Hash, (uint32_t)Path->FixedPosition.Y.raw_value);
        Mix(Hash, (uint32_t)Path->FixedVelocity.X.raw_value);
        Mix(Hash, (uint32_t)Path->FixedVelocity.Y.raw_value);
        Mix(Hash, (uint32_t)Path->PathIndex);
        Mix(Hash, (uint32_t)Path->LastPathStatus);
        Mix(Hash, (uint32_t)Path->LastPathEndReason);
        Mix(Hash, (uint32_t)Path->StallTicks);
        Mix(Hash, (uint32_t)Path->DestinationStallTicks);
    }
    return Hash;
}

std::vector<uint64_t> RunSchedule(
    const std::vector<double>& HostFrameSeconds,
    double SimulationScale)
{
    Core::ECS::SystemManager SystemManager;
    auto& Entities = SystemManager.GetEntityManager();
    Game::Map::GameMap Map;
    Map.Generate(32, 16);
    Game::Systems::MovementSystem::SetPathRequestCallback(nullptr);
    Game::Systems::MovementSystem::SetNavigationSyncCallback(nullptr);
    Game::Systems::MovementSystem Movement(SystemManager, &Map);

    CreateUnit(Entities, {160.0f, 224.0f},
               {864.0f, 224.0f}, 0);
    CreateUnit(Entities, {864.0f, 224.0f},
               {160.0f, 224.0f}, 1);
    CreateUnit(Entities, {512.0f, 160.0f},
               {512.0f, 288.0f}, 0);

    constexpr double TickSeconds = 1.0 / 24.0;
    constexpr uint64_t TargetTicks = 96;
    double Accumulator = 0.0;
    uint64_t Tick = 0;
    std::vector<uint64_t> Hashes;
    Hashes.reserve(TargetTicks);
    for (const double HostDt : HostFrameSeconds)
    {
        Accumulator += HostDt * SimulationScale;
        while (Accumulator + 1.0e-12 >= TickSeconds &&
               Tick < TargetTicks)
        {
            Movement.Update((float)TickSeconds);
            Accumulator -= TickSeconds;
            ++Tick;
            Hashes.push_back(StateHash(Entities, Tick));
        }
        if (Tick >= TargetTicks)
            break;
    }
    Require(Tick == TargetTicks,
            "frame schedule produces exactly 96 authoritative ticks");
    return Hashes;
}
}

int main()
{
    std::vector<double> Stable60(240, 1.0 / 60.0);

    std::vector<double> Alternating;
    Alternating.reserve(240);
    for (int Pair = 0; Pair < 120; ++Pair)
    {
        Alternating.push_back(1.0 / 120.0);
        Alternating.push_back(1.0 / 40.0);
    }

    std::vector<double> Accelerated(240, 1.0 / 240.0);

    const auto StableHashes = RunSchedule(Stable60, 1.0);
    const auto AlternatingHashes = RunSchedule(Alternating, 1.0);
    const auto AcceleratedHashes = RunSchedule(Accelerated, 4.0);
    Require(StableHashes == AlternatingHashes,
            "alternating host frames preserve every simulation-tick hash");
    Require(StableHashes == AcceleratedHashes,
            "accelerated observer input preserves every simulation-tick hash");

    std::printf(
        "NavigationDeterminismTest passed ticks=%zu final_hash=%llu\n",
        StableHashes.size(),
        (unsigned long long)StableHashes.back());
    return 0;
}
