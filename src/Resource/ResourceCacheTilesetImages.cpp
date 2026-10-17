#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/DdsGrpDecoder.h"
#include "Resource/GrpToGpu.h"
#include "Resource/Palette.h"

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
    return BuildTilesetAtlasForMap(TilesetName, nullptr);
}

bool ResourceCache::BuildTilesetAtlasForMap(const std::string& TilesetName,
                                            const std::vector<uint16_t>* MegaTileIds)
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
    auto Parsed = ParseDdsGrp(*HdMegatileData);
    if (!Parsed || Parsed->Frames.empty())
    {
        Core::Utils::Logger::Warning("ResourceCache",
            "Failed to parse HD tileset header for Tileset/" + TilesetName + ".dds.vr4");
        return false;
    }

    const int TotalFrames = static_cast<int>(Parsed->Frames.size());
    std::vector<bool> Needed(TotalFrames, false);
    Needed[0] = true; // Always include tile 0 (blank/fallback)

    // Include CV5 group 1 creep megatiles (16 variants)
    if (Ts->Cv5.size() >= 32)
    {
        for (size_t Variant = 0; Variant < 16; ++Variant)
        {
            uint16_t MegaIdx = Ts->Cv5[16u + Variant];
            if (MegaIdx < TotalFrames)
                Needed[MegaIdx] = true;
        }
    }

    if (MegaTileIds && !MegaTileIds->empty())
    {
        for (uint16_t RawTile : *MegaTileIds)
        {
            uint16_t MegaIdx = RawTile;
            if (!Ts->Cv5.empty())
            {
                uint16_t Group = (RawTile >> 4) & 0x7FF;
                uint16_t Sub = RawTile & 0xF;
                size_t Cv5Off = (size_t)Group * 16 + Sub;
                if (Cv5Off < Ts->Cv5.size())
                    MegaIdx = Ts->Cv5[Cv5Off];
            }
            if (MegaIdx < TotalFrames)
                Needed[MegaIdx] = true;
        }
    }
    else
    {
        // Fallback: full atlas if no map tiles specified
        std::fill(Needed.begin(), Needed.end(), true);
    }

    std::vector<int> MegaIdxToSlot(TotalFrames, -1);
    std::vector<uint16_t> SlotToMegaIdx;
    SlotToMegaIdx.reserve(TotalFrames);
    for (int I = 0; I < TotalFrames; ++I)
    {
        if (Needed[I])
        {
            MegaIdxToSlot[I] = static_cast<int>(SlotToMegaIdx.size());
            SlotToMegaIdx.push_back(static_cast<uint16_t>(I));
        }
    }

    const int FrameCount = static_cast<int>(SlotToMegaIdx.size());
    if (FrameCount == 0)
        return false;

    int FrameW = Parsed->Frames[0].Width;
    int FrameH = Parsed->Frames[0].Height;
    for (uint16_t MegaIdx : SlotToMegaIdx)
    {
        FrameW = std::max(FrameW, Parsed->Frames[MegaIdx].Width);
        FrameH = std::max(FrameH, Parsed->Frames[MegaIdx].Height);
    }
    if (FrameW <= 0 || FrameH <= 0)
        return false;

    const int CellW = FrameW + 2;
    const int CellH = FrameH + 2;
    const int Columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(FrameCount)))));
    const int Rows = (FrameCount + Columns - 1) / Columns;
    const int AtlasW = Columns * CellW;
    const int AtlasH = Rows * CellH;
    std::vector<uint8_t> Pixels(static_cast<size_t>(AtlasW) * static_cast<size_t>(AtlasH) * 4u, 0);

    for (int Slot = 0; Slot < FrameCount; ++Slot)
    {
        uint16_t MegaIdx = SlotToMegaIdx[Slot];
        const auto& FrameInfo = Parsed->Frames[MegaIdx];
        auto ImageOpt = LoadWicRgbaImageFromMemory(
            HdMegatileData->data() + FrameInfo.DataOffset,
            FrameInfo.ByteSize);
        if (!ImageOpt || ImageOpt->Width <= 0 || ImageOpt->Height <= 0 || ImageOpt->Rgba.empty())
            continue;

        const int DstX = (Slot % Columns) * CellW + 1;
        const int DstY = (Slot / Columns) * CellH + 1;
        const int CurW = ImageOpt->Width;
        const int CurH = ImageOpt->Height;

        for (int Y = 0; Y < CurH; ++Y)
        {
            const size_t SrcOff = (size_t)Y * (size_t)CurW * 4u;
            const size_t DstOff = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)DstX) * 4u;
            std::memcpy(&Pixels[DstOff], &ImageOpt->Rgba[SrcOff], (size_t)CurW * 4u);
        }

        // 1-pixel border replication for bilinear filtering
        for (int Y = 0; Y < CurH; ++Y)
        {
            const size_t LeftSrc = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)DstX) * 4u;
            const size_t LeftDst = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX - 1)) * 4u;
            std::memcpy(&Pixels[LeftDst], &Pixels[LeftSrc], 4u);

            const size_t RightSrc = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX + CurW - 1)) * 4u;
            const size_t RightDst = ((size_t)(DstY + Y) * (size_t)AtlasW + (size_t)(DstX + CurW)) * 4u;
            std::memcpy(&Pixels[RightDst], &Pixels[RightSrc], 4u);
        }

        for (int X = -1; X <= CurW; ++X)
        {
            const size_t TopSrc = ((size_t)DstY * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            const size_t TopDst = ((size_t)(DstY - 1) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            std::memcpy(&Pixels[TopDst], &Pixels[TopSrc], 4u);

            const size_t BottomSrc = ((size_t)(DstY + CurH - 1) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            const size_t BottomDst = ((size_t)(DstY + CurH) * (size_t)AtlasW + (size_t)(DstX + X)) * 4u;
            std::memcpy(&Pixels[BottomDst], &Pixels[BottomSrc], 4u);
        }
    }

    TilesetAtlas Atlas;
    Atlas.Image = UploadRgbaFull(AtlasW, AtlasH, Pixels.data());
    if (!Atlas.Image.Valid())
        return false;

    Atlas.FrameWidth = FrameW;
    Atlas.FrameHeight = FrameH;
    Atlas.CellWidth = CellW;
    Atlas.CellHeight = CellH;
    Atlas.Columns = Columns;
    Atlas.FrameCount = FrameCount;
    Atlas.MegaIdxToSlot = std::move(MegaIdxToSlot);

    mTilesets.AtlasesByName[TilesetName] = std::move(Atlas);
    const auto& StoredAtlas = mTilesets.AtlasesByName[TilesetName];
    Core::Utils::Logger::Info("ResourceCache",
        "Loaded HD tileset atlas from Tileset/" + TilesetName + ".dds.vr4: " +
        std::to_string(StoredAtlas.FrameCount) + " unique frames " +
        std::to_string(StoredAtlas.Image.Width) + "x" +
        std::to_string(StoredAtlas.Image.Height) +
        " (packed into " + std::to_string((AtlasW * AtlasH * 4) / (1024 * 1024)) + " MB texture)");
    return true;
#else
    Core::Utils::Logger::Info("ResourceCache",
        "HD tileset atlas decode is unavailable on this platform for Tileset/" +
        TilesetName + ".dds.vr4");
    return false;
#endif
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
    return GetTilesetAtlasForMap(TilesetName, nullptr);
}

const TilesetAtlas* ResourceCache::GetTilesetAtlasForMap(const std::string& TilesetName,
                                                        const std::vector<uint16_t>* MegaTileIds)
{
    auto It = mTilesets.AtlasesByName.find(TilesetName);
    if (It != mTilesets.AtlasesByName.end()) return &It->second;
    if (BuildTilesetAtlasForMap(TilesetName, MegaTileIds)) return &mTilesets.AtlasesByName[TilesetName];
    return nullptr;
}

bool ResourceCache::BuildTilesetCreepFrames(const std::string& TilesetName)
{
    auto TryRead = [&](const std::string& Base) -> std::optional<std::vector<uint8_t>>
    {
        if (auto Data = ReadFile(Base)) return Data;
        std::string Upper = Base;
        std::transform(Upper.begin(), Upper.end(), Upper.begin(),
            [](unsigned char C) { return (char)std::toupper(C); });
        if (auto Data = ReadFile(Upper)) return Data;
        std::string Lower = Base;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        return ReadFile(Lower);
    };

    auto& Out = mTilesets.CreepFramesByName[TilesetName];
    Out.clear();

#ifdef _WIN32
    // Remastered archives store the original tileset creep-edge GRP as DDS
    // frames. Prefer it so edge pixels match the HD terrain exactly.
    for (const std::string& Path : {
            "Tileset/" + TilesetName + ".dds.grp",
            "Tileset/" + TilesetName + ".grp.dds"})
    {
        auto Data = TryRead(Path);
        if (!Data) continue;
        auto Frames = LoadDdsGrpRgbaFrames(*Data);
        if (!Frames || Frames->empty()) continue;
        for (const auto& Frame : *Frames)
        {
            TilesetCreepFrame Uploaded;
            Uploaded.Image = UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data());
            // Remastered tileset GRPs are authored at 4x resolution but each
            // creep edge frame still occupies one logical 32x32 map tile.
            Uploaded.WorldWidth = 32;
            Uploaded.WorldHeight = 32;
            if (Uploaded.Image.Valid()) Out.push_back(std::move(Uploaded));
        }
        if (!Out.empty())
        {
            Core::Utils::Logger::Info("ResourceCache",
                "Loaded authentic HD creep edges from " + Path + ": " +
                std::to_string(Out.size()) + " frames");
            return true;
        }
    }
#endif

    auto GrpData = TryRead("Tileset/" + TilesetName + ".grp");
    auto WpeData = TryRead("Tileset/" + TilesetName + ".wpe");
    if (!GrpData || !WpeData)
        return false;
    auto Palette = DecodeWpe(*WpeData);
    if (!Palette)
        return false;
    GrpDecodeOptions Options;
    Options.PaletteOverride = &*Palette;
    Options.GenerateTeamColorMask = false;
    auto Decoded = DecodeGrpToRgba(*GrpData, *Palette, Options);
    if (!Decoded)
        return false;
    for (const auto& Frame : Decoded->Frames)
    {
        TilesetCreepFrame Uploaded;
        Uploaded.Image = UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data());
        Uploaded.OffsetX = Frame.OriginX;
        Uploaded.OffsetY = Frame.OriginY;
        Uploaded.WorldWidth = Frame.Width;
        Uploaded.WorldHeight = Frame.Height;
        if (Uploaded.Image.Valid()) Out.push_back(std::move(Uploaded));
    }
    if (!Out.empty())
    {
        Core::Utils::Logger::Info("ResourceCache",
            "Loaded authentic classic creep edges from Tileset/" + TilesetName +
            ".grp: " + std::to_string(Out.size()) + " frames");
    }
    return !Out.empty();
}

const std::vector<TilesetCreepFrame>* ResourceCache::GetTilesetCreepFrames(
    const std::string& TilesetName)
{
    auto It = mTilesets.CreepFramesByName.find(TilesetName);
    if (It != mTilesets.CreepFramesByName.end()) return &It->second;
    if (BuildTilesetCreepFrames(TilesetName))
        return &mTilesets.CreepFramesByName[TilesetName];
    return nullptr;
}

} // namespace Resource::Graphics
