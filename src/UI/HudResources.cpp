#include "UI/Hud.h"

#include <string>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "Core/Utils/Logger.h"
#include "UI/HudText.h"
#include "UI/HudRenderPrimitives.h"
#include "Render/Draw2D.h"
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

void Hud::Shutdown() {}

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
                                 Render::Color FallbackTint) -> int
    {
        const int IconSize = 16;
        const float TextSize = 12.0f;
        const int ValueChars = (int)std::strlen(Value);
        const int TextW = std::max(22, ValueChars * 7);
        const int EntryW = IconSize + 4 + TextW;
        const int X = RightX - EntryW;
        const int Y = 3;

        D.DrawRect((float)X - 3.0f, 0.0f, (float)EntryW + 6.0f, 20.0f,
                   Render::Color(0, 0, 0, 118));
        if (HdIcon && HdIcon->Valid())
        {
            DrawContainedImage(*HdIcon, { X, Y, IconSize, IconSize }, Render::Colors::White());
        }
        else if (AggIcons.Valid())
        {
            DrawAtlasImage(AggIcons, IconRect, { X, Y, IconSize, IconSize }, Render::Colors::White());
        }
        else
        {
            D.DrawRect((float)X + 1.0f, (float)Y + 1.0f, 14.0f, 14.0f, Render::Color(18, 20, 28, 230));
            D.DrawRectLines((float)X + 1.0f, (float)Y + 1.0f, 14.0f, 14.0f, FallbackTint, 1.0f);
            D.DrawText(FallbackLabel, (float)X + 4.0f, (float)Y + 3.0f, 9.0f, FallbackTint);
        }

        const float TextX = (float)X + (float)IconSize + 4.0f;
        const float TextY = 4.0f;
        D.DrawText(Value, TextX + 1.0f, TextY + 1.0f, TextSize, Render::Color(0, 0, 0, 210));
        D.DrawText(Value, TextX, TextY, TextSize, Render::Color(232, 242, 252, 255));
        return X - 8;
    };

    char MineralBuf[32];
    char GasBuf[32];
    char SupplyBuf[32];
    std::snprintf(MineralBuf, sizeof(MineralBuf), "%d", mState.Resources.MineralCount);
    std::snprintf(GasBuf, sizeof(GasBuf), "%d", mState.Resources.GasCount);
    std::snprintf(SupplyBuf, sizeof(SupplyBuf), "%d/%d",
                  mState.Resources.SupplyUsed, mState.Resources.SupplyMax);

    const auto* MineralIcon = GetLegacyIcon(0);
    const auto* GasIcon = GetLegacyIcon(2);
    const auto* SupplyIcon = GetLegacyIcon(3);
    int ResourceRight = ViewportWidth - 8;
    ResourceRight = DrawResourceEntry(ResourceRight, SupplyIcon, AggIconSupply, SupplyBuf, "S", Render::Color(220, 210, 120, 255));
    ResourceRight = DrawResourceEntry(ResourceRight, GasIcon, AggIconGas, GasBuf, "G", Render::Color(84, 208, 168, 255));
    DrawResourceEntry(ResourceRight, MineralIcon, AggIconMinerals, MineralBuf, "M", Render::Color(96, 180, 255, 255));
}

} // namespace Game::Client::UI



