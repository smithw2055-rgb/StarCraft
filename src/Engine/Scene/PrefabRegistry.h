#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Engine/DataAssetRegistry.h"
#include "Engine/Scene/PackedScene2D.h"
#include "Engine/Node2D.h"
#include "Resource/ModernAssetRegistry.h"

namespace Engine::Scene
{

// ============================================================================
// PrefabRegistry (Phase 41 & Phase 49 Zero-Dependency Engine Prefab Server):
// - Zero dependency on Game/Components.h.
// - Indexes PackedScene2D prefabs in an Engine::DataAssetTable<PackedScene2D>.
// - Automatically constructs data-driven unit prefabs (with shadow nodes,
//   main body sprites, subunit turrets, and ECS component initializers) from
//   cloud assets-R UnitDataAssets_All_228.json & HD_Image_Render_Table_999.json.
// ============================================================================
class PrefabRegistry
{
public:
    Engine::AssetHandle<PackedScene2D> RegisterPrefab(
        uint32_t PrefabID,
        std::string Name,
        PackedScene2D Prefab)
    {
        Prefab.PrefabID = PrefabID;
        Prefab.SemanticName = Name;
        return mPrefabs.Register(PrefabID, std::move(Name), std::move(Prefab));
    }

    const PackedScene2D* FindByID(uint32_t PrefabID) const noexcept
    {
        return mPrefabs.FindById(PrefabID);
    }

    const PackedScene2D* FindByName(std::string_view Name) const noexcept
    {
        return mPrefabs.FindByName(Name);
    }

    PrefabSpawnInstance SpawnByID(uint32_t PrefabID,
                                  Core::ECS::EntityManager& EM,
                                  Engine::SceneTree2D& SceneTree,
                                  const Core::Math::Vec2& WorldPos,
                                  int32_t OwnerPlayerID = 0) const
    {
        const PackedScene2D* Prefab = FindByID(PrefabID);
        if (!Prefab)
            return {};
        ++mTotalSpawned;
        return Prefab->Instantiate(EM, SceneTree, WorldPos, OwnerPlayerID);
    }

    PrefabSpawnInstance SpawnByName(std::string_view Name,
                                    Core::ECS::EntityManager& EM,
                                    Engine::SceneTree2D& SceneTree,
                                    const Core::Math::Vec2& WorldPos,
                                    int32_t OwnerPlayerID = 0) const
    {
        const PackedScene2D* Prefab = FindByName(Name);
        if (!Prefab)
            return {};
        ++mTotalSpawned;
        return Prefab->Instantiate(EM, SceneTree, WorldPos, OwnerPlayerID);
    }

    // Populates all 228 StarCraft unit/building prefabs directly from cloud assets-R
    size_t BuildUnitPrefabsFromModernRegistry(const Resource::ModernAssetRegistry& Registry)
    {
        size_t BuiltCount = 0;
        for (const auto& UnitAsset : Registry.GetUnits())
        {
            PackedScene2D Prefab;
            Prefab.PrefabID = UnitAsset.UnitID;
            Prefab.SemanticName = UnitAsset.UnitName.empty()
                                      ? ("Unit_" + std::to_string(UnitAsset.UnitID))
                                      : UnitAsset.UnitName;

            Prefab.RootNodeDescriptor.Name = Prefab.SemanticName + "_Root";
            Prefab.RootNodeDescriptor.RoleTag = static_cast<uint32_t>(::Render::SceneNodeRole::RootUnit);
            Prefab.RootNodeDescriptor.ElevationSortLayer = UnitAsset.ElevationSortLayer;

            // Automatic Shadow Child Descriptor
            if (UnitAsset.ImageID != 0)
            {
                const uint32_t ShadowImg = Registry.FindShadowImageIDByParent(UnitAsset.ImageID);
                if (ShadowImg != 0)
                {
                    PackedNode2DDescriptor ShadowDesc;
                    ShadowDesc.Name = "Shadow";
                    ShadowDesc.RoleTag = static_cast<uint32_t>(::Render::SceneNodeRole::ShadowOverlay);
                    ShadowDesc.ImageID = ShadowImg;
                    ShadowDesc.ElevationSortLayer = UnitAsset.ElevationSortLayer;
                    ShadowDesc.ZIndexOffset = -2;
                    Prefab.RootNodeDescriptor.Children.push_back(std::move(ShadowDesc));
                }
            }

            // Main Body Sprite Node with default Muzzle & Turret sockets
            PackedNode2DDescriptor BodyDesc;
            BodyDesc.Name = "MainBody";
            BodyDesc.RoleTag = static_cast<uint32_t>(::Render::SceneNodeRole::MainSprite);
            BodyDesc.ImageID = UnitAsset.ImageID != 0 ? UnitAsset.ImageID : 189u;
            BodyDesc.ElevationSortLayer = UnitAsset.ElevationSortLayer;
            BodyDesc.Sockets.push_back({"TurretSocket", {0.0f, -4.0f}});
            BodyDesc.Sockets.push_back({"MuzzleSocket", {8.0f, -6.0f}});

            if (UnitAsset.HasTurretAttachment && UnitAsset.TurretUnitID < 228u)
            {
                uint32_t SubImageID = 0;
                if (const auto* SubAsset = Registry.FindUnitByID(UnitAsset.TurretUnitID))
                    SubImageID = SubAsset->ImageID;

                PackedNode2DDescriptor TurretDesc;
                TurretDesc.Name = "TurretSubunit";
                TurretDesc.RoleTag = static_cast<uint32_t>(::Render::SceneNodeRole::TurretSubunit);
                TurretDesc.ImageID = SubImageID;
                TurretDesc.ElevationSortLayer = UnitAsset.ElevationSortLayer;
                TurretDesc.ZIndexOffset = 2;
                TurretDesc.BindToParentSocket = "TurretSocket";
                BodyDesc.Children.push_back(std::move(TurretDesc));
            }

            Prefab.RootNodeDescriptor.Children.push_back(std::move(BodyDesc));

            // Declarative ECS Component Spawner bound via Core::ECS::EngineAdapterHooks
            const Resource::ModernUnitDataAsset CapturedAsset = UnitAsset;
            Prefab.AddComponentInitializer(
                [CapturedAsset](Core::ECS::EntityManager& EM,
                                Core::ECS::EntityID Entity,
                                const Core::Math::Vec2& SpawnPos,
                                int32_t OwnerPlayerID) {
                    const auto& Hooks = Core::ECS::EngineAdapterHooks::Instance();
                    if (Hooks.InitSpawnedUnitPrefab)
                    {
                        Hooks.InitSpawnedUnitPrefab(
                            EM,
                            Entity,
                            SpawnPos.X,
                            SpawnPos.Y,
                            OwnerPlayerID,
                            CapturedAsset.UnitID,
                            CapturedAsset.MaxHP,
                            CapturedAsset.ShieldsEnabled,
                            CapturedAsset.MaxShields,
                            CapturedAsset.ImageID,
                            CapturedAsset.ElevationSortLayer);
                    }
                });

            const uint32_t Id = Prefab.PrefabID;
            const std::string Name = Prefab.SemanticName;
            mPrefabs.Register(Id, Name, std::move(Prefab));
            ++BuiltCount;
        }
        return BuiltCount;
    }

    const Engine::DataAssetTable<PackedScene2D>& GetTable() const noexcept { return mPrefabs; }
    Engine::DataAssetTable<PackedScene2D>& GetTable() noexcept { return mPrefabs; }
    uint64_t GetTotalSpawned() const noexcept { return mTotalSpawned; }

private:
    Engine::DataAssetTable<PackedScene2D> mPrefabs;
    mutable uint64_t mTotalSpawned = 0;
};

} // namespace Engine::Scene
