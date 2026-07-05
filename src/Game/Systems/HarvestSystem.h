#pragma once

#include <string>

#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"

namespace Game::Systems
{

inline constexpr int kPlayerResourceSlots = 12;

struct PlayerResources
{
    int Minerals = 400;
    int VespeneGas = 100;
    int SupplyUsed = 0;
    int SupplyMax = 10;
};

class HarvestSystem : public Core::ECS::ISystem
{
public:
    HarvestSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "HarvestSystem"; }

    static PlayerResources& GetResources(int PlayerID = 0)
    {
        static PlayerResources Resources[kPlayerResourceSlots];
        if (PlayerID < 0)
            PlayerID = 0;
        else if (PlayerID >= kPlayerResourceSlots)
            PlayerID = kPlayerResourceSlots - 1;
        return Resources[PlayerID];
    }

    static bool CanHarvest(const Core::ECS::EntityManager& EM,
                           int UnitID,
                           int TargetResourceID,
                           std::string* OutReason = nullptr);

    static bool IssueHarvest(Core::ECS::EntityManager& EM,
                             const std::vector<int>& Units,
                             int TargetResourceID);

private:
    void ProcessHarvester(Core::ECS::EntityManager& EM, int ID, float DeltaTime);
    int FindNearestDepot(Core::ECS::EntityManager& EM, int HarvesterID,
                         Game::Components::ResourceType Type);

    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
