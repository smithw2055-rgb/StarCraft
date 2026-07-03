#include "UI/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "Render/Draw2D.h"
#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"
#include "UI/HudInfoWidgets.h"
#include "UI/HudText.h"

namespace Game::Client::UI
{

namespace
{
    constexpr AtlasRect AggIconMinerals = { 0, 0, 16, 16 };
    constexpr AtlasRect AggIconGas = { 17, 0, 16, 16 };
}

void Hud::DrawStandardInfoPanel(const HudLayout& Layout,
                                bool HasModularConsole,
                                const Resource::Graphics::GpuImage& AggIcons)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    const RectI& InfoRect = Layout.InfoRect;
    const RectI& PreviewRect = Layout.PreviewRect;
    const RectI& QueueRect = Layout.QueueRect;
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
    char Buf[64];

    D.DrawText(Ellipsize(mState.InfoPanel.Name, NameChars), (float)HeaderRect.X, (float)HeaderRect.Y + 2.0f, 1.9f,
               Render::Color(240, 220, 180, 255));
    if (!mState.InfoPanel.TypeLine.empty())
    {
        D.DrawText(Ellipsize(mState.InfoPanel.TypeLine, DetailChars), (float)HeaderRect.X, (float)HeaderRect.Y + 22.0f, 1.2f,
                   Render::Color(170, 185, 210, 255));
    }
    if (!mState.InfoPanel.StatusLine.empty())
    {
        D.DrawText(Ellipsize(mState.InfoPanel.StatusLine, DetailChars), (float)HeaderRect.X, (float)HeaderRect.Y + 38.0f, 1.2f,
                   Render::Color(220, 210, 140, 255));
    }
    if (!mState.InfoPanel.StatLine.empty())
    {
        D.DrawText(Ellipsize(mState.InfoPanel.StatLine, DetailChars), (float)HeaderRect.X, (float)HeaderRect.Y + 54.0f, 1.0f,
                   Render::Color(188, 192, 204, 255));
    }
    if (!mState.InfoPanel.CombatLine.empty())
    {
        D.DrawText(Ellipsize(mState.InfoPanel.CombatLine, DetailChars), (float)HeaderRect.X + (float)HeaderRect.W * 0.45f, (float)HeaderRect.Y + 54.0f, 1.0f,
                   Render::Color(214, 195, 160, 255));
    }

    int BarY = StatBodyRect.Y;
    DrawInfoValueBar(BarY, "HP", mState.InfoPanel.CurrentValue, mState.InfoPanel.MaxValue,
                     StatBodyRect, FooterY, HpEmptyStrip, HpStrip, Render::Color(80, 200, 95, 255));
    DrawInfoValueBar(BarY, "SH", mState.InfoPanel.ShieldValue, mState.InfoPanel.ShieldMax,
                     StatBodyRect, FooterY, HpEmptyStrip, HpStrip, Render::Color(70, 150, 255, 255));
    DrawInfoValueBar(BarY, "EN", mState.InfoPanel.EnergyValue, mState.InfoPanel.EnergyMax,
                     StatBodyRect, FooterY, HpEmptyStrip, HpStrip, Render::Color(245, 200, 70, 255));
    if (mState.InfoPanel.Progress >= 0.0f)
    {
        DrawInfoProgressBar(BarY, mState.InfoPanel.Progress, StatBodyRect, Render::Color(220, 160, 70, 255));
    }

    const bool CarryingGas = mState.InfoPanel.CargoLine.find("Gas") != std::string::npos;
    const AtlasRect* CargoIcon = mState.InfoPanel.CargoLine.empty() ? nullptr : (CarryingGas ? &AggIconGas : &AggIconMinerals);
    DrawInfoFooterCard(FooterCardA,
                       mState.InfoPanel.CargoLine.empty() ? mState.InfoPanel.StatusLine : mState.InfoPanel.CargoLine,
                       Render::Color(175, 214, 210, 255),
                       AggIcons,
                       CargoIcon,
                       nullptr);
    if (!mState.InfoPanel.DetailLine.empty())
    {
        DrawInfoFooterCard(FooterCardB, mState.InfoPanel.DetailLine, Render::Color(196, 196, 184, 255),
                           AggIcons, nullptr, nullptr);
    }
    else if (!mState.InfoPanel.CombatLine.empty() && mState.InfoPanel.CombatLine != mState.InfoPanel.StatLine)
    {
        DrawInfoFooterCard(FooterCardB, mState.InfoPanel.CombatLine, Render::Color(214, 195, 160, 255),
                           AggIcons, nullptr, nullptr);
    }

    DrawInfoQueueList(QueueRect, mState.InfoPanel.QueueLabel, mState.InfoPanel.QueueEntries);

    if (mState.InfoPanel.SelectedCount > 1)
    {
        std::snprintf(Buf, sizeof(Buf), "Group: %d", mState.InfoPanel.SelectedCount);
        D.DrawText(Buf, (float)HeaderRect.X + (float)HeaderRect.W - 86.0f, (float)HeaderRect.Y + 2.0f, 1.0f,
                   Render::Color(180, 200, 220, 255));
    }
}

} // namespace Game::Client::UI
