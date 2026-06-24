#include "Game/sokol/TilesetRenderer.h"

#include "Render/sokol/Draw2D.h"
#include "Core/Utils/Logger.h"

namespace Game::SokolDemo
{

void TilesetRenderer::Init(const std::string& TilesetName,
                            Resource::SokolBridge::ResourceCacheSokol& Cache)
{
    mTilesetName = TilesetName;
    mCache = &Cache;

    if (TilesetName.empty()) return;
    if (!mCache->SetTilesetPalette(TilesetName))
    {
        Core::Utils::Logger::Warning("TilesetRenderer", "palette load failed: " + TilesetName);
        return;
    }
    const auto* Images = mCache->GetTilesetImages(TilesetName);
    if (Images) mMegatileCount = static_cast<int>(Images->size());
    const auto* Ts = mCache->GetTilesetData(TilesetName);
    if (Ts)
    {
        mCv5 = &Ts->Cv5;
        Core::Utils::Logger::Info("TilesetRenderer", "CV5 size=" + std::to_string(Ts->Cv5.size()) +
            " Vx4=" + std::to_string(Ts->Vx4.size()) + " Vf4=" + std::to_string(Ts->Vf4.size()));
    }
    Core::Utils::Logger::Info("TilesetRenderer", "Tileset '" + TilesetName +
        "' megatile count: " + std::to_string(mMegatileCount));
}

const Resource::SokolBridge::SokolImage* TilesetRenderer::GetMegatileImage(uint16_t MegatileIdx) const
{
    if (!mCache || mTilesetName.empty()) return nullptr;
    const auto* Images = mCache->GetTilesetImages(mTilesetName);
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

    const auto* Images = mCache->GetTilesetImages(mTilesetName);
    bool UseReal = (Images != nullptr) && (mMegatileCount > 0);

    static int DebugFrame = 5;
    if (DebugFrame > 0)
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

    int DrawnCount = 0;
    for (int TY = MinTY; TY < MaxTY; ++TY)
    {
        for (int TX = MinTX; TX < MaxTX; ++TX)
        {
            size_t Off = (size_t)TY * W + TX;
            if (Off >= Map.MegaTileIds.size()) continue;
            uint16_t RawTile = Map.MegaTileIds[Off];

            float PX = (float)(TX * TileSize);
            float PY = (float)(TY * TileSize);

            if (UseReal)
            {
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
                if (Img.Valid())
                {
                    Draw.DrawSpriteFromImage(Img, PX, PY, TileSize, TileSize);
                    DrawnCount++;
                    continue;
                }
            }

            // Synthetic fallback
            int Bucket = (int)(RawTile * 7u + 3u) % 8;  // hash the megatile id for variety
            Render::Color C = Palette[Bucket];
            Draw.DrawRect(PX, PY, TileSize, TileSize, C);
            // Add a darker edge to show tile boundaries
            Render::Color Edge(0, 0, 0, 60);
            Draw.DrawRect(PX, PY, TileSize, 1.0f, Edge);
            Draw.DrawRect(PX, PY, 1.0f, TileSize, Edge);
            DrawnCount++;
        }
    }

    static int DebugFrame2 = 5;
    if (DebugFrame2 > 0)
    {
        DebugFrame2--;
        Core::Utils::Logger::Info("TilesetRenderer",
            "DRAWN " + std::to_string(DrawnCount) + " tiles");
    }
}

} // namespace Game::SokolDemo
