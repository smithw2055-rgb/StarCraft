#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "Core/ECS/WorldSnapshot.h"
#include "UI/HudState.h"

namespace Game::Client::GameLoop
{

struct GameSaveSnapshot
{
    std::string MapPath;
    bool Paused = false;
    UI::HudResourceState Resources;

    // Phase 24: Optional authoritative ECS World deterministic snapshot persistence
    bool HasWorldSnapshot = false;
    uint64_t SimulationTick = 0;
    Core::ECS::WorldDeterministicSnapshot WorldSnapshot{};
};

bool WriteGameSave(FILE* File, const GameSaveSnapshot& Snapshot);
bool ReadGameSave(FILE* File, GameSaveSnapshot& Snapshot);

} // namespace Game::Client::GameLoop