#include "UI/Hud.h"

#include "Core/Platform/Platform.h"
#include "Render/Draw2D.h"
#include "UI/HudButtonStyle.h"
#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"
#include "UI/HudText.h"

#include <cstddef>

namespace Game::Client::UI
{

void Hud::DrawCommandPanel(const HudLayout& Layout, int ViewportWidth, int ViewportHeight)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    const auto* ButtonSkins = UIAssets.HasCache()
        ? UIAssets.GetCommandButtonSkins(SkinSetForPanelCategory((int)mState.CommandPanel.PanelCategory))
        : nullptr;
    const auto* NormalButtonSkin = FindPreferredButtonSkin(ButtonSkins, 0);
    mState.Selection.LastHovered = CheckCommandPanelClick((int)Core::Platform::GetMouseX(),
                                          (int)Core::Platform::GetMouseY(),
                                          ViewportWidth, ViewportHeight);

    for (int I = 0; I < 9; ++I)
    {
        const RectI& ButtonRect = Layout.CommandRects[(size_t)I];
        const auto& Button = mState.CommandPanel.ButtonDefs[(size_t)I];
        bool HasCmd = !Button.Label.empty();
        bool Enabled = Button.Enabled;
        const bool Hovered = I == mState.Selection.LastHovered;
        const bool Active = I == mState.ActiveCommand && HasCmd;
        const Render::Color SkinTint = SkinTintForState(HasCmd, Enabled, Hovered, Active);
        const auto* IconMask = GetButtonMaskImage(Button);
        const auto* Icon = GetButtonImage(Button);
        if (NormalButtonSkin && NormalButtonSkin->Valid())
        {
            D.DrawSpriteFromImage(*NormalButtonSkin,
                                  (float)ButtonRect.X, (float)ButtonRect.Y,
                                  (float)ButtonRect.W, (float)ButtonRect.H,
                                  SkinTint);
        }
        else
        {
            D.DrawRect((float)ButtonRect.X, (float)ButtonRect.Y,
                       (float)ButtonRect.W, (float)ButtonRect.H,
                       HasCmd
                           ? (Enabled ? Render::Color(38, 56, 88, 220) : Render::Color(35, 35, 45, 200))
                           : Render::Color(20, 20, 40, 100));
        }

        if (HasCmd && IconMask && IconMask->Valid())
        {
            const RectI IconRect = ComputeCommandIconRect(ButtonRect);
            DrawContainedImage(*IconMask, IconRect, IconTintForState(Button, Enabled, Hovered, Active));
        }
        else if (HasCmd && Icon && Icon->Valid())
        {
            const RectI IconRect = ComputeCommandIconRect(ButtonRect);
            DrawContainedImage(*Icon, IconRect, FallbackIconTintForState(Enabled, Active));
        }

        const bool HasVisualIcon = (IconMask && IconMask->Valid()) || (Icon && Icon->Valid());
        const bool DrawLabel = HasCmd && !HasVisualIcon;
        if (DrawLabel)
        {
            D.DrawText(Ellipsize(Button.Label, 10),
                       (float)ButtonRect.X + 4.0f,
                       (float)ButtonRect.Y + (float)ButtonRect.H - 14.0f,
                       1.0f,
                       Enabled ? Render::Color(220, 220, 220, 255) : Render::Color(170, 170, 170, 220));
        }
    }
}

} // namespace Game::Client::UI
