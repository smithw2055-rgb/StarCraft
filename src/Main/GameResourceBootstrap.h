#pragma once

#include <filesystem>
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

    const std::filesystem::path AssetRoot =
        std::filesystem::path(InstallPath) / "assets";
    if (!Game::Systems::AnimationSystem::InitializeIScriptAssets(
            (AssetRoot / "scripts" / "iscript.bin").string(),
            (AssetRoot / "arr" / "images.dat").string()))
    {
        Core::Utils::Logger::Error(
            "GameMain", "Project iscript assets failed to initialize from: " +
                AssetRoot.string());
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
    ConfigureBuildingAddonOffsetResolver(
        [&Client](int ParentDefID, int AddonDefID) {
            if (ParentDefID < 0 || AddonDefID < 0)
                return Core::Math::Vec2{96.0f, 16.0f};
            const auto ParentDef = Game::Races::GetUnitDefByID(ParentDefID);
            const auto AddonDef = Game::Races::GetUnitDefByID(AddonDefID);
            const auto* DataLoader = Client.GetDataAssets().GetDataLoader();
            const auto* Parent = DataLoader && ParentDef.ChkUnitID >= 0
                ? DataLoader->FindUnitByID((uint32_t)ParentDef.ChkUnitID)
                : nullptr;
            const auto* Addon = DataLoader && AddonDef.ChkUnitID >= 0
                ? DataLoader->FindUnitByID((uint32_t)AddonDef.ChkUnitID)
                : nullptr;
            if (!Parent || !Addon || Parent->PlacementWidth <= 0 ||
                Parent->PlacementHeight <= 0 || Addon->PlacementWidth <= 0 ||
                Addon->PlacementHeight <= 0)
            {
                return Core::Math::Vec2{96.0f, 16.0f};
            }
            return Core::Math::Vec2{
                (float)(-Parent->PlacementWidth / 2 + Addon->AddonPositionX +
                        Addon->PlacementWidth / 2),
                (float)(-Parent->PlacementHeight / 2 + Addon->AddonPositionY +
                        Addon->PlacementHeight / 2)};
        });
    ConfigureZergBurrowCapabilityResolver(
        [](int, uint32_t ImageID) {
            return ImageID != std::numeric_limits<uint32_t>::max() &&
                   Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                       (int)ImageID, Game::IScriptAnims::Anim_Burrow) &&
                   Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                       (int)ImageID, Game::IScriptAnims::Anim_UnBurrow);
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
            if (Offset.X == 127 && Offset.Y == 127)
                return std::nullopt;
            const float X = (float)(Flipped ? -Offset.X : Offset.X);
            return Core::Math::Vec2{X, (float)Offset.Y};
        });
    ConfigureAnimationWeaponImageResolver(
        [&Client](int WeaponID) -> std::optional<uint32_t> {
            const auto* Loader = Client.GetDataAssets().GetDataLoader();
            if (!Loader || WeaponID < 0)
                return std::nullopt;
            const auto* Weapon = Loader->FindWeaponByID((uint32_t)WeaponID);
            if (!Weapon || Weapon->FlingyID >= 209)
                return std::nullopt;
            const auto* Flingy = Loader->FindFlingyByID(Weapon->FlingyID);
            const auto* Sprite = Flingy ? Loader->FindSpriteByID(Flingy->SpriteID) : nullptr;
            if (!Sprite || Loader->ResolveImageGrpPath(Sprite->ImageID).empty())
                return std::nullopt;
            return Sprite->ImageID;
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
            Spec.HasImpactAnimation =
                Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                    (int)Sprite->ImageID, Game::IScriptAnims::Anim_Death);
            return Spec;
        });
    return true;
}

} // namespace Game::Client::GameLoop
