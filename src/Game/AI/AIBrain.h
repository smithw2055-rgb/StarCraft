#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include <vector>
#include <string>

namespace Game::Systems
{

enum class AIStrategy : uint8_t
{
    None,
    EconomicFocus,
    Rush,
    Balanced,
    TechRush
};

class AIBrain : public Core::ECS::ISystem
{
public:
    AIBrain(Core::ECS::SystemManager& SysMgr, int AIPlayerID, int TargetPlayerID);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "AIBrain"; }

private:
    struct BuildStep
    {
        int UnitDefID = -1;     // unit/building def ID
        int CountTarget = 1;    // how many to build
        bool IsBuilding = false;
    };

    void RunTerranAI(Core::ECS::EntityManager& EM);
    void RunZergAI(Core::ECS::EntityManager& EM);
    void RunProtossAI(Core::ECS::EntityManager& EM);

    void ManageEconomy(Core::ECS::EntityManager& EM);
    void ManageMilitary(Core::ECS::EntityManager& EM);
    void SendAttackWave(Core::ECS::EntityManager& EM);
    void TrainWorkerIfNeeded(Core::ECS::EntityManager& EM, int ProducerDefID, int WorkerDefID, int MaxWorkers);

    Core::Math::Vec2 FindBaseLocation(Core::ECS::EntityManager& EM);
    Core::Math::Vec2 FindEnemyBaseLocation(Core::ECS::EntityManager& EM);
    Core::Math::Vec2 FindExpansionLocation(Core::ECS::EntityManager& EM);
    int FindProducerFor(Core::ECS::EntityManager& EM, int UnitDefID);
    int FindIdleWorker(Core::ECS::EntityManager& EM);
    int FindNearestFreeBuilding(Core::ECS::EntityManager& EM, int BuildingDefID);
    bool FindBuildPositionNear(Core::ECS::EntityManager& EM, int BuildingDefID,
                               const Core::Math::Vec2& NearPos, Core::Math::Vec2& OutPos);
    bool StartBuildingNear(Core::ECS::EntityManager& EM, int BuilderID, int BuildingDefID,
                           const Core::Math::Vec2& NearPos);
    bool StartZergBuildingNear(Core::ECS::EntityManager& EM, int DroneID, int BuildingDefID,
                               const Core::Math::Vec2& NearPos);
    bool TrainUnitIfPossible(Core::ECS::EntityManager& EM, int UnitDefID);

    int CountUnits(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID = -1);
    int CountBuildings(Core::ECS::EntityManager& EM, int PlayerID, bool ExcludeAddons = true);
    int CountByDefID(Core::ECS::EntityManager& EM, int PlayerID, int DefID,
                     bool IncludeUnderConstruction = false);
    int GetRace(Core::ECS::EntityManager& EM);
    void BuildOrTrain(Core::ECS::EntityManager& EM, int DefID, const Core::Math::Vec2& NearPos);

    Core::ECS::SystemManager& mSystemMgr;
    int mAIPlayerID;
    int mTargetPlayerID;
    float mDecisionTimer = 0.0f;
    float mDecisionInterval = 2.0f;
    int mAttackWaveCount = 0;
    float mAttackCooldownRemaining = 0.0f;
    int mStage = 0;
    Core::Math::Vec2 mAiBasePos;
    Core::Math::Vec2 mEnemyBasePos;
    Core::Math::Vec2 mExpansionPos;
    bool mPositionsResolved = false;
    bool mExpansionStarted = false;
    std::vector<BuildStep> mBuildQueue;
    int mBuildQueueIdx = 0;
    float mBuildTimer = 0.0f;
    AIStrategy mStrategy = AIStrategy::Balanced;
};

} // namespace Game::Systems
