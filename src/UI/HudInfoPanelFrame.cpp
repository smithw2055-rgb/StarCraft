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
