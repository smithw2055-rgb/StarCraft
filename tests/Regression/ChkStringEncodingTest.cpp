#include "Game/Map/ChkParser.h"
#include "Game/Trigger/SCRTriggerParser.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    void U8(std::vector<uint8_t>& Bytes, uint8_t Value) { Bytes.push_back(Value); }

    void U16(std::vector<uint8_t>& Bytes, uint16_t Value)
    {
        U8(Bytes, (uint8_t)Value);
        U8(Bytes, (uint8_t)(Value >> 8));
    }

    void U32(std::vector<uint8_t>& Bytes, uint32_t Value)
    {
        U16(Bytes, (uint16_t)Value);
        U16(Bytes, (uint16_t)(Value >> 16));
    }

    void Section(std::vector<uint8_t>& Bytes, const char Name[5], const std::vector<uint8_t>& Payload)
    {
        Bytes.insert(Bytes.end(), Name, Name + 4);
        U32(Bytes, (uint32_t)Payload.size());
        Bytes.insert(Bytes.end(), Payload.begin(), Payload.end());
    }
}

int main()
{
    std::vector<uint8_t> Payload;
    U16(Payload, 1);
    U16(Payload, 4);
    const uint8_t LegacyBytes[] = { 0xC0, 0xD7, 0xC5, 0xB5, 0x00 };
    Payload.insert(Payload.end(), LegacyBytes, LegacyBytes + sizeof(LegacyBytes));

    std::vector<uint8_t> Chk;
    Section(Chk, "STR ", Payload);

    const std::string ExpectedUtf8 = "\xE9\x9B\xB7\xE8\xAF\xBA";

    Game::Trigger::SCRTriggerData TriggerData;
    Game::Trigger::SCRTriggerParser TriggerParser;
    Require(TriggerParser.Parse(Chk, TriggerData), "trigger parser reads CHK STR section");
    Require(TriggerData.Strings.size() > 1 && TriggerData.Strings[1] == ExpectedUtf8,
        "trigger parser decodes legacy CHK strings to UTF-8");

    Game::Map::ChkParser MapParser;
    Require(MapParser.Parse(Chk.data(), Chk.size()), "map parser reads CHK STR section");
    const auto& Strings = MapParser.GetStringTable().Strings;
    Require(!Strings.empty() && Strings[0] == ExpectedUtf8,
        "map parser decodes legacy CHK strings to UTF-8");

    std::printf("ChkStringEncodingTest passed\n");
    return 0;
}
