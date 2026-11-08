#include "Main/GameRuntimeFrame.h"
#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/FogOfWarSystem.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace Core::Platform
{
    bool IsKeyPressed(int) { return false; }
    bool IsKeyDown(int) { return false; }
    bool IsKeyReleased(int) { return false; }
    int GetWindowWidth() { return 1280; }
    int GetWindowHeight() { return 720; }
}

namespace
{

void Require(bool Condition, const char* Message)
{
    if (!Condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", Message);
        std::fflush(stderr);
        std::exit(1);
    }
}

int CreateBuilding(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID, int HP = 1000)
{
    int Eid = EM.CreateEntity();
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(Eid);
    Unit.ChkUnitID = ChkUnitID;
    Unit.HitPoints = HP;
    Unit.MaxHitPoints = HP;

    auto& Player = EM.AddComponent<Game::Components::PlayerComponent>(Eid);
    Player.PlayerID = PlayerID;

    auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(Eid);
    Building.IsBuilding = true;
    Building.UnderConstruction = false;
    const int DefID = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitID);
    Building.BuildingDefID = DefID >= 0 ? DefID : 1;

    return Eid;
}

} // namespace

int main()
{
    using namespace Game::Client::GameLoop;

    // Test 1: Victory check disabled
    {
        auto Session = std::make_unique<GameSession>();
        Session->EcsInitialized = true;
        Session->SetMeleeVictoryCheckEnabled(false);

        auto& EM = Session->Entities();
        CreateBuilding(EM, 0, 106); // Command Center for Local Player (0)

        UpdateMeleeMatchOutcome(*Session, 10.0f);
        Require(!Session->IsMatchOver(), "Victory check disabled should not alter match outcome");
    }

    // Test 2: Grace period (under 5 seconds)
    {
        auto Session = std::make_unique<GameSession>();
        Session->EcsInitialized = true;
        auto& EM = Session->Entities();
        CreateBuilding(EM, 0, 106); // Command Center
        // Opponent with zero buildings
        int Worker = EM.CreateEntity();
        EM.AddComponent<Game::Components::UnitComponent>(Worker).HitPoints = 60;
        EM.AddComponent<Game::Components::PlayerComponent>(Worker).PlayerID = 1;

        UpdateMeleeMatchOutcome(*Session, 2.0f);
        Require(!Session->IsMatchOver(), "Match outcome should remain Playing during 5s initial protection");
        Require(Session->ParticipatingPlayers[0] && Session->ParticipatingPlayers[1],
            "Both players with active entities must be marked participating");
    }

    // Test 3: Standard Victory (Opponent eliminated)
    {
        auto Session = std::make_unique<GameSession>();
        Session->EcsInitialized = true;
        auto& EM = Session->Entities();
        CreateBuilding(EM, 0, 106); // Local Player Command Center
        int EnemyHatchery = CreateBuilding(EM, 1, 131); // Player 1 Hatchery

        // Advance past grace period
        UpdateMeleeMatchOutcome(*Session, 6.0f);
        Require(!Session->IsMatchOver(), "Match ongoing while enemy has buildings");

        // Destroy enemy building
        EM.GetComponent<Game::Components::UnitComponent>(EnemyHatchery)->HitPoints = 0;
        UpdateMeleeMatchOutcome(*Session, 0.1f);
        Require(Session->IsMatchOver(), "Match should be over after enemy building destruction");
        Require(Session->IsVictory(), "Local player should achieve Victory");
        Require(!Session->IsDefeat(), "Victory is not Defeat");
    }

    // Test 4: Standard Defeat (Local player eliminated)
    {
        auto Session = std::make_unique<GameSession>();
        Session->EcsInitialized = true;
        auto& EM = Session->Entities();
        int LocalCC = CreateBuilding(EM, 0, 106);
        CreateBuilding(EM, 1, 131);

        UpdateMeleeMatchOutcome(*Session, 6.0f);
        Require(!Session->IsMatchOver(), "Match ongoing before destruction");

        // Destroy local player's building
        EM.GetComponent<Game::Components::UnitComponent>(LocalCC)->HitPoints = 0;
        UpdateMeleeMatchOutcome(*Session, 0.1f);
        Require(Session->IsMatchOver(), "Match should be over after local player building lost");
        Require(Session->IsDefeat(), "Local player should receive Defeat");
        Require(!Session->IsVictory(), "Defeat is not Victory");
    }

    // Test 5: Draw (Mutual simultaneous elimination)
    {
        auto Session = std::make_unique<GameSession>();
        Session->EcsInitialized = true;
        auto& EM = Session->Entities();
        int LocalCC = CreateBuilding(EM, 0, 106);
        int EnemyHatchery = CreateBuilding(EM, 1, 131);

        UpdateMeleeMatchOutcome(*Session, 6.0f);

        EM.GetComponent<Game::Components::UnitComponent>(LocalCC)->HitPoints = 0;
        EM.GetComponent<Game::Components::UnitComponent>(EnemyHatchery)->HitPoints = 0;
        UpdateMeleeMatchOutcome(*Session, 0.1f);
        Require(Session->IsMatchOver(), "Match over on mutual elimination");
        Require(Session->IsDraw(), "Outcome must be Draw");
    }

    // Test 6: Allies via FogSystem do not count as active enemies
    {
        auto Session = std::make_unique<GameSession>();
        Session->EcsInitialized = true;
        Game::Systems::FogOfWarSystem Fog(Session->EcsSysMgr, nullptr);
        Fog.SetAlliance(0, 2, true); // Player 0 allied with Player 2
        Session->FogSystem = &Fog;

        auto& EM = Session->Entities();
        CreateBuilding(EM, 0, 106); // Local Player 0
        int EnemyNexus = CreateBuilding(EM, 1, 154); // Enemy Player 1
        CreateBuilding(EM, 2, 106); // Allied Player 2

        UpdateMeleeMatchOutcome(*Session, 6.0f);
        Require(!Session->IsMatchOver(), "Match ongoing while enemy 1 is alive");

        // Destroy only enemy Nexus, ally 2 still alive
        EM.GetComponent<Game::Components::UnitComponent>(EnemyNexus)->HitPoints = 0;
        UpdateMeleeMatchOutcome(*Session, 0.1f);
        Require(Session->IsMatchOver() && Session->IsVictory(),
            "Destroying all non-allied enemies should yield Victory even if ally has buildings");

        Session->FogSystem = nullptr;
    }

    // Test 7: ResetMatchOutcome
    {
        auto Session = std::make_unique<GameSession>();
        Session->SetMatchOutcome(MatchOutcome::Victory);
        Session->AdvanceMatchElapsed(123.4f);
        Session->ParticipatingPlayers[0] = true;

        Require(Session->IsMatchOver(), "Sanity check match over");
        Session->ResetMatchOutcome();
        Require(!Session->IsMatchOver(), "Reset must restore Playing status");
        Require(Session->GetMatchOutcome() == MatchOutcome::Playing, "Outcome is Playing");
        Require(Session->GetMatchElapsedSeconds() == 0.0f, "Elapsed seconds reset");
        Require(!Session->ParticipatingPlayers[0], "Participating players cleared");
    }

    std::printf("MatchOutcomeTest passed\n");
    return 0;
}
