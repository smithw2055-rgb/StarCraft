#include "Game/Systems/RenderSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Core/Utils/Logger.h"

#include <algorithm>

namespace Game::Systems
{

static Color TerrainColor(Game::Map::TerrainType T)
{
    switch (T)
    {
        case Game::Map::TerrainType::Floor:    return { 60, 55, 45, 255 };
        case Game::Map::TerrainType::Grass:    return { 50, 90, 35, 255 };
        case Game::Map::TerrainType::Dirt:     return { 80, 65, 40, 255 };
        case Game::Map::TerrainType::Rock:     return { 70, 65, 55, 255 };
        case Game::Map::TerrainType::Water:    return { 20, 50, 80, 255 };
        case Game::Map::TerrainType::Lava:     return { 90, 35, 10, 255 };
        case Game::Map::TerrainType::HighGrass: return { 40, 75, 30, 255 };
        case Game::Map::TerrainType::LowRock:  return { 65, 60, 50, 255 };
        case Game::Map::TerrainType::HighRock: return { 55, 50, 40, 255 };
        case Game::Map::TerrainType::Ruins:    return { 45, 40, 35, 255 };
        case Game::Map::TerrainType::Floor2:
        case Game::Map::TerrainType::Floor3:   return { 65, 60, 50, 255 };
        case Game::Map::TerrainType::Mud:      return { 55, 45, 30, 255 };
        case Game::Map::TerrainType::Ash:      return { 40, 40, 40, 255 };
        case Game::Map::TerrainType::Sand:     return { 90, 80, 50, 255 };
        default: return { 30, 30, 40, 255 };
    }
}

static Color ResourceColor(Game::Map::ResourceType R)
{
    switch (R)
    {
        case Game::Map::ResourceType::Minerals:  return { 60, 120, 200, 255 };
        case Game::Map::ResourceType::VespeneGas: return { 160, 60, 200, 255 };
        default: return {};
    }
}

RenderSystem::RenderSystem(Resource::ResourceCache& Cache, Core::ECS::SystemManager& SysMgr)
    : mResourceCache(Cache)
    , mSystemMgr(SysMgr)
{
    mCamera.ResetOffset();
}

void RenderSystem::SetSelection(const std::vector<int>* Selected, bool IsDragging,
                                const Core::Math::Rect* DragRect)
{
    mSelectedEntities = Selected;
    mIsDragging = IsDragging;
    if (DragRect) mDragRect = *DragRect;
}

void RenderSystem::Update(float DeltaTime)
{
    if (DeltaTime > 0.0f)
    {
        mCamera.Update(DeltaTime);
        if (IsKeyPressed(KEY_F9))
        {
            mShowWalkability = !mShowWalkability;
            Core::Utils::Logger::Info("RenderSystem",
                std::string("Walkability overlay ") + (mShowWalkability ? "ON" : "OFF"));
        }
        return;
    }

    // Render pass (DeltaTime == 0)
    auto Cam2D = mCamera.GetCamera();
    BeginMode2D(Cam2D);

    if (mMap)
        DrawTerrain();
    else
        DrawGrid();

    DrawEntities();
    if (mShowWalkability) DrawWalkability();
    DrawSelection();
    EndMode2D();

    // Screen-space drag rect
    if (mIsDragging)
    {
        DrawRectangleLines(
            (int)mDragRect.X, (int)mDragRect.Y,
            (int)mDragRect.Width, (int)mDragRect.Height,
            Color{0, 200, 0, 200});
        DrawRectangle(
            (int)mDragRect.X, (int)mDragRect.Y,
            (int)mDragRect.Width, (int)mDragRect.Height,
            Color{0, 200, 0, 40});
    }
}

void RenderSystem::DrawGrid()
{
    const float GridSize = 64.0f;
    const int GridLines = 40;
    float CenterX = 0.0f, CenterY = 0.0f;

    for (int I = -GridLines; I <= GridLines; ++I)
    {
        Color GridColor = (I % 4 == 0) ? Color{40, 40, 40, 255} : Color{20, 20, 20, 255};
        DrawLine(
            CenterX + I * GridSize, CenterY - GridLines * GridSize,
            CenterX + I * GridSize, CenterY + GridLines * GridSize,
            GridColor);
        DrawLine(
            CenterX - GridLines * GridSize, CenterY + I * GridSize,
            CenterX + GridLines * GridSize, CenterY + I * GridSize,
            GridColor);
    }
}

void RenderSystem::DrawTerrain()
{
    if (!mMap) return;

    static constexpr float TileSize = 32.0f;

    Camera2D Cam = mCamera.GetCamera();
    Vector2 TopLeft = GetScreenToWorld2D({0, 0}, Cam);
    Vector2 BottomRight = GetScreenToWorld2D(
        {(float)GetScreenWidth(), (float)GetScreenHeight()}, Cam);

    int StartX = (int)(TopLeft.x / TileSize);
    if (StartX < 0) StartX = 0;
    int StartY = (int)(TopLeft.y / TileSize);
    if (StartY < 0) StartY = 0;
    int EndX = (int)(BottomRight.x / TileSize) + 1;
    if (EndX > mMap->GetWidth()) EndX = mMap->GetWidth();
    int EndY = (int)(BottomRight.y / TileSize) + 1;
    if (EndY > mMap->GetHeight()) EndY = mMap->GetHeight();

    for (int Y = StartY; Y < EndY; Y++)
    {
        for (int X = StartX; X < EndX; X++)
        {
            const auto& Tile = mMap->GetTile(X, Y);
            float PX = X * TileSize;
            float PY = Y * TileSize;

            if (mTilesetTextures && !mTilesetTextures->empty())
            {
                // Convert tile_id → megatile_index via CV5
                uint16_t TileID = Tile.MegaTileID;
                size_t MegaIdx = TileID;
                if (mCv5Data && !mCv5Data->empty())
                {
                    uint16_t Group = (TileID >> 4) & 0x7FF;
                    uint16_t Sub = TileID & 0xF;
                    size_t Cv5Off = Group * 16 + Sub;
                    if (Cv5Off < mCv5Data->size())
                        MegaIdx = (*mCv5Data)[Cv5Off];
                }
                if (MegaIdx >= mTilesetTextures->size())
                    MegaIdx = 0;
                const Texture2D& Tex = (*mTilesetTextures)[MegaIdx];
                if (Tex.id > 0)
                    DrawTexture(Tex, (int)PX, (int)PY, WHITE);
                else
                    DrawRectangle((int)PX, (int)PY, (int)TileSize, (int)TileSize,
                                  TerrainColor(Tile.Terrain));
            }
            else
            {
                DrawRectangle((int)PX, (int)PY, (int)TileSize, (int)TileSize,
                              TerrainColor(Tile.Terrain));
            }

            // Resource overlay
            if (Tile.Resource != Game::Map::ResourceType::None)
            {
                DrawRectangle((int)PX + 2, (int)PY + 2,
                              (int)TileSize - 4, (int)TileSize - 4,
                              ResourceColor(Tile.Resource));
            }
        }
    }
}

void RenderSystem::DrawEntities()
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<Game::Components::TransformComponent,
                                       Game::Components::RenderComponent>();

    struct SortEntry
    {
        Game::Components::RenderLayer Layer;
        int ZOrder;
        Game::Components::TransformComponent* Transform;
        Game::Components::RenderComponent* Render;
    };

    std::vector<SortEntry> Sorted;
    Sorted.reserve(Entities.size());

    for (auto ID : Entities)
    {
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(ID);
        auto* Render = EM.GetComponent<Game::Components::RenderComponent>(ID);
        if (!Transform || !Render || !Render->Visible)
            continue;
        Sorted.push_back({Render->Layer, Render->ZOrder, Transform, Render});
    }

    std::sort(Sorted.begin(), Sorted.end(),
        [](const SortEntry& A, const SortEntry& B)
        {
            if (A.Layer != B.Layer)
                return (uint8_t)A.Layer < (uint8_t)B.Layer;
            return A.ZOrder < B.ZOrder;
        });

    for (auto& Entry : Sorted)
    {
        auto* Transform = Entry.Transform;
        auto* Render = Entry.Render;

        if (Render->ImageID == 0)
        {
            int S = (Render->FrameIndex == 103) ? 32 :
                    (Render->FrameIndex >= 106 && Render->FrameIndex <= 120) ? 48 :
                    (Render->FrameIndex >= 100 && Render->FrameIndex <= 102) ? 24 :
                    (Render->FrameIndex >= 140) ? 12 : 18;
            int PX = (int)Transform->Position.X - S / 2;
            int PY = (int)Transform->Position.Y - S / 2;
            DrawRectangle(PX, PY, S, S, Render->Tint);
            if (S >= 30)
                DrawRectangleLines(PX, PY, S, S, Color{255, 255, 255, 80});
            continue;
        }

        auto* Frames = mResourceCache.GetSpriteTextures(Render->ImageID);
        if (!Frames || Frames->empty())
            continue;

        int FrameIdx = Render->FrameIndex;
        if (FrameIdx < 0 || FrameIdx >= (int)Frames->size())
            FrameIdx = 0;

        const Texture2D& Tex = (*Frames)[FrameIdx];
        if (Tex.id == 0) continue;

        Rectangle Src = {0, 0, (float)Tex.width, (float)Tex.height};
        float CX = Transform->Position.X;
        float CY = Transform->Position.Y;
        float HW = Tex.width * 0.5f;
        float HH = Tex.height * 0.5f;

        Rectangle Dst = {CX - HW, CY - HH, (float)Tex.width, (float)Tex.height};
        Vector2 Origin = {0, 0};

        if (Render->Flipped)
        {
            Dst.x = CX + HW;
            Dst.width = -Dst.width;
        }

        DrawTexturePro(Tex, Src, Dst, Origin, 0.0f, Render->Tint);
    }
}

void RenderSystem::DrawFog()
{
    if (!mFogOfWar || !mMap) return;

    Camera2D Cam = mCamera.GetCamera();
    Vector2 TopLeft = GetScreenToWorld2D({0, 0}, Cam);
    Vector2 BottomRight = GetScreenToWorld2D(
        {(float)GetScreenWidth(), (float)GetScreenHeight()}, Cam);

    int StartX = (int)(TopLeft.x / 32.0f);
    if (StartX < 0) StartX = 0;
    int StartY = (int)(TopLeft.y / 32.0f);
    if (StartY < 0) StartY = 0;
    int EndX = (int)(BottomRight.x / 32.0f) + 2;
    if (EndX > mMap->GetWidth()) EndX = mMap->GetWidth();
    int EndY = (int)(BottomRight.y / 32.0f) + 2;
    if (EndY > mMap->GetHeight()) EndY = mMap->GetHeight();

    for (int Y = StartY; Y < EndY; Y++)
    {
        for (int X = StartX; X < EndX; X++)
        {
            auto Fog = mFogOfWar->GetFog(X, Y, 0);
            if (Fog == Game::Systems::FogState::Visible)
                continue;

            Color C;
            if (Fog == Game::Systems::FogState::Unexplored)
                C = {0, 0, 0, 255};
            else
                C = {0, 0, 0, 160};

            DrawRectangle(X * 32, Y * 32, 32, 32, C);
        }
    }
}

void RenderSystem::DrawBuildGhost(Core::Math::Vec2 Position, int BuildingDefID, bool Valid)
{
    int S = 48;
    if (BuildingDefID >= 106 && BuildingDefID <= 120)
        S = 48;
    else
        S = 32;

    int PX = (int)Position.X - S / 2;
    int PY = (int)Position.Y - S / 2;

    Color C = Valid ? Color{0, 255, 0, 100} : Color{255, 0, 0, 100};
    Color Border = Valid ? Color{0, 255, 0, 200} : Color{255, 0, 0, 200};

    DrawRectangle(PX, PY, S, S, C);
    DrawRectangleLines(PX, PY, S, S, Border);
}

void RenderSystem::DrawWalkability()
{
    if (!mMap) return;

    Camera2D Cam = mCamera.GetCamera();
    Vector2 TopLeft = GetScreenToWorld2D({0, 0}, Cam);
    Vector2 BottomRight = GetScreenToWorld2D(
        {(float)GetScreenWidth(), (float)GetScreenHeight()}, Cam);

    int SX = (int)(TopLeft.x / 32.0f); if (SX < 0) SX = 0;
    int SY = (int)(TopLeft.y / 32.0f); if (SY < 0) SY = 0;
    int EX = (int)(BottomRight.x / 32.0f) + 2; if (EX > mMap->GetWidth()) EX = mMap->GetWidth();
    int EY = (int)(BottomRight.y / 32.0f) + 2; if (EY > mMap->GetHeight()) EY = mMap->GetHeight();

    for (int Y = SY; Y < EY; Y++)
    {
        for (int X = SX; X < EX; X++)
        {
            bool Walk = mMap->IsWalkable(X, Y);
            DrawRectangle(X * 32, Y * 32, 32, 32,
                Walk ? Color{0, 255, 0, 60} : Color{255, 0, 0, 80});
            if (!Walk)
                DrawRectangleLines(X * 32, Y * 32, 32, 32, Color{255, 0, 0, 120});
        }
    }
}

void RenderSystem::DrawSelection()
{
    if (!mSelectedEntities || mSelectedEntities->empty())
        return;

    auto& EM = mSystemMgr.GetEntityManager();

    for (int SelID : *mSelectedEntities)
    {
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(SelID);
        auto* Render = EM.GetComponent<Game::Components::RenderComponent>(SelID);
        if (!Transform || !Render) continue;

        int S = (Render->FrameIndex == 103) ? 34 :
                (Render->FrameIndex >= 100 && Render->FrameIndex <= 102) ? 26 : 22;

        DrawRectangleLines(
            (int)Transform->Position.X - S / 2,
            (int)Transform->Position.Y - S / 2,
            S, S, Color{0, 255, 0, 220});

        // HP bar
        if (Render->FrameIndex < 100)
        {
            DrawRectangle(
                (int)Transform->Position.X - 12,
                (int)Transform->Position.Y - S / 2 - 6,
                24, 4, Color{40, 40, 40, 200});
            DrawRectangle(
                (int)Transform->Position.X - 12,
                (int)Transform->Position.Y - S / 2 - 6,
                24, 4, Color{0, 200, 0, 220});
        }
    }
}

} // namespace Game::Systems
