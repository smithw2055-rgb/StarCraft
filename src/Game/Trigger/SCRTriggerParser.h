#pragma once

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
            const size_t Payload = Pos + 8;
            if (Payload + Size > Data.size())
                break;
            if (std::memcmp(Data.data() + Pos, Name, 4) == 0)
                return { &Data, Payload, Size };
            Pos = Payload + Size;
        }
        return {};
    }

    static void ParseStrings(const std::vector<uint8_t>& Data,
                             size_t Offset,
                             size_t Size,
                             std::vector<std::string>& OutStrings)
    {
        if (Size < 2) return;

        const uint16_t Count = ReadU16(Data, Offset);
        OutStrings.assign((size_t)Count + 1u, {});
        for (uint16_t I = 1; I <= Count; ++I)
        {
            const size_t OffsetPos = Offset + 2u + (size_t)(I - 1u) * 2u;
            if (OffsetPos + 2u > Offset + Size)
                break;

            const uint16_t StringOffset = ReadU16(Data, OffsetPos);
            const size_t StringPos = Offset + StringOffset;
            if (StringOffset == 0 || StringPos >= Offset + Size)
                continue;

            size_t End = StringPos;
            while (End < Offset + Size && Data[End] != 0)
                ++End;
            OutStrings[I] = Game::Locale::LegacyGameTextToUtf8(std::string(
                reinterpret_cast<const char*>(Data.data() + StringPos),
                End - StringPos));
        }
    }

    static void ParsePlayers(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Ownr = FindSection(Data, "OWNR");
        if (Ownr.Data)
            for (size_t I = 0; I < 12 && I < Ownr.Size; ++I)
                OutData.Players[I].Owner = ReadU8(Data, Ownr.Offset + I);

        Section Side = FindSection(Data, "SIDE");
        if (Side.Data)
            for (size_t I = 0; I < 12 && I < Side.Size; ++I)
                OutData.Players[I].Race = ReadU8(Data, Side.Offset + I);

        Section Colr = FindSection(Data, "COLR");
        if (Colr.Data)
            for (size_t I = 0; I < 8 && I < Colr.Size; ++I)
                OutData.Players[I].Color = ReadU8(Data, Colr.Offset + I);

        Section Forc = FindSection(Data, "FORC");
        if (Forc.Data)
        {
            for (size_t I = 0; I < 8 && I < Forc.Size; ++I)
                OutData.Players[I].Force = ReadU8(Data, Forc.Offset + I);
            for (size_t F = 0; F < 4 && 8 + F * 2 + 2 <= Forc.Size; ++F)
                OutData.Forces[F].NameStringID = ReadU16(Data, Forc.Offset + 8 + F * 2);
            for (size_t F = 0; F < 4 && 16 + F < Forc.Size; ++F)
                OutData.Forces[F].Flags = ReadU8(Data, Forc.Offset + 16 + F);
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
        if (!Mrgn.Data) return;

        const size_t Count = std::min<size_t>(255, Mrgn.Size / 20u);
        OutData.Locations.reserve(Count);
        for (size_t I = 0; I < Count; ++I)
        {
            const size_t Pos = Mrgn.Offset + I * 20u;
            SCRLocation Loc;
            Loc.ID = (uint32_t)I + 1u;
            Loc.Left = ReadI32(Data, Pos + 0);
            Loc.Top = ReadI32(Data, Pos + 4);
            Loc.Right = ReadI32(Data, Pos + 8);
            Loc.Bottom = ReadI32(Data, Pos + 12);
            Loc.StringID = ReadU16(Data, Pos + 16);
            Loc.ElevationFlags = ReadU16(Data, Pos + 18);
            Loc.Name = OutData.GetString(Loc.StringID);
            OutData.Locations.push_back(std::move(Loc));
        }
    }

    static void ParseWavs(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Wav = FindSection(Data, "WAV ");
        if (!Wav.Data) return;

        const size_t Count = Wav.Size / 4u;
        OutData.WavStringIDs.resize(Count);
        for (size_t I = 0; I < Count; ++I)
            OutData.WavStringIDs[I] = ReadU32(Data, Wav.Offset + I * 4u);
    }

    static void ParseUnitProperties(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Upus = FindSection(Data, "UPUS");
        if (Upus.Data)
            for (size_t I = 0; I < OutData.UnitProperties.size() && I < Upus.Size; ++I)
                OutData.UnitProperties[I].Used = ReadU8(Data, Upus.Offset + I) != 0;

        Section Uprp = FindSection(Data, "UPRP");
        if (!Uprp.Data) return;

        const size_t Count = std::min<size_t>(OutData.UnitProperties.size(), Uprp.Size / 20u);
        for (size_t I = 0; I < Count; ++I)
        {
            const size_t Pos = Uprp.Offset + I * 20u;
            auto& Props = OutData.UnitProperties[I];
            Props.ValidSpecialFlags = ReadU16(Data, Pos + 0);
            Props.ValidFieldFlags = ReadU16(Data, Pos + 2);
            Props.Owner = ReadU8(Data, Pos + 4);
            Props.HitPointPercent = ReadU8(Data, Pos + 5);
            Props.ShieldPercent = ReadU8(Data, Pos + 6);
            Props.EnergyPercent = ReadU8(Data, Pos + 7);
            Props.ResourceAmount = ReadU32(Data, Pos + 8);
            Props.HangarCount = ReadU16(Data, Pos + 12);
            Props.StateFlags = ReadU16(Data, Pos + 14);
            Props.Unused = ReadU32(Data, Pos + 16);
        }
    }

    static void ParseSwitchNames(const std::vector<uint8_t>& Data, SCRTriggerData& OutData)
    {
        Section Swnm = FindSection(Data, "SWNM");
        if (!Swnm.Data) return;

        const size_t Count = std::min<size_t>(OutData.SwitchNames.size(), Swnm.Size / 4u);
        for (size_t I = 0; I < Count; ++I)
            OutData.SwitchNames[I] = OutData.GetString(ReadU32(Data, Swnm.Offset + I * 4u));
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
