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


// === Consolidated from src/UI/HudResources.cpp ===
#include "UI/Hud.h"

#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "Core/Utils/Logger.h"
#include "UI/HudText.h"
#include "UI/HudRenderPrimitives.h"
#include "Render/Draw2D.h"
#include "Render/Private/SokolGpuInterop.h"
#include "third_party/sokol/sokol_gfx.h"
namespace Game::Client::UI
{

namespace
{
    constexpr AtlasRect AggIconMinerals = { 0, 0, 16, 16 };
    constexpr AtlasRect AggIconGas = { 17, 0, 16, 16 };
    constexpr AtlasRect AggIconSupply = { 34, 0, 16, 16 };

    using Game::Systems::TerranResearchId;

    int LegacyIconForVisual(HudVisualId VisualId)
    {
        switch (VisualId)
        {
            case HudVisualId::BasicBuild: return -1;
            case HudVisualId::AdvancedBuild: return -1;
            case HudVisualId::Back: return -1;
            default: return -1;
        }
    }

    int CommandIconForVisual(HudVisualId VisualId)
    {
        switch (VisualId)
        {
            case HudVisualId::BasicBuild:
                return IconForDef(Game::Races::GetUnitDefID("CommandCenter"));
            case HudVisualId::AdvancedBuild:
                return IconForDef(Game::Races::GetUnitDefID("Factory"));
            case HudVisualId::Back:
            case HudVisualId::Cancel:
                return 236;
            default:
                return -1;
        }
    }
}

void Hud::Init(Resource::Graphics::UIAssetCacheView UIAssets,
               Resource::Graphics::SpriteAssetCacheView SpriteAssets)
{
    mUIAssets = UIAssets;
    mSpriteAssets = SpriteAssets;
    if (!mUIAssets.HasCache())
        return;

    const auto& UIAssetsView = mUIAssets;
    const bool HasLeft = UIAssetsView.GetUIConsoleLeft().Valid();
    const bool HasCenter = UIAssetsView.GetUIConsoleCenter().Valid();
    const bool HasRight = UIAssetsView.GetUIConsoleRight().Valid();
    const bool HasPortrait = UIAssetsView.GetUIConsolePortraitFrame().Valid();
    const bool HasButtons = UIAssetsView.GetCommandButtonSkins(
        Resource::Graphics::CommandButtonSkinSet::Terran) != nullptr;

    Core::Utils::Logger::Info("Hud",
        std::string("Terran HUD scene panel: left=") + (HasLeft ? "1" : "0") +
        " center=" + (HasCenter ? "1" : "0") +
        " right=" + (HasRight ? "1" : "0") +
        " portrait=" + (HasPortrait ? "1" : "0") +
        " buttons=" + (HasButtons ? "1" : "0"));
}

void Hud::Shutdown()
{
    if (mMinimapTexture.View.id)
    {
        sg_destroy_view(Render::ToSokol(mMinimapTexture.View));
        mMinimapTexture.View = {};
    }
    if (mMinimapTexture.Image.id)
    {
        sg_destroy_image(Render::ToSokol(mMinimapTexture.Image));
        mMinimapTexture.Image = {};
    }
    mMinimapPixels.clear();
    mBaseTerrainColors.clear();
    mMinimapTexWidth = 0;
    mMinimapTexHeight = 0;
    mMinimapTilesetName.clear();
    mViewModel.UnbindFromWorld();
}

const Resource::Graphics::GpuImage* Hud::GetLegacyIcon(int IconIndex) const
{
    if (!mUIAssets.HasCache())
        return nullptr;
    const auto* Icons = mUIAssets.GetUIIcons();
    if (!Icons || IconIndex < 0 || IconIndex >= (int)Icons->size())
        return nullptr;
    return &(*Icons)[IconIndex];
}

std::string Hud::GetLegacyIconUri(int IconIndex) const
{
    if (!mUIAssets.HasCache())
        return {};
    const auto* IconUris = mUIAssets.GetUIIconUris();
    if (!IconUris || IconIndex < 0 || IconIndex >= (int)IconUris->size())
        return {};
    return (*IconUris)[(size_t)IconIndex];
}

const Resource::Graphics::GpuImage* Hud::GetCommandIcon(int IconIndex) const
{
    if (!mUIAssets.HasCache())
        return nullptr;
    const auto* Icons = mUIAssets.GetCommandIcons();
    if (!Icons || IconIndex < 0 || IconIndex >= (int)Icons->size())
        return nullptr;
    return &(*Icons)[IconIndex];
}

const Resource::Graphics::GpuImage* Hud::GetCommandIconMask(int IconIndex) const
{
    if (!mUIAssets.HasCache())
        return nullptr;
    const auto* Icons = mUIAssets.GetCommandIconMasks();
    if (!Icons || IconIndex < 0 || IconIndex >= (int)Icons->size())
        return nullptr;
    return &(*Icons)[IconIndex];
}

const Resource::Graphics::GpuImage* Hud::GetFirstValidSpriteFrame(int ChkUnitId) const
{
    if (!mSpriteAssets.HasCache() || ChkUnitId < 0)
        return nullptr;
    const auto* Frames = mSpriteAssets.GetSpriteFramesForChkUnit(ChkUnitId);
    if (!Frames || Frames->empty())
        return nullptr;

    for (const auto& Frame : *Frames)
    {
        if (Frame.Valid())
            return &Frame;
    }
    return nullptr;
}

const Resource::Graphics::GpuImage* Hud::GetSelectionPreviewImage() const
{
    if (mState.InfoPanel.PreviewChkUnitId >= 0 && mUIAssets.HasCache())
    {
        mUIAssets.EnsureWireframeFrame(mState.InfoPanel.PreviewChkUnitId);
        if (const auto* Wireframes = mUIAssets.GetWireframeFrames();
            Wireframes &&
            mState.InfoPanel.PreviewChkUnitId < (int)Wireframes->size() &&
            (*Wireframes)[(size_t)mState.InfoPanel.PreviewChkUnitId].Valid())
        {
            return &(*Wireframes)[(size_t)mState.InfoPanel.PreviewChkUnitId];
        }
    }

    if (mState.InfoPanel.PreviewDefID >= 0)
    {
        const auto Def = Game::Races::GetUnitDefByID(mState.InfoPanel.PreviewDefID);
        if (const auto* Icon = GetCommandIcon(IconForDef(mState.InfoPanel.PreviewDefID));
            Icon && Icon->Valid() && Def.Name != "Unknown")
        {
            return Icon;
        }
    }
    return GetFirstValidSpriteFrame(mState.InfoPanel.PreviewChkUnitId);
}

const Resource::Graphics::GpuImage* Hud::GetButtonImage(const CmdButtonDef& Button) const
{
    if (const int VisualCommandIcon = CommandIconForVisual(Button.VisualId);
        VisualCommandIcon >= 0)
    {
        if (const auto* Icon = GetCommandIcon(VisualCommandIcon); Icon && Icon->Valid())
            return Icon;
    }

    if (const auto* Icon = GetLegacyIcon(LegacyIconForVisual(Button.VisualId)); Icon && Icon->Valid())
        return Icon;

    if (Button.CommandId != HudCommandId::None)
    {
        if (const auto* Icon = GetCommandIcon(CommandIconForCommand(Button.CommandId));
            Icon && Icon->Valid())
        {
            return Icon;
        }
    }

    if (Button.ResearchId != TerranResearchId::None)
    {
        if (const auto* Icon = GetCommandIcon(CommandIconForResearch(Button.ResearchId));
            Icon && Icon->Valid())
        {
            return Icon;
        }
    }

    if (Button.TargetUnitDefID >= 0)
    {
        const auto Def = Game::Races::GetUnitDefByID(Button.TargetUnitDefID);
        if (Def.Name != "Unknown")
        {
            if (const auto* Icon = GetCommandIcon(IconForDef(Button.TargetUnitDefID));
                Icon && Icon->Valid())
            {
                return Icon;
            }
        }

        if (const auto* Icon = GetFirstValidSpriteFrame(Def.ChkUnitID); Icon && Icon->Valid())
            return Icon;
    }

    if (const auto* Icon = GetLegacyIcon(Button.IconIndex); Icon && Icon->Valid())
        return Icon;

    return nullptr;
}

const Resource::Graphics::GpuImage* Hud::GetButtonMaskImage(const CmdButtonDef& Button) const
{
    if (const int VisualCommandIcon = CommandIconForVisual(Button.VisualId);
        VisualCommandIcon >= 0)
    {
        if (const auto* Icon = GetCommandIconMask(VisualCommandIcon); Icon && Icon->Valid())
            return Icon;
    }

    if (Button.CommandId != HudCommandId::None)
    {
        if (const auto* Icon = GetCommandIconMask(CommandIconForCommand(Button.CommandId));
            Icon && Icon->Valid())
        {
            return Icon;
        }
    }

    if (Button.ResearchId != TerranResearchId::None)
    {
        if (const auto* Icon = GetCommandIconMask(CommandIconForResearch(Button.ResearchId));
            Icon && Icon->Valid())
        {
            return Icon;
        }
    }

    if (Button.TargetUnitDefID >= 0)
    {
        const auto Def = Game::Races::GetUnitDefByID(Button.TargetUnitDefID);
        if (Def.Name != "Unknown")
        {
            if (const auto* Icon = GetCommandIconMask(IconForDef(Button.TargetUnitDefID));
                Icon && Icon->Valid())
            {
                return Icon;
            }
        }
    }

    return nullptr;
}

std::string Hud::GetButtonIconUri(const CmdButtonDef& Button) const
{
    const auto* IconUris = mUIAssets.GetCommandIconUris();
    auto ResolveUri = [&](int IconIndex) -> std::string
    {
        if (!IconUris || IconIndex < 0 || IconIndex >= (int)IconUris->size())
            return {};
        return (*IconUris)[(size_t)IconIndex];
    };

    if (const int VisualCommandIcon = CommandIconForVisual(Button.VisualId);
        VisualCommandIcon >= 0)
    {
        if (auto Uri = ResolveUri(VisualCommandIcon); !Uri.empty())
            return Uri;
    }

    if (Button.CommandId != HudCommandId::None)
    {
        if (auto Uri = ResolveUri(CommandIconForCommand(Button.CommandId)); !Uri.empty())
            return Uri;
    }

    if (Button.ResearchId != TerranResearchId::None)
    {
        if (auto Uri = ResolveUri(CommandIconForResearch(Button.ResearchId)); !Uri.empty())
            return Uri;
    }

    if (Button.TargetUnitDefID >= 0)
    {
        const auto Def = Game::Races::GetUnitDefByID(Button.TargetUnitDefID);
        if (Def.Name != "Unknown")
        {
            if (auto Uri = ResolveUri(IconForDef(Button.TargetUnitDefID)); !Uri.empty())
                return Uri;
        }
    }

    return ResolveUri(Button.IconIndex);
}

std::string Hud::GetUnitIconUriForChkUnitId(int ChkUnitId) const
{
    const int DefId = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitId);
    if (DefId < 0)
        return {};

    const auto Def = Game::Races::GetUnitDefByID(DefId);
    if (Def.Name == "Unknown")
        return {};

    const auto* IconUris = mUIAssets.GetCommandIconUris();
    const int IconIndex = IconForDef(DefId);
    if (!IconUris || IconIndex < 0 || IconIndex >= (int)IconUris->size())
        return {};

    return (*IconUris)[(size_t)IconIndex];
}

void Hud::DrawResourceBar(int ViewportWidth, const Resource::Graphics::GpuImage& AggIcons)
{
    auto& D = Render::g_Draw2D;

    auto DrawResourceEntry = [&](int RightX,
                                 const Resource::Graphics::GpuImage* HdIcon,
                                 const AtlasRect& IconRect,
                                 const char* Value,
                                 const char* FallbackLabel,
                                 Render::Color FallbackTint,
                                 Render::Color TextColor) -> int
    {
        const int IconSize = 18;
        const float TextSize = 12.0f;
        const int ValueChars = (int)std::strlen(Value);
        const int TextW = std::max(36, ValueChars * 9 + 4);
        const int EntryW = IconSize + 8 + TextW + 8;
        const int X = RightX - EntryW;
        const int Y = 4;
        const int BarH = 22;

        // Semi-transparent dark metallic container with sleek border (SC:R style)
        D.DrawRect((float)X, (float)Y, (float)EntryW, (float)BarH,
                   Render::Color(10, 14, 20, 215));
        D.DrawRectLines((float)X, (float)Y, (float)EntryW, (float)BarH,
                        Render::Color(45, 75, 105, 200), 1.0f);

        // Icon
        const int IconX = X + 5;
        const int IconY = Y + 2;
        if (HdIcon && HdIcon->Valid())
        {
            DrawContainedImage(*HdIcon, { IconX, IconY, IconSize, IconSize }, Render::Colors::White());
        }
        else if (AggIcons.Valid())
        {
            DrawAtlasImage(AggIcons, IconRect, { IconX, IconY, IconSize, IconSize }, Render::Colors::White());
        }
        else
        {
            D.DrawRect((float)IconX, (float)IconY, (float)IconSize, (float)IconSize, Render::Color(18, 22, 30, 230));
            D.DrawRectLines((float)IconX, (float)IconY, (float)IconSize, (float)IconSize, FallbackTint, 1.0f);
            D.DrawText(FallbackLabel, (float)IconX + 4.0f, (float)IconY + 3.0f, 9.0f, FallbackTint);
        }

        // Text with drop shadow
        const float TextX = (float)IconX + (float)IconSize + 6.0f;
        const float TextY = (float)Y + 4.0f;
        D.DrawText(Value, TextX + 1.0f, TextY + 1.0f, TextSize, Render::Color(0, 0, 0, 230));
        D.DrawText(Value, TextX, TextY, TextSize, TextColor);
        return X - 6;
    };

    mViewModel.SetResourceState(mState.Resources);
    const char* MineralBuf = mViewModel.FormattedMineralText.Get().c_str();
    const char* GasBuf = mViewModel.FormattedGasText.Get().c_str();
    const char* SupplyBuf = mViewModel.FormattedSupplyText.Get().c_str();

    const auto* MineralIcon = GetLegacyIcon(0);
    const auto* GasIcon = GetLegacyIcon(2);
    const auto* SupplyIcon = GetLegacyIcon(3);

    // Ample margin from right edge to prevent any clipping on any resolution
    int ResourceRight = ViewportWidth - 20;
    ResourceRight = DrawResourceEntry(ResourceRight, SupplyIcon, AggIconSupply, SupplyBuf, "S",
                                      Render::Color(220, 210, 120, 255), Render::Color(255, 235, 140, 255));
    ResourceRight = DrawResourceEntry(ResourceRight, GasIcon, AggIconGas, GasBuf, "G",
                                      Render::Color(84, 208, 168, 255), Render::Color(105, 245, 165, 255));
    DrawResourceEntry(ResourceRight, MineralIcon, AggIconMinerals, MineralBuf, "M",
                      Render::Color(96, 180, 255, 255), Render::Color(215, 240, 255, 255));
}

} // namespace Game::Client::UI





// === Consolidated from src/UI/HudMinimap.cpp ===
#include "UI/Hud.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "Client/Entity.h"
#include "Core/Concurrency/JobSystem.h"
#include "Core/Platform/Platform.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Render/Draw2D.h"
#include "Render/Private/SokolGpuInterop.h"
#include "third_party/sokol/sokol_gfx.h"
#include "UI/HudLayout.h"
#include "UI/HudMinimapGeometry.h"

namespace Game::Client::UI
{

namespace
{

struct TerrainPalette
{
    uint32_t LowWalkable;
    uint32_t LowDirt;
    uint32_t MidGround;
    uint32_t HighGround;
    uint32_t Cliff;
    uint32_t WaterOrVoid;
};

TerrainPalette GetPaletteForTileset(const std::string& TilesetName)
{
    std::string Lower = TilesetName;
    for (char& C : Lower)
        C = (char)::tolower((unsigned char)C);

    if (Lower.find("space") != std::string::npos)
    {
        return { 0xFF4B4036, 0xFF3C322B, 0xFF584E43, 0xFF665C50, 0xFF2C2420, 0xFF0D0A07 };
    }
    if (Lower.find("install") != std::string::npos)
    {
        return { 0xFF64605C, 0xFF524E4A, 0xFF726E6A, 0xFF7A7672, 0xFF2C2824, 0xFF1C1814 };
    }
    if (Lower.find("ash") != std::string::npos)
    {
        return { 0xFF243048, 0xFF2C3852, 0xFF303E60, 0xFF3A4B75, 0xFF181C28, 0xFF122C8A };
    }
    if (Lower.find("jungle") != std::string::npos)
    {
        return { 0xFF244820, 0xFF28423B, 0xFF2E5E2B, 0xFF357031, 0xFF242820, 0xFF42351A };
    }
    if (Lower.find("desert") != std::string::npos)
    {
        return { 0xFF487085, 0xFF3E5A6E, 0xFF527B94, 0xFF52829B, 0xFF263442, 0xFF1C3045 };
    }
    if (Lower.find("ice") != std::string::npos)
    {
        return { 0xFF96827B, 0xFF8A786E, 0xFF9E9089, 0xFFA89E96, 0xFF453E37, 0xFF3E2A1C };
    }
    if (Lower.find("twilight") != std::string::npos)
    {
        return { 0xFF4B373E, 0xFF52414B, 0xFF5C4852, 0xFF66525C, 0xFF22181B, 0xFF301C22 };
    }
    // Default: badlands
    return { 0xFF28502E, 0xFF344E62, 0xFF3D6073, 0xFF396530, 0xFF252830, 0xFF542F1B };
}

} // namespace

void Hud::SetMinimapState(float CamX, float CamY, float CamZoom,
                          int VpWidth, int VpHeight,
                          int MapW, int MapH,
                          const int*, const bool*, const bool*)
{
    mState.Minimap.CameraX = CamX;
    mState.Minimap.CameraY = CamY;
    mState.Minimap.CameraZoom = CamZoom;
    mState.Minimap.ViewportWidth = VpWidth;
    mState.Minimap.ViewportHeight = VpHeight;
    mState.Minimap.MapWidth = MapW;
    mState.Minimap.MapHeight = MapH;
}

void Hud::SetMinimapViewportRect(float CamWorldX, float CamWorldY, float CamWorldW, float CamWorldH)
{
    mState.Minimap.CamVisibleX = CamWorldX;
    mState.Minimap.CamVisibleY = CamWorldY;
    mState.Minimap.CamVisibleW = CamWorldW;
    mState.Minimap.CamVisibleH = CamWorldH;
}

void Hud::RebuildBaseTerrainColors(int MapW, int MapH,
                                   const Game::Map::GameMap* EcsMap,
                                   const Game::Client::GameMap* ClientMap)
{
    const size_t TileCount = (size_t)MapW * (size_t)MapH;
    mBaseTerrainColors.resize(TileCount);

    const std::string TilesetName = ClientMap ? ClientMap->TilesetName : "";
    const TerrainPalette Pal = GetPaletteForTileset(TilesetName);

    for (int Y = 0; Y < MapH; ++Y)
    {
        for (int X = 0; X < MapW; ++X)
        {
            const size_t Idx = (size_t)Y * (size_t)MapW + (size_t)X;
            uint32_t Color = Pal.LowWalkable;

            if (EcsMap && EcsMap->IsInBounds(X, Y))
            {
                const auto& Tile = EcsMap->GetTile(X, Y);
                if (Tile.Terrain == Game::Components::TerrainType::Water)
                {
                    Color = Pal.WaterOrVoid;
                }
                else if (!Tile.Walkable && !Tile.Buildable)
                {
                    Color = (Tile.GroundHeight == 0 && Pal.WaterOrVoid != Pal.Cliff) ? Pal.WaterOrVoid : Pal.Cliff;
                }
                else if (Tile.GroundHeight >= 2)
                {
                    Color = Pal.HighGround;
                }
                else if (Tile.GroundHeight == 1)
                {
                    Color = Pal.MidGround;
                }
                else
                {
                    Color = (Tile.Terrain == Game::Components::TerrainType::Dirt) ? Pal.LowDirt : Pal.LowWalkable;
                }
            }
            else if (ClientMap && !ClientMap->Walkable.empty() && Idx < ClientMap->Walkable.size())
            {
                if (ClientMap->Walkable[Idx] == 0)
                    Color = Pal.Cliff;
                else
                    Color = Pal.LowWalkable;
            }

            uint16_t MegaTileID = 0;
            if (ClientMap && !ClientMap->MegaTileIds.empty() && Idx < ClientMap->MegaTileIds.size())
                MegaTileID = ClientMap->MegaTileIds[Idx];

            const int Var = (int)(((MegaTileID * 13u + (uint32_t)X * 7u + (uint32_t)Y * 11u) % 15u)) - 7;
            const uint8_t R = (uint8_t)std::clamp((int)(Color & 0xFF) + Var, 0, 255);
            const uint8_t G = (uint8_t)std::clamp((int)((Color >> 8) & 0xFF) + Var, 0, 255);
            const uint8_t B = (uint8_t)std::clamp((int)((Color >> 16) & 0xFF) + Var, 0, 255);

            mBaseTerrainColors[Idx] = 0xFF000000u | ((uint32_t)B << 16) | ((uint32_t)G << 8) | (uint32_t)R;
        }
    }
}

void Hud::UpdateMinimapTerrain(const Game::Map::GameMap* EcsMap,
                               const Game::Client::GameMap* ClientMap,
                               const Game::Systems::FogOfWarSystem* Fog,
                               int LocalPlayerID)
{
    const int MapW = ClientMap ? ClientMap->Width : (EcsMap ? EcsMap->GetWidth() : 0);
    const int MapH = ClientMap ? ClientMap->Height : (EcsMap ? EcsMap->GetHeight() : 0);
    if (MapW <= 0 || MapH <= 0)
        return;

    const std::string TilesetName = ClientMap ? ClientMap->TilesetName : "";
    const double Now = Core::Platform::GetAbsoluteTime();
    const bool TextureInvalid = !mMinimapTexture.Valid() ||
                                mMinimapTexWidth != MapW ||
                                mMinimapTexHeight != MapH ||
                                mMinimapTilesetName != TilesetName ||
                                mBaseTerrainColors.size() != (size_t)(MapW * MapH);

    const uint32_t CurrentFogRev = Fog ? Fog->GetRevision() : 0;
    const bool FogOrPlayerChanged = (CurrentFogRev != mLastFogRevision) || (LocalPlayerID != mLastLocalPlayerID);
    const bool ThrottleElapsed = (mLastMinimapTerrainTime < 0.0) || (Now - mLastMinimapTerrainTime >= 0.066);

    if (!TextureInvalid && (!FogOrPlayerChanged || !ThrottleElapsed))
    {
        return;
    }

    mLastFogRevision = CurrentFogRev;
    mLastLocalPlayerID = LocalPlayerID;
    mLastMinimapTerrainTime = Now;

    if (TextureInvalid)
    {
        RebuildBaseTerrainColors(MapW, MapH, EcsMap, ClientMap);
        mMinimapTexWidth = MapW;
        mMinimapTexHeight = MapH;
        mMinimapTilesetName = TilesetName;

        if (mMinimapTexture.View.id)
        {
            sg_destroy_view(Render::ToSokol(mMinimapTexture.View));
            mMinimapTexture.View = {};
        }
        if (mMinimapTexture.Image.id)
        {
            sg_destroy_image(Render::ToSokol(mMinimapTexture.Image));
            mMinimapTexture.Image = {};
        }
    }

    const size_t TileCount = (size_t)MapW * (size_t)MapH;
    if (mMinimapPixels.size() != TileCount)
        mMinimapPixels.resize(TileCount);

    Core::Concurrency::JobSystem::Instance().ParallelForRange(
        static_cast<uint32_t>(MapH),
        32u,
        [&](uint32_t BeginY, uint32_t EndY, uint32_t /*ThreadIdx*/)
        {
            for (uint32_t Y = BeginY; Y < EndY; ++Y)
            {
                for (int X = 0; X < MapW; ++X)
                {
                    const size_t Idx = (size_t)Y * (size_t)MapW + (size_t)X;
                    uint32_t Color = mBaseTerrainColors[Idx];

                    const bool HasCreep = (EcsMap && EcsMap->IsInBounds(X, (int)Y) && EcsMap->GetTile(X, (int)Y).HasCreep) ||
                                          (ClientMap && !ClientMap->Creep.empty() && Idx < ClientMap->Creep.size() && ClientMap->Creep[Idx]);
                    if (HasCreep)
                    {
                        Color = 0xFF80286Eu; // Creep purple
                    }

                    if (Fog)
                    {
                        const auto FogState = Fog->GetFog(X, (int)Y, LocalPlayerID);
                        if (FogState == Game::Systems::FogState::Unexplored)
                        {
                            mMinimapPixels[Idx] = 0xFF000000u; // Solid black
                        }
                        else if (FogState == Game::Systems::FogState::Explored)
                        {
                            const uint8_t R = (uint8_t)(((Color) & 0xFF) * 42 / 100);
                            const uint8_t G = (uint8_t)(((Color >> 8) & 0xFF) * 42 / 100);
                            const uint8_t B = (uint8_t)(((Color >> 16) & 0xFF) * 42 / 100);
                            mMinimapPixels[Idx] = 0xFF000000u | ((uint32_t)B << 16) | ((uint32_t)G << 8) | (uint32_t)R;
                        }
                        else
                        {
                            mMinimapPixels[Idx] = Color;
                        }
                    }
                    else
                    {
                        mMinimapPixels[Idx] = Color;
                    }
                }
            }
        });

    if (!sg_isvalid())
        return;

    if (!mMinimapTexture.Valid())
    {
        sg_image_desc Desc = {};
        Desc.width = MapW;
        Desc.height = MapH;
        Desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        Desc.usage.stream_update = true;
        Desc.data.mip_levels[0] = { mMinimapPixels.data(), mMinimapPixels.size() * sizeof(uint32_t) };
        Desc.label = "minimap-terrain";
        mMinimapTexture.Image = Render::FromSokol(sg_make_image(&Desc));

        sg_view_desc VDesc = {};
        VDesc.texture.image = Render::ToSokol(mMinimapTexture.Image);
        VDesc.label = "minimap-terrain-view";
        mMinimapTexture.View = Render::FromSokol(sg_make_view(&VDesc));
    }
    else
    {
        sg_image_data Data = {};
        Data.mip_levels[0] = { mMinimapPixels.data(), mMinimapPixels.size() * sizeof(uint32_t) };
        sg_update_image(Render::ToSokol(mMinimapTexture.Image), &Data);
    }
}

void Hud::DrawMinimapOverlay(Core::ECS::EntityManager& Entities,
                             const HudLayout& Layout,
                             int MapWidthTiles,
                             int MapHeightTiles)
{
    (void)Entities;
    auto& D = Render::g_Draw2D;
    D.DrawRectLines((float)Layout.MinimapRect.X - 1, (float)Layout.MinimapRect.Y - 1,
                    (float)Layout.MinimapRect.W + 2, (float)Layout.MinimapRect.H + 2,
                    Render::Color(100, 100, 120, 150), 1.0f);
    const RectI ContentRect = ResolveMinimapContentRect(
        Layout.MinimapRect, MapWidthTiles, MapHeightTiles);

    const float MapPxW = (float)(MapWidthTiles * 32);
    const float MapPxH = (float)(MapHeightTiles * 32);
    if (MapPxW <= 0.0f || MapPxH <= 0.0f)
        return;

    // 1. Draw terrain + Fog of War background
    if (mState.Minimap.ShowTerrain && mMinimapTexture.Valid())
    {
        D.DrawSprite(mMinimapTexture,
                     (float)ContentRect.X,
                     (float)ContentRect.Y,
                     (float)ContentRect.W,
                     (float)ContentRect.H);
    }
    else
    {
        D.DrawRect((float)ContentRect.X,
                   (float)ContentRect.Y,
                   (float)ContentRect.W,
                   (float)ContentRect.H,
                   Render::Color(0, 0, 0, 255));
    }

    // 2. Draw unit / building / resource dots
    if (!mState.Minimap.UnitDots.empty())
    {
        for (const auto& Dot : mState.Minimap.UnitDots)
        {
            const int PX = MinimapWorldToScreenX(ContentRect, Dot.Position.X, MapPxW);
            const int PY = MinimapWorldToScreenY(ContentRect, Dot.Position.Y, MapPxH);
            const uint8_t A = (uint8_t)((Dot.Color >> 24) & 0xFF);
            const uint8_t R = (uint8_t)((Dot.Color >> 16) & 0xFF);
            const uint8_t G = (uint8_t)((Dot.Color >> 8) & 0xFF);
            const uint8_t B = (uint8_t)(Dot.Color & 0xFF);
            const Render::Color DotColor(R, G, B, A ? A : 255);
            const float W = (float)std::max((uint8_t)1, Dot.Width);
            const float H = (float)std::max((uint8_t)1, Dot.Height);
            D.DrawRect((float)PX - W * 0.5f, (float)PY - H * 0.5f, W, H, DotColor);
        }
    }

    // 3. Draw Camera Viewport outline
    if (mState.Minimap.CamVisibleW > 0.0f && mState.Minimap.CamVisibleH > 0.0f)
    {
        const int X0 = MinimapWorldToScreenX(ContentRect, mState.Minimap.CamVisibleX, MapPxW);
        const int Y0 = MinimapWorldToScreenY(ContentRect, mState.Minimap.CamVisibleY, MapPxH);
        const int X1 = MinimapWorldToScreenX(ContentRect, mState.Minimap.CamVisibleX + mState.Minimap.CamVisibleW, MapPxW);
        const int Y1 = MinimapWorldToScreenY(ContentRect, mState.Minimap.CamVisibleY + mState.Minimap.CamVisibleH, MapPxH);

        const int MinX = std::max(ContentRect.X, std::min(X0, X1));
        const int MinY = std::max(ContentRect.Y, std::min(Y0, Y1));
        const int MaxX = std::min(ContentRect.X + ContentRect.W, std::max(X0, X1));
        const int MaxY = std::min(ContentRect.Y + ContentRect.H, std::max(Y0, Y1));

        if (MaxX > MinX && MaxY > MinY)
        {
            D.DrawRectLines((float)MinX, (float)MinY,
                            (float)(MaxX - MinX), (float)(MaxY - MinY),
                            Render::Color(255, 255, 255, 220), 1.0f);
        }
    }

    // 4. Draw Campaign radar pings
    if (mState.Minimap.ShowBeacons)
    {
        for (const auto& Ping : mState.Minimap.CampaignPings)
        {
            int PX = 0;
            int PY = 0;
            if (!MinimapWorldToScreenPoint(ContentRect,
                                           MapWidthTiles,
                                           MapHeightTiles,
                                           Ping.Position.X,
                                           Ping.Position.Y,
                                           PX,
                                           PY))
                continue;
            const float T = std::clamp(Ping.TimeRemaining / 4.0f, 0.0f, 1.0f);
            const float Radius = 4.0f + (1.0f - T) * 9.0f;
            const Render::Color PingColor(80, 210, 255, (uint8_t)(100 + 140 * T));
            D.DrawRectLines((float)PX - Radius,
                            (float)PY - Radius,
                            Radius * 2.0f,
                            Radius * 2.0f,
                            PingColor,
                            1.0f);
            D.DrawRect((float)PX - 1.0f,
                       (float)PY - Radius,
                       2.0f,
                       Radius * 2.0f,
                       PingColor);
            D.DrawRect((float)PX - Radius,
                       (float)PY - 1.0f,
                       Radius * 2.0f,
                       2.0f,
                       PingColor);
        }
    }

    // 5. Draw mode toggle buttons above the minimap
    DrawMinimapButtons(Layout);
}

void Hud::DrawMinimapButtons(const HudLayout& Layout) const
{
    auto& D = Render::g_Draw2D;
    const auto& Btns = Layout.MinimapButtonRects;

    if (Btns[0].W > 0 && Btns[2].W > 0)
    {
        const int BarX = Btns[0].X - 2;
        const int BarY = Btns[0].Y - 2;
        const int BarW = (Btns[2].X + Btns[2].W) - Btns[0].X + 4;
        const int BarH = Btns[0].H + 4;
        D.DrawRect((float)BarX, (float)BarY, (float)BarW, (float)BarH, Render::Color(10, 18, 32, 230));
        D.DrawRectLines((float)BarX, (float)BarY, (float)BarW, (float)BarH, Render::Color(35, 95, 180, 240), 1.0f);
    }

    const int MouseX = (int)Core::Platform::GetMouseX();
    const int MouseY = (int)Core::Platform::GetMouseY();

    for (int I = 0; I < 3; ++I)
    {
        const auto& Rect = Btns[(size_t)I];
        if (Rect.W <= 0)
            continue;

        bool Active = true;
        if (I == 0) Active = mState.Minimap.ShowTerrain;
        else if (I == 1) Active = mState.Minimap.UseAllianceColors;
        else if (I == 2) Active = mState.Minimap.ShowBeacons;

        const bool Hovered = Rect.Contains(MouseX, MouseY);

        const Render::Color BgCol = Active
            ? (Hovered ? Render::Color(35, 65, 110, 250) : Render::Color(20, 42, 75, 230))
            : (Hovered ? Render::Color(25, 30, 42, 230) : Render::Color(14, 18, 26, 210));
        const Render::Color BorderCol = Hovered
            ? Render::Color(90, 190, 255, 255)
            : (Active ? Render::Color(50, 130, 230, 220) : Render::Color(25, 55, 90, 160));

        D.DrawRect((float)Rect.X, (float)Rect.Y, (float)Rect.W, (float)Rect.H, BgCol);
        D.DrawRectLines((float)Rect.X, (float)Rect.Y, (float)Rect.W, (float)Rect.H, BorderCol, 1.0f);

        const float MidX = (float)Rect.X + (float)Rect.W * 0.5f;
        const float MidY = (float)Rect.Y + (float)Rect.H * 0.5f;

        if (I == 0)
        {
            // Terrain toggle: 3 yellow mountain peaks
            const Render::Color PeakCol = Active ? Render::Color(245, 215, 50, 255) : Render::Color(110, 100, 50, 180);
            D.DrawRect(MidX - 6.0f, MidY, 3.0f, 4.0f, PeakCol);
            D.DrawRect(MidX - 5.0f, MidY - 2.0f, 1.0f, 2.0f, PeakCol);
            D.DrawRect(MidX - 2.0f, MidY - 3.0f, 4.0f, 7.0f, PeakCol);
            D.DrawRect(MidX - 1.0f, MidY - 5.0f, 2.0f, 2.0f, PeakCol);
            D.DrawRect(MidX + 3.0f, MidY - 1.0f, 3.0f, 5.0f, PeakCol);
            D.DrawRect(MidX + 4.0f, MidY - 3.0f, 1.0f, 2.0f, PeakCol);
        }
        else if (I == 1)
        {
            // Alliance/Player colors toggle: dual colored dots
            const Render::Color C1 = Active ? Render::Color(55, 230, 85, 255) : Render::Color(60, 175, 255, 255);
            const Render::Color C2 = Active ? Render::Color(240, 50, 50, 255) : Render::Color(245, 205, 45, 255);
            D.DrawRect(MidX - 5.0f, MidY - 3.0f, 4.0f, 6.0f, C1);
            D.DrawRect(MidX + 1.0f, MidY - 3.0f, 4.0f, 6.0f, C2);
        }
        else if (I == 2)
        {
            // Beacons/Vision toggle: crosshair radar icon
            const Render::Color PipCol = Active ? Render::Color(75, 210, 255, 255) : Render::Color(50, 85, 120, 180);
            D.DrawRect(MidX - 5.0f, MidY - 1.0f, 10.0f, 2.0f, PipCol);
            D.DrawRect(MidX - 1.0f, MidY - 5.0f, 2.0f, 10.0f, PipCol);
            D.DrawRect(MidX - 2.0f, MidY - 2.0f, 4.0f, 4.0f, PipCol);
        }
    }
}

} // namespace Game::Client::UI
