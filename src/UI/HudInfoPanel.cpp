#include "UI/Hud.h"

#include "UI/HudText.h"

namespace Game::Client::UI
{

void Hud::DrawInfoPanel(Core::ECS::EntityManager& Entities,
                        const HudLayout& Layout,
                        bool HasModularConsole,
                        const Resource::Graphics::GpuImage& AggIcons)
{
    if (!mState.InfoPanel.HasSelection)
        return;

    const bool ScvPanel =
        mState.InfoPanel.SelectedCount <= 1 &&
        mState.Selection.SelectedTerranDefId == Game::Races::GetUnitDefID("SCV") &&
        !mState.InfoPanel.IsBuilding &&
        !mState.InfoPanel.IsResource;

    DrawInfoPanelFrame(Layout, HasModularConsole);
    DrawInfoPanelPortraits(Entities, Layout, HasModularConsole);

    if (ScvPanel)
    {
        DrawScvInfoPanel(Layout, HasModularConsole, AggIcons);
    }
    else
    {
        DrawStandardInfoPanel(Layout, HasModularConsole, AggIcons);
    }
}

} // namespace Game::Client::UI








// === Consolidated from src/UI/HudInfoPanelFrame.cpp ===
#include "UI/Hud.h"

#include <algorithm>

#include "Render/Draw2D.h"
#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"

namespace Game::Client::UI
{

void Hud::DrawInfoPanelFrame(const HudLayout& Layout, bool HasModularConsole)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    const auto HudTwire = UIAssets.GetHudTwireStrip();
    const auto HudTunit = UIAssets.GetHudTunitStrip();

    const RectI& InfoRect = Layout.InfoRect;
    const RectI& AuxiliaryRect = Layout.AuxiliaryRect;
    const RectI InfoInnerRect = HasModularConsole
        ? InsetRect(InfoRect, std::max(18, InfoRect.W / 34), std::max(16, InfoRect.H / 10))
        : InfoRect;
    const RectI AuxiliaryInnerRect = HasModularConsole
        ? InsetRect(AuxiliaryRect, std::max(14, AuxiliaryRect.W / 12), std::max(18, AuxiliaryRect.H / 11))
        : AuxiliaryRect;

    if (!HasModularConsole)
    {
        if (HudTunit.Valid())
        {
            D.DrawSpriteFromImage(HudTunit,
                                  (float)InfoRect.X,
                                  (float)InfoRect.Y,
                                  (float)InfoRect.W,
                                  (float)InfoRect.H,
                                  Render::Color(255, 255, 255, 150));
        }
        D.DrawRect((float)InfoInnerRect.X,
                   (float)InfoInnerRect.Y,
                   (float)InfoInnerRect.W,
                   (float)InfoInnerRect.H,
                   Render::Color(5, 5, 10, 90));
        D.DrawRectLines((float)InfoInnerRect.X,
                        (float)InfoInnerRect.Y,
                        (float)InfoInnerRect.W,
                        (float)InfoInnerRect.H,
                        Render::Color(120, 90, 60, 130),
                        1.0f);

        if (HudTwire.Valid())
        {
            D.DrawSpriteFromImage(HudTwire,
                                  (float)AuxiliaryRect.X,
                                  (float)AuxiliaryRect.Y,
                                  (float)AuxiliaryRect.W,
                                  (float)AuxiliaryRect.H,
                                  Render::Color(255, 255, 255, 150));
        }
        D.DrawRect((float)AuxiliaryInnerRect.X,
                   (float)AuxiliaryInnerRect.Y,
                   (float)AuxiliaryInnerRect.W,
                   (float)AuxiliaryInnerRect.H,
                   Render::Color(6, 6, 12, 100));
        D.DrawRectLines((float)AuxiliaryInnerRect.X,
                        (float)AuxiliaryInnerRect.Y,
                        (float)AuxiliaryInnerRect.W,
                        (float)AuxiliaryInnerRect.H,
                        Render::Color(120, 90, 60, 120),
                        1.0f);
    }
}

} // namespace Game::Client::UI


// === Consolidated from src/UI/HudInfoPanelPortrait.cpp ===
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
            SelectedFrames = mSpriteAssets.GetDatImageFrames(Render->ImageID);
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
    const auto* PortraitImage = ResolvePortraitFrameForChkUnitId(
        UIAssets,
        mState.InfoPanel.PreviewChkUnitId,
        GetAnimatedSelectionFallbackImage(Entities));

    const bool InMultiSelect =
        mState.InfoPanel.SelectedCount > 1 && !mState.InfoPanel.MultiSelectSlots.empty();

    if (!HasModularConsole)
    {
        if (!InMultiSelect)
        {
            D.DrawRect((float)PreviewPanel.X, (float)PreviewPanel.Y,
                       (float)PreviewPanel.W, (float)PreviewPanel.H,
                       Render::Color(10, 10, 14, 208));
            D.DrawRectLines((float)PreviewPanel.X, (float)PreviewPanel.Y,
                            (float)PreviewPanel.W, (float)PreviewPanel.H,
                            Render::Color(92, 112, 128, 150), 1.0f);
        }
        D.DrawRect((float)PortraitPanel.X, (float)PortraitPanel.Y,
                   (float)PortraitPanel.W, (float)PortraitPanel.H,
                   Render::Color(10, 10, 14, 205));
        D.DrawRectLines((float)PortraitPanel.X, (float)PortraitPanel.Y,
                        (float)PortraitPanel.W, (float)PortraitPanel.H,
                        Render::Color(95, 150, 90, 160), 1.0f);
    }

    if (!InMultiSelect)
    {
        const bool HasHealth = mState.InfoPanel.MaxValue > 0;
        const int BottomReserve = HasHealth ? 16 : 0;
        const RectI PreviewImageRect =
        {
            PreviewPanel.X + 6,
            PreviewPanel.Y + 4,
            std::max(8, PreviewPanel.W - 12),
            std::max(8, PreviewPanel.H - 8 - BottomReserve)
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

        if (HasHealth)
        {
            char Buf[64];
            const float HpPct = (float)mState.InfoPanel.CurrentValue / (float)mState.InfoPanel.MaxValue;
            const Render::Color HpCol = HpPct > 0.66f ? Render::Color(80, 220, 95, 255)
                                      : HpPct > 0.33f ? Render::Color(220, 210, 60, 255)
                                      : Render::Color(240, 70, 70, 255);

            if (mState.InfoPanel.ShieldMax > 0)
            {
                std::snprintf(Buf, sizeof(Buf), "%d/%d %d/%d",
                              mState.InfoPanel.ShieldValue, mState.InfoPanel.ShieldMax,
                              mState.InfoPanel.CurrentValue, mState.InfoPanel.MaxValue);
                const float CharW = 6.0f;
                const float TextW = (float)std::strlen(Buf) * CharW;
                const float TextX = (float)PreviewPanel.X + ((float)PreviewPanel.W - TextW) * 0.5f;
                const float TextY = (float)(PreviewPanel.Y + PreviewPanel.H - 13);
                D.DrawText(Buf, std::max((float)PreviewPanel.X + 2.0f, TextX), TextY, 0.82f, Render::Color(70, 190, 255, 255));
            }
            else
            {
                std::snprintf(Buf, sizeof(Buf), "%d/%d",
                              mState.InfoPanel.CurrentValue, mState.InfoPanel.MaxValue);
                const float CharW = 7.0f;
                const float TextW = (float)std::strlen(Buf) * CharW;
                const float TextX = (float)PreviewPanel.X + ((float)PreviewPanel.W - TextW) * 0.5f;
                const float TextY = (float)(PreviewPanel.Y + PreviewPanel.H - 13);
                D.DrawText(Buf, std::max((float)PreviewPanel.X + 2.0f, TextX), TextY, 0.88f, HpCol);
            }
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


// === Consolidated from src/UI/HudInfoPanelScv.cpp ===
#include "UI/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "Render/Draw2D.h"
#include "UI/HudButtonStyle.h"
#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"
#include "UI/HudInfoWidgets.h"
#include "UI/HudText.h"

namespace Game::Client::UI
{

namespace
{
    constexpr int IconArmor = 5;
    constexpr AtlasRect AggIconMinerals = { 0, 0, 16, 16 };
    constexpr AtlasRect AggIconGas = { 17, 0, 16, 16 };
}

void Hud::DrawScvInfoPanel(const HudLayout& Layout,
                           bool HasModularConsole,
                           const Resource::Graphics::GpuImage& AggIcons)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    const RectI& InfoRect = Layout.InfoRect;
    const RectI& PreviewRect = Layout.PreviewRect;
    const RectI InfoInnerRect = HasModularConsole
        ? InsetRect(InfoRect, std::max(18, InfoRect.W / 34), std::max(16, InfoRect.H / 10))
        : InfoRect;
    const auto HpEmptyStrip = UIAssets.GetHudHpBarEmptyStrip();
    const auto HpStrip = UIAssets.GetHudHpBarStrip();
    const int ContentGap = std::max(10, InfoInnerRect.W / 52);
    const int HeaderX = PreviewRect.X + PreviewRect.W + ContentGap;
    const int HeaderW = std::max(120, InfoInnerRect.X + InfoInnerRect.W - HeaderX - 10);
    const RectI HeaderRect =
    {
        HeaderX,
        PreviewRect.Y + 2,
        HeaderW,
        std::max(56, PreviewRect.H - 4)
    };
    const int FooterH = 24;
    const int FooterGap = 8;
    const int FooterY = InfoInnerRect.Y + InfoInnerRect.H - FooterH - 6;
    const RectI FooterCardA =
    {
        HeaderX,
        FooterY,
        std::max(56, (HeaderW - FooterGap) / 2),
        FooterH
    };
    const RectI FooterCardB =
    {
        FooterCardA.X + FooterCardA.W + FooterGap,
        FooterY,
        std::max(56, HeaderW - FooterCardA.W - FooterGap),
        FooterH
    };
    const size_t NameChars = std::max<size_t>(10, (size_t)std::floor((float)HeaderRect.W / 9.0f));
    const size_t DetailChars = std::max<size_t>(12, (size_t)std::floor((float)HeaderRect.W / 7.5f));
    const auto* MineralHdIcon = GetLegacyIcon(0);
    const auto* GasHdIcon = GetLegacyIcon(2);
    const auto* ArmorIcon = GetLegacyIcon(IconArmor);
    const auto* HarvestMask = GetCommandIconMask(CommandIconForCommand(HudCommandId::Harvest));
    const auto* MoveMask = GetCommandIconMask(CommandIconForCommand(HudCommandId::Move));
    char Buf[64];

    D.DrawText(Ellipsize(mState.InfoPanel.Name, NameChars),
               (float)HeaderRect.X, (float)HeaderRect.Y + 2.0f, 1.25f,
               Render::Color(230, 230, 235, 255));
    D.DrawText(Ellipsize(mState.InfoPanel.RoleLine.empty() ? "Private" : mState.InfoPanel.RoleLine, DetailChars),
               (float)HeaderRect.X, (float)HeaderRect.Y + 18.0f, 0.95f,
               Render::Color(210, 210, 220, 255));
    std::snprintf(Buf, sizeof(Buf), "Kills: %d", mState.InfoPanel.KillCount);
    D.DrawText(Buf, (float)HeaderRect.X, (float)HeaderRect.Y + 32.0f, 0.95f,
               Render::Color(220, 220, 225, 255));
    if (!mState.InfoPanel.StatusLine.empty())
    {
        D.DrawText(Ellipsize(mState.InfoPanel.StatusLine, DetailChars),
                   (float)HeaderRect.X + 80.0f, (float)HeaderRect.Y + 32.0f, 0.95f,
                   Render::Color(235, 220, 150, 255));
    }

    if (mState.InfoPanel.CarryingAmount > 0)
    {
        const bool CarryingGas = mState.InfoPanel.CargoLine.find("Gas") != std::string::npos;
        const auto* CargoHdIcon = CarryingGas ? GasHdIcon : MineralHdIcon;
        const RectI CargoIconRect = { HeaderRect.X + HeaderRect.W - 24, HeaderRect.Y + 2, 16, 16 };
        if (CargoHdIcon && CargoHdIcon->Valid())
            DrawContainedImage(*CargoHdIcon, CargoIconRect, Render::Colors::White());
        std::snprintf(Buf, sizeof(Buf), "%d", mState.InfoPanel.CarryingAmount);
        D.DrawText(Buf, (float)CargoIconRect.X - 24.0f, (float)CargoIconRect.Y + 2.0f, 0.95f,
                   CarryingGas ? Render::Color(84, 208, 168, 255) : Render::Color(96, 180, 255, 255));
    }

    int StatX = HeaderRect.X;
    StatX = DrawInfoMiniStat(StatX, HeaderRect, ArmorIcon, nullptr, std::to_string(mState.InfoPanel.Armor), Render::Color(215, 215, 215, 255));
    StatX = DrawInfoMiniStat(StatX, HeaderRect, nullptr, HarvestMask, std::to_string(mState.InfoPanel.CarryingAmount), Render::Color(228, 214, 96, 255));
    DrawInfoMiniStat(StatX, HeaderRect, nullptr, MoveMask, std::to_string(mState.InfoPanel.SpeedValue), Render::Color(214, 224, 238, 255));

    int BarY = HeaderRect.Y + 48;
    const RectI ScvStatBodyRect =
    {
        HeaderX,
        BarY,
        HeaderW,
        std::max(20, InfoInnerRect.Y + InfoInnerRect.H - BarY)
    };
    const int MaxBottomY = InfoInnerRect.Y + InfoInnerRect.H;
    DrawInfoValueBar(BarY, "HP", mState.InfoPanel.CurrentValue, mState.InfoPanel.MaxValue,
                     ScvStatBodyRect, MaxBottomY, HpEmptyStrip, HpStrip, Render::Color(80, 200, 95, 255));

    if (!mState.InfoPanel.CargoLine.empty())
    {
        const bool CarryingGas = mState.InfoPanel.CargoLine.find("Gas") != std::string::npos;
        const AtlasRect* CargoIcon = CarryingGas ? &AggIconGas : &AggIconMinerals;
        DrawInfoFooterCard(FooterCardA,
                           mState.InfoPanel.CargoLine,
                           Render::Color(170, 220, 210, 255),
                           AggIcons,
                           CargoIcon,
                           nullptr);
    }
    if (!mState.InfoPanel.DetailLine.empty() && mState.InfoPanel.DetailLine != mState.InfoPanel.StatusLine)
    {
        DrawInfoFooterCard(FooterCardB, mState.InfoPanel.DetailLine, Render::Color(200, 200, 185, 255),
                           AggIcons, nullptr, HarvestMask);
    }
}

} // namespace Game::Client::UI


// === Consolidated from src/UI/HudInfoPanelStandard.cpp ===
#include "UI/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "Core/Platform/Platform.h"
#include "Render/Draw2D.h"
#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"
#include "UI/HudInfoWidgets.h"
#include "UI/HudText.h"

namespace Game::Client::UI
{


void Hud::DrawStandardInfoPanel(const HudLayout& Layout,
                                bool HasModularConsole,
                                const Resource::Graphics::GpuImage& AggIcons)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    const RectI& InfoRect = Layout.InfoRect;
    const RectI& PreviewRect = Layout.PreviewRect;
    const RectI InfoInnerRect = HasModularConsole
        ? InsetRect(InfoRect, std::max(18, InfoRect.W / 34), std::max(16, InfoRect.H / 10))
        : InfoRect;
    const auto HpEmptyStrip = UIAssets.GetHudHpBarEmptyStrip();
    const auto HpStrip = UIAssets.GetHudHpBarStrip();
    const int ContentGap = std::max(10, InfoInnerRect.W / 52);
    const int HeaderX = PreviewRect.X + PreviewRect.W + ContentGap;
    const int HeaderW = std::max(120, InfoInnerRect.X + InfoInnerRect.W - HeaderX - 10);
    const RectI HeaderRect =
    {
        HeaderX,
        PreviewRect.Y + 2,
        HeaderW,
        std::max(56, PreviewRect.H - 4)
    };
    const int FooterH = 24;
    const int FooterGap = 8;
    const int FooterY = InfoInnerRect.Y + InfoInnerRect.H - FooterH - 6;
    const RectI FooterCardA =
    {
        HeaderX,
        FooterY,
        std::max(56, (HeaderW - FooterGap) / 2),
        FooterH
    };
    const RectI FooterCardB =
    {
        FooterCardA.X + FooterCardA.W + FooterGap,
        FooterY,
        std::max(56, HeaderW - FooterCardA.W - FooterGap),
        FooterH
    };
    const size_t NameChars = std::max<size_t>(10, (size_t)std::floor((float)HeaderRect.W / 9.0f));
    const size_t DetailChars = std::max<size_t>(12, (size_t)std::floor((float)HeaderRect.W / 7.5f));
    char Buf[64];

    // Clear queue slot hits before rebuilding
    mState.InfoPanel.QueueSlotHits.clear();
    mState.InfoPanel.MultiSelectSlotHits.clear();

    if (mState.InfoPanel.SelectedCount > 1 && !mState.InfoPanel.MultiSelectSlots.empty())
    {
        DrawMultiSelectPanel(Layout, HasModularConsole);
        return;
    }

    const int MouseX = (int)Core::Platform::GetMouseX();
    const int MouseY = (int)Core::Platform::GetMouseY();

    if (mState.InfoPanel.IsBuilding)
    {
        // -------------------------------------------------------------
        // Authentic Brood War Building Information & Production Layout
        // -------------------------------------------------------------
        const bool IsProtossBuilding = mState.InfoPanel.ShieldMax > 0;
        const Render::Color BorderColor = IsProtossBuilding
            ? Render::Color(50, 150, 245, 230)
            : Render::Color(60, 220, 80, 230);
        const Render::Color TextColor = Render::Color(220, 225, 235, 255);
        const Render::Color ProgressFillColor = IsProtossBuilding
            ? Render::Color(60, 160, 255, 255)
            : Render::Color(70, 220, 90, 255);

        // Building Name
        D.DrawText(Ellipsize(mState.InfoPanel.Name, NameChars),
                   (float)HeaderRect.X, (float)HeaderRect.Y + 2.0f, 1.2f,
                   Render::Color(240, 225, 185, 255));

        if (mState.InfoPanel.IsTechBuilding)
        {
            if (mState.InfoPanel.Progress >= 0.0f && !mState.InfoPanel.StatusLine.empty())
            {
                // Active research area (Single slot, NO slots 2..5!)
                constexpr int ActiveSlotSize = 34;
                const RectI Slot1 =
                {
                    HeaderX,
                    HeaderRect.Y + 18,
                    ActiveSlotSize,
                    ActiveSlotSize
                };

                D.DrawRect((float)Slot1.X, (float)Slot1.Y, (float)Slot1.W, (float)Slot1.H,
                           Render::Color(10, 14, 22, 230));

                int ActiveResId = mState.InfoPanel.ActiveResearchID;

                if (ActiveResId >= 0)
                {
                    const auto* ResIcon = GetCommandIcon(CommandIconForResearch((Game::Systems::TerranResearchId)ActiveResId));
                    if (ResIcon && ResIcon->Valid())
                    {
                        DrawContainedImage(*ResIcon,
                                           { Slot1.X + 2, Slot1.Y + 2, ActiveSlotSize - 4, ActiveSlotSize - 4 },
                                           Render::Colors::White());
                    }
                }

                D.DrawRectLines((float)Slot1.X, (float)Slot1.Y, (float)Slot1.W, (float)Slot1.H,
                                BorderColor, 1.2f);

                // Action text
                const int StatusX = HeaderX + ActiveSlotSize + 8;
                std::string ActionText = mState.InfoPanel.StatusLine;
                if (ActionText.empty())
                    ActionText = "Researching";
                D.DrawText(Ellipsize(ActionText, DetailChars), (float)StatusX, (float)(HeaderRect.Y + 18.0f), 0.95f,
                           TextColor);

                // Progress bar
                const int BarX = StatusX;
                const int BarYPos = HeaderRect.Y + 34;
                const int BarW = std::max(60, HeaderW - ActiveSlotSize - 14);
                constexpr int BarH = 9;

                D.DrawRect((float)BarX, (float)BarYPos, (float)BarW, (float)BarH,
                           Render::Color(14, 18, 26, 230));
                D.DrawRectLines((float)BarX, (float)BarYPos, (float)BarW, (float)BarH,
                                BorderColor, 1.0f);

                if (mState.InfoPanel.Progress >= 0.0f)
                {
                    const float Prog = std::clamp(mState.InfoPanel.Progress, 0.0f, 1.0f);
                    const float FillW = std::max(0.0f, (float)(BarW - 2) * Prog);
                    D.DrawRect((float)(BarX + 1), (float)(BarYPos + 1), FillW, (float)(BarH - 2),
                               ProgressFillColor);
                }
            }
            else if (!mState.InfoPanel.TechUpgrades.empty())
            {
                // Idle Tech building: display current tech/upgrade levels as icon placeholders
                constexpr int IconSize = 28;
                constexpr int IconGap = 6;
                const int RowY = HeaderRect.Y + 20;

                for (size_t I = 0; I < mState.InfoPanel.TechUpgrades.size() && I < 5; ++I)
                {
                    const auto& Tech = mState.InfoPanel.TechUpgrades[I];
                    const RectI Box =
                    {
                        HeaderX + (int)I * (IconSize + IconGap),
                        RowY,
                        IconSize,
                        IconSize
                    };

                    const bool Hover = (MouseX >= Box.X && MouseX < Box.X + Box.W &&
                                        MouseY >= Box.Y && MouseY < Box.Y + Box.H);

                    D.DrawRect((float)Box.X, (float)Box.Y, (float)Box.W, (float)Box.H,
                               Render::Color(10, 14, 22, 230));

                    const auto* ResIcon = GetCommandIcon(CommandIconForResearch((Game::Systems::TerranResearchId)Tech.ResearchId));
                    if (ResIcon && ResIcon->Valid())
                    {
                        const Render::Color Tint = (Tech.Level > 0)
                            ? Render::Colors::White()
                            : Render::Color(130, 135, 145, 160);
                        DrawContainedImage(*ResIcon,
                                           { Box.X + 2, Box.Y + 2, IconSize - 4, IconSize - 4 },
                                           Tint);
                    }

                    D.DrawRectLines((float)Box.X, (float)Box.Y, (float)Box.W, (float)Box.H,
                                    Hover ? Render::Color(255, 230, 90, 240) : BorderColor,
                                    Hover ? 1.5f : 1.0f);

                    char LvlBuf[8];
                    std::snprintf(LvlBuf, sizeof(LvlBuf), "L%d", Tech.Level);
                    const Render::Color LvlColor = (Tech.Level > 0)
                        ? (IsProtossBuilding ? Render::Color(80, 210, 255, 255) : Render::Color(80, 255, 120, 255))
                        : Render::Color(160, 165, 175, 200);
                    D.DrawText(LvlBuf, (float)(Box.X + Box.W - 13), (float)(Box.Y + Box.H - 9),
                               0.75f, LvlColor);
                }

                for (size_t I = 0; I < mState.InfoPanel.TechUpgrades.size() && I < 5; ++I)
                {
                    const auto& Tech = mState.InfoPanel.TechUpgrades[I];
                    const RectI Box =
                    {
                        HeaderX + (int)I * (IconSize + IconGap),
                        RowY,
                        IconSize,
                        IconSize
                    };
                    if (MouseX >= Box.X && MouseX < Box.X + Box.W &&
                        MouseY >= Box.Y && MouseY < Box.Y + Box.H)
                    {
                        std::string Info = Tech.Name + " (Level " + std::to_string(Tech.Level) + ")";
                        D.DrawText(Ellipsize(Info, DetailChars), (float)HeaderX, (float)(RowY + IconSize + 6),
                                   0.85f, Render::Color(240, 230, 150, 255));
                        break;
                    }
                }
            }
        }
        else
        {
            const bool HasProductionQueue = !mState.InfoPanel.QueueUnitDefIDs.empty() ||
                                            (mState.InfoPanel.Progress >= 0.0f && !mState.InfoPanel.StatusLine.empty());

            if (HasProductionQueue)
            {
                // Active production area (Slot 1)
                constexpr int ActiveSlotSize = 34;
                const RectI Slot1 =
                {
                    HeaderX,
                    HeaderRect.Y + 18,
                    ActiveSlotSize,
                    ActiveSlotSize
                };

                const bool HoverSlot1 = (MouseX >= Slot1.X && MouseX < Slot1.X + Slot1.W &&
                                         MouseY >= Slot1.Y && MouseY < Slot1.Y + Slot1.H);

                D.DrawRect((float)Slot1.X, (float)Slot1.Y, (float)Slot1.W, (float)Slot1.H,
                           Render::Color(10, 14, 22, 230));

                int ActiveDefID = -1;
                if (!mState.InfoPanel.QueueUnitDefIDs.empty())
                {
                    ActiveDefID = mState.InfoPanel.QueueUnitDefIDs[0];
                    mState.InfoPanel.QueueSlotHits.push_back({ Slot1.X, Slot1.Y, Slot1.W, Slot1.H, 0 });
                }

                if (ActiveDefID >= 0)
                {
                    const auto* UnitIcon = GetCommandIcon(IconForDef(ActiveDefID));
                    if (UnitIcon && UnitIcon->Valid())
                    {
                        DrawContainedImage(*UnitIcon,
                                           { Slot1.X + 2, Slot1.Y + 2, ActiveSlotSize - 4, ActiveSlotSize - 4 },
                                           Render::Colors::White());
                    }
                }

                // Draw Slot 1 border
                if (HoverSlot1 && ActiveDefID >= 0)
                {
                    D.DrawRect((float)Slot1.X, (float)Slot1.Y, (float)Slot1.W, (float)Slot1.H,
                               Render::Color(240, 60, 60, 75));
                    D.DrawRectLines((float)Slot1.X, (float)Slot1.Y, (float)Slot1.W, (float)Slot1.H,
                                    Render::Color(255, 120, 120, 240), 1.5f);
                }
                else
                {
                    D.DrawRectLines((float)Slot1.X, (float)Slot1.Y, (float)Slot1.W, (float)Slot1.H,
                                    BorderColor, 1.2f);
                }
                // Small subscript "1" in bottom-right corner of active slot
                D.DrawText("1", (float)(Slot1.X + Slot1.W - 8), (float)(Slot1.Y + Slot1.H - 11), 0.85f,
                           Render::Color(210, 215, 220, 220));

                // Status action text to the right of Slot 1
                const int StatusX = HeaderX + ActiveSlotSize + 8;
                std::string ActionText = mState.InfoPanel.StatusLine;
                if (ActionText.empty())
                    ActionText = "Training";
                D.DrawText(Ellipsize(ActionText, DetailChars), (float)StatusX, (float)(HeaderRect.Y + 18.0f), 0.95f,
                           TextColor);

                // Progress bar directly below status action text
                const int BarX = StatusX;
                const int BarYPos = HeaderRect.Y + 34;
                const int BarW = std::max(60, HeaderW - ActiveSlotSize - 14);
                constexpr int BarH = 9;

                D.DrawRect((float)BarX, (float)BarYPos, (float)BarW, (float)BarH,
                           Render::Color(14, 18, 26, 230));
                D.DrawRectLines((float)BarX, (float)BarYPos, (float)BarW, (float)BarH,
                                BorderColor, 1.0f);

                if (mState.InfoPanel.Progress >= 0.0f)
                {
                    const float Prog = std::clamp(mState.InfoPanel.Progress, 0.0f, 1.0f);
                    const float FillW = std::max(0.0f, (float)(BarW - 2) * Prog);
                    D.DrawRect((float)(BarX + 1), (float)(BarYPos + 1), FillW, (float)(BarH - 2),
                               ProgressFillColor);
                }

                // Horizontal queue slots (Slots 2, 3, 4, 5) directly below Slot 1 & progress bar
                constexpr int QueueSlotSize = 26;
                constexpr int QueueSlotGap = 6;
                const int QueueRowY = HeaderRect.Y + 56;

                for (size_t I = 1; I < 5; ++I)
                {
                    const RectI SlotRect =
                    {
                        HeaderX + (int)(I - 1) * (QueueSlotSize + QueueSlotGap),
                        QueueRowY,
                        QueueSlotSize,
                        QueueSlotSize
                    };

                    const bool IsQueued = (I < mState.InfoPanel.QueueUnitDefIDs.size());
                    const bool IsHovered = (MouseX >= SlotRect.X && MouseX < SlotRect.X + SlotRect.W &&
                                            MouseY >= SlotRect.Y && MouseY < SlotRect.Y + SlotRect.H);

                    D.DrawRect((float)SlotRect.X, (float)SlotRect.Y, (float)SlotRect.W, (float)SlotRect.H,
                               Render::Color(10, 14, 22, 230));

                    if (IsQueued)
                    {
                        mState.InfoPanel.QueueSlotHits.push_back({ SlotRect.X, SlotRect.Y, SlotRect.W, SlotRect.H, (int)I });
                        const int DefID = mState.InfoPanel.QueueUnitDefIDs[I];
                        const auto* UnitIcon = GetCommandIcon(IconForDef(DefID));
                        if (UnitIcon && UnitIcon->Valid())
                        {
                            DrawContainedImage(*UnitIcon,
                                               { SlotRect.X + 2, SlotRect.Y + 2, QueueSlotSize - 4, QueueSlotSize - 4 },
                                               Render::Colors::White());
                        }

                        if (IsHovered)
                        {
                            D.DrawRect((float)SlotRect.X, (float)SlotRect.Y, (float)SlotRect.W, (float)SlotRect.H,
                                       Render::Color(240, 60, 60, 75));
                            D.DrawRectLines((float)SlotRect.X, (float)SlotRect.Y, (float)SlotRect.W, (float)SlotRect.H,
                                            Render::Color(255, 120, 120, 240), 1.5f);
                        }
                        else
                        {
                            D.DrawRectLines((float)SlotRect.X, (float)SlotRect.Y, (float)SlotRect.W, (float)SlotRect.H,
                                            BorderColor, 1.2f);
                        }

                        // Small subscript numeral (2, 3, 4, 5)
                        char SlotSubBuf[8];
                        std::snprintf(SlotSubBuf, sizeof(SlotSubBuf), "%d", (int)(I + 1));
                        D.DrawText(SlotSubBuf, (float)(SlotRect.X + SlotRect.W - 8), (float)(SlotRect.Y + SlotRect.H - 11),
                                   0.85f, Render::Color(210, 215, 220, 220));
                    }
                    else
                    {
                        // Empty slot: draw border and large bold numeral in race color (matches Brood War SC:R)
                        D.DrawRectLines((float)SlotRect.X, (float)SlotRect.Y, (float)SlotRect.W, (float)SlotRect.H,
                                        BorderColor, 1.0f);
                        char SlotNumBuf[8];
                        std::snprintf(SlotNumBuf, sizeof(SlotNumBuf), "%d", (int)(I + 1));
                        D.DrawText(SlotNumBuf, (float)SlotRect.X + 8.0f, (float)SlotRect.Y + 4.0f, 1.35f,
                                   BorderColor);
                    }
                }
            }
            else if (mState.InfoPanel.IsTownCenter)
        {
            // Idle Town Center supply statistics (matches Brood War Screenshot 8)
            float CurrentY = (float)HeaderRect.Y + 20.0f;
            char SupplyBuf[64];

            std::snprintf(SupplyBuf, sizeof(SupplyBuf), "Supplies Used: %d", mState.InfoPanel.SupplyUsed);
            D.DrawText(SupplyBuf, (float)HeaderX, CurrentY, 0.95f, TextColor);
            CurrentY += 13.0f;

            std::snprintf(SupplyBuf, sizeof(SupplyBuf), "Supplies Provided: %d", mState.InfoPanel.SupplyProvided);
            D.DrawText(SupplyBuf, (float)HeaderX, CurrentY, 0.95f, TextColor);
            CurrentY += 13.0f;

            std::snprintf(SupplyBuf, sizeof(SupplyBuf), "Total Supplies: %d", mState.InfoPanel.SupplyTotal);
            D.DrawText(SupplyBuf, (float)HeaderX, CurrentY, 0.95f, TextColor);
            CurrentY += 13.0f;

            std::snprintf(SupplyBuf, sizeof(SupplyBuf), "Supplies Max: %d", mState.InfoPanel.SupplyMax);
            D.DrawText(SupplyBuf, (float)HeaderX, CurrentY, 0.95f, TextColor);
        }
        else
        {
            // Idle Production / Tech Building
            float CurrentY = (float)HeaderRect.Y + 20.0f;
            if (!mState.InfoPanel.StatusLine.empty())
            {
                D.DrawText(Ellipsize(mState.InfoPanel.StatusLine, DetailChars),
                           (float)HeaderX, CurrentY, 0.95f, Render::Color(170, 220, 160, 255));
                CurrentY += 14.0f;
            }
            if (!mState.InfoPanel.CargoLine.empty())
            {
                D.DrawText(Ellipsize(mState.InfoPanel.CargoLine, DetailChars),
                           (float)HeaderX, CurrentY, 0.9f, Render::Color(180, 200, 220, 255));
            }
        }
    }
    }
    else
    {
        // -------------------------------------------------------------
        // Unit / Resource Information Layout
        // -------------------------------------------------------------
        // Name
        D.DrawText(Ellipsize(mState.InfoPanel.Name, NameChars), (float)HeaderRect.X, (float)HeaderRect.Y + 2.0f, 1.25f,
                   Render::Color(240, 225, 185, 255));

        float CurrentY = (float)HeaderRect.Y + 18.0f;
        if (!mState.InfoPanel.TypeLine.empty())
        {
            D.DrawText(Ellipsize(mState.InfoPanel.TypeLine, DetailChars), (float)HeaderRect.X, CurrentY, 0.95f,
                       Render::Color(170, 185, 210, 255));
            CurrentY += 14.0f;
        }

        if (!mState.InfoPanel.StatusLine.empty())
        {
            D.DrawText(Ellipsize(mState.InfoPanel.StatusLine, DetailChars), (float)HeaderRect.X, CurrentY, 0.95f,
                       Render::Color(220, 210, 140, 255));
            CurrentY += 14.0f;
        }

        if (!mState.InfoPanel.CombatLine.empty())
        {
            D.DrawText(Ellipsize(mState.InfoPanel.CombatLine, DetailChars), (float)HeaderRect.X, CurrentY, 0.9f,
                       Render::Color(214, 195, 160, 255));
            CurrentY += 13.0f;
        }
        else if (!mState.InfoPanel.StatLine.empty())
        {
            D.DrawText(Ellipsize(mState.InfoPanel.StatLine, DetailChars), (float)HeaderRect.X, CurrentY, 0.9f,
                       Render::Color(188, 192, 204, 255));
            CurrentY += 13.0f;
        }

        int BarY = (int)std::lround(CurrentY + 3.0f);
        const RectI StatBodyRect =
        {
            HeaderX,
            BarY,
            HeaderW,
            std::max(20, InfoInnerRect.Y + InfoInnerRect.H - BarY)
        };
        const int MaxBottomY = InfoInnerRect.Y + InfoInnerRect.H;

        DrawInfoValueBar(BarY, "HP", mState.InfoPanel.CurrentValue, mState.InfoPanel.MaxValue,
                         StatBodyRect, MaxBottomY, HpEmptyStrip, HpStrip, Render::Color(80, 200, 95, 255));
        DrawInfoValueBar(BarY, "SH", mState.InfoPanel.ShieldValue, mState.InfoPanel.ShieldMax,
                         StatBodyRect, MaxBottomY, HpEmptyStrip, HpStrip, Render::Color(70, 150, 255, 255));
        DrawInfoValueBar(BarY, "EN", mState.InfoPanel.EnergyValue, mState.InfoPanel.EnergyMax,
                         StatBodyRect, MaxBottomY, HpEmptyStrip, HpStrip, Render::Color(245, 200, 70, 255));
        if (mState.InfoPanel.Progress >= 0.0f)
        {
            DrawInfoProgressBar(BarY, mState.InfoPanel.Progress, StatBodyRect, Render::Color(220, 160, 70, 255));
        }

        if (!mState.InfoPanel.CargoLine.empty())
        {
            const bool CarryingGas = mState.InfoPanel.CargoLine.find("Gas") != std::string::npos;
            const AtlasRect* CargoIcon = CarryingGas ? &AggIconGas : &AggIconMinerals;
            DrawInfoFooterCard(FooterCardA,
                               mState.InfoPanel.CargoLine,
                               Render::Color(175, 214, 210, 255),
                               AggIcons,
                               CargoIcon,
                               nullptr);
        }
        if (!mState.InfoPanel.DetailLine.empty() && mState.InfoPanel.DetailLine != mState.InfoPanel.StatusLine)
        {
            DrawInfoFooterCard(FooterCardB, mState.InfoPanel.DetailLine, Render::Color(196, 196, 184, 255),
                               AggIcons, nullptr, nullptr);
        }
    }

    if (mState.InfoPanel.SelectedCount > 1)
    {
        std::snprintf(Buf, sizeof(Buf), "Group: %d", mState.InfoPanel.SelectedCount);
        D.DrawText(Buf, (float)HeaderRect.X + (float)HeaderRect.W - 86.0f, (float)HeaderRect.Y + 2.0f, 1.0f,
                   Render::Color(180, 200, 220, 255));
    }
}

void Hud::DrawMultiSelectPanel(const HudLayout& Layout, bool HasModularConsole)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    const RectI& InfoRect = Layout.InfoRect;
    const RectI InfoInnerRect = HasModularConsole
        ? InsetRect(InfoRect, std::max(12, InfoRect.W / 40), std::max(10, InfoRect.H / 14))
        : InfoRect;

    const int MouseX = (int)Core::Platform::GetMouseX();
    const int MouseY = (int)Core::Platform::GetMouseY();

    mState.InfoPanel.MultiSelectSlotHits.clear();
    const auto& Slots = mState.InfoPanel.MultiSelectSlots;
    if (Slots.empty())
        return;

    constexpr int Cols = 6;
    constexpr int Rows = 2;
    constexpr int MaxSlots = Cols * Rows;
    const int TotalSlots = std::min((int)Slots.size(), MaxSlots);

    const int GapX = 5;
    const int GapY = 5;
    const int PadX = 6;
    const int PadY = 6;

    const int AvailW = InfoInnerRect.W - PadX * 2;
    const int AvailH = InfoInnerRect.H - PadY * 2;
    const int SlotW = std::max(20, (AvailW - (Cols - 1) * GapX) / Cols);
    const int SlotH = std::max(20, (AvailH - (Rows - 1) * GapY) / Rows);

    // Subtle dark backdrop for multi-select area
    D.DrawRect((float)InfoInnerRect.X, (float)InfoInnerRect.Y,
               (float)InfoInnerRect.W, (float)InfoInnerRect.H,
               Render::Color(8, 12, 20, 200));

    const auto* WireframeFrames = UIAssets.GetWireframeFrames();

    for (int I = 0; I < TotalSlots; ++I)
    {
        const auto& Slot = Slots[(size_t)I];
        const int Col = I % Cols;
        const int Row = I / Cols;

        const int SlotX = InfoInnerRect.X + PadX + Col * (SlotW + GapX);
        const int SlotY = InfoInnerRect.Y + PadY + Row * (SlotH + GapY);
        const RectI SlotRect = { SlotX, SlotY, SlotW, SlotH };

        const bool Hovered = SlotRect.Contains(MouseX, MouseY);

        // Compute Health Ratio (HP + Shields)
        const int TotalHp = Slot.CurrentHp + Slot.Shield;
        const int TotalMax = std::max(1, Slot.MaxHp + Slot.ShieldMax);
        const float Ratio = std::clamp((float)TotalHp / (float)TotalMax, 0.0f, 1.0f);

        // Color coding: Green = healthy, Yellow = damaged, Red = critical
        Render::Color StatusColor;
        if (Ratio > 0.66f)
            StatusColor = Render::Color(65, 230, 85, 255);
        else if (Ratio > 0.33f)
            StatusColor = Render::Color(245, 215, 45, 255);
        else
            StatusColor = Render::Color(245, 55, 55, 255);

        // Slot background
        const Render::Color SlotBg = Hovered
            ? Render::Color(20, 32, 50, 240)
            : Render::Color(10, 16, 26, 220);
        D.DrawRect((float)SlotX, (float)SlotY, (float)SlotW, (float)SlotH, SlotBg);

        // Slot border: glowing cyan/blue if hovered, subtle dark blue otherwise
        const Render::Color SlotBorder = Hovered
            ? Render::Color(80, 200, 255, 255)
            : Render::Color(30, 70, 115, 180);
        D.DrawRectLines((float)SlotX, (float)SlotY, (float)SlotW, (float)SlotH, SlotBorder, 1.0f);

        // Draw wireframe inside slot
        const int ContentPadding = 3;
        const int BarH = 4;
        const RectI IconRect =
        {
            SlotX + ContentPadding,
            SlotY + ContentPadding,
            std::max(1, SlotW - ContentPadding * 2),
            std::max(1, SlotH - ContentPadding * 2 - BarH - 2)
        };

        bool DrewWireframe = false;
        if (Slot.WireframeIndex >= 0)
        {
            UIAssets.EnsureWireframeFrame(Slot.WireframeIndex);
            if (WireframeFrames && Slot.WireframeIndex < (int)WireframeFrames->size() &&
                (*WireframeFrames)[(size_t)Slot.WireframeIndex].Valid())
            {
                const auto& WireframeImg = (*WireframeFrames)[(size_t)Slot.WireframeIndex];
                const auto* WireframeBounds = UIAssets.GetWireframeOpaqueBounds(Slot.WireframeIndex);
                if (WireframeBounds && WireframeBounds->Valid())
                {
                    DrawContainedAtlas(WireframeImg,
                                       { WireframeBounds->X, WireframeBounds->Y, WireframeBounds->W, WireframeBounds->H },
                                       IconRect,
                                       StatusColor);
                }
                else
                {
                    DrawContainedImage(WireframeImg, IconRect, StatusColor);
                }
                DrewWireframe = true;
            }
        }

        if (!DrewWireframe)
        {
            const std::string Initial = Slot.Name.empty() ? "?" : Slot.Name.substr(0, 3);
            D.DrawText(Initial, (float)IconRect.X + (float)IconRect.W * 0.5f - 8.0f,
                       (float)IconRect.Y + (float)IconRect.H * 0.5f - 5.0f, 0.9f, StatusColor);
        }

        // Mini Health/Shield bar at bottom of slot
        const int BarX = SlotX + ContentPadding;
        const int BarY = SlotY + SlotH - ContentPadding - BarH;
        const int BarW = std::max(1, SlotW - ContentPadding * 2);

        D.DrawRect((float)BarX, (float)BarY, (float)BarW, (float)BarH, Render::Color(12, 12, 16, 240));

        if (Slot.ShieldMax > 0)
        {
            const float ShieldRatio = std::clamp((float)Slot.Shield / (float)Slot.ShieldMax, 0.0f, 1.0f);
            const float HpRatio = std::clamp((float)Slot.CurrentHp / (float)Slot.MaxHp, 0.0f, 1.0f);
            const int ShieldSpanW = (int)std::lround((float)BarW * 0.4f);
            const int HpSpanW = BarW - ShieldSpanW;
            const int ShieldFillW = (int)std::lround((float)ShieldSpanW * ShieldRatio);
            const int HpFillW = (int)std::lround((float)HpSpanW * HpRatio);

            if (ShieldFillW > 0)
                D.DrawRect((float)BarX, (float)BarY, (float)ShieldFillW, (float)BarH, Render::Color(50, 180, 255, 255));
            if (HpFillW > 0)
                D.DrawRect((float)(BarX + ShieldSpanW), (float)BarY, (float)HpFillW, (float)BarH, StatusColor);
        }
        else
        {
            const float HpRatio = std::clamp((float)Slot.CurrentHp / (float)Slot.MaxHp, 0.0f, 1.0f);
            const int HpFillW = (int)std::lround((float)BarW * HpRatio);
            if (HpFillW > 0)
                D.DrawRect((float)BarX, (float)BarY, (float)HpFillW, (float)BarH, StatusColor);
        }

        mState.InfoPanel.MultiSelectSlotHits.push_back({ SlotX, SlotY, SlotW, SlotH, I, Slot.EntityId });
    }
}

} // namespace Game::Client::UI
