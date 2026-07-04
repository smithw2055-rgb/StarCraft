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
    T.push_back({"Marine",          Race::Terran, UnitSize::Small,  40,  0,  0, 0,  1,  50,   0,  24,  6,  0, 4, 1, 2.5f, 7,  false});
    T.push_back({"Firebat",         Race::Terran, UnitSize::Small,  50,  0,  0, 1,  1,  50,  25,  24,  8,  0, 2, 1, 2.5f, 7,  false});
    T.push_back({"Medic",           Race::Terran, UnitSize::Small,  60,  0, 50, 1,  1,  50,  25,  24,  0,  0, 0, 0, 2.5f, 8,  false});
    T.push_back({"Ghost",           Race::Terran, UnitSize::Small,  45,  0, 50, 0,  1,  25,  75,  30, 10,  0, 6, 1, 2.5f, 9,  false});
    T.push_back({"Vulture",         Race::Terran, UnitSize::Medium, 80,  0,  0, 0,  2,  75,   0,  30, 20,  0, 5, 1, 3.0f, 8,  false});
    T.push_back({"Goliath",         Race::Terran, UnitSize::Large, 125,  0,  0, 1,  2, 100,  50,  37, 12, 10, 6, 1, 2.6f, 8,  false});
    T.push_back({"SiegeTank",       Race::Terran, UnitSize::Large, 150,  0,  0, 1,  2, 150, 100,  45, 30,  0, 7, 1, 2.3f, 10, false});
    T.push_back({"Wraith",          Race::Terran, UnitSize::Medium,120,  0,  0, 0,  2, 150, 100,  60,  8,  8, 5, 1, 3.2f, 9,  false});
    T.push_back({"Dropship",        Race::Terran, UnitSize::Large, 150,  0,  0, 1,  2, 100, 100,  50,  0,  0, 0, 0, 3.0f, 8,  false});
    T.push_back({"ScienceVessel",   Race::Terran, UnitSize::Large, 200,  0,200, 1,  2, 100, 225,  80,  0,  0, 0, 0, 2.8f, 11, false});
    T.push_back({"Battlecruiser",   Race::Terran, UnitSize::Large, 500,  0,200, 3,  6, 400, 300,  90, 25, 25, 6, 1, 2.1f, 11, false});
    T.push_back({"Valkyrie",        Race::Terran, UnitSize::Large, 200,  0,  0, 2,  3, 250, 125,  60,  6, 16, 6, 1, 3.0f, 9,  false});

    int CommandCenterID = (int)T.size();
    T.push_back({"CommandCenter",   Race::Terran, UnitSize::Large, 1500, 0,  0, 1, 10, 400,   0, 120,  0,  0, 0, 0, 0.0f, 11, true, true});
    int ComsatStationID = (int)T.size();
    T.push_back({"ComsatStation",   Race::Terran, UnitSize::Large,  500, 0,200, 1,  0,  50,  50,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int NuclearSiloID = (int)T.size();
    T.push_back({"NuclearSilo",     Race::Terran, UnitSize::Large,  600, 0,  0, 1,  0, 100, 100,  60,  0,  0, 0, 0, 0.0f, 11, true});
    int SupplyDepotID = (int)T.size();
    T.push_back({"SupplyDepot",     Race::Terran, UnitSize::Large,  500, 0,  0, 1,  8, 100,   0,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int RefineryID = (int)T.size();
    T.push_back({"Refinery",        Race::Terran, UnitSize::Large,  500, 0,  0, 1,  0, 100,   0,  40,  0,  0, 0, 0, 0.0f, 11, true});
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
    T.push_back({"PhysicsLab",      Race::Terran, UnitSize::Large,  750, 0,  0, 1,  0,  50,  50,  30,  0,  0, 0, 0, 0.0f, 11, true});
    int MachineShopID = (int)T.size();
    T.push_back({"MachineShop",     Race::Terran, UnitSize::Large,  750, 0,  0, 1,  0,  50,  50,  30,  0,  0, 0, 0, 0.0f, 11, true});
    int EngineeringBayID = (int)T.size();
    T.push_back({"EngineeringBay",  Race::Terran, UnitSize::Large,  850, 0,  0, 1,  0, 125,   0,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int ArmoryID = (int)T.size();
    T.push_back({"Armory",          Race::Terran, UnitSize::Large,  750, 0,  0, 1,  0, 100,  50,  40,  0,  0, 0, 0, 0.0f, 11, true});
    int BunkerID = (int)T.size();
    T.push_back({"Bunker",          Race::Terran, UnitSize::Large,  350, 0,  0, 1,  0, 100,   0,  30,  0,  0, 0, 0, 0.0f, 11, true});
    int MissileTurretID = (int)T.size();
    T.push_back({"MissileTurret",   Race::Terran, UnitSize::Large,  200, 0,  0, 1,  0,  75,   0,  20, 20, 20, 7, 1, 0.0f, 11, true});

    T[0].ProducedBy = CommandCenterID;
    T[1].ProducedBy = BarracksID;
    T[2].ProducedBy = BarracksID; T[2].RequiredBuildings = {AcademyID};
    T[3].ProducedBy = BarracksID; T[3].RequiredBuildings = {AcademyID};
    T[4].ProducedBy = BarracksID; T[4].RequiredBuildings = {AcademyID, CovertOpsID};
    T[5].ProducedBy = FactoryID;
    T[6].ProducedBy = FactoryID;  T[6].RequiredBuildings = {MachineShopID};
    T[7].ProducedBy = FactoryID;  T[7].RequiredBuildings = {MachineShopID};
    T[8].ProducedBy = StarportID;
    T[9].ProducedBy = StarportID;
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
    SetTerranMeta(2, 20, 2);
    SetTerranMeta(3, 21, 3);
    SetTerranMeta(4, 1, 4);
    SetTerranMeta(5, 2, 5);
    SetTerranMeta(6, 3, 6);
    SetTerranMeta(7, 5, 7);
    SetTerranMeta(8, 10, 8);
    SetTerranMeta(9, 12, 9);
    SetTerranMeta(10, 11, 10);
    SetTerranMeta(11, 13, 11);
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
auto& Z = ZergDefs;
    int LarvaID = (int)Z.size();
    Z.push_back({"Larva",            Race::Zerg, UnitSize::Small,  20,  0,  0, 0,  0,  0,   0,  1,   0,  0,  0, 0,  0,   0,   false});
    int ZDroneID = (int)Z.size();
    Z.push_back({"Drone",            Race::Zerg, UnitSize::Small,  40,  0,  0, 0,  1,  50,  0,  17,  5,  0,  0, 1,  2.9f, 8,  false});
    int ZOverlordID = (int)Z.size();
    Z.push_back({"Overlord",         Race::Zerg, UnitSize::Large, 200,  0,  0, 0,  0,  100, 0,  25,  0,  0,  0, 0,  0.7f, 10, false});
    int ZZerglingID = (int)Z.size();
    Z.push_back({"Zergling",         Race::Zerg, UnitSize::Small,  35,  0,  0, 0,  0,  50,  0,  24,  5,  0,  1, 1,  3.4f, 7,  false});
    int ZHydraliskID = (int)Z.size();
    Z.push_back({"Hydralisk",        Race::Zerg, UnitSize::Medium, 80,  0,  0, 0,  1,  75,  25, 24,  10, 0,  4, 1,  2.5f, 7,  false});
    int ZMutaliskID = (int)Z.size();
    Z.push_back({"Mutalisk",         Race::Zerg, UnitSize::Medium,120,  0,  0, 0,  2,  100, 100,30,  9,  0,  3, 1,  3.5f, 8,  false});
    int ZScourgeID = (int)Z.size();
    Z.push_back({"Scourge",          Race::Zerg, UnitSize::Small,  25,  0,  0, 0,  0,  25,  75, 20,  5,  0,  1, 1,  4.5f, 5,  false});
    int ZQueenID = (int)Z.size();
    Z.push_back({"Queen",            Race::Zerg, UnitSize::Medium, 80,  0,  0, 0,  2,  100, 100,30,  0,  0,  0, 0,  2.0f, 9,  false});
    int ZUltraliskID = (int)Z.size();
    Z.push_back({"Ultralisk",        Race::Zerg, UnitSize::Large, 400,  0,  0, 1,  4,  200, 200,39,  20, 0,  7, 1,  2.9f, 8,  false});
    int ZDefilerID = (int)Z.size();
    Z.push_back({"Defiler",          Race::Zerg, UnitSize::Medium, 80,  0,  0, 1,  2,  50,  150,30,  0,  0,  0, 0,  2.0f, 9,  false});
    int ZLurkerID = (int)Z.size();
    Z.push_back({"Lurker",           Race::Zerg, UnitSize::Medium,125,  0,  0, 1,  2,  50,  100,28,  20, 0,  6, 1,  2.5f, 8,  false});
    int ZBroodlingID = (int)Z.size();
    Z.push_back({"Broodling",        Race::Zerg, UnitSize::Small,  15,  0,  0, 0,  0,  0,   0,  1,   5,  0,  0, 1,  2.5f, 5,  false});
    int ZInfestedID = (int)Z.size();
    Z.push_back({"Infested Terran",  Race::Zerg, UnitSize::Medium, 60,  0,  0, 0,  1,  0,   0,  20,  12, 0,  5, 1,  2.5f, 8,  false});
    int ZGuardianID = (int)Z.size();
    Z.push_back({"Guardian",         Race::Zerg, UnitSize::Medium,150,  0,  0, 2,  2,  50,  100,33,  20, 0,  8, 1,  2.5f, 10, false});
    int ZDevourerID = (int)Z.size();
    Z.push_back({"Devourer",         Race::Zerg, UnitSize::Medium,150,  0,  0, 0,  2,  100, 50, 33,  15, 0,  6, 1,  3.5f, 9,  false});

    // Zerg buildings
    int ZHatcheryID = (int)Z.size();
    Z.push_back({"Hatchery",         Race::Zerg, UnitSize::Large, 1500, 0,  0, 1,  0, 300, 0,  120, 0,  0,  0, 0,  0,   11, true, false, true});
    int ZLairID = (int)Z.size();
    Z.push_back({"Lair",             Race::Zerg, UnitSize::Large, 1800, 0,  0, 1,  0, 150, 100,80,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZHiveID = (int)Z.size();
    Z.push_back({"Hive",             Race::Zerg, UnitSize::Large, 2000, 0,  0, 1,  0, 200, 150,80,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZExtractorID = (int)Z.size();
    Z.push_back({"Extractor",        Race::Zerg, UnitSize::Large, 500,  0,  0, 1,  0, 50,  0,  40,  0,  0,  0, 0,  0,   11, true});
    int ZSpawningPoolID = (int)Z.size();
    Z.push_back({"Spawning Pool",    Race::Zerg, UnitSize::Large, 750,  0,  0, 1,  0, 200, 0,  60,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZEvolutionChamberID = (int)Z.size();
    Z.push_back({"Evolution Chamber",Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 75,  0,  40,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZHydraliskDenID = (int)Z.size();
    Z.push_back({"Hydralisk Den",    Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 75,  50, 40,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZSpireID = (int)Z.size();
    Z.push_back({"Spire",            Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 100, 100,60,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZNydusCanalID = (int)Z.size();
    Z.push_back({"Nydus Canal",      Race::Zerg, UnitSize::Large, 250,  0,  0, 1,  0, 150, 0,  40,  0,  0,  0, 0,  0,   11, true});
    int ZQueensNestID = (int)Z.size();
    Z.push_back({"Queen's Nest",     Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 75,  100,50,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZGreaterSpireID = (int)Z.size();
    Z.push_back({"Greater Spire",    Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 100, 150,60,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZUltraliskCavernID = (int)Z.size();
    Z.push_back({"Ultralisk Cavern", Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 150, 200,60,  0,  0,  0, 0,  0,   11, true, false, true});
    int ZDefilerMoundID = (int)Z.size();
    Z.push_back({"Defiler Mound",    Race::Zerg, UnitSize::Large, 600,  0,  0, 1,  0, 100, 150,50,  0,  0,  0, 0,  0,   11, true, false, true});

    // Assign production & requirements for Zerg
    Z[ZDroneID].ProducedBy = ZHatcheryID;       // Drone from Hatchery
    Z[ZOverlordID].ProducedBy = ZHatcheryID;    // Overlord from Hatchery
    Z[ZZerglingID].ProducedBy = ZHatcheryID;    // Zergling from Hatchery
    Z[ZHydraliskID].ProducedBy = ZHatcheryID;   // Hydralisk from Hatchery
    Z[ZMutaliskID].ProducedBy = ZHatcheryID;    // Mutalisk from Hatchery
    Z[ZScourgeID].ProducedBy = ZHatcheryID;     // Scourge from Hatchery
    Z[ZQueenID].ProducedBy = ZHatcheryID;       // Queen from Hatchery
    Z[ZUltraliskID].ProducedBy = ZHatcheryID;   // Ultralisk from Hatchery
    Z[ZDefilerID].ProducedBy = ZHatcheryID;     // Defiler from Hatchery
    Z[ZLurkerID].ProducedBy = ZHatcheryID;      // Lurker from Hatchery

    Z[ZDroneID].RequiredBuildings = {ZHatcheryID};
    Z[ZOverlordID].RequiredBuildings = {ZHatcheryID};
    Z[ZZerglingID].RequiredBuildings = {ZSpawningPoolID};
    Z[ZHydraliskID].RequiredBuildings = {ZSpawningPoolID};
    Z[ZMutaliskID].RequiredBuildings = {ZSpawningPoolID, ZHydraliskDenID, ZSpireID, ZLairID};
    Z[ZScourgeID].RequiredBuildings = {ZSpawningPoolID, ZSpireID};
    Z[ZQueenID].RequiredBuildings = {ZSpawningPoolID, ZQueensNestID, ZLairID};
    Z[ZUltraliskID].RequiredBuildings = {ZSpawningPoolID, ZUltraliskCavernID, ZHiveID};
    Z[ZDefilerID].RequiredBuildings = {ZSpawningPoolID, ZDefilerMoundID, ZHiveID};
    Z[ZLurkerID].RequiredBuildings = {ZSpawningPoolID, ZHydraliskDenID, ZLairID};

    // Zerg building requirements
    Z[ZLairID].RequiredBuildings = {ZHatcheryID, ZSpawningPoolID};
    Z[ZHiveID].RequiredBuildings = {ZLairID, ZQueensNestID, ZSpireID, ZUltraliskCavernID};
    Z[ZSpawningPoolID].RequiredBuildings = {ZHatcheryID};
    Z[ZEvolutionChamberID].RequiredBuildings = {ZHatcheryID};
    Z[ZHydraliskDenID].RequiredBuildings = {ZSpawningPoolID, ZLairID};
    Z[ZSpireID].RequiredBuildings = {ZLairID};
    Z[ZQueensNestID].RequiredBuildings = {ZLairID};
    Z[ZGreaterSpireID].RequiredBuildings = {ZSpireID, ZHiveID};
    Z[ZUltraliskCavernID].RequiredBuildings = {ZHiveID};
    Z[ZDefilerMoundID].RequiredBuildings = {ZHiveID};

        auto& P = ProtossDefs;
    int PProbeID = (int)P.size();
    P.push_back({"Probe",      Race::Protoss, UnitSize::Small,  20, 20, 0, 0,  1,  50,  0,  17,  5,  0,  0, 1,  2.8f, 9,  false});
    int PZealotID = (int)P.size();
    P.push_back({"Zealot",     Race::Protoss, UnitSize::Small,  80, 60, 0, 1,  2, 100,  0,  27,  8,  0,  1, 1,  2.5f, 7,  false});
    int PDragoonID = (int)P.size();
    P.push_back({"Dragoon",    Race::Protoss, UnitSize::Medium,100, 80, 0, 1,  2, 125, 50, 30,  20,  0,  4, 1,  2.4f, 8,  false});
    int PHighTemplarID = (int)P.size();
    P.push_back({"High Templar",Race::Protoss, UnitSize::Small, 40, 40, 0, 0,  2,  50, 150,30,  0,  0,  0, 0,  2.0f, 8,  false});
    int PDarkTemplarID = (int)P.size();
    P.push_back({"Dark Templar",Race::Protoss, UnitSize::Small, 80, 40, 0, 1,  2, 125, 125,30,  40,  0,  1, 1,  2.5f, 7,  false});
    int PArchonID = (int)P.size();
    P.push_back({"Archon",     Race::Protoss, UnitSize::Medium,300,300, 0, 0,  4,  0,   0,  1,  30,  0,  2, 1,  2.0f, 8,  false});
    int PDarkArchonID = (int)P.size();
    P.push_back({"Dark Archon",Race::Protoss, UnitSize::Medium,250,250, 0, 0,  4,  0,   0,  1,  0,   0,  0, 0,  2.0f, 8,  false});
    int PReaverID = (int)P.size();
    P.push_back({"Reaver",     Race::Protoss, UnitSize::Large, 100, 80, 0, 0,  4, 200, 100,35,  20,  0,  8, 1,  2.0f, 9,  false});
    int PShuttleID = (int)P.size();
    P.push_back({"Shuttle",    Race::Protoss, UnitSize::Medium, 60, 60, 0, 0,  2, 100,  0,  30,  0,  0,  0, 0,  3.0f, 8,  false});
    int PScoutID = (int)P.size();
    P.push_back({"Scout",      Race::Protoss, UnitSize::Medium,100, 80, 0, 0,  3, 275, 125,40,  8,   0,  4, 1,  3.5f, 9,  false});
    int PCorsairID = (int)P.size();
    P.push_back({"Corsair",    Race::Protoss, UnitSize::Medium, 80, 80, 0, 1,  2, 150, 100,30,  5,   0,  3, 1,  3.5f, 9,  false});
    int PCarrierID = (int)P.size();
    P.push_back({"Carrier",    Race::Protoss, UnitSize::Large, 200,150, 0, 2,  6, 350, 250,60,  6,   0,  6, 1,  2.5f, 11, false});
    int PArbiterID = (int)P.size();
    P.push_back({"Arbiter",    Race::Protoss, UnitSize::Large, 200,150, 0, 1,  4, 200, 150,50,  10,  0,  5, 1,  2.5f, 10, false});
    int PObserverID = (int)P.size();
    P.push_back({"Observer",   Race::Protoss, UnitSize::Small,  20, 20, 0, 0,  1,  25,  75, 20,  0,  0,  0, 0,  2.5f, 11, false});

    // Protoss buildings
    int PNexusID = (int)P.size();
    P.push_back({"Nexus",            Race::Protoss, UnitSize::Large, 1000,1000,0, 1,  0, 400, 0,  120, 0,  0,  0, 0,  0,   11, true, false, false, true});
    int PPylonID = (int)P.size();
    P.push_back({"Pylon",            Race::Protoss, UnitSize::Large, 300, 300, 0, 1,  0, 100, 0,  30,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PAssimilatorID = (int)P.size();
    P.push_back({"Assimilator",      Race::Protoss, UnitSize::Large, 450, 450, 0, 1,  0, 100, 0,  40,  0,  0,  0, 0,  0,   11, true});
    int PGatewayID = (int)P.size();
    P.push_back({"Gateway",          Race::Protoss, UnitSize::Large, 600, 600, 0, 1,  0, 150, 0,  60,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PForgeID = (int)P.size();
    P.push_back({"Forge",            Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 150, 0,  30,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PPhotonCannonID = (int)P.size();
    P.push_back({"Photon Cannon",    Race::Protoss, UnitSize::Large, 100, 100, 0, 1,  0, 100, 0,  30,  20, 20, 6, 1,  0,   11, true, false, false, true});
    int PCyberneticsCoreID = (int)P.size();
    P.push_back({"Cybernetics Core", Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 200, 0,  40,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PShieldBatteryID = (int)P.size();
    P.push_back({"Shield Battery",   Race::Protoss, UnitSize::Large, 200, 200, 0, 1,  0, 100, 0,  20,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PRoboticsFacilityID = (int)P.size();
    P.push_back({"Robotics Facility",Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 200, 100,40,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PStargateID = (int)P.size();
    P.push_back({"Stargate",         Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 150, 150,50,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PCitadelOfAdunID = (int)P.size();
    P.push_back({"Citadel of Adun",  Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 150, 100,50,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PTemplarArchivesID = (int)P.size();
    P.push_back({"Templar Archives", Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 150, 200,40,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PRoboticsSupportBayID = (int)P.size();
    P.push_back({"Robotics Support Bay",Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 100, 100,30,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PFleetBeaconID = (int)P.size();
    P.push_back({"Fleet Beacon",     Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 200, 150,40,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PArbiterTribunalID = (int)P.size();
    P.push_back({"Arbiter Tribunal", Race::Protoss, UnitSize::Large, 400, 400, 0, 1,  0, 200, 150,50,  0,  0,  0, 0,  0,   11, true, false, false, true});
    int PObservatoryID = (int)P.size();
    P.push_back({"Observatory",      Race::Protoss, UnitSize::Large, 200, 200, 0, 1,  0, 50,  100,30,  0,  0,  0, 0,  0,   11, true, false, false, true});

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
    P[PCorsairID].RequiredBuildings = {PStargateID, PFleetBeaconID};
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
    T.push_back({"JimRaynorMarine",    Race::Terran, UnitSize::Small, 100,  0,  0, 3,  1,  0,   0,  24, 12,  0, 5, 1, 2.6f, 7,  false});
    T.push_back({"JimRaynorVulture",   Race::Terran, UnitSize::Medium,300,  0,  0, 1,  2,  0,   0,  30, 30,  0, 5, 1, 3.2f, 8,  false});
    T.push_back({"SarahKerrigan",      Race::Terran, UnitSize::Small, 250,  0,50, 1,  1,  0,   0,  30, 30,  0, 6, 1, 2.6f, 9,  false});
    T.push_back({"EdmundDukeSiege",    Race::Terran, UnitSize::Large, 400,  0,  0, 2,  2,  0,   0,  45, 70,  0, 7, 1, 2.3f, 10, false});
    T.push_back({"GuiMontag",          Race::Terran, UnitSize::Small, 160,  0,  0, 1,  1,  0,   0,  24, 15,  0, 2, 1, 2.6f, 7,  false});
    T.push_back({"ArcturusMengsk",     Race::Terran, UnitSize::Large, 500,  0,200, 3,  6,  0,   0,  90, 50, 50, 6, 1, 2.2f, 11, false});
    T.push_back({"Hyperion",           Race::Terran, UnitSize::Large, 850,  0,200, 4,  6,  0,   0,  90, 50, 50, 6, 1, 2.1f, 11, false});
    T.push_back({"NoradII",            Race::Terran, UnitSize::Large, 700,  0,200, 4,  6,  0,   0,  90, 50, 50, 6, 1, 2.1f, 11, false});

    Z.push_back({"Torrasque",          Race::Zerg,   UnitSize::Large, 800,  0,  0, 4,  6,  0,   0,  50, 50,  0, 2, 1, 2.8f, 10, false});
    Z.push_back({"InfestedKerrigan",   Race::Zerg,   UnitSize::Small, 300,  0,50, 2,  1,  0,   0,  30, 50,  0, 5, 1, 2.6f, 9,  false});
    Z.push_back({"HunterKiller",       Race::Zerg,   UnitSize::Medium,250,  0,  0, 2,  2,  0,   0,  30, 25, 25, 5, 1, 2.8f, 10, false});
    Z.push_back({"DevouringOne",       Race::Zerg,   UnitSize::Small, 200,  0,  0, 1,  1,  0,   0,  20, 20,  0, 2, 1, 2.6f, 8,  false});

    P.push_back({"FenixZealot",        Race::Protoss, UnitSize::Medium,240, 50,  0, 1,  2,  0,   0,  40, 20,  0, 1, 0, 2.8f, 8,  false});
    P.push_back({"FenixDragoon",       Race::Protoss, UnitSize::Large, 300,100,  0, 2,  2,  0,   0,  50, 35, 35, 5, 0, 2.5f, 9,  false});
    P.push_back({"Zeratul",            Race::Protoss, UnitSize::Small, 200, 80,  0, 1,  1,  0,   0,  30, 50,  0, 1, 0, 2.8f, 9,  false});
    P.push_back({"Tassadar",           Race::Protoss, UnitSize::Small, 200, 80,100, 1,  2,  0,   0,  40, 30,  0, 0, 0, 2.5f, 10, false});
    P.push_back({"Warbringer",         Race::Protoss, UnitSize::Large, 300,150,  0, 2,  4,  0,   0,  60, 60,  0, 1, 0, 2.5f, 9,  false});
    P.push_back({"Gantrithor",         Race::Protoss, UnitSize::Large, 500,200,  0, 3,  6,  0,   0,  90, 50, 50, 6, 0, 2.2f, 11, false});
    P.push_back({"Danimoth",           Race::Protoss, UnitSize::Large, 400,150,  0, 2,  4,  0,   0,  90, 30, 30, 5, 0, 2.5f, 10, false});
    P.push_back({"Aldaris",            Race::Protoss, UnitSize::Large, 500,150,100, 2,  4,  0,   0,  90, 40, 40, 6, 0, 2.2f, 11, false});

    // Critters
    T.push_back({"Rhynadon",           Race::Terran,  UnitSize::Large,  60,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Bengalaas",          Race::Terran,  UnitSize::Large,  60,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Scantid",            Race::Terran,  UnitSize::Small,  40,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Kakaru",             Race::Terran,  UnitSize::Small,  40,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Ragnasaur",          Race::Terran,  UnitSize::Large,  80,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    T.push_back({"Ursadon",            Race::Terran,  UnitSize::Large,  80,  0,  0, 0,  0,  0,   0,   0,  0,  0, 0, 0, 1.5f,  5,  false});
    for (int I = 0; I < (int)T.size(); ++I)
        NameToID[T[I].Name] = I;
    for (int I = 0; I < (int)ZergDefs.size(); ++I)
        NameToID[ZergDefs[I].Name] = I + (int)T.size();
    for (int I = 0; I < (int)ProtossDefs.size(); ++I)
        NameToID[ProtossDefs[I].Name] = I + (int)T.size() + (int)ZergDefs.size();
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
int GetUnitDefID(const std::string& Name)
{
    InitDefs();
    auto It = NameToID.find(Name);
    if (It != NameToID.end()) return It->second;
    return -1;
}

} // namespace Game::Races
