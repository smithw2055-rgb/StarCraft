#include "Resource/ResourceCache.h"

#include "Core/Utils/Logger.h"

#include "Resource/DdsGrpDecoder.h"
#include "Resource/GrpToGpu.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace Resource::Graphics
{

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

const std::vector<GpuImage>* ResourceCache::GetTilesetImages(const std::string& TilesetName)
{
    auto It = mTilesets.ImagesByName.find(TilesetName);
    if (It != mTilesets.ImagesByName.end()) return &It->second;
    if (BuildTilesetImages(TilesetName)) return &mTilesets.ImagesByName[TilesetName];
    return nullptr;
}

} // namespace Resource::Graphics
