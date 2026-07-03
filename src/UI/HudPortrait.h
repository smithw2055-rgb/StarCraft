#pragma once

#include <algorithm>
#include <cmath>
#include <string>

#include "Core/Platform/Platform.h"
#include "Game/Races/RaceDef.h"
#include "Resource/UIAssetCacheView.h"

namespace Game::Client::UI
{

inline std::string ResolvePortraitKeyForSelection(int TerranDefId)
{
    if (TerranDefId < 0)
        return {};

    if (TerranDefId == Game::Races::GetUnitDefID("SCV")) return "tscv";
    if (TerranDefId == Game::Races::GetUnitDefID("Marine")) return "tmarine";
    if (TerranDefId == Game::Races::GetUnitDefID("Firebat")) return "tfirebat";
    if (TerranDefId == Game::Races::GetUnitDefID("Medic")) return "tmedic";
    if (TerranDefId == Game::Races::GetUnitDefID("Ghost")) return "tghost";
    if (TerranDefId == Game::Races::GetUnitDefID("Vulture")) return "tvulture";
    if (TerranDefId == Game::Races::GetUnitDefID("Goliath")) return "tgoliath";
    if (TerranDefId == Game::Races::GetUnitDefID("SiegeTank")) return "ttank";
    if (TerranDefId == Game::Races::GetUnitDefID("Wraith")) return "tphoenix";
    if (TerranDefId == Game::Races::GetUnitDefID("Dropship")) return "tdropshi";
    if (TerranDefId == Game::Races::GetUnitDefID("ScienceVessel")) return "tvessel";
    if (TerranDefId == Game::Races::GetUnitDefID("Battlecruiser")) return "tbattlec";
    if (TerranDefId == Game::Races::GetUnitDefID("Valkyrie")) return "tbomber";
    return {};
}

inline const Resource::Graphics::GpuImage* ResolvePortraitFrameForSelection(
    const Resource::Graphics::UIAssetCacheView& UIAssets,
    int TerranDefId,
    const Resource::Graphics::GpuImage* FallbackImage)
{
    if (!UIAssets.HasCache())
        return FallbackImage;

    const std::string PortraitKey = ResolvePortraitKeyForSelection(TerranDefId);
    if (PortraitKey.empty())
        return FallbackImage;

    const auto* Clip = UIAssets.GetPortraitClip(PortraitKey);
    if (!Clip || Clip->Frames.empty())
        return FallbackImage;

    float TotalDuration = 0.0f;
    for (const auto& Frame : Clip->Frames)
        TotalDuration += std::max(1.0f / 30.0f, Frame.DurationSeconds);
    if (TotalDuration <= 0.0f)
        TotalDuration = (float)Clip->Frames.size() * (1.0f / 12.0f);

    float Cursor = std::fmod((float)Core::Platform::GetAbsoluteTime(), TotalDuration);
    for (const auto& Frame : Clip->Frames)
    {
        const float Duration = std::max(1.0f / 30.0f, Frame.DurationSeconds);
        if (Cursor <= Duration)
            return &Frame.Image;
        Cursor -= Duration;
    }
    return &Clip->Frames.back().Image;
}

} // namespace Game::Client::UI
