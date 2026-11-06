#include "Client/EntityRenderer.h"
#include "Client/SelectionFootprint.h"

#include "Game/DataEnums.h"
#include "Game/PlayerColor.h"
#include "Game/ProtossPower.h"
#include "Resource/GameDataLoader.h"
#include "Render/Draw2D.h"
#include "Render/Color.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <unordered_map>

namespace Game::Client
{

namespace
{
    constexpr float kTwoPi = 6.28318530718f;

    bool DebugEntityRenderEnabled()
    {
        const char* Value = std::getenv("STARCRAFT_DEBUG_ENTITY_RENDER");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    bool DebugEntityMarkersEnabled()
    {
        const char* Value = std::getenv("STARCRAFT_DEBUG_ENTITY_MARKERS");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    struct FootprintVisual
    {
        float CenterX = 0.0f;
        float CenterY = 0.0f;
        float RadiusX = 12.0f;
        float RadiusY = 5.0f;
    };

    struct SelectionVisual
    {
        FootprintVisual Footprint;
    };

    const std::vector<Resource::Graphics::GpuImage>* ResolveFramesFor(
        const Resource::Graphics::SpriteAssetCacheView& SpriteAssets, int ImageId)
    {
        return SpriteAssets.GetDatImageFrames(ImageId);
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

    void DrawFootprintEllipseOutline(Render::Draw2D& Draw,
                                     float CenterX,
                                     float CenterY,
                                     float RadiusX,
                                     float RadiusY)
    {
        const int Segments = std::clamp((int)std::lround(RadiusX * 1.2f), 24, 36);
        const Render::Color Shadow(0, 28, 0, 200);
        const Render::Color Green(70, 255, 86, 255);
        for (int I = 0; I < Segments; ++I)
        {
            const float T = (float)I / (float)Segments;
            const float Angle = T * kTwoPi;
            const float PX = CenterX + std::cos(Angle) * RadiusX;
            const float PY = CenterY + std::sin(Angle) * RadiusY;
            Draw.DrawRect(PX - 1.25f, PY - 1.25f, 2.5f, 2.5f, Shadow);
            Draw.DrawRect(PX - 0.65f, PY - 0.65f, 1.3f, 1.3f, Green);
        }
    }

    void DrawPylonPowerField(Render::Draw2D& Draw, float WorldX, float WorldY)
    {
        const Render::Color Fill(48, 116, 255, 26);
        const Render::Color Edge(92, 176, 255, 210);
        const float RadiusX = Game::ProtossPower::PylonRadiusWorld;
        const float RadiusY = RadiusX * 0.5f;
        for (int Y = -(int)RadiusY; Y <= (int)RadiusY; Y += 4)
        {
            const float NormalizedY = (float)Y / RadiusY;
            const float HalfWidth = RadiusX * std::sqrt(
                std::max(0.0f, 1.0f - NormalizedY * NormalizedY));
            Draw.DrawRect(WorldX - HalfWidth, WorldY + (float)Y,
                          HalfWidth * 2.0f, 4.0f, Fill);
        }
        constexpr int Segments = 64;
        for (int I = 0; I < Segments; ++I)
        {
            const float Angle = ((float)I / (float)Segments) * kTwoPi;
            const float X = WorldX + std::cos(Angle) * RadiusX;
            const float Y = WorldY + std::sin(Angle) * RadiusY;
            Draw.DrawRect(X - 1.0f, Y - 1.0f, 2.0f, 2.0f, Edge);
        }
    }

    Render::Color HealthColor(float HpFrac)
    {
        if (HpFrac < 0.25f)
            return Render::Color(230, 48, 38, 255);
        if (HpFrac < 0.5f)
            return Render::Color(230, 210, 42, 255);
        return Render::Color(78, 220, 78, 255);
    }

    void DrawSegmentedBar(Render::Draw2D& Draw,
                          float CenterX,
                          float Y,
                          float Width,
                          float Frac,
                          Render::Color Fill)
    {
        Frac = std::clamp(Frac, 0.0f, 1.0f);
        const float Height = 5.0f;
        const float X0 = CenterX - Width * 0.5f;
        const Render::Color Border(0, 0, 0, 235);
        Draw.SetSortContext(Render::RenderLayer2D::StatusBars,
                            Render::RenderSubBand2D::StatusBarBorder,
                            0u);
        Draw.DrawRect(X0 - 1.0f, Y - 1.0f, Width + 2.0f, Height + 2.0f, Border);
        Draw.SetSortContext(Render::RenderLayer2D::StatusBars,
                            Render::RenderSubBand2D::StatusBarFill,
                            0u);
        Draw.DrawHealthBar(X0, Y, Width, Height, Frac, Fill);
    }

    void DrawSegmentedStatusBars(Render::Draw2D& Draw,
                                 float CenterX,
                                 float Y,
                                 float Width,
                                 int HitPoints,
                                 int MaxHitPoints,
                                 int ShieldPoints,
                                 int MaxShieldPoints,
                                 int EnergyPoints,
                                 int MaxEnergyPoints)
    {
        if (MaxHitPoints <= 0)
            return;

        const float HpFrac = (float)HitPoints / (float)MaxHitPoints;
        DrawSegmentedBar(Draw, CenterX, Y, Width, HpFrac, HealthColor(HpFrac));

        if (MaxShieldPoints > 0)
        {
            const float ShieldFrac = (float)ShieldPoints / (float)MaxShieldPoints;
            DrawSegmentedBar(Draw, CenterX, Y + 6.0f, Width, ShieldFrac,
                             Render::Color(64, 112, 255, 255));
        }
        else if (MaxEnergyPoints > 0)
        {
            const float EnergyFrac = (float)EnergyPoints / (float)MaxEnergyPoints;
            DrawSegmentedBar(Draw, CenterX, Y + 6.0f, Width, EnergyFrac,
                             Render::Color(178, 72, 230, 255));
        }
    }

    FootprintVisual UnitFootprint(float X,
                                  float Y,
                                  UnitSize Size,
                                  float FrameHalfW,
                                  float FrameHalfH)
    {
        float FallbackRadius = 12.0f;
        switch (Size)
        {
            case UnitSize::Small: FallbackRadius = 16.0f; break;
            case UnitSize::Medium: FallbackRadius = 23.0f; break;
            case UnitSize::Large: FallbackRadius = 34.0f; break;
        }
        const BuildingFootprintSpec F =
            SelectionEllipseFromVisualBounds(X, Y, FrameHalfW, FrameHalfH, FallbackRadius);
        return {F.CenterX, F.CenterY, F.RadiusX, F.RadiusY};
    }

    [[maybe_unused]] float UnitFallbackRadius(UnitSize Size)
    {
        switch (Size)
        {
            case UnitSize::Small: return 16.0f;
            case UnitSize::Medium: return 23.0f;
            case UnitSize::Large: return 34.0f;
        }
        return 12.0f;
    }

    float UnitFallbackRadius(Game::Components::UnitSize Size)
    {
        switch (Size)
        {
            case Game::Components::UnitSize::Small: return 16.0f;
            case Game::Components::UnitSize::Medium: return 23.0f;
            case Game::Components::UnitSize::Large: return 34.0f;
        }
        return 12.0f;
    }

    FootprintVisual UnitFootprintFromVisualRadius(float X,
                                                  float Y,
                                                  Game::Components::UnitSize Size,
                                                  float VisualRadius)
    {
        (void)VisualRadius;
        const BuildingFootprintSpec F =
            SelectionEllipseFromVisualRadius(X, Y, UnitFallbackRadius(Size), UnitFallbackRadius(Size));
        return {F.CenterX, F.CenterY, F.RadiusX, F.RadiusY};
    }

    FootprintVisual FootprintFromDatDimensions(float X,
                                               float Y,
                                               const Resource::UnitEntry& Unit)
    {
        const BuildingFootprintSpec F =
            SelectionEllipseFromDatBounds(
                X, Y,
                Unit.DimensionLeft, Unit.DimensionUp,
                Unit.DimensionRight, Unit.DimensionDown,
                12.0f);
        return {F.CenterX, F.CenterY, F.RadiusX, F.RadiusY};
    }

    FootprintVisual BuildingFootprintFromVisualRadius(float X,
                                                      float Y,
                                                      int ChkUnitID,
                                                      float VisualRadius)
    {
        const BuildingFootprintTiles Tiles = BuildingFootprintSize(ChkUnitID);
        const float FallbackHalfW = Tiles.W * 16.0f;
        const float FallbackHalfH = Tiles.H * 16.0f;
        const float FallbackRadius =
            std::sqrt(FallbackHalfW * FallbackHalfW + FallbackHalfH * FallbackHalfH);
        const BuildingFootprintSpec F =
            SelectionEllipseFromVisualRadius(X, Y, VisualRadius, FallbackRadius);
        return {F.CenterX, F.CenterY, F.RadiusX, F.RadiusY};
    }

    const Resource::GameDataLoader* DataLoader(
        const Resource::Graphics::DataAssetCacheView& DataAssets)
    {
        return DataAssets.HasCache() ? DataAssets.GetDataLoader() : nullptr;
    }

    const Resource::UnitEntry* FindUnitData(
        const Resource::Graphics::DataAssetCacheView& DataAssets,
        int ChkUnitID)
    {
        const auto* Loader = DataLoader(DataAssets);
        return Loader ? Loader->FindUnitByID((uint32_t)ChkUnitID) : nullptr;
    }

    const Resource::SpriteEntry* FindUnitSpriteData(
        const Resource::Graphics::DataAssetCacheView& DataAssets,
        int ChkUnitID)
    {
        const auto* Loader = DataLoader(DataAssets);
        if (!Loader)
            return nullptr;
        const uint32_t SpriteID = Loader->ResolveUnitSpriteID((uint32_t)ChkUnitID);
        return SpriteID != UINT32_MAX ? Loader->FindSpriteByID(SpriteID) : nullptr;
    }

    FootprintVisual FootprintFromSpriteCircle(float X,
                                              float Y,
                                              const Resource::SpriteEntry* Sprite)
    {
        if (Sprite)
        {
            const BuildingFootprintSpec F =
                SelectionEllipseFromSpriteCircle(
                    X, Y,
                    (int)Sprite->SelectionCircle,
                    (int)Sprite->SelectionCircleVerticalOffset,
                    12.0f);
            return {F.CenterX, F.CenterY, F.RadiusX, F.RadiusY};
        }

        return {X, Y, 12.0f, 5.0f};
    }

    FootprintVisual SelectionFootprintForUnit(
        const Resource::Graphics::DataAssetCacheView& DataAssets,
        const Resource::Graphics::SpriteAssetCacheView& SpriteAssets,
        int ChkUnitID,
        float X,
        float Y,
        bool IsBuilding,
        Game::Components::UnitSize Size,
        float VisualRadius)
    {
        (void)SpriteAssets;
        const auto* Sprite = FindUnitSpriteData(DataAssets, ChkUnitID);
        if (Sprite && SelectionCirclePixelSize((int)Sprite->SelectionCircle) > 0)
            return FootprintFromSpriteCircle(X, Y, Sprite);

        const auto* UnitData = FindUnitData(DataAssets, ChkUnitID);
        if (UnitData && UnitData->DimensionWidth() > 0 && UnitData->DimensionHeight() > 0)
            return FootprintFromDatDimensions(X, Y, *UnitData);

        return IsBuilding
            ? BuildingFootprintFromVisualRadius(X, Y, ChkUnitID, VisualRadius)
            : UnitFootprintFromVisualRadius(X, Y, Size, VisualRadius);
    }

    SelectionVisual SelectionVisualForUnit(
        const Resource::Graphics::DataAssetCacheView& DataAssets,
        const Resource::Graphics::SpriteAssetCacheView& SpriteAssets,
        int ChkUnitID,
        float X,
        float Y,
        bool IsBuilding,
        Game::Components::UnitSize Size,
        float VisualRadius)
    {
        if (const auto* Sprite = FindUnitSpriteData(DataAssets, ChkUnitID);
            Sprite && SelectionCirclePixelSize((int)Sprite->SelectionCircle) > 0)
        {
            return {
                FootprintFromSpriteCircle(X, Y, Sprite)
            };
        }

        return {
            SelectionFootprintForUnit(DataAssets, SpriteAssets, ChkUnitID,
                                      X, Y, IsBuilding, Size, VisualRadius)
        };
    }

    void DrawSelectionVisual(Render::Draw2D& Draw, const SelectionVisual& Visual)
    {
        const FootprintVisual& F = Visual.Footprint;
        DrawFootprintEllipseOutline(Draw, F.CenterX, F.CenterY, F.RadiusX, F.RadiusY);
    }

    float StatusBarWidthFromSpriteData(
        const Resource::Graphics::DataAssetCacheView& DataAssets,
        int ChkUnitID,
        const FootprintVisual& F,
        bool IsBuilding)
    {
        if (const auto* Sprite = FindUnitSpriteData(DataAssets, ChkUnitID);
            Sprite && Sprite->HealthBar > 0)
        {
            return (float)Sprite->HealthBar;
        }

        return IsBuilding
            ? std::max(42.0f, F.RadiusX * 1.10f)
            : std::max(24.0f, F.RadiusX * 1.30f);
    }

    void AccumulateRectVisualRadius(float OffsetX,
                                    float OffsetY,
                                    float HalfW,
                                    float HalfH,
                                    float& Radius)
    {
        const float Xs[2] = {OffsetX - HalfW, OffsetX + HalfW};
        const float Ys[2] = {OffsetY - HalfH, OffsetY + HalfH};
        for (float X : Xs)
        {
            for (float Y : Ys)
            {
                Radius = std::max(Radius, std::sqrt(X * X + Y * Y));
            }
        }
    }

    [[maybe_unused]] FootprintVisual UnitFootprint(float X,
                                  float Y,
                                  Game::Components::UnitSize Size,
                                  float FrameHalfW,
                                  float FrameHalfH)
    {
        float FallbackRadius = 12.0f;
        switch (Size)
        {
            case Game::Components::UnitSize::Small: FallbackRadius = 16.0f; break;
            case Game::Components::UnitSize::Medium: FallbackRadius = 23.0f; break;
            case Game::Components::UnitSize::Large: FallbackRadius = 34.0f; break;
        }
        const BuildingFootprintSpec F =
            SelectionEllipseFromVisualBounds(X, Y, FrameHalfW, FrameHalfH, FallbackRadius);
        return {F.CenterX, F.CenterY, F.RadiusX, F.RadiusY};
    }

    FootprintVisual BuildingFootprint(float X,
                                      float Y,
                                      int ChkUnitID,
                                      float FrameHalfW,
                                      float FrameHalfH)
    {
        const BuildingFootprintSpec F =
            BuildingFootprintForSelection(X, Y, ChkUnitID, FrameHalfW, FrameHalfH);
        return {
            F.CenterX,
            F.CenterY,
            F.RadiusX,
            F.RadiusY
        };
    }

    FootprintVisual FallbackFootprint(float X, float Y, float Size)
    {
        return {
            X,
            Y + Size * 0.38f,
            std::max(12.0f, Size * 0.30f),
            std::max(5.0f, Size * 0.12f)
        };
    }

    float StatusBarY(const FootprintVisual& F)
    {
        return F.CenterY + F.RadiusY + 1.0f;
    }

    int RenderSortBand(const Entity& E)
    {
        if (Game::Data::IsAirUnitType(E.ChkUnitId))
            return 2;
        if (E.Kind == EntityKind::Effect)
            return 3;
        return 1;
    }

    bool IsAirEntity(Core::ECS::EntityManager& Entities, int EntityId)
    {
        if (auto* Path = Entities.GetComponent<Game::Components::PathfindingComponent>(EntityId))
            if (Path->IsAir)
                return true;
        if (auto* Combat = Entities.GetComponent<Game::Components::CombatComponent>(EntityId))
            if (Combat->MyElevation == Game::Components::Elevation::Air)
                return true;
        if (auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId))
            return Game::Data::IsAirUnitType(Unit->ChkUnitID);
        return false;
    }

    int RenderSortBand(Core::ECS::EntityManager& Entities, int EntityId)
    {
        if (const auto* Effect =
                Entities.GetComponent<Game::Components::EffectComponent>(EntityId))
        {
            return Effect->RenderBand >= 0
                ? Effect->RenderBand
                : (Effect->DrawAbove ? 3 : 0);
        }
        if (IsAirEntity(Entities, EntityId))
            return 2;
        return 1;
    }

    Render::RenderLayer2D MapBandToRenderLayer(int Band)
    {
        if (Band <= 0)
            return Render::RenderLayer2D::GroundEffect;
        if (Band == 1)
            return Render::RenderLayer2D::GroundEntities;
        if (Band == 2)
            return Render::RenderLayer2D::AirEntities;
        return Render::RenderLayer2D::TopEffects;
    }

    struct SpriteVisualEffect
    {
        float Hallucination = 0.0f;
        float Cloaked = 0.0f;
        float Brighten = 0.0f;
        float Shadow = 0.0f;
        float WarpTexture = 0.0f;
        const Resource::Graphics::GpuImage* EffectMask = nullptr;

        bool Any() const
        {
            return Hallucination > 0.0f || Cloaked > 0.0f ||
                   Brighten > 0.0f || Shadow > 0.0f || WarpTexture > 0.0f;
        }
    };

    float ProtossConstructionRiftScale(
        Core::ECS::EntityManager& Entities,
        int EntityId)
    {
        const auto* Building =
            Entities.GetComponent<Game::Components::BuildingComponent>(EntityId);
        const auto* Unit =
            Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
        if (!Building || !Unit || !Building->UnderConstruction ||
            Unit->UnitRace != Game::Components::Race::Protoss)
        {
            return 1.0f;
        }
        if (Building->ProtossPhase ==
            Game::Components::ProtossConstructionPhase::RiftClosing)
        {
            return 1.0f;
        }
        if (Building->ProtossPhase !=
            Game::Components::ProtossConstructionPhase::Rift)
        {
            return 1.0f;
        }

        const float Progress = Building->TotalBuildTime > 0.0f
            ? std::clamp(Building->BuildProgress / Building->TotalBuildTime,
                         0.0f, 1.0f)
            : 0.0f;
        const float SmoothProgress =
            Progress * Progress * (3.0f - 2.0f * Progress);
        return 0.18f + 0.82f * SmoothProgress;
    }

    const std::vector<Resource::Graphics::GpuImage>* ProtossWarpTextureFrames(
        const Resource::Graphics::DataAssetCacheView& DataAssets,
        const Resource::Graphics::SpriteAssetCacheView& SpriteAssets)
    {
        const auto* Loader = DataLoader(DataAssets);
        if (!Loader || !SpriteAssets.HasCache())
            return nullptr;

        static const Resource::GameDataLoader* CachedLoader = nullptr;
        static uint32_t CachedImageID = UINT32_MAX;
        if (CachedLoader != Loader)
        {
            CachedLoader = Loader;
            CachedImageID = UINT32_MAX;
            for (uint32_t ImageID = 0; ImageID < 999; ++ImageID)
            {
                std::string Path = Loader->ResolveImageGrpPath(ImageID);
                std::replace(Path.begin(), Path.end(), '\\', '/');
                std::transform(Path.begin(), Path.end(), Path.begin(),
                    [](unsigned char C) { return (char)std::tolower(C); });
                if (Path == "protoss/texture.grp")
                {
                    CachedImageID = ImageID;
                    break;
                }
            }
        }
        return CachedImageID != UINT32_MAX
            ? SpriteAssets.GetDatImageFrames(CachedImageID)
            : nullptr;
    }

    bool HasStatusEffect(Core::ECS::EntityManager& Entities,
                         int EntityId,
                         Game::Components::StatusEffect Effect)
    {
        auto* Status = Entities.GetComponent<Game::Components::StatusEffectComponent>(EntityId);
        return Status && Status->HasEffect(Effect);
    }

    SpriteVisualEffect ResolveSpriteVisualEffect(
        Core::ECS::EntityManager& Entities,
        int EntityId,
        const Resource::Graphics::DataAssetCacheView& DataAssets,
        const Resource::Graphics::SpriteAssetCacheView& SpriteAssets,
        uint32_t ImageID)
    {
        SpriteVisualEffect Effect;
        if (auto* Cloak = Entities.GetComponent<Game::Components::CloakComponent>(EntityId);
            Cloak && (Cloak->IsCloaked || Cloak->IsBurrowed))
        {
            Effect.Cloaked = 1.0f;
        }

        if (HasStatusEffect(Entities, EntityId, Game::Components::StatusEffect::Hallucination))
            Effect.Hallucination = 1.0f;

        const auto* Building =
            Entities.GetComponent<Game::Components::BuildingComponent>(EntityId);
        const auto* Unit =
            Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
        const auto* Render =
            Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        if (Building && Building->UnderConstruction && Unit && Render &&
            Unit->UnitRace == Game::Components::Race::Protoss &&
            Render->UseProtossWarpTexture && ImageID == Render->ImageID)
        {
            if (const auto* WarpFrames =
                    ProtossWarpTextureFrames(DataAssets, SpriteAssets);
                WarpFrames && !WarpFrames->empty())
            {
                const int Frame = std::clamp(
                    Render->ProtossWarpTextureFrame,
                    0, (int)WarpFrames->size() - 1);
                Effect.WarpTexture = 1.0f;
                Effect.EffectMask = &(*WarpFrames)[(size_t)Frame];
            }
        }

        if (const auto* Loader = DataLoader(DataAssets))
        {
            if (const auto* Image = Loader->FindImageByID(ImageID))
            {
                if (Image->Modifier == 10)
                {
                    Effect.Shadow = 1.0f;
                }
            }
        }

        return Effect;
    }

    void DrawSpriteWithEffect(Render::Draw2D& Draw,
                              const Resource::Graphics::GpuImage& Img,
                              float X,
                              float Y,
                              float W,
                              float H,
                              Render::Color Tint,
                              bool Flipped,
                              const SpriteVisualEffect& Effect,
                              const Game::Components::PlayerComponent* Player)
    {
        SpriteVisualEffect Effective = Effect;
        if (Img.ShadowSprite)
            Effective.Shadow = 1.0f;

        const bool UseWarpTexture =
            Effective.WarpTexture > 0.0f && Effective.EffectMask &&
            Effective.EffectMask->Valid();
        const bool UseTeamColor = !UseWarpTexture &&
            Effective.Shadow <= 0.0f && Player && Img.TeamColorMask.Valid();
        const float WarpScaleX = UseWarpTexture && Effective.EffectMask->Width > 0
            ? (float)Img.Width / (float)Effective.EffectMask->Width : 1.0f;
        const float WarpScaleY = UseWarpTexture && Effective.EffectMask->Height > 0
            ? (float)Img.Height / (float)Effective.EffectMask->Height : 1.0f;
        if (Effective.Any() || UseTeamColor)
        {
            Draw.DrawSpriteEffect(Img, X, Y, W, H,
                                  0.0f, 0.0f, 1.0f, 1.0f,
                                  Tint, Flipped,
                                  Effective.Hallucination,
                                  Effective.Cloaked,
                                  Effective.Brighten,
                                  Effective.Shadow,
                                  UseWarpTexture ? 1.0f : 0.0f,
                                  WarpScaleX,
                                  WarpScaleY,
                                  UseTeamColor ? Game::PlayerColorForID(Player->PlayerID) :
                                                 Render::Colors::White(),
                                  UseWarpTexture ? Effective.EffectMask :
                                      (UseTeamColor ? &Img.TeamColorMask : nullptr));
            return;
        }

        Draw.DrawSprite(Img, X, Y, W, H,
                        0.0f, 0.0f, 1.0f, 1.0f,
                        Tint, Flipped);
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

    float FrameHalfW = 0.0f;
    float FrameHalfH = 0.0f;
    ResolveFrameExtents(E, FrameHalfW, FrameHalfH);

    if (E.Kind == EntityKind::Unit)
    {
        const FootprintVisual F = UnitFootprint(E.X, E.Y, E.Size, FrameHalfW, FrameHalfH);
        DrawFootprintEllipseOutline(Draw, F.CenterX, F.CenterY, F.RadiusX, F.RadiusY);
        return;
    }

    if (E.Kind == EntityKind::Building)
    {
        const FootprintVisual F = BuildingFootprint(E.X, E.Y, E.ChkUnitId, FrameHalfW, FrameHalfH);
        DrawFootprintEllipseOutline(Draw, F.CenterX, F.CenterY, F.RadiusX, F.RadiusY);
        return;
    }

    const FootprintVisual F = FallbackFootprint(E.X, E.Y, std::max(32.0f, FrameHalfW * 2.0f));
    DrawFootprintEllipseOutline(Draw, F.CenterX, F.CenterY, F.RadiusX, F.RadiusY);
}

void EntityRenderer::DrawRallyIndicator(Render::Draw2D& Draw,
                                        Core::ECS::EntityManager& Entities,
                                        int BuildingEntityId) const
{
    auto* Cmd = Entities.GetComponent<Game::Components::CommandComponent>(BuildingEntityId);
    auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(BuildingEntityId);
    if (!Cmd || !Trans || !Cmd->HasRallyPoint || Cmd->RallyDisplayTimer <= 0.0f)
        return;

    const auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(BuildingEntityId);
    if (!Unit)
        return;

    Core::Math::Vec2 StartPos = Trans->Position;
    Core::Math::Vec2 TargetPos = Cmd->RallyPoint;

    if (Cmd->TargetEntityID >= 0 && Entities.IsAlive(Cmd->TargetEntityID))
    {
        if (auto* TargetTrans = Entities.GetComponent<Game::Components::TransformComponent>(Cmd->TargetEntityID))
        {
            TargetPos = TargetTrans->Position;
        }
    }

    const float DX = TargetPos.X - StartPos.X;
    const float DY = TargetPos.Y - StartPos.Y;
    const float Dist = std::sqrt(DX * DX + DY * DY);
    if (Dist < 8.0f)
        return;

    // Fade out over the final 0.5 seconds
    const float Fade = std::clamp(Cmd->RallyDisplayTimer / 0.5f, 0.0f, 1.0f);
    const uint8_t AlphaMain = static_cast<uint8_t>(255.0f * Fade);
    const uint8_t AlphaShadow = static_cast<uint8_t>(210.0f * Fade);

    // Green colors matching selection indicators
    const Render::Color Shadow(0, 0, 0, AlphaShadow);
    const Render::Color Green(70, 255, 86, AlphaMain);
    const Render::Color GreenBright(160, 255, 170, AlphaMain);

    auto DrawSegment = [&](float X1, float Y1, float X2, float Y2, Render::Color Color, float Thickness)
    {
        const float SegDX = X2 - X1;
        const float SegDY = Y2 - Y1;
        const float SegLen = std::sqrt(SegDX * SegDX + SegDY * SegDY);
        if (SegLen <= 0.001f) return;
        const int Steps = std::max(1, (int)std::ceil(SegLen / 2.0f));
        for (int I = 0; I <= Steps; ++I)
        {
            const float T = (float)I / (float)Steps;
            const float PX = X1 + SegDX * T;
            const float PY = Y1 + SegDY * T;
            Draw.DrawRect(PX - Thickness * 0.5f, PY - Thickness * 0.5f, Thickness, Thickness, Color);
        }
    };

    // Draw dashed line from building to rally point
    const Core::Math::Vec2 Dir{DX / Dist, DY / Dist};
    constexpr float DashLen = 7.0f;
    constexpr float GapLen = 5.0f;
    constexpr float Step = DashLen + GapLen;

    for (float D = 0.0f; D < Dist; D += Step)
    {
        const float SegLen = std::min(DashLen, Dist - D);
        const Core::Math::Vec2 P1 = StartPos + Dir * D;
        const Core::Math::Vec2 P2 = P1 + Dir * SegLen;
        DrawSegment(P1.X + 1.0f, P1.Y + 1.0f, P2.X + 1.0f, P2.Y + 1.0f, Shadow, 2.5f);
        DrawSegment(P1.X, P1.Y, P2.X, P2.Y, Green, 1.8f);
    }

    // Destination rally marker: bright green diamond reticle with center dot
    constexpr float R = 8.0f;
    // Shadow diamond
    DrawSegment(TargetPos.X - R + 1.0f, TargetPos.Y + 1.0f, TargetPos.X + 1.0f, TargetPos.Y - R + 1.0f, Shadow, 2.0f);
    DrawSegment(TargetPos.X + 1.0f, TargetPos.Y - R + 1.0f, TargetPos.X + R + 1.0f, TargetPos.Y + 1.0f, Shadow, 2.0f);
    DrawSegment(TargetPos.X + R + 1.0f, TargetPos.Y + 1.0f, TargetPos.X + 1.0f, TargetPos.Y + R + 1.0f, Shadow, 2.0f);
    DrawSegment(TargetPos.X + 1.0f, TargetPos.Y + R + 1.0f, TargetPos.X - R + 1.0f, TargetPos.Y + 1.0f, Shadow, 2.0f);

    // Bright green diamond
    DrawSegment(TargetPos.X - R, TargetPos.Y, TargetPos.X, TargetPos.Y - R, Green, 1.6f);
    DrawSegment(TargetPos.X, TargetPos.Y - R, TargetPos.X + R, TargetPos.Y, Green, 1.6f);
    DrawSegment(TargetPos.X + R, TargetPos.Y, TargetPos.X, TargetPos.Y + R, Green, 1.6f);
    DrawSegment(TargetPos.X, TargetPos.Y + R, TargetPos.X - R, TargetPos.Y, Green, 1.6f);

    // Center cross / dot
    Draw.DrawRect(TargetPos.X - 1.5f + 1.0f, TargetPos.Y - 1.5f + 1.0f, 3.0f, 3.0f, Shadow);
    Draw.DrawRect(TargetPos.X - 1.5f, TargetPos.Y - 1.5f, 3.0f, 3.0f, GreenBright);
    DrawSegment(TargetPos.X - 4.0f, TargetPos.Y, TargetPos.X + 4.0f, TargetPos.Y, GreenBright, 1.2f);
    DrawSegment(TargetPos.X, TargetPos.Y - 4.0f, TargetPos.X, TargetPos.Y + 4.0f, GreenBright, 1.2f);
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
        const Entity& EA = Entities.GetAll()[A];
        const Entity& EB = Entities.GetAll()[B];
        const int BandA = RenderSortBand(EA);
        const int BandB = RenderSortBand(EB);
        if (BandA != BandB)
            return BandA < BandB;
        return EA.Y < EB.Y;
    });

    // Render each visible entity
    for (int I : Order)
    {
        const Entity& E = Entities.GetAll()[I];
        bool DrewSprite = false;
        const bool IsSelected = SelectedLookup.find(E.Id) != SelectedLookup.end();
        const bool ShowHealthBar = E.Kind != EntityKind::Resource && E.MaxHitPoints > 0 && IsSelected;

        if (IsSelected)
            DrawSelectionMarker(E);

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
                    const FootprintVisual F = E.Kind == EntityKind::Building
                        ? BuildingFootprint(E.X, E.Y, E.ChkUnitId, HW, HH)
                        : UnitFootprint(E.X, E.Y, E.Size, HW, HH);
                    const float BarW = E.Kind == EntityKind::Building
                        ? std::max(42.0f, F.RadiusX * 1.10f)
                        : std::max(24.0f, F.RadiusX * 1.30f);
                    DrawSegmentedStatusBars(Draw, F.CenterX, StatusBarY(F), BarW,
                                            E.HitPoints, E.MaxHitPoints,
                                            0, 0, 0, 0);
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
                const FootprintVisual F = E.Kind == EntityKind::Building
                    ? BuildingFootprint(E.X, E.Y, E.ChkUnitId, Size * 0.5f, Size * 0.5f)
                    : FallbackFootprint(E.X, E.Y, Size);
                DrawSegmentedStatusBars(Draw, F.CenterX, StatusBarY(F),
                                        std::max(24.0f, F.RadiusX * 1.30f),
                                        E.HitPoints, E.MaxHitPoints,
                                        0, 0, 0, 0);
            }
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

    float VisualRadius = 0.0f;
    const auto* Sprite = FindUnitSpriteData(mDataAssets, Unit->ChkUnitID);
    if (!Sprite || SelectionCirclePixelSize((int)Sprite->SelectionCircle) <= 0)
        VisualRadius = ResolveVisualRadius(Entities, EntityId);

    const auto VisualPos = Trans->GetInterpolatedPosition(mInterpolationAlpha);
    DrawSelectionVisual(
        Draw,
        SelectionVisualForUnit(mDataAssets, mSpriteAssets, Unit->ChkUnitID,
                               VisualPos.X, VisualPos.Y,
                               IsBuilding, Unit->SizeType, VisualRadius));
}

float EntityRenderer::ResolveVisualRadius(Core::ECS::EntityManager& Entities, int EntityId) const
{
    if (!mSpriteAssets.HasCache())
        return 0.0f;

    auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
    if (!RenderComp)
        return 0.0f;

    float Radius = 0.0f;
    if (const auto* Frames = mSpriteAssets.GetDatImageFrames(RenderComp->ImageID);
        Frames && !Frames->empty())
    {
        const int FrameIndex = std::clamp(RenderComp->FrameIndex, 0, (int)Frames->size() - 1);
        const auto& Img = (*Frames)[FrameIndex];
        if (Img.Valid())
            AccumulateRectVisualRadius(0.0f, 0.0f,
                                       (float)Img.Width * 0.5f,
                                       (float)Img.Height * 0.5f,
                                       Radius);
    }

    if (RenderComp->SubunitImageID != UINT32_MAX)
    {
        if (const auto* Frames = mSpriteAssets.GetDatImageFrames(RenderComp->SubunitImageID);
            Frames && !Frames->empty())
        {
            const int FrameIndex = std::clamp(RenderComp->FrameIndex, 0, (int)Frames->size() - 1);
            const auto& Img = (*Frames)[FrameIndex];
            if (Img.Valid())
                AccumulateRectVisualRadius(0.0f, 0.0f,
                                           (float)Img.Width * 0.5f,
                                           (float)Img.Height * 0.5f,
                                           Radius);
        }
    }

    if (auto* Tree = Entities.GetComponent<Game::Components::ImageTreeComponent>(EntityId))
    {
        for (size_t NodeIndex = 0; NodeIndex < Tree->Nodes.size(); ++NodeIndex)
        {
            if (Tree->Nodes[NodeIndex].ParentIndex < 0)
            {
                AccumulateImageNodeVisualRadius(
                    *RenderComp, Tree->Nodes, (int)NodeIndex,
                    0.0f, 0.0f, 0, Radius);
            }
        }
    }

    return Radius;
}

void EntityRenderer::AccumulateImageNodeVisualRadius(
    const Game::Components::RenderComponent& RenderComp,
    const std::vector<Game::Components::ImageSubnode>& Nodes,
    int NodeIndex,
    float ParentOffsetX,
    float ParentOffsetY,
    int Depth,
    float& Radius) const
{
    if (!mSpriteAssets.HasCache() || NodeIndex < 0 || NodeIndex >= (int)Nodes.size() || Depth > 8)
        return;

    const auto& Node = Nodes[(size_t)NodeIndex];
    if (!Node.Visible)
        return;

    const float NodeOffsetX = ParentOffsetX + Node.Offset.X;
    const float NodeOffsetY = ParentOffsetY + Node.Offset.Y;
    const bool OffsetAlreadyMirrored =
        Node.UseSpecialOffset || Node.OverlayOffsetType >= 0;
    const float DrawOffsetX = OffsetAlreadyMirrored
        ? NodeOffsetX
        : (RenderComp.Flipped ? -NodeOffsetX : NodeOffsetX);

    const auto* Frames = Node.UseSpriteID ?
        mSpriteAssets.GetSpriteOverlayFrames(Node.ImageID) :
        mSpriteAssets.GetDatImageFrames(Node.ImageID);
    if (Frames && !Frames->empty())
    {
        const int FrameIndex = std::clamp(Node.FrameIndex, 0, (int)Frames->size() - 1);
        const auto& Img = (*Frames)[FrameIndex];
        if (Img.Valid())
        {
            AccumulateRectVisualRadius(DrawOffsetX, NodeOffsetY,
                                       (float)Img.Width * 0.5f,
                                       (float)Img.Height * 0.5f,
                                       Radius);
        }
    }

    for (size_t I = 0; I < Nodes.size(); ++I)
    {
        if (Nodes[I].ParentIndex == NodeIndex)
        {
            AccumulateImageNodeVisualRadius(
                RenderComp, Nodes, (int)I,
                NodeOffsetX, NodeOffsetY, Depth + 1, Radius);
        }
    }
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
    auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityId);
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

    const float RiftScale = ProtossConstructionRiftScale(Entities, EntityId);
    const float DrawWidth = (float)Img.Width * RiftScale;
    const float DrawHeight = (float)Img.Height * RiftScale;
    const float HW = DrawWidth * 0.5f;
    const float HH = DrawHeight * 0.5f;
    // Normal IScript offsets are root-sprite-space values and follow the root
    // mirror. LO offsets are resolved for the current flipped root frame by the
    // animation system, so applying that mirror twice moves flames/sparks to
    // the wrong side. Directional child VMs already expose an absolute flip;
    // non-directional overlays inherit the root flip here.
    const bool OffsetAlreadyMirrored =
        Node.UseSpecialOffset || Node.OverlayOffsetType >= 0;
    const float DrawOffsetX = OffsetAlreadyMirrored
        ? OffsetX
        : (RenderComp->Flipped ? -OffsetX : OffsetX);
    const bool Flipped = (Node.InheritSpriteDirection || Node.FollowsMainGraphic)
        ? Node.Flipped
        : (RenderComp->Flipped != Node.Flipped);
    const SpriteVisualEffect NodeEffect = ResolveSpriteVisualEffect(
        Entities, EntityId, mDataAssets, mSpriteAssets, Node.ImageID);
    const bool IsShadowImage = NodeEffect.Shadow > 0.0f;
    const auto* Building =
        Entities.GetComponent<Game::Components::BuildingComponent>(EntityId);
    const float RootVisualOffsetX = IsShadowImage ? 0.0f : RenderComp->VisualOffset.X;
    const float RootVisualOffsetY = IsShadowImage
        ? 0.0f
        : RenderComp->VisualOffset.Y + BuildingFlightBodyOffsetY(Building);
    const uint8_t Alpha = (uint8_t)std::lround(
        std::clamp(Node.Opacity, 0.0f, 1.0f) * 255.0f);
    const auto VisualPos = Trans->GetInterpolatedPosition(mInterpolationAlpha);
    DrawSpriteWithEffect(Render::g_Draw2D,
                         Img,
                         VisualPos.X + RootVisualOffsetX + DrawOffsetX * RiftScale - HW,
                         VisualPos.Y + RootVisualOffsetY + OffsetY * RiftScale - HH,
                         DrawWidth,
                         DrawHeight,
                         Render::Color(255, 255, 255, Alpha),
                         Flipped,
                         NodeEffect,
                         Player);
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
    auto IsEntitySelected = [&](int EntityId) -> bool {
        return std::find(SelectedIds.begin(), SelectedIds.end(), EntityId) != SelectedIds.end();
    };

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
        bool HasVisibleEffectChild = false;
        if (Entities.GetComponent<Game::Components::EffectComponent>(EntityId))
        {
            if (const auto* Tree =
                    Entities.GetComponent<Game::Components::ImageTreeComponent>(EntityId))
            {
                HasVisibleEffectChild = std::any_of(
                    Tree->Nodes.begin(), Tree->Nodes.end(),
                    [](const Game::Components::ImageSubnode& Node) {
                        return Node.Visible;
                    });
            }
        }
        if (!Trans || !RenderComp ||
            (!RenderComp->Visible && !HasVisibleEffectChild))
            return true;
        const auto VisualPos = Trans->GetInterpolatedPosition(mInterpolationAlpha);
        return VisualPos.X < CullMinX || VisualPos.X > CullMaxX ||
               VisualPos.Y < CullMinY || VisualPos.Y > CullMaxY;
    }), Order.end());

    struct RenderSortItem
    {
        int EntityId = 0;
        int Band = 0;
        int ZOrder = 0;
        float Y = 0.0f;
    };
    std::vector<RenderSortItem> SortedItems;
    SortedItems.reserve(Order.size());
    for (int EntityId : Order)
    {
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        const auto VisualPos = Trans ? Trans->GetInterpolatedPosition(mInterpolationAlpha) : Core::Math::Vec2{};
        SortedItems.push_back({
            EntityId,
            RenderSortBand(Entities, EntityId),
            RenderComp ? RenderComp->ZOrder : 0,
            VisualPos.Y
        });
    }

    std::stable_sort(SortedItems.begin(), SortedItems.end(), [](const RenderSortItem& A, const RenderSortItem& B) {
        if (A.Band != B.Band)
            return A.Band < B.Band;
        if (A.ZOrder != B.ZOrder)
            return A.ZOrder < B.ZOrder;
        return A.Y < B.Y;
    });

    auto& Draw = Render::g_Draw2D;
    if (mUseRenderQueue)
    {
        mRenderServer.BeginWorldFrame(mCameraX, mCameraY, mViewW, mViewH, mZoom, 128.0f);
        Draw.SetActiveQueue(&mRenderServer.GetWorldQueue());
    }
    static int DebugEntityFrames = 8;
    int DebugSpriteCount = 0;
    int DebugFallbackCount = 0;
    int DebugLoggedEntities = 0;
    if (DebugEntityRenderEnabled() && DebugEntityFrames > 0)
    {
        Core::Utils::Logger::Info(
            "EntityRenderer",
            "visible entity candidates=" + std::to_string(Order.size()) +
                " cam=" + std::to_string((int)mCameraX) + "," +
                std::to_string((int)mCameraY) +
                " zoom=" + std::to_string(mZoom));
    }
    std::unordered_map<int, SelectionVisual> SelectionVisualCache;
    SelectionVisualCache.reserve(SelectedIds.size());
    auto GetSelectionVisual =
        [&](int EntityId,
            const Game::Components::TransformComponent& Trans,
            const Game::Components::UnitComponent& Unit,
            bool IsBuilding) -> const SelectionVisual&
        {
            auto It = SelectionVisualCache.find(EntityId);
            if (It != SelectionVisualCache.end())
                return It->second;

            float VisualRadius = 0.0f;
            const auto* Sprite = FindUnitSpriteData(mDataAssets, Unit.ChkUnitID);
            if (!Sprite || SelectionCirclePixelSize((int)Sprite->SelectionCircle) <= 0)
                VisualRadius = ResolveVisualRadius(Entities, EntityId);

            const auto VisualPos = Trans.GetInterpolatedPosition(mInterpolationAlpha);
            auto Inserted = SelectionVisualCache.emplace(
                EntityId,
                SelectionVisualForUnit(mDataAssets, mSpriteAssets, Unit.ChkUnitID,
                                       VisualPos.X, VisualPos.Y,
                                       IsBuilding, Unit.SizeType, VisualRadius));
            return Inserted.first->second;
        };

    // Draw selected Pylon power fields as a ground overlay before any entity
    // sprites. A world-space circle projects to this 2:1 isometric ellipse,
    // while buildings and units remain fully in front of it.
    Draw.SetSortContext(Render::RenderLayer2D::GroundPowerField,
                        Render::RenderSubBand2D::RootSprite,
                        0u);
    for (int EntityId : SelectedIds)
    {
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
        if (Trans && RenderComp && RenderComp->Visible && Unit && Unit->ChkUnitID == 156)
        {
            const SelectionVisual& Visual =
                GetSelectionVisual(EntityId, *Trans, *Unit, true);
            DrawPylonPowerField(Draw,
                                Visual.Footprint.CenterX,
                                Visual.Footprint.CenterY);
        }
    }

    Draw.SetSortContext(Render::RenderLayer2D::SelectionOverlay,
                        Render::RenderSubBand2D::RootSprite,
                        0u);
    for (const auto& Item : SortedItems)
    {
        if (!IsEntitySelected(Item.EntityId))
            continue;
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(Item.EntityId);
        auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(Item.EntityId);
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(Item.EntityId);
        if (!Trans || !RenderComp || !RenderComp->Visible || !Unit)
            continue;
        const bool IsBuilding =
            Entities.GetComponent<Game::Components::BuildingComponent>(Item.EntityId) != nullptr;
        DrawSelectionVisual(Draw, GetSelectionVisual(Item.EntityId, *Trans, *Unit, IsBuilding));
    }

    Draw.SetSortContext(Render::RenderLayer2D::RallyOverlay,
                        Render::RenderSubBand2D::RootSprite,
                        0u);
    for (int EntityId : SelectedIds)
    {
        if (Entities.IsAlive(EntityId))
            DrawRallyIndicator(Draw, Entities, EntityId);
    }

    for (const auto& Item : SortedItems)
    {
        const int EntityId = Item.EntityId;
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
        auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityId);
        if (!Trans || !RenderComp || !Unit)
            continue;

        bool DrewSprite = false;
        const bool IsResource = Entities.GetComponent<Game::Components::ResourceComponent>(EntityId) != nullptr;
        const bool IsBuilding = Entities.GetComponent<Game::Components::BuildingComponent>(EntityId) != nullptr;
        const auto* Building =
            Entities.GetComponent<Game::Components::BuildingComponent>(EntityId);
        const float FlightBodyOffsetY = BuildingFlightBodyOffsetY(Building);
        const SpriteVisualEffect VisualEffect = ResolveSpriteVisualEffect(
            Entities, EntityId, mDataAssets, mSpriteAssets, RenderComp->ImageID);
        const Render::RenderLayer2D EntityLayer = MapBandToRenderLayer(Item.Band);
        const uint16_t DepthKey = Render::SortKey2D::EncodeDepthKey(Item.Y, Item.ZOrder);

        Draw.SetSortContext(EntityLayer, Render::RenderSubBand2D::UnderNode, DepthKey);
        if (auto* Tree = Entities.GetComponent<Game::Components::ImageTreeComponent>(EntityId))
        {
            for (size_t NodeIndex = 0; NodeIndex < Tree->Nodes.size(); ++NodeIndex)
                if (Tree->Nodes[NodeIndex].ParentIndex < 0 && !Tree->Nodes[NodeIndex].DrawAbove)
                    DrawImageNodeTree(Entities, EntityId, Tree->Nodes, (int)NodeIndex);
        }
        Draw.SetSortContext(EntityLayer,
                            (VisualEffect.Shadow > 0.0f)
                                ? Render::RenderSubBand2D::Shadow
                                : Render::RenderSubBand2D::RootSprite,
                            DepthKey);

        const auto* Frames = mSpriteAssets.HasCache() ?
            mSpriteAssets.GetDatImageFrames(RenderComp->ImageID) :
            nullptr;
        if (RenderComp->Visible && Frames && !Frames->empty())
        {
            if (RenderComp->TotalFrames <= 1)
                RenderComp->TotalFrames = (int)Frames->size();
            const int FrameIndex = std::clamp(RenderComp->FrameIndex, 0, (int)Frames->size() - 1);
            const auto& Img = (*Frames)[FrameIndex];
            if (DebugEntityRenderEnabled() && DebugEntityFrames > 0 && DebugLoggedEntities < 6)
            {
                Core::Utils::Logger::Info(
                    "EntityRenderer",
                    "entity id=" + std::to_string(EntityId) +
                        " chk=" + std::to_string(Unit->ChkUnitID) +
                        " image=" + std::to_string(RenderComp->ImageID) +
                        " frame=" + std::to_string(FrameIndex) +
                        " frames=" + std::to_string(Frames->size()) +
                        " valid=" + std::to_string(Img.Valid() ? 1 : 0) +
                        " visible=" + std::to_string(Img.HasVisiblePixels ? 1 : 0) +
                        " size=" + std::to_string(Img.Width) + "x" +
                        std::to_string(Img.Height) +
                        " pos=" + std::to_string((int)Trans->Position.X) +
                        "," + std::to_string((int)Trans->Position.Y));
                ++DebugLoggedEntities;
            }
            if (Img.Valid())
            {
                const float RiftScale =
                    ProtossConstructionRiftScale(Entities, EntityId);
                const float DrawWidth = (float)Img.Width * RiftScale;
                const float DrawHeight = (float)Img.Height * RiftScale;
                const float HW = DrawWidth * 0.5f;
                const float HH = DrawHeight * 0.5f;
                const auto VisualPos = Trans->GetInterpolatedPosition(mInterpolationAlpha);
                DrawSpriteWithEffect(Draw,
                                     Img,
                                     VisualPos.X + RenderComp->VisualOffset.X - HW,
                                     VisualPos.Y + RenderComp->VisualOffset.Y + FlightBodyOffsetY - HH,
                                     DrawWidth,
                                     DrawHeight,
                                     RenderComp->Tint,
                                     RenderComp->Flipped,
                                     VisualEffect,
                                     Player);
                DrewSprite = true;
                ++DebugSpriteCount;
            }
        }

        Draw.SetSortContext(EntityLayer, Render::RenderSubBand2D::OverNode, DepthKey);
        if (auto* Tree = Entities.GetComponent<Game::Components::ImageTreeComponent>(EntityId))
        {
            for (size_t NodeIndex = 0; NodeIndex < Tree->Nodes.size(); ++NodeIndex)
                if (Tree->Nodes[NodeIndex].ParentIndex < 0 && Tree->Nodes[NodeIndex].DrawAbove)
                    DrawImageNodeTree(Entities, EntityId, Tree->Nodes, (int)NodeIndex);
        }
        Draw.SetSortContext(EntityLayer, Render::RenderSubBand2D::RootSprite, DepthKey);

        if (!DrewSprite && RenderComp->Visible)
        {
            ++DebugFallbackCount;
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
            const float Y = Trans->Position.Y + FlightBodyOffsetY - Size * 0.5f;
            Draw.DrawRect(X, Y, Size, Size, Base);
            Draw.DrawRectLines(X, Y, Size, Size, Render::Color(0, 0, 0, 255), 2.0f);
        }

        if (DebugEntityMarkersEnabled())
        {
            Render::Color Marker(255, 0, 255, 255);
            if (IsResource)
                Marker = Render::Color(0, 255, 255, 255);
            else if (IsBuilding)
                Marker = Render::Color(255, 230, 0, 255);
            Draw.DrawRect(Trans->Position.X - 6.0f,
                          Trans->Position.Y - 6.0f,
                          12.0f,
                          12.0f,
                          Marker);
            Draw.DrawRectLines(Trans->Position.X - 9.0f,
                               Trans->Position.Y - 9.0f,
                               18.0f,
                               18.0f,
                               Render::Color(0, 0, 0, 255),
                               2.0f);
        }
    }

    auto Bullets = Entities.GetEntitiesWith<Game::Components::TransformComponent,
                                            Game::Components::RenderComponent,
                                            Game::Components::BulletComponent>();
    std::stable_sort(Bullets.begin(), Bullets.end(), [&](int A, int B) {
        const auto* TA = Entities.GetComponent<Game::Components::TransformComponent>(A);
        const auto* TB = Entities.GetComponent<Game::Components::TransformComponent>(B);
        return TA && TB ? TA->Position.Y < TB->Position.Y : A < B;
    });
    for (int EntityId : Bullets)
    {
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        const auto VisualPos = Trans ? Trans->GetInterpolatedPosition(mInterpolationAlpha) : Core::Math::Vec2{};
        if (!Trans || !RenderComp || !RenderComp->Visible ||
            VisualPos.X < CullMinX || VisualPos.X > CullMaxX ||
            VisualPos.Y < CullMinY || VisualPos.Y > CullMaxY)
        {
            continue;
        }

        const uint16_t BulletDepth =
            Render::SortKey2D::EncodeDepthKey(VisualPos.Y, RenderComp->ZOrder);
        Draw.SetSortContext(Render::RenderLayer2D::Projectiles,
                            Render::RenderSubBand2D::UnderNode,
                            BulletDepth);
        if (auto* Tree = Entities.GetComponent<Game::Components::ImageTreeComponent>(EntityId))
        {
            for (size_t NodeIndex = 0; NodeIndex < Tree->Nodes.size(); ++NodeIndex)
                if (Tree->Nodes[NodeIndex].ParentIndex < 0 && !Tree->Nodes[NodeIndex].DrawAbove)
                    DrawImageNodeTree(Entities, EntityId, Tree->Nodes, (int)NodeIndex);
        }

        Draw.SetSortContext(Render::RenderLayer2D::Projectiles,
                            Render::RenderSubBand2D::RootSprite,
                            BulletDepth);
        const auto* Frames = mSpriteAssets.GetDatImageFrames(RenderComp->ImageID);
        if (Frames && !Frames->empty())
        {
            RenderComp->TotalFrames = std::max(RenderComp->TotalFrames, (int)Frames->size());
            const int FrameIndex = std::clamp(RenderComp->FrameIndex, 0, (int)Frames->size() - 1);
            const auto& Img = (*Frames)[FrameIndex];
            if (Img.Valid())
            {
                const float HW = (float)Img.Width * 0.5f;
                const float HH = (float)Img.Height * 0.5f;
                DrawSpriteWithEffect(
                    Draw, Img,
                    VisualPos.X - HW, VisualPos.Y - HH,
                    (float)Img.Width, (float)Img.Height,
                    RenderComp->Tint, RenderComp->Flipped,
                    ResolveSpriteVisualEffect(
                        Entities, EntityId, mDataAssets, mSpriteAssets,
                        RenderComp->ImageID),
                    Entities.GetComponent<Game::Components::PlayerComponent>(EntityId));
                ++DebugSpriteCount;
            }
        }

        Draw.SetSortContext(Render::RenderLayer2D::Projectiles,
                            Render::RenderSubBand2D::OverNode,
                            BulletDepth);
        if (auto* Tree = Entities.GetComponent<Game::Components::ImageTreeComponent>(EntityId))
        {
            for (size_t NodeIndex = 0; NodeIndex < Tree->Nodes.size(); ++NodeIndex)
                if (Tree->Nodes[NodeIndex].ParentIndex < 0 && Tree->Nodes[NodeIndex].DrawAbove)
                    DrawImageNodeTree(Entities, EntityId, Tree->Nodes, (int)NodeIndex);
        }
    }

    for (const auto& Item : SortedItems)
    {
        if (!IsEntitySelected(Item.EntityId))
            continue;
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(Item.EntityId);
        auto* RenderComp = Entities.GetComponent<Game::Components::RenderComponent>(Item.EntityId);
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(Item.EntityId);
        if (!Trans || !RenderComp || !RenderComp->Visible || !Unit || Unit->MaxHitPoints <= 0)
            continue;
        if (Entities.GetComponent<Game::Components::ResourceComponent>(Item.EntityId))
            continue;
        const bool IsBuilding =
            Entities.GetComponent<Game::Components::BuildingComponent>(Item.EntityId) != nullptr;
        const FootprintVisual& F =
            GetSelectionVisual(Item.EntityId, *Trans, *Unit, IsBuilding).Footprint;
        DrawSegmentedStatusBars(
            Draw, F.CenterX, StatusBarY(F),
            StatusBarWidthFromSpriteData(mDataAssets, Unit->ChkUnitID, F, IsBuilding),
            Unit->HitPoints, Unit->MaxHitPoints,
            Unit->ShieldPoints, Unit->MaxShieldPoints,
            Unit->EnergyPoints, Unit->MaxEnergyPoints);
    }

    if (mUseRenderQueue)
    {
        Draw.SetActiveQueue(nullptr);
        mRenderServer.FlushWorldFrame(Draw);
    }

    if (DebugEntityRenderEnabled() && DebugEntityFrames > 0)
    {
        --DebugEntityFrames;
        Core::Utils::Logger::Info(
            "EntityRenderer",
            "draw summary sprites=" + std::to_string(DebugSpriteCount) +
                " fallback=" + std::to_string(DebugFallbackCount));
    }
}

} // namespace Game::Client
