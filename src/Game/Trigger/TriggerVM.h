#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/Trigger/SCRTriggerParser.h"

namespace Game::Trigger
{

enum class TriggerOutcome : uint8_t
{
    Playing,
    Victory,
    Defeat,
    Draw
};

bool IsBuildingUnitID(int ChkUnitID);
bool IsFactoryUnitID(int ChkUnitID);
bool MatchUnitGroup(int UnitID, uint16_t UnitGroup);

struct TriggerRuntimeEvent
{
    uint8_t ActionID = 0;
    int PlayerID = 0;
    std::string Text;
    std::string SoundPath;
    Core::Math::Vec2 Position;
    uint32_t Data = 0;
    uint32_t DurationMs = 0;
};

struct TriggerLocation
{
    uint32_t ID = 0;
    std::string Name;
    int32_t Left = 0;
    int32_t Top = 0;
    int32_t Right = 0;
    int32_t Bottom = 0;
    uint16_t ElevationFlags = 0;

    Core::Math::Vec2 Center() const
    {
        return { (float)(Left + Right) * 0.5f, (float)(Top + Bottom) * 0.5f };
    }

    bool Contains(Core::Math::Vec2 Pos) const
    {
        const int32_t MinX = Left < Right ? Left : Right;
        const int32_t MaxX = Left < Right ? Right : Left;
        const int32_t MinY = Top < Bottom ? Top : Bottom;
        const int32_t MaxY = Top < Bottom ? Bottom : Top;
        return Pos.X >= (float)MinX && Pos.X <= (float)MaxX &&
               Pos.Y >= (float)MinY && Pos.Y <= (float)MaxY;
    }
};

struct TriggerRuntimeConfig
{
    std::function<void(int, int, bool)> SetAlliance;
    std::function<int(int, Core::Math::Vec2, int)> ResolveImageID;
    std::function<bool(int, uint32_t)> CanBurrow;
    std::function<Game::Components::UnitCollisionBounds(int)> ResolveCollisionBounds;
    std::function<void(Core::Math::Vec2)> CenterView;
    std::function<void(Core::Math::Vec2)> MinimapPing;
    std::function<void(bool)> SetGamePaused;
    std::function<uint32_t(int)> ResolveSubunitImageID;
};

class TriggerVM
{
public:
    TriggerVM();

    bool LoadScript(const SCRTriggerData& Data);
    void SetRuntimeConfig(TriggerRuntimeConfig Config) { mConfig = std::move(Config); }

    void Reset();
    void Update(Core::ECS::EntityManager& EM, float DeltaTime);

    bool HasScript() const { return !mTriggers.empty(); }
    size_t GetTriggerCount() const { return mTriggers.size(); }
    size_t GetLocationCount() const { return mLocations.size(); }
    const TriggerLocation* GetLocationById(uint32_t LocationID) const;

    void SetSwitch(int Idx, bool Value);
    bool GetSwitch(int Idx) const;
    void SetCountdownTimer(int Seconds);
    int GetCountdownTimer() const { return (int)std::ceil(mCountdownTimer); }
    void SetTimerPaused(bool Paused) { mTimerPaused = Paused; }
    void AddDeath(int PlayerID, int UnitID, int Count = 1);
    int GetDeathCount(int PlayerID, int UnitID) const;
    int GetKillCount(int PlayerID, int UnitID) const;
    int GetKillScore(int PlayerID) const;
    int GetScore(int PlayerID) const;

    TriggerOutcome GetPlayerOutcome(int PlayerID) const;
    const std::vector<std::string>& GetObjectives() const { return mObjectives; }
    const std::vector<TriggerRuntimeEvent>& GetEvents() const { return mEvents; }
    void ClearEvents() { mEvents.clear(); }
    const std::array<int, 256>& GetUnsupportedConditionCounts() const { return mUnsupportedConditionCounts; }
    const std::array<int, 256>& GetUnsupportedActionCounts() const { return mUnsupportedActionCounts; }
    int GetActiveAIScriptCount() const;

private:
    struct RunningTrigger
    {
        size_t TriggerIndex = 0;
        int Owner = 0;
        uint32_t Flags = 0;
        size_t CurrentActionIndex = 0;
        std::array<uint8_t, SCRTrigger::kActionCount> ActionFlags{};
        bool Completed = false;
        bool Waiting = false;
        int WaitTimerMs = 0;
        float WaitTimerRemainingSeconds = 0.0f;
    };

    struct RunningAIScript
    {
        bool Active = false;
        int PlayerID = 0;
        uint32_t ScriptID = 0;
        bool HasLocation = false;
        Core::Math::Vec2 Anchor;
        Core::Math::Vec2 LastIssuedTarget;
        bool HasIssuedTarget = false;
        std::unordered_map<int, Core::Math::Vec2> AssignedTargets;
        std::unordered_map<int, float> AssignedTolerances;
        float TickAccumulator = 0.0f;
        int WaveCount = 0;
    };

    struct TrackedUnitInfo
    {
        int PlayerID = -1;
        int UnitID = -1;
    };

    std::vector<int> ResolvePlayers(int Owner, uint32_t PlayerGroup) const;
    bool IsPlayerActive(int PlayerID) const;
    bool AreAllied(int PlayerA, int PlayerB) const;
    bool CompareCount(uint8_t ComparisonType, int Count, uint32_t Amount) const;
    bool MatchUnitGroup(int UnitID, uint16_t UnitGroup) const;
    bool MatchPlayerGroup(int Owner, uint32_t PlayerGroup, int CandidatePlayer) const;
    std::vector<int> FindMatchingUnits(Core::ECS::EntityManager& EM,
                                       int Owner,
                                       uint32_t PlayerGroup,
                                       uint16_t UnitGroup,
                                       uint32_t LocationID,
                                       bool IncludeDead = false) const;
    int CountMatchingUnits(Core::ECS::EntityManager& EM,
                           int Owner,
                           uint32_t PlayerGroup,
                           uint16_t UnitGroup,
                           uint32_t LocationID,
                           bool IncludeDead = false) const;
    bool TestCondition(Core::ECS::EntityManager& EM, const SCRCondition& Cond, int Owner) const;
    bool ExecuteAction(Core::ECS::EntityManager& EM,
                       RunningTrigger& Running,
                       const SCRAction& Action,
                       int Owner);

    int CreateUnit(Core::ECS::EntityManager& EM, int ChkUnitID, Core::Math::Vec2 Pos, int Owner, const SCRUnitProperties* Props);
    void SyncDeathsFromEntityManager(Core::ECS::EntityManager& EM);
    void UpdateRescuePassiveUnits(Core::ECS::EntityManager& EM);
    void RecordUnitDeath(int VictimPlayerID, int UnitID, int KillerPlayerID, bool AwardKill);
    void StartAIScript(Core::ECS::EntityManager& EM, int Owner, uint32_t ScriptID, uint32_t LocationID, bool HasLocation);
    void UpdateAIScripts(Core::ECS::EntityManager& EM, float DeltaTime);
    Core::Math::Vec2 ResolveAIScriptTarget(Core::ECS::EntityManager& EM, const RunningAIScript& Script) const;
    int IssueAIScriptOrders(Core::ECS::EntityManager& EM, RunningAIScript& Script, const Core::Math::Vec2& Target);
    void RecordUnsupportedCondition(uint8_t ConditionType) const;
    void RecordUnsupportedAction(uint8_t ActionType) const;
    void ApplyNumberModifier(int& Value, uint8_t Modifier, int Amount);
    void Emit(uint8_t ActionID, int PlayerID, const std::string& Text,
              Core::Math::Vec2 Position = {}, uint32_t Data = 0,
              std::string SoundPath = {}, uint32_t DurationMs = 0);

    SCRTriggerData mData;
    std::vector<TriggerLocation> mLocations;
    std::vector<SCRTrigger> mTriggers;
    std::vector<RunningTrigger> mRunningTriggers;
    std::array<bool, 256> mSwitches{};
    std::array<std::array<uint8_t, 12>, 12> mAlliance{};
    std::array<TriggerOutcome, 12> mOutcomes{};
    std::array<std::array<int, 233>, 12> mDeaths{};
    std::array<std::array<int, 233>, 12> mKills{};
    std::array<int, 12> mScores{};
    std::array<int, 12> mKillScores{};
    std::array<int, 12> mWaitTimersMs{};
    std::array<bool, 12> mWaiting{};
    std::array<RunningAIScript, 12> mAIScripts{};
    std::vector<std::string> mObjectives;
    std::vector<TriggerRuntimeEvent> mEvents;
    TriggerRuntimeConfig mConfig;
    std::unordered_map<int, TrackedUnitInfo> mTrackedUnits;
    mutable std::array<int, 256> mUnsupportedConditionCounts{};
    mutable std::array<int, 256> mUnsupportedActionCounts{};
    float mCountdownTimer = 0.0f;
    float mGameTime = 0.0f;
    float mTriggerAccumulator = 0.0f;
    bool mForceTriggerStep = true;
    bool mTimerPaused = false;
    bool mGamePaused = false;
    bool mDebugEnabled = false;
};

} // namespace Game::Trigger
