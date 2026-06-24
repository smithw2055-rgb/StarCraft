#include "Game/sokol/EntityRenderer.h"

#include "Render/sokol/Draw2D.h"
#include "Render/sokol/Color.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Game::SokolDemo
{

namespace
{
    const std::vector<Resource::SokolBridge::SokolImage>* ResolveFramesFor(
        Resource::SokolBridge::ResourceCacheSokol& Cache, int ImageId)
    {
        return Cache.GetSpriteFrames(ImageId);
    }

    void ResolveFrameExtents(const Entity& E, float& HalfW, float& HalfH)
    {
        HalfW = 0.0f;
        HalfH = 0.0f;
        if (E.Frames && !E.Frames->empty() && E.FrameIndex >= 0 && E.FrameIndex < (int)E.Frames->size())
        {
            const auto& Img = (*E.Frames)[E.FrameIndex];
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
    if (!mCache) return;
    for (auto& E : Entities.GetAll())
    {
        if (E.Frames) continue;
        if (E.ImageId <= 0) continue;
        E.Frames = ResolveFramesFor(*mCache, E.ImageId);
        if (E.Frames) E.FrameCount = (int)E.Frames->size();
    }
}

void EntityRenderer::UpdateAnimation(EntityManager& Entities, float DeltaSeconds)
{
    // Idle animation: slowly cycle frames for entities with >1 frame
    // that haven't been animated by the ECS MovementSystem (their FrameIndex
    // is set to 0 = standing still). This gives a subtle life-like effect.
    static std::unordered_map<int, float> AnimTimers;
    for (auto& E : Entities.GetAll())
    {
        if (E.FrameCount <= 1) continue;
        if (E.FrameIndex != 0) continue; // ECS is driving this entity
        auto& T = AnimTimers[E.Id];
        T += DeltaSeconds;
        if (T > 2.0f) // Cycle every 2s for idle units
        {
            T = 0.0f;
            // Brief "blink": show frame 0, then cycle through 2 frames
            static int IdlePhase = 0;
            IdlePhase = (IdlePhase + 1) % 4;
            if (IdlePhase == 0) E.FrameIndex = 0;
            else if (IdlePhase < 3) E.FrameIndex = 0;
        }
    }
}

void EntityRenderer::DrawSelectionMarker(const Entity& E)
{
    if (E.Kind == EntityKind::Resource)
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

        if (E.Frames && !E.Frames->empty() && E.FrameIndex >= 0 && E.FrameIndex < (int)E.Frames->size())
        {
            const auto& Img = (*E.Frames)[E.FrameIndex];
            if (Img.Valid())
            {
                float HW = Img.Width * 0.5f;
                float HH = Img.Height * 0.5f;
                Draw.DrawSprite(Img.Image, Img.View,
                                E.X - HW, E.Y - HH,
                                (float)Img.Width, (float)Img.Height,
                                0.0f, 0.0f, 1.0f, 1.0f,
                                Render::Colors::White(), E.Flipped);
                DrewSprite = true;
            }
        }

        if (!DrewSprite)
        {
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
            }
            if (E.PlayerId == 1) Base = Render::Color(255, 80, 80, 240);
            if (E.PlayerId == 2) Base = Render::Color(80, 80, 255, 240);

            float X = E.X - Size * 0.5f;
            float Y = E.Y - Size * 0.5f;
            Draw.DrawRect(X, Y, Size, Size, Base);
            Draw.DrawRectLines(X, Y, Size, Size, Render::Color(0, 0, 0, 255), 2.0f);

            float HpFrac = (E.MaxHitPoints > 0) ? (float)E.HitPoints / (float)E.MaxHitPoints : 1.0f;
            Draw.DrawRect(X, Y - 6.0f, Size, 4.0f, Render::Color(40, 40, 40, 220));
            Draw.DrawRect(X, Y - 6.0f, Size * HpFrac, 4.0f, Render::Color(80, 230, 80, 255));
        }

        if (SelectedLookup.find(E.Id) != SelectedLookup.end())
        {
            DrawSelectionMarker(E);
        }
    }
}

} // namespace Game::SokolDemo
