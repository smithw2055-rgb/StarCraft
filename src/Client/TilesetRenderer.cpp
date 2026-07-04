#include "Client/TilesetRenderer.h"

#include "Render/Draw2D.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
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
        uint32_t TextureId = 0;
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
                        Img.Image.id,
                        RawTile
                    });
                }
                else
                {
                    SyntheticTiles.push_back({
                        nullptr,
                        PX,
                        PY,
                        0,
                        RawTile
                    });
                }
            }
        }

        std::stable_sort(VisibleTiles.begin(), VisibleTiles.end(),
                         [](const VisibleTileDraw& A, const VisibleTileDraw& B) {
                             if (A.TextureId != B.TextureId)
                                 return A.TextureId < B.TextureId;
                             return A.RawTile < B.RawTile;
                         });

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

    static int DebugFrame2 = 5;
    if (DebugMapTilesEnabled() && DebugFrame2 > 0)
    {
        DebugFrame2--;
        Core::Utils::Logger::Info("TilesetRenderer",
            "DRAWN " + std::to_string(DrawnCount) + " tiles");
    }
}

} // namespace Game::Client
