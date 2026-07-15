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
    const auto MissileTurret = Game::Races::GetUnitDefByName("MissileTurret");
    Require(MissileTurret.GroundDamage == 0 && MissileTurret.AirDamage > 0,
        "Missile Turret is an active air-only defense");
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
    Require(Game::Races::GetSupplyProvidedByDefID(
                Game::Races::GetUnitDefID("Pylon")) == 8,
        "Pylon provides Protoss supply");
    Require(Game::Races::GetSupplyProvidedByDefID(
                Game::Races::GetUnitDefID("Nexus")) == 10,
        "Nexus provides base Protoss supply");
    for (const auto& Def : Game::Races::GetProtossDefs())
    {
        if (!Def.IsBuilding)
            continue;

        const bool PowerExempt = Def.Name == "Nexus" ||
                                 Def.Name == "Assimilator" ||
                                 Def.Name == "Pylon";
        Require(Def.RequiresPylon == !PowerExempt,
            "only Nexus, Assimilator, and Pylon are exempt from Protoss power");
    }
    Require(Game::Races::GetUnitDefByName("Marine").GroundDamage == 6 &&
                Game::Races::GetUnitDefByName("Marine").AirDamage == 6,
        "Marine Gauss Rifle targets ground and air with its reference damage");
    Require(Game::Races::GetUnitDefByName("Goliath").AirDamage == 20 &&
                Game::Races::GetUnitDefByName("Wraith").AirDamage == 20,
        "Terran anti-air weapon damage matches the reference weapon data");
    Require(Game::Races::GetUnitDefByName("Valkyrie").GroundDamage == 0 &&
                Game::Races::GetUnitDefByName("Valkyrie").AirDamage == 12,
        "Valkyrie is air-only and uses its two-missile volley damage");
    Require(Game::Races::GetUnitDefByName("Hydralisk").AirDamage == 10 &&
                Game::Races::GetUnitDefByName("Mutalisk").AirDamage == 9 &&
                Game::Races::GetUnitDefByName("Scourge").GroundDamage == 0 &&
                Game::Races::GetUnitDefByName("Scourge").AirDamage == 110,
        "Zerg anti-air combat values match the reference weapon data");
    Require(Game::Races::GetUnitDefByName("Zergling").SupplyCost == 1,
        "single-Zergling production reserves supply instead of bypassing population limits");
    Require(Game::Races::GetUnitDefByName("Queen").HitPoints == 120 &&
                Game::Races::GetUnitDefByName("Broodling").HitPoints == 30,
        "Zerg spell-unit hit points match the reference unit data");
    Require(Game::Races::GetUnitDefByName("Zealot").HitPoints == 100 &&
                Game::Races::GetUnitDefByName("Dragoon").AirDamage == 20 &&
                Game::Races::GetUnitDefByName("Corsair").GroundDamage == 0 &&
                Game::Races::GetUnitDefByName("Corsair").AirDamage == 5,
        "Protoss core unit health and target channels match reference data");
    Require(Game::Races::GetUnitDefByName("Archon").HitPoints == 10 &&
                Game::Races::GetUnitDefByName("Archon").ShieldPoints == 350 &&
                Game::Races::GetUnitDefByName("Dark Archon").HitPoints == 25 &&
                Game::Races::GetUnitDefByName("Dark Archon").ShieldPoints == 200,
        "Archon variants preserve their reference health and shield split");
    Require(Game::Races::GetUnitDefByName("Scout").HitPoints == 150 &&
                Game::Races::GetUnitDefByName("Scout").ShieldPoints == 100 &&
                Game::Races::GetUnitDefByName("Scout").AirDamage == 28 &&
                Game::Races::GetUnitDefByName("Carrier").HitPoints == 300 &&
                Game::Races::GetUnitDefByName("Observer").HitPoints == 40,
        "Protoss air-unit health and modeled multi-shot damage match reference data");

    const int HatcheryDefID = Game::Races::GetUnitDefID("Hatchery");
    const int LairDefID = Game::Races::GetUnitDefID("Lair");
    const int HiveDefID = Game::Races::GetUnitDefID("Hive");
    Require(Game::Races::BuildingDefSatisfiesRequirement(LairDefID, HatcheryDefID) &&
                Game::Races::BuildingDefSatisfiesRequirement(HiveDefID, HatcheryDefID) &&
                Game::Races::BuildingDefSatisfiesRequirement(HiveDefID, LairDefID),
        "Zerg town-hall upgrades inherit lower-tier technology requirements");
    Require(Game::Races::BuildingDefSatisfiesRequirement(
                Game::Races::GetUnitDefID("Greater Spire"),
                Game::Races::GetUnitDefID("Spire")),
        "Greater Spire inherits Spire technology requirements");

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
    Require(Bootstrap::Detail::ApplyObserverRaceList(
                "Terran,Zerg,Protoss", 0, Bootstrap::Detail::StartRace::Zerg) ==
            Bootstrap::Detail::StartRace::Terran &&
            Bootstrap::Detail::ApplyObserverRaceList(
                "Terran,Zerg,Protoss", 1, Bootstrap::Detail::StartRace::Terran) ==
            Bootstrap::Detail::StartRace::Zerg &&
            Bootstrap::Detail::ApplyObserverRaceList(
                "Terran,Zerg,Protoss", 2, Bootstrap::Detail::StartRace::Terran) ==
            Bootstrap::Detail::StartRace::Protoss,
        "observer race list assigns Terran, Zerg, and Protoss per player");

    Require(Bootstrap::Detail::IsCampaignMissionPath(
                "assets/locales/zhCN/Assets/campaign/Terran/Terran03/staredit/scenario.chk"),
        "campaign mission paths disable the generic skirmish AI");
    Require(Bootstrap::Detail::IsCampaignMissionPath(
                "ASSETS\\LOCALES\\ZHCN\\ASSETS\\CAMPAIGN\\TERRAN\\TERRAN03\\STAREDIT\\SCENARIO.CHK"),
        "campaign path detection accepts Windows separators and case differences");
    Require(!Bootstrap::Detail::IsCampaignMissionPath(
                "assets/maps/custom/BigGameHunters/scenario.chk"),
        "custom maps retain the generic skirmish AI");

    Game::Trigger::SCRTriggerData PlayerSelectionData;
    PlayerSelectionData.Players[0].Owner = 7;
    PlayerSelectionData.Players[1].Owner = 6;
    Require(Game::Client::GameLoop::ResolveLocalPlayerID(PlayerSelectionData) == 1,
        "generic skirmish AI targets the actual CHK human player instead of fixed player zero");

    std::printf("RaceDefinitionMappingTest passed\n");
    return 0;
}
