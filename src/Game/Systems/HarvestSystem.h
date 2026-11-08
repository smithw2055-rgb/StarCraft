#pragma once

#include <array>
#include <string>
#include <vector>

#include "Core/ECS/SystemManager.h"
#include "Core/ECS/WorldSnapshot.h"
#include "Engine/Gameplay/EconomyAndProductionServer.h"
#include "Game/Components.h"

namespace Game::Systems
{

using Core::ECS::kPlayerResourceSlots;
using PlayerResources = Core::ECS::PlayerResources;
using PlayerResourceStore = Core::ECS::PlayerResourceStore;

class HarvestSystem : public Core::ECS::ISystem
{
public:
    HarvestSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "HarvestSystem"; }

    static PlayerResources& GetResources(Core::ECS::EntityManager& EM, int PlayerID = 0)
    {
        if (PlayerID < 0)
            PlayerID = 0;
        else if (PlayerID >= kPlayerResourceSlots)
            PlayerID = kPlayerResourceSlots - 1;
        if (auto* Store = EM.GetResource<PlayerResourceStore>())
            return Store->Players[(size_t)PlayerID];
        return GetDefaultStore().Players[(size_t)PlayerID];
    }

    static const PlayerResources& GetResources(const Core::ECS::EntityManager& EM, int PlayerID = 0)
    {
        if (PlayerID < 0)
            PlayerID = 0;
        else if (PlayerID >= kPlayerResourceSlots)
            PlayerID = kPlayerResourceSlots - 1;
        if (const auto* Store = EM.GetResource<PlayerResourceStore>())
            return Store->Players[(size_t)PlayerID];
        return GetDefaultStore().Players[(size_t)PlayerID];
    }

    static void ResetResources(Core::ECS::EntityManager& EM)
    {
        EM.SetResource<PlayerResourceStore>();
        EM.ActivateWorld();
    }

    // Phase 75 Bidirectional Synchronization with Engine::Gameplay::EconomyAndProductionServer
    static void SyncToEngineEconomyServer(const Core::ECS::EntityManager& EM,
                                          Engine::Gameplay::EconomyAndProductionServer& Server) noexcept
    {
        for (int P = 0; P < kPlayerResourceSlots; ++P)
        {
            const auto& Res = GetResources(EM, P);
            auto& W = Server.Wallet(P);
            W.Minerals = Res.Minerals;
            W.Gas = Res.VespeneGas;
            W.SupplyUsed = Res.SupplyUsed;
            W.SupplyMax = Res.SupplyMax;
        }
    }

    static void SyncFromEngineEconomyServer(Core::ECS::EntityManager& EM,
                                            const Engine::Gameplay::EconomyAndProductionServer& Server) noexcept
    {
        for (int P = 0; P < kPlayerResourceSlots; ++P)
        {
            auto& Res = GetResources(EM, P);
            const auto& W = Server.Wallet(P);
            Res.Minerals = W.Minerals;
            Res.VespeneGas = W.Gas;
            Res.SupplyUsed = W.SupplyUsed;
            Res.SupplyMax = W.SupplyMax;
        }
    }

    // Legacy static overload: resolves against the active EntityManager World if it has
    // an isolated PlayerResourceStore bound, otherwise falls back to the default store.
    static PlayerResources& GetResources(int PlayerID = 0)
    {
        if (auto* ActiveWorld = Core::ECS::EntityManager::GetActiveWorld())
        {
            if (ActiveWorld->HasResource<PlayerResourceStore>())
                return GetResources(*ActiveWorld, PlayerID);
        }
        if (PlayerID < 0)
            PlayerID = 0;
        else if (PlayerID >= kPlayerResourceSlots)
            PlayerID = kPlayerResourceSlots - 1;
        return GetDefaultStore().Players[(size_t)PlayerID];
    }

    static bool CanHarvest(const Core::ECS::EntityManager& EM,
                           int UnitID,
                           int TargetResourceID,
                           std::string* OutReason = nullptr);

    static bool IssueHarvest(Core::ECS::EntityManager& EM,
                             const std::vector<int>& Units,
                             int TargetResourceID);

    // A lifted Command Center is not a valid return depot. Workers whose
    // automatic harvest loop stopped only because no depot was available keep
    // their desired resource, so landing can resume those nearby loops without
    // overriding an explicit player Stop/Move command.
    static int ResumeNearbyHarvestersForDepot(Core::ECS::EntityManager& EM,
                                               int DepotID);

private:
    static PlayerResourceStore& GetDefaultStore()
    {
        static PlayerResourceStore DefaultStore;
        return DefaultStore;
    }

    void ProcessHarvester(Core::ECS::EntityManager& EM, int ID, float DeltaTime);
    static int FindNearestDepot(Core::ECS::EntityManager& EM, int HarvesterID,
                                Game::Components::ResourceType Type,
                                int ExcludedDepotID = -1);

    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
