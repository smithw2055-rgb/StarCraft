#include "Resource/Vf4Decoder.h"
#include "Core/Utils/Logger.h"

namespace Resource
{

Vf4Data Vf4Decoder::Decode(const std::vector<uint8_t>& Data)
{
    Vf4Data Result;

    if (Data.empty() || Data.size() % EntryBytes != 0)
    {
        Core::Utils::Logger::Warning("Vf4Decoder", "VF4 data size invalid");
        return Result;
    }

    size_t Count = Data.size() / EntryBytes;
    Result.Entries.reserve(Count);

    for (size_t I = 0; I < Count; I++)
    {
        size_t Off = I * EntryBytes;
        Vf4Entry Entry;
        for (int J = 0; J < 16; J++)
        {
            size_t ByteOff = Off + J * 2;
            if (ByteOff + 1 >= Data.size()) break;
            Entry.Flags[J] = Data[ByteOff] | (Data[ByteOff + 1] << 8);
        }
        Result.Entries.push_back(Entry);
    }

    Core::Utils::Logger::Info("Vf4Decoder",
        "Decoded " + std::to_string(Count) + " VF4 entries");
    return Result;
}

} // namespace Resource
