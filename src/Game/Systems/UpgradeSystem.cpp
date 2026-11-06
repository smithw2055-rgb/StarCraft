#include "Game/Systems/UpgradeSystem.h"

#include <algorithm>
#include <string>

#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/HarvestSystem.h"

namespace Game::Systems
{
using Core::Utils::Logger;

namespace
{
    using Game::Components::BuildingComponent;
    using Game::Components::PlayerComponent;
    using Game::Components::UnitComponent;
    using Game::Components::Race;

    struct ResearchDef
    {
        ResearchId Id = ResearchId::None;
        const char* Name = "";
        const char* ProducerName = "";
        int MineralCost = 0;
        int GasCost = 0;
        int MineralStep = 0;
        int GasStep = 0;
        int MaxLevel = 1;
        int TimeCostFrames = 0;
        int TimeStepFrames = 0;
    };

    constexpr int kBroodWarLogicFramesPerSecond = 24;

    PlayerUpgradeStore gFallbackUpgradeStore;

    int ClampPlayerSlot(int PlayerID)
    {
        if (PlayerID < 0)
            return 0;
        if (PlayerID >= kPlayerResourceSlots)
            return kPlayerResourceSlots - 1;
        return PlayerID;
    }

    PlayerUpgradeStore& ResolveUpgradeStore(Core::ECS::EntityManager& EM)
    {
        if (auto* Store = EM.GetResource<PlayerUpgradeStore>())
            return *Store;
        return gFallbackUpgradeStore;
    }

    const PlayerUpgradeStore& ResolveUpgradeStore(const Core::ECS::EntityManager& EM)
    {
        if (const auto* Store = EM.GetResource<PlayerUpgradeStore>())
            return *Store;
        return gFallbackUpgradeStore;
    }

    PlayerUpgradeStore& ResolveActiveOrFallbackUpgradeStore()
    {
        if (auto* ActiveWorld = Core::ECS::EntityManager::GetActiveWorld())
        {
            if (auto* Store = ActiveWorld->GetResource<PlayerUpgradeStore>())
                return *Store;
        }
        return gFallbackUpgradeStore;
    }

    constexpr ResearchDef kResearchDefs[] =
    {
        // Terran
        {ResearchId::Terran_InfantryWeapons, "Infantry Weapons", "EngineeringBay", 100, 100, 75, 75, 3, 4000, 480},
        {ResearchId::Terran_InfantryArmor,   "Infantry Armor",   "EngineeringBay", 100, 100, 75, 75, 3, 4000, 480},
        {ResearchId::Terran_VehicleWeapons,  "Vehicle Weapons",  "Armory",         100, 100, 75, 75, 3, 4000, 480},
        {ResearchId::Terran_VehicleArmor,    "Vehicle Armor",    "Armory",         100, 100, 75, 75, 3, 4000, 480},
        {ResearchId::Terran_ShipWeapons,     "Ship Weapons",     "Armory",         100, 100, 50, 50, 3, 4000, 480},
        {ResearchId::Terran_ShipArmor,       "Ship Armor",       "Armory",         150, 150, 75, 75, 3, 4000, 480},
        {ResearchId::Terran_StimPacks,       "Stim Packs",       "Academy",        100, 100, 0,  0,  1, 1200, 0},
        {ResearchId::Terran_U238Shells,      "U-238 Shells",     "Academy",        150, 150, 0,  0,  1, 1500, 0},
        {ResearchId::Terran_SiegeMode,       "Siege Mode",       "MachineShop",    150, 150, 0,  0,  1, 1200, 0},
        {ResearchId::Terran_SpiderMines,     "Spider Mines",     "MachineShop",    100, 100, 0,  0,  1, 1200, 0},
        {ResearchId::Terran_CloakingField,   "Cloaking Field",   "ControlTower",   150, 150, 0,  0,  1, 1500, 0},
        {ResearchId::Terran_IonThrusters,    "Ion Thrusters",    "MachineShop",    100, 100, 0,  0,  1, 1500, 0},
        {ResearchId::Terran_Lockdown,        "Lockdown",         "CovertOps",      200, 200, 0,  0,  1, 1500, 0},
        {ResearchId::Terran_MoebiusReactor,  "Moebius Reactor",  "CovertOps",      100, 100, 0,  0,  1, 2500, 0},
        {ResearchId::Terran_OcularImplants,  "Ocular Implants",  "CovertOps",      100, 100, 0,  0,  1, 2500, 0},
        {ResearchId::Terran_ApolloReactor,   "Apollo Reactor",   "ControlTower",   200, 200, 0,  0,  1, 2500, 0},
        {ResearchId::Terran_TitanReactor,    "Titan Reactor",    "ScienceFacility",150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Terran_CaduceusReactor, "Caduceus Reactor", "Academy",        150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Terran_YamatoGun,       "Yamato Gun",       "PhysicsLab",     100, 100, 0,  0,  1, 1800, 0},
        {ResearchId::Terran_ColossusReactor, "Colossus Reactor", "PhysicsLab",     150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Terran_CharonBoosters,  "Charon Boosters",  "MachineShop",    100, 100, 0,  0,  1, 2000, 0},
        {ResearchId::Terran_PersonalCloaking,"Personal Cloaking","CovertOps",      100, 100, 0,  0,  1, 1200, 0},
        {ResearchId::Terran_EMPShockwave,    "EMP Shockwave",    "ScienceFacility",200, 200, 0,  0,  1, 1200, 0},
        {ResearchId::Terran_Irradiate,       "Irradiate",        "ScienceFacility",200, 200, 0,  0,  1, 1200, 0},
        {ResearchId::Terran_Restoration,     "Restoration",      "Academy",        100, 100, 0,  0,  1, 1200, 0},
        {ResearchId::Terran_OpticalFlare,    "Optical Flare",    "Academy",        100, 100, 0,  0,  1, 1200, 0},

        // Zerg
        {ResearchId::Zerg_MeleeAttacks,      "Melee Attacks",      "Evolution Chamber", 100, 100, 50, 50, 3, 4000, 480},
        {ResearchId::Zerg_MissileAttacks,    "Missile Attacks",    "Evolution Chamber", 100, 100, 50, 50, 3, 4000, 480},
        {ResearchId::Zerg_Carapace,          "Carapace",           "Evolution Chamber", 150, 150, 75, 75, 3, 4000, 480},
        {ResearchId::Zerg_FlyerAttacks,      "Flyer Attacks",      "Spire",            100, 100, 75, 75, 3, 4000, 480},
        {ResearchId::Zerg_FlyerCarapace,     "Flyer Carapace",     "Spire",            150, 150, 75, 75, 3, 4000, 480},
        {ResearchId::Zerg_AdrenalGlands,     "Adrenal Glands",     "Spawning Pool",    200, 200, 0,  0,  1, 1500, 0},
        {ResearchId::Zerg_MetabolicBoost,    "Metabolic Boost",    "Spawning Pool",    100, 100, 0,  0,  1, 1500, 0},
        {ResearchId::Zerg_AnabolicSynthesis, "Anabolic Synthesis", "Ultralisk Cavern", 200, 200, 0,  0,  1, 2000, 0},
        {ResearchId::Zerg_ChitinousPlating,  "Chitinous Plating",  "Ultralisk Cavern", 150, 150, 0,  0,  1, 2000, 0},
        {ResearchId::Zerg_MuscularAugments,  "Muscular Augments",  "Hydralisk Den",    150, 150, 0,  0,  1, 1500, 0},
        {ResearchId::Zerg_GroovedSpines,     "Grooved Spines",     "Hydralisk Den",    150, 150, 0,  0,  1, 1500, 0},
        {ResearchId::Zerg_VentralSacs,       "Ventral Sacs",       "Lair",             200, 200, 0,  0,  1, 2400, 0},
        {ResearchId::Zerg_Antennae,          "Antennae",           "Lair",             150, 150, 0,  0,  1, 2000, 0},
        {ResearchId::Zerg_PneumatizedCarapace,"Pneumatized Carapace","Lair",           150, 150, 0,  0,  1, 2000, 0},
        {ResearchId::Zerg_Burrow,            "Burrow",             "Hatchery",         100, 100, 0,  0,  1, 1200, 0},
        {ResearchId::Zerg_Consume,           "Consume",            "Defiler Mound",    100, 100, 0,  0,  1, 1500, 0},
        {ResearchId::Zerg_Ensnare,           "Ensnare",            "Queen's Nest",     100, 100, 0,  0,  1, 1200, 0},
        {ResearchId::Zerg_Plague,            "Plague",             "Defiler Mound",    200, 200, 0,  0,  1, 1500, 0},
        {ResearchId::Zerg_SpawnBroodling,    "Spawn Broodling",    "Queen's Nest",     100, 100, 0,  0,  1, 1200, 0},
        {ResearchId::Zerg_QueenEnergy,       "Gamete Meiosis",     "Queen's Nest",     150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Zerg_DefilerEnergy,     "Metasynaptic Node",  "Defiler Mound",    150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Zerg_LurkerMorph,       "Lurker Aspect",      "Hydralisk Den",    200, 200, 0,  0,  1, 1800, 0},

        // Protoss
        {ResearchId::Protoss_GroundWeapons,   "Ground Weapons",    "Forge",            100, 100, 50, 50, 3, 4000, 480},
        {ResearchId::Protoss_GroundArmor,     "Ground Armor",      "Forge",            100, 100, 75, 75, 3, 4000, 480},
        {ResearchId::Protoss_PlasmaShields,   "Plasma Shields",    "Forge",            200, 200, 100,100,3, 4000, 480},
        {ResearchId::Protoss_AirWeapons,      "Air Weapons",       "Cybernetics Core", 100, 100, 75, 75, 3, 4000, 480},
        {ResearchId::Protoss_AirArmor,        "Air Armor",         "Cybernetics Core", 150, 150, 75, 75, 3, 4000, 480},
        {ResearchId::Protoss_SingularityCharge,"Singularity Charge","Cybernetics Core",150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Protoss_GraviticDrive,   "Gravitic Drive",    "Robotics Support Bay",200,200,0,  0,  1, 2500, 0},
        {ResearchId::Protoss_ScarabDamage,    "Scarab Damage",     "Robotics Support Bay",200,200,0,  0,  1, 2500, 0},
        {ResearchId::Protoss_ZealotSpeed,     "Leg Enhancement",   "Citadel of Adun",  150, 150, 0,  0,  1, 2000, 0},
        {ResearchId::Protoss_SensorArray,     "Sensor Array",      "Observatory",      150, 150, 0,  0,  1, 2000, 0},
        {ResearchId::Protoss_ApialSensors,    "Apial Sensors",     "Fleet Beacon",     100, 100, 0,  0,  1, 2500, 0},
        {ResearchId::Protoss_KhaydarinAmulet, "Khaydarin Amulet",  "Templar Archives", 150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Protoss_ArgusJewel,      "Argus Jewel",       "Fleet Beacon",     100, 100, 0,  0,  1, 2500, 0},
        {ResearchId::Protoss_ArgusTalisman,   "Argus Talisman",    "Templar Archives", 150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Protoss_KhaydarinCore,   "Khaydarin Core",    "Arbiter Tribunal", 150, 150, 0,  0,  1, 2500, 0},
        {ResearchId::Protoss_PsionicStorm,    "Psionic Storm",     "Templar Archives", 200, 200, 0,  0,  1, 1800, 0},
        {ResearchId::Protoss_Hallucination,   "Hallucination",     "Templar Archives", 150, 150, 0,  0,  1, 1200, 0},
        {ResearchId::Protoss_Recall,          "Recall",            "Arbiter Tribunal", 150, 150, 0,  0,  1, 1800, 0},
        {ResearchId::Protoss_StasisField,     "Stasis Field",      "Arbiter Tribunal", 150, 150, 0,  0,  1, 1500, 0},
        {ResearchId::Protoss_MindControl,     "Mind Control",      "Templar Archives", 200, 200, 0,  0,  1, 1800, 0},
        {ResearchId::Protoss_Maelstrom,       "Maelstrom",         "Templar Archives", 100, 100, 0,  0,  1, 1500, 0},
        {ResearchId::Protoss_DisruptionWeb,   "Disruption Web",    "Fleet Beacon",     200, 200, 0,  0,  1, 1200, 0},
        {ResearchId::Protoss_ReaverCapacity,  "Reaver Capacity",   "Robotics Support Bay",200,200,0,  0,  1, 2500, 0},
        {ResearchId::Protoss_CarrierCapacity, "Carrier Capacity",  "Fleet Beacon",     100, 100, 0,  0,  1, 1500, 0},
        {ResearchId::Protoss_GraviticBoosters,"Gravitic Boosters", "Observatory",      150, 150, 0,  0,  1, 2000, 0},
        {ResearchId::Protoss_GraviticThrusters,"Gravitic Thrusters","Fleet Beacon",    200, 200, 0,  0,  1, 2500, 0},
    };

    void SetReason(std::string* OutReason, const std::string& Reason)
    {
        if (OutReason)
            *OutReason = Reason;
    }

    size_t ToIndex(ResearchId ResearchId)
    {
        return static_cast<size_t>(ResearchId);
    }

    const ResearchDef* FindResearchDef(ResearchId ResearchId)
    {
        for (const auto& Def : kResearchDefs)
            if (Def.Id == ResearchId)
                return &Def;
        return nullptr;
    }

    int ResolveDefID(const Core::ECS::EntityManager& EM, int EntityID)
    {
        if (const auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
        {
            if (Building->BuildingDefID >= 0)
                return Building->BuildingDefID;
        }
        if (const auto* Unit = EM.GetComponent<UnitComponent>(EntityID))
        {
            // Siege mode is a separate units.dat entry but shares the tank's
            // vehicle weapon/armor upgrade family in Brood War.
            if (Unit->ChkUnitID == 30)
                return Game::Races::GetUnitDefID("SiegeTank");
            return Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        }
        return -1;
    }

    bool PlayerHasCompletedBuilding(const Core::ECS::EntityManager& EM,
                                    int PlayerID,
                                    const char* RequiredName)
    {
        const int RequiredDefID = Game::Races::GetUnitDefID(RequiredName);
        if (RequiredDefID < 0)
            return false;

        return EM.AnyEntityWith<BuildingComponent, UnitComponent, PlayerComponent>(
            [&](int EntityID)
            {
                const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
                const auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
                const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
                if (!Player || Player->PlayerID != PlayerID || !Building ||
                    Building->UnderConstruction || !Unit || Unit->HitPoints <= 0)
                {
                    return false;
                }

                return Game::Races::BuildingDefSatisfiesRequirement(
                    ResolveDefID(EM, EntityID), RequiredDefID);
            });
    }

    bool PlayerHasOwnedBuilding(const Core::ECS::EntityManager& EM,
                                int PlayerID,
                                const char* RequiredName)
    {
        const int RequiredDefID = Game::Races::GetUnitDefID(RequiredName);
        if (RequiredDefID < 0)
            return false;

        for (int EntityID : EM.GetEntitiesWith<BuildingComponent,
                                               UnitComponent,
                                               PlayerComponent>())
        {
            const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
            const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            if (Player && Player->PlayerID == PlayerID && Unit &&
                Unit->HitPoints > 0 && ResolveDefID(EM, EntityID) == RequiredDefID)
            {
                return true;
            }
        }
        return false;
    }

    bool IsZergMultiLevelUpgrade(ResearchId ResearchId)
    {
        switch (ResearchId)
        {
            case ResearchId::Zerg_MeleeAttacks:
            case ResearchId::Zerg_MissileAttacks:
            case ResearchId::Zerg_Carapace:
            case ResearchId::Zerg_FlyerAttacks:
            case ResearchId::Zerg_FlyerCarapace:
                return true;
            default:
                return false;
        }
    }

    const char* GetRequiredTechnology(ResearchId ResearchId, int CurrentLevel)
    {
        switch (ResearchId)
        {
            case ResearchId::Terran_InfantryWeapons:
            case ResearchId::Terran_InfantryArmor:
            case ResearchId::Terran_VehicleWeapons:
            case ResearchId::Terran_VehicleArmor:
            case ResearchId::Terran_ShipWeapons:
            case ResearchId::Terran_ShipArmor:
                return CurrentLevel > 0 ? "ScienceFacility" : nullptr;

            case ResearchId::Zerg_MeleeAttacks:
            case ResearchId::Zerg_MissileAttacks:
            case ResearchId::Zerg_Carapace:
            case ResearchId::Zerg_FlyerAttacks:
            case ResearchId::Zerg_FlyerCarapace:
                if (CurrentLevel == 1)
                    return "Lair";
                if (CurrentLevel == 2)
                    return "Hive";
                return nullptr;

            case ResearchId::Protoss_GroundWeapons:
            case ResearchId::Protoss_GroundArmor:
                return CurrentLevel > 0 ? "Templar Archives" : nullptr;
            case ResearchId::Protoss_AirWeapons:
            case ResearchId::Protoss_AirArmor:
                return CurrentLevel > 0 ? "Fleet Beacon" : nullptr;
            case ResearchId::Protoss_PlasmaShields:
                return CurrentLevel > 0 ? "Cybernetics Core" : nullptr;

            case ResearchId::Zerg_AdrenalGlands:
                return "Hive";
            case ResearchId::Zerg_LurkerMorph:
                return "Lair";
            case ResearchId::Terran_CharonBoosters:
                return "Armory";
            default:
                return nullptr;
        }
    }

    int GetResearchTimeFrames(const ResearchDef& Research, int CurrentLevel)
    {
        return Research.TimeCostFrames + CurrentLevel * Research.TimeStepFrames;
    }

    float GetResearchTimeSeconds(const ResearchDef& Research, int CurrentLevel)
    {
        return static_cast<float>(GetResearchTimeFrames(Research, CurrentLevel)) /
               static_cast<float>(kBroodWarLogicFramesPerSecond);
    }

    bool IsTerranInfantry(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "SCV" || D.Name == "Marine" || D.Name == "Firebat" ||
               D.Name == "Medic" || D.Name == "Ghost" ||
               D.Name == "JimRaynorMarine" || D.Name == "SarahKerrigan" ||
               D.Name == "GuiMontag";
    }

    bool IsTerranVehicle(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Vulture" || D.Name == "Goliath" || D.Name == "SiegeTank" ||
               D.Name == "JimRaynorVulture" || D.Name == "EdmundDukeSiege";
    }

    bool IsTerranShip(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Wraith" || D.Name == "Dropship" || D.Name == "ScienceVessel" ||
               D.Name == "Battlecruiser" || D.Name == "Valkyrie" ||
               D.Name == "ArcturusMengsk" || D.Name == "Hyperion" || D.Name == "NoradII";
    }

    bool IsZergGround(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.RaceType == Race::Zerg && !D.IsBuilding &&
               D.Name != "Mutalisk" && D.Name != "Guardian" &&
               D.Name != "Devourer" && D.Name != "Scourge" &&
               D.Name != "Queen" && D.Name != "Overlord";
    }

    bool IsZergMelee(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Zergling" || D.Name == "Ultralisk" || D.Name == "Broodling" ||
               D.Name == "Torrasque" || D.Name == "InfestedKerrigan" || D.Name == "DevouringOne";
    }

    bool IsZergFlyer(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Mutalisk" || D.Name == "Guardian" || D.Name == "Devourer" ||
               D.Name == "Scourge" || D.Name == "Queen" || D.Name == "Overlord";
    }

    bool IsProtossGround(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.RaceType == Race::Protoss && !D.Name.empty() && !D.IsBuilding &&
               D.Name != "Scout" && D.Name != "Corsair" && D.Name != "Carrier" &&
               D.Name != "Arbiter" && D.Name != "Observer" && D.Name != "Shuttle" &&
               D.Name != "Gantrithor" && D.Name != "Danimoth";
    }

    bool IsProtossAir(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Scout" || D.Name == "Corsair" || D.Name == "Carrier" ||
               D.Name == "Arbiter" || D.Name == "Shuttle" || D.Name == "Observer" ||
               D.Name == "Gantrithor" || D.Name == "Danimoth";
    }

    ResearchId GetEnergyUpgradeForDef(const Game::Races::UnitDef& Def)
    {
        if (Def.Name == "Ghost")
            return ResearchId::Terran_MoebiusReactor;
        if (Def.Name == "Wraith")
            return ResearchId::Terran_ApolloReactor;
        if (Def.Name == "ScienceVessel")
            return ResearchId::Terran_TitanReactor;
        if (Def.Name == "Battlecruiser")
            return ResearchId::Terran_ColossusReactor;
        if (Def.Name == "Medic")
            return ResearchId::Terran_CaduceusReactor;
        if (Def.Name == "Queen")
            return ResearchId::Zerg_QueenEnergy;
        if (Def.Name == "Defiler")
            return ResearchId::Zerg_DefilerEnergy;
        if (Def.Name == "Corsair")
            return ResearchId::Protoss_ArgusJewel;
        if (Def.Name == "Dark Archon")
            return ResearchId::Protoss_ArgusTalisman;
        if (Def.Name == "High Templar")
            return ResearchId::Protoss_KhaydarinAmulet;
        if (Def.Name == "Arbiter")
            return ResearchId::Protoss_KhaydarinCore;
        return ResearchId::None;
    }

    bool IsEnergyCapUpgrade(ResearchId ResearchId)
    {
        switch (ResearchId)
        {
            case ResearchId::Terran_MoebiusReactor:
            case ResearchId::Terran_ApolloReactor:
            case ResearchId::Terran_TitanReactor:
            case ResearchId::Terran_ColossusReactor:
            case ResearchId::Terran_CaduceusReactor:
            case ResearchId::Zerg_QueenEnergy:
            case ResearchId::Zerg_DefilerEnergy:
            case ResearchId::Protoss_ArgusJewel:
            case ResearchId::Protoss_ArgusTalisman:
            case ResearchId::Protoss_KhaydarinAmulet:
            case ResearchId::Protoss_KhaydarinCore:
                return true;
            default:
                return false;
        }
    }
}

UpgradeSystem::UpgradeSystem(Core::ECS::SystemManager& SysMgr)
    : mSystemMgr(SysMgr)
{
    Logger::Info("UpgradeSystem", "Initialized (Terran + Zerg + Protoss)");
}

PlayerUpgrades& UpgradeSystem::GetUpgrades(Core::ECS::EntityManager& EM, int PlayerID)
{
    return ResolveUpgradeStore(EM).Upgrades[(size_t)ClampPlayerSlot(PlayerID)];
}

const PlayerUpgrades& UpgradeSystem::GetUpgrades(const Core::ECS::EntityManager& EM, int PlayerID)
{
    return ResolveUpgradeStore(EM).Upgrades[(size_t)ClampPlayerSlot(PlayerID)];
}

PlayerUpgrades& UpgradeSystem::GetUpgrades(int PlayerID)
{
    return ResolveActiveOrFallbackUpgradeStore().Upgrades[(size_t)ClampPlayerSlot(PlayerID)];
}

void UpgradeSystem::ResetUpgrades(Core::ECS::EntityManager& EM)
{
    EM.SetResource<PlayerUpgradeStore>();
    EM.ActivateWorld();
}

void UpgradeSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto& Store = ResolveUpgradeStore(EM);
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (auto ID : Buildings)
    {
        auto* Bldg = EM.GetComponent<BuildingComponent>(ID);
        if (!Bldg || !Bldg->IsResearching) continue;
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (Bldg->UnderConstruction || !Unit || Unit->HitPoints <= 0 ||
            (Bldg->RequiresPylon && !Bldg->HasPylonPower))
            continue;

        Bldg->BuildTimeRemaining -= DeltaTime;
        if (Bldg->BuildTimeRemaining <= 0.0f)
        {
            Bldg->BuildTimeRemaining = 0.0f;

            int PlayerID = Bldg->ResearchPlayer;
            int Rid = Bldg->ResearchingID;
            if (Rid >= 0 && PlayerID >= 0)
            {
                int CurrentLevel = GetResearchLevel(EM, PlayerID, static_cast<ResearchId>(Rid));
                auto& Upgrades = GetUpgrades(EM, PlayerID);
                const int NewLevel = CurrentLevel + 1;
                Upgrades.Levels[static_cast<size_t>(Rid)] =
                    static_cast<uint8_t>(NewLevel);

                const ResearchDef* Research = FindResearchDef(static_cast<ResearchId>(Rid));
                Logger::Info("UpgradeSystem",
                    "Player " + std::to_string(PlayerID) + " completed " +
                    (Research ? std::string(Research->Name) : "research") + " level " +
                    std::to_string(NewLevel));

                EM.Events().Emit(Game::Events::ResearchCompletedEvent{
                    PlayerID,
                    ID,
                    static_cast<uint8_t>(Rid),
                    NewLevel});

                if (IsEnergyCapUpgrade(static_cast<ResearchId>(Rid)))
                {
                    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
                    for (int UnitID : Units)
                    {
                        auto* UnitPlayer = EM.GetComponent<PlayerComponent>(UnitID);
                        if (UnitPlayer && UnitPlayer->PlayerID == PlayerID)
                            RefreshUnitEnergyCap(EM, UnitID);
                    }
                }
                if (PlayerID < kPlayerResourceSlots &&
                    Rid < static_cast<int>(ResearchId::Count))
                {
                    Store.Researching[(size_t)PlayerID][(size_t)Rid] = false;
                }
            }

            Bldg->IsResearching = false;
            Bldg->ResearchingID = -1;
            Bldg->ResearchPlayer = -1;
        }
    }
}

bool UpgradeSystem::CanStartResearch(const Core::ECS::EntityManager& EM,
                                     int BuildingID,
                                     ResearchId ResearchId,
                                     std::string* OutReason)
{
    const ResearchDef* Research = FindResearchDef(ResearchId);
    if (!Research)
    {
        SetReason(OutReason, "unknown research");
        return false;
    }

    const auto* Building = EM.GetComponent<BuildingComponent>(BuildingID);
    if (!Building)
    {
        SetReason(OutReason, "research producer is not a building");
        return false;
    }
    if (Building->UnderConstruction)
    {
        SetReason(OutReason, "building is still under construction");
        return false;
    }
    if (Building->RequiresPylon && !Building->HasPylonPower)
    {
        SetReason(OutReason, "building requires pylon power");
        return false;
    }
    if (Building->IsResearching)
    {
        SetReason(OutReason, "building is already researching");
        return false;
    }
    const auto* ProducerUnit = EM.GetComponent<UnitComponent>(BuildingID);
    if (!ProducerUnit || ProducerUnit->HitPoints <= 0)
    {
        SetReason(OutReason, "research producer is not operational");
        return false;
    }
    if (!EM.GetComponent<Game::Components::TransformComponent>(BuildingID))
    {
        SetReason(OutReason, "research producer missing transform");
        return false;
    }

    const int ProducerDefID = ResolveDefID(EM, BuildingID);
    const int RequiredProducerDefID = Game::Races::GetUnitDefID(Research->ProducerName);
    if (!Game::Races::BuildingDefSatisfiesRequirement(
            ProducerDefID, RequiredProducerDefID))
    {
        SetReason(OutReason, "wrong producer for research");
        return false;
    }

    const auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int CurrentLevel = GetResearchLevel(EM, PlayerID, ResearchId);
    if (CurrentLevel >= Research->MaxLevel)
    {
        SetReason(OutReason, "research already maxed");
        return false;
    }

    if (const char* RequiredTechnology =
            GetRequiredTechnology(ResearchId, CurrentLevel))
    {
        bool HasRequiredTechnology =
            PlayerHasCompletedBuilding(EM, PlayerID, RequiredTechnology);
        // During a Lair -> Hive morph OpenBW no longer counts a completed
        // Lair, but it does accept the owned (not-yet-complete) Hive for +2.
        if (!HasRequiredTechnology && CurrentLevel == 1 &&
            IsZergMultiLevelUpgrade(ResearchId))
        {
            HasRequiredTechnology = PlayerHasOwnedBuilding(EM, PlayerID, "Hive");
        }
        if (!HasRequiredTechnology)
        {
            SetReason(OutReason,
                      "missing required technology: " +
                          std::string(RequiredTechnology));
            return false;
        }
    }
    const auto& Store = ResolveUpgradeStore(EM);
    if (PlayerID >= 0 && PlayerID < kPlayerResourceSlots &&
        Store.Researching[(size_t)PlayerID][static_cast<size_t>(ResearchId)])
    {
        SetReason(OutReason, "research already in progress for player");
        return false;
    }

    const int MineralCost = Research->MineralCost + CurrentLevel * Research->MineralStep;
    const int GasCost = Research->GasCost + CurrentLevel * Research->GasStep;
    const auto& Resources = HarvestSystem::GetResources(EM, PlayerID);
    if (Resources.Minerals < MineralCost || Resources.VespeneGas < GasCost)
    {
        SetReason(OutReason, "not enough resources");
        return false;
    }

    return true;
}

bool UpgradeSystem::StartResearch(Core::ECS::EntityManager& EM,
                                   int BuildingID,
                                   ResearchId ResearchId)
{
    std::string Reason;
    if (!CanStartResearch(EM, BuildingID, ResearchId, &Reason))
    {
        Logger::Debug("UpgradeSystem", "Cannot research: " + Reason);
        return false;
    }

    auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const ResearchDef* Research = FindResearchDef(ResearchId);
    if (!Research)
        return false;

    const int CurrentLevel = GetResearchLevel(EM, PlayerID, ResearchId);
    auto& Resources = HarvestSystem::GetResources(EM, PlayerID);
    Resources.Minerals -= Research->MineralCost + CurrentLevel * Research->MineralStep;
    Resources.VespeneGas -= Research->GasCost + CurrentLevel * Research->GasStep;

    // Time-progress: use BuildingComponent's BuildTimeRemaining as research timer
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    if (Bldg)
    {
        Bldg->BuildTimeRemaining = GetResearchTimeSeconds(*Research, CurrentLevel);
        Bldg->IsResearching = true;
        Bldg->ResearchingID = static_cast<int>(ResearchId);
        Bldg->ResearchPlayer = PlayerID;
    }
    if (PlayerID >= 0 && PlayerID < kPlayerResourceSlots)
        ResolveUpgradeStore(EM).Researching[(size_t)PlayerID][static_cast<size_t>(ResearchId)] = true;

    Logger::Info("UpgradeSystem",
        "Player " + std::to_string(PlayerID) + " started " +
        std::string(Research->Name) + " level " +
        std::to_string(CurrentLevel + 1));
    return true;
}

bool UpgradeSystem::CancelResearch(Core::ECS::EntityManager& EM,
                                   int BuildingID,
                                   bool BuildingDestroyed)
{
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    if (!Bldg || !Bldg->IsResearching) return false;

    const auto ResearchId = static_cast<Game::Systems::ResearchId>(Bldg->ResearchingID);
    auto* Player = EM.GetComponent<PlayerComponent>(BuildingID);
    const int PlayerID = Bldg->ResearchPlayer >= 0
        ? Bldg->ResearchPlayer
        : (Player ? Player->PlayerID : 0);
    int MineralCost = 0;
    int GasCost = 0;
    if (GetResearchCost(EM, ResearchId, PlayerID, MineralCost, GasCost))
    {
        auto& Resources = HarvestSystem::GetResources(EM, PlayerID);
        const int RefundNumerator = BuildingDestroyed ? 3 : 4;
        Resources.Minerals += MineralCost * RefundNumerator / 4;
        Resources.VespeneGas += GasCost * RefundNumerator / 4;
    }
    if (PlayerID >= 0 && PlayerID < kPlayerResourceSlots &&
        ResearchId > ResearchId::None && ResearchId < ResearchId::Count)
    {
        ResolveUpgradeStore(EM).Researching[(size_t)PlayerID][static_cast<size_t>(ResearchId)] = false;
    }

    Bldg->BuildTimeRemaining = 0.0f;
    Bldg->IsResearching = false;
    Bldg->ResearchingID = -1;
    Bldg->ResearchPlayer = -1;
    return true;
}

int UpgradeSystem::GetResearchLevel(const Core::ECS::EntityManager& EM, int PlayerID, ResearchId ResearchId)
{
    if (ResearchId == ResearchId::None)
        return 0;
    return GetUpgrades(EM, PlayerID).Levels[ToIndex(ResearchId)];
}

bool UpgradeSystem::HasResearch(const Core::ECS::EntityManager& EM, int PlayerID, ResearchId ResearchId)
{
    return GetResearchLevel(EM, PlayerID, ResearchId) > 0;
}

int UpgradeSystem::GetResearchLevel(int PlayerID, ResearchId ResearchId)
{
    if (ResearchId == ResearchId::None)
        return 0;
    return GetUpgrades(PlayerID).Levels[ToIndex(ResearchId)];
}

bool UpgradeSystem::HasResearch(int PlayerID, ResearchId ResearchId)
{
    return GetResearchLevel(PlayerID, ResearchId) > 0;
}

bool UpgradeSystem::IsUpgradeType(ResearchId ResearchId)
{
    switch (ResearchId)
    {
        case ResearchId::Terran_InfantryWeapons:
        case ResearchId::Terran_InfantryArmor:
        case ResearchId::Terran_VehicleWeapons:
        case ResearchId::Terran_VehicleArmor:
        case ResearchId::Terran_ShipWeapons:
        case ResearchId::Terran_ShipArmor:
        case ResearchId::Terran_U238Shells:
        case ResearchId::Terran_IonThrusters:
        case ResearchId::Terran_MoebiusReactor:
        case ResearchId::Terran_OcularImplants:
        case ResearchId::Terran_ApolloReactor:
        case ResearchId::Terran_TitanReactor:
        case ResearchId::Terran_CaduceusReactor:
        case ResearchId::Terran_ColossusReactor:
        case ResearchId::Terran_CharonBoosters:
        case ResearchId::Zerg_MeleeAttacks:
        case ResearchId::Zerg_MissileAttacks:
        case ResearchId::Zerg_Carapace:
        case ResearchId::Zerg_FlyerAttacks:
        case ResearchId::Zerg_FlyerCarapace:
        case ResearchId::Zerg_AdrenalGlands:
        case ResearchId::Zerg_MetabolicBoost:
        case ResearchId::Zerg_AnabolicSynthesis:
        case ResearchId::Zerg_ChitinousPlating:
        case ResearchId::Zerg_MuscularAugments:
        case ResearchId::Zerg_GroovedSpines:
        case ResearchId::Zerg_VentralSacs:
        case ResearchId::Zerg_Antennae:
        case ResearchId::Zerg_PneumatizedCarapace:
        case ResearchId::Zerg_QueenEnergy:
        case ResearchId::Zerg_DefilerEnergy:
        case ResearchId::Protoss_GroundWeapons:
        case ResearchId::Protoss_GroundArmor:
        case ResearchId::Protoss_PlasmaShields:
        case ResearchId::Protoss_AirWeapons:
        case ResearchId::Protoss_AirArmor:
        case ResearchId::Protoss_SingularityCharge:
        case ResearchId::Protoss_GraviticDrive:
        case ResearchId::Protoss_ScarabDamage:
        case ResearchId::Protoss_ZealotSpeed:
        case ResearchId::Protoss_SensorArray:
        case ResearchId::Protoss_ApialSensors:
        case ResearchId::Protoss_KhaydarinAmulet:
        case ResearchId::Protoss_ArgusJewel:
        case ResearchId::Protoss_ArgusTalisman:
        case ResearchId::Protoss_KhaydarinCore:
        case ResearchId::Protoss_ReaverCapacity:
        case ResearchId::Protoss_CarrierCapacity:
        case ResearchId::Protoss_GraviticBoosters:
        case ResearchId::Protoss_GraviticThrusters:
            return true;
        default:
            return false;
    }
}

std::string_view UpgradeSystem::GetResearchDisplayName(ResearchId ResearchId)
{
    if (const ResearchDef* Research = FindResearchDef(ResearchId))
        return Research->Name;
    return "Unknown Research";
}

int UpgradeSystem::GetResearchTimeCost(ResearchId ResearchId, int PlayerID)
{
    const ResearchDef* Research = FindResearchDef(ResearchId);
    if (!Research)
        return 0;

    const int CurrentLevel = GetResearchLevel(PlayerID, ResearchId);
    const int Frames = GetResearchTimeFrames(*Research, CurrentLevel);
    return (Frames + kBroodWarLogicFramesPerSecond - 1) /
           kBroodWarLogicFramesPerSecond;
}

bool UpgradeSystem::GetResearchCost(const Core::ECS::EntityManager& EM,
                                    ResearchId ResearchId,
                                    int PlayerID,
                                    int& OutMinerals,
                                    int& OutGas)
{
    const ResearchDef* Research = FindResearchDef(ResearchId);
    if (!Research)
    {
        OutMinerals = 0;
        OutGas = 0;
        return false;
    }

    const int CurrentLevel = GetResearchLevel(EM, PlayerID, ResearchId);
    OutMinerals = Research->MineralCost + CurrentLevel * Research->MineralStep;
    OutGas = Research->GasCost + CurrentLevel * Research->GasStep;
    return true;
}

bool UpgradeSystem::GetResearchCost(ResearchId ResearchId,
                                    int PlayerID,
                                    int& OutMinerals,
                                    int& OutGas)
{
    const ResearchDef* Research = FindResearchDef(ResearchId);
    if (!Research)
    {
        OutMinerals = 0;
        OutGas = 0;
        return false;
    }

    const int CurrentLevel = GetResearchLevel(PlayerID, ResearchId);
    OutMinerals = Research->MineralCost + CurrentLevel * Research->MineralStep;
    OutGas = Research->GasCost + CurrentLevel * Research->GasStep;
    return true;
}

int UpgradeSystem::GetWeaponBonus(const Core::ECS::EntityManager& EM, int EntityID)
{
    return GetWeaponBonusForTarget(EM, EntityID, false);
}

int UpgradeSystem::GetWeaponBonusForTarget(const Core::ECS::EntityManager& EM,
                                           int EntityID,
                                           bool TargetIsAir)
{
    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    if (const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        Unit && Unit->ChkUnitID == 30 && !TargetIsAir)
    {
        return 5 * GetResearchLevel(
            EM, PlayerID, ResearchId::Terran_VehicleWeapons);
    }
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0) return 0;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.IsBuilding)
        return 0;

    const int DamageBonusPerLevel = TargetIsAir
        ? Def.AirDamageBonus
        : Def.GroundDamageBonus;
    if (DamageBonusPerLevel <= 0)
        return 0;

    ResearchId WeaponUpgrade = ResearchId::None;

    if (Def.RaceType == Race::Terran)
    {
        if (IsTerranInfantry(DefID))
            WeaponUpgrade = ResearchId::Terran_InfantryWeapons;
        else if (IsTerranVehicle(DefID))
            WeaponUpgrade = ResearchId::Terran_VehicleWeapons;
        else if (IsTerranShip(DefID))
            WeaponUpgrade = ResearchId::Terran_ShipWeapons;
    }
    else if (Def.RaceType == Race::Zerg)
    {
        if (IsZergMelee(DefID))
            WeaponUpgrade = ResearchId::Zerg_MeleeAttacks;
        else if (IsZergFlyer(DefID))
            WeaponUpgrade = ResearchId::Zerg_FlyerAttacks;
        else
            WeaponUpgrade = ResearchId::Zerg_MissileAttacks;
    }
    else if (Def.RaceType == Race::Protoss)
    {
        if (!TargetIsAir && Def.Name == "Reaver")
            WeaponUpgrade = ResearchId::Protoss_ScarabDamage;
        else if (IsProtossGround(DefID))
            WeaponUpgrade = ResearchId::Protoss_GroundWeapons;
        else if (IsProtossAir(DefID))
            WeaponUpgrade = ResearchId::Protoss_AirWeapons;
    }

    if (WeaponUpgrade == ResearchId::None)
        return 0;
    return DamageBonusPerLevel * GetResearchLevel(EM, PlayerID, WeaponUpgrade);
}

int UpgradeSystem::GetArmorBonus(const Core::ECS::EntityManager& EM, int EntityID)
{
    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0) return 0;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.IsBuilding)
        return 0;

    if (Def.RaceType == Race::Terran)
    {
        if (IsTerranInfantry(DefID))
            return GetResearchLevel(EM, PlayerID, ResearchId::Terran_InfantryArmor);
        if (IsTerranVehicle(DefID))
            return GetResearchLevel(EM, PlayerID, ResearchId::Terran_VehicleArmor);
        if (IsTerranShip(DefID))
            return GetResearchLevel(EM, PlayerID, ResearchId::Terran_ShipArmor);
    }
    else if (Def.RaceType == Race::Zerg)
    {
        if (IsZergFlyer(DefID))
            return GetResearchLevel(EM, PlayerID, ResearchId::Zerg_FlyerCarapace);

        if (!IsZergGround(DefID))
            return 0;
        int Bonus = GetResearchLevel(EM, PlayerID, ResearchId::Zerg_Carapace);
        if (Def.Name == "Ultralisk" &&
            HasResearch(EM, PlayerID, ResearchId::Zerg_ChitinousPlating))
        {
            Bonus += 2;
        }
        return Bonus;
    }
    else if (Def.RaceType == Race::Protoss)
    {
        if (IsProtossGround(DefID))
            return GetResearchLevel(EM, PlayerID, ResearchId::Protoss_GroundArmor);
        if (IsProtossAir(DefID))
            return GetResearchLevel(EM, PlayerID, ResearchId::Protoss_AirArmor);
    }
    return 0;
}

int UpgradeSystem::GetRangeBonusPixels(const Core::ECS::EntityManager& EM, int EntityID)
{
    return GetRangeBonusPixelsForTarget(EM, EntityID, false);
}

int UpgradeSystem::GetRangeBonusPixelsForTarget(const Core::ECS::EntityManager& EM,
                                                int EntityID,
                                                bool TargetIsAir)
{
    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 0;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if ((Def.Name == "Marine" || Def.Name == "JimRaynorMarine") &&
        HasResearch(EM, PlayerID, ResearchId::Terran_U238Shells))
    {
        return 32;
    }
    if (Def.Name == "Dragoon" &&
        HasResearch(EM, PlayerID, ResearchId::Protoss_SingularityCharge))
    {
        return 64;
    }
    if (Def.Name == "Hydralisk" &&
        HasResearch(EM, PlayerID, ResearchId::Zerg_GroovedSpines))
    {
        return 32;
    }
    if (TargetIsAir &&
        Def.Name == "Goliath" &&
        HasResearch(EM, PlayerID, ResearchId::Terran_CharonBoosters))
    {
        return 96;
    }

    return 0;
}

int UpgradeSystem::GetSightRangeBonusTiles(const Core::ECS::EntityManager& EM, int EntityID)
{
    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 0;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.Name == "Ghost" &&
        HasResearch(EM, PlayerID, ResearchId::Terran_OcularImplants))
    {
        return std::max(0, 11 - Def.SightRange);
    }
    if (Def.Name == "Overlord" &&
        HasResearch(EM, PlayerID, ResearchId::Zerg_Antennae))
    {
        return std::max(0, 11 - Def.SightRange);
    }
    if (Def.Name == "Observer" &&
        HasResearch(EM, PlayerID, ResearchId::Protoss_SensorArray))
    {
        return std::max(0, 11 - Def.SightRange);
    }
    if (Def.Name == "Scout" &&
        HasResearch(EM, PlayerID, ResearchId::Protoss_ApialSensors))
    {
        return std::max(0, 11 - Def.SightRange);
    }

    return 0;
}

float UpgradeSystem::GetMovementSpeedMultiplier(const Core::ECS::EntityManager& EM,
                                                int EntityID)
{
    const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Unit)
        return 1.0f;

    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;

    switch (Unit->ChkUnitID)
    {
        case 2:  // Vulture
        case 19: // JimRaynorVulture
            return HasResearch(EM, PlayerID, ResearchId::Terran_IonThrusters)
                ? (2560.0f / 1707.0f) : 1.0f;
        case 37: // Zergling
            return HasResearch(EM, PlayerID, ResearchId::Zerg_MetabolicBoost) ? 1.5f : 1.0f;
        case 38: // Hydralisk
            return HasResearch(EM, PlayerID, ResearchId::Zerg_MuscularAugments) ? 1.5f : 1.0f;
        case 39: // Ultralisk
            return HasResearch(EM, PlayerID, ResearchId::Zerg_AnabolicSynthesis) ? 1.5f : 1.0f;
        case 42: // Overlord
            return HasResearch(EM, PlayerID, ResearchId::Zerg_PneumatizedCarapace)
                ? (853.0f / 213.0f) : 1.0f;
        case 65: // Zealot
            return HasResearch(EM, PlayerID, ResearchId::Protoss_ZealotSpeed) ? 1.5f : 1.0f;
        case 69: // Shuttle
            return HasResearch(EM, PlayerID, ResearchId::Protoss_GraviticDrive)
                ? (1699.0f / 1133.0f) : 1.0f;
        case 70: // Scout
            return HasResearch(EM, PlayerID, ResearchId::Protoss_GraviticThrusters)
                ? (1707.0f / 1280.0f) : 1.0f;
        case 84: // Observer
            return HasResearch(EM, PlayerID, ResearchId::Protoss_GraviticBoosters)
                ? (1279.0f / 853.0f) : 1.0f;
        default:
            return 1.0f;
    }
}

float UpgradeSystem::GetAttackCooldownMultiplier(const Core::ECS::EntityManager& EM,
                                                 int EntityID)
{
    const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Unit || Unit->ChkUnitID != 37)
        return 1.0f;

    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    return HasResearch(EM, PlayerID, ResearchId::Zerg_AdrenalGlands) ? 0.5f : 1.0f;
}

int UpgradeSystem::GetEffectiveMaxEnergyPoints(const Core::ECS::EntityManager& EM,
                                               int EntityID)
{
    const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    if (!Unit)
        return 0;

    const int DefID = ResolveDefID(EM, EntityID);
    int BaseCap = std::max(0, Unit->MaxEnergyPoints);
    if (DefID >= 0)
    {
        const auto& Def = Game::Races::GetUnitDefByID(DefID);
        const ResearchId EnergyUpgrade = GetEnergyUpgradeForDef(Def);
        if (EnergyUpgrade != ResearchId::None)
        {
            const int PlayerID = Player ? Player->PlayerID : 0;
            return HasResearch(EM, PlayerID, EnergyUpgrade) ? 250 : 200;
        }
        BaseCap = std::max(BaseCap, Def.EnergyPoints);
    }
    return BaseCap;
}

void UpgradeSystem::RefreshUnitEnergyCap(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Unit)
        return;

    const int EffectiveMax = GetEffectiveMaxEnergyPoints(EM, EntityID);
    if (EffectiveMax <= 0)
        return;

    Unit->MaxEnergyPoints = EffectiveMax;
    Unit->EnergyPoints = std::min(Unit->EnergyPoints, EffectiveMax);
}

int UpgradeSystem::GetMaxHangarCount(const Core::ECS::EntityManager& EM, int EntityID)
{
    const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 0;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.Name == "Carrier")
    {
        return HasResearch(EM, PlayerID, ResearchId::Protoss_CarrierCapacity) ? 8 : 4;
    }
    if (Def.Name == "Reaver")
    {
        return HasResearch(EM, PlayerID, ResearchId::Protoss_ReaverCapacity) ? 10 : 5;
    }
    return 0;
}

} // namespace Game::Systems
