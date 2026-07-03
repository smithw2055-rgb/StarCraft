#include "Client/EntityRenderer.h"

#include "Render/Draw2D.h"
#include "Render/Color.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cmath>

namespace Game::Client
{

namespace
{
    const std::vector<Resource::Graphics::GpuImage>* ResolveFramesFor(
        const Resource::Graphics::SpriteAssetCacheView& SpriteAssets, int ImageId)
    {
        return SpriteAssets.GetSpriteFrames(ImageId);
    }

    void ResolveFrameExtents(const Entity& E, float& HalfW, float& HalfH)
    {
        HalfW = 0.0f;
        HalfH = 0.0f;
        if (E.Frames && !E.Frames->empty())
        {
            const int FrameIndex = std::clamp(E.FrameIndex, 0, (int)E.Frames->size() - 1);
            const auto& Img = (*E.Frames)[FrameIndex];
            if (Img.Valid())
            {
                HalfW = (float)Img.Width * 0.5f;
                HalfH = (float)Img.Height * 0.5f;
            }
        }
    }
}

void EntityRenderer::ResolveFrames(EntityManager& Entities)
{
    if (!mSpriteAssets.HasCache()) return;
    for (auto& E : Entities.GetAll())
    {
        if (!E.Frames && E.ImageId >= 0)
        {
            E.Frames = ResolveFramesFor(mSpriteAssets, E.ImageId);
            if (E.Frames) E.FrameCount = (int)E.Frames->size();
        }
        for (auto& Node : E.ImageNodes)
        {
            if (!Node.Frames && Node.ImageId >= 0)
            {
                Node.Frames = ResolveFramesFor(mSpriteAssets, Node.ImageId);
                if (Node.Frames) Node.FrameCount = (int)Node.Frames->size();
            }
        }
    }
}

void EntityRenderer::UpdateAnimation(EntityManager& Entities, float DeltaSeconds)
{
    (void)Entities;
    (void)DeltaSeconds;
    // ECS systems own animation state. Keeping this renderer pass inert avoids
    // looping every GRP blindly when the unit is idle or a building is static.
}

void EntityRenderer::DrawSelectionMarker(const Entity& E)
{
    if (E.Kind == EntityKind::Resource || E.Kind == EntityKind::Effect)
        return;

    auto& Draw = Render::g_Draw2D;
    const Render::Color Shadow(0, 40, 0, 180);
    const Render::Color Glow(72, 255, 96, 255);

    float FrameHalfW = 0.0f;
    float FrameHalfH = 0.0f;
    ResolveFrameExtents(E, FrameHalfW, FrameHalfH);

    if (E.Kind == EntityKind::Unit)
    {
        float RadiusX = 12.0f;
        switch (E.Size)
        {
            case UnitSize::Small: RadiusX = 11.0f; break;
            case UnitSize::Medium: RadiusX = 15.0f; break;
            case UnitSize::Large: RadiusX = 20.0f; break;
        }
        if (FrameHalfW > 0.0f)
            RadiusX = std::max(RadiusX, FrameHalfW * 0.22f);

        float RadiusY = std::max(5.0f, RadiusX * 0.42f);
        float CenterY = E.Y + std::max(8.0f, FrameHalfH * 0.55f);
        constexpr int Segments = 28;
        for (int I = 0; I < Segments; ++I)
        {
            float T = (float)I / (float)Segments;
            float Angle = T * 6.28318530718f;
            float PX = E.X + std::cos(Angle) * RadiusX;
            float PY = CenterY + std::sin(Angle) * RadiusY;
            Draw.DrawRect(PX - 1.0f, PY - 1.0f, 3.0f, 3.0f, Shadow);
            Draw.DrawRect(PX, PY, 2.0f, 2.0f, Glow);
        }
        return;
    }

    float Width = 64.0f;
    float Height = 36.0f;
    if (E.Kind == EntityKind::Building)
    {
        Width = std::max(96.0f, FrameHalfW * 1.38f);
        Height = std::max(48.0f, std::max(24.0f, FrameHalfH * 0.60f));
    }
    else
    {
        Width = std::max(36.0f, FrameHalfW * 0.9f);
        Height = std::max(20.0f, FrameHalfH * 0.32f);
    }

    float X = E.X - Width * 0.5f;
    float Y = E.Y + std::max(10.0f, FrameHalfH * 0.68f) - Height * 0.5f;
    Draw.DrawRectLines(X - 1.0f, Y - 1.0f, Width + 2.0f, Height + 2.0f, Shadow, 1.0f);
    Draw.DrawRectLines(X, Y, Width, Height, Glow, 1.0f);
}

void EntityRenderer::DrawImageNode(const Entity& E, const EntityImageNode& Node, float OffsetX, float OffsetY) const
{
    if (!Node.Visible || !Node.Frames || Node.Frames->empty())
        return;

    int FrameIndex = std::clamp(Node.FrameIndex, 0, (int)Node.Frames->size() - 1);
    const auto& Img = (*Node.Frames)[FrameIndex];
    if (!Img.Valid())
        return;

    float HW = Img.Width * 0.5f;
    float HH = Img.Height * 0.5f;
    OffsetX = E.Flipped ? -OffsetX : OffsetX;
    bool Flipped = E.Flipped != Node.Flipped;
    Render::g_Draw2D.DrawSprite(Img,
                                E.X + OffsetX - HW, E.Y + OffsetY - HH,
                                (float)Img.Width, (float)Img.Height,
                                0.0f, 0.0f, 1.0f, 1.0f,
                                Render::Colors::White(), Flipped);
}

void EntityRenderer::DrawImageNodeTree(const Entity& E,
                                       const std::vector<EntityImageNode>& Nodes,
                                       int NodeIndex,
                                       float ParentOffsetX,
                                       float ParentOffsetY,
                                       int Depth) const
{
    if (NodeIndex < 0 || NodeIndex >= (int)Nodes.size() || Depth > 8)
        return;

    const auto& Node = Nodes[(size_t)NodeIndex];
    if (!Node.Visible)
        return;

    const float NodeOffsetX = ParentOffsetX + Node.OffsetX;
    const float NodeOffsetY = ParentOffsetY + Node.OffsetY;

    for (size_t I = 0; I < Nodes.size(); ++I)
        if (Nodes[I].ParentIndex == NodeIndex && !Nodes[I].DrawAbove)
            DrawImageNodeTree(E, Nodes, (int)I, NodeOffsetX, NodeOffsetY, Depth + 1);

    DrawImageNode(E, Node, NodeOffsetX, NodeOffsetY);

    for (size_t I = 0; I < Nodes.size(); ++I)
        if (Nodes[I].ParentIndex == NodeIndex && Nodes[I].DrawAbove)
            DrawImageNodeTree(E, Nodes, (int)I, NodeOffsetX, NodeOffsetY, Depth + 1);
}

void EntityRenderer::Render(const EntityManager& Entities, const std::vector<int>& SelectedIds)
{
    auto& Draw = Render::g_Draw2D;
    std::unordered_map<int, bool> SelectedLookup;
    SelectedLookup.reserve(SelectedIds.size());
    for (int Id : SelectedIds)
        SelectedLookup[Id] = true;

    // Viewport culling: compute visible world rect
    float HalfW = 0, HalfH = 0;
    float CullMinX = -1e9f, CullMinY = -1e9f, CullMaxX = 1e9f, CullMaxY = 1e9f;
    if (mZoom > 0.0f)
    {
        HalfW = mViewW * 0.5f / mZoom;
        HalfH = mViewH * 0.5f / mZoom;
        CullMinX = mCameraX - HalfW - 64.0f;
        CullMinY = mCameraY - HalfH - 64.0f;
        CullMaxX = mCameraX + HalfW + 64.0f;
        CullMaxY = mCameraY + HalfH + 64.0f;
    }

    // Collect visible entities
    std::vector<int> Order;
    Order.reserve(Entities.GetCount());
    for (int I = 0; I < Entities.GetCount(); ++I)
    {
        const auto& E = Entities.GetAll()[I];
        if (!E.Visible) continue;
        if (E.X < CullMinX || E.X > CullMaxX || E.Y < CullMinY || E.Y > CullMaxY)
            continue;
        Order.push_back(I);
    }
    std::stable_sort(Order.begin(), Order.end(), [&](int A, int B) {
        return Entities.GetAll()[A].Y < Entities.GetAll()[B].Y;
    });

    // Render each visible entity
    for (int I : Order)
    {
        const Entity& E = Entities.GetAll()[I];
        bool DrewSprite = false;
        const bool IsSelected = SelectedLookup.find(E.Id) != SelectedLookup.end();
        const bool ShowHealthBar = E.Kind != EntityKind::Resource && E.MaxHitPoints > 0 && IsSelected;

        for (size_t NodeIndex = 0; NodeIndex < E.ImageNodes.size(); ++NodeIndex)
            if (E.ImageNodes[NodeIndex].ParentIndex < 0 && !E.ImageNodes[NodeIndex].DrawAbove)
                DrawImageNodeTree(E, E.ImageNodes, (int)NodeIndex);

        if (E.Frames && !E.Frames->empty())
        {
            const int FrameIndex = std::clamp(E.FrameIndex, 0, (int)E.Frames->size() - 1);
            const auto& Img = (*E.Frames)[FrameIndex];
            if (Img.Valid())
            {
                float HW = Img.Width * 0.5f;
                float HH = Img.Height * 0.5f;
                Draw.DrawSprite(Img,
                                E.X - HW, E.Y - HH,
                                (float)Img.Width, (float)Img.Height,
                                0.0f, 0.0f, 1.0f, 1.0f,
                                Render::Colors::White(), E.Flipped);
                DrewSprite = true;

                if (ShowHealthBar)
                {
                    float BarW = std::max(16.0f, HW * 0.8f);
                    float BarH = 3.0f;
                    float BarX = E.X - BarW * 0.5f;
                    float BarY = E.Y - HH - 8.0f;
                    float HpFrac = (float)E.HitPoints / (float)E.MaxHitPoints;
                    Draw.DrawRect(BarX, BarY, BarW, BarH, Render::Color(40, 40, 40, 200));
                    Render::Color HpColor(80, 230, 80, 255);
                    if (HpFrac < 0.5f) HpColor = Render::Color(230, 230, 30, 255);
                    if (HpFrac < 0.25f) HpColor = Render::Color(230, 30, 30, 255);
                    Draw.DrawRect(BarX, BarY, BarW * HpFrac, BarH, HpColor);
                }
            }
        }

        for (size_t NodeIndex = 0; NodeIndex < E.ImageNodes.size(); ++NodeIndex)
            if (E.ImageNodes[NodeIndex].ParentIndex < 0 && E.ImageNodes[NodeIndex].DrawAbove)
                DrawImageNodeTree(E, E.ImageNodes, (int)NodeIndex);

        if (!DrewSprite)
        {
            static std::unordered_map<int, bool> MissingSpriteLogged;
            if (!MissingSpriteLogged[E.ChkUnitId])
            {
                MissingSpriteLogged[E.ChkUnitId] = true;
                if (E.Frames && E.Frames->empty())
                {
                    Core::Utils::Logger::Warning("EntityRenderer",
                        "Empty sprite frames for CHK unit " + std::to_string(E.ChkUnitId) +
                        " image=" + std::to_string(E.ImageId));
                }
                else
                {
                    Core::Utils::Logger::Warning("EntityRenderer",
                        "Missing sprite frames for CHK unit " + std::to_string(E.ChkUnitId) +
                        " image=" + std::to_string(E.ImageId));
                }
            }

            float Size = 28.0f;
            if (E.Size == UnitSize::Large) Size = 96.0f;
            if (E.Size == UnitSize::Medium) Size = 36.0f;
            if (E.Kind == EntityKind::Resource) Size = 22.0f;

            Render::Color Base(200, 200, 200, 240);
            switch (E.Kind)
            {
                case EntityKind::Building: Base = Render::Color(60, 200, 255, 240); break;
                case EntityKind::Unit:     Base = Render::Color(255, 255, 60, 240); break;
                case EntityKind::Resource: Base = Render::Color(220, 220, 240, 240); break;
                case EntityKind::Effect:   Base = Render::Color(180, 220, 255, 180); break;
            }
            if (E.PlayerId == 1) Base = Render::Color(255, 80, 80, 240);
            if (E.PlayerId == 2) Base = Render::Color(80, 80, 255, 240);

            float X = E.X - Size * 0.5f;
            float Y = E.Y - Size * 0.5f;
            Draw.DrawRect(X, Y, Size, Size, Base);
            Draw.DrawRectLines(X, Y, Size, Size, Render::Color(0, 0, 0, 255), 2.0f);

            if (ShowHealthBar)
            {
                float HpFrac = (E.MaxHitPoints > 0) ? (float)E.HitPoints / (float)E.MaxHitPoints : 1.0f;
                Draw.DrawRect(X, Y - 6.0f, Size, 4.0f, Render::Color(40, 40, 40, 220));
                Draw.DrawRect(X, Y - 6.0f, Size * HpFrac, 4.0f, Render::Color(80, 230, 80, 255));
            }
        }

        if (IsSelected)
        {
            DrawSelectionMarker(E);
        }
    }
}

void EntityRenderer::DrawSelectionMarker(Core::ECS::EntityManager& Entities, int EntityId)
{
    auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
    auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
    auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
    if (!Trans || !Unit || !RenderComp)
        return;

    const bool IsResource = Entities.GetComponent<Game::Components::ResourceComponent>(EntityId) != nullptr;
    const bool IsBuilding = Entities.GetComponent<Game::Components::BuildingComponent>(EntityId) != nullptr;
    const bool IsEffect = Entities.GetComponent<Game::Components::EffectComponent>(EntityId) != nullptr;
    if (IsResource || IsEffect)
        return;

    auto& Draw = Render::g_Draw2D;
    const Render::Color Shadow(0, 40, 0, 180);
    const Render::Color Glow(72, 255, 96, 255);

    float FrameHalfW = 0.0f;
    float FrameHalfH = 0.0f;
    if (mSpriteAssets.HasCache())
    {
        if (const auto* Frames = mSpriteAssets.GetDatImageFrames(RenderComp->ImageID);
            Frames && !Frames->empty())
        {
            const int FrameIndex = std::clamp(RenderComp->FrameIndex, 0, (int)Frames->size() - 1);
            const auto& Img = (*Frames)[FrameIndex];
            if (Img.Valid())
            {
                FrameHalfW = (float)Img.Width * 0.5f;
                FrameHalfH = (float)Img.Height * 0.5f;
            }
        }
    }

    if (!IsBuilding)
    {
        float RadiusX = 12.0f;
        switch (Unit->SizeType)
        {
            case Game::Components::UnitSize::Small: RadiusX = 11.0f; break;
            case Game::Components::UnitSize::Medium: RadiusX = 15.0f; break;
            case Game::Components::UnitSize::Large: RadiusX = 20.0f; break;
        }
        if (FrameHalfW > 0.0f)
            RadiusX = std::max(RadiusX, FrameHalfW * 0.22f);

        const float RadiusY = std::max(5.0f, RadiusX * 0.42f);
        const float CenterY = Trans->Position.Y + std::max(8.0f, FrameHalfH * 0.55f);
        constexpr int Segments = 28;
        for (int I = 0; I < Segments; ++I)
        {
            const float T = (float)I / (float)Segments;
            const float Angle = T * 6.28318530718f;
            const float PX = Trans->Position.X + std::cos(Angle) * RadiusX;
            const float PY = CenterY + std::sin(Angle) * RadiusY;
            Draw.DrawRect(PX - 1.0f, PY - 1.0f, 3.0f, 3.0f, Shadow);
            Draw.DrawRect(PX, PY, 2.0f, 2.0f, Glow);
        }
        return;
    }

    const float Width = std::max(96.0f, FrameHalfW * 1.38f);
    const float Height = std::max(48.0f, std::max(24.0f, FrameHalfH * 0.60f));
    const float X = Trans->Position.X - Width * 0.5f;
    const float Y = Trans->Position.Y + std::max(10.0f, FrameHalfH * 0.68f) - Height * 0.5f;
    Draw.DrawRectLines(X - 1.0f, Y - 1.0f, Width + 2.0f, Height + 2.0f, Shadow, 1.0f);
    Draw.DrawRectLines(X, Y, Width, Height, Glow, 1.0f);
}

void EntityRenderer::DrawImageNode(Core::ECS::EntityManager& Entities,
                                   int EntityId,
                                   const Game::Components::ImageSubnode& Node,
                                   float OffsetX,
                                   float OffsetY) const
{
    if (!mSpriteAssets.HasCache() || !Node.Visible)
        return;

    auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
    auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
    if (!Trans || !RenderComp)
        return;

    const auto* Frames = Node.UseSpriteID ?
        mSpriteAssets.GetSpriteOverlayFrames(Node.ImageID) :
        mSpriteAssets.GetDatImageFrames(Node.ImageID);
    if (!Frames || Frames->empty())
        return;

    const int FrameIndex = std::clamp(Node.FrameIndex, 0, (int)Frames->size() - 1);
    const auto& Img = (*Frames)[FrameIndex];
    if (!Img.Valid())
        return;

    const float HW = Img.Width * 0.5f;
    const float HH = Img.Height * 0.5f;
    OffsetX = RenderComp->Flipped ? -OffsetX : OffsetX;
    const bool Flipped = RenderComp->Flipped != Node.Flipped;
    Render::g_Draw2D.DrawSprite(Img,
                                Trans->Position.X + OffsetX - HW,
                                Trans->Position.Y + OffsetY - HH,
                                (float)Img.Width, (float)Img.Height,
                                0.0f, 0.0f, 1.0f, 1.0f,
                                Render::Colors::White(), Flipped);
}

void EntityRenderer::DrawImageNodeTree(Core::ECS::EntityManager& Entities,
                                       int EntityId,
                                       const std::vector<Game::Components::ImageSubnode>& Nodes,
                                       int NodeIndex,
                                       float ParentOffsetX,
                                       float ParentOffsetY,
                                       int Depth) const
{
    if (NodeIndex < 0 || NodeIndex >= (int)Nodes.size() || Depth > 8)
        return;

    const auto& Node = Nodes[(size_t)NodeIndex];
    if (!Node.Visible)
        return;

    const float NodeOffsetX = ParentOffsetX + Node.Offset.X;
    const float NodeOffsetY = ParentOffsetY + Node.Offset.Y;

    for (size_t I = 0; I < Nodes.size(); ++I)
        if (Nodes[I].ParentIndex == NodeIndex && !Nodes[I].DrawAbove)
            DrawImageNodeTree(Entities, EntityId, Nodes, (int)I, NodeOffsetX, NodeOffsetY, Depth + 1);

    DrawImageNode(Entities, EntityId, Node, NodeOffsetX, NodeOffsetY);

    for (size_t I = 0; I < Nodes.size(); ++I)
        if (Nodes[I].ParentIndex == NodeIndex && Nodes[I].DrawAbove)
            DrawImageNodeTree(Entities, EntityId, Nodes, (int)I, NodeOffsetX, NodeOffsetY, Depth + 1);
}

void EntityRenderer::Render(Core::ECS::EntityManager& Entities, const std::vector<int>& SelectedIds)
{
    std::unordered_map<int, bool> SelectedLookup;
    SelectedLookup.reserve(SelectedIds.size());
    for (int Id : SelectedIds)
        SelectedLookup[Id] = true;

    float HalfW = 0.0f;
    float HalfH = 0.0f;
    float CullMinX = -1e9f;
    float CullMinY = -1e9f;
    float CullMaxX = 1e9f;
    float CullMaxY = 1e9f;
    if (mZoom > 0.0f)
    {
        HalfW = mViewW * 0.5f / mZoom;
        HalfH = mViewH * 0.5f / mZoom;
        CullMinX = mCameraX - HalfW - 128.0f;
        CullMinY = mCameraY - HalfH - 128.0f;
        CullMaxX = mCameraX + HalfW + 128.0f;
        CullMaxY = mCameraY + HalfH + 128.0f;
    }

    auto Order = Entities.GetEntitiesWith<Game::Components::TransformComponent,
                                          Game::Components::RenderComponent,
                                          Game::Components::UnitComponent>();
    Order.erase(std::remove_if(Order.begin(), Order.end(), [&](int EntityId) {
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        return !Trans || !RenderComp || !RenderComp->Visible ||
               Trans->Position.X < CullMinX ||
               Trans->Position.X > CullMaxX ||
               Trans->Position.Y < CullMinY ||
               Trans->Position.Y > CullMaxY;
    }), Order.end());

    std::stable_sort(Order.begin(), Order.end(), [&](int A, int B) {
        auto* TA = Entities.GetComponent<Game::Components::TransformComponent>(A);
        auto* TB = Entities.GetComponent<Game::Components::TransformComponent>(B);
        return TA && TB ? TA->Position.Y < TB->Position.Y : A < B;
    });

    auto& Draw = Render::g_Draw2D;
    for (int EntityId : Order)
    {
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
        auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityId);
        if (!Trans || !RenderComp || !Unit)
            continue;

        bool DrewSprite = false;
        const bool IsSelected = SelectedLookup.find(EntityId) != SelectedLookup.end();
        const bool IsResource = Entities.GetComponent<Game::Components::ResourceComponent>(EntityId) != nullptr;
        const bool IsBuilding = Entities.GetComponent<Game::Components::BuildingComponent>(EntityId) != nullptr;
        const bool ShowHealthBar = !IsResource && Unit->MaxHitPoints > 0 && IsSelected;

        if (auto* Tree = Entities.GetComponent<Game::Components::ImageTreeComponent>(EntityId))
        {
            for (size_t NodeIndex = 0; NodeIndex < Tree->Nodes.size(); ++NodeIndex)
                if (Tree->Nodes[NodeIndex].ParentIndex < 0 && !Tree->Nodes[NodeIndex].DrawAbove)
                    DrawImageNodeTree(Entities, EntityId, Tree->Nodes, (int)NodeIndex);
        }

        const auto* Frames = mSpriteAssets.HasCache() ?
            mSpriteAssets.GetDatImageFrames(RenderComp->ImageID) :
            nullptr;
        if (Frames && !Frames->empty())
        {
            if (RenderComp->TotalFrames <= 1)
                RenderComp->TotalFrames = (int)Frames->size();
            const int FrameIndex = std::clamp(RenderComp->FrameIndex, 0, (int)Frames->size() - 1);
            const auto& Img = (*Frames)[FrameIndex];
            if (Img.Valid())
            {
                const float HW = Img.Width * 0.5f;
                const float HH = Img.Height * 0.5f;
                Draw.DrawSprite(Img,
                                Trans->Position.X - HW, Trans->Position.Y - HH,
                                (float)Img.Width, (float)Img.Height,
                                0.0f, 0.0f, 1.0f, 1.0f,
                                RenderComp->Tint, RenderComp->Flipped);
                DrewSprite = true;

                if (RenderComp->SubunitImageID != UINT32_MAX)
                {
                    const auto* SubunitFrames = mSpriteAssets.GetDatImageFrames(RenderComp->SubunitImageID);
                    if (SubunitFrames && !SubunitFrames->empty())
                    {
                        const int SubFrameIndex = std::clamp(
                            RenderComp->FrameIndex, 0, (int)SubunitFrames->size() - 1);
                        const auto& SubImg = (*SubunitFrames)[SubFrameIndex];
                        if (SubImg.Valid())
                        {
                            const float SubHW = SubImg.Width * 0.5f;
                            const float SubHH = SubImg.Height * 0.5f;
                            Draw.DrawSprite(SubImg,
                                            Trans->Position.X - SubHW,
                                            Trans->Position.Y - SubHH,
                                            (float)SubImg.Width,
                                            (float)SubImg.Height,
                                            0.0f, 0.0f, 1.0f, 1.0f,
                                            RenderComp->Tint,
                                            RenderComp->Flipped);
                        }
                    }
                }

                if (ShowHealthBar)
                {
                    const float BarW = std::max(16.0f, HW * 0.8f);
                    const float BarH = 3.0f;
                    const float BarX = Trans->Position.X - BarW * 0.5f;
                    const float BarY = Trans->Position.Y - HH - 8.0f;
                    const float HpFrac = (float)Unit->HitPoints / (float)Unit->MaxHitPoints;
                    Draw.DrawRect(BarX, BarY, BarW, BarH, Render::Color(40, 40, 40, 200));
                    Render::Color HpColor(80, 230, 80, 255);
                    if (HpFrac < 0.5f) HpColor = Render::Color(230, 230, 30, 255);
                    if (HpFrac < 0.25f) HpColor = Render::Color(230, 30, 30, 255);
                    Draw.DrawRect(BarX, BarY, BarW * HpFrac, BarH, HpColor);
                }
            }
        }

        if (auto* Tree = Entities.GetComponent<Game::Components::ImageTreeComponent>(EntityId))
        {
            for (size_t NodeIndex = 0; NodeIndex < Tree->Nodes.size(); ++NodeIndex)
                if (Tree->Nodes[NodeIndex].ParentIndex < 0 && Tree->Nodes[NodeIndex].DrawAbove)
                    DrawImageNodeTree(Entities, EntityId, Tree->Nodes, (int)NodeIndex);
        }

        if (!DrewSprite)
        {
            static std::unordered_map<int, bool> MissingSpriteLogged;
            if (!MissingSpriteLogged[Unit->ChkUnitID])
            {
                MissingSpriteLogged[Unit->ChkUnitID] = true;
                Core::Utils::Logger::Warning("EntityRenderer",
                    "Missing sprite frames for CHK unit " + std::to_string(Unit->ChkUnitID) +
                    " image=" + std::to_string(RenderComp->ImageID));
            }

            float Size = 28.0f;
            if (Unit->SizeType == Game::Components::UnitSize::Large) Size = 96.0f;
            if (Unit->SizeType == Game::Components::UnitSize::Medium) Size = 36.0f;
            if (IsResource) Size = 22.0f;

            Render::Color Base(200, 200, 200, 240);
            if (IsBuilding) Base = Render::Color(60, 200, 255, 240);
            else if (!IsResource) Base = Render::Color(255, 255, 60, 240);
            else Base = Render::Color(220, 220, 240, 240);
            if (Player && Player->PlayerID == 1) Base = Render::Color(255, 80, 80, 240);
            if (Player && Player->PlayerID == 2) Base = Render::Color(80, 80, 255, 240);

            const float X = Trans->Position.X - Size * 0.5f;
            const float Y = Trans->Position.Y - Size * 0.5f;
            Draw.DrawRect(X, Y, Size, Size, Base);
            Draw.DrawRectLines(X, Y, Size, Size, Render::Color(0, 0, 0, 255), 2.0f);

            if (ShowHealthBar)
            {
                const float HpFrac = Unit->MaxHitPoints > 0 ? (float)Unit->HitPoints / (float)Unit->MaxHitPoints : 1.0f;
                Draw.DrawRect(X, Y - 6.0f, Size, 4.0f, Render::Color(40, 40, 40, 220));
                Draw.DrawRect(X, Y - 6.0f, Size * HpFrac, 4.0f, Render::Color(80, 230, 80, 255));
            }
        }

        if (IsSelected)
            DrawSelectionMarker(Entities, EntityId);
    }
}

} // namespace Game::Client
