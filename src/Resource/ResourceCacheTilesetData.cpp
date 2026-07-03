#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include <cctype>
#include <optional>
#include <string>
#include <vector>

namespace Resource::Graphics
{

bool ResourceCache::SetTilesetPalette(const std::string& TilesetName)
{
    std::optional<std::vector<uint8_t>> WpeData;
    std::string WpePath = "Tileset/" + TilesetName + ".wpe";
    WpeData = ReadFile(WpePath);
    if (!WpeData)
    {
        std::string Upper;
        Upper.reserve(WpePath.size());
        for (char C : WpePath) Upper += (char)std::toupper((unsigned char)C);
        WpeData = ReadFile(Upper);
    }
    if (!WpeData)
    {
        Core::Utils::Logger::Warning("ResourceCache", "WPE not found for tileset: " + TilesetName);
        return false;
    }

    auto PalOpt = DecodeWpe(*WpeData);
    if (!PalOpt) return false;
    mData.ActivePalette = *PalOpt;

    DestroyTilesetCache();
    Core::Utils::Logger::Info("ResourceCache", "Set tileset palette: " + TilesetName);
    return true;
}

TilesetData* ResourceCache::LoadTilesetData(const std::string& TilesetName)
{
    auto It = mTilesets.DataByName.find(TilesetName);
    if (It != mTilesets.DataByName.end()) return &It->second;

    auto TryRead = [&](const std::string& Base) -> std::optional<std::vector<uint8_t>>
    {
        auto D = ReadFile(Base);
        if (D) return D;
        std::string Upper;
        Upper.reserve(Base.size());
        for (char C : Base) Upper += (char)std::toupper((unsigned char)C);
        auto D2 = ReadFile(Upper);
        if (D2) return D2;
        std::string Lower;
        Lower.reserve(Base.size());
        for (char C : Base) Lower += (char)std::tolower((unsigned char)C);
        auto D3 = ReadFile(Lower);
        if (D3) return D3;
        Core::Utils::Logger::Warning("ResourceCache", "Tileset file not found: " + Base);
        return std::nullopt;
    };

    auto Vx4Data = TryRead("Tileset/" + TilesetName + ".vx4");
    if (!Vx4Data) return nullptr;
    auto Vr4Data = TryRead("Tileset/" + TilesetName + ".vr4");
    if (!Vr4Data) return nullptr;
    auto Cv5Data = TryRead("Tileset/" + TilesetName + ".cv5");

    TilesetData& Data = mTilesets.DataByName[TilesetName];
    auto Vx4Result = Vx4Decoder::Decode(*Vx4Data);
    Data.Vx4.clear();
    Data.Vx4.reserve(Vx4Result.MegaTileCount * 16);
    for (auto& M : Vx4Result.MegaTiles)
        for (int J = 0; J < 16; J++) Data.Vx4.push_back(M.MiniTileIDs[J]);
    Data.Vr4 = std::move(*Vr4Data);

    if (Cv5Data)
    {
        auto Cv5Entries = Cv5Decoder::Decode(*Cv5Data);
        Data.Cv5.clear();
        Data.Cv5.reserve(Cv5Entries.size() * 16);
        for (auto& E : Cv5Entries)
            for (int J = 0; J < 16; J++) Data.Cv5.push_back(E.MegaTileIndices[J]);
    }

    auto Vf4Data = TryRead("Tileset/" + TilesetName + ".vf4");
    if (Vf4Data)
    {
        auto Vf4Result = Vf4Decoder::Decode(*Vf4Data);
        Data.Vf4.clear();
        Data.Vf4.reserve(Vf4Result.Entries.size() * 16);
        for (auto& E : Vf4Result.Entries)
            for (int J = 0; J < 16; J++) Data.Vf4.push_back(E.Flags[J]);
    }
    else
    {
        size_t MiniTileCount = Data.Vr4.size() / 64;
        std::vector<bool> MiniWalkable(MiniTileCount, false);
        for (size_t M = 0; M < MiniTileCount; M++)
        {
            uint8_t Counts[256] = {0};
            int NonZero = 0;
            for (int P = 0; P < 64; P++)
            {
                uint8_t V = Data.Vr4[M * 64 + P];
                if (V != 0 && Counts[V] == 0) NonZero++;
                Counts[V]++;
            }
            MiniWalkable[M] = (NonZero >= 3);
        }
        size_t MegaTileCount = Data.Vx4.size() / 16;
        Data.Vf4.clear();
        Data.Vf4.reserve(MegaTileCount * 16);
        for (size_t M = 0; M < MegaTileCount; M++)
        {
            for (int J = 0; J < 16; J++)
            {
                uint16_t MiniIdx = Data.Vx4[M * 16 + J];
                uint16_t Vr4Entry = MiniIdx / 2;
                bool Walkable = (Vr4Entry < MiniWalkable.size()) ? MiniWalkable[Vr4Entry] : true;
                Data.Vf4.push_back(Walkable ? 1 : 0);
            }
        }
    }

    Data.Loaded = true;
    Core::Utils::Logger::Info("ResourceCache",
        "Loaded tileset " + TilesetName + ": VX4=" +
        std::to_string(Data.Vx4.size() / 16) + " entries, CV5=" +
        std::to_string(Data.Cv5.size() / 16) + " groups, VR4=" +
        std::to_string(Data.Vr4.size() / 64) + " tiles, VF4=" +
        std::to_string(Data.Vf4.size() / 16) + " entries");
    return &Data;
}

const TilesetData* ResourceCache::GetTilesetData(const std::string& TilesetName)
{
    auto It = mTilesets.DataByName.find(TilesetName);
    if (It != mTilesets.DataByName.end()) return &It->second;
    return nullptr;
}

} // namespace Resource::Graphics
