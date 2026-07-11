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

    PlayerUpgrades gUpgrades[kPlayerResourceSlots];
    std::array<std::array<bool, static_cast<size_t>(ResearchId::Count)>,
               kPlayerResourceSlots> gResearching{};

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
        {ResearchId::Terran_OcularImplants,  "Ocular Implants",  "CovertOps",      100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Terran_ApolloReactor,   "Apollo Reactor",   "ControlTower",   200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Terran_TitanReactor,    "Titan Reactor",    "ScienceFacility",150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Terran_CaduceusReactor, "Caduceus Reactor", "Academy",        150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Terran_YamatoGun,       "Yamato Gun",       "PhysicsLab",     100, 100, 0,  0,  1, 40, 0},
        {ResearchId::Terran_ColossusReactor, "Colossus Reactor", "PhysicsLab",     150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Terran_CharonBoosters,  "Charon Boosters",  "MachineShop",    100, 100, 0,  0,  1, 40, 0},

        // Zerg
        {ResearchId::Zerg_MeleeAttacks,      "Melee Attacks",      "Evolution Chamber", 100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_MissileAttacks,    "Missile Attacks",    "Evolution Chamber", 100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_Carapace,          "Carapace",           "Evolution Chamber", 150, 150, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_FlyerAttacks,      "Flyer Attacks",      "Spire",            100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_FlyerCarapace,     "Flyer Carapace",     "Spire",            150, 150, 75, 75, 3, 30, 10},
        {ResearchId::Zerg_AdrenalGlands,     "Adrenal Glands",     "Spawning Pool",    200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Zerg_MetabolicBoost,    "Metabolic Boost",    "Spawning Pool",    100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_AnabolicSynthesis, "Anabolic Synthesis", "Ultralisk Cavern", 200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Zerg_ChitinousPlating,  "Chitinous Plating",  "Ultralisk Cavern", 150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Zerg_MuscularAugments,  "Muscular Augments",  "Hydralisk Den",    150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_GroovedSpines,     "Grooved Spines",     "Hydralisk Den",    150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_VentralSacs,       "Ventral Sacs",       "Lair",             200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Zerg_Antennae,          "Antennae",           "Lair",             150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_PneumatizedCarapace,"Pneumatized Carapace","Lair",           150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_Burrow,            "Burrow",             "Lair",             100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_Consume,           "Consume",            "Defiler Mound",    100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_Ensnare,           "Ensnare",            "Queen's Nest",     100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_Plague,            "Plague",             "Defiler Mound",    200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Zerg_SpawnBroodling,    "Spawn Broodling",    "Queen's Nest",     100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_QueenEnergy,       "Gamete Meiosis",     "Queen's Nest",     150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_DefilerEnergy,     "Metasynaptic Node",  "Defiler Mound",    150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Zerg_LurkerMorph,       "Lurker Aspect",      "Hydralisk Den",    200, 200, 0,  0,  1, 45, 0},

        // Protoss
        {ResearchId::Protoss_GroundWeapons,   "Ground Weapons",    "Forge",            100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Protoss_GroundArmor,     "Ground Armor",      "Forge",            100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Protoss_PlasmaShields,   "Plasma Shields",    "Forge",            200, 200, 100,100,3, 30, 10},
        {ResearchId::Protoss_AirWeapons,      "Air Weapons",       "Cybernetics Core", 100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Protoss_AirArmor,        "Air Armor",         "Cybernetics Core", 100, 100, 75, 75, 3, 30, 10},
        {ResearchId::Protoss_SingularityCharge,"Singularity Charge","Cybernetics Core",150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_GraviticDrive,   "Gravitic Drive",    "Robotics Support Bay",100,100,0,  0,  1, 30, 0},
        {ResearchId::Protoss_ScarabDamage,    "Scarab Damage",     "Robotics Support Bay",200,200,0,  0,  1, 40, 0},
        {ResearchId::Protoss_ZealotSpeed,     "Leg Enhancement",   "Citadel of Adun",  150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_SensorArray,     "Sensor Array",      "Observatory",      150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_ApialSensors,    "Apial Sensors",     "Fleet Beacon",     100, 100, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_KhaydarinAmulet, "Khaydarin Amulet",  "Templar Archives", 150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_ArgusJewel,      "Argus Jewel",       "Fleet Beacon",     150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_ArgusTalisman,   "Argus Talisman",    "Templar Archives", 150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_KhaydarinCore,   "Khaydarin Core",    "Arbiter Tribunal", 150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_PsionicStorm,    "Psionic Storm",     "Templar Archives", 200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_Hallucination,   "Hallucination",     "Templar Archives", 200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_Recall,          "Recall",            "Arbiter Tribunal", 150, 150, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_StasisField,     "Stasis Field",      "Arbiter Tribunal", 150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_MindControl,     "Mind Control",      "Templar Archives", 200, 200, 0,  0,  1, 40, 0},
        {ResearchId::Protoss_Maelstrom,       "Maelstrom",         "Templar Archives", 150, 150, 0,  0,  1, 30, 0},
        {ResearchId::Protoss_ReaverCapacity,  "Reaver Capacity",   "Robotics Support Bay",200,200,0,  0,  1, 30, 0},
        {ResearchId::Protoss_CarrierCapacity, "Carrier Capacity",  "Fleet Beacon",     200, 200, 0,  0,  1, 30, 0},
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

PlayerUpgrades& UpgradeSystem::GetUpgrades(int PlayerID)
{
    if (PlayerID < 0) PlayerID = 0;
    if (PlayerID >= kPlayerResourceSlots) PlayerID = kPlayerResourceSlots - 1;
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
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (Bldg->UnderConstruction || !Unit || Unit->HitPoints <= 0)
            continue;

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
                    gResearching[(size_t)PlayerID][(size_t)Rid] = false;
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
    if (Building->IsResearching)
    {
        SetReason(OutReason, "building is already researching");
        return false;
    }
    auto* ProducerUnit = MutableEM.GetComponent<UnitComponent>(BuildingID);
    if (!ProducerUnit || ProducerUnit->HitPoints <= 0)
    {
        SetReason(OutReason, "research producer is not operational");
        return false;
    }
    if (!MutableEM.GetComponent<Game::Components::TransformComponent>(BuildingID))
    {
        SetReason(OutReason, "research producer missing transform");
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
    if (PlayerID >= 0 && PlayerID < kPlayerResourceSlots &&
        gResearching[(size_t)PlayerID][static_cast<size_t>(ResearchId)])
    {
        SetReason(OutReason, "research already in progress for player");
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
    if (PlayerID >= 0 && PlayerID < kPlayerResourceSlots)
        gResearching[(size_t)PlayerID][static_cast<size_t>(ResearchId)] = true;

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
    if (GetResearchCost(ResearchId, PlayerID, MineralCost, GasCost))
    {
        auto& Resources = HarvestSystem::GetResources(PlayerID);
        const int RefundNumerator = BuildingDestroyed ? 3 : 4;
        Resources.Minerals += MineralCost * RefundNumerator / 4;
        Resources.VespeneGas += GasCost * RefundNumerator / 4;
    }
    if (PlayerID >= 0 && PlayerID < kPlayerResourceSlots &&
        ResearchId > ResearchId::None && ResearchId < ResearchId::Count)
    {
        gResearching[(size_t)PlayerID][static_cast<size_t>(ResearchId)] = false;
    }

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
    return Research->TimeCost + CurrentLevel * Research->TimeStep;
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

        int Bonus = GetResearchLevel(PlayerID, ResearchId::Zerg_Carapace);
        const auto& Def = Game::Races::GetUnitDefByID(DefID);
        if (Def.Name == "Ultralisk" &&
            HasResearch(PlayerID, ResearchId::Zerg_ChitinousPlating))
        {
            Bonus += 2;
        }
        return Bonus;
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

int UpgradeSystem::GetRangeBonusPixels(const Core::ECS::EntityManager& EM, int EntityID)
{
    return GetRangeBonusPixelsForTarget(EM, EntityID, false);
}

int UpgradeSystem::GetRangeBonusPixelsForTarget(const Core::ECS::EntityManager& EM,
                                                int EntityID,
                                                bool TargetIsAir)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 0;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if ((Def.Name == "Marine" || Def.Name == "JimRaynorMarine") &&
        HasResearch(PlayerID, ResearchId::Terran_U238Shells))
    {
        return 32;
    }
    if (Def.Name == "Dragoon" &&
        HasResearch(PlayerID, ResearchId::Protoss_SingularityCharge))
    {
        return 64;
    }
    if (Def.Name == "Hydralisk" &&
        HasResearch(PlayerID, ResearchId::Zerg_GroovedSpines))
    {
        return 32;
    }
    if (TargetIsAir &&
        Def.Name == "Goliath" &&
        HasResearch(PlayerID, ResearchId::Terran_CharonBoosters))
    {
        return 96;
    }

    return 0;
}

int UpgradeSystem::GetSightRangeBonusTiles(const Core::ECS::EntityManager& EM, int EntityID)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 0;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.Name == "Ghost" &&
        HasResearch(PlayerID, ResearchId::Terran_OcularImplants))
    {
        return std::max(0, 11 - Def.SightRange);
    }
    if (Def.Name == "Overlord" &&
        HasResearch(PlayerID, ResearchId::Zerg_Antennae))
    {
        return std::max(0, 11 - Def.SightRange);
    }
    if (Def.Name == "Observer" &&
        HasResearch(PlayerID, ResearchId::Protoss_SensorArray))
    {
        return std::max(0, 11 - Def.SightRange);
    }
    if (Def.Name == "Scout" &&
        HasResearch(PlayerID, ResearchId::Protoss_ApialSensors))
    {
        return std::max(0, 11 - Def.SightRange);
    }

    return 0;
}

float UpgradeSystem::GetMovementSpeedMultiplier(const Core::ECS::EntityManager& EM,
                                                int EntityID)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 1.0f;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.Name == "Vulture" &&
        HasResearch(PlayerID, ResearchId::Terran_IonThrusters))
    {
        return 1.5f;
    }
    if (Def.Name == "Zergling" &&
        HasResearch(PlayerID, ResearchId::Zerg_MetabolicBoost))
    {
        return 1.5f;
    }
    if (Def.Name == "Hydralisk" &&
        HasResearch(PlayerID, ResearchId::Zerg_MuscularAugments))
    {
        return 1.5f;
    }
    if (Def.Name == "Ultralisk" &&
        HasResearch(PlayerID, ResearchId::Zerg_AnabolicSynthesis))
    {
        return 1.5f;
    }
    if (Def.Name == "Zealot" &&
        HasResearch(PlayerID, ResearchId::Protoss_ZealotSpeed))
    {
        return 1.5f;
    }

    return 1.0f;
}

float UpgradeSystem::GetAttackCooldownMultiplier(const Core::ECS::EntityManager& EM,
                                                 int EntityID)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 1.0f;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.Name == "Zergling" &&
        HasResearch(PlayerID, ResearchId::Zerg_AdrenalGlands))
    {
        return 0.5f;
    }

    return 1.0f;
}

int UpgradeSystem::GetEffectiveMaxEnergyPoints(const Core::ECS::EntityManager& EM,
                                               int EntityID)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Unit = MutableEM.GetComponent<UnitComponent>(EntityID);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
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
            return HasResearch(PlayerID, EnergyUpgrade) ? 250 : 200;
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
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 0;

    const auto& Def = Game::Races::GetUnitDefByID(DefID);
    if (Def.Name == "Carrier")
    {
        return HasResearch(PlayerID, ResearchId::Protoss_CarrierCapacity) ? 8 : 4;
    }
    if (Def.Name == "Reaver")
    {
        return HasResearch(PlayerID, ResearchId::Protoss_ReaverCapacity) ? 10 : 5;
    }
    return 0;
}

} // namespace Game::Systems
