#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "Core/ECS/SystemManager.h"

namespace Game::Systems
{

enum class ResearchId : uint8_t
{
    None = 0,

    // Terran (0-15)
    Terran_InfantryWeapons,
    Terran_InfantryArmor,
    Terran_VehicleWeapons,
    Terran_VehicleArmor,
    Terran_ShipWeapons,
    Terran_ShipArmor,
    Terran_StimPacks,
    Terran_U238Shells,
    Terran_SiegeMode,
    Terran_SpiderMines,
    Terran_CloakingField,
    Terran_IonThrusters,
    Terran_Lockdown,
    Terran_MoebiusReactor,
    Terran_OcularImplants,
    Terran_ApolloReactor,
    Terran_TitanReactor,
    Terran_CaduceusReactor,
    Terran_YamatoGun,
    Terran_ColossusReactor,
    Terran_CharonBoosters,

    // Zerg (16-33)
    Zerg_MeleeAttacks,
    Zerg_MissileAttacks,
    Zerg_Carapace,
    Zerg_FlyerAttacks,
    Zerg_FlyerCarapace,
    Zerg_AdrenalGlands,
    Zerg_MetabolicBoost,
    Zerg_AnabolicSynthesis,
    Zerg_ChitinousPlating,
    Zerg_MuscularAugments,
    Zerg_GroovedSpines,
    Zerg_VentralSacs,
    Zerg_Antennae,
    Zerg_PneumatizedCarapace,
    Zerg_Burrow,
    Zerg_Consume,
    Zerg_Ensnare,
    Zerg_Plague,
    Zerg_SpawnBroodling,
    Zerg_QueenEnergy,
    Zerg_DefilerEnergy,
    Zerg_LurkerMorph,

    // Protoss (34-51)
    Protoss_GroundWeapons,
    Protoss_GroundArmor,
    Protoss_PlasmaShields,
    Protoss_AirWeapons,
    Protoss_AirArmor,
    Protoss_SingularityCharge,
    Protoss_GraviticDrive,
    Protoss_ScarabDamage,
    Protoss_ZealotSpeed,
    Protoss_SensorArray,
    Protoss_ApialSensors,
    Protoss_KhaydarinAmulet,
    Protoss_ArgusJewel,
    Protoss_ArgusTalisman,
    Protoss_KhaydarinCore,
    Protoss_PsionicStorm,
    Protoss_Hallucination,
    Protoss_Recall,
    Protoss_StasisField,
    Protoss_MindControl,
    Protoss_Maelstrom,
    Protoss_DisruptionWeb,
    Protoss_ReaverCapacity,
    Protoss_CarrierCapacity,

    Count
};

struct PlayerUpgrades
{
    std::array<uint8_t, static_cast<size_t>(ResearchId::Count)> Levels = {};

    int GetLevel(ResearchId Id) const
    {
        size_t Idx = static_cast<size_t>(Id);
        if (Idx < Levels.size()) return Levels[Idx];
        return 0;
    }
};

class UpgradeSystem : public Core::ECS::ISystem
{
public:
    UpgradeSystem(Core::ECS::SystemManager& SysMgr);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "UpgradeSystem"; }

    static PlayerUpgrades& GetUpgrades(int PlayerID = 0);

    static bool CanStartResearch(const Core::ECS::EntityManager& EM,
                                  int BuildingID,
                                  ResearchId ResearchId,
                                  std::string* OutReason = nullptr);

    static bool StartResearch(Core::ECS::EntityManager& EM,
                               int BuildingID,
                               ResearchId ResearchId);

    static bool CancelResearch(Core::ECS::EntityManager& EM, int BuildingID);

    static int GetResearchLevel(int PlayerID, ResearchId ResearchId);
    static bool HasResearch(int PlayerID, ResearchId ResearchId);
    static bool IsUpgradeType(ResearchId ResearchId);
    static std::string_view GetResearchDisplayName(ResearchId ResearchId);
    static int GetResearchTimeCost(ResearchId ResearchId, int PlayerID);
    static bool GetResearchCost(ResearchId ResearchId,
                                int PlayerID,
                                int& OutMinerals,
                                int& OutGas);

    static int GetWeaponBonus(const Core::ECS::EntityManager& EM, int EntityID);
    static int GetArmorBonus(const Core::ECS::EntityManager& EM, int EntityID);
    static int GetRangeBonusPixels(const Core::ECS::EntityManager& EM, int EntityID);
    static int GetRangeBonusPixelsForTarget(const Core::ECS::EntityManager& EM,
                                            int EntityID,
                                            bool TargetIsAir);
    static int GetSightRangeBonusTiles(const Core::ECS::EntityManager& EM, int EntityID);
    static float GetMovementSpeedMultiplier(const Core::ECS::EntityManager& EM, int EntityID);
    static float GetAttackCooldownMultiplier(const Core::ECS::EntityManager& EM, int EntityID);
    static int GetEffectiveMaxEnergyPoints(const Core::ECS::EntityManager& EM, int EntityID);
    static void RefreshUnitEnergyCap(Core::ECS::EntityManager& EM, int EntityID);
    static int GetMaxHangarCount(const Core::ECS::EntityManager& EM, int EntityID);

private:
    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
