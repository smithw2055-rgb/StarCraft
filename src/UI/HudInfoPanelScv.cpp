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
