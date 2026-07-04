#pragma once

#include <string>

#include "Client/ClientRuntime.h"
#include "Core/Utils/Logger.h"
#include "Main/GameSystemAssetResolvers.h"
#include "Resource/GameDataLoader.h"

namespace Game::Client::GameLoop
{

inline bool InitializeClientResourcesAndSystemAssetResolvers(Game::Client::ClientRuntime& Client,
                                                            const std::string& InstallPath)
{
    if (!Client.InitializeResources(InstallPath))
    {
        Core::Utils::Logger::Error("GameMain", "ResourceCache init failed");
        return false;
    }

    ConfigureBuildAndProductionImageResolvers(
        [&Client](int ChkUnitID) { return Client.GetSpriteAssets().ResolveImageID(ChkUnitID); });
    ConfigureGameplayCollisionBoundsResolvers(
        [&Client](int ChkUnitID) {
            Game::Components::UnitCollisionBounds Bounds;
            auto DataAssets = Client.GetDataAssets();
            if (!DataAssets.HasCache())
                return Bounds;

            const auto* DataLoader = DataAssets.GetDataLoader();
            const auto* UnitData = DataLoader ? DataLoader->FindUnitByID((uint32_t)ChkUnitID) : nullptr;
            if (!UnitData)
                return Bounds;

            Bounds.IsValid = UnitData->DimensionWidth() > 0 && UnitData->DimensionHeight() > 0;
            Bounds.Left = UnitData->DimensionLeft;
            Bounds.Up = UnitData->DimensionUp;
            Bounds.Right = UnitData->DimensionRight;
            Bounds.Down = UnitData->DimensionDown;
            return Bounds;
        });
    return true;
}

} // namespace Game::Client::GameLoop
