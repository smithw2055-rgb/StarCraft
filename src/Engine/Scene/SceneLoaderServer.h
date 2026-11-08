#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Core/Utils/JsonLite.h"
#include "Engine/Navigation/NavigationServer2D.h"
#include "Engine/Node2D.h"
#include "Engine/Physics/PhysicsServer2D.h"
#include "Engine/Scene/PrefabRegistry.h"

namespace Engine::Scene
{

struct PlacedSceneUnitRecord2D
{
    uint32_t InstanceIndex = 0;
    uint32_t UnitID = 0;
    std::string UnitName;
    int32_t OwnerPlayer = 0;
    Core::Math::Vec2 WorldPositionPx{0.0f, 0.0f};
    int32_t TileX = 0;
    int32_t TileY = 0;
    uint32_t ResourceAmount = 0;
    std::string ModernPrefabBundle;
};

struct TilesetGpuLutRecord2D
{
    std::string TilesetName;
    std::string GpuLutTexturePath;
    uint32_t Cv5Groups = 0;
    uint32_t Vf4MegaTiles = 0;
    uint32_t Vx4ExHDMegaTiles = 0;
    uint32_t UniqueMegaTilesInScene = 0;
};

struct FoliageAtlasRecord2D
{
    uint32_t AtlasIndex = 0;
    std::string FileName;
    int32_t WidthPx = 0;
    int32_t HeightPx = 0;
};

struct LoadedSceneManifest2D
{
    std::string SceneName;
    std::string MapTitle;
    std::string EraTilesetName;
    int32_t WidthTiles = 0;
    int32_t HeightTiles = 0;
    int32_t WidthNavMiniTiles = 0;
    int32_t HeightNavMiniTiles = 0;
    std::vector<PlacedSceneUnitRecord2D> PlacedUnits;
    std::vector<Core::ECS::EntityID> SpawnedEntities;
    std::vector<TilesetGpuLutRecord2D> TilesetGpuLuts;
    std::vector<FoliageAtlasRecord2D> FoliageAtlases;
    uint32_t FoliageRecordCount = 0;
};

// ============================================================================
// SceneLoaderServer (Phase 48):
// Referenced from Godot 4 ResourceLoader / SceneTree & Unity SceneManager:
// - Loads declarative JSON scenes (e.g. cloud Byways_Chk_Full_Scene.json),
//   GPU Tileset LUT catalogs (Terrain_CHK_And_HD_Effects_Manifest.json), and
//   HD Foliage manifests (Foliage_HD_Manifest.json).
// - Automatically configures NavigationServer2D grid bounds, registers spawned
//   unit collision bodies into PhysicsServer2D, and instantiates all placed
//   units via PrefabRegistry into the ECS EntityManager & SceneTree2D.
// ============================================================================
class SceneLoaderServer
{
public:
    bool LoadSceneFromAssetsR(const std::string& AssetsRRootPath,
                              Core::ECS::EntityManager& EM,
                              Engine::SceneTree2D& SceneTree,
                              const PrefabRegistry& Prefabs,
                              Engine::Navigation::NavigationServer2D* NavServer = nullptr,
                              Engine::Physics::PhysicsServer2D* PhysicsServer = nullptr)
    {
        const std::string EnvDir = AssetsRRootPath + "/04_Terrain_Environment_And_Shaders";
        mActiveScene = LoadedSceneManifest2D{};

        if (!ParseChkSceneJson(EnvDir + "/Byways_Chk_Full_Scene.json", mActiveScene))
            return false;

        ParseTerrainManifestJson(EnvDir + "/Terrain_CHK_And_HD_Effects_Manifest.json", mActiveScene);
        ParseFoliageManifestJson(EnvDir + "/Foliage_HD_Manifest.json", mActiveScene);

        if (NavServer && mActiveScene.WidthTiles > 0 && mActiveScene.HeightTiles > 0)
        {
            NavServer->InitializeGrid(mActiveScene.WidthTiles, mActiveScene.HeightTiles, 32.0f);
            NavServer->RebuildConnectedRegions(1u);
        }

        for (const auto& UnitRec : mActiveScene.PlacedUnits)
        {
            auto Spawned = Prefabs.SpawnByID(
                UnitRec.UnitID,
                EM,
                SceneTree,
                UnitRec.WorldPositionPx,
                UnitRec.OwnerPlayer);

            if (Spawned.Entity != Core::ECS::InvalidEntity)
            {
                mActiveScene.SpawnedEntities.push_back(Spawned.Entity);
                if (PhysicsServer)
                {
                    Engine::Physics::PhysicsBody2D Body{};
                    Body.Entity = Spawned.Entity;
                    Body.Position = UnitRec.WorldPositionPx;
                    Body.Radius = 16.0f;
                    const bool IsResource = (UnitRec.UnitID >= 176u && UnitRec.UnitID <= 188u);
                    Body.LayerMask = IsResource
                                         ? Engine::Physics::CollisionLayer::Resource
                                         : Engine::Physics::CollisionLayer::GroundUnit;
                    Body.IsStatic = IsResource;
                    PhysicsServer->RegisterOrUpdateBody(Body);
                }
            }
        }

        if (PhysicsServer)
            PhysicsServer->RebuildBroadphase();

        ++mScenesLoadedCount;
        return true;
    }

    const LoadedSceneManifest2D& GetActiveScene() const noexcept { return mActiveScene; }
    uint64_t GetScenesLoadedCount() const noexcept { return mScenesLoadedCount; }

private:
    static bool ParseChkSceneJson(const std::string& Path, LoadedSceneManifest2D& Out)
    {
        Core::Utils::JsonValue Root;
        if (!Core::Utils::JsonParser::ParseFile(Path, Root) || !Root.IsObject())
            return false;

        Out.SceneName = Root["scene_name"].AsString();
        Out.MapTitle = Root["map_title"].AsString();
        Out.EraTilesetName = Root["era_tileset_name"].AsString();

        if (Root["dimensions_tiles"].IsArray() && Root["dimensions_tiles"].Size() >= 2)
        {
            Out.WidthTiles = static_cast<int32_t>(Root["dimensions_tiles"][0].AsInt());
            Out.HeightTiles = static_cast<int32_t>(Root["dimensions_tiles"][1].AsInt());
        }
        if (Root["dimensions_Navigation_minitiles"].IsArray() &&
            Root["dimensions_Navigation_minitiles"].Size() >= 2)
        {
            Out.WidthNavMiniTiles = static_cast<int32_t>(Root["dimensions_Navigation_minitiles"][0].AsInt());
            Out.HeightNavMiniTiles = static_cast<int32_t>(Root["dimensions_Navigation_minitiles"][1].AsInt());
        }

        const auto& UnitsArr = Root["placed_units"];
        if (UnitsArr.IsArray())
        {
            Out.PlacedUnits.reserve(UnitsArr.Size());
            for (size_t i = 0; i < UnitsArr.Size(); ++i)
            {
                const auto& U = UnitsArr[i];
                if (!U.IsObject())
                    continue;
                PlacedSceneUnitRecord2D Rec{};
                Rec.InstanceIndex = static_cast<uint32_t>(U["instance_index"].AsInt());
                Rec.UnitID = static_cast<uint32_t>(U["unit_id"].AsInt());
                Rec.UnitName = U["unit_name"].AsString();
                Rec.OwnerPlayer = static_cast<int32_t>(U["owner_player"].AsInt());
                Rec.ResourceAmount = static_cast<uint32_t>(U["resource_amount"].AsInt());
                Rec.ModernPrefabBundle = U["modern_prefab_bundle"].AsString();
                if (U["position_px"].IsArray() && U["position_px"].Size() >= 2)
                {
                    Rec.WorldPositionPx.X = U["position_px"][0].AsFloat();
                    Rec.WorldPositionPx.Y = U["position_px"][1].AsFloat();
                }
                if (U["tile_coords"].IsArray() && U["tile_coords"].Size() >= 2)
                {
                    Rec.TileX = static_cast<int32_t>(U["tile_coords"][0].AsInt());
                    Rec.TileY = static_cast<int32_t>(U["tile_coords"][1].AsInt());
                }
                Out.PlacedUnits.push_back(std::move(Rec));
            }
        }
        return Out.WidthTiles > 0 && Out.HeightTiles > 0;
    }

    static bool ParseTerrainManifestJson(const std::string& Path, LoadedSceneManifest2D& Out)
    {
        Core::Utils::JsonValue Root;
        if (!Core::Utils::JsonParser::ParseFile(Path, Root) || !Root.IsObject())
            return false;

        const auto& Luts = Root["gpu_tileset_luts"];
        if (Luts.IsArray())
        {
            for (size_t i = 0; i < Luts.Size(); ++i)
            {
                const auto& Item = Luts[i];
                if (!Item.IsObject())
                    continue;
                TilesetGpuLutRecord2D Lut{};
                Lut.TilesetName = Item["tileset"].AsString();
                Lut.GpuLutTexturePath = Item["gpu_lut_texture"].AsString();
                Lut.Cv5Groups = static_cast<uint32_t>(Item["cv5_groups"].AsInt());
                Lut.Vf4MegaTiles = static_cast<uint32_t>(Item["vf4_megatiles"].AsInt());
                Lut.Vx4ExHDMegaTiles = static_cast<uint32_t>(Item["vx4ex_hd_megatiles"].AsInt());
                Lut.UniqueMegaTilesInScene = static_cast<uint32_t>(Item["unique_megatiles_in_byways"].AsInt());
                Out.TilesetGpuLuts.push_back(std::move(Lut));
            }
        }
        return true;
    }

    static bool ParseFoliageManifestJson(const std::string& Path, LoadedSceneManifest2D& Out)
    {
        Core::Utils::JsonValue Root;
        if (!Core::Utils::JsonParser::ParseFile(Path, Root) || !Root.IsObject())
            return false;

        Out.FoliageRecordCount = static_cast<uint32_t>(Root["foliage_bin_records_count"].AsInt());
        const auto& Atlases = Root["hd_atlases"];
        if (Atlases.IsArray())
        {
            for (size_t i = 0; i < Atlases.Size(); ++i)
            {
                const auto& A = Atlases[i];
                if (!A.IsObject())
                    continue;
                FoliageAtlasRecord2D Rec{};
                Rec.AtlasIndex = static_cast<uint32_t>(A["atlas_index"].AsInt());
                Rec.FileName = A["file"].AsString();
                if (A["resolution"].IsArray() && A["resolution"].Size() >= 2)
                {
                    Rec.WidthPx = static_cast<int32_t>(A["resolution"][0].AsInt());
                    Rec.HeightPx = static_cast<int32_t>(A["resolution"][1].AsInt());
                }
                Out.FoliageAtlases.push_back(std::move(Rec));
            }
        }
        return true;
    }

    LoadedSceneManifest2D mActiveScene;
    uint64_t mScenesLoadedCount = 0;
};

} // namespace Engine::Scene
