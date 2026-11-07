#include "Main/GameLoop/GameSaveState.h"

#include "Engine/ECS/WorldSerializer.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace Game::Client::GameLoop
{

namespace
{
    void WriteHexLine(FILE* File, const char* Prefix, const void* Data, size_t ByteLen)
    {
        std::fprintf(File, "%s %zu ", Prefix, ByteLen);
        const auto* Bytes = static_cast<const uint8_t*>(Data);
        for (size_t i = 0; i < ByteLen; ++i)
            std::fprintf(File, "%02X", Bytes[i]);
        std::fprintf(File, "\n");
    }

    bool DecodeHexBytes(const char* HexStr, void* OutData, size_t ExpectedBytes)
    {
        auto HexNibble = [](char C) -> int {
            if (C >= '0' && C <= '9') return C - '0';
            if (C >= 'A' && C <= 'F') return 10 + (C - 'A');
            if (C >= 'a' && C <= 'f') return 10 + (C - 'a');
            return -1;
        };
        auto* OutBytes = static_cast<uint8_t*>(OutData);
        for (size_t i = 0; i < ExpectedBytes; ++i)
        {
            const int Hi = HexNibble(HexStr[i * 2]);
            const int Lo = HexNibble(HexStr[i * 2 + 1]);
            if (Hi < 0 || Lo < 0)
                return false;
            OutBytes[i] = static_cast<uint8_t>((Hi << 4) | Lo);
        }
        return true;
    }
} // namespace

bool WriteGameSave(FILE* File, const GameSaveSnapshot& Snapshot)
{
    if (!File)
        return false;

    std::fprintf(File, "SCVS 1\n");
    std::fprintf(File, "map %s\n", Snapshot.MapPath.c_str());
    std::fprintf(File, "paused %d\n", Snapshot.Paused ? 1 : 0);
    std::fprintf(File, "minerals %d\n", Snapshot.Resources.MineralCount);
    std::fprintf(File, "gas %d\n", Snapshot.Resources.GasCount);
    std::fprintf(File, "supply_used %d\n", Snapshot.Resources.SupplyUsed);
    std::fprintf(File, "supply_max %d\n", Snapshot.Resources.SupplyMax);

    if (Snapshot.HasWorldSnapshot)
    {
        std::fprintf(File, "has_world_snapshot 1\n");
        std::fprintf(File, "sim_tick %llu\n", static_cast<unsigned long long>(Snapshot.SimulationTick));
        std::fprintf(File, "world_frame %u\n", Snapshot.WorldSnapshot.FrameNumber);
        std::fprintf(File, "world_hash %u\n", Snapshot.WorldSnapshot.StateHashFNV1a);
        if (Snapshot.WorldSnapshot.HasUpgradesResource)
        {
            WriteHexLine(File, "world_upgrades",
                         &Snapshot.WorldSnapshot.Upgrades,
                         sizeof(Snapshot.WorldSnapshot.Upgrades));
        }
        if (Snapshot.WorldSnapshot.HasEconomyResource)
        {
            WriteHexLine(File, "world_economy",
                         &Snapshot.WorldSnapshot.Economy,
                         sizeof(Snapshot.WorldSnapshot.Economy));
        }

        std::vector<uint8_t> WorldBlob;
        if (Engine::ECS::WorldSerializer::SerializeToBytes(Snapshot.WorldSnapshot.GenericSnapshot, WorldBlob))
        {
            WriteHexLine(File, "world_generic_blob", WorldBlob.data(), WorldBlob.size());
        }
    }
    return true;
}

bool ReadGameSave(FILE* File, GameSaveSnapshot& Snapshot)
{
    if (!File)
        return false;

    std::vector<char> LineBuf(524288, 0);
    if (!std::fgets(LineBuf.data(), static_cast<int>(LineBuf.size()), File) ||
        std::strncmp(LineBuf.data(), "SCVS 1", 6) != 0)
        return false;

    int Paused = Snapshot.Paused ? 1 : 0;
    int Minerals = Snapshot.Resources.MineralCount;
    int Gas = Snapshot.Resources.GasCount;
    int SupplyUsed = Snapshot.Resources.SupplyUsed;
    int SupplyMax = Snapshot.Resources.SupplyMax;
    int HasWorldSnap = 0;
    unsigned long long SimTick = 0;
    unsigned int WorldFrame = 0;
    unsigned int WorldHash = 0;

    Snapshot.HasWorldSnapshot = false;
    Snapshot.WorldSnapshot = {};
    std::vector<uint8_t> WorldBlob;

    while (std::fgets(LineBuf.data(), static_cast<int>(LineBuf.size()), File))
    {
        const char* Line = LineBuf.data();
        if (std::strncmp(Line, "map ", 4) == 0)
        {
            std::string Path(Line + 4);
            while (!Path.empty() && (Path.back() == '\n' || Path.back() == '\r'))
                Path.pop_back();
            Snapshot.MapPath = std::move(Path);
            continue;
        }
        if (std::sscanf(Line, "paused %d", &Paused) == 1) continue;
        if (std::sscanf(Line, "minerals %d", &Minerals) == 1) continue;
        if (std::sscanf(Line, "gas %d", &Gas) == 1) continue;
        if (std::sscanf(Line, "supply_used %d", &SupplyUsed) == 1) continue;
        if (std::sscanf(Line, "supply_max %d", &SupplyMax) == 1) continue;
        if (std::sscanf(Line, "has_world_snapshot %d", &HasWorldSnap) == 1)
        {
            Snapshot.HasWorldSnapshot = (HasWorldSnap != 0);
            continue;
        }
        if (std::sscanf(Line, "sim_tick %llu", &SimTick) == 1)
        {
            Snapshot.SimulationTick = static_cast<uint64_t>(SimTick);
            continue;
        }
        if (std::sscanf(Line, "world_frame %u", &WorldFrame) == 1)
        {
            Snapshot.WorldSnapshot.FrameNumber = WorldFrame;
            continue;
        }
        if (std::sscanf(Line, "world_hash %u", &WorldHash) == 1)
        {
            Snapshot.WorldSnapshot.StateHashFNV1a = WorldHash;
            continue;
        }
        size_t ByteLen = 0;
        int Offset = 0;
        if (std::sscanf(Line, "world_upgrades %zu %n", &ByteLen, &Offset) == 1 &&
            ByteLen == sizeof(Snapshot.WorldSnapshot.Upgrades))
        {
            if (DecodeHexBytes(Line + Offset, &Snapshot.WorldSnapshot.Upgrades, ByteLen))
                Snapshot.WorldSnapshot.HasUpgradesResource = true;
            continue;
        }
        if (std::sscanf(Line, "world_economy %zu %n", &ByteLen, &Offset) == 1 &&
            ByteLen == sizeof(Snapshot.WorldSnapshot.Economy))
        {
            if (DecodeHexBytes(Line + Offset, &Snapshot.WorldSnapshot.Economy, ByteLen))
                Snapshot.WorldSnapshot.HasEconomyResource = true;
            continue;
        }
        if (std::sscanf(Line, "world_generic_blob %zu %n", &ByteLen, &Offset) == 1 && ByteLen > 0)
        {
            WorldBlob.resize(ByteLen);
            if (DecodeHexBytes(Line + Offset, WorldBlob.data(), ByteLen) &&
                Engine::ECS::WorldSerializer::DeserializeFromBytes(WorldBlob, Snapshot.WorldSnapshot.GenericSnapshot))
            {
                Snapshot.WorldSnapshot.Entities.clear();
                for (const auto& Ent : Snapshot.WorldSnapshot.GenericSnapshot.GetEntities())
                {
                    Snapshot.WorldSnapshot.Entities.push_back({Ent.Id, Ent.Generation});
                }
            }
            continue;
        }
    }

    Snapshot.Paused = Paused != 0;
    Snapshot.Resources.MineralCount = Minerals;
    Snapshot.Resources.GasCount = Gas;
    Snapshot.Resources.SupplyUsed = SupplyUsed;
    Snapshot.Resources.SupplyMax = SupplyMax;
    return true;
}

} // namespace Game::Client::GameLoop
