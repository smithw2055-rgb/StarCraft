#include "Game/Races/RaceDef.h"
#include "Main/GameSessionBootstrap.h"

#include <algorithm>
#include <cmath>
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

    void RequireBaseStats(const char* Name,
                          int HitPoints,
                          int ShieldPoints,
                          int Armor,
                          Game::Components::UnitSize Size)
    {
        const auto Def = Game::Races::GetUnitDefByName(Name);
        if (Def.HitPoints != HitPoints || Def.ShieldPoints != ShieldPoints ||
            Def.Armor != Armor || Def.SizeType != Size)
        {
            std::fprintf(stderr,
                "FAIL: %s base stats expected hp=%d shield=%d armor=%d size=%d, "
                "got hp=%d shield=%d armor=%d size=%d\n",
                Name, HitPoints, ShieldPoints, Armor, (int)Size,
                Def.HitPoints, Def.ShieldPoints, Def.Armor, (int)Def.SizeType);
            std::exit(1);
        }
    }

    void RequireCost(const char* Name, int Minerals, int Gas)
    {
        const auto Def = Game::Races::GetUnitDefByName(Name);
        if (Def.MineralCost != Minerals || Def.GasCost != Gas)
        {
            std::fprintf(stderr,
                "FAIL: %s cost expected minerals=%d gas=%d, got minerals=%d gas=%d\n",
                Name, Minerals, Gas, Def.MineralCost, Def.GasCost);
            std::exit(1);
        }
    }

    void RequireWeapon(const char* Name,
                       int GroundDamage,
                       int AirDamage,
                       int GroundBonus,
                       int AirBonus,
                       int GroundHits,
                       int AirHits,
                       float GroundCooldown,
                       float AirCooldown,
                       float AttackRange,
                       Game::Components::DamageType GroundType,
                       Game::Components::DamageType AirType)
    {
        const auto Def = Game::Races::GetUnitDefByName(Name);
        const bool Matches =
            Def.GroundDamage == GroundDamage && Def.AirDamage == AirDamage &&
            Def.GroundDamageBonus == GroundBonus && Def.AirDamageBonus == AirBonus &&
            Def.GroundHitCount == GroundHits && Def.AirHitCount == AirHits &&
            std::fabs(Def.GroundAttackCooldown - GroundCooldown) < 0.0001f &&
            std::fabs(Def.AirAttackCooldown - AirCooldown) < 0.0001f &&
            std::fabs(Def.AttackRange - AttackRange) < 0.0001f &&
            Def.GroundDmgType == GroundType && Def.AirDmgType == AirType;
        if (!Matches)
        {
            std::fprintf(stderr, "FAIL: %s weapon metadata does not match weapons.dat\n", Name);
            std::exit(1);
        }
    }
}

int main()
{
    using Game::Components::DamageType;
    using Game::Components::Race;
    using Game::Components::SplashType;
    using Game::Components::UnitSize;
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
    Require(Game::Races::IsDetectorChkUnitID(144),
        "Spore Colony raw CHK id provides detector metadata");
    Require(!Game::Races::IsDetectorChkUnitID(139),
        "Evolution Chamber is not a detector");
    Require(Game::Races::IsDetectorChkUnitID(22),
        "Hero Magellan provides detector metadata");
    Require(Game::Races::IsDetectorChkUnitID(57),
        "Hero Yggdrasill provides detector metadata");
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
    // Base durability and size are the ordinary-unit/building values from
    // units.dat, including the easy-to-miss large air-unit classifications.
    RequireBaseStats("Wraith", 120, 0, 0, UnitSize::Large);
    RequireBaseStats("Refinery", 750, 0, 1, UnitSize::Large);
    RequireBaseStats("PhysicsLab", 600, 0, 1, UnitSize::Large);
    RequireBaseStats("MissileTurret", 200, 0, 0, UnitSize::Large);

    RequireBaseStats("Larva", 25, 0, 10, UnitSize::Small);
    RequireBaseStats("Mutalisk", 120, 0, 0, UnitSize::Small);
    RequireBaseStats("Infested Terran", 60, 0, 0, UnitSize::Small);
    RequireBaseStats("Guardian", 150, 0, 2, UnitSize::Large);
    RequireBaseStats("Devourer", 250, 0, 2, UnitSize::Large);
    RequireBaseStats("Hatchery", 1250, 0, 1, UnitSize::Large);
    RequireBaseStats("Hive", 2500, 0, 1, UnitSize::Large);
    RequireBaseStats("Extractor", 750, 0, 1, UnitSize::Large);
    RequireBaseStats("Evolution Chamber", 750, 0, 1, UnitSize::Large);
    RequireBaseStats("Hydralisk Den", 850, 0, 1, UnitSize::Large);
    RequireBaseStats("Queen's Nest", 850, 0, 1, UnitSize::Large);
    RequireBaseStats("Greater Spire", 1000, 0, 1, UnitSize::Large);
    RequireBaseStats("Defiler Mound", 850, 0, 1, UnitSize::Large);
    RequireBaseStats("Creep Colony", 400, 0, 0, UnitSize::Large);
    RequireBaseStats("Spore Colony", 400, 0, 0, UnitSize::Large);
    RequireBaseStats("Sunken Colony", 300, 0, 2, UnitSize::Large);

    RequireBaseStats("Dragoon", 100, 80, 1, UnitSize::Large);
    RequireBaseStats("Archon", 10, 350, 0, UnitSize::Large);
    RequireBaseStats("Dark Archon", 25, 200, 1, UnitSize::Large);
    RequireBaseStats("Shuttle", 80, 60, 1, UnitSize::Large);
    RequireBaseStats("Scout", 150, 100, 0, UnitSize::Large);
    RequireBaseStats("Carrier", 300, 150, 4, UnitSize::Large);
    RequireBaseStats("Nexus", 750, 750, 1, UnitSize::Large);
    RequireBaseStats("Pylon", 300, 300, 0, UnitSize::Large);
    RequireBaseStats("Gateway", 500, 500, 1, UnitSize::Large);
    RequireBaseStats("Forge", 550, 550, 1, UnitSize::Large);
    RequireBaseStats("Photon Cannon", 100, 100, 0, UnitSize::Large);
    RequireBaseStats("Cybernetics Core", 500, 500, 1, UnitSize::Large);
    RequireBaseStats("Robotics Facility", 500, 500, 1, UnitSize::Large);
    RequireBaseStats("Stargate", 600, 600, 1, UnitSize::Large);
    RequireBaseStats("Citadel of Adun", 450, 450, 1, UnitSize::Large);
    RequireBaseStats("Templar Archives", 500, 500, 1, UnitSize::Large);
    RequireBaseStats("Robotics Support Bay", 450, 450, 1, UnitSize::Large);
    RequireBaseStats("Fleet Beacon", 500, 500, 1, UnitSize::Large);
    RequireBaseStats("Arbiter Tribunal", 500, 500, 1, UnitSize::Large);
    RequireBaseStats("Observatory", 250, 250, 1, UnitSize::Large);

    // Resource costs use the final Brood War melee values, including morph
    // costs and the advanced Zerg/Protoss technology structures.
    RequireCost("Devourer", 150, 50);
    RequireCost("Hydralisk Den", 100, 50);
    RequireCost("Spire", 200, 150);
    RequireCost("Queen's Nest", 150, 100);
    RequireCost("Defiler Mound", 100, 100);
    RequireCost("Dark Templar", 125, 100);
    RequireCost("Shuttle", 200, 0);
    RequireCost("Arbiter", 100, 350);
    RequireCost("Photon Cannon", 150, 0);
    RequireCost("Robotics Facility", 200, 200);
    RequireCost("Robotics Support Bay", 150, 100);
    RequireCost("Fleet Beacon", 300, 200);

    // Weapon values are per hit.  Goliath/Scout missiles, Zealot blades,
    // Firebat flames, and Valkyrie rockets carry explicit hit counts.
    RequireWeapon("SCV", 5, 0, 0, 0, 1, 1, 15.0f / 24.0f, 0.0f,
        10.0f / 32.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Marine", 6, 6, 1, 1, 1, 1, 15.0f / 24.0f, 15.0f / 24.0f,
        4.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Firebat", 8, 0, 1, 0, 2, 1, 22.0f / 24.0f, 0.0f,
        1.0f, DamageType::Concussive, DamageType::Normal);
    Require(Game::Races::GetUnitDefByName("Firebat").WeaponBeh ==
                Game::Components::WeaponBehavior::Melee,
        "Firebat flame attack remains close-range under Dark Swarm");
    RequireWeapon("Ghost", 10, 10, 1, 1, 1, 1, 22.0f / 24.0f, 22.0f / 24.0f,
        7.0f, DamageType::Concussive, DamageType::Concussive);
    RequireWeapon("Vulture", 20, 0, 2, 0, 1, 1, 30.0f / 24.0f, 0.0f,
        5.0f, DamageType::Concussive, DamageType::Normal);
    RequireWeapon("Goliath", 12, 10, 1, 2, 1, 2, 22.0f / 24.0f, 22.0f / 24.0f,
        6.0f, DamageType::Normal, DamageType::Explosive);
    RequireWeapon("SiegeTank", 30, 0, 3, 0, 1, 1, 37.0f / 24.0f, 0.0f,
        7.0f, DamageType::Explosive, DamageType::Normal);
    RequireWeapon("Wraith", 8, 20, 1, 2, 1, 1, 30.0f / 24.0f, 22.0f / 24.0f,
        5.0f, DamageType::Normal, DamageType::Explosive);
    RequireWeapon("Battlecruiser", 25, 25, 3, 3, 1, 1,
        30.0f / 24.0f, 30.0f / 24.0f, 6.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Valkyrie", 0, 6, 0, 1, 1, 8, 0.0f, 64.0f / 24.0f,
        6.0f, DamageType::Normal, DamageType::Explosive);
    RequireWeapon("MissileTurret", 0, 20, 0, 0, 1, 1, 0.0f, 15.0f / 24.0f,
        7.0f, DamageType::Normal, DamageType::Explosive);

    RequireWeapon("Drone", 5, 0, 0, 0, 1, 1, 22.0f / 24.0f, 0.0f,
        1.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Zergling", 5, 0, 1, 0, 1, 1, 8.0f / 24.0f, 0.0f,
        15.0f / 32.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Hydralisk", 10, 10, 1, 1, 1, 1, 15.0f / 24.0f, 15.0f / 24.0f,
        4.0f, DamageType::Explosive, DamageType::Explosive);
    RequireWeapon("Mutalisk", 9, 9, 1, 1, 1, 1, 30.0f / 24.0f, 30.0f / 24.0f,
        3.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Scourge", 0, 110, 0, 0, 1, 1, 0.0f, 1.0f / 24.0f,
        3.0f / 32.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Ultralisk", 20, 0, 3, 0, 1, 1, 15.0f / 24.0f, 0.0f,
        25.0f / 32.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Lurker", 20, 0, 2, 0, 1, 1, 37.0f / 24.0f, 0.0f,
        6.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Broodling", 4, 0, 1, 0, 1, 1, 15.0f / 24.0f, 0.0f,
        2.0f / 32.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Infested Terran", 500, 0, 0, 0, 1, 1, 1.0f / 24.0f, 0.0f,
        3.0f / 32.0f, DamageType::Explosive, DamageType::Normal);
    RequireWeapon("Guardian", 20, 0, 2, 0, 1, 1, 30.0f / 24.0f, 0.0f,
        8.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Devourer", 0, 25, 0, 2, 1, 1, 0.0f, 100.0f / 24.0f,
        6.0f, DamageType::Normal, DamageType::Explosive);
    RequireWeapon("Spore Colony", 0, 15, 0, 0, 1, 1, 0.0f, 15.0f / 24.0f,
        7.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Sunken Colony", 40, 0, 0, 0, 1, 1, 32.0f / 24.0f, 0.0f,
        7.0f, DamageType::Explosive, DamageType::Normal);

    RequireWeapon("Probe", 5, 0, 0, 0, 1, 1, 22.0f / 24.0f, 0.0f,
        1.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Zealot", 8, 0, 1, 0, 2, 1, 22.0f / 24.0f, 0.0f,
        15.0f / 32.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Dragoon", 20, 20, 2, 2, 1, 1, 30.0f / 24.0f, 30.0f / 24.0f,
        4.0f, DamageType::Explosive, DamageType::Explosive);
    RequireWeapon("Dark Templar", 40, 0, 3, 0, 1, 1, 30.0f / 24.0f, 0.0f,
        15.0f / 32.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Archon", 30, 30, 3, 3, 1, 1, 20.0f / 24.0f, 20.0f / 24.0f,
        2.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Reaver", 100, 0, 25, 0, 1, 1, 60.0f / 24.0f, 0.0f,
        8.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Scout", 8, 14, 1, 1, 1, 2, 30.0f / 24.0f, 22.0f / 24.0f,
        4.0f, DamageType::Normal, DamageType::Explosive);
    RequireWeapon("Corsair", 0, 5, 0, 1, 1, 1, 0.0f, 8.0f / 24.0f,
        5.0f, DamageType::Normal, DamageType::Explosive);
    RequireWeapon("Carrier", 6, 6, 1, 1, 1, 1, 24.0f / 24.0f, 24.0f / 24.0f,
        8.0f, DamageType::Normal, DamageType::Normal);
    RequireWeapon("Arbiter", 10, 10, 1, 1, 1, 1, 45.0f / 24.0f, 45.0f / 24.0f,
        5.0f, DamageType::Explosive, DamageType::Explosive);
    RequireWeapon("Photon Cannon", 20, 20, 0, 0, 1, 1,
        22.0f / 24.0f, 22.0f / 24.0f, 7.0f, DamageType::Normal, DamageType::Normal);

    const auto Firebat = Game::Races::GetUnitDefByName("Firebat");
    const auto Lurker = Game::Races::GetUnitDefByName("Lurker");
    const auto Archon = Game::Races::GetUnitDefByName("Archon");
    const auto Infested = Game::Races::GetUnitDefByName("Infested Terran");
    const auto Reaver = Game::Races::GetUnitDefByName("Reaver");
    const auto Valkyrie = Game::Races::GetUnitDefByName("Valkyrie");
    const auto Corsair = Game::Races::GetUnitDefByName("Corsair");
    Require(Firebat.Splash == SplashType::Line && Lurker.Splash == SplashType::Line,
        "Firebat and Lurker use enemy-only line splash");
    Require(Archon.Splash == SplashType::EnemyRadial,
        "Archon uses enemy-only radial splash");
    Require(Infested.Splash == SplashType::Radial && Reaver.Splash == SplashType::Radial,
        "Infested Terran and Reaver use classic radial splash");
    Require(Valkyrie.Splash == SplashType::Air && Corsair.Splash == SplashType::Air,
        "Valkyrie and Corsair use air-only splash");
    Require(Game::Races::GetUnitDefByName("Scourge").SelfDestructOnAttack &&
                Infested.SelfDestructOnAttack,
        "Scourge and Infested Terran self-destruct when their attack lands");

    Game::Components::CombatComponent AppliedValkyrie;
    Game::Races::ApplyCombatDefinition(AppliedValkyrie, Valkyrie, true);
    Require(AppliedValkyrie.AirDamageBonus == 1 && AppliedValkyrie.AirHitCount == 8 &&
                std::fabs(AppliedValkyrie.AirAttackCooldown - 64.0f / 24.0f) < 0.0001f &&
                AppliedValkyrie.Splash == SplashType::Air &&
                std::fabs(AppliedValkyrie.SplashInnerRadius - 5.0f / 32.0f) < 0.0001f &&
                std::fabs(AppliedValkyrie.SplashMediumRadius - 50.0f / 32.0f) < 0.0001f &&
                std::fabs(AppliedValkyrie.SplashRadius - 100.0f / 32.0f) < 0.0001f,
        "ApplyCombatDefinition copies target-specific volley and splash metadata");

    Require(Game::Races::GetUnitDefByName("Zergling").SupplyCost == 1,
        "a Zergling pair reserves one shared supply unit");
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
                Game::Races::GetUnitDefByName("Scout").AirDamage == 14 &&
                Game::Races::GetUnitDefByName("Scout").AirHitCount == 2 &&
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

    const auto Goliath = Game::Races::GetUnitDefByName("Goliath");
    const auto Dropship = Game::Races::GetUnitDefByName("Dropship");
    const auto CorsairProduction = Game::Races::GetUnitDefByName("Corsair");
    const auto HasRequirement = [](const Game::Races::UnitDef& Def, const char* Name)
    {
        const std::vector<Game::Races::UnitDef>* RaceDefs = nullptr;
        if (Def.RaceType == Race::Terran)
            RaceDefs = &Game::Races::GetTerranDefs();
        else if (Def.RaceType == Race::Zerg)
            RaceDefs = &Game::Races::GetZergDefs();
        else
            RaceDefs = &Game::Races::GetProtossDefs();

        int RequiredID = -1;
        for (size_t Index = 0; Index < RaceDefs->size(); ++Index)
        {
            if ((*RaceDefs)[Index].Name == Name)
            {
                RequiredID = (int)Index;
                break;
            }
        }
        return std::find(Def.RequiredBuildings.begin(), Def.RequiredBuildings.end(),
                         RequiredID) != Def.RequiredBuildings.end();
    };
    Require(HasRequirement(Goliath, "Armory") &&
                !HasRequirement(Goliath, "MachineShop"),
        "Goliath production requires an Armory rather than a Machine Shop");
    Require(std::fabs(Goliath.GroundAttackRange - 6.0f) < 0.0001f &&
                std::fabs(Goliath.AirAttackRange - 5.0f) < 0.0001f,
        "Goliath preserves distinct classic ground and air weapon ranges");
    Require(HasRequirement(Dropship, "ControlTower"),
        "Dropship production requires a Control Tower");
    Require(HasRequirement(CorsairProduction, "Stargate") &&
                !HasRequirement(CorsairProduction, "Fleet Beacon"),
        "Corsair production requires only its Stargate producer");

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

    using Game::Systems::AIDifficulty;
    Require(Bootstrap::Detail::ApplyPlayerDifficultyList(
                "Easy,Medium,Hard", 0, AIDifficulty::Medium) == AIDifficulty::Easy &&
            Bootstrap::Detail::ApplyPlayerDifficultyList(
                "Easy,Medium,Hard", 1, AIDifficulty::Easy) == AIDifficulty::Medium &&
            Bootstrap::Detail::ApplyPlayerDifficultyList(
                "Easy,Medium,Hard", 2, AIDifficulty::Easy) == AIDifficulty::Hard,
        "AI difficulty list assigns Easy, Medium, and Hard by actual player slot");
    Require(Bootstrap::Detail::ApplyPlayerDifficultyList(
                "Hard,invalid", 1, AIDifficulty::Easy) == AIDifficulty::Easy &&
            Bootstrap::Detail::ApplyPlayerDifficultyList(
                "Hard", 2, AIDifficulty::Medium) == AIDifficulty::Medium &&
            Bootstrap::Detail::ApplyPlayerDifficultyList(
                "Easy,Medium", -1, AIDifficulty::Hard) == AIDifficulty::Hard,
        "invalid, missing, and negative difficulty slots preserve the configured fallback");

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
