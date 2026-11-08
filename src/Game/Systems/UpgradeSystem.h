#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "Core/ECS/SystemManager.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Engine/Gameplay/TechAndUpgradeServer.h"

namespace Game::Systems
{

using ResearchId = Core::ECS::ResearchId;
using PlayerUpgrades = Core::ECS::PlayerUpgrades;
using PlayerUpgradeStore = Core::ECS::PlayerUpgradeStore;

class UpgradeSystem : public Core::ECS::ISystem
{
public:
    UpgradeSystem(Core::ECS::SystemManager& SysMgr);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "UpgradeSystem"; }

    static PlayerUpgrades& GetUpgrades(Core::ECS::EntityManager& EM, int PlayerID = 0);
    static const PlayerUpgrades& GetUpgrades(const Core::ECS::EntityManager& EM, int PlayerID = 0);
    static PlayerUpgrades& GetUpgrades(int PlayerID = 0);

    static void ResetUpgrades(Core::ECS::EntityManager& EM);

    static bool CanStartResearch(const Core::ECS::EntityManager& EM,
                                  int BuildingID,
                                  ResearchId ResearchId,
                                  std::string* OutReason = nullptr);

    static bool StartResearch(Core::ECS::EntityManager& EM,
                               int BuildingID,
                               ResearchId ResearchId);

    static bool CancelResearch(Core::ECS::EntityManager& EM,
                               int BuildingID,
                               bool BuildingDestroyed = false);

    static int GetResearchLevel(const Core::ECS::EntityManager& EM, int PlayerID, ResearchId ResearchId);
    static bool HasResearch(const Core::ECS::EntityManager& EM, int PlayerID, ResearchId ResearchId);
    static int GetResearchLevel(int PlayerID, ResearchId ResearchId);
    static bool HasResearch(int PlayerID, ResearchId ResearchId);
    static bool IsUpgradeType(ResearchId ResearchId);
    static std::string_view GetResearchDisplayName(ResearchId ResearchId);
    static int GetResearchTimeCost(ResearchId ResearchId, int PlayerID);
    static bool GetResearchCost(ResearchId ResearchId,
                                int PlayerID,
                                int& OutMinerals,
                                int& OutGas);
    static bool GetResearchCost(const Core::ECS::EntityManager& EM,
                                ResearchId ResearchId,
                                int PlayerID,
                                int& OutMinerals,
                                int& OutGas);

    static int GetWeaponBonus(const Core::ECS::EntityManager& EM, int EntityID);
    static int GetWeaponBonusForTarget(const Core::ECS::EntityManager& EM,
                                       int EntityID,
                                       bool TargetIsAir);
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

    static void BindEngineTechAndUpgradeServer(Engine::Gameplay::TechAndUpgradeServer* Server) noexcept { sEngineTechServer = Server; }
    static Engine::Gameplay::TechAndUpgradeServer* GetBoundEngineTechAndUpgradeServer() noexcept { return sEngineTechServer; }

private:
    Core::ECS::SystemManager& mSystemMgr;
    inline static Engine::Gameplay::TechAndUpgradeServer* sEngineTechServer = nullptr;
};

} // namespace Game::Systems
