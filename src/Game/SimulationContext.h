#pragma once

#include <cstdint>

#include "Core/Audio/ModernAudioEventRouter.h"
#include "Core/ECS/EntityManager.h"

namespace Game::Map
{
class GameMap;
}

namespace Game::Systems
{
class FogOfWarSystem;
}

namespace Game
{

// World-scoped singleton resource holding authoritative simulation context.
// Stored inside Core::ECS::EntityManager via GetOrCreateResource<SimulationContext>()
// to eliminate cross-session global static pointers.
struct SimulationContext
{
    const Game::Map::GameMap* Map = nullptr;
    Game::Map::GameMap* MutableMap = nullptr;
    Game::Systems::FogOfWarSystem* FogOfWar = nullptr;
    int LocalPlayerID = 0;

    static SimulationContext& Get(Core::ECS::EntityManager& EM)
    {
        return EM.GetOrCreateResource<SimulationContext>();
    }

    static const SimulationContext* TryGet(const Core::ECS::EntityManager& EM)
    {
        return EM.GetResource<SimulationContext>();
    }
};

namespace Events
{

struct UnitDeathEvent
{
    int VictimEntityID = -1;
    int VictimPlayerID = -1;
    int VictimChkUnitID = -1;
    int KillerEntityID = -1;
    int KillerPlayerID = -1;
    bool AwardKill = false;
};

struct ResourceChangedEvent
{
    int PlayerID = 0;
    int MineralsDelta = 0;
    int GasDelta = 0;
    int NewMinerals = 0;
    int NewGas = 0;
};

struct ResearchCompletedEvent
{
    int PlayerID = 0;
    int BuildingEntityID = -1;
    uint8_t ResearchIdValue = 0;
    int NewLevel = 0;
};

struct BuildingCompletedEvent
{
    int BuildingEntityID = -1;
    int PlayerID = 0;
    int BuildingDefID = -1;
};

using SoundRequestEvent = Core::Audio::SoundRequestEvent;

} // namespace Events

} // namespace Game
