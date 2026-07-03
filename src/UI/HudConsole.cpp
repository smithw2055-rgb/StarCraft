#include "UI/Hud.h"

#include "UI/HudLayout.h"
#include "UI/HudRenderPrimitives.h"

#include <algorithm>
#include <cmath>

#include "Render/Draw2D.h"

namespace Game::Client::UI
{

namespace
{
    constexpr int TerranLedInsetPx = 30;
    constexpr int TerranLedOffsetYPx = 10;
}

bool Hud::DrawConsoleChrome(const HudLayout& Layout, int ViewportWidth)
{
    auto& D = Render::g_Draw2D;
    const auto& UIAssets = mUIAssets;
    auto ConsoleLeftImg = UIAssets.GetUIConsoleLeft();
    auto ConsoleCenterImg = UIAssets.GetUIConsoleCenter();
    auto ConsoleRightImg = UIAssets.GetUIConsoleRight();
    auto ConsolePortraitFrameImg = UIAssets.GetUIConsolePortraitFrame();
    auto ConsoleApmImg = UIAssets.GetUIConsoleApm();
    auto ConsoleTimeImg = UIAssets.GetUIConsoleTime();
    auto ConsoleImg = UIAssets.GetUIConsole();
    auto ConsoleOverImg = UIAssets.GetUIConsoleOver();

    const bool HasModularConsole =
        ConsoleLeftImg.Valid() &&
        ConsoleCenterImg.Valid() &&
        ConsoleRightImg.Valid() &&
        ConsolePortraitFrameImg.Valid();

    if (HasModularConsole)
    {
        const float BaseScale = (float)Layout.ConsoleRect.H /
            (float)std::max(ConsoleLeftImg.Height, ConsoleRightImg.Height);
        const int LeftW = std::max(1, (int)std::lround((float)ConsoleLeftImg.Width * BaseScale));
        const int LeftH = std::max(1, (int)std::lround((float)ConsoleLeftImg.Height * BaseScale));
        const int RightW = std::max(1, (int)std::lround((float)ConsoleRightImg.Width * BaseScale));
        const int RightH = std::max(1, (int)std::lround((float)ConsoleRightImg.Height * BaseScale));
        const RectI LeftBaseRect =
        {
            0,
            Layout.ConsoleRect.Y + Layout.ConsoleRect.H - LeftH,
            LeftW,
            LeftH
        };
        const RectI RightBaseRect =
        {
            ViewportWidth - RightW,
            Layout.ConsoleRect.Y + Layout.ConsoleRect.H - RightH,
            RightW,
            RightH
        };

        D.DrawSpriteFromImage(ConsoleLeftImg,
                              (float)LeftBaseRect.X, (float)LeftBaseRect.Y,
                              (float)LeftBaseRect.W, (float)LeftBaseRect.H,
                              Render::Colors::White());
        D.DrawSpriteFromImage(ConsoleRightImg,
                              (float)RightBaseRect.X, (float)RightBaseRect.Y,
                              (float)RightBaseRect.W, (float)RightBaseRect.H,
                              Render::Colors::White());

        const RectI CenterOuterRect = Layout.InfoFrameRect;
        const RectI PortraitOuterRect = Layout.PortraitFrameRect;

        D.DrawSpriteFromImage(ConsoleCenterImg,
                              (float)CenterOuterRect.X, (float)CenterOuterRect.Y,
                              (float)CenterOuterRect.W, (float)CenterOuterRect.H,
                              Render::Colors::White());
        D.DrawSpriteFromImage(ConsolePortraitFrameImg,
                              (float)PortraitOuterRect.X, (float)PortraitOuterRect.Y,
                              (float)PortraitOuterRect.W, (float)PortraitOuterRect.H,
                              Render::Colors::White());

        auto DrawPlaque = [&](const Resource::Graphics::GpuImage& Image,
                              const RectI& AnchorRect,
                              bool AlignRight)
        {
            if (!Image.Valid() || Image.Width <= 0 || Image.Height <= 0)
                return;

            const int PlaqueH = std::max(24, std::min(42, AnchorRect.H / 5));
            const int PlaqueW = std::max(PlaqueH * 2, (int)std::lround((float)Image.Width * (float)PlaqueH / (float)Image.Height));
            const int X = AlignRight
                ? (AnchorRect.X + AnchorRect.W - PlaqueW - 8 - TerranLedInsetPx)
                : (AnchorRect.X + 8 + TerranLedInsetPx);
            const int Y = AnchorRect.Y - PlaqueH / 3 + TerranLedOffsetYPx;
            D.DrawSpriteFromImage(Image, (float)X, (float)Y, (float)PlaqueW, (float)PlaqueH, Render::Colors::White());
        };

        DrawPlaque(ConsoleTimeImg, CenterOuterRect, false);
        DrawPlaque(ConsoleApmImg, CenterOuterRect, true);
    }
    else if (ConsoleImg.Valid())
    {
        DrawConsoleSlices(ConsoleImg, Layout.ConsoleRect, Render::Colors::White());
        if (ConsoleOverImg.Valid())
            DrawConsoleSlices(ConsoleOverImg, Layout.ConsoleRect, Render::Colors::White());
    }
    else
    {
        D.DrawRect((float)Layout.ConsoleRect.X, (float)Layout.ConsoleRect.Y,
                   (float)Layout.ConsoleRect.W, (float)Layout.ConsoleRect.H,
                   Render::Color(20, 20, 40, 220));
    }

    return HasModularConsole;
}

} // namespace Game::Client::UI
