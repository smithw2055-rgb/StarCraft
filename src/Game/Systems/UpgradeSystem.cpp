#include "Game/Systems/UpgradeSystem.h"

#include <algorithm>
#include <string>

#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
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
        int TimeCost = 30;       // seconds per level
        int TimeStep = 10;       // extra time per additional level
    };

    PlayerUpgrades gUpgrades[8];

    constexpr ResearchDef kResearchDefs[] =
    {
        // Terran
        {ResearchId::Terran_InfantryWeapons, "Infantry Weapons", "EngineeringBay", 100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Terran_InfantryArmor,   "Infantry Armor",   "EngineeringBay", 100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Terran_VehicleWeapons,  "Vehicle Weapons",  "Armory",         100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Terran_VehicleArmor,    "Vehicle Armor",    "Armory",         100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Terran_ShipWeapons,     "Ship Weapons",     "Armory",         100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Terran_ShipArmor,       "Ship Armor",       "Armory",         100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Terran_StimPacks,       "Stim Packs",       "Academy",        100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Terran_U238Shells,      "U-238 Shells",     "Academy",        150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Terran_SiegeMode,       "Siege Mode",       "MachineShop",    150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Terran_SpiderMines,     "Spider Mines",     "MachineShop",    100, 100, 0,  0,  1, 40, 0},
        {ResearchId::Terran_CloakingField,   "Cloaking Field",   "ControlTower",   150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Terran_IonThrusters,    "Ion Thrusters",    "ControlTower",   100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Terran_Lockdown,        "Lockdown",         "CovertOps",      200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Terran_MoebiusReactor,  "Moebius Reactor",  "CovertOps",      100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Terran_YamatoGun,       "Yamato Gun",       "PhysicsLab",     100, 100, 0,  0,  1, 40, 0},
        {ResearchId::Terran_ColossusReactor, "Colossus Reactor", "PhysicsLab",     150, 150, 0,  0,  1, 40, 0},

        // Zerg
        {ResearchId::Zerg_MeleeAttacks,      "Melee Attacks",      "EvolutionChamber", 100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_MissileAttacks,    "Missile Attacks",    "EvolutionChamber", 100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_Carapace,          "Carapace",           "EvolutionChamber", 150, 150, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_FlyerAttacks,      "Flyer Attacks",      "Spire",            100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_FlyerCarapace,     "Flyer Carapace",     "Spire",            150, 150, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_AdrenalGlands,     "Adrenal Glands",     "SpawningPool",     200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Zerg_MetabolicBoost,    "Metabolic Boost",    "SpawningPool",     100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_VentralSacs,       "Ventral Sacs",       "Lair",             200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Zerg_Antennae,          "Antennae",           "Lair",             150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_PneumatizedCarapace,"Pneumatized Carapace","Lair",           150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_Burrow,            "Burrow",             "Lair",             100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_Consume,           "Consume",            "DefilerMound",     100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_Ensnare,           "Ensnare",            "QueenNest",        100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_Plague,            "Plague",             "DefilerMound",     200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Zerg_SpawnBroodling,    "Spawn Broodling",    "QueenNest",        100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_QueenEnergy,       "Gamete Meiosis",     "QueenNest",        150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_DefilerEnergy,     "Metasynaptic Node",  "DefilerMound",     150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_LurkerMorph,       "Lurker Aspect",      "HydraliskDen",     200, 200, 0,  0,  1, 45, 0},

        // Protoss
        {ResearchId::Protoss_GroundWeapons,   "Ground Weapons",    "Forge",            100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Protoss_GroundArmor,     "Ground Armor",      "Forge",            100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Protoss_PlasmaShields,   "Plasma Shields",    "Forge",            200, 200, 100,100,3, 30, 10},
        {ResearchId::Protoss_AirWeapons,      "Air Weapons",       "CyberneticsCore",  100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Protoss_AirArmor,        "Air Armor",         "CyberneticsCore",  100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Protoss_SingularityCharge,"Singularity Charge","CyberneticsCore",  150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_GraviticDrive,   "Gravitic Drive",    "RoboticsSupportBay",100,100, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_ScarabDamage,    "Scarab Damage",     "RoboticsSupportBay",200,200, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_ZealotSpeed,     "Leg Enhancement",   "CitadelOfAdun",    150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_KhaydarinAmulet, "Khaydarin Amulet",  "TemplarArchives",  150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_PsionicStorm,    "Psionic Storm",     "TemplarArchives",  200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_Hallucination,   "Hallucination",     "TemplarArchives",  200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_Recall,          "Recall",            "ArbiterTribunal",  150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_StasisField,     "Stasis Field",      "ArbiterTribunal",  150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_MindControl,     "Mind Control",      "TemplarArchives",  200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_Maelstrom,       "Maelstrom",         "TemplarArchives",  150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_ReaverCapacity,  "Reaver Capacity",   "RoboticsSupportBay",200,200, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_CarrierCapacity, "Carrier Capacity",  "FleetBeacon",      200, 200, 0,  0,  1, 30, 0},
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
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
        if (const auto* Building = MutableEM.GetComponent<BuildingComponent>(EntityID))
        {
            if (Building->BuildingDefID >= 0)
                return Building->BuildingDefID;
        }
        if (const auto* Unit = MutableEM.GetComponent<UnitComponent>(EntityID))
            return Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
    }

    int GetPlayerRace(const Core::ECS::EntityManager& EM, int EntityID)
    {
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
        if (auto* Unit = MutableEM.GetComponent<UnitComponent>(EntityID))
            return (int)Unit->UnitRace;
        return 0;
    }

    bool IsTerranInfantry(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Marine" || D.Name == "Firebat" || D.Name == "Medic" || D.Name == "Ghost";
    }

    bool IsTerranVehicle(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Vulture" || D.Name == "Goliath" || D.Name == "SiegeTank";
    }

    bool IsTerranShip(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Wraith" || D.Name == "Dropship" || D.Name == "ScienceVessel" ||
               D.Name == "Battlecruiser" || D.Name == "Valkyrie";
    }

    bool IsZergGround(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.RaceType == Race::Zerg && !D.Name.empty() && D.RaceType != Race{};
    }

    bool IsZergMelee(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Zergling" || D.Name == "Ultralisk" || D.Name == "Broodling";
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
        return D.RaceType == Race::Protoss && !D.Name.empty() &&
               D.Name != "Scout" && D.Name != "Corsair" && D.Name != "Carrier" &&
               D.Name != "Arbiter" && D.Name != "Observer" && D.Name != "Shuttle";
    }

    bool IsProtossAir(int DefID)
    {
        const auto& D = Game::Races::GetUnitDefByID(DefID);
        return D.Name == "Scout" || D.Name == "Corsair" || D.Name == "Carrier" ||
               D.Name == "Arbiter" || D.Name == "Shuttle";
    }
}

UpgradeSystem::UpgradeSystem(Core::ECS::SystemManager& SysMgr)
    : mSystemMgr(SysMgr)
{
    Logger::Info("UpgradeSystem", "Initialized (Terran + Zerg + Protoss)");
}

PlayerUpgrades& UpgradeSystem::GetUpgrades(int PlayerID)
{
    if (PlayerID < 0) PlayerID = 0;
    if (PlayerID >= 8) PlayerID = 7;
    return gUpgrades[PlayerID];
}

void UpgradeSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (auto ID : Buildings)
    {
        auto* Bldg = EM.GetComponent<BuildingComponent>(ID);
        if (!Bldg || !Bldg->IsResearching) continue;

        Bldg->BuildTimeRemaining -= DeltaTime;
        if (Bldg->BuildTimeRemaining <= 0.0f)
        {
            Bldg->BuildTimeRemaining = 0.0f;

            int PlayerID = Bldg->ResearchPlayer;
            int Rid = Bldg->ResearchingID;
            if (Rid >= 0 && PlayerID >= 0)
            {
                int CurrentLevel = GetResearchLevel(PlayerID, static_cast<ResearchId>(Rid));
                auto& Upgrades = GetUpgrades(PlayerID);
                Upgrades.Levels[static_cast<size_t>(Rid)] =
                    static_cast<uint8_t>(CurrentLevel + 1);

                const ResearchDef* Research = FindResearchDef(static_cast<ResearchId>(Rid));
                Logger::Info("UpgradeSystem",
                    "Player " + std::to_string(PlayerID) + " completed " +
                    (Research ? std::string(Research->Name) : "research") + " level " +
                    std::to_string(CurrentLevel + 1));
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

    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Building = MutableEM.GetComponent<BuildingComponent>(BuildingID);
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

    const int ProducerDefID = ResolveDefID(EM, BuildingID);
    if (ProducerDefID != Game::Races::GetUnitDefID(Research->ProducerName))
    {
        SetReason(OutReason, "wrong producer for research");
        return false;
    }

    const auto* Player = MutableEM.GetComponent<PlayerComponent>(BuildingID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int CurrentLevel = GetResearchLevel(PlayerID, ResearchId);
    if (CurrentLevel >= Research->MaxLevel)
    {
        SetReason(OutReason, "research already maxed");
        return false;
    }

    const int MineralCost = Research->MineralCost + CurrentLevel * Research->MineralStep;
    const int GasCost = Research->GasCost + CurrentLevel * Research->GasStep;
    auto& Resources = HarvestSystem::GetResources(PlayerID);
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

    const int CurrentLevel = GetResearchLevel(PlayerID, ResearchId);
    auto& Resources = HarvestSystem::GetResources(PlayerID);
    Resources.Minerals -= Research->MineralCost + CurrentLevel * Research->MineralStep;
    Resources.VespeneGas -= Research->GasCost + CurrentLevel * Research->GasStep;

    // Time-progress: use BuildingComponent's BuildTimeRemaining as research timer
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    if (Bldg)
    {
        Bldg->BuildTimeRemaining = (float)(Research->TimeCost + CurrentLevel * Research->TimeStep);
        Bldg->IsResearching = true;
        Bldg->ResearchingID = static_cast<int>(ResearchId);
        Bldg->ResearchPlayer = PlayerID;
    }

    Logger::Info("UpgradeSystem",
        "Player " + std::to_string(PlayerID) + " started " +
        std::string(Research->Name) + " level " +
        std::to_string(CurrentLevel + 1));
    return true;
}

bool UpgradeSystem::CancelResearch(Core::ECS::EntityManager& EM, int BuildingID)
{
    auto* Bldg = EM.GetComponent<BuildingComponent>(BuildingID);
    if (!Bldg || !Bldg->IsResearching) return false;

    Bldg->BuildTimeRemaining = 0.0f;
    Bldg->IsResearching = false;
    Bldg->ResearchingID = -1;
    Bldg->ResearchPlayer = -1;
    return true;
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

std::string_view UpgradeSystem::GetResearchDisplayName(ResearchId ResearchId)
{
    if (const ResearchDef* Research = FindResearchDef(ResearchId))
        return Research->Name;
    return "Unknown Research";
}

int UpgradeSystem::GetWeaponBonus(const Core::ECS::EntityManager& EM, int EntityID)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0) return 0;

    int RaceVal = GetPlayerRace(EM, EntityID);

    if (RaceVal == 0) // Terran
    {
        if (IsTerranInfantry(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Terran_InfantryWeapons);
        if (IsTerranVehicle(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Terran_VehicleWeapons);
        if (IsTerranShip(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Terran_ShipWeapons);
    }
    else if (RaceVal == 1) // Zerg
    {
        if (IsZergMelee(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Zerg_MeleeAttacks);
        if (IsZergFlyer(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Zerg_FlyerAttacks);
        // Default: missile attacks for other zerg
        return GetResearchLevel(PlayerID, ResearchId::Zerg_MissileAttacks);
    }
    else if (RaceVal == 2) // Protoss
    {
        if (IsProtossGround(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Protoss_GroundWeapons);
        if (IsProtossAir(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Protoss_AirWeapons);
    }
    return 0;
}

int UpgradeSystem::GetArmorBonus(const Core::ECS::EntityManager& EM, int EntityID)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0) return 0;

    int RaceVal = GetPlayerRace(EM, EntityID);

    if (RaceVal == 0) // Terran
    {
        if (IsTerranInfantry(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Terran_InfantryArmor);
        if (IsTerranVehicle(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Terran_VehicleArmor);
        if (IsTerranShip(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Terran_ShipArmor);
    }
    else if (RaceVal == 1) // Zerg
    {
        if (IsZergFlyer(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Zerg_FlyerCarapace);
        return GetResearchLevel(PlayerID, ResearchId::Zerg_Carapace);
    }
    else if (RaceVal == 2) // Protoss
    {
        if (IsProtossGround(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Protoss_GroundArmor);
        if (IsProtossAir(DefID))
            return GetResearchLevel(PlayerID, ResearchId::Protoss_AirArmor);
    }
    return 0;
}

} // namespace Game::Systems
