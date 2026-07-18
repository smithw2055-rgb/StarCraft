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
    Components::WeaponBehavior WeaponBeh = Components::WeaponBehavior::Melee;
    Components::SplashType Splash = Components::SplashType::None;
    float SplashRadius = 0.0f;

    std::vector<int> RequiredBuildings;  // building UnitDef IDs required
    int ProducedBy = -1;                  // producer UnitDef ID (building or morph source)
    int ChkUnitID = 0;
    int DefaultIconIndex = -1;
    bool IsAddon = false;
    int AddonOnBuildingDefID = -1;

    // Weapon values mirror weapons.dat on a per-hit basis.  The target-specific
    // cooldowns are expressed in seconds; zero keeps the legacy AttackCooldown
    // fallback for definitions that have not been calibrated yet.
    int GroundDamageBonus = 0;
    int AirDamageBonus = 0;
    int GroundHitCount = 1;
    int AirHitCount = 1;
    float GroundAttackCooldown = 0.0f;
    float AirAttackCooldown = 0.0f;
    float SplashInnerRadius = 0.0f;
    float SplashMediumRadius = 0.0f;
    bool SelfDestructOnAttack = false;
    // Appended after the legacy aggregate-initialized prefix so existing unit
    // table rows keep their original field alignment.
    float GroundAttackRange = 0.0f;
    float AirAttackRange = 0.0f;
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
int GetUnitDefIDByChkUnitID(int ChkUnitID);
int GetTerranDefIDByChkUnitID(int ChkUnitID);
Race GetRaceByChkUnitID(int ChkUnitID, Race Fallback = Race::Terran);
int GetSupplyProvidedByDefID(int ID);
bool IsDetectorChkUnitID(int ChkUnitID);
bool BuildingDefSatisfiesRequirement(int ActualDefID, int RequiredDefID);

inline void ApplyCombatDefinition(Components::CombatComponent& Combat,
                                  const UnitDef& Def,
                                  bool IsAirUnit)
{
    Combat.GroundDamage = Def.GroundDamage;
    Combat.AirDamage = Def.AirDamage;
    Combat.GroundDamageBonus = Def.GroundDamageBonus;
    Combat.AirDamageBonus = Def.AirDamageBonus;
    Combat.GroundHitCount = Def.GroundHitCount;
    Combat.AirHitCount = Def.AirHitCount;
    Combat.GroundDmgType = Def.GroundDmgType;
    Combat.AirDmgType = Def.AirDmgType;
    Combat.AttackRange = Def.AttackRange;
    Combat.GroundAttackRange = Def.GroundAttackRange;
    Combat.AirAttackRange = Def.AirAttackRange;
    Combat.MinimumAttackRange = 0.0f;
    Combat.AttackCooldown = Def.AttackCooldown;
    Combat.GroundAttackCooldown = Def.GroundAttackCooldown;
    Combat.AirAttackCooldown = Def.AirAttackCooldown;
    Combat.WeaponBeh = Def.WeaponBeh;
    Combat.Splash = Def.Splash;
    Combat.SplashInnerRadius = Def.SplashInnerRadius;
    Combat.SplashMediumRadius = Def.SplashMediumRadius;
    Combat.SplashRadius = Def.SplashRadius;
    Combat.SelfDestructOnAttack = Def.SelfDestructOnAttack;
    Combat.CanTargetGround = Def.GroundDamage > 0;
    Combat.CanTargetAir = Def.AirDamage > 0;
    Combat.MyElevation = IsAirUnit ? Components::Elevation::Air
                                   : Components::Elevation::Ground;
    Combat.IsRanged = Def.AttackRange > 1.5f ||
                      Def.WeaponBeh != Components::WeaponBehavior::Melee;
}

} // namespace Game::Races
