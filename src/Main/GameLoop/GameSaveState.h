#pragma once

#include <cstdio>
#include <string>

#include "UI/HudState.h"

namespace Game::Client::GameLoop
{

struct GameSaveSnapshot
{
    std::string MapPath;
    bool Paused = false;
    UI::HudResourceState Resources;
};

bool WriteGameSave(FILE* File, const GameSaveSnapshot& Snapshot);
bool ReadGameSave(FILE* File, GameSaveSnapshot& Snapshot);

} // namespace Game::Client::GameLoop