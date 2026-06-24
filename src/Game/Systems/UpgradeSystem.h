#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "Core/ECS/SystemManager.h"

namespace Game::Systems
{

enum class TerranResearchId : uint8_t
{
    None = 0,
    InfantryWeapons,
    InfantryArmor,
    VehicleWeapons,
    VehicleArmor,
    ShipWeapons,
    ShipArmor,
    StimPacks,
    U238Shells,
    SiegeMode,
    SpiderMines,
    CloakingField,
    IonThrusters,
    Lockdown,
    MoebiusReactor,
    YamatoGun,
    ColossusReactor,
    Count
};

struct PlayerUpgrades
{
    std::array<uint8_t, static_cast<size_t>(TerranResearchId::Count)> Levels = {};
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
                                 TerranResearchId ResearchId,
                                 std::string* OutReason = nullptr);

    static bool StartResearch(Core::ECS::EntityManager& EM,
                              int BuildingID,
                              TerranResearchId ResearchId);

    static int GetResearchLevel(int PlayerID, TerranResearchId ResearchId);
    static bool HasResearch(int PlayerID, TerranResearchId ResearchId);
    static std::string_view GetResearchDisplayName(TerranResearchId ResearchId);

    static int GetWeaponBonus(const Core::ECS::EntityManager& EM, int EntityID);
    static int GetArmorBonus(const Core::ECS::EntityManager& EM, int EntityID);

private:
    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
