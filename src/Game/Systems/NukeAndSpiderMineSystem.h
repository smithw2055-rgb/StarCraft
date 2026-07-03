#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"

namespace Game::Systems
{

class NukeAndSpiderMineSystem : public Core::ECS::ISystem
{
public:
    NukeAndSpiderMineSystem(Core::ECS::SystemManager& SysMgr, class BulletSystem& BulletSys)
        : mSystemMgr(SysMgr), mBulletSystem(BulletSys) {}

    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "NukeAndSpiderMineSystem"; }

    void ArmNuke(int GhostEntityID, int SiloEntityID, const Core::Math::Vec2& TargetPos);
    void CancelNuke(int EntityID);
    void PlaceSpiderMine(int VultureEntityID);
    bool IsSpiderMineTriggered(int MineEntityID) const;
    void TriggerSpiderMine(int MineEntityID, int TargetEntityID);

private:
    void UpdateNuke(float DeltaTime);
    void DetonateNuke();
    void UpdateSpiderMines(float DeltaTime);
    bool CheckSpiderMineTrigger(int MineEntityID);

    Core::ECS::SystemManager& mSystemMgr;
    class BulletSystem& mBulletSystem;

    // Nuke internal state
    int mNukeGhostEntityID = -1;
    int mNukeSiloEntityID = -1;
    Core::Math::Vec2 mNukeTargetPosition;
    float mNukeArmTimer = 0.0f;
    float mNukeFlightTimer = 0.0f;
    bool mNukeArmed = false;
    bool mNukeLaunched = false;
    bool mNukeDetonated = false;
    bool mNukeHasNukeDot = false;
    int mNukeDotEntityID = -1;

    struct SpiderMineState
    {
        int VultureEntityID = -1;
        int MineEntityID = -1;
        Core::Math::Vec2 PlacementPosition;
        float BurrowTimer = 0.0f;
        float TriggerRange = 96.0f;
        float ChaseSpeed = 400.0f;
        int MaxMines = 3;
        int CurrentMineCount = 0;
        bool Burrowed = false;
    };

    std::vector<SpiderMineState> mMines;
    float mNukeCooldownRemaining = 0.0f;
};

} // namespace Game::Systems
