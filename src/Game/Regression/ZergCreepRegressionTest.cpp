#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/ZergSystem.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace Game::Systems
{

void CombatSystem::IssueAttack(Core::ECS::EntityManager&,
                               const std::vector<int>&,
                               int)
{
}

} // namespace Game::Systems

namespace
{

void Require(bool Value, const std::string& Message)
{
    if (Value)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message.c_str());
    std::exit(1);
}

int CreateZergBuilding(Core::ECS::EntityManager& EM,
                       int ChkUnitID,
                       int TileX,
                       int TileY)
{
    const int EntityID = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = {
        TileX * 32.0f + 16.0f,
        TileY * 32.0f + 16.0f
    };
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
    Unit.ChkUnitID = ChkUnitID;
    Unit.UnitRace = Game::Components::Race::Zerg;
    Unit.HitPoints = 400;
    Unit.MaxHitPoints = 400;
    auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
    Building.IsBuilding = true;
    Building.IsOrganic = true;
    Building.UnderConstruction = false;
    Building.BuildingDefID =
        Game::Races::GetUnitDefIDByChkUnitID(ChkUnitID);
    return EntityID;
}

int CountCreep(const Game::Map::GameMap& Map)
{
    int Count = 0;
    for (int Y = 0; Y < Map.GetHeight(); ++Y)
        for (int X = 0; X < Map.GetWidth(); ++X)
            if (Map.GetTile(X, Y).HasCreep)
                ++Count;
    return Count;
}

std::vector<uint8_t> SnapshotCreep(const Game::Map::GameMap& Map)
{
    std::vector<uint8_t> Result(
        (size_t)Map.GetWidth() * (size_t)Map.GetHeight(), 0);
    for (int Y = 0; Y < Map.GetHeight(); ++Y)
        for (int X = 0; X < Map.GetWidth(); ++X)
            Result[(size_t)Y * (size_t)Map.GetWidth() + (size_t)X] =
                Map.GetTile(X, Y).HasCreep ? 1u : 0u;
    return Result;
}

bool WasBoundaryTile(const std::vector<uint8_t>& Creep,
                     int Width,
                     int Height,
                     int TileX,
                     int TileY)
{
    for (int DY = -1; DY <= 1; ++DY)
    {
        for (int DX = -1; DX <= 1; ++DX)
        {
            if (DX == 0 && DY == 0)
                continue;
            const int X = TileX + DX;
            const int Y = TileY + DY;
            if (X < 0 || X >= Width || Y < 0 || Y >= Height ||
                !Creep[(size_t)Y * (size_t)Width + (size_t)X])
            {
                return true;
            }
        }
    }
    return false;
}

int ExactHitPointsRaw(const Game::Components::UnitComponent& Unit)
{
    return Unit.HitPoints * 256 - Unit.HitPointSubDamage;
}

std::pair<int, int> SimulateZergRegeneration(float DeltaTime, int Steps)
{
    Core::ECS::SystemManager SystemManager;
    Game::Systems::ZergSystem Zerg(SystemManager, nullptr);
    auto& EM = SystemManager.GetEntityManager();
    const int EntityID = EM.CreateEntity();
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
    Unit.ChkUnitID = 38; // Hydralisk
    Unit.UnitRace = Game::Components::Race::Zerg;
    Unit.HitPoints = 90;
    Unit.MaxHitPoints = 100;

    for (int Step = 0; Step < Steps; ++Step)
        Zerg.Update(DeltaTime);

    return {Unit.HitPoints, Unit.HitPointSubDamage};
}

void VerifySourceTypeRegressesAfterDeath(int ChkUnitID)
{
    Core::ECS::SystemManager SystemManager;
    Game::Map::GameMap Map;
    Map.Generate(48, 40);
    Game::Systems::ZergSystem Zerg(SystemManager, &Map);
    auto& EM = SystemManager.GetEntityManager();

    const int Source = CreateZergBuilding(EM, ChkUnitID, 24, 22);
    Zerg.Update(0.01f);
    const int FullCount = CountCreep(Map);
    Require(FullCount > 20,
        "CHK " + std::to_string(ChkUnitID) + " creates a creep field");

    auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Source);
    Require(Unit != nullptr, "creep source has unit state");
    Unit->HitPoints = 0;
    Zerg.Update(0.11f);
    const int FirstRegressionCount = CountCreep(Map);
    Require(FirstRegressionCount < FullCount,
        "dead CHK " + std::to_string(ChkUnitID) + " stops sustaining creep");
    Require(FirstRegressionCount > 0,
        "dead CHK " + std::to_string(ChkUnitID) +
        " does not clear its whole creep field in one update");
}

} // namespace

int main()
{
    {
        Core::ECS::SystemManager RegenSystemManager;
        Game::Map::GameMap RegenMap;
        RegenMap.Generate(16, 16);
        Game::Systems::ZergSystem RegenZerg(RegenSystemManager, &RegenMap);
        auto& RegenEM = RegenSystemManager.GetEntityManager();
        const int UnitID = RegenEM.CreateEntity();
        RegenEM.AddComponent<Game::Components::TransformComponent>(UnitID)
            .Position = {80.0f, 80.0f};
        auto& Unit =
            RegenEM.AddComponent<Game::Components::UnitComponent>(UnitID);
        Unit.ChkUnitID = 38; // Hydralisk
        Unit.UnitRace = Game::Components::Race::Zerg;
        Unit.HitPoints = 90;
        Unit.MaxHitPoints = 100;

        Require(!RegenMap.GetTile(2, 2).HasCreep,
            "regeneration test unit starts away from creep");
        RegenZerg.Update(1.0f);
        Require(!RegenMap.GetTile(2, 2).HasCreep,
            "regeneration does not require or create creep under a unit");
        Require(ExactHitPointsRaw(Unit) == 90 * 256 + 4 * 24,
            "completed Zerg unit regenerates 4/256 HP per 24 Hz tick off creep");
    }

    {
        Core::ECS::SystemManager ExactSystemManager;
        Game::Systems::ZergSystem ExactZerg(ExactSystemManager, nullptr);
        auto& ExactEM = ExactSystemManager.GetEntityManager();
        const int UnitID = ExactEM.CreateEntity();
        auto& Unit =
            ExactEM.AddComponent<Game::Components::UnitComponent>(UnitID);
        Unit.ChkUnitID = 38;
        Unit.UnitRace = Game::Components::Race::Zerg;
        Unit.HitPoints = 96;
        Unit.MaxHitPoints = 100;

        for (int Tick = 0; Tick < 256; ++Tick)
            ExactZerg.Update(1.0f / 24.0f);

        Require(Unit.HitPoints == 100 && Unit.HitPointSubDamage == 0,
            "256 Brood War ticks regenerate exactly four visible HP");
        Require(Unit.HealthRegenAccumulator == 0.0f,
            "reaching full health clears the Zerg regeneration accumulator");
    }

    {
        const auto LargeStep = SimulateZergRegeneration(1.0f, 1);
        const auto SmallSteps = SimulateZergRegeneration(1.0f / 24.0f, 24);
        Require(LargeStep == SmallSteps,
            "Zerg regeneration is independent of large or small delta-time slices");
        Require(LargeStep.first * 256 - LargeStep.second ==
                    90 * 256 + 4 * 24,
            "one second of sliced regeneration restores exactly 96 sub-points");
    }

    {
        Core::ECS::SystemManager BuildingSystemManager;
        Game::Systems::ZergSystem BuildingZerg(BuildingSystemManager, nullptr);
        auto& BuildingEM = BuildingSystemManager.GetEntityManager();
        const int CompleteID = CreateZergBuilding(BuildingEM, 131, 4, 4);
        const int IncompleteID = CreateZergBuilding(BuildingEM, 131, 8, 4);
        auto* Complete =
            BuildingEM.GetComponent<Game::Components::UnitComponent>(CompleteID);
        auto* Incomplete =
            BuildingEM.GetComponent<Game::Components::UnitComponent>(IncompleteID);
        auto* IncompleteBuilding =
            BuildingEM.GetComponent<Game::Components::BuildingComponent>(IncompleteID);
        Require(Complete && Incomplete && IncompleteBuilding,
            "regeneration building fixtures have unit and construction state");
        Complete->HitPoints = 399;
        Complete->HitPointSubDamage = 128;
        Incomplete->HitPoints = 399;
        Incomplete->HitPointSubDamage = 128;
        IncompleteBuilding->UnderConstruction = true;

        BuildingZerg.Update(1.0f);
        Require(ExactHitPointsRaw(*Complete) == 399 * 256 - 128 + 4 * 24,
            "completed Zerg buildings regenerate without a creep map");
        Require(ExactHitPointsRaw(*Incomplete) == 399 * 256 - 128 &&
                Incomplete->HealthRegenAccumulator == 0.0f,
            "unfinished Zerg buildings neither regenerate nor bank elapsed time");
    }

    {
        Core::ECS::SystemManager BurrowSystemManager;
        Game::Map::GameMap BurrowMap;
        BurrowMap.Generate(16, 16);
        Game::Systems::ZergSystem BurrowZerg(BurrowSystemManager, &BurrowMap);
        auto& BurrowEM = BurrowSystemManager.GetEntityManager();
        const int UnitID = BurrowEM.CreateEntity();
        BurrowEM.AddComponent<Game::Components::TransformComponent>(UnitID)
            .Position = {128.0f, 128.0f};
        auto& Unit = BurrowEM.AddComponent<Game::Components::UnitComponent>(UnitID);
        Unit.ChkUnitID = 37;
        Unit.HitPoints = 35;
        Unit.MaxHitPoints = 35;
        auto& Burrow =
            BurrowEM.AddComponent<Game::Components::BurrowComponent>(UnitID);
        Burrow.CanBurrow = true;
        Require(Game::Systems::ZergSystem::BurrowUnit(BurrowEM, UnitID),
            "unit begins Burrow transition without requiring an animation test hook");
        BurrowZerg.Update(2.1f);
        auto* Cloak =
            BurrowEM.GetComponent<Game::Components::CloakComponent>(UnitID);
        Require(Burrow.Phase == Game::Components::BurrowPhase::Burrowed &&
                Burrow.IsBurrowed && Cloak && Cloak->IsBurrowed &&
                Cloak->IsCloaked,
            "missing Burrow iscript signal converges through gameplay timeout");

        Require(Game::Systems::ZergSystem::BurrowUnit(BurrowEM, UnitID),
            "burrow toggle begins UnBurrow transition");
        BurrowZerg.Update(2.1f);
        Require(Burrow.Phase == Game::Components::BurrowPhase::Surface &&
                !Burrow.IsBurrowed && Cloak && !Cloak->IsBurrowed &&
                !Cloak->IsCloaked,
            "missing UnBurrow signal restores a movable surface unit");
    }

    {
        Core::ECS::SystemManager PreplacedSystemManager;
        Game::Map::GameMap PreplacedMap;
        PreplacedMap.Generate(32, 24);
        for (int Y = 9; Y <= 11; ++Y)
        {
            for (int X = 14; X <= 17; ++X)
            {
                auto Tile = PreplacedMap.GetTile(X, Y);
                Tile.HasCreep = true;
                PreplacedMap.SetTile(X, Y, Tile);
            }
        }
        Game::Systems::ZergSystem PreplacedZerg(
            PreplacedSystemManager, &PreplacedMap);
        const int PreplacedCount = CountCreep(PreplacedMap);
        PreplacedZerg.Update(30.0f);
        Require(CountCreep(PreplacedMap) == PreplacedCount,
            "pre-placed creep remains stable until a registered source is removed");
    }

    {
        Core::ECS::SystemManager PreplacedOverlapSystemManager;
        Game::Map::GameMap PreplacedOverlapMap;
        PreplacedOverlapMap.Generate(48, 40);
        for (const auto& [X, Y] :
             std::array<std::pair<int, int>, 2>{{{24, 22}, {3, 3}}})
        {
            auto Tile = PreplacedOverlapMap.GetTile(X, Y);
            Tile.HasCreep = true;
            PreplacedOverlapMap.SetTile(X, Y, Tile);
        }
        Game::Systems::ZergSystem PreplacedOverlapZerg(
            PreplacedOverlapSystemManager, &PreplacedOverlapMap);
        auto& PreplacedOverlapEM =
            PreplacedOverlapSystemManager.GetEntityManager();
        const int Source =
            CreateZergBuilding(PreplacedOverlapEM, 131, 24, 22);
        PreplacedOverlapZerg.Update(0.01f);
        PreplacedOverlapEM.GetComponent<Game::Components::UnitComponent>(Source)
            ->HitPoints = 0;
        for (int Step = 0;
             Step < 2000 && PreplacedOverlapMap.GetTile(24, 22).HasCreep;
             ++Step)
        {
            PreplacedOverlapZerg.Update(0.11f);
        }
        Require(!PreplacedOverlapMap.GetTile(24, 22).HasCreep,
            "pre-placed creep inside a removed provider area follows OpenBW recession");
        Require(PreplacedOverlapMap.GetTile(3, 3).HasCreep,
            "pre-placed creep outside every removed provider area remains intact");
    }

    // OpenBW's unit_type_spreads_creep list: all three base tiers plus every
    // colony form. Each must stop sustaining immediately when its HP reaches 0.
    for (int ChkUnitID : std::array<int, 6>{131, 132, 133, 143, 144, 146})
        VerifySourceTypeRegressesAfterDeath(ChkUnitID);

    {
        Core::ECS::SystemManager ResustainSystemManager;
        Game::Map::GameMap ResustainMap;
        ResustainMap.Generate(48, 40);
        Game::Systems::ZergSystem ResustainZerg(
            ResustainSystemManager, &ResustainMap);
        auto& ResustainEM = ResustainSystemManager.GetEntityManager();
        const int OldSource = CreateZergBuilding(ResustainEM, 131, 24, 22);
        ResustainZerg.Update(0.01f);
        ResustainEM.GetComponent<Game::Components::UnitComponent>(OldSource)
            ->HitPoints = 0;
        ResustainZerg.Update(0.11f);

        CreateZergBuilding(ResustainEM, 146, 24, 22);
        ResustainZerg.Update(0.01f);
        const int ResustainedCount = CountCreep(ResustainMap);
        for (int Step = 0; Step < 20; ++Step)
            ResustainZerg.Update(0.11f);
        Require(CountCreep(ResustainMap) == ResustainedCount,
            "a newly overlapping live source cancels queued creep regression");
    }

    {
        Core::ECS::SystemManager FootprintSystemManager;
        Game::Map::GameMap FootprintMap;
        FootprintMap.Generate(64, 48);
        Game::Systems::ZergSystem FootprintZerg(
            FootprintSystemManager, &FootprintMap);
        auto& FootprintEM = FootprintSystemManager.GetEntityManager();
        const int FootprintHatchery =
            CreateZergBuilding(FootprintEM, 131, 22, 24);
        const int HydraliskDen =
            CreateZergBuilding(FootprintEM, 135, 26, 24);
        FootprintZerg.Update(0.01f);

        FootprintEM.GetComponent<Game::Components::UnitComponent>(
            FootprintHatchery)->HitPoints = 0;
        for (int Step = 0; Step < 2000 && CountCreep(FootprintMap) > 6; ++Step)
            FootprintZerg.Update(0.11f);

        Require(CountCreep(FootprintMap) == 6,
            "after the Hatchery dies, a live Hydralisk Den sustains only its 3x2 footprint");
        for (int Y = 23; Y <= 24; ++Y)
            for (int X = 25; X <= 27; ++X)
                Require(FootprintMap.GetTile(X, Y).HasCreep,
                    "live grounded Zerg tech building keeps creep under its footprint");

        FootprintEM.DestroyEntity(HydraliskDen);
        FootprintZerg.Update(0.11f);
        Require(CountCreep(FootprintMap) < 6 && CountCreep(FootprintMap) > 0,
            "destroying the last ordinary Zerg provider starts gradual footprint recession");
        for (int Step = 0; Step < 100 && CountCreep(FootprintMap) > 0; ++Step)
            FootprintZerg.Update(0.11f);
        Require(CountCreep(FootprintMap) == 0,
            "ordinary Zerg building footprint fully recedes after that building dies");
    }

    Core::ECS::SystemManager SystemManager;
    Game::Map::GameMap Map;
    Map.Generate(64, 48);
    Game::Systems::ZergSystem Zerg(SystemManager, &Map);
    auto& EM = SystemManager.GetEntityManager();

    const int Hatchery = CreateZergBuilding(EM, 131, 22, 24);
    const int SporeColony = CreateZergBuilding(EM, 144, 34, 24);
    Zerg.Update(0.01f);
    const int CombinedCount = CountCreep(Map);
    const auto BeforeDeath = SnapshotCreep(Map);
    Require(CombinedCount > 80, "overlapping live sources create a combined creep field");
    Require(Map.GetTile(28, 24).HasCreep,
        "overlap tile starts covered by Hatchery and Spore Colony");
    Require(Map.GetTile(14, 24).HasCreep,
        "Hatchery-exclusive outer tile starts covered");

    EM.GetComponent<Game::Components::UnitComponent>(Hatchery)->HitPoints = 0;
    Zerg.Update(0.11f);
    const int AfterFirstStep = CountCreep(Map);
    Require(AfterFirstStep < CombinedCount && AfterFirstStep > 0,
        "source death begins gradual regression without an instant blanket clear");

    int RemovedX = -1;
    int RemovedY = -1;
    int RemovedCount = 0;
    for (int Y = 0; Y < Map.GetHeight(); ++Y)
    {
        for (int X = 0; X < Map.GetWidth(); ++X)
        {
            const size_t Index =
                (size_t)Y * (size_t)Map.GetWidth() + (size_t)X;
            if (BeforeDeath[Index] && !Map.GetTile(X, Y).HasCreep)
            {
                RemovedX = X;
                RemovedY = Y;
                ++RemovedCount;
            }
        }
    }
    Require(RemovedCount == 1,
        "one regression interval removes exactly one unsupported creep tile");
    Require(WasBoundaryTile(BeforeDeath, Map.GetWidth(), Map.GetHeight(),
                            RemovedX, RemovedY),
        "the first regressed tile comes from the outside boundary");

    for (int Step = 0; Step < 1000 && Map.GetTile(14, 24).HasCreep; ++Step)
        Zerg.Update(0.11f);
    Require(!Map.GetTile(14, 24).HasCreep,
        "Hatchery-exclusive creep recedes completely after the Hatchery dies");
    Require(Map.GetTile(28, 24).HasCreep,
        "overlapping creep remains while the Spore Colony source is alive");
    Require(Map.GetTile(34, 24).HasCreep,
        "the living Spore Colony continues to sustain its own creep");

    const int BeforeDestroy = CountCreep(Map);
    EM.DestroyEntity(SporeColony);
    Zerg.Update(0.11f);
    Require(CountCreep(Map) < BeforeDestroy && CountCreep(Map) > 0,
        "destroyed colony starts gradual regression without one-frame clearing");

    for (int Step = 0; Step < 2000 && CountCreep(Map) > 0; ++Step)
        Zerg.Update(0.11f);
    Require(CountCreep(Map) == 0,
        "all creep eventually recedes after the final live source is destroyed");

    std::puts("Zerg creep and regeneration regression tests passed");
    return 0;
}
