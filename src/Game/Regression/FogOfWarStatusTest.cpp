#include "Game/Systems/FogOfWarSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Systems/UpgradeSystem.h"

#include <cstdio>
#include <cstdlib>

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    int SpawnSightUnit(Core::ECS::EntityManager& EM,
                       Core::Math::Vec2 Position,
                       int PlayerID,
                       int SightRange)
    {
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.HitPoints = 40;
        Unit.MaxHitPoints = 40;
        Unit.SightRange = SightRange;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        return EntityID;
    }
}

int main()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();

    Game::Map::GameMap Map;
    Map.Generate(16, 16);
    Game::Systems::FogOfWarSystem Fog(SysMgr, &Map);

    const int Caster = SpawnSightUnit(EM, {32.0f, 32.0f}, 0, 1);
    const int Target = SpawnSightUnit(EM, {320.0f, 320.0f}, 1, 3);

    Fog.Update(0.1f);
    Require(!Fog.IsVisible(10, 10, 0),
            "player cannot see a remote enemy before parasite vision");

    auto& Status =
        EM.AddComponent<Game::Components::StatusEffectComponent>(Target);
    Status.AddEffect(Game::Components::StatusEffect::Parasite, 999.0f, Caster);

    Fog.Update(0.1f);
    Require(Fog.IsVisible(10, 10, 0),
            "parasite reveals the target tile to the caster player");
    Require(Fog.IsVisible(12, 10, 0),
            "parasite shares the target unit sight radius with the caster player");
    Require(!Fog.IsDetected(10, 10, 0),
            "parasite vision does not grant detector coverage by itself");

    bool SawTargetSnapshot = false;
    for (const auto& Snapshot : Fog.GetLastSeenSnapshots())
    {
        if (Snapshot.UnitEntityID == Target && Snapshot.PlayerID == 1)
            SawTargetSnapshot = true;
    }
    Require(SawTargetSnapshot,
            "parasite visibility feeds last-seen snapshots for the local player");

    Status.RemoveEffect(Game::Components::StatusEffect::Parasite);
    Fog.Update(0.1f);
    Require(!Fog.IsVisible(10, 10, 0),
            "parasite visibility stops when the status is removed");
    Require(Fog.IsExplored(10, 10, 0),
            "previous parasite vision leaves explored fog after visibility ends");

    const int Overlord = SpawnSightUnit(EM, {32.0f, 32.0f}, 2, 9);
    auto* OverlordUnit = EM.GetComponent<Game::Components::UnitComponent>(Overlord);
    Require(OverlordUnit != nullptr, "overlord sight test has unit data");
    OverlordUnit->ChkUnitID = 42;
    OverlordUnit->UnitRace = Game::Components::Race::Zerg;

    Fog.Update(0.1f);
    Require(!Fog.IsVisible(12, 1, 2),
            "base Overlord sight does not reveal the eleventh tile");

    Game::Systems::UpgradeSystem::GetUpgrades(2)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Zerg_Antennae)] = 1;
    Fog.Update(0.1f);
    Require(Fog.IsVisible(12, 1, 2),
            "Antennae extends Overlord fog reveal to eleven tiles");

    const int RevealID = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(RevealID).Position =
        {448.0f, 448.0f};
    auto& Reveal =
        EM.AddComponent<Game::Components::RevealAreaComponent>(RevealID);
    Reveal.PlayerID = 0;
    Reveal.SightRange = 2;
    Reveal.DurationRemaining = 0.01f;
    Reveal.ProvidesDetection = true;

    Fog.Update(0.1f);
    Require(Fog.IsVisible(14, 14, 0),
            "temporary reveal areas make their target tile visible");
    Require(Fog.IsDetected(14, 14, 0),
            "temporary reveal areas can provide detector coverage");
    Require(!EM.IsAlive(RevealID),
            "temporary reveal areas expire after their duration elapses");

    Fog.Update(0.1f);
    Require(!Fog.IsVisible(14, 14, 0),
            "expired reveal areas stop making tiles visible");
    Require(Fog.IsExplored(14, 14, 0),
            "expired reveal areas leave explored fog behind");

    std::printf("FogOfWarStatusTest passed\n");
    return 0;
}
