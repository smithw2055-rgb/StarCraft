#pragma once

#include "Engine/Scripting/ScenarioTriggerServer.h"
#include "Game/Locale/TextEncoding.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Game::Trigger
{

struct SCRLocation
{
    uint32_t ID = 0;
    uint32_t StringID = 0;
    int32_t Left = 0;
    int32_t Top = 0;
    int32_t Right = 0;
    int32_t Bottom = 0;
    uint16_t ElevationFlags = 0;
    std::string Name;
};

struct SCRUnitProperties
{
    uint16_t ValidSpecialFlags = 0;
    uint16_t ValidFieldFlags = 0;
    uint8_t Owner = 0;
    uint8_t HitPointPercent = 100;
    uint8_t ShieldPercent = 100;
    uint8_t EnergyPercent = 100;
    uint32_t ResourceAmount = 0;
    uint16_t HangarCount = 0;
    uint16_t StateFlags = 0;
    uint32_t Unused = 0;
    bool Used = false;
};

struct SCRCondition
{
    uint32_t LocationID = 0;
    uint32_t PlayerID = 0;
    uint32_t Amount = 0;
    uint16_t UnitID = 0;
    uint8_t ComparisonType = 0;
    uint8_t ConditionType = 0;
    uint8_t Extra = 0;
    uint8_t Flags = 0;
    uint16_t Internal = 0;

    bool Disabled() const { return (Flags & 0x02) != 0; }
};

struct SCRAction
{
    uint32_t LocationID = 0;
    uint32_t StringID = 0;
    uint32_t WavID = 0;
    uint32_t Time = 0;
    uint32_t PlayerID = 0;
    uint32_t Secondary = 0;
    uint16_t UnitID = 0;
    uint8_t ActionType = 0;
    uint8_t Amount = 0;
    uint8_t Flags = 0;
    std::array<uint8_t, 3> Internal{};

    bool Disabled() const { return (Flags & 0x02) != 0; }
};

struct SCRTrigger
{
    static constexpr size_t kConditionCount = 16;
    static constexpr size_t kActionCount = 64;
    static constexpr size_t kConditionSize = 20;
    static constexpr size_t kActionSize = 32;
    static constexpr size_t kTriggerSize = kConditionCount * kConditionSize +
        kActionCount * kActionSize + 4 + 28;

    std::array<SCRCondition, kConditionCount> Conditions{};
    std::array<SCRAction, kActionCount> Actions{};
    uint32_t ExecutionFlags = 0;
    std::array<uint8_t, 28> Owners{};

    bool EnabledForAnyPlayer() const
    {
        return std::any_of(Owners.begin(), Owners.end(), [](uint8_t Owner) { return Owner != 0; });
    }
};

struct SCRPlayerInfo
{
    uint8_t Owner = 7;
    uint8_t Race = 5;
    uint8_t Force = 0;
    uint8_t Color = 0;
};

struct SCRForceInfo
{
    uint16_t NameStringID = 0;
    uint8_t Flags = 0;
};

struct SCRTriggerData
{
    uint32_t NumPlayers = 0;
    uint16_t ScenarioNameStringID = 0;
    uint16_t ScenarioDescriptionStringID = 0;
    std::array<SCRPlayerInfo, 12> Players{};
    std::array<SCRForceInfo, 4> Forces{};
    std::vector<SCRLocation> Locations;
    std::vector<SCRTrigger> Triggers;
    std::vector<SCRTrigger> BriefingTriggers;
    std::vector<std::string> Strings;
    std::vector<uint32_t> WavStringIDs;
    std::array<SCRUnitProperties, 64> UnitProperties{};
    std::array<std::string, 256> SwitchNames{};
    std::vector<uint8_t> RawData;

    std::string GetString(uint32_t StringID) const
    {
        if (StringID < Strings.size())
            return Strings[StringID];
        return {};
    }

    std::string GetWavPath(uint32_t WavID) const
    {
        std::string Direct = GetString(WavID);
        if (!Direct.empty())
            return Direct;
        if (WavID < WavStringIDs.size())
            return GetString(WavStringIDs[(size_t)WavID]);
        return {};
    }
};

class SCRTriggerParser
{
public:
    bool Parse(const std::vector<uint8_t>& ChkData, SCRTriggerData& OutData)
    {
        if (ChkData.size() < 8) return false;

        OutData = {};
        OutData.RawData = ChkData;

        Section Str = FindSection(ChkData, "STR ");
        if (Str.Data && Str.Size >= 2)
            ParseStrings(*Str.Data, Str.Offset, Str.Size, OutData.Strings);

        ParsePlayers(ChkData, OutData);
        ParseScenarioProperties(ChkData, OutData);
        ParseLocations(ChkData, OutData);
        ParseWavs(ChkData, OutData);
        ParseUnitProperties(ChkData, OutData);
        ParseSwitchNames(ChkData, OutData);
        ParseTriggers(ChkData, OutData);
        ParseBriefingTriggers(ChkData, OutData);

        OutData.NumPlayers = 8;
        return true;
    }

    // Phase 65 CHK Trigger Compiler:
    // Compiles parsed StarCraft .chk SCRTrigger records into Engine::Scripting::ScenarioRule
    // entries on Engine::Scripting::ScenarioTriggerServer (supporting Always, ElapsedTime,
    // Switch, Accumulate/Deaths/Score thresholds, PreserveTrigger, SetSwitch, Wait, SetResources,
    // Victory/Defeat/Transmission/RunAIScript signals).
    static size_t CompileToScenarioTriggerServer(
        const SCRTriggerData& Data,
        Engine::Scripting::ScenarioTriggerServer& Server)
    {
        size_t CompiledCount = 0;
        for (size_t I = 0; I < Data.Triggers.size(); ++I)
        {
            const auto& Trig = Data.Triggers[I];
            Engine::Scripting::ScenarioRule Rule;
            Rule.RuleID = static_cast<uint32_t>(I + 1);
            Rule.Name = "CHK_Trigger_" + std::to_string(Rule.RuleID);
            Rule.PreserveTrigger = (Trig.ExecutionFlags & 0x04u) != 0;

            uint32_t Mask = 0;
            for (size_t P = 0; P < std::min<size_t>(28, Trig.Owners.size()); ++P)
            {
                if (Trig.Owners[P] != 0)
                    Mask |= (1u << P);
            }
            Rule.PlayerMask = Mask != 0 ? Mask : 0xFFFFFFFFu;

            bool NeverFires = false;
            for (const auto& Cond : Trig.Conditions)
            {
                if (Cond.ConditionType == 0 || Cond.Disabled())
                    continue;
                if (Cond.ConditionType == 23) // Never
                {
                    NeverFires = true;
                    break;
                }
                Engine::Scripting::TriggerCondition OutCond;
                switch (Cond.ConditionType)
                {
                case 22: // Always
                    OutCond.Kind = Engine::Scripting::TriggerConditionKind::Always;
                    break;
                case 12: // Elapsed time (seconds -> 24Hz ticks)
                    OutCond.Kind = Engine::Scripting::TriggerConditionKind::ElapsedTicksAtLeast;
                    OutCond.TargetInt = static_cast<int64_t>(Cond.Amount) * 24;
                    break;
                case 11: // Switch
                    OutCond.Kind = Engine::Scripting::TriggerConditionKind::BlackboardBoolEquals;
                    OutCond.Key = "Switch_" + std::to_string(Cond.Extra);
                    OutCond.TargetBool = (Cond.ComparisonType == 2); // 2 = Set, 3 = Cleared
                    break;
                case 4:  // Accumulate
                case 15: // Deaths
                case 21: // Score
                default:
                    OutCond.Kind = Engine::Scripting::TriggerConditionKind::BlackboardIntAtLeast;
                    OutCond.Key = std::string(GetConditionName(Cond.ConditionType)) + "_P" +
                                  std::to_string(Cond.PlayerID);
                    OutCond.TargetInt = static_cast<int64_t>(Cond.Amount);
                    break;
                }
                Rule.Conditions.push_back(std::move(OutCond));
            }
            if (NeverFires)
                continue;

            for (const auto& Act : Trig.Actions)
            {
                if (Act.ActionType == 0 || Act.Disabled())
                    continue;
                if (Act.ActionType == 3) // Preserve trigger
                {
                    Rule.PreserveTrigger = true;
                    continue;
                }
                Engine::Scripting::TriggerAction OutAct;
                switch (Act.ActionType)
                {
                case 4: // Wait (milliseconds -> 24Hz ticks)
                    OutAct.Kind = Engine::Scripting::TriggerActionKind::WaitTicks;
                    OutAct.WaitTicks = std::max<uint32_t>(1u, (Act.Time * 24u) / 1000u);
                    break;
                case 13: // Set switch
                    OutAct.Kind = Engine::Scripting::TriggerActionKind::SetBlackboardBool;
                    OutAct.Key = "Switch_" + std::to_string(Act.Secondary);
                    OutAct.BoolValue = (Act.Amount == 4); // 4 = Set, 5 = Clear
                    break;
                case 26: // Set resources
                    OutAct.Kind = (Act.Amount == 8)
                                      ? Engine::Scripting::TriggerActionKind::AddBlackboardInt
                                      : Engine::Scripting::TriggerActionKind::SetBlackboardInt;
                    OutAct.Key = "Accumulate_P" + std::to_string(Act.PlayerID);
                    OutAct.IntValue = static_cast<int64_t>(Act.Secondary);
                    break;
                default:
                    OutAct.Kind = Engine::Scripting::TriggerActionKind::EmitSignal;
                    OutAct.Key = GetActionName(Act.ActionType);
                    OutAct.IntValue = static_cast<int64_t>(Act.UnitID != 0 ? Act.UnitID : Act.StringID);
                    break;
                }
                Rule.Actions.push_back(std::move(OutAct));
            }

            Server.RegisterRule(std::move(Rule));
            ++CompiledCount;
        }
        return CompiledCount;
    }

    static const char* GetConditionName(uint16_t ConditionID)
    {
        switch (ConditionID)
        {
            case 0: return "No condition";
            case 1: return "Countdown timer";
            case 2: return "Command";
            case 3: return "Bring";
            case 4: return "Accumulate";
            case 5: return "Kill";
            case 6: return "Command the most";
            case 7: return "Commands the most at";
            case 8: return "Most kills";
            case 9: return "Highest score";
            case 10: return "Most resources";
            case 11: return "Switch";
            case 12: return "Elapsed time";
            case 13: return "Mission briefing";
            case 14: return "Opponents";
            case 15: return "Deaths";
            case 16: return "Command the least";
            case 17: return "Command the least at";
            case 18: return "Least kills";
            case 19: return "Lowest score";
            case 20: return "Least resources";
            case 21: return "Score";
            case 22: return "Always";
            case 23: return "Never";
            default: return "Unknown";
        }
    }

    static const char* GetBriefingActionName(uint8_t ActionType)
    {
        switch (ActionType)
        {
            case 0: return "No briefing action";
            case 1: return "Wait";
            case 2: return "Play WAV";
            case 3: return "Text message";
            case 4: return "Mission objectives";
            case 5: return "Show portrait";
            case 6: return "Hide portrait";
            case 7: return "Display speaking portrait";
            case 8: return "Transmission";
            case 9: return "Skip tutorial";
            default: return "Unknown";
        }
    }

    static const char* GetActionName(uint16_t ActionID)
    {
        switch (ActionID)
        {
            case 0: return "No action";
            case 1: return "Victory";
            case 2: return "Defeat";
            case 3: return "Preserve trigger";
            case 4: return "Wait";
            case 5: return "Pause game";
            case 6: return "Unpause game";
            case 7: return "Transmission";
            case 8: return "Play WAV";
            case 9: return "Display text";
            case 10: return "Center view";
            case 11: return "Create unit with properties";
            case 12: return "Set mission objectives";
            case 13: return "Set switch";
            case 14: return "Set countdown timer";
            case 15: return "Run AI script";
            case 16: return "Run AI script at location";
            case 17: return "Leaderboard control";
            case 18: return "Leaderboard control at location";
            case 19: return "Leaderboard resources";
            case 20: return "Leaderboard kills";
            case 21: return "Leaderboard points";
            case 22: return "Kill unit";
            case 23: return "Kill unit at location";
            case 24: return "Remove unit";
            case 25: return "Remove unit at location";
            case 26: return "Set resources";
            case 27: return "Set score";
            case 28: return "Minimap ping";
            case 29: return "Talking portrait";
            case 30: return "Mute unit speech";
            case 31: return "Unmute unit speech";
            case 32: return "Leaderboard computer players";
            case 33: return "Leaderboard goal control";
            case 34: return "Leaderboard goal control at location";
            case 35: return "Leaderboard goal resources";
            case 36: return "Leaderboard goal kills";
            case 37: return "Leaderboard goal points";
            case 38: return "Move location";
            case 39: return "Move unit";
            case 40: return "Leaderboard greed";
            case 41: return "Set next scenario";
            case 42: return "Set doodad state";
            case 43: return "Set invincibility";
            case 44: return "Create unit";
            case 45: return "Set deaths";
            case 46: return "Order";
            case 47: return "Comment";
            case 48: return "Give units to player";
            case 49: return "Modify unit hit points";
            case 50: return "Modify unit energy";
            case 51: return "Modify unit shield points";
            case 52: return "Modify unit resource amount";
            case 53: return "Modify unit hangar count";
            case 54: return "Pause timer";
            case 55: return "Unpause timer";
            case 56: return "Draw";
            case 57: return "Set alliance status";
            case 58: return "Disable debug mode";
            case 59: return "Enable debug mode";
            default: return "Unknown";
        }
    }

private:
    struct Section
    {
        const std::vector<uint8_t>* Data = nullptr;
        size_t Offset = 0;
        size_t Size = 0;
    };

    static Section FindSection(const std::vector<uint8_t>& Data, const char Name[5])
    {
        size_t Pos = 0;
        while (Pos + 8 <= Data.size())
        {
            const uint32_t Size = ReadU32(Data, Pos + 4);
            if (std::memcmp(&Data[Pos], Name, 4) == 0)
            {
                const size_t Available = Data.size() - (Pos + 8);
                return Section{&Data, Pos + 8, std::min<size_t>(Size, Available)};
            }
            if (Size > Data.size() - (Pos + 8))
                break;
            Pos += 8 + Size;
        }
        return {};
    }

    static void ParseStrings(const std::vector<uint8_t>& Data,
                              size_t Offset,
                              size_t Size,
                              std::vector<std::string>& OutStrings)
    {
        const uint16_t NumStrings = ReadU16(Data, Offset);
        OutStrings.resize((size_t)NumStrings + 1);
        for (uint16_t I = 0; I < NumStrings; ++I)
        {
            const size_t TablePos = Offset + 2 + (size_t)I * 2;
            if (TablePos + 2 > Offset + Size)
                break;
            const uint16_t RelOffset = ReadU16(Data, TablePos);
            if (RelOffset >= Size)
                continue;
            const size_t StrStart = Offset + RelOffset;
            size_t StrEnd = StrStart;
            while (StrEnd < Offset + Size && Data[StrEnd] != 0)
                ++StrEnd;
            std::string Raw((const char*)&Data[StrStart], StrEnd - StrStart);
            OutStrings[(size_t)I + 1] = Game::Locale::LegacyGameTextToUtf8(Raw);
        }
    }

    static void ParsePlayers(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Ownr = FindSection(Data, "OWNR");
        if (Ownr.Data)
        {
            for (size_t I = 0; I < std::min<size_t>(12, Ownr.Size); ++I)
                OutData.Players[I].Owner = ReadU8(Data, Ownr.Offset + I);
        }

        Section Side = FindSection(Data, "SIDE");
        if (Side.Data)
        {
            for (size_t I = 0; I < std::min<size_t>(12, Side.Size); ++I)
                OutData.Players[I].Race = ReadU8(Data, Side.Offset + I);
        }

        Section Forc = FindSection(Data, "FORC");
        if (Forc.Data)
        {
            for (size_t I = 0; I < std::min<size_t>(8, Forc.Size); ++I)
                OutData.Players[I].Force = ReadU8(Data, Forc.Offset + I);
            for (size_t I = 0; I < 4 && 8 + I * 2 + 2 <= Forc.Size; ++I)
                OutData.Forces[I].NameStringID = ReadU16(Data, Forc.Offset + 8 + I * 2);
            for (size_t I = 0; I < 4 && 16 + I < Forc.Size; ++I)
                OutData.Forces[I].Flags = ReadU8(Data, Forc.Offset + 16 + I);
        }

        Section Colr = FindSection(Data, "COLR");
        if (Colr.Data)
        {
            for (size_t I = 0; I < std::min<size_t>(8, Colr.Size); ++I)
                OutData.Players[I].Color = ReadU8(Data, Colr.Offset + I);
        }
    }

    static void ParseScenarioProperties(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Sprp = FindSection(Data, "SPRP");
        if (Sprp.Data && Sprp.Size >= 4)
        {
            OutData.ScenarioNameStringID = ReadU16(Data, Sprp.Offset + 0);
            OutData.ScenarioDescriptionStringID = ReadU16(Data, Sprp.Offset + 2);
        }
    }

    static void ParseLocations(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Mrgn = FindSection(Data, "MRGN");
        if (!Mrgn.Data || Mrgn.Size < 20)
            return;

        const size_t Count = Mrgn.Size / 20;
        OutData.Locations.resize(Count);
        for (size_t I = 0; I < Count; ++I)
        {
            const size_t Pos = Mrgn.Offset + I * 20;
            auto& Loc = OutData.Locations[I];
            Loc.ID = (uint32_t)(I + 1);
            Loc.Left = ReadI32(Data, Pos + 0);
            Loc.Top = ReadI32(Data, Pos + 4);
            Loc.Right = ReadI32(Data, Pos + 8);
            Loc.Bottom = ReadI32(Data, Pos + 12);
            Loc.StringID = ReadU16(Data, Pos + 16);
            Loc.ElevationFlags = ReadU16(Data, Pos + 18);
            Loc.Name = OutData.GetString(Loc.StringID);
        }
    }

    static void ParseWavs(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Wav = FindSection(Data, "WAV ");
        if (!Wav.Data || Wav.Size < 4)
            return;

        const size_t Count = Wav.Size / 4;
        OutData.WavStringIDs.resize(Count);
        for (size_t I = 0; I < Count; ++I)
            OutData.WavStringIDs[I] = ReadU32(Data, Wav.Offset + I * 4);
    }

    static void ParseUnitProperties(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Upus = FindSection(Data, "UPUS");
        if (Upus.Data)
        {
            for (size_t I = 0; I < std::min<size_t>(64, Upus.Size); ++I)
                OutData.UnitProperties[I].Used = ReadU8(Data, Upus.Offset + I) != 0;
        }

        Section Uprp = FindSection(Data, "UPRP");
        if (!Uprp.Data || Uprp.Size < 20)
            return;

        const size_t Count = std::min<size_t>(64, Uprp.Size / 20);
        for (size_t I = 0; I < Count; ++I)
        {
            const size_t Pos = Uprp.Offset + I * 20;
            auto& Prop = OutData.UnitProperties[I];
            Prop.ValidSpecialFlags = ReadU16(Data, Pos + 0);
            Prop.ValidFieldFlags = ReadU16(Data, Pos + 2);
            Prop.Owner = ReadU8(Data, Pos + 4);
            Prop.HitPointPercent = ReadU8(Data, Pos + 5);
            Prop.ShieldPercent = ReadU8(Data, Pos + 6);
            Prop.EnergyPercent = ReadU8(Data, Pos + 7);
            Prop.ResourceAmount = ReadU32(Data, Pos + 8);
            Prop.HangarCount = ReadU16(Data, Pos + 12);
            Prop.StateFlags = ReadU16(Data, Pos + 14);
            Prop.Unused = ReadU32(Data, Pos + 16);
        }
    }

    static void ParseSwitchNames(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Swnm = FindSection(Data, "SWNM");
        if (!Swnm.Data || Swnm.Size < 4)
            return;

        const size_t Count = std::min<size_t>(256, Swnm.Size / 4);
        for (size_t I = 0; I < Count; ++I)
        {
            const uint32_t StringID = ReadU32(Data, Swnm.Offset + I * 4);
            OutData.SwitchNames[I] = OutData.GetString(StringID);
        }
    }

    static void ParseTriggers(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Trig = FindSection(Data, "TRIG");
        if (!Trig.Data || Trig.Size < SCRTrigger::kTriggerSize)
            return;

        const size_t TriggerCount = Trig.Size / SCRTrigger::kTriggerSize;
        OutData.Triggers.reserve(TriggerCount);
        for (size_t I = 0; I < TriggerCount; ++I)
        {
            SCRTrigger Trigger;
            size_t Pos = Trig.Offset + I * SCRTrigger::kTriggerSize;

            for (auto& Cond : Trigger.Conditions)
            {
                Cond.LocationID = ReadU32(Data, Pos + 0);
                Cond.PlayerID = ReadU32(Data, Pos + 4);
                Cond.Amount = ReadU32(Data, Pos + 8);
                Cond.UnitID = ReadU16(Data, Pos + 12);
                Cond.ComparisonType = ReadU8(Data, Pos + 14);
                Cond.ConditionType = ReadU8(Data, Pos + 15);
                Cond.Extra = ReadU8(Data, Pos + 16);
                Cond.Flags = ReadU8(Data, Pos + 17);
                Cond.Internal = ReadU16(Data, Pos + 18);
                Pos += SCRTrigger::kConditionSize;
            }

            for (auto& Action : Trigger.Actions)
            {
                Action.LocationID = ReadU32(Data, Pos + 0);
                Action.StringID = ReadU32(Data, Pos + 4);
                Action.WavID = ReadU32(Data, Pos + 8);
                Action.Time = ReadU32(Data, Pos + 12);
                Action.PlayerID = ReadU32(Data, Pos + 16);
                Action.Secondary = ReadU32(Data, Pos + 20);
                Action.UnitID = ReadU16(Data, Pos + 24);
                Action.ActionType = ReadU8(Data, Pos + 26);
                Action.Amount = ReadU8(Data, Pos + 27);
                Action.Flags = ReadU8(Data, Pos + 28);
                Action.Internal[0] = ReadU8(Data, Pos + 29);
                Action.Internal[1] = ReadU8(Data, Pos + 30);
                Action.Internal[2] = ReadU8(Data, Pos + 31);
                Pos += SCRTrigger::kActionSize;
            }

            Trigger.ExecutionFlags = ReadU32(Data, Pos);
            Pos += 4;
            for (auto& Owner : Trigger.Owners)
                Owner = ReadU8(Data, Pos++);

            if (Trigger.EnabledForAnyPlayer())
                OutData.Triggers.push_back(Trigger);
        }
    }

    static void ParseBriefingTriggers(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Mbrf = FindSection(Data, "MBRF");
        if (!Mbrf.Data || Mbrf.Size < SCRTrigger::kTriggerSize)
            return;

        const size_t TriggerCount = Mbrf.Size / SCRTrigger::kTriggerSize;
        OutData.BriefingTriggers.reserve(TriggerCount);
        for (size_t I = 0; I < TriggerCount; ++I)
        {
            SCRTrigger Trigger;
            size_t Pos = Mbrf.Offset + I * SCRTrigger::kTriggerSize;

            for (auto& Cond : Trigger.Conditions)
            {
                Cond.LocationID = ReadU32(Data, Pos + 0);
                Cond.PlayerID = ReadU32(Data, Pos + 4);
                Cond.Amount = ReadU32(Data, Pos + 8);
                Cond.UnitID = ReadU16(Data, Pos + 12);
                Cond.ComparisonType = ReadU8(Data, Pos + 14);
                Cond.ConditionType = ReadU8(Data, Pos + 15);
                Cond.Extra = ReadU8(Data, Pos + 16);
                Cond.Flags = ReadU8(Data, Pos + 17);
                Cond.Internal = ReadU16(Data, Pos + 18);
                Pos += SCRTrigger::kConditionSize;
            }

            for (auto& Action : Trigger.Actions)
            {
                Action.LocationID = ReadU32(Data, Pos + 0);
                Action.StringID = ReadU32(Data, Pos + 4);
                Action.WavID = ReadU32(Data, Pos + 8);
                Action.Time = ReadU32(Data, Pos + 12);
                Action.PlayerID = ReadU32(Data, Pos + 16);
                Action.Secondary = ReadU32(Data, Pos + 20);
                Action.UnitID = ReadU16(Data, Pos + 24);
                Action.ActionType = ReadU8(Data, Pos + 26);
                Action.Amount = ReadU8(Data, Pos + 27);
                Action.Flags = ReadU8(Data, Pos + 28);
                Action.Internal[0] = ReadU8(Data, Pos + 29);
                Action.Internal[1] = ReadU8(Data, Pos + 30);
                Action.Internal[2] = ReadU8(Data, Pos + 31);
                Pos += SCRTrigger::kActionSize;
            }

            Trigger.ExecutionFlags = ReadU32(Data, Pos);
            Pos += 4;
            for (auto& Owner : Trigger.Owners)
                Owner = ReadU8(Data, Pos++);

            OutData.BriefingTriggers.push_back(Trigger);
        }
    }

    static uint8_t ReadU8(const std::vector<uint8_t>& Data, size_t Offset)
    {
        if (Offset >= Data.size()) return 0;
        return Data[Offset];
    }

    static uint16_t ReadU16(const std::vector<uint8_t>& Data, size_t Offset)
    {
        if (Offset + 2 > Data.size()) return 0;
        return (uint16_t)(Data[Offset] | (Data[Offset + 1] << 8));
    }

    static uint32_t ReadU32(const std::vector<uint8_t>& Data, size_t Offset)
    {
        if (Offset + 4 > Data.size()) return 0;
        return (uint32_t)Data[Offset] |
               ((uint32_t)Data[Offset + 1] << 8) |
               ((uint32_t)Data[Offset + 2] << 16) |
               ((uint32_t)Data[Offset + 3] << 24);
    }

    static int32_t ReadI32(const std::vector<uint8_t>& Data, size_t Offset)
    {
        return (int32_t)ReadU32(Data, Offset);
    }
};

} // namespace Game::Trigger
