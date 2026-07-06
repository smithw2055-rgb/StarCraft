#include "Game/Races/RaceDef.h"
#include "Main/GameSessionBootstrap.h"

#include <cstdio>
#include <cstdlib>
#include <string>

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

    void RequireUnit(int ChkUnitID,
                     const char* ExpectedName,
                     Game::Components::Race ExpectedRace,
                     bool ExpectedBuilding)
    {
        const int DefID = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitID);
        if (DefID < 0)
        {
            std::fprintf(stderr, "FAIL: CHK unit %d (%s) maps to a UnitDef\n",
                ChkUnitID, ExpectedName);
            std::exit(1);
        }

        const auto Def = Game::Races::GetUnitDefByID(DefID);
        Require(Def.Name == ExpectedName, "CHK unit maps to the expected UnitDef name");
        Require(Def.RaceType == ExpectedRace, "CHK unit maps to the expected race");
        Require(Def.IsBuilding == ExpectedBuilding, "CHK unit maps to the expected building flag");
        Require(Def.ChkUnitID == ChkUnitID, "UnitDef preserves the original CHK unit id");
    }
}

int main()
{
    using Game::Components::Race;
    namespace Bootstrap = Game::Client::GameLoop::GameSessionBootstrap;

    RequireUnit(7, "SCV", Race::Terran, false);
    RequireUnit(8, "Wraith", Race::Terran, false);
    RequireUnit(9, "ScienceVessel", Race::Terran, false);
    RequireUnit(11, "Dropship", Race::Terran, false);
    RequireUnit(12, "Battlecruiser", Race::Terran, false);
    RequireUnit(20, "JimRaynorMarine", Race::Terran, false);
    RequireUnit(25, "EdmundDukeSiege", Race::Terran, false);
    RequireUnit(32, "Firebat", Race::Terran, false);
    RequireUnit(34, "Medic", Race::Terran, false);
    RequireUnit(58, "Valkyrie", Race::Terran, false);
    RequireUnit(106, "CommandCenter", Race::Terran, true);
    RequireUnit(120, "MachineShop", Race::Terran, true);
    RequireUnit(122, "EngineeringBay", Race::Terran, true);
    RequireUnit(124, "MissileTurret", Race::Terran, true);
    RequireUnit(125, "Bunker", Race::Terran, true);
    RequireUnit(41, "Drone", Race::Zerg, false);
    RequireUnit(42, "Overlord", Race::Zerg, false);
    RequireUnit(43, "Mutalisk", Race::Zerg, false);
    RequireUnit(48, "Torrasque", Race::Zerg, false);
    RequireUnit(53, "HunterKiller", Race::Zerg, false);
    RequireUnit(131, "Hatchery", Race::Zerg, true);
    RequireUnit(64, "Probe", Race::Protoss, false);
    RequireUnit(66, "Dragoon", Race::Protoss, false);
    RequireUnit(75, "Zeratul", Race::Protoss, false);
    RequireUnit(78, "FenixDragoon", Race::Protoss, false);
    RequireUnit(82, "Gantrithor", Race::Protoss, false);
    RequireUnit(84, "Observer", Race::Protoss, false);
    RequireUnit(154, "Nexus", Race::Protoss, true);

    Require(Game::Data::IsAirUnitType(43), "Mutalisk raw CHK id is treated as air");
    Require(Game::Data::IsAirUnitType(84), "Observer raw CHK id is treated as air");
    Require(Game::Data::IsAirUnitType(80), "Hero Mojo raw CHK id is treated as air");
    Require(Game::Data::IsAirUnitType(86), "Hero Danimoth raw CHK id is treated as air");
    Require(Game::Data::IsAirUnitType(88), "Hero Artanis raw CHK id is treated as air");
    Require(!Game::Data::IsAirUnitType(41), "Drone raw CHK id is treated as ground");
    Require(!Game::Data::IsAirUnitType(64), "Probe raw CHK id is treated as ground");
    Require(!Game::Data::IsAirUnitType(78), "Hero Fenix Dragoon raw CHK id is treated as ground");
    Require(Game::Races::IsDetectorChkUnitID(9),
        "Science Vessel raw CHK id provides detector metadata");
    Require(!Game::Races::IsDetectorChkUnitID(58),
        "Valkyrie raw CHK id is not treated as a detector");
    Require(Game::Races::IsDetectorChkUnitID(124),
        "Missile Turret raw CHK id provides detector metadata");
    Require(Game::Races::GetRaceByChkUnitID(88, Race::Terran) == Race::Protoss,
        "Protoss hero Artanis raw CHK id is classified without a UnitDef");
    Require(Game::Races::GetRaceByChkUnitID(104, Race::Terran) == Race::Zerg,
        "Infested Duran raw CHK id is classified as Zerg without a UnitDef");
    Require(Game::Races::GetRaceByChkUnitID(99, Race::Zerg) == Race::Terran,
        "Samir Duran raw CHK id is classified as Terran without a UnitDef");
    Require(Game::Races::GetRaceByChkUnitID(130, Race::Terran) == Race::Zerg,
        "Infested Command Center raw CHK id is classified as Zerg building data");

    const int OverlordDefID = Game::Races::GetUnitDefID("Overlord");
    const auto OverlordDef = Game::Races::GetUnitDefByID(OverlordDefID);
    Require(OverlordDef.SupplyCost == 0, "Overlord does not consume supply");
    Require(Game::Races::GetSupplyProvidedByDefID(OverlordDefID) == 8,
        "Overlord provides Zerg supply");

    Require(Bootstrap::Detail::TownHallDefForRace(Bootstrap::Detail::StartRace::Terran) ==
            Game::Races::GetUnitDefID("CommandCenter"),
        "Terran playable fallback uses Command Center");
    Require(Bootstrap::Detail::WorkerDefForRace(Bootstrap::Detail::StartRace::Terran) ==
            Game::Races::GetUnitDefID("SCV"),
        "Terran playable fallback uses SCV");
    Require(Bootstrap::Detail::TownHallDefForRace(Bootstrap::Detail::StartRace::Zerg) ==
            Game::Races::GetUnitDefID("Hatchery"),
        "Zerg playable fallback uses Hatchery");
    Require(Bootstrap::Detail::WorkerDefForRace(Bootstrap::Detail::StartRace::Zerg) ==
            Game::Races::GetUnitDefID("Drone"),
        "Zerg playable fallback uses Drone");
    Require(Bootstrap::Detail::TownHallDefForRace(Bootstrap::Detail::StartRace::Protoss) ==
            Game::Races::GetUnitDefID("Nexus"),
        "Protoss playable fallback uses Nexus");
    Require(Bootstrap::Detail::WorkerDefForRace(Bootstrap::Detail::StartRace::Protoss) ==
            Game::Races::GetUnitDefID("Probe"),
        "Protoss playable fallback uses Probe");

    Require(Bootstrap::Detail::RaceFromChkUnitID(43,
            Bootstrap::Detail::StartRace::Terran) == Bootstrap::Detail::StartRace::Zerg,
        "Zerg CHK units infer Zerg playable fallback race");
    Require(Bootstrap::Detail::RaceFromChkUnitID(64,
            Bootstrap::Detail::StartRace::Terran) == Bootstrap::Detail::StartRace::Protoss,
        "Protoss CHK units infer Protoss playable fallback race");
    Require(Bootstrap::Detail::RaceFromChkUnitID(7,
            Bootstrap::Detail::StartRace::Zerg) == Bootstrap::Detail::StartRace::Terran,
        "Terran CHK units infer Terran playable fallback race");
    Require(Bootstrap::Detail::RaceFromChkUnitID(88,
            Bootstrap::Detail::StartRace::Terran) == Bootstrap::Detail::StartRace::Protoss,
        "unmodeled Protoss hero CHK units infer Protoss playable fallback race");
    Require(Bootstrap::Detail::RaceFromChkUnitID(104,
            Bootstrap::Detail::StartRace::Terran) == Bootstrap::Detail::StartRace::Zerg,
        "unmodeled Zerg hero CHK units infer Zerg playable fallback race");

    Require(Bootstrap::Detail::RaceFromScriptRace(0) ==
            Bootstrap::Detail::StartRace::Zerg,
        "SCRIPT/SIDE race 0 selects Zerg fallback");
    Require(Bootstrap::Detail::RaceFromScriptRace(1) ==
            Bootstrap::Detail::StartRace::Terran,
        "SCRIPT/SIDE race 1 selects Terran fallback");
    Require(Bootstrap::Detail::RaceFromScriptRace(2) ==
            Bootstrap::Detail::StartRace::Protoss,
        "SCRIPT/SIDE race 2 selects Protoss fallback");

    std::printf("RaceDefinitionMappingTest passed\n");
    return 0;
}
