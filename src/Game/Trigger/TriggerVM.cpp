#include "Game/Trigger/TriggerVM.h"

#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Game::Trigger
{
using namespace Game::Components;
using Core::Utils::Logger;

namespace
{
    constexpr float kTriggerStepSeconds = 1.0f;
    constexpr int kTriggerStepMs = 1000;
    constexpr uint32_t kAnywhereLocationId = 64;
    constexpr float kAIScriptRetargetPixels = 8.0f;
    constexpr float kRescuePassiveRangePixels = 96.0f;
    constexpr uint8_t kChkOwnerRescuePassive = 3;
    constexpr uint8_t kChkOwnerHuman = 6;

    bool IsBuildingUnitID(int ChkUnitID)
    {
        if ((ChkUnitID >= 106 && ChkUnitID <= 127) || (ChkUnitID >= 130 && ChkUnitID <= 152))
            return true;
        if (ChkUnitID >= 154 && ChkUnitID <= 175)
            return true;
        if (ChkUnitID >= 189 && ChkUnitID <= 213)
            return true;
        return false;
    }

    bool IsFactoryUnitID(int ChkUnitID)
    {
        switch (ChkUnitID)
        {
            case 111: case 113: case 114: case 115: case 116:
            case 131: case 132: case 133: case 154: case 155:
            case 156: case 157: case 159: case 160: return true;
            default: return false;
        }
    }

    Race RaceFromChkUnit(int ChkUnitID)
    {
        int DefID = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitID);
        if (DefID >= 0)
            return Game::Races::GetUnitDefByID(DefID).RaceType;
        if ((ChkUnitID >= 37 && ChkUnitID <= 62) || (ChkUnitID >= 131 && ChkUnitID <= 151))
            return Race::Zerg;
        if ((ChkUnitID >= 64 && ChkUnitID <= 84) || (ChkUnitID >= 154 && ChkUnitID <= 172))
            return Race::Protoss;
        return Race::Terran;
    }

    int NormalizeTriggerPlayerID(uint32_t PlayerID)
    {
        if (PlayerID >= 1 && PlayerID <= 8)
            return (int)PlayerID - 1;
        return (int)PlayerID;
    }

    size_t RuntimePlayerDataIndex(const SCRTriggerData& Data, int PlayerID)
    {
        const bool OneBasedPlayerSlots =
            Data.Players[0].Owner == 0 &&
            std::any_of(Data.Players.begin() + 1, Data.Players.begin() + 9,
                [](const SCRPlayerInfo& Info) { return Info.Owner != 0; });
        if (OneBasedPlayerSlots && PlayerID >= 0 && PlayerID < 8)
            return (size_t)PlayerID + 1u;
        if (PlayerID >= 0 && PlayerID < (int)Data.Players.size())
            return (size_t)PlayerID;
        return 0u;
    }

    bool RuntimePlayerHasOwner(const SCRTriggerData& Data, int PlayerID, uint8_t Owner)
    {
        if (PlayerID < 0 || PlayerID >= 8)
            return false;
        return Data.Players[RuntimePlayerDataIndex(Data, PlayerID)].Owner == Owner;
    }

    int ClampPlayer(int PlayerID)
    {
        return PlayerID >= 0 && PlayerID < 12 ? PlayerID : 0;
    }

    void ClearUnitMotionAfterTeleport(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }

        if (auto* Cmd = EM.GetComponent<CommandComponent>(EntityID))
        {
            Cmd->CancelAll();
            Cmd->CommandQueue.clear();
            Cmd->TargetEntityID = -1;
            Cmd->TargetPosition = {};
        }
    }
}

TriggerVM::TriggerVM()
{
    Reset();
}

bool TriggerVM::LoadScript(const SCRTriggerData& Data)
{
    mData = Data;
    mLocations.clear();
    mTriggers = Data.Triggers;
    mRunningTriggers.clear();
    mObjectives.clear();
    mEvents.clear();
    mUnsupportedConditionCounts.fill(0);
    mUnsupportedActionCounts.fill(0);
    mTrackedUnits.clear();

    for (const auto& Loc : Data.Locations)
    {
        TriggerLocation RuntimeLoc;
        RuntimeLoc.ID = Loc.ID;
        RuntimeLoc.Name = Loc.Name.empty() ? ("Location" + std::to_string(Loc.ID)) : Loc.Name;
        RuntimeLoc.Left = Loc.Left;
        RuntimeLoc.Top = Loc.Top;
        RuntimeLoc.Right = Loc.Right;
        RuntimeLoc.Bottom = Loc.Bottom;
        RuntimeLoc.ElevationFlags = Loc.ElevationFlags;
        mLocations.push_back(RuntimeLoc);
    }

    if (mLocations.empty())
    {
        TriggerLocation Anywhere;
        Anywhere.ID = kAnywhereLocationId;
        Anywhere.Name = "Anywhere";
        Anywhere.Left = 0;
        Anywhere.Top = 0;
        Anywhere.Right = 8192;
        Anywhere.Bottom = 8192;
        mLocations.push_back(Anywhere);
    }

    Reset();

    for (size_t Index = 0; Index < mTriggers.size(); ++Index)
    {
        const auto& Trigger = mTriggers[Index];
        for (int Owner = 0; Owner < 8; ++Owner)
        {
            const uint32_t ForceGroup =
                18u + (uint32_t)mData.Players[RuntimePlayerDataIndex(mData, Owner)].Force;
            const size_t TriggerOwnerIndex = (size_t)Owner + 1u;
            if ((TriggerOwnerIndex >= Trigger.Owners.size() || !Trigger.Owners[TriggerOwnerIndex]) &&
                !Trigger.Owners[17] &&
                (ForceGroup >= Trigger.Owners.size() || !Trigger.Owners[ForceGroup]))
                continue;

            RunningTrigger Running;
            Running.TriggerIndex = Index;
            Running.Owner = Owner;
            Running.Flags = Trigger.ExecutionFlags;
            Running.Completed = (Running.Flags & 0x08) != 0;
            mRunningTriggers.push_back(Running);
        }
    }

    Logger::Info("TriggerVM",
        "Loaded CHK triggers: raw=" + std::to_string(mTriggers.size()) +
        " running=" + std::to_string(mRunningTriggers.size()) +
        " locations=" + std::to_string(mLocations.size()));
    return !mRunningTriggers.empty();
}

void TriggerVM::Reset()
{
    mSwitches.fill(false);
    mOutcomes.fill(TriggerOutcome::Playing);
    mScores.fill(0);
    mKillScores.fill(0);
    mWaitTimersMs.fill(0);
    mWaiting.fill(false);
    mAIScripts = {};
    mDeaths = {};
    mKills = {};
    mTrackedUnits.clear();
    mCountdownTimer = 0.0f;
    mGameTime = 0.0f;
    mTriggerAccumulator = 0.0f;
    mForceTriggerStep = true;
    mTimerPaused = false;
    mGamePaused = false;
    mDebugEnabled = false;

    for (int A = 0; A < 12; ++A)
        for (int B = 0; B < 12; ++B)
            mAlliance[A][B] = (A == B) ? 2 : 0;
}

void TriggerVM::Update(Core::ECS::EntityManager& EM, float DeltaTime)
{
    if (DeltaTime <= 0.0f || mRunningTriggers.empty())
        return;

    SyncDeathsFromEntityManager(EM);
    UpdateAIScripts(EM, DeltaTime);
    UpdateRescuePassiveUnits(EM);
    if (!mTimerPaused && mCountdownTimer > 0.0f)
        mCountdownTimer = std::max(0.0f, mCountdownTimer - DeltaTime);

    for (int Player = 0; Player < 12; ++Player)
    {
        if (!mWaiting[Player]) continue;
        if (mWaitTimersMs[Player] <= 0)
        {
            mWaiting[Player] = false;
            continue;
        }
        mWaitTimersMs[Player] = std::max(0, mWaitTimersMs[Player] - (int)(DeltaTime * 1000.0f));
        if (mWaitTimersMs[Player] == 0)
            mWaiting[Player] = false;
    }

    mTriggerAccumulator += DeltaTime;
    bool RunStep = mForceTriggerStep || mTriggerAccumulator >= kTriggerStepSeconds;
    mForceTriggerStep = false;
    int Steps = 0;
    while (RunStep && Steps < 8)
    {
        if (mTriggerAccumulator >= kTriggerStepSeconds)
            mTriggerAccumulator -= kTriggerStepSeconds;
        ++Steps;

        for (auto& Running : mRunningTriggers)
        {
            if (Running.Completed)
                continue;
            if (mWaiting[ClampPlayer(Running.Owner)])
                continue;

            const auto& Trigger = mTriggers[Running.TriggerIndex];
            bool AllMet = true;
            if ((Running.Flags & 0x01) == 0)
            {
                for (const auto& Cond : Trigger.Conditions)
                {
                    if (Cond.ConditionType == 0)
                        break;
                    if (Cond.Disabled())
                        continue;
                    if (!TestCondition(EM, Cond, Running.Owner))
                    {
                        AllMet = false;
                        break;
                    }
                }
            }
            if (!AllMet)
                continue;

            const bool ContinuingActions = (Running.Flags & 0x01) != 0;
            Running.Flags |= 0x01;
            if (!ContinuingActions)
                Running.CurrentActionIndex = 0;
            for (; Running.CurrentActionIndex < Trigger.Actions.size(); ++Running.CurrentActionIndex)
            {
                const auto& Action = Trigger.Actions[Running.CurrentActionIndex];
                if (Action.ActionType == 0)
                {
                    Running.CurrentActionIndex = Trigger.Actions.size();
                    break;
                }
                if (Action.Disabled())
                    continue;
                if (!ExecuteAction(EM, Running, Action, Running.Owner))
                    break;
            }

            if (Running.CurrentActionIndex >= Trigger.Actions.size())
            {
                if ((Running.Flags & 0x04) != 0)
                {
                    Running.CurrentActionIndex = 0;
                    Running.Flags &= ~0x51u;
                }
                else
                {
                    Running.Completed = true;
                    Running.Flags |= 0x08;
                }
            }
        }

        RunStep = mTriggerAccumulator >= kTriggerStepSeconds;
    }
    if (Steps >= 8 && mTriggerAccumulator >= kTriggerStepSeconds)
        mTriggerAccumulator = 0.0f;

    mGameTime += DeltaTime;
}

const TriggerLocation* TriggerVM::GetLocationById(uint32_t LocationID) const
{
    if (LocationID == 0)
        return nullptr;
    for (const auto& Loc : mLocations)
        if (Loc.ID == LocationID)
            return &Loc;
    if (LocationID > 0 && LocationID <= mLocations.size())
        return &mLocations[(size_t)LocationID - 1u];
    return nullptr;
}

void TriggerVM::SetSwitch(int Idx, bool Value)
{
    if (Idx >= 0 && Idx < (int)mSwitches.size())
        mSwitches[(size_t)Idx] = Value;
}

bool TriggerVM::GetSwitch(int Idx) const
{
    if (Idx >= 0 && Idx < (int)mSwitches.size())
        return mSwitches[(size_t)Idx];
    return false;
}

void TriggerVM::SetCountdownTimer(int Seconds)
{
    mCountdownTimer = (float)std::max(0, Seconds);
}

void TriggerVM::AddDeath(int PlayerID, int UnitID, int Count)
{
    if (PlayerID < 0 || PlayerID >= 12 || UnitID < 0 || UnitID >= 233)
        return;
    mDeaths[(size_t)PlayerID][(size_t)UnitID] += Count;
}

int TriggerVM::GetDeathCount(int PlayerID, int UnitID) const
{
    if (PlayerID < 0 || PlayerID >= 12 || UnitID < 0 || UnitID >= 233)
        return 0;
    return mDeaths[(size_t)PlayerID][(size_t)UnitID];
}

int TriggerVM::GetKillCount(int PlayerID, int UnitID) const
{
    if (PlayerID < 0 || PlayerID >= 12 || UnitID < 0 || UnitID >= 233)
        return 0;
    return mKills[(size_t)PlayerID][(size_t)UnitID];
}

int TriggerVM::GetKillScore(int PlayerID) const
{
    if (PlayerID < 0 || PlayerID >= 12)
        return 0;
    return mKillScores[(size_t)PlayerID];
}

int TriggerVM::GetScore(int PlayerID) const
{
    if (PlayerID < 0 || PlayerID >= 12)
        return 0;
    return mScores[(size_t)PlayerID];
}

TriggerOutcome TriggerVM::GetPlayerOutcome(int PlayerID) const
{
    if (PlayerID < 0 || PlayerID >= (int)mOutcomes.size())
        return TriggerOutcome::Playing;
    return mOutcomes[(size_t)PlayerID];
}

int TriggerVM::GetActiveAIScriptCount() const
{
    int Count = 0;
    for (const auto& Script : mAIScripts)
        if (Script.Active)
            ++Count;
    return Count;
}

std::vector<int> TriggerVM::ResolvePlayers(int Owner, uint32_t PlayerGroup) const
{
    std::vector<int> Players;
    for (int P = 0; P < 12; ++P)
        if (MatchPlayerGroup(Owner, PlayerGroup, P))
            Players.push_back(P);
    return Players;
}

bool TriggerVM::IsPlayerActive(int PlayerID) const
{
    if (PlayerID < 0 || PlayerID >= 12)
        return false;
    if (PlayerID >= 8)
        return mData.Players[(size_t)PlayerID].Owner != 0;
    return mData.Players[RuntimePlayerDataIndex(mData, PlayerID)].Owner != 0 ||
        mData.RawData.empty();
}

bool TriggerVM::AreAllied(int PlayerA, int PlayerB) const
{
    if (PlayerA < 0 || PlayerA >= 12 || PlayerB < 0 || PlayerB >= 12)
        return false;
    return mAlliance[(size_t)PlayerA][(size_t)PlayerB] >= 1;
}

bool TriggerVM::CompareCount(uint8_t ComparisonType, int Count, uint32_t Amount) const
{
    switch (ComparisonType)
    {
        case 0: return Count >= (int)Amount;
        case 1: return Count <= (int)Amount;
        case 2:
        case 10: return Count == (int)Amount;
        default: return false;
    }
}

bool TriggerVM::MatchUnitGroup(int UnitID, uint16_t UnitGroup) const
{
    if (UnitGroup == 228) return false;
    if (UnitGroup == 229) return true;
    if (UnitGroup == 230) return !IsBuildingUnitID(UnitID);
    if (UnitGroup == 231) return IsBuildingUnitID(UnitID);
    if (UnitGroup == 232) return IsFactoryUnitID(UnitID);
    return UnitID == (int)UnitGroup;
}

bool TriggerVM::MatchPlayerGroup(int Owner, uint32_t PlayerGroup, int CandidatePlayer) const
{
    if (!IsPlayerActive(CandidatePlayer))
        return false;
    if (PlayerGroup < 12)
        return CandidatePlayer == NormalizeTriggerPlayerID(PlayerGroup);
    switch (PlayerGroup)
    {
        case 12: return false;
        case 13: return CandidatePlayer == Owner;
        case 14: return CandidatePlayer != Owner && !AreAllied(Owner, CandidatePlayer);
        case 15: return CandidatePlayer != Owner && AreAllied(Owner, CandidatePlayer);
        case 16: return CandidatePlayer != Owner && mAlliance[(size_t)Owner][(size_t)CandidatePlayer] == 1;
        case 17: return CandidatePlayer < 8;
        case 18: case 19: case 20: case 21:
            return CandidatePlayer < 8 &&
                mData.Players[RuntimePlayerDataIndex(mData, CandidatePlayer)].Force ==
                    (uint8_t)(PlayerGroup - 18);
        case 26:
            return CandidatePlayer != Owner && !AreAllied(Owner, CandidatePlayer);
        default:
            return false;
    }
}

std::vector<int> TriggerVM::FindMatchingUnits(Core::ECS::EntityManager& EM,
                                              int Owner,
                                              uint32_t PlayerGroup,
                                              uint16_t UnitGroup,
                                              uint32_t LocationID,
                                              bool IncludeDead) const
{
    std::vector<int> Matches;
    const TriggerLocation* Loc = GetLocationById(LocationID);
    auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
    for (int ID : Units)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Player = EM.GetComponent<PlayerComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        if (!Unit || !Player || !Trans) continue;
        if (!IncludeDead && Unit->HitPoints <= 0) continue;
        if (!MatchPlayerGroup(Owner, PlayerGroup, Player->PlayerID)) continue;
        if (!MatchUnitGroup(Unit->ChkUnitID, UnitGroup)) continue;
        if (Loc && !Loc->Contains(Trans->Position)) continue;
        Matches.push_back(ID);
    }
    return Matches;
}

int TriggerVM::CountMatchingUnits(Core::ECS::EntityManager& EM,
                                  int Owner,
                                  uint32_t PlayerGroup,
                                  uint16_t UnitGroup,
                                  uint32_t LocationID,
                                  bool IncludeDead) const
{
    return (int)FindMatchingUnits(EM, Owner, PlayerGroup, UnitGroup, LocationID, IncludeDead).size();
}

bool TriggerVM::TestCondition(Core::ECS::EntityManager& EM, const SCRCondition& Cond, int Owner) const
{
    switch (Cond.ConditionType)
    {
        case 0:
        case 22:
            return true;
        case 13:
        case 23:
            return false;
        case 1:
            return CompareCount(Cond.ComparisonType, (int)mCountdownTimer, Cond.Amount);
        case 2:
            return CompareCount(Cond.ComparisonType,
                CountMatchingUnits(EM, Owner, Cond.PlayerID, Cond.UnitID, 0), Cond.Amount);
        case 3:
            return CompareCount(Cond.ComparisonType,
                CountMatchingUnits(EM, Owner, Cond.PlayerID, Cond.UnitID, Cond.LocationID), Cond.Amount);
        case 4:
        {
            int Total = 0;
            for (int Player : ResolvePlayers(Owner, Cond.PlayerID))
            {
                auto& Res = Game::Systems::HarvestSystem::GetResources(Player);
                if (Cond.Extra == 0 || Cond.Extra == 2) Total += Res.Minerals;
                if (Cond.Extra == 1 || Cond.Extra == 2) Total += Res.VespeneGas;
            }
            return CompareCount(Cond.ComparisonType, Total, Cond.Amount);
        }
        case 5:
        {
            int Total = 0;
            for (int Player : ResolvePlayers(Owner, Cond.PlayerID))
                if (Cond.UnitID < 233)
                    Total += mKills[(size_t)std::min(Player, 11)][Cond.UnitID];
            return CompareCount(Cond.ComparisonType, Total, Cond.Amount);
        }
        case 15:
        {
            int Total = 0;
            for (int Player : ResolvePlayers(Owner, Cond.PlayerID))
                if (Cond.UnitID < 233)
                    Total += mDeaths[(size_t)std::min(Player, 11)][Cond.UnitID];
            return CompareCount(Cond.ComparisonType, Total, Cond.Amount);
        }
        case 6:
        case 7:
        case 16:
        case 17:
        {
            int OwnerCount = CountMatchingUnits(EM, Owner, Owner, Cond.UnitID,
                (Cond.ConditionType == 7 || Cond.ConditionType == 17) ? Cond.LocationID : 0);
            bool WantsMost = Cond.ConditionType == 6 || Cond.ConditionType == 7;
            for (int Player = 0; Player < 8; ++Player)
            {
                if (Player == Owner || !IsPlayerActive(Player)) continue;
                int Count = CountMatchingUnits(EM, Owner, Player, Cond.UnitID,
                    (Cond.ConditionType == 7 || Cond.ConditionType == 17) ? Cond.LocationID : 0);
                if (WantsMost && Count > OwnerCount) return false;
                if (!WantsMost && Count < OwnerCount) return false;
            }
            return true;
        }
        case 8:
        case 18:
        {
            bool WantsMost = Cond.ConditionType == 8;
            int OwnerKills = mKillScores[(size_t)Owner];
            for (int Player = 0; Player < 8; ++Player)
            {
                if (Player == Owner || !IsPlayerActive(Player)) continue;
                if (WantsMost && mKillScores[(size_t)Player] > OwnerKills) return false;
                if (!WantsMost && mKillScores[(size_t)Player] < OwnerKills) return false;
            }
            return true;
        }
        case 9:
        case 19:
        {
            bool WantsHighest = Cond.ConditionType == 9;
            int OwnerScore = mScores[(size_t)Owner];
            for (int Player = 0; Player < 8; ++Player)
            {
                if (Player == Owner || !IsPlayerActive(Player)) continue;
                if (WantsHighest && mScores[(size_t)Player] > OwnerScore) return false;
                if (!WantsHighest && mScores[(size_t)Player] < OwnerScore) return false;
            }
            return true;
        }
        case 10:
        case 20:
        {
            bool WantsMost = Cond.ConditionType == 10;
            auto ResValue = [](int P) {
                auto& Res = Game::Systems::HarvestSystem::GetResources(P);
                return Res.Minerals + Res.VespeneGas;
            };
            int OwnerRes = ResValue(Owner);
            for (int Player = 0; Player < 8; ++Player)
            {
                if (Player == Owner || !IsPlayerActive(Player)) continue;
                int Value = ResValue(Player);
                if (WantsMost && Value > OwnerRes) return false;
                if (!WantsMost && Value < OwnerRes) return false;
            }
            return true;
        }
        case 11:
        {
            bool SwitchSet = GetSwitch(Cond.Extra);
            if (Cond.ComparisonType == 2) return SwitchSet;
            if (Cond.ComparisonType == 3) return !SwitchSet;
            return SwitchSet;
        }
        case 12:
            return CompareCount(Cond.ComparisonType, (int)mGameTime, Cond.Amount);
        case 14:
        {
            int Count = 0;
            for (int Player : ResolvePlayers(Owner, Cond.PlayerID))
            {
                for (int Other = 0; Other < 8; ++Other)
                    if (Other != Player && IsPlayerActive(Other) && !AreAllied(Player, Other))
                        ++Count;
            }
            return CompareCount(Cond.ComparisonType, Count, Cond.Amount);
        }
        case 21:
        {
            int Total = 0;
            for (int Player : ResolvePlayers(Owner, Cond.PlayerID))
                Total += mScores[(size_t)std::min(Player, 11)];
            return CompareCount(Cond.ComparisonType, Total, Cond.Amount);
        }
        default:
            RecordUnsupportedCondition(Cond.ConditionType);
            return false;
    }
}

bool TriggerVM::ExecuteAction(Core::ECS::EntityManager& EM,
                              RunningTrigger& Running,
                              const SCRAction& Action,
                              int Owner)
{
    const uint8_t Type = Action.ActionType;
    const uint8_t Modifier = Action.Amount;
    switch (Type)
    {
        case 1:
            mOutcomes[(size_t)Owner] = TriggerOutcome::Victory;
            Emit(Type, Owner, "Victory");
            return true;
        case 2:
            mOutcomes[(size_t)Owner] = TriggerOutcome::Defeat;
            Emit(Type, Owner, "Defeat");
            return true;
        case 3:
            Running.Flags |= 0x04;
            return true;
        case 4:
            if (mWaiting[(size_t)Owner]) return false;
            if (Running.ActionFlags[Running.CurrentActionIndex] & 0x01)
            {
                Running.ActionFlags[Running.CurrentActionIndex] &= ~0x01;
                return true;
            }
            mWaiting[(size_t)Owner] = true;
            mWaitTimersMs[(size_t)Owner] = std::max(0, (int)Action.Time);
            Running.ActionFlags[Running.CurrentActionIndex] |= 0x01;
            return false;
        case 5:
            mGamePaused = true;
            if (mConfig.SetGamePaused)
                mConfig.SetGamePaused(true);
            Emit(Type, Owner, "Pause game");
            return true;
        case 6:
            mGamePaused = false;
            if (mConfig.SetGamePaused)
                mConfig.SetGamePaused(false);
            Emit(Type, Owner, "Unpause game");
            return true;
        case 7:
        {
            const TriggerLocation* Loc = GetLocationById(Action.LocationID);
            const Core::Math::Vec2 Pos = Loc ? Loc->Center() : Core::Math::Vec2{};
            if (Loc && mConfig.CenterView)
                mConfig.CenterView(Pos);
            Emit(Type, Owner, mData.GetString(Action.StringID), Pos, Action.UnitID,
                mData.GetWavPath(Action.WavID), Action.Time);
            return true;
        }
        case 8:
            Emit(Type, Owner, "", {}, 0, mData.GetWavPath(Action.WavID), Action.Time);
            return true;
        case 9:
        case 47:
            Emit(Type, Owner, mData.GetString(Action.StringID));
            return true;
        case 10:
        {
            const TriggerLocation* Loc = GetLocationById(Action.LocationID);
            if (Loc && mConfig.CenterView)
                mConfig.CenterView(Loc->Center());
            Emit(Type, Owner, "", Loc ? Loc->Center() : Core::Math::Vec2{});
            return true;
        }
        case 28:
        {
            const TriggerLocation* Loc = GetLocationById(Action.LocationID);
            if (Loc && mConfig.MinimapPing)
                mConfig.MinimapPing(Loc->Center());
            Emit(Type, Owner, "", Loc ? Loc->Center() : Core::Math::Vec2{});
            return true;
        }
        case 11:
        case 44:
        {
            const TriggerLocation* Loc = GetLocationById(Action.LocationID);
            if (!Loc) return true;
            const SCRUnitProperties* Props = nullptr;
            if (Type == 11 && Action.Secondary > 0 && Action.Secondary <= mData.UnitProperties.size())
                Props = &mData.UnitProperties[(size_t)Action.Secondary - 1u];
            int Count = Modifier == 0 ? 1 : Modifier;
            for (int Player : ResolvePlayers(Owner, Action.PlayerID))
            {
                Core::Math::Vec2 Pos = Loc->Center();
                for (int I = 0; I < Count; ++I)
                    CreateUnit(EM, Action.UnitID, { Pos.X + (float)(I * 32), Pos.Y }, Player, Props);
            }
            return true;
        }
        case 12:
        {
            std::string Text = mData.GetString(Action.StringID);
            if (!Text.empty())
                mObjectives.push_back(Text);
            Emit(Type, Owner, Text);
            return true;
        }
        case 13:
            if (Modifier == 4) SetSwitch((int)Action.Secondary, true);
            else if (Modifier == 5) SetSwitch((int)Action.Secondary, false);
            else if (Modifier == 6) SetSwitch((int)Action.Secondary, !GetSwitch((int)Action.Secondary));
            else if (Modifier == 11) SetSwitch((int)Action.Secondary, ((int)mGameTime + Owner + (int)Action.Secondary) & 1);
            return true;
        case 14:
        {
            int Timer = (int)mCountdownTimer;
            ApplyNumberModifier(Timer, Modifier, (int)Action.Time);
            mCountdownTimer = (float)std::max(0, Timer);
            return true;
        }
        case 15:
        case 16:
            StartAIScript(EM, Owner, Action.Secondary, Action.LocationID, Type == 16);
            return true;
        case 17: case 18:
            Emit(Type, Owner, mData.GetString(Action.StringID), {}, Action.UnitID);
            return true;
        case 19: case 20: case 21:
        case 32: case 33: case 34: case 35: case 36: case 37: case 40:
            Emit(Type, Owner, mData.GetString(Action.StringID), {}, Action.Secondary);
            return true;
        case 22:
        case 23:
        case 24:
        case 25:
        {
            uint32_t LocId = (Type == 23 || Type == 25) ? Action.LocationID : 0;
            int Limit = (Type == 23 || Type == 25) ? (Modifier == 0 ? 0 : Modifier) : 0;
            auto Units = FindMatchingUnits(EM, Owner, Action.PlayerID, Action.UnitID, LocId);
            int Done = 0;
            for (int ID : Units)
            {
                if (Limit > 0 && Done >= Limit) break;
                if (auto* Unit = EM.GetComponent<UnitComponent>(ID))
                {
                    Unit->HitPoints = 0;
                    if (Type == 22 || Type == 23)
                        Game::Systems::RecordUnitDeath(EM, ID, -1, false);
                }
                EM.DestroyEntity(ID);
                ++Done;
            }
            return true;
        }
        case 26:
            for (int Player : ResolvePlayers(Owner, Action.PlayerID))
            {
                auto& Res = Game::Systems::HarvestSystem::GetResources(Player);
                if (Action.UnitID == 0 || Action.UnitID == 2) ApplyNumberModifier(Res.Minerals, Modifier, (int)Action.Secondary);
                if (Action.UnitID == 1 || Action.UnitID == 2) ApplyNumberModifier(Res.VespeneGas, Modifier, (int)Action.Secondary);
            }
            return true;
        case 27:
            for (int Player : ResolvePlayers(Owner, Action.PlayerID))
                ApplyNumberModifier(mScores[(size_t)std::min(Player, 11)], Modifier, (int)Action.Secondary);
            return true;
        case 29:
            Emit(Type, Owner, "", {}, Action.UnitID, {}, Action.Time);
            return true;
        case 30: case 31:
            Emit(Type, Owner, "", {}, Action.UnitID);
            return true;
        case 38:
        {
            TriggerLocation* Source = nullptr;
            TriggerLocation* Dest = nullptr;
            for (auto& Loc : mLocations)
            {
                if (Loc.ID == Action.LocationID) Source = &Loc;
                if (Loc.ID == Action.Secondary) Dest = &Loc;
            }
            if (Source && Dest)
            {
                Core::Math::Vec2 Center = Source->Center();
                auto Units = FindMatchingUnits(EM, Owner, Action.PlayerID, Action.UnitID, Action.LocationID);
                if (!Units.empty())
                    if (auto* T = EM.GetComponent<TransformComponent>(Units.front()))
                        Center = T->Position;
                const Core::Math::Vec2 Old = Dest->Center();
                int DX = (int)(Center.X - Old.X);
                int DY = (int)(Center.Y - Old.Y);
                Dest->Left += DX; Dest->Right += DX; Dest->Top += DY; Dest->Bottom += DY;
            }
            return true;
        }
        case 39:
        {
            const TriggerLocation* Dest = GetLocationById(Action.Secondary);
            if (!Dest) return true;
            auto Units = FindMatchingUnits(EM, Owner, Action.PlayerID, Action.UnitID, Action.LocationID);
            int Limit = Modifier == 0 ? 0 : Modifier;
            int Done = 0;
            for (int ID : Units)
            {
                if (Limit > 0 && Done >= Limit) break;
                if (auto* T = EM.GetComponent<TransformComponent>(ID))
                {
                    T->Position = Dest->Center();
                    ClearUnitMotionAfterTeleport(EM, ID);
                }
                ++Done;
            }
            return true;
        }
        case 41:
            Emit(Type, Owner, mData.GetString(Action.StringID));
            return true;
        case 42:
            Emit(Type, Owner, "Set doodad state", GetLocationById(Action.LocationID) ? GetLocationById(Action.LocationID)->Center() : Core::Math::Vec2{}, Modifier);
            return true;
        case 43:
        {
            auto Units = FindMatchingUnits(EM, Owner, Action.PlayerID, Action.UnitID, Action.LocationID);
            for (int ID : Units)
                if (auto* Unit = EM.GetComponent<UnitComponent>(ID))
                {
                    if (Modifier == 4)
                        Unit->IsInvincible = true;
                    else if (Modifier == 5)
                        Unit->IsInvincible = false;
                    else if (Modifier == 6)
                        Unit->IsInvincible = !Unit->IsInvincible;
                }
            return true;
        }
        case 45:
            for (int Player : ResolvePlayers(Owner, Action.PlayerID))
                if (Action.UnitID < 233)
                    ApplyNumberModifier(mDeaths[(size_t)std::min(Player, 11)][Action.UnitID], Modifier, (int)Action.Secondary);
            return true;
        case 46:
        {
            const TriggerLocation* Dest = GetLocationById(Action.Secondary);
            if (!Dest) return true;
            auto Units = FindMatchingUnits(EM, Owner, Action.PlayerID, Action.UnitID, Action.LocationID);
            for (int ID : Units)
            {
                Order MoveOrder;
                MoveOrder.TargetPosition = Dest->Center();
                MoveOrder.TargetEntityID = -1;
                MoveOrder.Type = Modifier == 1 ? OrderType::Patrol :
                    (Modifier == 2 ? OrderType::AttackMove : OrderType::Move);
                Game::Systems::CommandSystem::IssueOrder(EM, ID, MoveOrder, false);
            }
            return true;
        }
        case 48:
        {
            auto Units = FindMatchingUnits(EM, Owner, Action.PlayerID, Action.UnitID, Action.LocationID);
            int Limit = Modifier == 0 ? 0 : Modifier;
            int Done = 0;
            for (int ID : Units)
            {
                if (Limit > 0 && Done >= Limit) break;
                if (auto* Player = EM.GetComponent<PlayerComponent>(ID))
                    Player->PlayerID = NormalizeTriggerPlayerID(Action.Secondary);
                ++Done;
            }
            return true;
        }
        case 49: case 50: case 51: case 53:
        {
            auto Units = FindMatchingUnits(EM, Owner, Action.PlayerID, Action.UnitID, Action.LocationID);
            int Limit = Action.Amount == 0 ? 0 : Action.Amount;
            int Done = 0;
            for (int ID : Units)
            {
                if (Limit > 0 && Done >= Limit) break;
                if (auto* Unit = EM.GetComponent<UnitComponent>(ID))
                {
                    int Percent = std::clamp((int)Action.Secondary, 0, 100);
                    if (Type == 49) Unit->HitPoints = std::max(1, Unit->MaxHitPoints * Percent / 100);
                    else if (Type == 50) Unit->EnergyPoints = Unit->MaxEnergyPoints * Percent / 100;
                    else if (Type == 51) Unit->ShieldPoints = Unit->MaxShieldPoints * Percent / 100;
                    else if (Type == 53) Unit->HangarCount = std::max(0, (int)Action.Secondary);
                }
                ++Done;
            }
            return true;
        }
        case 52:
        {
            auto Units = FindMatchingUnits(EM, Owner, Action.PlayerID, Action.UnitID, Action.LocationID);
            int Limit = Action.Amount == 0 ? 0 : Action.Amount;
            int Done = 0;
            for (int ID : Units)
            {
                if (Limit > 0 && Done >= Limit) break;
                if (auto* Res = EM.GetComponent<ResourceComponent>(ID))
                    Res->Amount = (int)Action.Secondary;
                ++Done;
            }
            return true;
        }
        case 54:
            SetTimerPaused(true);
            return true;
        case 55:
            SetTimerPaused(false);
            return true;
        case 56:
            mOutcomes[(size_t)Owner] = TriggerOutcome::Draw;
            Emit(Type, Owner, "Draw");
            return true;
        case 57:
            for (int Player : ResolvePlayers(Owner, Action.PlayerID))
            {
                bool Allied = Action.UnitID == 1 || Action.UnitID == 2;
                mAlliance[(size_t)Owner][(size_t)std::min(Player, 11)] = (uint8_t)Action.UnitID;
                if (mConfig.SetAlliance && Player < 8 && Owner < 8)
                    mConfig.SetAlliance(Owner, Player, Allied);
            }
            return true;
        case 58:
            mDebugEnabled = false;
            return true;
        case 59:
            mDebugEnabled = true;
            return true;
        default:
            RecordUnsupportedAction(Type);
            return true;
    }
}

int TriggerVM::CreateUnit(Core::ECS::EntityManager& EM, int ChkUnitID, Core::Math::Vec2 Pos, int Owner, const SCRUnitProperties* Props)
{
    int Eid = EM.CreateEntity();
    auto& Trans = EM.AddComponent<TransformComponent>(Eid);
    auto& Unit = EM.AddComponent<UnitComponent>(Eid);
    auto& Player = EM.AddComponent<PlayerComponent>(Eid);
    auto& Render = EM.AddComponent<RenderComponent>(Eid);

    Trans.Position = Pos;
    Unit.ChkUnitID = ChkUnitID;
    Unit.UnitRace = RaceFromChkUnit(ChkUnitID);
    Unit.MaxHitPoints = 100;
    Unit.HitPoints = 100;
    Unit.SightRange = 8;
    Unit.Speed = IsBuildingUnitID(ChkUnitID) ? 0.0f : 2.8f;
    Player.PlayerID = Owner;
    Render.ImageID = mConfig.ResolveImageID ? (uint32_t)mConfig.ResolveImageID(ChkUnitID, Pos, Owner) : (uint32_t)ChkUnitID;

    int UnitDefID = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitID);
    if (UnitDefID >= 0)
    {
        auto Def = Game::Races::GetUnitDefByID(UnitDefID);
        Unit.UnitRace = Def.RaceType;
        Unit.SizeType = Def.SizeType;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxShieldPoints = Def.ShieldPoints;
        Unit.ShieldPoints = Def.ShieldPoints;
        Unit.MaxEnergyPoints = Def.EnergyPoints;
        Unit.EnergyPoints = Def.EnergyPoints;
        Unit.Armor = Def.Armor;
        Unit.Speed = Def.Speed;
        Unit.SightRange = Def.SightRange;
        Unit.SupplyCost = Def.SupplyCost;
        if (Def.IsBuilding)
        {
            auto& Building = EM.AddComponent<BuildingComponent>(Eid);
            Building.IsBuilding = true;
            Building.UnderConstruction = false;
            Building.BuildingDefID = UnitDefID;
            Building.CanLiftOff = Def.CanLiftOff;
            Building.RequiresCreep = Def.RequiresCreep;
            Building.RequiresPylon = Def.RequiresPylon;
        }
        else
        {
            EM.AddComponent<CommandComponent>(Eid);
            auto& Path = EM.AddComponent<PathfindingComponent>(Eid);
            Path.IsAir = Game::Data::IsAirUnitType(ChkUnitID);
            if (mConfig.ResolveCollisionBounds)
                ApplyCollisionBounds(Path, mConfig.ResolveCollisionBounds(ChkUnitID));
        }
        if (Def.GroundDamage > 0 || Def.AirDamage > 0)
        {
            auto& Combat = EM.AddComponent<CombatComponent>(Eid);
            Combat.GroundDamage = Def.GroundDamage;
            Combat.AirDamage = Def.AirDamage;
            Combat.GroundDmgType = Def.GroundDmgType;
            Combat.AirDmgType = Def.AirDmgType;
            Combat.AttackRange = Def.AttackRange;
            Combat.AttackCooldown = Def.AttackCooldown;
            Combat.CanTargetGround = Def.GroundDamage > 0;
            Combat.CanTargetAir = Def.AirDamage > 0;
            Combat.MyElevation = Game::Data::IsAirUnitType(ChkUnitID) ?
                Elevation::Air :
                Elevation::Ground;
            Combat.IsRanged = Def.AttackRange > 1.5f;
        }
    }
    else if (IsBuildingUnitID(ChkUnitID))
    {
        auto& Building = EM.AddComponent<BuildingComponent>(Eid);
        Building.IsBuilding = true;
        Building.UnderConstruction = false;
    }
    else
    {
        EM.AddComponent<CommandComponent>(Eid);
        auto& Path = EM.AddComponent<PathfindingComponent>(Eid);
        Path.IsAir = Game::Data::IsAirUnitType(ChkUnitID);
        if (mConfig.ResolveCollisionBounds)
            ApplyCollisionBounds(Path, mConfig.ResolveCollisionBounds(ChkUnitID));
    }

    if ((IsBuildingUnitID(ChkUnitID) || ChkUnitID == 188 ||
         (ChkUnitID >= 176 && ChkUnitID <= 178)) &&
        !EM.GetComponent<PathfindingComponent>(Eid))
    {
        auto& Path = EM.AddComponent<PathfindingComponent>(Eid);
        if (mConfig.ResolveCollisionBounds)
            ApplyCollisionBounds(Path, mConfig.ResolveCollisionBounds(ChkUnitID));
    }

    if (ChkUnitID >= 176 && ChkUnitID <= 178)
    {
        auto& Res = EM.AddComponent<ResourceComponent>(Eid);
        Res.Type = ResourceType::Minerals;
        Res.Amount = Props && Props->ResourceAmount ? (int)Props->ResourceAmount : 1500;
        Res.MaxAmount = Res.Amount;
    }
    else if (ChkUnitID == 188)
    {
        auto& Res = EM.AddComponent<ResourceComponent>(Eid);
        Res.Type = ResourceType::VespeneGas;
        Res.Amount = Props && Props->ResourceAmount ? (int)Props->ResourceAmount : 5000;
        Res.MaxAmount = Res.Amount;
    }

    if (Props)
    {
        if (Props->ValidFieldFlags & 0x02) Unit.HitPoints = std::max(1, Unit.MaxHitPoints * Props->HitPointPercent / 100);
        if (Props->ValidFieldFlags & 0x04) Unit.ShieldPoints = Unit.MaxShieldPoints * Props->ShieldPercent / 100;
        if (Props->ValidFieldFlags & 0x08) Unit.EnergyPoints = Unit.MaxEnergyPoints * Props->EnergyPercent / 100;
        if (Props->ValidFieldFlags & 0x20) Unit.HangarCount = std::max(0, (int)Props->HangarCount);
        if (Props->ValidSpecialFlags & 0x10) Unit.IsInvincible = (Props->StateFlags & 0x10) != 0;
        if ((Props->ValidFieldFlags & 0x10) && EM.GetComponent<ResourceComponent>(Eid))
            EM.GetComponent<ResourceComponent>(Eid)->Amount = (int)Props->ResourceAmount;
        if (Props->ValidSpecialFlags & 0x13)
        {
            auto& Status = EM.AddComponent<CloakComponent>(Eid);
            Status.IsCloaked = (Props->StateFlags & 0x01) != 0;
            Status.IsBurrowed = (Props->StateFlags & 0x02) != 0;
        }
    }

    return Eid;
}

void TriggerVM::SyncDeathsFromEntityManager(Core::ECS::EntityManager& EM)
{
    auto AliveUnits = EM.GetEntitiesWith<UnitComponent, PlayerComponent>();
    for (int ID : AliveUnits)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Player = EM.GetComponent<PlayerComponent>(ID);
        if (!Unit || !Player || Unit->HitPoints <= 0)
            continue;
        mTrackedUnits[ID] = { Player->PlayerID, Unit->ChkUnitID };
    }

    for (auto It = mTrackedUnits.begin(); It != mTrackedUnits.end();)
    {
        const int EntityID = It->first;
        if (EM.IsAlive(EntityID))
        {
            ++It;
            continue;
        }

        TrackedUnitInfo Info = It->second;
        int KillerPlayerID = -1;
        bool AwardKill = false;
        if (auto* Record = EM.GetComponent<DeathRecordComponent>(EntityID))
        {
            if (Record->CountedByTriggers)
            {
                It = mTrackedUnits.erase(It);
                continue;
            }
            if (Record->VictimPlayerID >= 0)
                Info.PlayerID = Record->VictimPlayerID;
            if (Record->VictimUnitID >= 0)
                Info.UnitID = Record->VictimUnitID;
            KillerPlayerID = Record->KillerPlayerID;
            AwardKill = Record->AwardKill;
            Record->CountedByTriggers = true;
        }

        RecordUnitDeath(Info.PlayerID, Info.UnitID, KillerPlayerID, AwardKill);
        It = mTrackedUnits.erase(It);
    }
}

void TriggerVM::UpdateRescuePassiveUnits(Core::ECS::EntityManager& EM)
{
    if (mData.RawData.empty())
        return;

    const float RescueRangeSq = kRescuePassiveRangePixels * kRescuePassiveRangePixels;
    auto EntityIds = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
    for (int RescueID : EntityIds)
    {
        auto* RescueTransform = EM.GetComponent<TransformComponent>(RescueID);
        auto* RescueUnit = EM.GetComponent<UnitComponent>(RescueID);
        auto* RescuePlayer = EM.GetComponent<PlayerComponent>(RescueID);
        if (!RescueTransform || !RescueUnit || !RescuePlayer || RescueUnit->HitPoints <= 0)
            continue;
        if (!RuntimePlayerHasOwner(mData, RescuePlayer->PlayerID, kChkOwnerRescuePassive))
            continue;
        if (EM.GetComponent<ResourceComponent>(RescueID))
            continue;

        for (int RescuerID : EntityIds)
        {
            if (RescuerID == RescueID)
                continue;

            auto* RescuerTransform = EM.GetComponent<TransformComponent>(RescuerID);
            auto* RescuerUnit = EM.GetComponent<UnitComponent>(RescuerID);
            auto* RescuerPlayer = EM.GetComponent<PlayerComponent>(RescuerID);
            if (!RescuerTransform || !RescuerUnit || !RescuerPlayer || RescuerUnit->HitPoints <= 0)
                continue;
            if (!RuntimePlayerHasOwner(mData, RescuerPlayer->PlayerID, kChkOwnerHuman))
                continue;
            if (EM.GetComponent<ResourceComponent>(RescuerID) ||
                EM.GetComponent<BuildingComponent>(RescuerID))
            {
                continue;
            }

            const Core::Math::Vec2 Delta =
                RescueTransform->Position - RescuerTransform->Position;
            if (Delta.X * Delta.X + Delta.Y * Delta.Y > RescueRangeSq)
                continue;

            const int OldPlayerID = RescuePlayer->PlayerID;
            RescuePlayer->PlayerID = RescuerPlayer->PlayerID;
            Logger::Info(
                "TriggerVM",
                "Rescued passive unit entity=" + std::to_string(RescueID) +
                    " unit=" + std::to_string(RescueUnit->ChkUnitID) +
                    " from player=" + std::to_string(OldPlayerID) +
                    " to player=" + std::to_string(RescuePlayer->PlayerID));
            break;
        }
    }
}

void TriggerVM::RecordUnitDeath(int VictimPlayerID, int UnitID, int KillerPlayerID, bool AwardKill)
{
    if (VictimPlayerID >= 0 && VictimPlayerID < 12 && UnitID >= 0 && UnitID < 233)
        ++mDeaths[(size_t)VictimPlayerID][(size_t)UnitID];

    if (!AwardKill || KillerPlayerID < 0 || KillerPlayerID >= 12 || UnitID < 0 || UnitID >= 233)
        return;

    ++mKills[(size_t)KillerPlayerID][(size_t)UnitID];
    ++mKillScores[(size_t)KillerPlayerID];
    mScores[(size_t)KillerPlayerID] += 1;
}

void TriggerVM::StartAIScript(Core::ECS::EntityManager& EM, int Owner, uint32_t ScriptID, uint32_t LocationID, bool HasLocation)
{
    if (Owner < 0 || Owner >= (int)mAIScripts.size())
        return;

    auto& Script = mAIScripts[(size_t)Owner];
    RunningAIScript TargetContext = Script;
    TargetContext.PlayerID = Owner;
    TargetContext.HasLocation = HasLocation;
    const TriggerLocation* Loc = HasLocation ? GetLocationById(LocationID) : nullptr;
    const Core::Math::Vec2 NewAnchor = HasLocation && Loc ? Loc->Center() : ResolveAIScriptTarget(EM, TargetContext);
    if (Script.Active && Script.ScriptID == ScriptID && Script.HasLocation == HasLocation)
    {
        if (Script.Anchor.DistanceTo(NewAnchor) <= kAIScriptRetargetPixels)
            return;

        Script.Anchor = NewAnchor;
        Script.TickAccumulator = 0.0f;
        IssueAIScriptOrders(EM, Script, Script.Anchor);
        return;
    }

    Script.Active = true;
    Script.PlayerID = Owner;
    Script.ScriptID = ScriptID;
    Script.HasLocation = HasLocation;
    Script.TickAccumulator = 0.0f;
    Script.WaveCount = 0;
    Script.Anchor = NewAnchor;

    const int Issued = IssueAIScriptOrders(EM, Script, Script.Anchor);
    Emit(HasLocation ? 16 : 15, Owner,
         "AI script " + std::to_string(ScriptID) + " active for player " + std::to_string(Owner) +
             " orders=" + std::to_string(Issued),
         Script.Anchor,
         ScriptID);
}

void TriggerVM::UpdateAIScripts(Core::ECS::EntityManager& EM, float DeltaTime)
{
    constexpr float kAIScriptOrderSeconds = 5.0f;
    for (auto& Script : mAIScripts)
    {
        if (!Script.Active)
            continue;
        Script.TickAccumulator += DeltaTime;
        if (Script.TickAccumulator < kAIScriptOrderSeconds)
            continue;
        Script.TickAccumulator = 0.0f;

        const Core::Math::Vec2 Target = ResolveAIScriptTarget(EM, Script);
        int Issued = IssueAIScriptOrders(EM, Script, Target);
        if (Issued > 0)
            ++Script.WaveCount;
    }
}

Core::Math::Vec2 TriggerVM::ResolveAIScriptTarget(Core::ECS::EntityManager& EM, const RunningAIScript& Script) const
{
    if (Script.HasLocation)
        return Script.Anchor;

    Core::Math::Vec2 Best = Script.Anchor;
    float BestDist = std::numeric_limits<float>::max();
    Core::Math::Vec2 Origin{};
    bool HasOrigin = false;

    auto MyUnits = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
    for (int ID : MyUnits)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (!Player || !Trans || !Unit || Unit->HitPoints <= 0)
            continue;
        if (Player->PlayerID != Script.PlayerID)
            continue;
        Origin = Trans->Position;
        HasOrigin = true;
        break;
    }

    auto Targets = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
    for (int ID : Targets)
    {
        auto* Player = EM.GetComponent<PlayerComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (!Player || !Trans || !Unit || Unit->HitPoints <= 0)
            continue;
        if (Player->PlayerID == Script.PlayerID || AreAllied(Script.PlayerID, Player->PlayerID))
            continue;

        const float Dist = HasOrigin ? Origin.DistanceTo(Trans->Position) : 0.0f;
        if (Dist < BestDist)
        {
            BestDist = Dist;
            Best = Trans->Position;
        }
    }

    return Best;
}

int TriggerVM::IssueAIScriptOrders(Core::ECS::EntityManager& EM, RunningAIScript& Script, const Core::Math::Vec2& Target)
{
    int Issued = 0;
    auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
    for (int ID : Units)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Player = EM.GetComponent<PlayerComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        if (!Unit || !Player || !Trans || Unit->HitPoints <= 0)
            continue;
        if (Player->PlayerID != Script.PlayerID)
            continue;
        if (!EM.GetComponent<CombatComponent>(ID))
            continue;

        auto* Path = EM.GetComponent<PathfindingComponent>(ID);
        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        const bool SameAttackMove =
            Cmd &&
            Cmd->CurrentCommand == CommandType::AttackMove &&
            Cmd->TargetEntityID < 0 &&
            Cmd->TargetPosition.DistanceTo(Target) <= kAIScriptRetargetPixels;
        const bool HasActivePath = Path && Path->PathIndex < (int)Path->Path.size();
        const float NodeTolerance = Path ? Path->NodeTolerance : 0.0f;
        const bool IsAtTarget = Trans->Position.DistanceTo(Target) <= std::max(NodeTolerance, kAIScriptRetargetPixels);
        if (SameAttackMove && (HasActivePath || IsAtTarget))
            continue;

        Order AttackOrder;
        AttackOrder.Type = OrderType::AttackMove;
        AttackOrder.TargetPosition = Target;
        AttackOrder.TargetEntityID = -1;
        Game::Systems::CommandSystem::IssueOrder(EM, ID, AttackOrder, false);
        if (auto* IssuedCmd = EM.GetComponent<CommandComponent>(ID);
            IssuedCmd && IssuedCmd->CurrentCommand == CommandType::AttackMove)
        {
            if (auto* IssuedPath = EM.GetComponent<PathfindingComponent>(ID))
                IssuedPath->NodeTolerance = std::max(IssuedPath->NodeTolerance, kAIScriptRetargetPixels);
            ++Issued;
        }
    }
    return Issued;
}

void TriggerVM::RecordUnsupportedCondition(uint8_t ConditionType) const
{
    int& Count = mUnsupportedConditionCounts[(size_t)ConditionType];
    if (Count == 0)
        Logger::Warning("TriggerVM", "Unsupported condition id " + std::to_string(ConditionType));
    ++Count;
}

void TriggerVM::RecordUnsupportedAction(uint8_t ActionType) const
{
    int& Count = mUnsupportedActionCounts[(size_t)ActionType];
    if (Count == 0)
        Logger::Warning("TriggerVM", "Unsupported action id " + std::to_string(ActionType));
    ++Count;
}

void TriggerVM::ApplyNumberModifier(int& Value, uint8_t Modifier, int Amount)
{
    if (Modifier == 7) Value = Amount;
    else if (Modifier == 8) Value += Amount;
    else if (Modifier == 9) Value = std::max(0, Value - Amount);
    else Value = Amount;
}

void TriggerVM::Emit(uint8_t ActionID, int PlayerID, const std::string& Text,
                     Core::Math::Vec2 Position, uint32_t Data,
                     std::string SoundPath, uint32_t DurationMs)
{
    mEvents.push_back({ ActionID, PlayerID, Text, std::move(SoundPath), Position, Data, DurationMs });
    if (!Text.empty())
        Logger::Info("TriggerVM", Text);
}

} // namespace Game::Trigger
