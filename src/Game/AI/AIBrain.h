#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"

namespace Game::Systems
{

class AIBrain : public Core::ECS::ISystem
{
public:
    AIBrain(Core::ECS::SystemManager& SysMgr, int AIPlayerID, int TargetPlayerID);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "AIBrain"; }

private:
    void ManageEconomy(Core::ECS::EntityManager& EM);
    void ManageMilitary(Core::ECS::EntityManager& EM);
    void SendAttackWave(Core::ECS::EntityManager& EM);
    Core::Math::Vec2 FindBaseLocation(Core::ECS::EntityManager& EM);
    Core::Math::Vec2 FindEnemyBaseLocation(Core::ECS::EntityManager& EM);
    int CountUnits(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID);
    int CountBuildings(Core::ECS::EntityManager& EM, int PlayerID, bool ExcludeAddons = true);

    Core::ECS::SystemManager& mSystemMgr;
    int mAIPlayerID;
    int mTargetPlayerID;
    float mDecisionTimer = 0.0f;
    float mDecisionInterval = 2.5f;
    int mAttackWaveCount = 0;
    bool mHasBarracks = false;
    Core::Math::Vec2 mAiBasePos;
    Core::Math::Vec2 mEnemyBasePos;
    bool mPositionsResolved = false;
};

} // namespace Game::Systems