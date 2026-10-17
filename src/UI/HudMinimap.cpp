#include "UI/Hud.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "Client/Entity.h"
#include "Core/Platform/Platform.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Render/Draw2D.h"
#include "Render/Private/SokolGpuInterop.h"
#include "third_party/sokol/sokol_gfx.h"
#include "UI/HudLayout.h"
#include "UI/HudMinimapGeometry.h"

namespace Game::Client::UI
{

namespace
{

struct TerrainPalette
{
    uint32_t LowWalkable;
    uint32_t LowDirt;
    uint32_t MidGround;
    uint32_t HighGround;
    uint32_t Cliff;
    uint32_t WaterOrVoid;
};

TerrainPalette GetPaletteForTileset(const std::string& TilesetName)
{
    std::string Lower = TilesetName;
    for (char& C : Lower)
        C = (char)::tolower((unsigned char)C);

    if (Lower.find("space") != std::string::npos)
    {
        return { 0xFF4B4036, 0xFF3C322B, 0xFF584E43, 0xFF665C50, 0xFF2C2420, 0xFF0D0A07 };
    }
    if (Lower.find("install") != std::string::npos)
    {
        return { 0xFF64605C, 0xFF524E4A, 0xFF726E6A, 0xFF7A7672, 0xFF2C2824, 0xFF1C1814 };
    }
    if (Lower.find("ash") != std::string::npos)
    {
        return { 0xFF243048, 0xFF2C3852, 0xFF303E60, 0xFF3A4B75, 0xFF181C28, 0xFF122C8A };
    }
    if (Lower.find("jungle") != std::string::npos)
    {
        return { 0xFF244820, 0xFF28423B, 0xFF2E5E2B, 0xFF357031, 0xFF242820, 0xFF42351A };
    }
    if (Lower.find("desert") != std::string::npos)
    {
        return { 0xFF487085, 0xFF3E5A6E, 0xFF527B94, 0xFF52829B, 0xFF263442, 0xFF1C3045 };
    }
    if (Lower.find("ice") != std::string::npos)
    {
        return { 0xFF96827B, 0xFF8A786E, 0xFF9E9089, 0xFFA89E96, 0xFF453E37, 0xFF3E2A1C };
    }
    if (Lower.find("twilight") != std::string::npos)
    {
        return { 0xFF4B373E, 0xFF52414B, 0xFF5C4852, 0xFF66525C, 0xFF22181B, 0xFF301C22 };
    }
    // Default: badlands
    return { 0xFF28502E, 0xFF344E62, 0xFF3D6073, 0xFF396530, 0xFF252830, 0xFF542F1B };
}

} // namespace

void Hud::SetMinimapState(float CamX, float CamY, float CamZoom,
                          int VpWidth, int VpHeight,
                          int MapW, int MapH,
                          const int*, const bool*, const bool*)
{
    mState.Minimap.CameraX = CamX;
    mState.Minimap.CameraY = CamY;
    mState.Minimap.CameraZoom = CamZoom;
    mState.Minimap.ViewportWidth = VpWidth;
    mState.Minimap.ViewportHeight = VpHeight;
    mState.Minimap.MapWidth = MapW;
    mState.Minimap.MapHeight = MapH;
}

void Hud::SetMinimapViewportRect(float CamWorldX, float CamWorldY, float CamWorldW, float CamWorldH)
{
    mState.Minimap.CamVisibleX = CamWorldX;
    mState.Minimap.CamVisibleY = CamWorldY;
    mState.Minimap.CamVisibleW = CamWorldW;
    mState.Minimap.CamVisibleH = CamWorldH;
}

void Hud::DrawMinimap(float, float, float, float) const
{
}

void Hud::RebuildBaseTerrainColors(int MapW, int MapH,
                                   const Game::Map::GameMap* EcsMap,
                                   const Game::Client::GameMap* ClientMap)
{
    const size_t TileCount = (size_t)MapW * (size_t)MapH;
    mBaseTerrainColors.resize(TileCount);

    const std::string TilesetName = ClientMap ? ClientMap->TilesetName : "";
    const TerrainPalette Pal = GetPaletteForTileset(TilesetName);

    for (int Y = 0; Y < MapH; ++Y)
    {
        for (int X = 0; X < MapW; ++X)
        {
            const size_t Idx = (size_t)Y * (size_t)MapW + (size_t)X;
            uint32_t Color = Pal.LowWalkable;

            if (EcsMap && EcsMap->IsInBounds(X, Y))
            {
                const auto& Tile = EcsMap->GetTile(X, Y);
                if (Tile.Terrain == Game::Components::TerrainType::Water)
                {
                    Color = Pal.WaterOrVoid;
                }
                else if (!Tile.Walkable && !Tile.Buildable)
                {
                    Color = (Tile.GroundHeight == 0 && Pal.WaterOrVoid != Pal.Cliff) ? Pal.WaterOrVoid : Pal.Cliff;
                }
                else if (Tile.GroundHeight >= 2)
                {
                    Color = Pal.HighGround;
                }
                else if (Tile.GroundHeight == 1)
                {
                    Color = Pal.MidGround;
                }
                else
                {
                    Color = (Tile.Terrain == Game::Components::TerrainType::Dirt) ? Pal.LowDirt : Pal.LowWalkable;
                }
            }
            else if (ClientMap && !ClientMap->Walkable.empty() && Idx < ClientMap->Walkable.size())
            {
                if (ClientMap->Walkable[Idx] == 0)
                    Color = Pal.Cliff;
                else
                    Color = Pal.LowWalkable;
            }

            uint16_t MegaTileID = 0;
            if (ClientMap && !ClientMap->MegaTileIds.empty() && Idx < ClientMap->MegaTileIds.size())
                MegaTileID = ClientMap->MegaTileIds[Idx];

            const int Var = (int)(((MegaTileID * 13u + (uint32_t)X * 7u + (uint32_t)Y * 11u) % 15u)) - 7;
            const uint8_t R = (uint8_t)std::clamp((int)(Color & 0xFF) + Var, 0, 255);
            const uint8_t G = (uint8_t)std::clamp((int)((Color >> 8) & 0xFF) + Var, 0, 255);
            const uint8_t B = (uint8_t)std::clamp((int)((Color >> 16) & 0xFF) + Var, 0, 255);

            mBaseTerrainColors[Idx] = 0xFF000000u | ((uint32_t)B << 16) | ((uint32_t)G << 8) | (uint32_t)R;
        }
    }
}

void Hud::UpdateMinimapTerrain(const Game::Map::GameMap* EcsMap,
                               const Game::Client::GameMap* ClientMap,
                               const Game::Systems::FogOfWarSystem* Fog,
                               int LocalPlayerID)
{
    const int MapW = ClientMap ? ClientMap->Width : (EcsMap ? EcsMap->GetWidth() : 0);
    const int MapH = ClientMap ? ClientMap->Height : (EcsMap ? EcsMap->GetHeight() : 0);
    if (MapW <= 0 || MapH <= 0)
        return;

    const std::string TilesetName = ClientMap ? ClientMap->TilesetName : "";
    const double Now = Core::Platform::GetAbsoluteTime();
    const bool TextureInvalid = !mMinimapTexture.Valid() ||
                                mMinimapTexWidth != MapW ||
                                mMinimapTexHeight != MapH ||
                                mMinimapTilesetName != TilesetName ||
                                mBaseTerrainColors.size() != (size_t)(MapW * MapH);

    const uint32_t CurrentFogRev = Fog ? Fog->GetRevision() : 0;
    const bool FogOrPlayerChanged = (CurrentFogRev != mLastFogRevision) || (LocalPlayerID != mLastLocalPlayerID);
    const bool ThrottleElapsed = (mLastMinimapTerrainTime < 0.0) || (Now - mLastMinimapTerrainTime >= 0.066);

    if (!TextureInvalid && (!FogOrPlayerChanged || !ThrottleElapsed))
    {
        return;
    }

    mLastFogRevision = CurrentFogRev;
    mLastLocalPlayerID = LocalPlayerID;
    mLastMinimapTerrainTime = Now;

    if (TextureInvalid)
    {
        RebuildBaseTerrainColors(MapW, MapH, EcsMap, ClientMap);
        mMinimapTexWidth = MapW;
        mMinimapTexHeight = MapH;
        mMinimapTilesetName = TilesetName;

        if (mMinimapTexture.View.id)
        {
            sg_destroy_view(Render::ToSokol(mMinimapTexture.View));
            mMinimapTexture.View = {};
        }
        if (mMinimapTexture.Image.id)
        {
            sg_destroy_image(Render::ToSokol(mMinimapTexture.Image));
            mMinimapTexture.Image = {};
        }
    }

    const size_t TileCount = (size_t)MapW * (size_t)MapH;
    if (mMinimapPixels.size() != TileCount)
        mMinimapPixels.resize(TileCount);

    for (int Y = 0; Y < MapH; ++Y)
    {
        for (int X = 0; X < MapW; ++X)
        {
            const size_t Idx = (size_t)Y * (size_t)MapW + (size_t)X;
            uint32_t Color = mBaseTerrainColors[Idx];

            const bool HasCreep = (EcsMap && EcsMap->IsInBounds(X, Y) && EcsMap->GetTile(X, Y).HasCreep) ||
                                  (ClientMap && !ClientMap->Creep.empty() && Idx < ClientMap->Creep.size() && ClientMap->Creep[Idx]);
            if (HasCreep)
            {
                Color = 0xFF80286Eu; // Creep purple
            }

            if (Fog)
            {
                const auto FogState = Fog->GetFog(X, Y, LocalPlayerID);
                if (FogState == Game::Systems::FogState::Unexplored)
                {
                    mMinimapPixels[Idx] = 0xFF000000u; // Solid black
                }
                else if (FogState == Game::Systems::FogState::Explored)
                {
                    const uint8_t R = (uint8_t)(((Color) & 0xFF) * 42 / 100);
                    const uint8_t G = (uint8_t)(((Color >> 8) & 0xFF) * 42 / 100);
                    const uint8_t B = (uint8_t)(((Color >> 16) & 0xFF) * 42 / 100);
                    mMinimapPixels[Idx] = 0xFF000000u | ((uint32_t)B << 16) | ((uint32_t)G << 8) | (uint32_t)R;
                }
                else
                {
                    mMinimapPixels[Idx] = Color;
                }
            }
            else
            {
                mMinimapPixels[Idx] = Color;
            }
        }
    }

    if (!sg_isvalid())
        return;

    if (!mMinimapTexture.Valid())
    {
        sg_image_desc Desc = {};
        Desc.width = MapW;
        Desc.height = MapH;
        Desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        Desc.usage.stream_update = true;
        Desc.data.mip_levels[0] = { mMinimapPixels.data(), mMinimapPixels.size() * sizeof(uint32_t) };
        Desc.label = "minimap-terrain";
        mMinimapTexture.Image = Render::FromSokol(sg_make_image(&Desc));

        sg_view_desc VDesc = {};
        VDesc.texture.image = Render::ToSokol(mMinimapTexture.Image);
        VDesc.label = "minimap-terrain-view";
        mMinimapTexture.View = Render::FromSokol(sg_make_view(&VDesc));
    }
    else
    {
        sg_image_data Data = {};
        Data.mip_levels[0] = { mMinimapPixels.data(), mMinimapPixels.size() * sizeof(uint32_t) };
        sg_update_image(Render::ToSokol(mMinimapTexture.Image), &Data);
    }
}

void Hud::DrawMinimapOverlay(Core::ECS::EntityManager& Entities,
                             const HudLayout& Layout,
                             int MapWidthTiles,
                             int MapHeightTiles)
{
    (void)Entities;
    auto& D = Render::g_Draw2D;
    D.DrawRectLines((float)Layout.MinimapRect.X - 1, (float)Layout.MinimapRect.Y - 1,
                    (float)Layout.MinimapRect.W + 2, (float)Layout.MinimapRect.H + 2,
                    Render::Color(100, 100, 120, 150), 1.0f);
    const RectI ContentRect = ResolveMinimapContentRect(
        Layout.MinimapRect, MapWidthTiles, MapHeightTiles);

    const float MapPxW = (float)(MapWidthTiles * 32);
    const float MapPxH = (float)(MapHeightTiles * 32);
    if (MapPxW <= 0.0f || MapPxH <= 0.0f)
        return;

    // 1. Draw terrain + Fog of War background
    if (mState.Minimap.ShowTerrain && mMinimapTexture.Valid())
    {
        D.DrawSprite(mMinimapTexture,
                     (float)ContentRect.X,
                     (float)ContentRect.Y,
                     (float)ContentRect.W,
                     (float)ContentRect.H);
    }
    else
    {
        D.DrawRect((float)ContentRect.X,
                   (float)ContentRect.Y,
                   (float)ContentRect.W,
                   (float)ContentRect.H,
                   Render::Color(0, 0, 0, 255));
    }

    // 2. Draw unit / building / resource dots
    if (!mState.Minimap.UnitDots.empty())
    {
        for (const auto& Dot : mState.Minimap.UnitDots)
        {
            const int PX = MinimapWorldToScreenX(ContentRect, Dot.Position.X, MapPxW);
            const int PY = MinimapWorldToScreenY(ContentRect, Dot.Position.Y, MapPxH);
            const uint8_t A = (uint8_t)((Dot.Color >> 24) & 0xFF);
            const uint8_t R = (uint8_t)((Dot.Color >> 16) & 0xFF);
            const uint8_t G = (uint8_t)((Dot.Color >> 8) & 0xFF);
            const uint8_t B = (uint8_t)(Dot.Color & 0xFF);
            const Render::Color DotColor(R, G, B, A ? A : 255);
            const float W = (float)std::max((uint8_t)1, Dot.Width);
            const float H = (float)std::max((uint8_t)1, Dot.Height);
            D.DrawRect((float)PX - W * 0.5f, (float)PY - H * 0.5f, W, H, DotColor);
        }
    }

    // 3. Draw Camera Viewport outline
    if (mState.Minimap.CamVisibleW > 0.0f && mState.Minimap.CamVisibleH > 0.0f)
    {
        const int X0 = MinimapWorldToScreenX(ContentRect, mState.Minimap.CamVisibleX, MapPxW);
        const int Y0 = MinimapWorldToScreenY(ContentRect, mState.Minimap.CamVisibleY, MapPxH);
        const int X1 = MinimapWorldToScreenX(ContentRect, mState.Minimap.CamVisibleX + mState.Minimap.CamVisibleW, MapPxW);
        const int Y1 = MinimapWorldToScreenY(ContentRect, mState.Minimap.CamVisibleY + mState.Minimap.CamVisibleH, MapPxH);

        const int MinX = std::max(ContentRect.X, std::min(X0, X1));
        const int MinY = std::max(ContentRect.Y, std::min(Y0, Y1));
        const int MaxX = std::min(ContentRect.X + ContentRect.W, std::max(X0, X1));
        const int MaxY = std::min(ContentRect.Y + ContentRect.H, std::max(Y0, Y1));

        if (MaxX > MinX && MaxY > MinY)
        {
            D.DrawRectLines((float)MinX, (float)MinY,
                            (float)(MaxX - MinX), (float)(MaxY - MinY),
                            Render::Color(255, 255, 255, 220), 1.0f);
        }
    }

    // 4. Draw Campaign radar pings
    if (mState.Minimap.ShowBeacons)
    {
        for (const auto& Ping : mState.Minimap.CampaignPings)
        {
            int PX = 0;
            int PY = 0;
            if (!MinimapWorldToScreenPoint(ContentRect,
                                           MapWidthTiles,
                                           MapHeightTiles,
                                           Ping.Position.X,
                                           Ping.Position.Y,
                                           PX,
                                           PY))
                continue;
            const float T = std::clamp(Ping.TimeRemaining / 4.0f, 0.0f, 1.0f);
            const float Radius = 4.0f + (1.0f - T) * 9.0f;
            const Render::Color PingColor(80, 210, 255, (uint8_t)(100 + 140 * T));
            D.DrawRectLines((float)PX - Radius,
                            (float)PY - Radius,
                            Radius * 2.0f,
                            Radius * 2.0f,
                            PingColor,
                            1.0f);
            D.DrawRect((float)PX - 1.0f,
                       (float)PY - Radius,
                       2.0f,
                       Radius * 2.0f,
                       PingColor);
            D.DrawRect((float)PX - Radius,
                       (float)PY - 1.0f,
                       Radius * 2.0f,
                       2.0f,
                       PingColor);
        }
    }

    // 5. Draw mode toggle buttons above the minimap
    DrawMinimapButtons(Layout);
}

void Hud::DrawMinimapButtons(const HudLayout& Layout) const
{
    auto& D = Render::g_Draw2D;
    const auto& Btns = Layout.MinimapButtonRects;

    if (Btns[0].W > 0 && Btns[2].W > 0)
    {
        const int BarX = Btns[0].X - 2;
        const int BarY = Btns[0].Y - 2;
        const int BarW = (Btns[2].X + Btns[2].W) - Btns[0].X + 4;
        const int BarH = Btns[0].H + 4;
        D.DrawRect((float)BarX, (float)BarY, (float)BarW, (float)BarH, Render::Color(10, 18, 32, 230));
        D.DrawRectLines((float)BarX, (float)BarY, (float)BarW, (float)BarH, Render::Color(35, 95, 180, 240), 1.0f);
    }

    const int MouseX = (int)Core::Platform::GetMouseX();
    const int MouseY = (int)Core::Platform::GetMouseY();

    for (int I = 0; I < 3; ++I)
    {
        const auto& Rect = Btns[(size_t)I];
        if (Rect.W <= 0)
            continue;

        bool Active = true;
        if (I == 0) Active = mState.Minimap.ShowTerrain;
        else if (I == 1) Active = mState.Minimap.UseAllianceColors;
        else if (I == 2) Active = mState.Minimap.ShowBeacons;

        const bool Hovered = Rect.Contains(MouseX, MouseY);

        const Render::Color BgCol = Active
            ? (Hovered ? Render::Color(35, 65, 110, 250) : Render::Color(20, 42, 75, 230))
            : (Hovered ? Render::Color(25, 30, 42, 230) : Render::Color(14, 18, 26, 210));
        const Render::Color BorderCol = Hovered
            ? Render::Color(90, 190, 255, 255)
            : (Active ? Render::Color(50, 130, 230, 220) : Render::Color(25, 55, 90, 160));

        D.DrawRect((float)Rect.X, (float)Rect.Y, (float)Rect.W, (float)Rect.H, BgCol);
        D.DrawRectLines((float)Rect.X, (float)Rect.Y, (float)Rect.W, (float)Rect.H, BorderCol, 1.0f);

        const float MidX = (float)Rect.X + (float)Rect.W * 0.5f;
        const float MidY = (float)Rect.Y + (float)Rect.H * 0.5f;

        if (I == 0)
        {
            // Terrain toggle: 3 yellow mountain peaks
            const Render::Color PeakCol = Active ? Render::Color(245, 215, 50, 255) : Render::Color(110, 100, 50, 180);
            D.DrawRect(MidX - 6.0f, MidY, 3.0f, 4.0f, PeakCol);
            D.DrawRect(MidX - 5.0f, MidY - 2.0f, 1.0f, 2.0f, PeakCol);
            D.DrawRect(MidX - 2.0f, MidY - 3.0f, 4.0f, 7.0f, PeakCol);
            D.DrawRect(MidX - 1.0f, MidY - 5.0f, 2.0f, 2.0f, PeakCol);
            D.DrawRect(MidX + 3.0f, MidY - 1.0f, 3.0f, 5.0f, PeakCol);
            D.DrawRect(MidX + 4.0f, MidY - 3.0f, 1.0f, 2.0f, PeakCol);
        }
        else if (I == 1)
        {
            // Alliance/Player colors toggle: dual colored dots
            const Render::Color C1 = Active ? Render::Color(55, 230, 85, 255) : Render::Color(60, 175, 255, 255);
            const Render::Color C2 = Active ? Render::Color(240, 50, 50, 255) : Render::Color(245, 205, 45, 255);
            D.DrawRect(MidX - 5.0f, MidY - 3.0f, 4.0f, 6.0f, C1);
            D.DrawRect(MidX + 1.0f, MidY - 3.0f, 4.0f, 6.0f, C2);
        }
        else if (I == 2)
        {
            // Beacons/Vision toggle: crosshair radar icon
            const Render::Color PipCol = Active ? Render::Color(75, 210, 255, 255) : Render::Color(50, 85, 120, 180);
            D.DrawRect(MidX - 5.0f, MidY - 1.0f, 10.0f, 2.0f, PipCol);
            D.DrawRect(MidX - 1.0f, MidY - 5.0f, 2.0f, 10.0f, PipCol);
            D.DrawRect(MidX - 2.0f, MidY - 2.0f, 4.0f, 4.0f, PipCol);
        }
    }
}

} // namespace Game::Client::UI
