#include "Game/Systems/UpgradeSystem.h"

#include <algorithm>
#include <string>

#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/HarvestSystem.h"

namespace Game::Systems
{

namespace
{
    using Game::Components::BuildingComponent;
    using Game::Components::PlayerComponent;
    using Game::Components::UnitComponent;

    struct ResearchDef
    {
        TerranResearchId Id = TerranResearchId::None;
        const char* Name = "";
        const char* ProducerName = "";
        int MineralCost = 0;
        int GasCost = 0;
        int MineralStep = 0;
        int GasStep = 0;
        int MaxLevel = 1;
    };

    PlayerUpgrades gUpgrades[8];

    constexpr ResearchDef kResearchDefs[] =
    {
        {TerranResearchId::InfantryWeapons, "Terran Infantry Weapons", "EngineeringBay", 100, 100, 75, 75, 3},
        {TerranResearchId::InfantryArmor,   "Terran Infantry Armor",   "EngineeringBay", 100, 100, 75, 75, 3},
        {TerranResearchId::VehicleWeapons,  "Terran Vehicle Weapons",  "Armory",         100, 100, 75, 75, 3},
        {TerranResearchId::VehicleArmor,    "Terran Vehicle Armor",    "Armory",         100, 100, 75, 75, 3},
        {TerranResearchId::ShipWeapons,     "Terran Ship Weapons",     "Armory",         100, 100, 75, 75, 3},
        {TerranResearchId::ShipArmor,       "Terran Ship Armor",       "Armory",         100, 100, 75, 75, 3},
        {TerranResearchId::StimPacks,       "Stim Packs",              "Academy",        100, 100, 0,  0,  1},
        {TerranResearchId::U238Shells,      "U-238 Shells",            "Academy",        150, 150, 0,  0,  1},
        {TerranResearchId::SiegeMode,       "Siege Mode",              "MachineShop",    150, 150, 0,  0,  1},
        {TerranResearchId::SpiderMines,     "Spider Mines",            "MachineShop",    100, 100, 0,  0,  1},
        {TerranResearchId::CloakingField,   "Cloaking Field",          "ControlTower",   150, 150, 0,  0,  1},
        {TerranResearchId::IonThrusters,    "Ion Thrusters",           "ControlTower",   100, 100, 0,  0,  1},
        {TerranResearchId::Lockdown,        "Lockdown",                "CovertOps",      200, 200, 0,  0,  1},
        {TerranResearchId::MoebiusReactor,  "Moebius Reactor",         "CovertOps",      100, 100, 0,  0,  1},
        {TerranResearchId::YamatoGun,       "Yamato Gun",              "PhysicsLab",     100, 100, 0,  0,  1},
        {TerranResearchId::ColossusReactor, "Colossus Reactor",        "PhysicsLab",     150, 150, 0,  0,  1},
    };

    void SetReason(std::string* OutReason, const std::string& Reason)
    {
        if (OutReason)
            *OutReason = Reason;
    }

    size_t ToIndex(TerranResearchId ResearchId)
    {
        return static_cast<size_t>(ResearchId);
    }

    const ResearchDef* FindResearchDef(TerranResearchId ResearchId)
    {
        for (const auto& Def : kResearchDefs)
        {
            if (Def.Id == ResearchId)
                return &Def;
        }
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
            return Game::Races::GetTerranDefIDByChkUnitID(Unit->ChkUnitID);
        return -1;
    }

    bool IsTerranInfantry(int DefID)
    {
        const int Marine = Game::Races::GetUnitDefID("Marine");
        const int Firebat = Game::Races::GetUnitDefID("Firebat");
        const int Medic = Game::Races::GetUnitDefID("Medic");
        const int Ghost = Game::Races::GetUnitDefID("Ghost");
        return DefID == Marine || DefID == Firebat || DefID == Medic || DefID == Ghost;
    }

    bool IsTerranVehicle(int DefID)
    {
        const int Vulture = Game::Races::GetUnitDefID("Vulture");
        const int Goliath = Game::Races::GetUnitDefID("Goliath");
        const int SiegeTank = Game::Races::GetUnitDefID("SiegeTank");
        return DefID == Vulture || DefID == Goliath || DefID == SiegeTank;
    }

    bool IsTerranShip(int DefID)
    {
        const int Wraith = Game::Races::GetUnitDefID("Wraith");
        const int Dropship = Game::Races::GetUnitDefID("Dropship");
        const int ScienceVessel = Game::Races::GetUnitDefID("ScienceVessel");
        const int Battlecruiser = Game::Races::GetUnitDefID("Battlecruiser");
        const int Valkyrie = Game::Races::GetUnitDefID("Valkyrie");
        return DefID == Wraith || DefID == Dropship || DefID == ScienceVessel ||
               DefID == Battlecruiser || DefID == Valkyrie;
    }
}

UpgradeSystem::UpgradeSystem(Core::ECS::SystemManager& SysMgr)
    : mSystemMgr(SysMgr)
{
    Core::Utils::Logger::Info("UpgradeSystem", "Initialized");
}

PlayerUpgrades& UpgradeSystem::GetUpgrades(int PlayerID)
{
    return gUpgrades[PlayerID];
}

void UpgradeSystem::Update(float DeltaTime)
{
    (void)DeltaTime;
}

bool UpgradeSystem::CanStartResearch(const Core::ECS::EntityManager& EM,
                                     int BuildingID,
                                     TerranResearchId ResearchId,
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
        SetReason(OutReason, "research producer is still under construction");
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
        SetReason(OutReason, "research already complete");
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
                                  TerranResearchId ResearchId)
{
    std::string Reason;
    if (!CanStartResearch(EM, BuildingID, ResearchId, &Reason))
    {
        Core::Utils::Logger::Debug("UpgradeSystem", "Cannot research: " + Reason);
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

    auto& Upgrades = GetUpgrades(PlayerID);
    Upgrades.Levels[ToIndex(ResearchId)] =
        static_cast<uint8_t>(CurrentLevel + 1);

    Core::Utils::Logger::Info(
        "UpgradeSystem",
        "Player " + std::to_string(PlayerID) + " completed " +
        std::string(Research->Name) + " level " +
        std::to_string(CurrentLevel + 1));
    return true;
}

int UpgradeSystem::GetResearchLevel(int PlayerID, TerranResearchId ResearchId)
{
    if (ResearchId == TerranResearchId::None)
        return 0;
    return GetUpgrades(PlayerID).Levels[ToIndex(ResearchId)];
}

bool UpgradeSystem::HasResearch(int PlayerID, TerranResearchId ResearchId)
{
    return GetResearchLevel(PlayerID, ResearchId) > 0;
}

std::string_view UpgradeSystem::GetResearchDisplayName(TerranResearchId ResearchId)
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
    if (DefID < 0)
        return 0;

    if (IsTerranInfantry(DefID))
        return GetResearchLevel(PlayerID, TerranResearchId::InfantryWeapons);
    if (IsTerranVehicle(DefID))
        return GetResearchLevel(PlayerID, TerranResearchId::VehicleWeapons);
    if (IsTerranShip(DefID))
        return GetResearchLevel(PlayerID, TerranResearchId::ShipWeapons);
    return 0;
}

int UpgradeSystem::GetArmorBonus(const Core::ECS::EntityManager& EM, int EntityID)
{
    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(EM);
    auto* Player = MutableEM.GetComponent<PlayerComponent>(EntityID);
    const int PlayerID = Player ? Player->PlayerID : 0;
    const int DefID = ResolveDefID(EM, EntityID);
    if (DefID < 0)
        return 0;

    if (IsTerranInfantry(DefID))
        return GetResearchLevel(PlayerID, TerranResearchId::InfantryArmor);
    if (IsTerranVehicle(DefID))
        return GetResearchLevel(PlayerID, TerranResearchId::VehicleArmor);
    if (IsTerranShip(DefID))
        return GetResearchLevel(PlayerID, TerranResearchId::ShipArmor);
    return 0;
}

} // namespace Game::Systems
