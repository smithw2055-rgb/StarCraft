#include "Game/Campaign/CampaignController.h"
#include "Game/Components.h"
#include "Game/Systems/HarvestSystem.h"
#include "Core/Utils/Logger.h"

namespace Game::Campaign
{

using namespace Game::Components;

void CampaignController::Init()
{
    Reset();
    Core::Utils::Logger::Info("Campaign", "Campaign started");
}

void CampaignController::Reset()
{
    mState = CampaignState::Playing;
    mStateTimer = 0.0f;
    mObjectives.clear();
    mObjectives.push_back({"Build a Barracks", false});
    mObjectives.push_back({"Train 6 Marines", false});
    mObjectives.push_back({"Destroy all enemy buildings", false});
}

void CampaignController::Update(Core::ECS::EntityManager& EM, float DeltaTime)
{
    if (mState == CampaignState::Victory || mState == CampaignState::Defeat)
    {
        mStateTimer += DeltaTime;
        return;
    }

    CheckObjectives(EM);

    // Check defeat: player 0 has no buildings
    bool HasPlayerBuilding = false;
    {
        auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
        for (int Eid : Buildings)
        {
            auto* P = EM.GetComponent<PlayerComponent>(Eid);
            auto* B = EM.GetComponent<BuildingComponent>(Eid);
            if (P && B && P->PlayerID == 0 && !B->UnderConstruction)
            { HasPlayerBuilding = true; break; }
        }
    }
    if (!HasPlayerBuilding)
    {
        mState = CampaignState::Defeat;
        mStateTimer = 0.0f;
        Core::Utils::Logger::Info("Campaign", "Defeat - no buildings remaining");
        return;
    }

    // Check victory: no enemy buildings
    bool HasEnemyBuilding = false;
    {
        auto Buildings = EM.GetEntitiesWith<BuildingComponent, PlayerComponent>();
        for (int Eid : Buildings)
        {
            auto* P = EM.GetComponent<PlayerComponent>(Eid);
            if (P && P->PlayerID > 0)
            { HasEnemyBuilding = true; break; }
        }
    }
    if (!HasEnemyBuilding && !mObjectives.empty())
    {
        // Also check all objectives are complete
        bool AllComplete = true;
        for (auto& O : mObjectives) if (!O.Complete) { AllComplete = false; break; }
        if (AllComplete)
        {
            mState = CampaignState::Victory;
            mStateTimer = 0.0f;
            Core::Utils::Logger::Info("Campaign", "Victory!");
        }
    }
}

void CampaignController::CheckObjectives(Core::ECS::EntityManager& EM)
{
    if (mObjectives.empty()) return;

    // Objective 1: Build a Barracks
    if (!mObjectives[0].Complete)
    {
        auto Buildings = EM.GetEntitiesWith<BuildingComponent, UnitComponent, PlayerComponent>();
        for (int Eid : Buildings)
        {
            auto* P = EM.GetComponent<PlayerComponent>(Eid);
            auto* U = EM.GetComponent<UnitComponent>(Eid);
            auto* B = EM.GetComponent<BuildingComponent>(Eid);
            if (P && U && B && P->PlayerID == 0 && U->ChkUnitID == 0x71 && !B->UnderConstruction)
            {
                mObjectives[0].Complete = true;
                Core::Utils::Logger::Info("Campaign", "Objective 1 complete: Barracks built");
                break;
            }
        }
    }

    // Objective 2: Train 6 Marines
    if (!mObjectives[1].Complete)
    {
        int MarineCount = 0;
        auto Units = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
        for (int Eid : Units)
        {
            auto* P = EM.GetComponent<PlayerComponent>(Eid);
            auto* U = EM.GetComponent<UnitComponent>(Eid);
            if (P && U && P->PlayerID == 0 && U->ChkUnitID == 0x08 && U->HitPoints > 0)
                MarineCount++;
        }
        if (MarineCount >= 6)
        {
            mObjectives[1].Complete = true;
            Core::Utils::Logger::Info("Campaign", "Objective 2 complete: 6 Marines trained");
        }
    }

    // Objective 3: Destroy all enemy buildings - handled in Update()
}

} // namespace Game::Campaign