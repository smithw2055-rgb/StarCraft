#include "Game/Races/RaceDef.h"

#include <unordered_map>
#include <cassert>

namespace Game::Races
{

static std::vector<UnitDef> TerranDefs;
static std::vector<UnitDef> ZergDefs;
static std::vector<UnitDef> ProtossDefs;
static std::unordered_map<std::string, int> NameToID;
static bool DefsInitialized = false;

static void InitDefs()
{
    if (DefsInitialized) return;
    DefsInitialized = true;

        auto& T = TerranDefs;

    T.push_back({"SCV",             Race::Terran, UnitSize::Small,  60,  0,  0, 0,  1,  50,   0,  17,  5,  0, 0, 1, 2.8f, 8,  false});
    T.push_back({"Marine",          Race::Terran, UnitSize::Small,  40,  0,  0, 0,  1,  50,   0,  24,  6,  6, 4, 1, 2.5f, 7,  false});
    T.push_back({"Firebat",         Race::Terran, UnitSize::Small,  50,  0,  0, 1,  1,  50,  25,  24,  8,  0, 1, 1, 2.5f, 7,  false});
    T.push_back({"Medic",           Race::Terran, UnitSize::Small,  60,  0, 50, 1,  1,  50,  25,  24,  0,  0, 0, 0, 2.5f, 8,  false});
    T.push_back({"Ghost",           Race::Terran, UnitSize::Small,  45,  0, 50, 0,  1,  25,  75,  30, 10, 10, 7, 1, 2.5f, 9,  false});
    T.push_back({"Vulture",         Race::Terran, UnitSize::Medium, 80,  0,  0, 0,  2,  75,   0,  30, 20,  0, 5, 1, 3.0f, 8,  false});
    T.push_back({"Goliath",         Race::Terran, UnitSize::Large, 125,  0,  0, 1,  2, 100,  50,  37, 12, 20, 6, 1, 2.6f, 8,  false});
    T.push_back({"SiegeTank",       Race::Terran, UnitSize::Large, 150,  0,  0, 1,  2, 150, 100,  45, 30,  0, 7, 1, 2.3f, 10, false});
    T.push_back({"Wraith",          Race::Terran, UnitSize::Large, 120,  0,  0, 0,  2, 150, 100,  60,  8, 20, 5, 1, 3.2f, 9,  false});
    T.push_back({"Dropship",        Race::Terran, UnitSize::Large, 150,  0,  0, 1,  2, 100, 100,  50,  0,  0, 0, 0, 3.0f, 8,  false});
    T.push_back({"ScienceVessel",   Race::Terran, UnitSize::Large, 200,  0,200, 1,  2, 100, 225,  80,  0,  0, 0, 0, 2.8f, 11, false});
    T.push_back({"Battlecruiser",   Race::Terran, UnitSize::Large, 500,  0,200, 3,  6, 400, 300,  90, 25, 25, 6, 1, 2.1f, 11, false});
    T.push_back({"Valkyrie",        Race::Terran, UnitSize::Large, 200,  0,  0, 2,  3, 250, 125,  60,  0,  6, 6, 1, 3.0f, 9,  false});

    int CommandCenterID = (int)T.size();
    T.push_back({"CommandCenter",   Race::Terran, UnitSize::Large, 1500, 0,  0, 1, 10, 400,   0, 120,  0,  0, 0, 0, 0.0f, 11, true, true});
    int ComsatStationID = (int)T.size();
    T.push_back({"ComsatStation",   Race::Terran, UnitSize::Large,  500, 0,200, 1,  0,  50,  50,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int NuclearSiloID = (int)T.size();
    T.push_back({"NuclearSilo",     Race::Terran, UnitSize::Large,  600, 0,  0, 1,  0, 100, 100,  60,  0,  0, 0, 0, 0.0f, 11, true});
    int SupplyDepotID = (int)T.size();
    T.push_back({"SupplyDepot",     Race::Terran, UnitSize::Large,  500, 0,  0, 1,  8, 100,   0,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int RefineryID = (int)T.size();
    T.push_back({"Refinery",        Race::Terran, UnitSize::Large,  750, 0,  0, 1,  0, 100,   0,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int BarracksID = (int)T.size();
    T.push_back({"Barracks",        Race::Terran, UnitSize::Large, 1000, 0,  0, 1,  0, 150,   0,  60,  0,  0, 0, 0, 0.0f, 11, true});
    int AcademyID = (int)T.size();
    T.push_back({"Academy",         Race::Terran, UnitSize::Large,  600, 0,  0, 1,  0, 150,   0,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int FactoryID = (int)T.size();
    T.push_back({"Factory",         Race::Terran, UnitSize::Large, 1250, 0,  0, 1,  0, 200, 100,  60,  0,  0, 0, 0, 0.0f, 11, true});
    int StarportID = (int)T.size();
    T.push_back({"Starport",        Race::Terran, UnitSize::Large, 1300, 0,  0, 1,  0, 150, 100,  50,  0,  0, 0, 0, 0.0f, 11, true});
    int ControlTowerID = (int)T.size();
    T.push_back({"ControlTower",    Race::Terran, UnitSize::Large,  500, 0,  0, 1,  0,  50,  50,  30,  0,  0, 0, 0, 0.0f, 11, true});
    int ScienceFacilityID = (int)T.size();
    T.push_back({"ScienceFacility", Race::Terran, UnitSize::Large,  850, 0,  0, 1,  0, 100, 150,  60,  0,  0, 0, 0, 0.0f, 11, true});
    int CovertOpsID = (int)T.size();
    T.push_back({"CovertOps",       Race::Terran, UnitSize::Large,  750, 0,  0, 1,  0,  50,  50,  30,  0,  0, 0, 0, 0.0f, 11, true});
    int PhysicsLabID = (int)T.size();
    T.push_back({"PhysicsLab",      Race::Terran, UnitSize::Large,  600, 0,  0, 1,  0,  50,  50,  30,  0,  0, 0, 0, 0.0f, 11, true});
    int MachineShopID = (int)T.size();
    T.push_back({"MachineShop",     Race::Terran, UnitSize::Large,  750, 0,  0, 1,  0,  50,  50,  30,  0,  0, 0, 0, 0.0f, 11, true});
    int EngineeringBayID = (int)T.size();
    T.push_back({"EngineeringBay",  Race::Terran, UnitSize::Large,  850, 0,  0, 1,  0, 125,   0,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int ArmoryID = (int)T.size();
    T.push_back({"Armory",          Race::Terran, UnitSize::Large,  750, 0,  0, 1,  0, 100,  50,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int BunkerID = (int)T.size();
    T.push_back({"Bunker",          Race::Terran, UnitSize::Large,  350, 0,  0, 1,  0, 100,   0,  30,  0,  0, 0, 0, 0.0f, 11, true});
    int MissileTurretID = (int)T.size();
    T.push_back({"MissileTurret",   Race::Terran, UnitSize::Large,  200, 0,  0, 0,  0,  75,   0,  20,  0, 20, 7, 1, 0.0f, 11, true});

    T[0].ProducedBy = CommandCenterID;
    T[1].ProducedBy = BarracksID;
    T[2].ProducedBy = BarracksID; T[2].RequiredBuildings = {AcademyID};
    T[3].ProducedBy = BarracksID; T[3].RequiredBuildings = {AcademyID};
    T[4].ProducedBy = BarracksID; T[4].RequiredBuildings = {AcademyID, CovertOpsID};
    T[5].ProducedBy = FactoryID;
    T[6].ProducedBy = FactoryID;  T[6].RequiredBuildings = {ArmoryID};
    T[7].ProducedBy = FactoryID;  T[7].RequiredBuildings = {MachineShopID};
    T[8].ProducedBy = StarportID;
    T[9].ProducedBy = StarportID; T[9].RequiredBuildings = {ControlTowerID};
    T[10].ProducedBy = StarportID; T[10].RequiredBuildings = {ControlTowerID, ScienceFacilityID};
    T[11].ProducedBy = StarportID; T[11].RequiredBuildings = {ControlTowerID, PhysicsLabID};
    T[12].ProducedBy = StarportID; T[12].RequiredBuildings = {ControlTowerID, ArmoryID};

    T[ComsatStationID].ProducedBy = CommandCenterID;
    T[ComsatStationID].RequiredBuildings = {AcademyID};
    T[NuclearSiloID].ProducedBy = CommandCenterID;
    T[NuclearSiloID].RequiredBuildings = {CovertOpsID};
    T[ControlTowerID].ProducedBy = StarportID;
    T[ControlTowerID].RequiredBuildings = {StarportID};
    T[CovertOpsID].ProducedBy = ScienceFacilityID;
    T[CovertOpsID].RequiredBuildings = {ScienceFacilityID};
    T[PhysicsLabID].ProducedBy = ScienceFacilityID;
    T[PhysicsLabID].RequiredBuildings = {ScienceFacilityID};
    T[MachineShopID].ProducedBy = FactoryID;
    T[MachineShopID].RequiredBuildings = {FactoryID};

    T[BarracksID].RequiredBuildings = {SupplyDepotID};
    T[AcademyID].RequiredBuildings = {BarracksID};
    T[FactoryID].RequiredBuildings = {BarracksID};
    T[StarportID].RequiredBuildings = {FactoryID};
    T[ScienceFacilityID].RequiredBuildings = {StarportID};
    T[EngineeringBayID].RequiredBuildings = {CommandCenterID};
    T[ArmoryID].RequiredBuildings = {FactoryID};
    T[BunkerID].RequiredBuildings = {BarracksID};
    T[MissileTurretID].RequiredBuildings = {EngineeringBayID};

    auto SetTerranMeta = [&](int DefID, int ChkUnitID, int IconIndex, bool IsAddon = false, int AddonOnBuildingDefID = -1)
    {
        T[(size_t)DefID].ChkUnitID = ChkUnitID;
        T[(size_t)DefID].DefaultIconIndex = IconIndex;
        T[(size_t)DefID].IsAddon = IsAddon;
        T[(size_t)DefID].AddonOnBuildingDefID = AddonOnBuildingDefID;
    };

    SetTerranMeta(0, 0x07, 0);
    SetTerranMeta(1, 0, 1);
    SetTerranMeta(2, 32, 2);
    SetTerranMeta(3, 34, 3);
    SetTerranMeta(4, 1, 4);
    SetTerranMeta(5, 2, 5);
    SetTerranMeta(6, 3, 6);
    SetTerranMeta(7, 5, 7);
    SetTerranMeta(8, 8, 8);
    SetTerranMeta(9, 11, 9);
    SetTerranMeta(10, 9, 10);
    SetTerranMeta(11, 12, 11);
    SetTerranMeta(12, 58, 12);
    SetTerranMeta(CommandCenterID, 0x6A, 13);
    SetTerranMeta(ComsatStationID, 0x6B, 14, true, CommandCenterID);
    SetTerranMeta(NuclearSiloID, 0x6C, 15, true, CommandCenterID);
    SetTerranMeta(SupplyDepotID, 0x6D, 16);
    SetTerranMeta(RefineryID, 0x6E, 17);
    SetTerranMeta(BarracksID, 0x6F, 18);
    SetTerranMeta(AcademyID, 0x70, 19);
    SetTerranMeta(FactoryID, 0x71, 20);
    SetTerranMeta(StarportID, 0x72, 21);
    SetTerranMeta(ControlTowerID, 0x73, 22, true, StarportID);
    SetTerranMeta(ScienceFacilityID, 0x74, 23);
    SetTerranMeta(CovertOpsID, 0x75, 24, true, ScienceFacilityID);
    SetTerranMeta(PhysicsLabID, 0x76, 25, true, ScienceFacilityID);
    SetTerranMeta(MachineShopID, 0x78, 26, true, FactoryID);
    SetTerranMeta(EngineeringBayID, 0x7A, 27);
    SetTerranMeta(ArmoryID, 0x7B, 28);
    SetTerranMeta(BunkerID, 0x7D, 29);
    SetTerranMeta(MissileTurretID, 0x7C, 30);

    // These are the five Terran structures whose project images expose both
    // LiftOff and Landing iscript entry points.
    for (int DefID : {CommandCenterID, BarracksID, FactoryID, StarportID, ScienceFacilityID})
        T[(size_t)DefID].CanLiftOff = true;

    auto ApplyDefaultWeaponMetadata = [](std::vector<UnitDef>& Defs)
    {
        for (auto& Def : Defs)
        {
            if (Def.GroundDamage <= 0 && Def.AirDamage <= 0)
                continue;
            Def.WeaponBeh = Def.AttackRange > 1.5f
                ? Components::WeaponBehavior::FlyDirect
                : Components::WeaponBehavior::Melee;
        }
    };

    auto& Z = ZergDefs;
    int LarvaID = (int)Z.size();
    Z.push_back({"Larva",            Race::Zerg, UnitSize::Small,  25,  0,  0,10,  0,  0,   0,  1,   0,  0,  0, 0,  0,   0,   false});
    int ZDroneID = (int)Z.size();
    Z.push_back({"Drone",            Race::Zerg, UnitSize::Small,  40,  0,  0, 0,  1,  50,  0,  17,  5,  0,  0, 1,  2.9f, 8,  false});
    int ZOverlordID = (int)Z.size();
    Z.push_back({"Overlord",         Race::Zerg, UnitSize::Large, 200,  0,  0, 0,  0,  100, 0,  25,  0,  0,  0, 0,  0.7f, 9,  false});
    int ZZerglingID = (int)Z.size();
    // One egg produces a pair; the pair shares this single supply reservation.
    Z.push_back({"Zergling",         Race::Zerg, UnitSize::Small,  35,  0,  0, 0,  1,  50,  0,  24,  5,  0,  1, 1,  3.4f, 7,  false});
    int ZHydraliskID = (int)Z.size();
    Z.push_back({"Hydralisk",        Race::Zerg, UnitSize::Medium, 80,  0,  0, 0,  1,  75,  25, 24,  10, 10, 4, 1,  2.5f, 6,  false});
    int ZMutaliskID = (int)Z.size();
    Z.push_back({"Mutalisk",         Race::Zerg, UnitSize::Small, 120,  0,  0, 0,  2,  100, 100,30,  9,  9,  3, 1,  3.5f, 8,  false});
    int ZScourgeID = (int)Z.size();
    Z.push_back({"Scourge",          Race::Zerg, UnitSize::Small,  25,  0,  0, 0,  1,  25,  75, 20,  0,110, 1, 1,  4.5f, 5,  false});
    int ZQueenID = (int)Z.size();
    Z.push_back({"Queen",            Race::Zerg, UnitSize::Medium,120,  0,  0, 0,  2,  100, 100,30,  0,  0,  0, 0,  2.0f,10,  false});
    int ZUltraliskID = (int)Z.size();
    Z.push_back({"Ultralisk",        Race::Zerg, UnitSize::Large, 400,  0,  0, 1,  4,  200, 200,39,  20, 0,  1, 1,  2.9f, 7,  false});
    int ZDefilerID = (int)Z.size();
    Z.push_back({"Defiler",          Race::Zerg, UnitSize::Medium, 80,  0,200, 1,  2,  50,  150,30,  0,  0,  0, 0,  2.0f, 9,  false});
    int ZLurkerID = (int)Z.size();
    Z.push_back({"Lurker",           Race::Zerg, UnitSize::Medium,125,  0,  0, 1,  2,  50,  100,28,  20, 0,  6, 1,  2.5f, 8,  false});
    int ZBroodlingID = (int)Z.size();
    Z.push_back({"Broodling",        Race::Zerg, UnitSize::Small,  30,  0,  0, 0,  0,  0,   0,  1,   4,  0,  1, 1,  2.5f, 5,  false});
    int ZInfestedID = (int)Z.size();
    Z.push_back({"Infested Terran",  Race::Zerg, UnitSize::Small,  60,  0,  0, 0,  1,  0,   0,  20, 500, 0,  5, 1,  2.5f, 8,  false});
    int ZGuardianID = (int)Z.size();
    Z.push_back({"Guardian",         Race::Zerg, UnitSize::Large, 150,  0,  0, 2,  2,  50,  100,33,  20, 0,  8, 1,  2.5f, 10, false});
    int ZDevourerID = (int)Z.size();
    Z.push_back({"Devourer",         Race::Zerg, UnitSize::Large, 250,  0,  0, 2,  2,  150, 50, 33,  0, 25, 6, 1,  3.5f, 9,  false});

    // Zerg buildings
    int ZHatcheryID = (int)Z.size();
    Z.push_back({"Hatchery",         Race::Zerg, UnitSize::Large, 1250, 0,  0, 1,  0, 300, 0,  120, 0,  0,  0, 0,  0,   11, true, false, false});
    int ZLairID = (int)Z.size();
    Z.push_back({"Lair",             Race::Zerg, UnitSize::Large, 1800, 0,  0, 1,  0, 150, 100,80,  0,  0,  0, 0,  0,   11, true, false, false});
    int ZHiveID = (int)Z.size();
    Z.push_back({"Hive",             Race::Zerg, UnitSize::Large, 2500, 0,  0, 1,  0, 200, 150,80,  0,  0,  0, 0,  0,   11, true, false, false});
    int ZExtractorID = (int)Z.size();
    Z.push_back({"Extractor",        Race::Zerg, UnitSize::Large, 750,  0,  0, 1,  0, 50,  0,  40,  0,  0,  0, 0,  0,   11, true});
    int ZSpawningPoolID = (int)Z.size();
    Z.push_back({"Spawning Pool",    Race::Zerg, UnitSize::Large, 750,  0,  0, 1,  0, 200, 0,  60,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZEvolutionChamberID = (int)Z.size();
    Z.push_back({"Evolution Chamber",Race::Zerg, UnitSize::Large, 750,  0,  0, 1,  0, 75,  0,  40,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZHydraliskDenID = (int)Z.size();
    Z.push_back({"Hydralisk Den",    Race::Zerg, UnitSize::Large, 850,  0,  0, 1,  0, 100, 50, 40,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZSpireID = (int)Z.size();
    Z.push_back({"Spire",            Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 200, 150,60,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZNydusCanalID = (int)Z.size();
    Z.push_back({"Nydus Canal",      Race::Zerg, UnitSize::Large, 250,  0,  0, 1,  0, 150, 0,  40,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZQueensNestID = (int)Z.size();
    Z.push_back({"Queen's Nest",     Race::Zerg, UnitSize::Large, 850,  0,  0, 1,  0, 150, 100,50,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZGreaterSpireID = (int)Z.size();
    Z.push_back({"Greater Spire",    Race::Zerg, UnitSize::Large,1000,  0,  0, 1,  0, 100, 150,60,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZUltraliskCavernID = (int)Z.size();
    Z.push_back({"Ultralisk Cavern", Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 150, 200,60,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZDefilerMoundID = (int)Z.size();
    Z.push_back({"Defiler Mound",    Race::Zerg, UnitSize::Large, 850,  0,  0, 1,  0, 100, 100,50,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZCreepColonyID = (int)Z.size();
    Z.push_back({"Creep Colony",     Race::Zerg, UnitSize::Large, 400,  0,  0, 0,  0, 75,  0,  20,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZSporeColonyID = (int)Z.size();
    Z.push_back({"Spore Colony",     Race::Zerg, UnitSize::Large, 400,  0,  0, 0,  0, 50,  0,  20,   0,15,  7, 0,  0,   11, true, false, true});
    int ZSunkenColonyID = (int)Z.size();
    Z.push_back({"Sunken Colony",    Race::Zerg, UnitSize::Large, 300,  0,  0, 2,  0, 50,  0,  20,  40, 0,  7, 0,  0,   11, true, false, true});

    // Assign production & requirements for Zerg
    Z[ZDroneID].ProducedBy = -1;                // Larva -> Egg -> Drone
    Z[ZOverlordID].ProducedBy = -1;             // Larva -> Egg -> Overlord
    Z[ZZerglingID].ProducedBy = -1;             // Larva -> Egg -> Zerglings
    Z[ZHydraliskID].ProducedBy = -1;             // Larva -> Egg -> Hydralisk
    Z[ZMutaliskID].ProducedBy = -1;              // Larva -> Egg -> Mutalisk
    Z[ZScourgeID].ProducedBy = -1;               // Larva -> Egg -> Scourge
    Z[ZQueenID].ProducedBy = -1;                 // Larva -> Egg -> Queen
    Z[ZUltraliskID].ProducedBy = -1;             // Larva -> Egg -> Ultralisk
    Z[ZDefilerID].ProducedBy = -1;               // Larva -> Egg -> Defiler
    Z[ZLurkerID].ProducedBy = ZHydraliskID;     // Hydralisk -> Lurker Egg -> Lurker

    Z[ZDroneID].RequiredBuildings = {ZHatcheryID};
    Z[ZOverlordID].RequiredBuildings = {ZHatcheryID};
    Z[ZZerglingID].RequiredBuildings = {ZSpawningPoolID};
    Z[ZHydraliskID].RequiredBuildings = {ZHydraliskDenID};
    Z[ZMutaliskID].RequiredBuildings = {ZSpireID};
    Z[ZScourgeID].RequiredBuildings = {ZSpireID};
    Z[ZQueenID].RequiredBuildings = {ZQueensNestID};
    Z[ZUltraliskID].RequiredBuildings = {ZUltraliskCavernID};
    Z[ZDefilerID].RequiredBuildings = {ZDefilerMoundID};
    Z[ZLurkerID].RequiredBuildings = {ZHydraliskDenID, ZLairID};

    // Zerg building requirements
    Z[ZLairID].RequiredBuildings = {ZHatcheryID, ZSpawningPoolID};
    // Brood War progression is Lair + Queen's Nest -> Hive. Requiring an
    // Ultralisk Cavern here creates an impossible cycle because that building
    // itself requires Hive.
    Z[ZHiveID].RequiredBuildings = {ZLairID, ZQueensNestID};
    Z[ZSpawningPoolID].RequiredBuildings = {ZHatcheryID};
    Z[ZEvolutionChamberID].RequiredBuildings = {ZHatcheryID};
    Z[ZHydraliskDenID].RequiredBuildings = {ZSpawningPoolID};
    Z[ZSpireID].RequiredBuildings = {ZLairID};
    Z[ZQueensNestID].RequiredBuildings = {ZLairID};
    Z[ZGreaterSpireID].RequiredBuildings = {ZSpireID, ZHiveID};
    Z[ZUltraliskCavernID].RequiredBuildings = {ZHiveID};
    Z[ZDefilerMoundID].RequiredBuildings = {ZHiveID};
    Z[ZCreepColonyID].RequiredBuildings = {ZHatcheryID};
    Z[ZSporeColonyID].RequiredBuildings = {ZEvolutionChamberID};
    Z[ZSunkenColonyID].RequiredBuildings = {ZSpawningPoolID};

        auto& P = ProtossDefs;
    int PProbeID = (int)P.size();
    P.push_back({"Probe",      Race::Protoss, UnitSize::Small,  20, 20, 0, 0,  1,  50,  0,  17,  5,  0,  0, 1,  2.8f, 9,  false});
    int PZealotID = (int)P.size();
    P.push_back({"Zealot",     Race::Protoss, UnitSize::Small, 100, 60, 0, 1,  2, 100,  0,  27,  8,  0,  1, 1,  2.5f, 7,  false});
    int PDragoonID = (int)P.size();
    P.push_back({"Dragoon",    Race::Protoss, UnitSize::Large, 100, 80, 0, 1,  2, 125, 50, 30,  20, 20, 4, 1,  2.4f, 8,  false});
    int PHighTemplarID = (int)P.size();
    P.push_back({"High Templar",Race::Protoss, UnitSize::Small, 40, 40,200, 0,  2,  50, 150,30,  0,  0,  0, 0,  2.0f, 8,  false});
    int PDarkTemplarID = (int)P.size();
    P.push_back({"Dark Templar",Race::Protoss, UnitSize::Small, 80, 40, 0, 1,  2, 125, 100,30,  40,  0,  1, 1,  2.5f, 7,  false});
    int PArchonID = (int)P.size();
    P.push_back({"Archon",     Race::Protoss, UnitSize::Large,  10,350, 0, 0,  4,  0,   0,  1,  30, 30,  2, 1,  2.0f, 8,  false});
    int PDarkArchonID = (int)P.size();
    P.push_back({"Dark Archon",Race::Protoss, UnitSize::Large,  25,200, 0, 1,  4,  0,   0,  1,  0,  0,  0, 0,  2.0f,10,  false});
    int PReaverID = (int)P.size();
    P.push_back({"Reaver",     Race::Protoss, UnitSize::Large, 100, 80, 0, 0,  4, 200, 100,35, 100,  0,  8, 1,  2.0f, 9,  false});
    int PShuttleID = (int)P.size();
    P.push_back({"Shuttle",    Race::Protoss, UnitSize::Large,  80, 60, 0, 1,  2, 200,  0,  30,  0,  0,  0, 0,  3.0f, 8,  false});
    int PScoutID = (int)P.size();
    P.push_back({"Scout",      Race::Protoss, UnitSize::Large, 150,100, 0, 0,  3, 275, 125,40,  8,  14, 4, 1,  3.5f, 8,  false});
    int PCorsairID = (int)P.size();
    P.push_back({"Corsair",    Race::Protoss, UnitSize::Medium,100, 80, 0, 1,  2, 150, 100,30,  0,   5, 5, 1,  3.5f, 9,  false});
    int PCarrierID = (int)P.size();
    P.push_back({"Carrier",    Race::Protoss, UnitSize::Large, 300,150, 0, 4,  6, 350, 250,60,  6,   6, 6, 1,  2.5f,11, false});
    int PArbiterID = (int)P.size();
    P.push_back({"Arbiter",    Race::Protoss, UnitSize::Large, 200,150, 0, 1,  4, 100, 350,50,  10, 10, 5, 1,  2.5f, 9, false});
    int PObserverID = (int)P.size();
    P.push_back({"Observer",   Race::Protoss, UnitSize::Small,  40, 20, 0, 0,  1,  25,  75, 20,  0,  0,  0, 0,  2.5f, 9,  false});

    // Protoss buildings
    int PNexusID = (int)P.size();
    P.push_back({"Nexus",            Race::Protoss, UnitSize::Large, 750, 750,0, 1,  0, 400, 0,  120, 0,  0,  0, 0,  0,   11, true, false, false, false});
    int PPylonID = (int)P.size();
    P.push_back({"Pylon",            Race::Protoss, UnitSize::Large, 300, 300, 0, 0,  0, 100, 0,  30,  0,  0,  0, 0,  0,   11, true, false, false, false});
    int PAssimilatorID = (int)P.size();
    P.push_back({"Assimilator",      Race::Protoss, UnitSize::Large, 450, 450, 0, 1,  0, 100, 0,  40,  0,  0,  0, 0,  0,   11, true});
    int PGatewayID = (int)P.size();
    P.push_back({"Gateway",          Race::Protoss, UnitSize::Large, 500, 500, 0, 1,  0, 150, 0,  60,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PForgeID = (int)P.size();
    P.push_back({"Forge",            Race::Protoss, UnitSize::Large, 550, 550, 0, 1,  0, 150, 0,  30,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PPhotonCannonID = (int)P.size();
    P.push_back({"Photon Cannon",    Race::Protoss, UnitSize::Large, 100, 100, 0, 0,  0, 150, 0,  30,  20, 20, 7, 1,  0,   11, true, false, false, true});
    int PCyberneticsCoreID = (int)P.size();
    P.push_back({"Cybernetics Core", Race::Protoss, UnitSize::Large, 500, 500, 0, 1,  0, 200, 0,  40,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PShieldBatteryID = (int)P.size();
    P.push_back({"Shield Battery",   Race::Protoss, UnitSize::Large, 200, 200, 0, 1,  0, 100, 0,  20,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PRoboticsFacilityID = (int)P.size();
    P.push_back({"Robotics Facility",Race::Protoss, UnitSize::Large, 500, 500, 0, 1,  0, 200, 200,40,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PStargateID = (int)P.size();
    P.push_back({"Stargate",         Race::Protoss, UnitSize::Large, 600, 600, 0, 1,  0, 150, 150,50,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PCitadelOfAdunID = (int)P.size();
    P.push_back({"Citadel of Adun",  Race::Protoss, UnitSize::Large, 450, 450, 0, 1,  0, 150, 100,50,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PTemplarArchivesID = (int)P.size();
    P.push_back({"Templar Archives", Race::Protoss, UnitSize::Large, 500, 500, 0, 1,  0, 150, 200,40,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PRoboticsSupportBayID = (int)P.size();
    P.push_back({"Robotics Support Bay",Race::Protoss, UnitSize::Large, 450, 450, 0, 1,  0, 150, 100,30,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PFleetBeaconID = (int)P.size();
    P.push_back({"Fleet Beacon",     Race::Protoss, UnitSize::Large, 500, 500, 0, 1,  0, 300, 200,40,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PArbiterTribunalID = (int)P.size();
    P.push_back({"Arbiter Tribunal", Race::Protoss, UnitSize::Large, 500, 500, 0, 1,  0, 200, 150,50,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PObservatoryID = (int)P.size();
    P.push_back({"Observatory",      Race::Protoss, UnitSize::Large, 250, 250, 0, 1,  0, 50,  100,30,  0,  0,  0, 0,  0,   11, true, false, false, true});

    // Protoss production & requirements
    P[PProbeID].ProducedBy = PNexusID;
    P[PZealotID].ProducedBy = PGatewayID;
    P[PDragoonID].ProducedBy = PGatewayID;
    P[PHighTemplarID].ProducedBy = PGatewayID;
    P[PDarkTemplarID].ProducedBy = PGatewayID;
    P[PReaverID].ProducedBy = PRoboticsFacilityID;
    P[PShuttleID].ProducedBy = PRoboticsFacilityID;
    P[PScoutID].ProducedBy = PStargateID;
    P[PCorsairID].ProducedBy = PStargateID;
    P[PCarrierID].ProducedBy = PStargateID;
    P[PArbiterID].ProducedBy = PStargateID;
    P[PArbiterID].ProducedBy = PStargateID;
    P[PObserverID].ProducedBy = PRoboticsFacilityID;

    P[PZealotID].RequiredBuildings = {PGatewayID};
    P[PDragoonID].RequiredBuildings = {PGatewayID, PCyberneticsCoreID};
    P[PHighTemplarID].RequiredBuildings = {PGatewayID, PCitadelOfAdunID, PTemplarArchivesID};
    P[PDarkTemplarID].RequiredBuildings = {PGatewayID, PTemplarArchivesID};
    P[PReaverID].RequiredBuildings = {PRoboticsFacilityID, PRoboticsSupportBayID};
    P[PShuttleID].RequiredBuildings = {PRoboticsFacilityID};
    P[PScoutID].RequiredBuildings = {PStargateID, PCyberneticsCoreID};
    P[PCorsairID].RequiredBuildings = {PStargateID};
    P[PCarrierID].RequiredBuildings = {PStargateID, PFleetBeaconID};
    P[PArbiterID].RequiredBuildings = {PStargateID, PArbiterTribunalID};
    P[PObserverID].RequiredBuildings = {PRoboticsFacilityID, PObservatoryID};

    // Protoss building requirements
    P[PGatewayID].RequiredBuildings = {PNexusID};
    P[PForgeID].RequiredBuildings = {PNexusID};
    P[PCyberneticsCoreID].RequiredBuildings = {PGatewayID};
    P[PShieldBatteryID].RequiredBuildings = {PGatewayID};
    P[PRoboticsFacilityID].RequiredBuildings = {PCyberneticsCoreID};
    P[PStargateID].RequiredBuildings = {PCyberneticsCoreID};
    P[PCitadelOfAdunID].RequiredBuildings = {PCyberneticsCoreID};
    P[PTemplarArchivesID].RequiredBuildings = {PCitadelOfAdunID};
    P[PRoboticsSupportBayID].RequiredBuildings = {PRoboticsFacilityID};
    P[PFleetBeaconID].RequiredBuildings = {PStargateID};
    P[PArbiterTribunalID].RequiredBuildings = {PTemplarArchivesID, PStargateID};
    P[PObservatoryID].RequiredBuildings = {PRoboticsFacilityID};

    Z[LarvaID].ChkUnitID = 35;
    Z[ZDroneID].ChkUnitID = 41;
    Z[ZOverlordID].ChkUnitID = 42;
    Z[ZZerglingID].ChkUnitID = 37;
    Z[ZHydraliskID].ChkUnitID = 38;
    Z[ZMutaliskID].ChkUnitID = 43;
    Z[ZScourgeID].ChkUnitID = 47;
    Z[ZQueenID].ChkUnitID = 45;
    Z[ZUltraliskID].ChkUnitID = 39;
    Z[ZDefilerID].ChkUnitID = 46;
    Z[ZLurkerID].ChkUnitID = 103;
    Z[ZBroodlingID].ChkUnitID = 40;
    Z[ZInfestedID].ChkUnitID = 50;
    Z[ZGuardianID].ChkUnitID = 44;
    Z[ZDevourerID].ChkUnitID = 62;
    Z[ZHatcheryID].ChkUnitID = 131;
    Z[ZLairID].ChkUnitID = 132;
    Z[ZHiveID].ChkUnitID = 133;
    Z[ZExtractorID].ChkUnitID = 149;
    Z[ZSpawningPoolID].ChkUnitID = 142;
    Z[ZEvolutionChamberID].ChkUnitID = 139;
    Z[ZHydraliskDenID].ChkUnitID = 135;
    Z[ZSpireID].ChkUnitID = 141;
    Z[ZNydusCanalID].ChkUnitID = 134;
    Z[ZQueensNestID].ChkUnitID = 138;
    Z[ZGreaterSpireID].ChkUnitID = 137;
    Z[ZUltraliskCavernID].ChkUnitID = 140;
    Z[ZDefilerMoundID].ChkUnitID = 136;
    Z[ZCreepColonyID].ChkUnitID = 143;
    Z[ZSporeColonyID].ChkUnitID = 144;
    Z[ZSunkenColonyID].ChkUnitID = 146;

    P[PProbeID].ChkUnitID = 64;
    P[PZealotID].ChkUnitID = 65;
    P[PDragoonID].ChkUnitID = 66;
    P[PHighTemplarID].ChkUnitID = 67;
    P[PDarkTemplarID].ChkUnitID = 61;
    P[PArchonID].ChkUnitID = 68;
    P[PDarkArchonID].ChkUnitID = 63;
    P[PReaverID].ChkUnitID = 83;
    P[PShuttleID].ChkUnitID = 69;
    P[PScoutID].ChkUnitID = 70;
    P[PCorsairID].ChkUnitID = 60;
    P[PCarrierID].ChkUnitID = 72;
    P[PArbiterID].ChkUnitID = 71;
    P[PObserverID].ChkUnitID = 84;
    P[PNexusID].ChkUnitID = 154;
    P[PPylonID].ChkUnitID = 156;
    P[PAssimilatorID].ChkUnitID = 157;
    P[PGatewayID].ChkUnitID = 160;
    P[PForgeID].ChkUnitID = 166;
    P[PPhotonCannonID].ChkUnitID = 162;
    P[PCyberneticsCoreID].ChkUnitID = 164;
    P[PShieldBatteryID].ChkUnitID = 172;
    P[PRoboticsFacilityID].ChkUnitID = 155;
    P[PStargateID].ChkUnitID = 167;
    P[PCitadelOfAdunID].ChkUnitID = 163;
    P[PTemplarArchivesID].ChkUnitID = 165;
    P[PRoboticsSupportBayID].ChkUnitID = 171;
    P[PFleetBeaconID].ChkUnitID = 169;
    P[PArbiterTribunalID].ChkUnitID = 170;
    P[PObservatoryID].ChkUnitID = 159;

    // ---- Hero & Special Units (data-driven alignment with openbw-ref) ----
    const int TerranHeroBase = (int)T.size();
    T.push_back({"JimRaynorMarine",    Race::Terran, UnitSize::Small, 100,  0,  0, 3,  1,  0,   0,  24, 12,  0, 5, 1, 2.6f, 7,  false});
    T.push_back({"JimRaynorVulture",   Race::Terran, UnitSize::Medium,300,  0,  0, 1,  2,  0,   0,  30, 30,  0, 5, 1, 3.2f, 8,  false});
    T.push_back({"SarahKerrigan",      Race::Terran, UnitSize::Small, 250,  0,50, 1,  1,  0,   0,  30, 30,  0, 6, 1, 2.6f, 9,  false});
    T.push_back({"EdmundDukeSiege",    Race::Terran, UnitSize::Large, 400,  0,  0, 2,  2,  0,   0,  45, 70,  0, 7, 1, 2.3f, 10, false});
    T.push_back({"GuiMontag",          Race::Terran, UnitSize::Small, 160,  0,  0, 1,  1,  0,   0,  24, 15,  0, 2, 1, 2.6f, 7,  false});
    T.push_back({"ArcturusMengsk",     Race::Terran, UnitSize::Large, 500,  0,200, 3,  6,  0,   0,  90, 50, 50, 6, 1, 2.2f, 11, false});
    T.push_back({"Hyperion",           Race::Terran, UnitSize::Large, 850,  0,200, 4,  6,  0,   0,  90, 50, 50, 6, 1, 2.1f, 11, false});
    T.push_back({"NoradII",            Race::Terran, UnitSize::Large, 700,  0,200, 4,  6,  0,   0,  90, 50, 50, 6, 1, 2.1f, 11, false});
    T[TerranHeroBase + 0].ChkUnitID = 20;
    T[TerranHeroBase + 1].ChkUnitID = 19;
    T[TerranHeroBase + 2].ChkUnitID = 16;
    T[TerranHeroBase + 3].ChkUnitID = 25;
    T[TerranHeroBase + 4].ChkUnitID = 10;
    T[TerranHeroBase + 5].ChkUnitID = 27;
    T[TerranHeroBase + 6].ChkUnitID = 28;
    T[TerranHeroBase + 7].ChkUnitID = 29;

    const int ZergHeroBase = (int)Z.size();
    Z.push_back({"Torrasque",          Race::Zerg,   UnitSize::Large, 800,  0,  0, 4,  6,  0,   0,  50, 50,  0, 2, 1, 2.8f, 10, false});
    Z.push_back({"InfestedKerrigan",   Race::Zerg,   UnitSize::Small, 300,  0,50, 2,  1,  0,   0,  30, 50,  0, 5, 1, 2.6f, 9,  false});
    Z.push_back({"HunterKiller",       Race::Zerg,   UnitSize::Medium,250,  0,  0, 2,  2,  0,   0,  30, 25, 25, 5, 1, 2.8f, 10, false});
    Z.push_back({"DevouringOne",       Race::Zerg,   UnitSize::Small, 200,  0,  0, 1,  1,  0,   0,  20, 20,  0, 2, 1, 2.6f, 8,  false});
    Z[ZergHeroBase + 0].ChkUnitID = 48;
    Z[ZergHeroBase + 1].ChkUnitID = 51;
    Z[ZergHeroBase + 2].ChkUnitID = 53;
    Z[ZergHeroBase + 3].ChkUnitID = 54;

    const int ProtossHeroBase = (int)P.size();
    P.push_back({"FenixZealot",        Race::Protoss, UnitSize::Medium,240, 50,  0, 1,  2,  0,   0,  40, 20,  0, 1, 0, 2.8f, 8,  false});
    P.push_back({"FenixDragoon",       Race::Protoss, UnitSize::Large, 300,100,  0, 2,  2,  0,   0,  50, 35, 35, 5, 0, 2.5f, 9,  false});
    P.push_back({"Zeratul",            Race::Protoss, UnitSize::Small, 200, 80,  0, 1,  1,  0,   0,  30, 50,  0, 1, 0, 2.8f, 9,  false});
    P.push_back({"Tassadar",           Race::Protoss, UnitSize::Small, 200, 80,100, 1,  2,  0,   0,  40, 30,  0, 0, 0, 2.5f, 10, false});
    P.push_back({"Warbringer",         Race::Protoss, UnitSize::Large, 300,150,  0, 2,  4,  0,   0,  60, 60,  0, 1, 0, 2.5f, 9,  false});
    P.push_back({"Gantrithor",         Race::Protoss, UnitSize::Large, 500,200,  0, 3,  6,  0,   0,  90, 50, 50, 6, 0, 2.2f, 11, false});
    P.push_back({"Danimoth",           Race::Protoss, UnitSize::Large, 400,150,  0, 2,  4,  0,   0,  90, 30, 30, 5, 0, 2.5f, 10, false});
    P.push_back({"Aldaris",            Race::Protoss, UnitSize::Large, 500,150,100, 2,  4,  0,   0,  90, 40, 40, 6, 0, 2.2f, 11, false});
    P[ProtossHeroBase + 0].ChkUnitID = 77;
    P[ProtossHeroBase + 1].ChkUnitID = 78;
    P[ProtossHeroBase + 2].ChkUnitID = 75;
    P[ProtossHeroBase + 3].ChkUnitID = 79;
    P[ProtossHeroBase + 4].ChkUnitID = 81;
    P[ProtossHeroBase + 5].ChkUnitID = 82;
    P[ProtossHeroBase + 6].ChkUnitID = 86;
    P[ProtossHeroBase + 7].ChkUnitID = 87;

    // Critters
    const int CritterBase = (int)T.size();
    T.push_back({"Rhynadon",           Race::Terran,  UnitSize::Large,  60,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Bengalaas",          Race::Terran,  UnitSize::Large,  60,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Scantid",            Race::Terran,  UnitSize::Small,  40,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Kakaru",             Race::Terran,  UnitSize::Small,  40,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Ragnasaur",          Race::Terran,  UnitSize::Large,  80,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Ursadon",            Race::Terran,  UnitSize::Large,  80,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T[CritterBase + 0].ChkUnitID = 89;
    T[CritterBase + 1].ChkUnitID = 90;
    T[CritterBase + 2].ChkUnitID = 93;
    T[CritterBase + 3].ChkUnitID = 94;
    T[CritterBase + 4].ChkUnitID = 95;
    T[CritterBase + 5].ChkUnitID = 96;
    for (int I = 0; I < (int)T.size(); ++I)
        NameToID[T[I].Name] = I;
    for (int I = 0; I < (int)ZergDefs.size(); ++I)
        NameToID[ZergDefs[I].Name] = I + (int)T.size();
    for (int I = 0; I < (int)ProtossDefs.size(); ++I)
        NameToID[ProtossDefs[I].Name] = I + (int)T.size() + (int)ZergDefs.size();

    ApplyDefaultWeaponMetadata(T);
    ApplyDefaultWeaponMetadata(ZergDefs);
    ApplyDefaultWeaponMetadata(ProtossDefs);

    auto SetGroundWeapon = [](UnitDef& Def,
                              int Damage,
                              int DamageBonus,
                              int HitCount,
                              int CooldownFrames,
                              Components::DamageType DamageType)
    {
        Def.GroundDamage = Damage;
        Def.GroundDamageBonus = DamageBonus;
        Def.GroundHitCount = HitCount;
        Def.GroundAttackCooldown = (float)CooldownFrames / 24.0f;
        Def.AttackCooldown = Def.GroundAttackCooldown;
        Def.GroundDmgType = DamageType;
    };
    auto SetAirWeapon = [](UnitDef& Def,
                           int Damage,
                           int DamageBonus,
                           int HitCount,
                           int CooldownFrames,
                           Components::DamageType DamageType)
    {
        Def.AirDamage = Damage;
        Def.AirDamageBonus = DamageBonus;
        Def.AirHitCount = HitCount;
        Def.AirAttackCooldown = (float)CooldownFrames / 24.0f;
        if (Def.GroundDamage <= 0)
            Def.AttackCooldown = Def.AirAttackCooldown;
        Def.AirDmgType = DamageType;
    };
    auto SetSplash = [](UnitDef& Def,
                        Components::SplashType Splash,
                        int InnerPixels,
                        int MediumPixels,
                        int OuterPixels)
    {
        Def.Splash = Splash;
        Def.SplashInnerRadius = (float)InnerPixels / 32.0f;
        Def.SplashMediumRadius = (float)MediumPixels / 32.0f;
        Def.SplashRadius = (float)OuterPixels / 32.0f;
    };

    // Terran weapons.dat values.  Damage and bonuses are per projectile/strike;
    // multi-projectile attacks carry their multiplier in the hit-count fields.
    SetGroundWeapon(T[0], 5, 0, 1, 15, Components::DamageType::Normal); // SCV
    T[0].AttackRange = 10.0f / 32.0f;
    SetGroundWeapon(T[1], 6, 1, 1, 15, Components::DamageType::Normal); // Marine
    SetAirWeapon(T[1], 6, 1, 1, 15, Components::DamageType::Normal);
    T[1].AttackRange = 4.0f;
    SetGroundWeapon(T[2], 8, 1, 2, 22, Components::DamageType::Concussive); // Firebat
    T[2].AttackRange = 1.0f;
    // Flame splash is delivered through the bullet system, but the weapon is
    // still a close-range attack and must work under Dark Swarm.
    T[2].WeaponBeh = Components::WeaponBehavior::Melee;
    SetSplash(T[2], Components::SplashType::Line, 15, 20, 25);
    SetGroundWeapon(T[4], 10, 1, 1, 22, Components::DamageType::Concussive); // Ghost
    SetAirWeapon(T[4], 10, 1, 1, 22, Components::DamageType::Concussive);
    T[4].AttackRange = 7.0f;
    SetGroundWeapon(T[5], 20, 2, 1, 30, Components::DamageType::Concussive); // Vulture
    T[5].AttackRange = 5.0f;
    SetGroundWeapon(T[6], 12, 1, 1, 22, Components::DamageType::Normal); // Goliath
    SetAirWeapon(T[6], 10, 2, 2, 22, Components::DamageType::Explosive);
    T[6].AttackRange = 6.0f;
    T[6].GroundAttackRange = 6.0f;
    T[6].AirAttackRange = 5.0f;
    SetGroundWeapon(T[7], 30, 3, 1, 37, Components::DamageType::Explosive); // Tank mode
    T[7].AttackRange = 7.0f;
    SetGroundWeapon(T[8], 8, 1, 1, 30, Components::DamageType::Normal); // Wraith
    SetAirWeapon(T[8], 20, 2, 1, 22, Components::DamageType::Explosive);
    T[8].AttackRange = 5.0f;
    SetGroundWeapon(T[11], 25, 3, 1, 30, Components::DamageType::Normal); // Battlecruiser
    SetAirWeapon(T[11], 25, 3, 1, 30, Components::DamageType::Normal);
    T[11].AttackRange = 6.0f;
    SetAirWeapon(T[12], 6, 1, 8, 64, Components::DamageType::Explosive); // Valkyrie
    T[12].AttackRange = 6.0f;
    SetSplash(T[12], Components::SplashType::Air, 5, 50, 100);
    SetAirWeapon(T[(size_t)MissileTurretID], 20, 0, 1, 15,
                 Components::DamageType::Explosive);
    T[(size_t)MissileTurretID].AttackRange = 7.0f;

    // Zerg weapons.dat values.
    SetGroundWeapon(Z[(size_t)ZDroneID], 5, 0, 1, 22, Components::DamageType::Normal);
    Z[(size_t)ZDroneID].AttackRange = 1.0f;
    SetGroundWeapon(Z[(size_t)ZZerglingID], 5, 1, 1, 8, Components::DamageType::Normal);
    Z[(size_t)ZZerglingID].AttackRange = 15.0f / 32.0f;
    SetGroundWeapon(Z[(size_t)ZHydraliskID], 10, 1, 1, 15, Components::DamageType::Explosive);
    SetAirWeapon(Z[(size_t)ZHydraliskID], 10, 1, 1, 15, Components::DamageType::Explosive);
    Z[(size_t)ZHydraliskID].AttackRange = 4.0f;
    SetGroundWeapon(Z[(size_t)ZMutaliskID], 9, 1, 1, 30, Components::DamageType::Normal);
    SetAirWeapon(Z[(size_t)ZMutaliskID], 9, 1, 1, 30, Components::DamageType::Normal);
    Z[(size_t)ZMutaliskID].AttackRange = 3.0f;
    Z[(size_t)ZMutaliskID].WeaponBeh = Components::WeaponBehavior::Bounce;
    SetAirWeapon(Z[(size_t)ZScourgeID], 110, 0, 1, 1, Components::DamageType::Normal);
    Z[(size_t)ZScourgeID].AttackRange = 3.0f / 32.0f;
    Z[(size_t)ZScourgeID].SelfDestructOnAttack = true;
    SetGroundWeapon(Z[(size_t)ZUltraliskID], 20, 3, 1, 15, Components::DamageType::Normal);
    Z[(size_t)ZUltraliskID].AttackRange = 25.0f / 32.0f;
    SetGroundWeapon(Z[(size_t)ZLurkerID], 20, 2, 1, 37, Components::DamageType::Normal);
    Z[(size_t)ZLurkerID].AttackRange = 6.0f;
    Z[(size_t)ZLurkerID].WeaponBeh = Components::WeaponBehavior::AppearOnTarget;
    SetSplash(Z[(size_t)ZLurkerID], Components::SplashType::Line, 20, 20, 20);
    SetGroundWeapon(Z[(size_t)ZBroodlingID], 4, 1, 1, 15, Components::DamageType::Normal);
    Z[(size_t)ZBroodlingID].AttackRange = 2.0f / 32.0f;
    SetGroundWeapon(Z[(size_t)ZInfestedID], 500, 0, 1, 1, Components::DamageType::Explosive);
    Z[(size_t)ZInfestedID].AttackRange = 3.0f / 32.0f;
    Z[(size_t)ZInfestedID].WeaponBeh = Components::WeaponBehavior::Melee;
    Z[(size_t)ZInfestedID].SelfDestructOnAttack = true;
    SetSplash(Z[(size_t)ZInfestedID], Components::SplashType::Radial, 20, 40, 60);
    SetGroundWeapon(Z[(size_t)ZGuardianID], 20, 2, 1, 30, Components::DamageType::Normal);
    Z[(size_t)ZGuardianID].AttackRange = 8.0f;
    SetAirWeapon(Z[(size_t)ZDevourerID], 25, 2, 1, 100, Components::DamageType::Explosive);
    Z[(size_t)ZDevourerID].AttackRange = 6.0f;
    SetAirWeapon(Z[(size_t)ZSporeColonyID], 15, 0, 1, 15, Components::DamageType::Normal);
    Z[(size_t)ZSporeColonyID].AttackRange = 7.0f;
    SetGroundWeapon(Z[(size_t)ZSunkenColonyID], 40, 0, 1, 32,
                    Components::DamageType::Explosive);
    Z[(size_t)ZSunkenColonyID].AttackRange = 7.0f;

    // Protoss weapons.dat values.  Reavers model the launched Scarab's damage;
    // carriers keep one modeled interceptor strike while retaining both target
    // channels until fighter entities are represented explicitly.
    SetGroundWeapon(P[(size_t)PProbeID], 5, 0, 1, 22, Components::DamageType::Normal);
    P[(size_t)PProbeID].AttackRange = 1.0f;
    SetGroundWeapon(P[(size_t)PZealotID], 8, 1, 2, 22, Components::DamageType::Normal);
    P[(size_t)PZealotID].AttackRange = 15.0f / 32.0f;
    SetGroundWeapon(P[(size_t)PDragoonID], 20, 2, 1, 30, Components::DamageType::Explosive);
    SetAirWeapon(P[(size_t)PDragoonID], 20, 2, 1, 30, Components::DamageType::Explosive);
    P[(size_t)PDragoonID].AttackRange = 4.0f;
    SetGroundWeapon(P[(size_t)PDarkTemplarID], 40, 3, 1, 30, Components::DamageType::Normal);
    P[(size_t)PDarkTemplarID].AttackRange = 15.0f / 32.0f;
    SetGroundWeapon(P[(size_t)PArchonID], 30, 3, 1, 20, Components::DamageType::Normal);
    SetAirWeapon(P[(size_t)PArchonID], 30, 3, 1, 20, Components::DamageType::Normal);
    P[(size_t)PArchonID].AttackRange = 2.0f;
    P[(size_t)PArchonID].WeaponBeh = Components::WeaponBehavior::AppearOnTarget;
    SetSplash(P[(size_t)PArchonID], Components::SplashType::EnemyRadial, 3, 15, 30);
    SetGroundWeapon(P[(size_t)PReaverID], 100, 25, 1, 60, Components::DamageType::Normal);
    P[(size_t)PReaverID].AttackRange = 8.0f;
    P[(size_t)PReaverID].WeaponBeh = Components::WeaponBehavior::Lob;
    SetSplash(P[(size_t)PReaverID], Components::SplashType::Radial, 20, 40, 60);
    SetGroundWeapon(P[(size_t)PScoutID], 8, 1, 1, 30, Components::DamageType::Normal);
    SetAirWeapon(P[(size_t)PScoutID], 14, 1, 2, 22, Components::DamageType::Explosive);
    P[(size_t)PScoutID].AttackRange = 4.0f;
    SetAirWeapon(P[(size_t)PCorsairID], 5, 1, 1, 8, Components::DamageType::Explosive);
    P[(size_t)PCorsairID].AttackRange = 5.0f;
    P[(size_t)PCorsairID].WeaponBeh = Components::WeaponBehavior::AppearOnTarget;
    SetSplash(P[(size_t)PCorsairID], Components::SplashType::Air, 5, 50, 100);
    SetGroundWeapon(P[(size_t)PCarrierID], 6, 1, 1, 24, Components::DamageType::Normal);
    SetAirWeapon(P[(size_t)PCarrierID], 6, 1, 1, 24, Components::DamageType::Normal);
    P[(size_t)PCarrierID].AttackRange = 8.0f;
    SetGroundWeapon(P[(size_t)PArbiterID], 10, 1, 1, 45, Components::DamageType::Explosive);
    SetAirWeapon(P[(size_t)PArbiterID], 10, 1, 1, 45, Components::DamageType::Explosive);
    P[(size_t)PArbiterID].AttackRange = 5.0f;
    SetGroundWeapon(P[(size_t)PPhotonCannonID], 20, 0, 1, 22,
                    Components::DamageType::Normal);
    SetAirWeapon(P[(size_t)PPhotonCannonID], 20, 0, 1, 22,
                 Components::DamageType::Normal);
    P[(size_t)PPhotonCannonID].AttackRange = 7.0f;
}

const std::vector<UnitDef>& GetTerranDefs()  { InitDefs(); return TerranDefs; }
const std::vector<UnitDef>& GetZergDefs()    { InitDefs(); return ZergDefs; }
const std::vector<UnitDef>& GetProtossDefs() { InitDefs(); return ProtossDefs; }

UnitDef GetUnitDefByName(const std::string& Name)
{
    InitDefs();
    auto It = NameToID.find(Name);
    assert(It != NameToID.end() && "UnitDef not found");
    int ID = It->second;
    if (ID < (int)TerranDefs.size()) return TerranDefs[ID];
    ID -= (int)TerranDefs.size();
    if (ID < (int)ZergDefs.size()) return ZergDefs[ID];
    ID -= (int)ZergDefs.size();
    return ProtossDefs[ID];
}

UnitDef GetUnitDefByID(int ID)
{
    InitDefs();
    int TerranCount = (int)TerranDefs.size();
    int ZergCount = (int)ZergDefs.size();
    int ProtossCount = (int)ProtossDefs.size();
    if (ID >= 0 && ID < TerranCount) return TerranDefs[ID];
    ID -= TerranCount;
    if (ID >= 0 && ID < ZergCount) return ZergDefs[ID];
    ID -= ZergCount;
    if (ID >= 0 && ID < ProtossCount) return ProtossDefs[ID];
    UnitDef Empty;
    Empty.Name = "Unknown";
    return Empty;
}

int GetTerranDefIDByChkUnitID(int ChkUnitID)
{
    InitDefs();
    for (int I = 0; I < (int)TerranDefs.size(); ++I)
    {
        if (TerranDefs[I].ChkUnitID == ChkUnitID)
            return I;
    }
    return -1;
}

int GetUnitDefIDByChkUnitID(int ChkUnitID)
{
    InitDefs();
    for (int I = 0; I < (int)TerranDefs.size(); ++I)
    {
        if (TerranDefs[I].ChkUnitID == ChkUnitID)
            return I;
    }
    const int ZergOffset = (int)TerranDefs.size();
    for (int I = 0; I < (int)ZergDefs.size(); ++I)
    {
        if (ZergDefs[I].ChkUnitID == ChkUnitID)
            return ZergOffset + I;
    }
    const int ProtossOffset = ZergOffset + (int)ZergDefs.size();
    for (int I = 0; I < (int)ProtossDefs.size(); ++I)
    {
        if (ProtossDefs[I].ChkUnitID == ChkUnitID)
            return ProtossOffset + I;
    }
    return -1;
}

Race GetRaceByChkUnitID(int ChkUnitID, Race Fallback)
{
    const int DefID = GetUnitDefIDByChkUnitID(ChkUnitID);
    if (DefID >= 0)
        return GetUnitDefByID(DefID).RaceType;

    if ((ChkUnitID >= 35 && ChkUnitID <= 57) ||
        ChkUnitID == 59 ||
        ChkUnitID == 62 ||
        ChkUnitID == 97 ||
        ChkUnitID == 103 ||
        ChkUnitID == 104 ||
        (ChkUnitID >= 130 && ChkUnitID <= 152))
    {
        return Race::Zerg;
    }

    if (ChkUnitID == 60 ||
        ChkUnitID == 61 ||
        ChkUnitID == 63 ||
        (ChkUnitID >= 64 && ChkUnitID <= 88) ||
        ChkUnitID == 98 ||
        ChkUnitID == 105 ||
        (ChkUnitID >= 154 && ChkUnitID <= 175))
    {
        return Race::Protoss;
    }

    if ((ChkUnitID >= 0 && ChkUnitID <= 34) ||
        ChkUnitID == 58 ||
        ChkUnitID == 99 ||
        ChkUnitID == 100 ||
        ChkUnitID == 102 ||
        (ChkUnitID >= 106 && ChkUnitID <= 127))
    {
        return Race::Terran;
    }

    return Fallback;
}

int GetSupplyProvidedByDefID(int ID)
{
    const UnitDef Def = GetUnitDefByID(ID);
    if (Def.Name == "Unknown")
        return 0;
    if (Def.Name == "Overlord" || Def.Name == "SupplyDepot" ||
        Def.Name == "Pylon")
        return 8;
    if (Def.Name == "CommandCenter" || Def.Name == "Nexus" ||
        Def.Name == "Hatchery" || Def.Name == "Lair" || Def.Name == "Hive")
    {
        return 10;
    }
    return 0;
}

bool IsDetectorChkUnitID(int ChkUnitID)
{
    switch (ChkUnitID)
    {
        case 33:  // Spell Scanner Sweep
        case 9:   // Terran Science Vessel
        case 124: // Terran Missile Turret
        case 42:  // Zerg Overlord
        case 139: // Zerg Spore Colony
        case 84:  // Protoss Observer
        case 162: // Protoss Photon Cannon
            return true;
        default:
            return false;
    }
}

bool BuildingDefSatisfiesRequirement(int ActualDefID, int RequiredDefID)
{
    if (ActualDefID == RequiredDefID)
        return true;

    const auto Actual = GetUnitDefByID(ActualDefID);
    const auto Required = GetUnitDefByID(RequiredDefID);
    if (Actual.RaceType != Race::Zerg || Required.RaceType != Race::Zerg)
        return false;

    if (Required.Name == "Hatchery")
        return Actual.Name == "Lair" || Actual.Name == "Hive";
    if (Required.Name == "Lair")
        return Actual.Name == "Hive";
    if (Required.Name == "Spire")
        return Actual.Name == "Greater Spire";
    return false;
}

int GetUnitDefID(const std::string& Name)
{
    InitDefs();
    auto It = NameToID.find(Name);
    if (It != NameToID.end()) return It->second;
    return -1;
}

} // namespace Game::Races
