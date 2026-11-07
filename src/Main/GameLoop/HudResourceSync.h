#pragma once

#include "Game/Systems/HarvestSystem.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void ResetPlayerResourceState()
{
    for (int PlayerId = 0; PlayerId < 8; ++PlayerId)
    {
        auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerId);
        Resources.Minerals = 500;
        Resources.VespeneGas = 0;
        Resources.SupplyUsed = 0;
        Resources.SupplyMax = 0;
    }
}

inline UI::HudResourceState CaptureHudResourceState(const UI::Hud& Hud)
{
    return Hud.GetResourceState();
}

inline void RestorePlayerResourcesFromHudState(UI::Hud& Hud, const UI::HudResourceState& State, int PlayerId = 0)
{
    auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerId);
    Resources.Minerals = State.MineralCount;
    Resources.VespeneGas = State.GasCount;
    Resources.SupplyUsed = State.SupplyUsed;
    Resources.SupplyMax = State.SupplyMax;
    Hud.SetResourceState(State);
}

inline void SyncHudFromPlayerResources(UI::Hud& Hud, int PlayerId = 0)
{
    auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerId);
    UI::HudResourceState HudResources;
    HudResources.MineralCount = Resources.Minerals;
    HudResources.GasCount = Resources.VespeneGas;
    HudResources.SupplyUsed = Resources.SupplyUsed;
    HudResources.SupplyMax = Resources.SupplyMax;
    Hud.SetResourceState(HudResources);
}

} // namespace Game::Client::GameLoop
