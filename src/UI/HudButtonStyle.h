#pragma once

#include <cstddef>
#include <vector>

#include "Render/Color.h"
#include "Resource/GpuImage.h"
#include "Resource/UIAssetTypes.h"
#include "UI/HudState.h"

namespace Game::Client::UI
{

inline Resource::Graphics::CommandButtonSkinSet SkinSetForPanelCategory(int CategoryValue)
{
    using Resource::Graphics::CommandButtonSkinSet;
    switch (CategoryValue)
    {
        case 3: return CommandButtonSkinSet::Protoss;
        case 2: return CommandButtonSkinSet::Zerg;
        case 1:
        case 0:
        default:
            return CommandButtonSkinSet::Terran;
    }
}

inline const Resource::Graphics::GpuImage* FindPreferredButtonSkin(
    const std::vector<Resource::Graphics::GpuImage>* Skins,
    int PreferredIndex)
{
    if (!Skins || Skins->empty())
        return nullptr;

    if (PreferredIndex >= 0 &&
        PreferredIndex < static_cast<int>(Skins->size()) &&
        (*Skins)[static_cast<size_t>(PreferredIndex)].Valid())
    {
        return &(*Skins)[static_cast<size_t>(PreferredIndex)];
    }

    for (const auto& Skin : *Skins)
    {
        if (Skin.Valid())
            return &Skin;
    }
    return nullptr;
}

inline bool IsNeutralButtonIcon(const CmdButtonDef& Button)
{
    return Button.CommandId == HudCommandId::Stop || Button.Kind == ActionKind::Back;
}

inline Render::Color SkinTintForState(bool HasCommand, bool Enabled, bool Hovered, bool Active)
{
    if (!HasCommand)
        return Render::Color(90, 90, 96, 80);
    if (!Enabled)
        return Render::Color(118, 128, 140, 185);
    if (Active)
        return Render::Color(255, 255, 255, 255);
    if (Hovered)
        return Render::Color(232, 240, 255, 250);
    return Render::Colors::White();
}

inline Render::Color IconTintForState(const CmdButtonDef& Button, bool Enabled, bool Hovered, bool Active)
{
    const bool Neutral = IsNeutralButtonIcon(Button);
    if (!Enabled)
        return Neutral ? Render::Color(125, 128, 132, 205) : Render::Color(124, 118, 82, 205);
    if (Neutral)
        return Active ? Render::Color(255, 255, 255, 255)
                      : (Hovered ? Render::Color(245, 245, 248, 255)
                                 : Render::Color(230, 230, 236, 255));
    if (Active)
        return Render::Color(255, 246, 138, 255);
    if (Hovered)
        return Render::Color(255, 239, 108, 255);
    return Render::Color(245, 226, 78, 255);
}

inline Render::Color FallbackIconTintForState(bool Enabled, bool Active)
{
    if (!Enabled)
        return Render::Color(145, 145, 145, 190);
    if (Active)
        return Render::Color(255, 250, 205, 255);
    return Render::Colors::White();
}

} // namespace Game::Client::UI
