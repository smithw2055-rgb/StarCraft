#pragma once

#include "Main/GameSession.h"
#include "Game/Components.h"
#include "Game/Systems/GeyserState.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Core/Utils/Logger.h"

#include <array>
#include <cstdint>
#include <string>

namespace Game::Client::GameLoop
{

inline void UpdateMeleeMatchOutcome(GameSession& Session, float Dt)
{
    if (!Session.IsEcsInitialized() ||
        !Session.IsMeleeVictoryCheckEnabled() ||
        Session.IsMatchOver())
    {
        return;
    }

    Session.AdvanceMatchElapsed(Dt);

    auto& EM = Session.Entities();
    std::array<int, 8> BuildingCounts{};
    std::array<int, 8> UnitCounts{};

    for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                      Game::Components::PlayerComponent>())
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Eid);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
        if (!Unit || !Player || Unit->HitPoints <= 0 ||
            Player->PlayerID < 0 || Player->PlayerID >= 8)
        {
            continue;
        }
        ++UnitCounts[(size_t)Player->PlayerID];
        if (Game::Systems::IsMeleeStructureEntity(EM, Eid))
            ++BuildingCounts[(size_t)Player->PlayerID];
    }

    for (int P = 0; P < 8; ++P)
    {
        if (UnitCounts[(size_t)P] > 0 || BuildingCounts[(size_t)P] > 0)
            Session.ParticipatingPlayers[(size_t)P] = true;
    }

    if (Session.GetMatchElapsedSeconds() < 5.0f)
        return;

    const int LocalPlayerID = Session.GetLocalPlayerID();
    const auto* Fog = Session.GetFogSystem();

    if (!Session.ParticipatingPlayers[(size_t)LocalPlayerID])
        return;

    int ActiveOpponents = 0;
    int EliminatedOpponents = 0;

    for (int P = 0; P < 8; ++P)
    {
        if (P == LocalPlayerID || !Session.ParticipatingPlayers[(size_t)P])
            continue;
        if (Fog && Fog->AreAllied(LocalPlayerID, P))
            continue;

        ++ActiveOpponents;
        if (BuildingCounts[(size_t)P] == 0)
            ++EliminatedOpponents;
    }

    const bool LocalEliminated = BuildingCounts[(size_t)LocalPlayerID] == 0;

    if (LocalEliminated)
    {
        if (ActiveOpponents > 0 && EliminatedOpponents == ActiveOpponents)
        {
            Session.SetMatchOutcome(MatchOutcome::Draw);
            Core::Utils::Logger::Info(
                "MatchOutcome", "Melee match draw at simulated_seconds=" +
                std::to_string((int)Session.GetMatchElapsedSeconds()));
        }
        else
        {
            Session.SetMatchOutcome(MatchOutcome::Defeat);
            Core::Utils::Logger::Info(
                "MatchOutcome", "Melee match defeat: local player " +
                std::to_string(LocalPlayerID) + " eliminated at simulated_seconds=" +
                std::to_string((int)Session.GetMatchElapsedSeconds()));
        }
    }
    else if (ActiveOpponents > 0 && EliminatedOpponents == ActiveOpponents)
    {
        Session.SetMatchOutcome(MatchOutcome::Victory);
        Core::Utils::Logger::Info(
            "MatchOutcome", "Melee match victory: local player " +
            std::to_string(LocalPlayerID) + " victorious at simulated_seconds=" +
            std::to_string((int)Session.GetMatchElapsedSeconds()));
    }
}

} // namespace Game::Client::GameLoop
