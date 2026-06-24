#pragma once

#include <string>
#include <vector>

#include "Core/ECS/SystemManager.h"

namespace Game::Campaign
{

struct Objective
{
    std::string Description;
    bool Complete = false;
};

enum class CampaignState : uint8_t
{
    Playing,
    Victory,
    Defeat
};

class CampaignController
{
public:
    void Init();
    void Update(Core::ECS::EntityManager& EM, float DeltaTime);
    void Reset();

    CampaignState GetState() const { return mState; }
    const std::vector<Objective>& GetObjectives() const { return mObjectives; }
    float GetStateTimer() const { return mStateTimer; }
    const std::string& GetMissionName() const { static std::string N = "Terran Campaign"; return N; }

private:
    void CheckObjectives(Core::ECS::EntityManager& EM);

    CampaignState mState = CampaignState::Playing;
    std::vector<Objective> mObjectives;
    float mStateTimer = 0.0f;
};

} // namespace Game::Campaign