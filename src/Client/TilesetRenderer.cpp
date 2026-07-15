#include "Client/TilesetRenderer.h"

#include "Render/Draw2D.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <vector>

namespace Game::Client
{

namespace
{
    bool DebugMapTilesEnabled()
    {
        static const bool Enabled = std::getenv("STARCRAFT_DEBUG_MAP_TILES") != nullptr;
        return Enabled;
    }

    struct VisibleTileDraw
    {
        const Resource::Graphics::GpuImage* Image = nullptr;
        float X = 0.0f;
        float Y = 0.0f;
        uint16_t RawTile = 0;
    };
}

void TilesetRenderer::Init(const std::string& TilesetName,
                            Resource::Graphics::TilesetAssetCacheView TilesetAssets)
{
    mTilesetName = TilesetName;
    mTilesetAssets = TilesetAssets;

    if (TilesetName.empty()) return;
    if (!mTilesetAssets.SetTilesetPalette(TilesetName))
    {
        Core::Utils::Logger::Warning("TilesetRenderer", "palette load failed: " + TilesetName);
        return;
    }
    mAtlas = mTilesetAssets.GetTilesetAtlas(TilesetName);
    mCreepFrames = mTilesetAssets.GetTilesetCreepFrames(TilesetName);
    if (mAtlas)
        mMegatileCount = mAtlas->FrameCount;
    else
    {
        const auto* Images = mTilesetAssets.GetTilesetImages(TilesetName);
        if (Images) mMegatileCount = static_cast<int>(Images->size());
    }

    const auto* Ts = mTilesetAssets.GetTilesetData(TilesetName);
    if (Ts)
    {
        mCv5 = &Ts->Cv5;
        Core::Utils::Logger::Info("TilesetRenderer", "CV5 size=" + std::to_string(Ts->Cv5.size()) +
            " Vx4=" + std::to_string(Ts->Vx4.size()) + " Vf4=" + std::to_string(Ts->Vf4.size()));
    }
    Core::Utils::Logger::Info("TilesetRenderer", "Tileset '" + TilesetName +
        "' megatile count: " + std::to_string(mMegatileCount));
}

const Resource::Graphics::GpuImage* TilesetRenderer::GetMegatileImage(uint16_t MegatileIdx) const
{
    if (!mTilesetAssets.HasCache() || mTilesetName.empty()) return nullptr;
    const auto* Images = mTilesetAssets.GetTilesetImages(mTilesetName);
    if (!Images) return nullptr;
    if (MegatileIdx >= Images->size()) return nullptr;
    return &(*Images)[MegatileIdx];
}

void TilesetRenderer::Render(const GameMap& Map)
{
    if (!mCamera || mTilesetName.empty()) return;
    if (Map.MegaTileIds.empty()) return;

    int W = Map.Width;
    int H = Map.Height;
    if (W <= 0 || H <= 0) return;

    float Wx = mCamera->GetX();
    float Wy = mCamera->GetY();
    float Zoom = mCamera->GetZoom();
    int VW = mCamera->GetViewportWidth();
    int VH = mCamera->GetViewportHeight();

    int MinTX = std::max(0, (int)std::floor((Wx - VW * 0.5f / Zoom) / TileSize));
    int MinTY = std::max(0, (int)std::floor((Wy - VH * 0.5f / Zoom) / TileSize));
    int MaxTX = std::min(W, (int)std::ceil((Wx + VW * 0.5f / Zoom) / TileSize) + 1);
    int MaxTY = std::min(H, (int)std::ceil((Wy + VH * 0.5f / Zoom) / TileSize) + 1);

    auto& Draw = Render::g_Draw2D;

    const auto* Atlas = mAtlas ? mAtlas : mTilesetAssets.GetTilesetAtlas(mTilesetName);
    const bool UseAtlas = Atlas && Atlas->Image.Valid() &&
        Atlas->FrameWidth > 0 && Atlas->FrameHeight > 0 &&
        Atlas->CellWidth > 0 && Atlas->CellHeight > 0 &&
        Atlas->Columns > 0 && Atlas->FrameCount > 0;
    const auto* Images = UseAtlas ? nullptr : mTilesetAssets.GetTilesetImages(mTilesetName);
    bool UseReal = UseAtlas || ((Images != nullptr) && (mMegatileCount > 0));

    static int DebugFrame = 5;
    if (DebugMapTilesEnabled() && DebugFrame > 0)
    {
        DebugFrame--;
        Core::Utils::Logger::Info("TilesetRenderer",
            "RENDER: cam=(" + std::to_string((int)Wx) + "," + std::to_string((int)Wy) +
            ") zoom=" + std::to_string(Zoom) +
            " VW=" + std::to_string(VW) + " VH=" + std::to_string(VH) +
            " map=" + std::to_string(W) + "x" + std::to_string(H) +
            " tiles=[" + std::to_string(MinTX) + ".." + std::to_string(MaxTX) +
            ")x[" + std::to_string(MinTY) + ".." + std::to_string(MaxTY) + ")" +
            " UseReal=" + std::to_string((int)UseReal) +
            " UseAtlas=" + std::to_string((int)UseAtlas) +
            " mMega=" + std::to_string(mMegatileCount) +
            " Images=" + std::to_string(Images != nullptr ? (int)Images->size() : -1));
    }

    // Synthetic palette: derive a color from the raw MegaTileID
    // so the map is visible even when the tileset atlas is not available.
    // Colors are chosen to roughly approximate each tileset's dominant look.
    static const Render::Color Palette[8] = {
        Render::Color(56, 110, 60, 255),    // 0: grass
        Render::Color(140, 110, 70, 255),   // 1: dirt
        Render::Color(60, 60, 80, 255),     // 2: platform
        Render::Color(200, 180, 110, 255),  // 3: sand
        Render::Color(150, 170, 200, 255),  // 4: ice
        Render::Color(120, 60, 50, 255),    // 5: ash
        Render::Color(80, 130, 90, 255),    // 6: jungle
        Render::Color(70, 80, 110, 255),    // 7: twilight
    };
    auto DrawSyntheticTile = [&](uint16_t RawTile, float PX, float PY) {
        int Bucket = (int)(RawTile * 7u + 3u) % 8;  // hash the megatile id for variety
        Render::Color C = Palette[Bucket];
        Draw.DrawRect(PX, PY, TileSize, TileSize, C);
        // Add a darker edge to show tile boundaries
        Render::Color Edge(0, 0, 0, 60);
        Draw.DrawRect(PX, PY, TileSize, 1.0f, Edge);
        Draw.DrawRect(PX, PY, 1.0f, TileSize, Edge);
    };

    int DrawnCount = 0;

    if (UseAtlas)
    {
        for (int TY = MinTY; TY < MaxTY; ++TY)
        {
            for (int TX = MinTX; TX < MaxTX; ++TX)
            {
                size_t Off = (size_t)TY * W + TX;
                if (Off >= Map.MegaTileIds.size()) continue;
                uint16_t RawTile = Map.MegaTileIds[Off];

                uint16_t MegaIdx = RawTile;
                if (mCv5 && !mCv5->empty())
                {
                    uint16_t Group = (RawTile >> 4) & 0x7FF;
                    uint16_t Sub = RawTile & 0xF;
                    size_t Cv5Off = (size_t)Group * 16 + Sub;
                    if (Cv5Off < mCv5->size())
                        MegaIdx = (*mCv5)[Cv5Off];
                }
                if ((int)MegaIdx >= Atlas->FrameCount) MegaIdx = 0;

                const int Col = MegaIdx % Atlas->Columns;
                const int Row = MegaIdx / Atlas->Columns;
                const int SrcX = Col * Atlas->CellWidth + 1;
                const int SrcY = Row * Atlas->CellHeight + 1;
                const float U0 = (float)SrcX / (float)Atlas->Image.Width;
                const float V0 = (float)SrcY / (float)Atlas->Image.Height;
                const float U1 = (float)(SrcX + Atlas->FrameWidth) / (float)Atlas->Image.Width;
                const float V1 = (float)(SrcY + Atlas->FrameHeight) / (float)Atlas->Image.Height;

                Draw.DrawSprite(Atlas->Image,
                                (float)(TX * TileSize),
                                (float)(TY * TileSize),
                                TileSize,
                                TileSize,
                                U0,
                                V0,
                                U1,
                                V1);
                DrawnCount++;
            }
        }
    }
    else if (UseReal)
    {
        std::vector<VisibleTileDraw> VisibleTiles;
        std::vector<VisibleTileDraw> SyntheticTiles;
        VisibleTiles.reserve((size_t)std::max(0, MaxTX - MinTX) *
                             (size_t)std::max(0, MaxTY - MinTY));

        for (int TY = MinTY; TY < MaxTY; ++TY)
        {
            for (int TX = MinTX; TX < MaxTX; ++TX)
            {
                size_t Off = (size_t)TY * W + TX;
                if (Off >= Map.MegaTileIds.size()) continue;
                uint16_t RawTile = Map.MegaTileIds[Off];

                uint16_t MegaIdx = RawTile;
                if (mCv5 && !mCv5->empty())
                {
                    uint16_t Group = (RawTile >> 4) & 0x7FF;
                    uint16_t Sub = RawTile & 0xF;
                    size_t Cv5Off = (size_t)Group * 16 + Sub;
                    if (Cv5Off < mCv5->size())
                        MegaIdx = (*mCv5)[Cv5Off];
                }
                if (MegaIdx >= Images->size()) MegaIdx = 0;
                const auto& Img = (*Images)[MegaIdx];
                const float PX = (float)(TX * TileSize);
                const float PY = (float)(TY * TileSize);
                if (Img.Valid())
                {
                    VisibleTiles.push_back({
                        &Img,
                        PX,
                        PY,
                        RawTile
                    });
                }
                else
                {
                    SyntheticTiles.push_back({
                        nullptr,
                        PX,
                        PY,
                        RawTile
                    });
                }
            }
        }

        for (const VisibleTileDraw& Tile : VisibleTiles)
        {
            Draw.DrawSpriteFromImage(*Tile.Image, Tile.X, Tile.Y, TileSize, TileSize);
            DrawnCount++;
        }

        for (const VisibleTileDraw& Tile : SyntheticTiles)
        {
            DrawSyntheticTile(Tile.RawTile, Tile.X, Tile.Y);
            DrawnCount++;
        }
    }
    else
    {
        for (int TY = MinTY; TY < MaxTY; ++TY)
        {
            for (int TX = MinTX; TX < MaxTX; ++TX)
            {
                size_t Off = (size_t)TY * W + TX;
                if (Off >= Map.MegaTileIds.size()) continue;
                uint16_t RawTile = Map.MegaTileIds[Off];

                float PX = (float)(TX * TileSize);
                float PY = (float)(TY * TileSize);

                DrawSyntheticTile(RawTile, PX, PY);
                DrawnCount++;
            }
        }
    }

    // StarCraft renders creep interiors from CV5 group 1. These are authentic
    // per-tileset megatiles, not a color overlay. Deterministic variation keeps
    // neighboring creep tiles from repeating the same 32px texture.
    const auto DrawMegatile = [&](uint16_t MegaIdx, float PX, float PY)
    {
        if (UseAtlas && MegaIdx < (uint16_t)Atlas->FrameCount)
        {
            const int Col = MegaIdx % Atlas->Columns;
            const int Row = MegaIdx / Atlas->Columns;
            const int SrcX = Col * Atlas->CellWidth + 1;
            const int SrcY = Row * Atlas->CellHeight + 1;
            Draw.DrawSprite(Atlas->Image, PX, PY, TileSize, TileSize,
                (float)SrcX / (float)Atlas->Image.Width,
                (float)SrcY / (float)Atlas->Image.Height,
                (float)(SrcX + Atlas->FrameWidth) / (float)Atlas->Image.Width,
                (float)(SrcY + Atlas->FrameHeight) / (float)Atlas->Image.Height);
            return;
        }
        if (Images && MegaIdx < Images->size() && (*Images)[MegaIdx].Valid())
            Draw.DrawSpriteFromImage((*Images)[MegaIdx], PX, PY, TileSize, TileSize);
    };

    for (int TY = MinTY; TY < MaxTY; ++TY)
    {
        for (int TX = MinTX; TX < MaxTX; ++TX)
        {
            const size_t TileIndex = (size_t)TY * (size_t)W + (size_t)TX;
            if (TileIndex >= Map.Creep.size() || Map.Creep[TileIndex] == 0)
                continue;
            const float PX = (float)(TX * TileSize);
            const float PY = (float)(TY * TileSize);
            if (mCv5 && mCv5->size() >= 32)
            {
                std::array<uint16_t, 16> ValidCreepMegatiles{};
                size_t ValidCount = 0;
                for (size_t Variant = 0; Variant < 16; ++Variant)
                {
                    const uint16_t MegaIdx = (*mCv5)[16u + Variant];
                    if (MegaIdx != 0 &&
                        ((UseAtlas && MegaIdx < (uint16_t)Atlas->FrameCount) ||
                         (Images && MegaIdx < Images->size() && (*Images)[MegaIdx].Valid())))
                    {
                        ValidCreepMegatiles[ValidCount++] = MegaIdx;
                    }
                }
                if (ValidCount > 0)
                {
                    const size_t Variant = (size_t)(TX * 17 + TY * 31) % ValidCount;
                    DrawMegatile(ValidCreepMegatiles[Variant], PX, PY);
                }
            }
        }
    }

    // The original tileset GRP supplies transition masks on non-creep tiles.
    // Use OpenBW's exact eight-neighbor-to-frame lookup.
    if (mCreepFrames && !mCreepFrames->empty())
    {
        static const std::array<int, 256> EdgeFrameByNeighbors = []
        {
            std::array<int, 256> NeighborIndex{};
            std::array<int, 128> DenseIndex{};
            for (int I = 0; I < 256; ++I)
            {
                int V = 0;
                if (I & 2) V |= 0x10;
                if (I & 8) V |= 0x24;
                if (I & 0x10) V |= 9;
                if (I & 0x40) V |= 2;
                if ((I & 0xc0) == 0xc0) V |= 1;
                if ((I & 0x60) == 0x60) V |= 4;
                if ((I & 3) == 3) V |= 0x20;
                if ((I & 6) == 6) V |= 8;
                if ((V & 0x21) == 0x21) V |= 0x40;
                if ((V & 0x0c) == 0x0c) V |= 0x40;
                NeighborIndex[(size_t)I] = V;
            }
            int Count = 0;
            for (int I = 0; I < 128; ++I)
            {
                if (std::find(NeighborIndex.begin(), NeighborIndex.end(), I) !=
                    NeighborIndex.end())
                    DenseIndex[(size_t)I] = Count++;
            }
            std::array<int, 256> Result{};
            for (int I = 0; I < 256; ++I)
                Result[(size_t)I] = DenseIndex[(size_t)NeighborIndex[(size_t)I]];
            return Result;
        }();
        static constexpr int Dirs[8][2] = {
            {1, 1}, {0, 1}, {-1, 1}, {1, 0},
            {-1, 0}, {1, -1}, {0, -1}, {-1, -1}};
        for (int TY = MinTY; TY < MaxTY; ++TY)
        {
            for (int TX = MinTX; TX < MaxTX; ++TX)
            {
                const size_t TileIndex = (size_t)TY * (size_t)W + (size_t)TX;
                if (TileIndex < Map.Creep.size() && Map.Creep[TileIndex] != 0)
                    continue;
                int Mask = 0;
                for (int I = 0; I < 8; ++I)
                {
                    const int NX = TX + Dirs[I][0];
                    const int NY = TY + Dirs[I][1];
                    if (NX < 0 || NX >= W || NY < 0 || NY >= H) continue;
                    const size_t NI = (size_t)NY * (size_t)W + (size_t)NX;
                    if (NI < Map.Creep.size() && Map.Creep[NI] != 0)
                        Mask |= 1 << I;
                }
                const int FrameNumber = EdgeFrameByNeighbors[(size_t)Mask];
                if (FrameNumber <= 0 || (size_t)(FrameNumber - 1) >= mCreepFrames->size())
                    continue;
                const auto& Frame = (*mCreepFrames)[(size_t)(FrameNumber - 1)];
                Draw.DrawSpriteFromImage(Frame.Image,
                    (float)(TX * TileSize + Frame.OffsetX),
                    (float)(TY * TileSize + Frame.OffsetY),
                    (float)(Frame.WorldWidth > 0 ? Frame.WorldWidth : Frame.Image.Width),
                    (float)(Frame.WorldHeight > 0 ? Frame.WorldHeight : Frame.Image.Height));
            }
        }
    }

    static int DebugFrame2 = 5;
    if (DebugMapTilesEnabled() && DebugFrame2 > 0)
    {
        DebugFrame2--;
        Core::Utils::Logger::Info("TilesetRenderer",
            "DRAWN " + std::to_string(DrawnCount) + " tiles");
    }
}

} // namespace Game::Client
