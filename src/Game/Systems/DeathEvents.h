#pragma once

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"

namespace Game::Systems
{

inline void RecordUnitDeath(Core::ECS::EntityManager& EM,
                            int VictimEntityID,
                            int KillerEntityID = -1,
                            bool AwardKill = true)
{
    auto* VictimUnit = EM.GetComponent<Game::Components::UnitComponent>(VictimEntityID);
    auto* VictimPlayer = EM.GetComponent<Game::Components::PlayerComponent>(VictimEntityID);
    if (!VictimUnit || !VictimPlayer)
        return;

    auto* Existing = EM.GetComponent<Game::Components::DeathRecordComponent>(VictimEntityID);
    if (Existing)
        return;

    auto& Record = EM.AddComponent<Game::Components::DeathRecordComponent>(VictimEntityID);
    Record.VictimPlayerID = VictimPlayer->PlayerID;
    Record.VictimUnitID = VictimUnit->ChkUnitID;
    Record.KillerEntityID = KillerEntityID;

    if (KillerEntityID >= 0)
    {
        if (auto* KillerPlayer = EM.GetComponent<Game::Components::PlayerComponent>(KillerEntityID))
            Record.KillerPlayerID = KillerPlayer->PlayerID;
    }
    Record.AwardKill = AwardKill &&
        Record.KillerPlayerID >= 0 &&
        Record.KillerPlayerID != Record.VictimPlayerID;
}

} // namespace Game::Systems
