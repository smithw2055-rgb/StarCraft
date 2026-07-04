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

    size_t MegatileCount = Ts->Vx4.size() / 16;
    auto& Images = mTilesets.ImagesByName[TilesetName];
    Images.clear();
    Images.reserve(MegatileCount);

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

    constexpr int MegaW = 32, MegaH = 32;
    constexpr int MiniW = 8, MiniH = 8;

    if (mSources.UseExtractedAssets)
    {
        auto HdMegatileData = TryRead("Tileset/" + TilesetName + ".dds.vr4");
        if (HdMegatileData)
        {
#ifdef _WIN32
            auto HdFrames = LoadDdsGrpRgbaFrames(*HdMegatileData);
            if (HdFrames && !HdFrames->empty())
            {
                Images.reserve(HdFrames->size());
                for (const auto& Frame : *HdFrames)
                {
                    Images.push_back(UploadRgbaFull(Frame.Width, Frame.Height, Frame.Rgba.data()));
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
        }
        else
        {
            Core::Utils::Logger::Info("ResourceCache",
                "HD tileset megatile file not found for Tileset/" + TilesetName + ".dds.vr4");
        }
    }

    std::vector<uint8_t> Pixels(MegaW * MegaH * 4, 0);

    for (size_t M = 0; M < MegatileCount; M++)
    {
        std::memset(Pixels.data(), 0, Pixels.size());
        for (int Iy = 0; Iy < 4; Iy++)
        {
            for (int Ix = 0; Ix < 4; Ix++)
            {
                size_t Vx4Off = M * 16 + Iy * 4 + Ix;
                if (Vx4Off >= Ts->Vx4.size()) continue;
                uint16_t ImgIdx = Ts->Vx4[Vx4Off];
                size_t Vr4Entry = ImgIdx / 2;
                bool Inverted = (ImgIdx & 1) != 0;
                size_t Vr4Off = Vr4Entry * 64;
                if (Vr4Off + 64 > Ts->Vr4.size()) continue;

                for (int Py = 0; Py < MiniH; Py++)
                {
                    for (int Px = 0; Px < MiniW; Px++)
                    {
                        int SrcPx = Inverted ? (MiniW - 1 - Px) : Px;
                        uint8_t PalIdx = (uint8_t)Ts->Vr4[Vr4Off + Py * MiniW + SrcPx];
                        int DstX = Ix * MiniW + Px;
                        int DstY = Iy * MiniH + Py;
                        int DstOff = (DstY * MegaW + DstX) * 4;
                        const SokoColor& C = mData.ActivePalette.Colors[PalIdx];
                        Pixels[DstOff + 0] = C.R;
                        Pixels[DstOff + 1] = C.G;
                        Pixels[DstOff + 2] = C.B;
                        Pixels[DstOff + 3] = C.A;
                    }
                }
            }
        }
        Images.push_back(UploadRgbaFull(MegaW, MegaH, Pixels.data()));
    }
    Core::Utils::Logger::Info("ResourceCache",
        "Built tileset " + TilesetName + ": " + std::to_string(Images.size()) + " megatiles");
    return true;
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

    if (mSources.UseExtractedAssets)
    {
        auto HdMegatileData = TryRead("Tileset/" + TilesetName + ".dds.vr4");
        if (HdMegatileData)
        {
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
        }
    }

    constexpr int MegaW = 32, MegaH = 32;
    constexpr int MiniW = 8, MiniH = 8;
    const size_t MegatileCount = Ts->Vx4.size() / 16;
    std::vector<DecodedRgbaImage> Frames;
    Frames.reserve(MegatileCount);

    for (size_t M = 0; M < MegatileCount; M++)
    {
        DecodedRgbaImage Frame;
        Frame.Width = MegaW;
        Frame.Height = MegaH;
        Frame.Rgba.resize((size_t)MegaW * (size_t)MegaH * 4u, 0);
        for (int Iy = 0; Iy < 4; Iy++)
        {
            for (int Ix = 0; Ix < 4; Ix++)
            {
                size_t Vx4Off = M * 16 + Iy * 4 + Ix;
                if (Vx4Off >= Ts->Vx4.size()) continue;
                uint16_t ImgIdx = Ts->Vx4[Vx4Off];
                size_t Vr4Entry = ImgIdx / 2;
                bool Inverted = (ImgIdx & 1) != 0;
                size_t Vr4Off = Vr4Entry * 64;
                if (Vr4Off + 64 > Ts->Vr4.size()) continue;

                for (int Py = 0; Py < MiniH; Py++)
                {
                    for (int Px = 0; Px < MiniW; Px++)
                    {
                        int SrcPx = Inverted ? (MiniW - 1 - Px) : Px;
                        uint8_t PalIdx = (uint8_t)Ts->Vr4[Vr4Off + Py * MiniW + SrcPx];
                        int DstX = Ix * MiniW + Px;
                        int DstY = Iy * MiniH + Py;
                        int DstOff = (DstY * MegaW + DstX) * 4;
                        const SokoColor& C = mData.ActivePalette.Colors[PalIdx];
                        Frame.Rgba[(size_t)DstOff + 0] = C.R;
                        Frame.Rgba[(size_t)DstOff + 1] = C.G;
                        Frame.Rgba[(size_t)DstOff + 2] = C.B;
                        Frame.Rgba[(size_t)DstOff + 3] = C.A;
                    }
                }
            }
        }
        Frames.push_back(std::move(Frame));
    }

    auto AtlasOpt = BuildAtlasFromFrames(Frames);
    if (!AtlasOpt)
        return false;
    mTilesets.AtlasesByName[TilesetName] = std::move(*AtlasOpt);
    const auto& Atlas = mTilesets.AtlasesByName[TilesetName];
    Core::Utils::Logger::Info("ResourceCache",
        "Built tileset atlas " + TilesetName + ": " +
        std::to_string(Atlas.FrameCount) + " megatiles " +
        std::to_string(Atlas.Image.Width) + "x" +
        std::to_string(Atlas.Image.Height));
    return true;
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
