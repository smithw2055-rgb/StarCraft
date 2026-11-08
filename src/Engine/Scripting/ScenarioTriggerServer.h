#pragma once

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Utils/JsonLite.h"
#include "Engine/AI/BehaviorTreeServer.h"

namespace Engine::Scripting
{

// ============================================================================
// ScenarioTriggerServer (Phase 58):
// Referenced from Godot 4 Signal / EventSheet & Unity Visual Scripting:
// - General-purpose deterministic Condition -> Action scenario rule engine.
// - Decouples map/campaign trigger evaluation (timers, switches, resource/score
//   thresholds, entity area predicates, victory/defeat signals) from game-specific
//   binary chunk formats.
// ============================================================================
enum class TriggerConditionKind : uint8_t
{
    Always = 0,
    ElapsedTicksAtLeast = 1,
    BlackboardIntAtLeast = 2,
    BlackboardBoolEquals = 3,
    CustomPredicate = 4
};

struct TriggerCondition
{
    TriggerConditionKind Kind = TriggerConditionKind::Always;
    std::string Key;
    int64_t TargetInt = 0;
    bool TargetBool = true;
    std::function<bool(const Engine::AI::Blackboard&, uint64_t)> CustomCheck;
};

enum class TriggerActionKind : uint8_t
{
    SetBlackboardInt = 0,
    AddBlackboardInt = 1,
    SetBlackboardBool = 2,
    EmitSignal = 3,
    WaitTicks = 4,
    CustomCallback = 5
};

struct TriggerAction
{
    TriggerActionKind Kind = TriggerActionKind::SetBlackboardInt;
    std::string Key;
    int64_t IntValue = 0;
    bool BoolValue = true;
    uint32_t WaitTicks = 0;
    std::function<void(Engine::AI::Blackboard&, uint64_t)> CustomExec;
};

struct ScenarioRule
{
    uint32_t RuleID = 0;
    std::string Name;
    uint32_t PlayerMask = 0xFFFFFFFFu;
    bool PreserveTrigger = false;
    bool Enabled = true;
    uint64_t WaitUntilTick = 0;
    uint32_t ExecutionCount = 0;
    std::vector<TriggerCondition> Conditions;
    std::vector<TriggerAction> Actions;
};

struct ScenarioSignalEvent
{
    uint64_t Tick = 0;
    uint32_t RuleID = 0;
    std::string SignalName;
    int64_t Payload = 0;
};

class ScenarioTriggerServer
{
public:
    ScenarioTriggerServer() = default;

    uint32_t RegisterRule(ScenarioRule Rule)
    {
        if (Rule.RuleID == 0)
            Rule.RuleID = static_cast<uint32_t>(mRules.size() + 1);
        const uint32_t AssignedID = Rule.RuleID;
        mRules.push_back(std::move(Rule));
        return AssignedID;
    }

    bool ImportScenarioMetadataFromChkJsonFile(const std::string& FilePath)
    {
        std::ifstream In(FilePath);
        if (!In.is_open())
            return false;
        std::ostringstream SS;
        SS << In.rdbuf();
        return ImportScenarioMetadataFromChkJson(SS.str());
    }

    bool ImportScenarioMetadataFromChkJson(std::string_view JsonText)
    {
        const auto Root = Core::Utils::JsonParser::ParseString(std::string(JsonText));
        if (!Root.IsObject())
            return false;

        mSceneName = Root["scene_name"].AsString();
        mTriggerChunkByteSize = 0;
        const auto& Chunks = Root["chunks_inventory"];
        if (Chunks.IsArray())
        {
            for (const auto& C : Chunks.AsArray())
            {
                if (C["chunk_id"].AsString() == "TRIG")
                {
                    mTriggerChunkByteSize = static_cast<uint32_t>(C["byte_size"].AsInt(0));
                    break;
                }
            }
        }
        // Standard StarCraft TRIG record is 2400 bytes per trigger
        mImportedTriggerRecordCount = mTriggerChunkByteSize / 2400u;
        return !mSceneName.empty();
    }

    uint32_t EvaluateTick(uint64_t CurrentTick)
    {
        uint32_t FiredRules = 0;
        const size_t RuleCount = mRules.size();
        for (size_t i = 0; i < RuleCount; ++i)
        {
            auto& Rule = mRules[i];
            if (!Rule.Enabled || CurrentTick < Rule.WaitUntilTick)
                continue;

            bool AllMet = true;
            for (const auto& Cond : Rule.Conditions)
            {
                if (!EvaluateCondition(Cond, CurrentTick))
                {
                    AllMet = false;
                    break;
                }
            }
            if (!AllMet)
                continue;

            const size_t ActionCount = Rule.Actions.size();
            for (size_t a = 0; a < ActionCount; ++a)
            {
                const TriggerAction ActCopy = Rule.Actions[a];
                ExecuteAction(mRules[i], ActCopy, CurrentTick);
            }

            ++mRules[i].ExecutionCount;
            ++FiredRules;
            if (!mRules[i].PreserveTrigger)
                mRules[i].Enabled = false;
        }
        return FiredRules;
    }

    void Clear() noexcept
    {
        mRules.clear();
        mEmittedSignals.clear();
        mBlackboard = Engine::AI::Blackboard{};
        mSceneName.clear();
        mTriggerChunkByteSize = 0;
        mImportedTriggerRecordCount = 0;
    }

    Engine::AI::Blackboard& Blackboard() noexcept { return mBlackboard; }
    const Engine::AI::Blackboard& Blackboard() const noexcept { return mBlackboard; }

    const std::vector<ScenarioRule>& GetRules() const noexcept { return mRules; }
    const std::vector<ScenarioSignalEvent>& GetEmittedSignals() const noexcept { return mEmittedSignals; }
    const std::string& GetSceneName() const noexcept { return mSceneName; }
    uint32_t GetTriggerChunkByteSize() const noexcept { return mTriggerChunkByteSize; }
    uint32_t GetImportedTriggerRecordCount() const noexcept { return mImportedTriggerRecordCount; }

private:
    bool EvaluateCondition(const TriggerCondition& Cond, uint64_t CurrentTick) const
    {
        switch (Cond.Kind)
        {
        case TriggerConditionKind::Always:
            return true;
        case TriggerConditionKind::ElapsedTicksAtLeast:
            return static_cast<int64_t>(CurrentTick) >= Cond.TargetInt;
        case TriggerConditionKind::BlackboardIntAtLeast:
            return mBlackboard.GetInt(Cond.Key, 0) >= Cond.TargetInt;
        case TriggerConditionKind::BlackboardBoolEquals:
            return mBlackboard.GetBool(Cond.Key, false) == Cond.TargetBool;
        case TriggerConditionKind::CustomPredicate:
            return Cond.CustomCheck ? Cond.CustomCheck(mBlackboard, CurrentTick) : false;
        }
        return false;
    }

    void ExecuteAction(ScenarioRule& Rule, const TriggerAction& Act, uint64_t CurrentTick)
    {
        switch (Act.Kind)
        {
        case TriggerActionKind::SetBlackboardInt:
            mBlackboard.SetInt(Act.Key, Act.IntValue);
            break;
        case TriggerActionKind::AddBlackboardInt:
            mBlackboard.SetInt(Act.Key, mBlackboard.GetInt(Act.Key, 0) + Act.IntValue);
            break;
        case TriggerActionKind::SetBlackboardBool:
            mBlackboard.SetBool(Act.Key, Act.BoolValue);
            break;
        case TriggerActionKind::EmitSignal:
            mEmittedSignals.push_back(ScenarioSignalEvent{CurrentTick, Rule.RuleID, Act.Key, Act.IntValue});
            break;
        case TriggerActionKind::WaitTicks:
            Rule.WaitUntilTick = CurrentTick + Act.WaitTicks;
            break;
        case TriggerActionKind::CustomCallback:
            if (Act.CustomExec)
                Act.CustomExec(mBlackboard, CurrentTick);
            break;
        }
    }

    Engine::AI::Blackboard mBlackboard;
    std::vector<ScenarioRule> mRules;
    std::vector<ScenarioSignalEvent> mEmittedSignals;
    std::string mSceneName;
    uint32_t mTriggerChunkByteSize = 0;
    uint32_t mImportedTriggerRecordCount = 0;
};

} // namespace Engine::Scripting
