#include "UI/Hud.h"
#include "UI/HudLayout.h"

#include "Core/ECS/EntityManager.h"
namespace Game::Client::UI
{


void Hud::Draw(Core::ECS::EntityManager& Entities,
               const std::vector<int>& SelectedIds,
               int MapWidthTiles, int MapHeightTiles,
               int VW, int VH)
{
    const auto& UIAssets = mUIAssets;
    const auto AggIcons = UIAssets.GetHudAggIcons();
    const auto Layout = ResolveHudLayout(VW, VH);

    const bool HasModularConsole = DrawConsoleChrome(Layout, VW);

    DrawCommandPanel(Layout, VW, VH);

    DrawResourceBar(VW, AggIcons);

    mState.Selection.SelectedCount = (int)SelectedIds.size();

    DrawInfoPanel(Entities, Layout, HasModularConsole, AggIcons);

    DrawMinimapOverlay(Entities, Layout, MapWidthTiles, MapHeightTiles);
}

} // namespace Game::Client::UI

// === Consolidated HudConsole ===
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

        if (ConsoleTimeImg.Valid() && ConsoleTimeImg.Width > 0 && ConsoleTimeImg.Height > 0)
        {
            const float TimeScale = BaseScale * 1.05f;
            const int BadgeW = std::max(60, (int)std::lround((float)ConsoleTimeImg.Width * TimeScale));
            const int BadgeH = std::max(22, (int)std::lround((float)ConsoleTimeImg.Height * TimeScale));
            const int BadgeX = CenterOuterRect.X + (CenterOuterRect.W - BadgeW) / 2;
            const int BadgeY = CenterOuterRect.Y - BadgeH / 2 + 1;

            D.DrawSpriteFromImage(ConsoleTimeImg,
                                  (float)BadgeX, (float)BadgeY,
                                  (float)BadgeW, (float)BadgeH,
                                  Render::Colors::White());

            const int TotalSeconds = std::max(0, (int)mState.MatchElapsedSeconds);
            const int Minutes = TotalSeconds / 60;
            const int Seconds = TotalSeconds % 60;
            char TimeStr[16];
            std::snprintf(TimeStr, sizeof(TimeStr), "%02d:%02d", Minutes, Seconds);

            const float TextX = (float)BadgeX + (float)BadgeW * 0.5f - 18.0f;
            const float TextY = (float)BadgeY + (float)BadgeH * 0.5f - 6.0f;
            D.DrawText(TimeStr, TextX, TextY, 1.05f, Render::Color(245, 245, 255, 255));
        }
        else
        {
            DrawPlaque(ConsoleTimeImg, CenterOuterRect, false);
        }
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

// === Consolidated HudInput ===
#include <cstddef>
#include <sstream>

#include "Core/Utils/InputDebug.h"
#include "UI/HudMinimapGeometry.h"

namespace Game::Client::UI
{

bool Hud::CheckMinimapClick(int SX, int SY,
                            int VW, int VH,
                            int MW, int MH,
                            float& OutWX, float& OutWY) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    const bool Hit = MinimapScreenToWorld(Layout.MinimapRect, MW, MH, SX, SY, OutWX, OutWY);
    if (Core::Utils::InputDebugEnabled())
    {
        std::ostringstream Stream;
        Stream << "screen=" << SX << "," << SY
               << " viewport=" << VW << "x" << VH
               << " map=" << MW << "x" << MH
               << " rect=" << Layout.MinimapRect.X << "," << Layout.MinimapRect.Y
               << "," << Layout.MinimapRect.W << "," << Layout.MinimapRect.H
               << " hit=" << (Hit ? 1 : 0);
        if (Hit)
            Stream << " world=" << OutWX << "," << OutWY;
        Core::Utils::InputDebugLog("InputHudMinimap", Stream.str());
    }
    return Hit;
}

int Hud::CheckCommandPanelClick(int SX, int SY, int VW, int VH) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    for (std::size_t Index = 0; Index < Layout.CommandRects.size(); ++Index)
    {
        if (Layout.CommandRects[Index].Contains(SX, SY))
            return (int)Index;
    }
    return -1;
}

int Hud::CheckQueueSlotClick(int SX, int SY) const
{
    for (const auto& Hit : mState.InfoPanel.QueueSlotHits)
    {
        if (SX >= Hit.X && SX < Hit.X + Hit.W &&
            SY >= Hit.Y && SY < Hit.Y + Hit.H)
        {
            return Hit.QueueIndex;
        }
    }
    return -1;
}

int Hud::CheckMultiSelectSlotClick(int SX, int SY, int& OutEntityId) const
{
    for (const auto& Hit : mState.InfoPanel.MultiSelectSlotHits)
    {
        if (SX >= Hit.X && SX < Hit.X + Hit.W &&
            SY >= Hit.Y && SY < Hit.Y + Hit.H)
        {
            OutEntityId = Hit.EntityId;
            return Hit.SlotIndex;
        }
    }
    OutEntityId = -1;
    return -1;
}

int Hud::CheckMinimapButtonClick(int SX, int SY, int VW, int VH) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    for (int I = 0; I < 3; ++I)
    {
        if (Layout.MinimapButtonRects[I].Contains(SX, SY))
            return I;
    }
    return -1;
}

void Hud::ToggleMinimapTerrain()
{
    mState.Minimap.ShowTerrain = !mState.Minimap.ShowTerrain;
}

void Hud::ToggleMinimapAllianceColors()
{
    mState.Minimap.UseAllianceColors = !mState.Minimap.UseAllianceColors;
}

void Hud::ToggleMinimapBeacons()
{
    mState.Minimap.ShowBeacons = !mState.Minimap.ShowBeacons;
}

bool Hud::IsPointInHudBand(int, int SY, int VW, int VH) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    return SY >= Layout.HudTop || SY < kResourceBarHeight;
}

int Hud::GetHudHeight(int VW, int VH) const
{
    return ResolveHudLayout(VW, VH).HudHeight;
}

} // namespace Game::Client::UI

// === Consolidated HudCommandPanelRender ===
#include "Core/Platform/Platform.h"
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
        const bool IsTransportCommand =
            Button.CommandId == HudCommandId::Load ||
            Button.CommandId == HudCommandId::UnloadAll;
        const bool DrawLabel = HasCmd && (!HasVisualIcon || IsTransportCommand);
        if (DrawLabel)
        {
            if (HasVisualIcon && IsTransportCommand)
            {
                D.DrawRect((float)ButtonRect.X + 2.0f,
                           (float)ButtonRect.Y + (float)ButtonRect.H - 17.0f,
                           (float)ButtonRect.W - 4.0f, 15.0f,
                           Render::Color(0, 0, 0, 180));
            }
            D.DrawText(Ellipsize(Button.Label, 10),
                       (float)ButtonRect.X + 4.0f,
                       (float)ButtonRect.Y + (float)ButtonRect.H - 14.0f,
                       1.0f,
                       Enabled ? Render::Color(220, 220, 220, 255) : Render::Color(170, 170, 170, 220));
        }
    }
}

} // namespace Game::Client::UI
