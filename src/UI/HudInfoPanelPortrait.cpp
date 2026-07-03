#include "UI/Hud.h"

#include <algorithm>
#include <array>
#include <vector>

#include "Core/Platform/Platform.h"
#include "Game/Components.h"
#include "Render/Draw2D.h"
#include "UI/HudLayout.h"
#include "UI/HudPortrait.h"
#include "UI/HudRenderPrimitives.h"

namespace Game::Client::UI
{

const Resource::Graphics::GpuImage* Hud::GetAnimatedSelectionFallbackImage(Core::ECS::EntityManager& Entities) const
{
    const std::vector<Resource::Graphics::GpuImage>* SelectedFrames = nullptr;
    int SelectedFrameIndex = 0;
    if (mSpriteAssets.HasCache() && mState.Selection.SelectedEntityId > 0 && Entities.IsAlive(mState.Selection.SelectedEntityId))
    {
        if (auto* Render = Entities.GetComponent<Game::Components::RenderComponent>(mState.Selection.SelectedEntityId))
        {
            SelectedFrames = mSpriteAssets.GetSpriteFrames(Render->ImageID);
            SelectedFrameIndex = Render->FrameIndex;
        }
    }
    if (SelectedFrames && !SelectedFrames->empty())
    {
        const auto& Frames = *SelectedFrames;
        const int CurrentIndex = std::clamp(SelectedFrameIndex, 0, (int)Frames.size() - 1);
        if (CurrentIndex >= 0 && CurrentIndex < (int)Frames.size() && Frames[(size_t)CurrentIndex].Valid() &&
            CurrentIndex != 0)
        {
            return &Frames[(size_t)CurrentIndex];
        }

        std::array<int, 6> ValidFrames = {{ -1, -1, -1, -1, -1, -1 }};
        size_t ValidCount = 0;
        for (size_t Index = 0; Index < Frames.size() && ValidCount < ValidFrames.size(); ++Index)
        {
            if (Frames[Index].Valid())
                ValidFrames[ValidCount++] = (int)Index;
        }
        if (ValidCount > 0)
        {
            const size_t AnimIndex =
                (size_t)(Core::Platform::GetAbsoluteTime() * 6.0) % ValidCount;
            return &Frames[(size_t)ValidFrames[AnimIndex]];
        }
    }
    return GetFirstValidSpriteFrame(mState.InfoPanel.PreviewChkUnitId);
}

void Hud::DrawInfoPanelPortraits(Core::ECS::EntityManager& Entities,
                                 const HudLayout& Layout,
                                 bool HasModularConsole)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    const RectI& PreviewRect = Layout.PreviewRect;
    const RectI& AuxiliaryRect = Layout.AuxiliaryRect;
    const RectI AuxiliaryInnerRect = HasModularConsole
        ? InsetRect(AuxiliaryRect, std::max(14, AuxiliaryRect.W / 12), std::max(18, AuxiliaryRect.H / 11))
        : AuxiliaryRect;
    const RectI PortraitPanel = InsetRect(AuxiliaryInnerRect,
                                          std::max(6, AuxiliaryInnerRect.W / 14),
                                          std::max(8, AuxiliaryInnerRect.H / 14));
    const RectI PreviewPanel = InsetRect(PreviewRect,
                                         std::max(6, PreviewRect.W / 10),
                                         std::max(6, PreviewRect.H / 10));

    const auto* WireframePreviewImage = GetSelectionPreviewImage();
    const auto* WireframeFrames = UIAssets.GetWireframeFrames();
    const bool PreviewUsesWireframe =
        WireframeFrames &&
        mState.InfoPanel.PreviewChkUnitId >= 0 &&
        mState.InfoPanel.PreviewChkUnitId < (int)WireframeFrames->size() &&
        WireframePreviewImage == &(*WireframeFrames)[(size_t)mState.InfoPanel.PreviewChkUnitId];
    const auto* WireframeBounds =
        (PreviewUsesWireframe && UIAssets.HasCache() && mState.InfoPanel.PreviewChkUnitId >= 0)
        ? UIAssets.GetWireframeOpaqueBounds(mState.InfoPanel.PreviewChkUnitId)
        : nullptr;
    const auto* PortraitImage = ResolvePortraitFrameForSelection(
        UIAssets,
        mState.Selection.SelectedTerranDefId,
        GetAnimatedSelectionFallbackImage(Entities));

    D.DrawRect((float)PreviewPanel.X, (float)PreviewPanel.Y,
               (float)PreviewPanel.W, (float)PreviewPanel.H,
               Render::Color(10, 10, 14, 208));
    D.DrawRectLines((float)PreviewPanel.X, (float)PreviewPanel.Y,
                    (float)PreviewPanel.W, (float)PreviewPanel.H,
                    Render::Color(92, 112, 128, 150), 1.0f);
    D.DrawRect((float)PortraitPanel.X, (float)PortraitPanel.Y,
               (float)PortraitPanel.W, (float)PortraitPanel.H,
               Render::Color(10, 10, 14, 205));
    D.DrawRectLines((float)PortraitPanel.X, (float)PortraitPanel.Y,
                    (float)PortraitPanel.W, (float)PortraitPanel.H,
                    Render::Color(95, 150, 90, 160), 1.0f);

    const RectI PreviewImageRect =
    {
        PreviewPanel.X + 6,
        PreviewPanel.Y + 6,
        std::max(8, PreviewPanel.W - 12),
        std::max(8, PreviewPanel.H - 12)
    };
    if (WireframePreviewImage && WireframePreviewImage->Valid())
    {
        if (WireframeBounds && WireframeBounds->Valid())
        {
            DrawContainedAtlas(*WireframePreviewImage,
                               { WireframeBounds->X, WireframeBounds->Y, WireframeBounds->W, WireframeBounds->H },
                               PreviewImageRect,
                               Render::Colors::White());
        }
        else
        {
            DrawContainedImage(*WireframePreviewImage, PreviewImageRect, Render::Colors::White());
        }
    }

    if (PortraitImage && PortraitImage->Valid())
    {
        DrawContainedImage(*PortraitImage,
                           { PortraitPanel.X + 6,
                             PortraitPanel.Y + 6,
                             std::max(8, PortraitPanel.W - 12),
                             std::max(8, PortraitPanel.H - 12) },
                           Render::Colors::White());
    }
}

} // namespace Game::Client::UI
