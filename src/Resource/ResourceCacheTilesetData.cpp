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
    if (!mTilesets.ActiveName.empty() && mTilesets.ActiveName != TilesetName)
    {
        DestroySpriteCache();
        mTilesets.LightTableRgbaByColorShift.clear();
    }
    mTilesets.ActiveName = TilesetName;
    Core::Utils::Logger::Info("ResourceCache", "HD tilesets use RGBA megatile textures; skipping WPE palette load");
    return true;
}

TilesetData* ResourceCache::LoadTilesetData(const std::string& TilesetName)
{
    auto It = mTilesets.DataByName.find(TilesetName);
    if (It != mTilesets.DataByName.end()) return &It->second;

    auto TryRead = [&](const std::string& Base, bool Required) -> std::optional<std::vector<uint8_t>>
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
        if (Required)
            Core::Utils::Logger::Warning("ResourceCache", "HD tileset metadata file not found: " + Base);
        return std::nullopt;
    };

    auto Vx4Data = TryRead("Tileset/" + TilesetName + ".vx4ex", true);
    if (!Vx4Data) return nullptr;
    auto Cv5Data = TryRead("Tileset/" + TilesetName + ".cv5", true);
    if (!Cv5Data) return nullptr;
    auto Vf4Data = TryRead("Tileset/" + TilesetName + ".vf4", true);
    if (!Vf4Data) return nullptr;

    TilesetData& Data = mTilesets.DataByName[TilesetName];
    auto Vx4Result = Vx4Decoder::Decode(*Vx4Data);
    Data.Vx4.clear();
    Data.Vx4.reserve(Vx4Result.MegaTileCount * 16);
    for (auto& M : Vx4Result.MegaTiles)
        for (int J = 0; J < 16; J++) Data.Vx4.push_back(M.MiniTileIDs[J]);

    auto Cv5Entries = Cv5Decoder::Decode(*Cv5Data);
    Data.Cv5.clear();
    Data.Cv5.reserve(Cv5Entries.size() * 16);
    for (auto& E : Cv5Entries)
        for (int J = 0; J < 16; J++) Data.Cv5.push_back(E.MegaTileIndices[J]);

    auto Vf4Result = Vf4Decoder::Decode(*Vf4Data);
    Data.Vf4.clear();
    Data.Vf4.reserve(Vf4Result.Entries.size() * 16);
    for (auto& E : Vf4Result.Entries)
        for (int J = 0; J < 16; J++) Data.Vf4.push_back(E.Flags[J]);

    Data.Loaded = true;
    Core::Utils::Logger::Info("ResourceCache",
        "Loaded HD tileset metadata " + TilesetName + ": VX4EX=" +
        std::to_string(Data.Vx4.size() / 16) + " entries, CV5=" +
        std::to_string(Data.Cv5.size() / 16) + " groups, VF4=" +
        std::to_string(Data.Vf4.size() / 16) + " entries");
    return &Data;
}

const TilesetData* ResourceCache::GetTilesetData(const std::string& TilesetName)
{
    auto It = mTilesets.DataByName.find(TilesetName);
    if (It != mTilesets.DataByName.end()) return &It->second;
    return LoadTilesetData(TilesetName);
}

} // namespace Resource::Graphics
