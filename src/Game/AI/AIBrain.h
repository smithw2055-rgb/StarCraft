#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include <vector>
#include <string>

namespace Game::Map { class GameMap; }

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
    AIBrain(Core::ECS::SystemManager& SysMgr, int AIPlayerID, int TargetPlayerID,
            bool ShowcaseMode = false, const Game::Map::GameMap* Map = nullptr);
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
    void RunTerranShowcase(Core::ECS::EntityManager& EM);
    void RunZergAI(Core::ECS::EntityManager& EM);
    void RunZergShowcase(Core::ECS::EntityManager& EM);
    void RunProtossAI(Core::ECS::EntityManager& EM);
    void RunProtossShowcase(Core::ECS::EntityManager& EM);

    void ManageEconomy(Core::ECS::EntityManager& EM);
    void ManageMilitary(Core::ECS::EntityManager& EM);
    bool DefendBaseIfNeeded(Core::ECS::EntityManager& EM);
    void ManageTerranRepairs(Core::ECS::EntityManager& EM);
    void SendAttackWave(Core::ECS::EntityManager& EM, int WaveSize);
    void TrainWorkerIfNeeded(Core::ECS::EntityManager& EM, int ProducerDefID, int WorkerDefID, int MaxWorkers);

    Core::Math::Vec2 FindBaseLocation(Core::ECS::EntityManager& EM);
    Core::Math::Vec2 FindEnemyBaseLocation(Core::ECS::EntityManager& EM);
    bool HasLivingForces(Core::ECS::EntityManager& EM, int PlayerID) const;
    void RetargetIfNeeded(Core::ECS::EntityManager& EM);
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
    int FindAvailableAddonParent(Core::ECS::EntityManager& EM, int ParentDefID);
    int FindTerranBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID);
    int FindZergBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID);
    int FindProtossBuilderFor(Core::ECS::EntityManager& EM, int BuildingDefID);
    bool StartGasBuilding(Core::ECS::EntityManager& EM, int BuilderID,
                          int GasBuildingDefID);
    bool StartZergGasBuilding(Core::ECS::EntityManager& EM, int DroneID,
                              int ExtractorDefID);
    void ManageGas(Core::ECS::EntityManager& EM, int RefineryDefID);
    Core::Math::Vec2 FindShowcaseMapCenter(Core::ECS::EntityManager& EM) const;
    Core::Math::Vec2 ShowcaseBuildPosition(Core::ECS::EntityManager& EM,
                                            size_t LayoutIndex) const;
    void UpdateShowcaseRallyPoints(Core::ECS::EntityManager& EM);

    int CountUnits(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID = -1);
    int CountBuildings(Core::ECS::EntityManager& EM, int PlayerID, bool ExcludeAddons = true);
    int CountByDefID(Core::ECS::EntityManager& EM, int PlayerID, int DefID,
                     bool IncludeUnderConstruction = false);
    int CountQueuedByDefID(Core::ECS::EntityManager& EM, int PlayerID, int DefID) const;
    int GetRace(Core::ECS::EntityManager& EM);
    void BuildOrTrain(Core::ECS::EntityManager& EM, int DefID, const Core::Math::Vec2& NearPos);

    Core::ECS::SystemManager& mSystemMgr;
    int mAIPlayerID;
    int mTargetPlayerID;
    float mDecisionTimer = 0.0f;
    float mElapsedSeconds = 0.0f;
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
    bool mShowcaseMode = false;
    bool mDefendingBase = false;
    const Game::Map::GameMap* mMap = nullptr;
    bool mShowcaseCompleteLogged = false;
    bool mShowcaseStartedLogged = false;
    std::vector<int> mShowcaseQueuedUnits;
    std::vector<int> mShowcaseRallyConfiguredBuildings;
    std::vector<int> mActiveAttackGroup;
    float mActiveAttackGroupAge = 0.0f;
};

} // namespace Game::Systems
