#pragma once

#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

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
        [&Client](int ChkUnitID) { return Client.GetSpriteAssets().ResolveImageID(ChkUnitID); },
        [&Client](int ChkUnitID) {
            auto DataAssets = Client.GetDataAssets();
            const auto* DataLoader = DataAssets.GetDataLoader();
            if (!DataLoader || ChkUnitID < 0)
                return std::numeric_limits<uint32_t>::max();
            return DataLoader->ResolveUnitConstructionImageID((uint32_t)ChkUnitID);
        });
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
    auto OverlayTables = std::make_shared<std::unordered_map<std::string, Resource::ImageOffsetTable>>();
    ConfigureAnimationOverlayOffsetResolver(
        [&Client, OverlayTables](uint32_t ImageID, size_t LoType, size_t Frame,
                                 size_t OffsetIndex, bool Flipped)
            -> std::optional<Core::Math::Vec2> {
            auto DataAssets = Client.GetDataAssets();
            const auto* DataLoader = DataAssets.GetDataLoader();
            if (!DataLoader)
                return std::nullopt;

            const std::string Path = DataLoader->ResolveImageOverlayPath(ImageID, LoType);
            if (Path.empty())
                return std::nullopt;

            auto It = OverlayTables->find(Path);
            if (It == OverlayTables->end())
            {
                auto Data = Client.GetArchiveSource().ReadWithFallback("unit/" + Path);
                if (!Data)
                    return std::nullopt;
                auto Table = Resource::DatDecoder::DecodeImageOffsets(*Data);
                if (!Table)
                    return std::nullopt;
                It = OverlayTables->emplace(Path, std::move(*Table)).first;
            }

            if (Frame >= It->second.Frames.size() ||
                OffsetIndex >= It->second.Frames[Frame].size())
            {
                return std::nullopt;
            }

            const auto& Offset = It->second.Frames[Frame][OffsetIndex];
            const float X = (float)(Flipped ? -Offset.X : Offset.X);
            return Core::Math::Vec2{X, (float)Offset.Y};
        });
    ConfigureBulletVisualResolver(
        [&Client](int ChkUnitID, bool TargetIsAir)
            -> std::optional<Game::Systems::BulletSystem::VisualSpec> {
            const auto* Loader = Client.GetDataAssets().GetDataLoader();
            if (!Loader || ChkUnitID < 0)
                return std::nullopt;
            const auto* Unit = Loader->FindUnitByID((uint32_t)ChkUnitID);
            if (!Unit)
                return std::nullopt;
            const uint32_t WeaponID = TargetIsAir ? Unit->AirWeapon : Unit->GroundWeapon;
            const auto* Weapon = Loader->FindWeaponByID(WeaponID);
            if (!Weapon || Weapon->FlingyID >= 209)
                return std::nullopt;
            const auto* Flingy = Loader->FindFlingyByID(Weapon->FlingyID);
            if (!Flingy)
                return std::nullopt;
            const auto* Sprite = Loader->FindSpriteByID(Flingy->SpriteID);
            if (!Sprite || Loader->ResolveImageGrpPath(Sprite->ImageID).empty())
                return std::nullopt;

            Game::Systems::BulletSystem::VisualSpec Spec;
            Spec.ImageID = Sprite->ImageID;
            Spec.Speed = (float)Flingy->TopSpeed * 24.0f / 256.0f;
            return Spec;
        });
    return true;
}

} // namespace Game::Client::GameLoop
