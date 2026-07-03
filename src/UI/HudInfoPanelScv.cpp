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
    const int FooterH = 28;
    const int FooterGap = 8;
    const int FooterY = InfoInnerRect.Y + InfoInnerRect.H - FooterH - 8;
    const int StatTop = std::max(PreviewRect.Y + PreviewRect.H + 10, HeaderRect.Y + 60);
    const int StatBottom = std::max(StatTop + 24, FooterY - 8);
    const RectI StatBodyRect =
    {
        InfoInnerRect.X + 12,
        StatTop,
        std::max(120, InfoInnerRect.W - 24),
        std::max(24, StatBottom - StatTop)
    };
    const RectI FooterCardA =
    {
        StatBodyRect.X,
        FooterY,
        std::max(56, (StatBodyRect.W - FooterGap) / 2),
        FooterH
    };
    const RectI FooterCardB =
    {
        FooterCardA.X + FooterCardA.W + FooterGap,
        FooterY,
        std::max(56, StatBodyRect.W - FooterCardA.W - FooterGap),
        FooterH
    };
    const size_t NameChars = std::max<size_t>(10, (size_t)std::floor((float)HeaderRect.W / 8.5f));
    const size_t DetailChars = std::max<size_t>(12, (size_t)std::floor((float)HeaderRect.W / 7.2f));
    const auto* MineralHdIcon = GetLegacyIcon(0);
    const auto* GasHdIcon = GetLegacyIcon(2);
    const auto* ArmorIcon = GetLegacyIcon(IconArmor);
    const auto* HarvestMask = GetCommandIconMask(CommandIconForCommand(HudCommandId::Harvest));
    const auto* MoveMask = GetCommandIconMask(CommandIconForCommand(HudCommandId::Move));
    char Buf[64];

    D.DrawText(Ellipsize(mState.InfoPanel.Name, NameChars),
               (float)HeaderRect.X, (float)HeaderRect.Y + 4.0f, 1.5f,
               Render::Color(230, 230, 235, 255));
    D.DrawText(Ellipsize(mState.InfoPanel.RoleLine.empty() ? "Private" : mState.InfoPanel.RoleLine, DetailChars),
               (float)HeaderRect.X, (float)HeaderRect.Y + 22.0f, 1.1f,
               Render::Color(210, 210, 220, 255));
    std::snprintf(Buf, sizeof(Buf), "Kills: %d", mState.InfoPanel.KillCount);
    D.DrawText(Buf, (float)HeaderRect.X, (float)HeaderRect.Y + 38.0f, 1.0f,
               Render::Color(220, 220, 225, 255));
    if (!mState.InfoPanel.StatusLine.empty())
    {
        D.DrawText(Ellipsize(mState.InfoPanel.StatusLine, DetailChars),
                   (float)HeaderRect.X + 150.0f, (float)HeaderRect.Y + 38.0f, 1.0f,
                   Render::Color(235, 220, 150, 255));
    }
    if (!mState.InfoPanel.DetailLine.empty())
    {
        D.DrawText(Ellipsize(mState.InfoPanel.DetailLine, DetailChars),
                   (float)HeaderRect.X, (float)HeaderRect.Y + 54.0f, 1.0f,
                   Render::Color(180, 210, 180, 255));
    }

    if (mState.InfoPanel.CarryingAmount > 0)
    {
        const bool CarryingGas = mState.InfoPanel.CargoLine.find("Gas") != std::string::npos;
        const auto* CargoHdIcon = CarryingGas ? GasHdIcon : MineralHdIcon;
        const RectI CargoIconRect = { HeaderRect.X + HeaderRect.W - 26, HeaderRect.Y + 4, 18, 18 };
        if (CargoHdIcon && CargoHdIcon->Valid())
            DrawContainedImage(*CargoHdIcon, CargoIconRect, Render::Colors::White());
        std::snprintf(Buf, sizeof(Buf), "%d", mState.InfoPanel.CarryingAmount);
        D.DrawText(Buf, (float)CargoIconRect.X - 26.0f, (float)CargoIconRect.Y + 3.0f, 1.0f,
                   CarryingGas ? Render::Color(84, 208, 168, 255) : Render::Color(96, 180, 255, 255));
    }

    int StatX = HeaderRect.X;
    StatX = DrawInfoMiniStat(StatX, HeaderRect, ArmorIcon, nullptr, std::to_string(mState.InfoPanel.Armor), Render::Color(215, 215, 215, 255));
    StatX = DrawInfoMiniStat(StatX, HeaderRect, nullptr, HarvestMask, std::to_string(mState.InfoPanel.CarryingAmount), Render::Color(228, 214, 96, 255));
    DrawInfoMiniStat(StatX, HeaderRect, nullptr, MoveMask, std::to_string(mState.InfoPanel.SpeedValue), Render::Color(214, 224, 238, 255));

    int BarY = StatBodyRect.Y;
    DrawInfoValueBar(BarY, "HP", mState.InfoPanel.CurrentValue, mState.InfoPanel.MaxValue,
                     StatBodyRect, FooterY, HpEmptyStrip, HpStrip, Render::Color(80, 200, 95, 255));

    const bool CarryingGas = mState.InfoPanel.CargoLine.find("Gas") != std::string::npos;
    const AtlasRect* CargoIcon = mState.InfoPanel.CargoLine.empty() ? nullptr : (CarryingGas ? &AggIconGas : &AggIconMinerals);
    DrawInfoFooterCard(FooterCardA,
                       mState.InfoPanel.CargoLine.empty() ? mState.InfoPanel.StatusLine : mState.InfoPanel.CargoLine,
                       Render::Color(170, 220, 210, 255),
                       AggIcons,
                       CargoIcon,
                       mState.InfoPanel.CargoLine.empty() ? MoveMask : nullptr);
    if (!mState.InfoPanel.DetailLine.empty() && mState.InfoPanel.DetailLine != mState.InfoPanel.StatusLine)
    {
        DrawInfoFooterCard(FooterCardB, mState.InfoPanel.DetailLine, Render::Color(200, 200, 185, 255),
                           AggIcons, nullptr, HarvestMask);
    }
}

} // namespace Game::Client::UI
