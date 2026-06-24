#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include "Game/Components.h"

namespace Game::Races
{
using Components::Race;
using Components::UnitSize;

struct UnitDef
{
    std::string Name;
    Race RaceType;
    UnitSize SizeType = UnitSize::Medium;

    int HitPoints = 100;
    int ShieldPoints = 0;
    int EnergyPoints = 0;
    int Armor = 0;

    int SupplyCost = 1;
    int MineralCost = 50;
    int GasCost = 0;
    int BuildTime = 30;

    int GroundDamage = 0;
    int AirDamage = 0;
    float AttackRange = 1.0f;
    float AttackCooldown = 1.0f;
    float Speed = 2.0f;
    int SightRange = 8;

    bool IsBuilding = false;
    bool CanLiftOff = false;
    bool RequiresCreep = false;
    bool RequiresPylon = false;

    Components::DamageType GroundDmgType = Components::DamageType::Normal;
    Components::DamageType AirDmgType = Components::DamageType::Normal;

    std::vector<int> RequiredBuildings;  // building UnitDef IDs required
    int ProducedBy = -1;                  // building UnitDef ID that produces this
    int ChkUnitID = 0;
    int DefaultIconIndex = -1;
    bool IsAddon = false;
    int AddonOnBuildingDefID = -1;
};

//
// Unit/Building definition tables
//
const std::vector<UnitDef>& GetTerranDefs();
const std::vector<UnitDef>& GetZergDefs();
const std::vector<UnitDef>& GetProtossDefs();

UnitDef GetUnitDefByName(const std::string& Name);
UnitDef GetUnitDefByID(int ID);
int GetUnitDefID(const std::string& Name);
int GetTerranDefIDByChkUnitID(int ChkUnitID);

} // namespace Game::Races
