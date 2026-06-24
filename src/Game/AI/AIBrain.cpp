#include "Game/AI/AIBrain.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Map/Map.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstdlib>

namespace Game::Systems
{

using namespace Game::Components;

// Terran CHK unit IDs
static constexpr int ChkCommandCenter = 0x6E;
static constexpr int ChkBarracks = 0x71;
static constexpr int ChkScv = 0x07;
static constexpr int ChkMarine = 0x08;

AIBrain::AIBrain(Core::ECS::SystemManager& SysMgr, int AIPlayerID, int TargetPlayerID)
    : mSystemMgr(SysMgr)
    , mAIPlayerID(AIPlayerID)
    , mTargetPlayerID(TargetPlayerID)
{
    Core::Utils::Logger::Info("AIBrain",
        "AI created for player " + std::to_string(mAIPlayerID) +
        " targeting player " + std::to_string(mTargetPlayerID));
}

Core::Math::Vec2 AIBrain::FindBaseLocation(Core::ECS::EntityManager& EM)
{
    auto Entities = EM.GetEntitiesWith<TransformComponent, PlayerComponent, BuildingComponent>();
    for (int Eid : Entities)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        if (Player && Player->PlayerID == mAIPlayerID && Trans)
            return Trans->Position;
    }
    return Core::Math::Vec2(1000.0f, 1000.0f);
}

Core::Math::Vec2 AIBrain::FindEnemyBaseLocation(Core::ECS::EntityManager& EM)
{
    auto Entities = EM.GetEntitiesWith<TransformComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        if (Player && Player->PlayerID == mTargetPlayerID && Trans)
            return Trans->Position;
    }
    return Core::Math::Vec2(2000.0f, 2000.0f);
}

int AIBrain::CountUnits(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID)
{
    int Count = 0;
    auto Entities = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        if (Unit && Player && Player->PlayerID == PlayerID)
        {
            if (ChkUnitID < 0 || Unit->ChkUnitID == ChkUnitID)
                Count++;
        }
    }
    return Count;
}

int AIBrain::CountBuildings(Core::ECS::EntityManager& EM, int PlayerID, bool ExcludeAddons)
{
    int Count = 0;
    auto Entities = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
    for (int Eid : Entities)
    {
        auto* B = EM.GetComponent<BuildingComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (B && P && P->PlayerID == PlayerID && B->IsBuilding && !B->UnderConstruction)
        {
            if (!ExcludeAddons || !B->IsAddon)
                Count++;
        }
    }
    return Count;
}

void AIBrain::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();

    // Resolve positions once
    if (!mPositionsResolved)
    {
        mAiBasePos = FindBaseLocation(EM);
        mEnemyBasePos = FindEnemyBaseLocation(EM);
        if (mAiBasePos.LengthSq() > 0 && mEnemyBasePos.LengthSq() > 0)
        {
            mPositionsResolved = true;
            Core::Utils::Logger::Info("AIBrain",
                "AI base at (" + std::to_string((int)mAiBasePos.X) + "," +
                std::to_string((int)mAiBasePos.Y) + "), enemy at (" +
                std::to_string((int)mEnemyBasePos.X) + "," +
                std::to_string((int)mEnemyBasePos.Y) + ")");
        }
    }

    // Decision timer
    mDecisionTimer += DeltaTime;
    if (mDecisionTimer < mDecisionInterval) return;
    mDecisionTimer = 0.0f;

    int CC_Count = CountUnits(EM, mAIPlayerID, ChkCommandCenter);
    int SCV_Count = CountUnits(EM, mAIPlayerID, ChkScv);
    int Barracks_Count = CountBuildings(EM, mAIPlayerID) - CC_Count + CountUnits(EM, mAIPlayerID, ChkBarracks);
    int Marine_Count = CountUnits(EM, mAIPlayerID, ChkMarine);
    auto& Res = HarvestSystem::GetResources(mAIPlayerID);

    // Find AI Command Center for training
    int AICommandCenterID = -1;
    int AIBarracksID = -1;
    int AISCVID = -1;
    {
        auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
        for (int Eid : Units)
        {
            auto* U = EM.GetComponent<UnitComponent>(Eid);
            auto* P = EM.GetComponent<PlayerComponent>(Eid);
            if (!U || !P || P->PlayerID != mAIPlayerID) continue;
            if (U->ChkUnitID == ChkCommandCenter) AICommandCenterID = Eid;
            if (U->ChkUnitID == ChkBarracks) AIBarracksID = Eid;
            if (U->ChkUnitID == ChkScv && AISCVID < 0) AISCVID = Eid;
        }
    }

    // ── Economy: send idle SCVs to harvest ──
    {
        // Find nearest mineral patch for the AI
        int NearestMineral = -1;
        float BestDist = 1e9f;
        auto Minerals = EM.GetEntitiesWith<TransformComponent, ResourceComponent>();
        for (int Mid : Minerals)
        {
            auto* RT = EM.GetComponent<TransformComponent>(Mid);
            auto* Res = EM.GetComponent<ResourceComponent>(Mid);
            if (!RT || !Res || Res->Type != ResourceType::Minerals || Res->IsDepleted) continue;
            float D = RT->Position.DistanceTo(mAiBasePos);
            if (D < BestDist) { BestDist = D; NearestMineral = Mid; }
        }
        if (NearestMineral >= 0)
        {
            auto ScvUnits = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
            for (int Eid : ScvUnits)
            {
                auto* U = EM.GetComponent<UnitComponent>(Eid);
                auto* P = EM.GetComponent<PlayerComponent>(Eid);
                auto* Harvest = EM.GetComponent<HarvestComponent>(Eid);
                if (!U || !P || P->PlayerID != mAIPlayerID) continue;
                if (U->ChkUnitID != ChkScv) continue;
                if (!Harvest || Harvest->State == HarvestState::Idle)
                {
                    HarvestSystem::IssueHarvest(EM, {Eid}, NearestMineral);
                }
            }
        }
    }

    // ── Economy: train SCVs ──
    if (AICommandCenterID >= 0 && SCV_Count < 8 && Res.Minerals >= 50)
    {
        auto TerranDefs = Races::GetTerranDefs();
        // Find SCV def ID
        for (size_t Ti = 0; Ti < TerranDefs.size(); Ti++)
        {
            if (TerranDefs[Ti].Name == "SCV")
            {
                ProductionSystem::StartTrain(EM, AICommandCenterID, (int)Ti);
                break;
            }
        }
    }

    // ── Build Barracks if needed ──
    if (Barracks_Count < 1 && AISCVID >= 0 && Res.Minerals >= 150)
    {
        auto TerranDefs = Races::GetTerranDefs();
        int BarracksDefID = -1;
        for (size_t Ti = 0; Ti < TerranDefs.size(); Ti++)
        {
            if (TerranDefs[Ti].Name == "Barracks")
            { BarracksDefID = (int)Ti; break; }
        }
        if (BarracksDefID >= 0)
        {
            Core::Math::Vec2 BuildPos = mAiBasePos;
            BuildPos.X += 80.0f + (float)(std::rand() % 60);
            BuildPos.Y += 40.0f + (float)(std::rand() % 40);
            BuildingSystem::StartBuild(EM, AISCVID, BarracksDefID, BuildPos);
            mHasBarracks = true;
        }
    }

    // ── Train Marines ──
    if (AIBarracksID >= 0 && Marine_Count < 12 && Res.Minerals >= 50)
    {
        auto TerranDefs = Races::GetTerranDefs();
        for (size_t Ti = 0; Ti < TerranDefs.size(); Ti++)
        {
            if (TerranDefs[Ti].Name == "Marine")
            {
                ProductionSystem::StartTrain(EM, AIBarracksID, (int)Ti);
                break;
            }
        }
    }

    // ── Send attack waves ──
    if (Marine_Count >= 6)
    {
        SendAttackWave(EM);
    }
}

void AIBrain::SendAttackWave(Core::ECS::EntityManager& EM)
{
    std::vector<int> Attackers;
    auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent, CombatComponent, TransformComponent>();
    for (int Eid : Units)
    {
        auto* U = EM.GetComponent<UnitComponent>(Eid);
        auto* P = EM.GetComponent<PlayerComponent>(Eid);
        if (U && P && P->PlayerID == mAIPlayerID && U->ChkUnitID == ChkMarine)
        {
            if (U->HitPoints > 0)
                Attackers.push_back(Eid);
        }
    }

    if ((int)Attackers.size() >= 6)
    {
        CombatSystem::IssueAttackMove(EM, Attackers, mEnemyBasePos);
        mAttackWaveCount++;
        Core::Utils::Logger::Info("AIBrain",
            "Sent attack wave " + std::to_string(mAttackWaveCount) +
            " with " + std::to_string(Attackers.size()) + " Marines");
    }
}

} // namespace Game::Systems