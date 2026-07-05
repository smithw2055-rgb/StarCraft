#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/DdsGrpDecoder.h"
#include "Resource/GrpToGpu.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace Resource::Graphics
{

namespace
{
std::optional<TilesetAtlas> BuildAtlasFromFrames(const std::vector<DecodedRgbaImage>& Frames)
{
    if (Frames.empty())
        return std::nullopt;

    int FrameW = 0;
    int FrameH = 0;
    for (const auto& Frame : Frames)
    {
        FrameW = std::max(FrameW, Frame.Width);
        FrameH = std::max(FrameH, Frame.Height);
    }
    if (FrameW <= 0 || FrameH <= 0)
        return std::nullopt;

    const int Count = (int)Frames.size();
    const int CellW = FrameW + 2;
    const int CellH = FrameH + 2;
    const int Columns = std::max(1, (int)std::ceil(std::sqrt((double)Count)));
    const int Rows = (Count + Columns - 1) / Columns;
    const int AtlasW = Columns * CellW;
    const int AtlasH = Rows * CellH;
    std::vector<uint8_t> Pixels((size_t)AtlasW * (size_t)AtlasH * 4u, 0);

    for (int I = 0; I < Count; ++I)
    {
        const auto& Frame = Frames[(size_t)I];
        if (Frame.Width <= 0 || Frame.Height <= 0 || Frame.Rgba.empty())
            continue;

        const int DstX = (I % Columns) * CellW + 1;
        const int DstY = (I / Columns) * CellH + 1;
        for (int Y = 0; Y < Frame.Height; ++Y)
        {
            const size_t SrcOff = (size_t)Y * (size_t)Frame.Width * 4u;
            const size_t DstOff =
                ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)DstX) * 4u;
            std::memcpy(&Pixels[DstOff], &Frame.Rgba[SrcOff], (size_t)Frame.Width * 4u);
        }

        for (int Y = 0; Y < Frame.Height; ++Y)
        {
            const size_t LeftSrc = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)DstX) * 4u;
            const size_t LeftDst = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX - 1)) * 4u;
            std::memcpy(&Pixels[LeftDst], &Pixels[LeftSrc], 4u);

            const size_t RightSrc =
                ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX + Frame.Width - 1)) * 4u;
            const size_t RightDst =
                ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX + Frame.Width)) * 4u;
            std::memcpy(&Pixels[RightDst], &Pixels[RightSrc], 4u);
        }

        for (int X = -1; X <= Frame.Width; ++X)
        {
            const size_t TopSrc = ((size_t)DstY * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            const size_t TopDst = ((size_t)(DstY - 1) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            std::memcpy(&Pixels[TopDst], &Pixels[TopSrc], 4u);

            const size_t BottomSrc =
                ((size_t)(DstY + Frame.Height - 1) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            const size_t BottomDst =
                ((size_t)(DstY + Frame.Height) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            std::memcpy(&Pixels[BottomDst], &Pixels[BottomSrc], 4u);
        }
    }

    TilesetAtlas Atlas;
    Atlas.Image = UploadRgbaFull(AtlasW, AtlasH, Pixels.data());
    if (!Atlas.Image.Valid())
        return std::nullopt;
    Atlas.FrameWidth = FrameW;
    Atlas.FrameHeight = FrameH;
    Atlas.CellWidth = CellW;
    Atlas.CellHeight = CellH;
    Atlas.Columns = Columns;
    Atlas.FrameCount = Count;
    return Atlas;
}

}

bool ResourceCache::BuildTilesetImages(const std::string& TilesetName)
{
    auto* Ts = LoadTilesetData(TilesetName);
    if (!Ts || !Ts->Loaded) return false;

    auto& Images = mTilesets.ImagesByName[TilesetName];
    Images.clear();

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
        return ReadFile(Lower);
    };

    auto HdMegatileData = TryRead("Tileset/" + TilesetName + ".dds.vr4");
    if (!HdMegatileData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "HD tileset megatile file not found for Tileset/" + TilesetName + ".dds.vr4");
        return false;
    }

#ifdef _WIN32
    auto HdFrames = LoadDdsGrpRgbaFrames(*HdMegatileData);
    if (HdFrames && !HdFrames->empty())
    {
        Images.reserve(HdFrames->size());
        for (const auto& Frame : *HdFrames)
        {
            Images.push_back(UploadRgbaFull(
                Frame.Width, Frame.Height, Frame.Rgba.data()));
        }

        Core::Utils::Logger::Info("ResourceCache",
            "Loaded HD tileset megatiles from Tileset/" + TilesetName + ".dds.vr4: " +
            std::to_string(Images.size()) + " frames");
        return !Images.empty();
    }
    Core::Utils::Logger::Warning("ResourceCache",
        "Failed to decode HD tileset megatiles from Tileset/" + TilesetName + ".dds.vr4");
#else
    Core::Utils::Logger::Info("ResourceCache",
        "HD tileset megatile decode is unavailable on this platform for Tileset/" +
        TilesetName + ".dds.vr4");
#endif
    return false;
}

bool ResourceCache::BuildTilesetAtlas(const std::string& TilesetName)
{
    auto* Ts = LoadTilesetData(TilesetName);
    if (!Ts || !Ts->Loaded) return false;

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
        return ReadFile(Lower);
    };

    auto HdMegatileData = TryRead("Tileset/" + TilesetName + ".dds.vr4");
    if (!HdMegatileData)
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "HD tileset atlas file not found for Tileset/" + TilesetName + ".dds.vr4");
        return false;
    }

#ifdef _WIN32
    auto HdFrames = LoadDdsGrpRgbaFrames(*HdMegatileData);
    if (HdFrames && !HdFrames->empty())
    {
        auto AtlasOpt = BuildAtlasFromFrames(*HdFrames);
        if (AtlasOpt)
        {
            mTilesets.AtlasesByName[TilesetName] = std::move(*AtlasOpt);
            const auto& Atlas = mTilesets.AtlasesByName[TilesetName];
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded HD tileset atlas from Tileset/" + TilesetName + ".dds.vr4: " +
                std::to_string(Atlas.FrameCount) + " frames " +
                std::to_string(Atlas.Image.Width) + "x" +
                std::to_string(Atlas.Image.Height));
            return true;
        }
    }
    Core::Utils::Logger::Warning("ResourceCache",
        "Failed to build HD tileset atlas from Tileset/" + TilesetName + ".dds.vr4");
#else
    Core::Utils::Logger::Info("ResourceCache",
        "HD tileset atlas decode is unavailable on this platform for Tileset/" +
        TilesetName + ".dds.vr4");
#endif
    return false;
}

const std::vector<GpuImage>* ResourceCache::GetTilesetImages(const std::string& TilesetName)
{
    auto It = mTilesets.ImagesByName.find(TilesetName);
    if (It != mTilesets.ImagesByName.end()) return &It->second;
    if (BuildTilesetImages(TilesetName)) return &mTilesets.ImagesByName[TilesetName];
    return nullptr;
}

const TilesetAtlas* ResourceCache::GetTilesetAtlas(const std::string& TilesetName)
{
    auto It = mTilesets.AtlasesByName.find(TilesetName);
    if (It != mTilesets.AtlasesByName.end()) return &It->second;
    if (BuildTilesetAtlas(TilesetName)) return &mTilesets.AtlasesByName[TilesetName];
    return nullptr;
}

} // namespace Resource::Graphics
