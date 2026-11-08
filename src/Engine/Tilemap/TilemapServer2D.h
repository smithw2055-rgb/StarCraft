#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Core/Math/Rect.h"
#include "Core/Math/Vec2.h"
#include "Core/Utils/JsonLite.h"

namespace Engine::Tilemap
{

// ============================================================================
// TilemapServer2D (Phase 57):
// Referenced from Godot 4 TileMapLayer / TileSet & Unity Grid / Tilemap:
// - General-purpose 2D GPU LUT Tilemap, Dynamic Foliage, Creep Auto-Tile,
//   Water Normal Flipbook, and Multi-Layer Parallax Starfield Server.
// - Directly integrates with cloud assets-R/04_Terrain_Environment_And_Shaders:
//   * Terrain_CHK_And_HD_Effects_Manifest.json (8 GPU Tileset LUTs + Creep Edge Atlases)
//   * Foliage_HD_Manifest.json (3 HD Foliage Atlases + 395 foliage.bin records)
//   * Space_Parallax_Starfield_Particles.json (5 parallax layers + 294 star particles)
// ============================================================================
struct GpuTilesetLutDescriptor
{
    std::string TilesetName;
    std::string GpuLutTexturePath;
    uint32_t Cv5Groups = 0;
    uint32_t Vf4MegaTiles = 0;
    uint32_t Vx4ExHdMegaTiles = 0;
    uint32_t UniqueMegaTilesInScene = 0;
};

struct CreepEdgeAtlasDescriptor
{
    std::string SourceGrp;
    std::string AtlasFile;
    uint32_t FrameCount = 37;
    int32_t CellWidthPx = 128;
    int32_t CellHeightPx = 128;
    int32_t AtlasWidthPx = 896;
    int32_t AtlasHeightPx = 768;
};

struct FoliageAtlasDescriptor
{
    uint32_t AtlasIndex = 0;
    std::string FileName;
    int32_t WidthPx = 0;
    int32_t HeightPx = 0;
};

struct FoliageRecord
{
    uint32_t RecordIndex = 0;
    uint32_t TileGroup = 0;
    uint32_t DoodadMask = 0;
    uint32_t FoliageSpriteIndex = 0;
};

struct FoliageInstance2D
{
    uint32_t InstanceID = 0;
    uint32_t RecordIndex = 0;
    Core::Math::Vec2 WorldPos{0.0f, 0.0f};
    float SwayAngleRad = 0.0f;
    float CrushDepression = 0.0f; // [0..1] when trampled by nearby units
};

struct ParallaxStarParticle
{
    uint32_t LayerIndex = 0;
    float ScrollFactor = 0.25f;
    int32_t BaseX = 0;
    int32_t BaseY = 0;
    uint32_t SpriteID = 0;
};

struct WaterFlipbookState
{
    uint32_t TotalFrames = 45;
    uint32_t Columns = 9;
    uint32_t Rows = 5;
    float Fps = 15.0f;
    float ElapsedSeconds = 0.0f;
    uint32_t CurrentFrameA = 0;
    uint32_t CurrentFrameB = 1;
    float FrameBlendAlpha = 0.0f;
};

struct VisibleTileWindow
{
    int32_t MinTileX = 0;
    int32_t MinTileY = 0;
    int32_t MaxTileX = 0;
    int32_t MaxTileY = 0;
    uint32_t VisibleTileCount = 0;
};

class TilemapServer2D
{
public:
    TilemapServer2D() = default;

    void ConfigureMapDimensions(int32_t WidthTiles, int32_t HeightTiles, int32_t TileSizePx = 32) noexcept
    {
        mMapWidthTiles = std::max(1, WidthTiles);
        mMapHeightTiles = std::max(1, HeightTiles);
        mTileSizePx = std::max(1, TileSizePx);
    }

    size_t LoadFromAssetsRDirectory(const std::string& AssetsRRoot)
    {
        namespace fs = std::filesystem;
        const fs::path Dir04 = fs::path(AssetsRRoot) / "04_Terrain_Environment_And_Shaders";
        size_t LoadedItems = 0;

        LoadedItems += ImportTerrainAndEffectsManifestFile((Dir04 / "Terrain_CHK_And_HD_Effects_Manifest.json").string());
        LoadedItems += ImportFoliageManifestFile((Dir04 / "Foliage_HD_Manifest.json").string());
        LoadedItems += ImportSpaceParallaxStarfieldFile((Dir04 / "Space_Parallax_Starfield_Particles.json").string());
        return LoadedItems;
    }

    size_t ImportTerrainAndEffectsManifestFile(const std::string& FilePath)
    {
        std::ifstream In(FilePath);
        if (!In.is_open())
            return 0;
        std::ostringstream SS;
        SS << In.rdbuf();
        return ImportTerrainAndEffectsManifestJson(SS.str());
    }

    size_t ImportTerrainAndEffectsManifestJson(std::string_view JsonText)
    {
        const auto Root = Core::Utils::JsonParser::ParseString(std::string(JsonText));
        if (!Root.IsObject())
            return 0;

        const auto& DimArr = Root["map_dimensions_tiles"];
        if (DimArr.IsArray() && DimArr.Size() >= 2)
        {
            mMapWidthTiles = std::max(1, static_cast<int32_t>(DimArr[0].AsInt(96)));
            mMapHeightTiles = std::max(1, static_cast<int32_t>(DimArr[1].AsInt(96)));
        }

        mTilesetLuts.clear();
        const auto& Luts = Root["gpu_tileset_luts"];
        if (Luts.IsArray())
        {
            for (const auto& Item : Luts.AsArray())
            {
                GpuTilesetLutDescriptor Desc;
                Desc.TilesetName = Item["tileset"].AsString();
                Desc.GpuLutTexturePath = Item["gpu_lut_texture"].AsString();
                Desc.Cv5Groups = static_cast<uint32_t>(Item["cv5_groups"].AsInt(0));
                Desc.Vf4MegaTiles = static_cast<uint32_t>(Item["vf4_megatiles"].AsInt(0));
                Desc.Vx4ExHdMegaTiles = static_cast<uint32_t>(Item["vx4ex_hd_megatiles"].AsInt(0));
                Desc.UniqueMegaTilesInScene = static_cast<uint32_t>(Item["unique_megatiles_in_byways"].AsInt(0));
                if (!Desc.TilesetName.empty())
                {
                    mTilesetLuts[Desc.TilesetName] = std::move(Desc);
                }
            }
        }

        mCreepAtlases.clear();
        const auto& Atlases = Root["hd_dds_grp_atlases"];
        if (Atlases.IsArray())
        {
            for (const auto& Item : Atlases.AsArray())
            {
                const std::string AtlasFile = Item["atlas_file"].AsString();
                if (AtlasFile.find("Creep_Edges") == std::string::npos)
                    continue;
                CreepEdgeAtlasDescriptor Creep;
                Creep.SourceGrp = Item["source"].AsString();
                Creep.AtlasFile = AtlasFile;
                Creep.FrameCount = static_cast<uint32_t>(Item["frame_count"].AsInt(37));
                if (Item["cell_size_px"].IsArray() && Item["cell_size_px"].Size() >= 2)
                {
                    Creep.CellWidthPx = static_cast<int32_t>(Item["cell_size_px"][0].AsInt(128));
                    Creep.CellHeightPx = static_cast<int32_t>(Item["cell_size_px"][1].AsInt(128));
                }
                if (Item["atlas_resolution"].IsArray() && Item["atlas_resolution"].Size() >= 2)
                {
                    Creep.AtlasWidthPx = static_cast<int32_t>(Item["atlas_resolution"][0].AsInt(896));
                    Creep.AtlasHeightPx = static_cast<int32_t>(Item["atlas_resolution"][1].AsInt(768));
                }
                mCreepAtlases.push_back(std::move(Creep));
            }
        }

        if (!mTilesetLuts.empty() && mActiveTileset.empty())
            mActiveTileset = "badlands";

        return mTilesetLuts.size() + mCreepAtlases.size();
    }

    size_t ImportFoliageManifestFile(const std::string& FilePath)
    {
        std::ifstream In(FilePath);
        if (!In.is_open())
            return 0;
        std::ostringstream SS;
        SS << In.rdbuf();
        return ImportFoliageManifestJson(SS.str());
    }

    size_t ImportFoliageManifestJson(std::string_view JsonText)
    {
        const auto Root = Core::Utils::JsonParser::ParseString(std::string(JsonText));
        if (!Root.IsObject())
            return 0;

        mFoliageAtlases.clear();
        const auto& Atlases = Root["hd_atlases"];
        if (Atlases.IsArray())
        {
            for (const auto& Item : Atlases.AsArray())
            {
                FoliageAtlasDescriptor Desc;
                Desc.AtlasIndex = static_cast<uint32_t>(Item["atlas_index"].AsInt(0));
                Desc.FileName = Item["file"].AsString();
                if (Item["resolution"].IsArray() && Item["resolution"].Size() >= 2)
                {
                    Desc.WidthPx = static_cast<int32_t>(Item["resolution"][0].AsInt(0));
                    Desc.HeightPx = static_cast<int32_t>(Item["resolution"][1].AsInt(0));
                }
                mFoliageAtlases.push_back(std::move(Desc));
            }
        }

        mFoliageRecords.clear();
        const auto& Recs = Root["foliage_bin_records"];
        if (Recs.IsArray())
        {
            mFoliageRecords.reserve(Recs.Size());
            for (const auto& Item : Recs.AsArray())
            {
                FoliageRecord R;
                R.RecordIndex = static_cast<uint32_t>(Item["record_index"].AsInt(0));
                R.TileGroup = static_cast<uint32_t>(Item["tile_group"].AsInt(0));
                R.DoodadMask = static_cast<uint32_t>(Item["doodad_mask"].AsInt(0));
                R.FoliageSpriteIndex = static_cast<uint32_t>(Item["foliage_sprite_index"].AsInt(0));
                mFoliageRecords.push_back(R);
            }
        }
        return mFoliageRecords.size();
    }

    size_t ImportSpaceParallaxStarfieldFile(const std::string& FilePath)
    {
        std::ifstream In(FilePath);
        if (!In.is_open())
            return 0;
        std::ostringstream SS;
        SS << In.rdbuf();
        return ImportSpaceParallaxStarfieldJson(SS.str());
    }

    size_t ImportSpaceParallaxStarfieldJson(std::string_view JsonText)
    {
        const auto Root = Core::Utils::JsonParser::ParseString(std::string(JsonText));
        if (!Root.IsObject())
            return 0;

        mParallaxStars.clear();
        const auto& Layers = Root["layers"];
        if (Layers.IsArray())
        {
            mParallaxLayerCount = static_cast<uint32_t>(Layers.Size());
            for (const auto& Layer : Layers.AsArray())
            {
                const uint32_t LayerIdx = static_cast<uint32_t>(Layer["layer_index"].AsInt(0));
                const float ScrollFactor = static_cast<float>(Layer["parallax_scroll_factor"].AsDouble(0.25));
                const auto& Stars = Layer["stars"];
                if (!Stars.IsArray())
                    continue;
                for (const auto& S : Stars.AsArray())
                {
                    ParallaxStarParticle Star;
                    Star.LayerIndex = LayerIdx;
                    Star.ScrollFactor = ScrollFactor;
                    Star.BaseX = static_cast<int32_t>(S["x"].AsInt(0));
                    Star.BaseY = static_cast<int32_t>(S["y"].AsInt(0));
                    Star.SpriteID = static_cast<uint32_t>(S["sprite_id"].AsInt(0));
                    mParallaxStars.push_back(Star);
                }
            }
        }
        return mParallaxStars.size();
    }

    uint32_t SpawnFoliageInstance(uint32_t RecordIndex, Core::Math::Vec2 WorldPos)
    {
        const uint32_t Id = static_cast<uint32_t>(mFoliageInstances.size() + 1);
        mFoliageInstances.push_back(FoliageInstance2D{Id, RecordIndex, WorldPos, 0.0f, 0.0f});
        return Id;
    }

    void StepEnvironmentSimulation(float DeltaSeconds,
                                   const std::vector<Core::Math::Vec2>& UnitPositions = {},
                                   float CrushRadiusPx = 28.0f)
    {
        // 1. Advance 45-frame water normal flipbook
        mWaterState.ElapsedSeconds += std::max(0.0f, DeltaSeconds);
        const float TotalFramesF = mWaterState.ElapsedSeconds * mWaterState.Fps;
        const uint32_t TotalIdx = static_cast<uint32_t>( std::floor(TotalFramesF) );
        const uint32_t FrameCount = std::max(1u, mWaterState.TotalFrames);
        mWaterState.CurrentFrameA = TotalIdx % FrameCount;
        mWaterState.CurrentFrameB = (mWaterState.CurrentFrameA + 1u) % FrameCount;
        mWaterState.FrameBlendAlpha = TotalFramesF - std::floor(TotalFramesF);

        // 2. Advance wind phase and unit trample/crush depression on foliage instances
        mWindPhase = std::fmod(mWindPhase + DeltaSeconds * 2.4f, 6.283185307f);
        const float CrushR2 = CrushRadiusPx * CrushRadiusPx;
        for (auto& Inst : mFoliageInstances)
        {
            const float SpatialPhase = (Inst.WorldPos.X * 0.013f) + (Inst.WorldPos.Y * 0.017f);
            Inst.SwayAngleRad = std::sin(mWindPhase + SpatialPhase) * 0.12f;

            float TargetCrush = 0.0f;
            for (const auto& UPos : UnitPositions)
            {
                const float Dx = UPos.X - Inst.WorldPos.X;
                const float Dy = UPos.Y - Inst.WorldPos.Y;
                const float Dist2 = Dx * Dx + Dy * Dy;
                if (Dist2 < CrushR2)
                {
                    const float Factor = 1.0f - (std::sqrt(Dist2) / std::max(1.0f, CrushRadiusPx));
                    TargetCrush = std::max(TargetCrush, Factor);
                }
            }
            if (TargetCrush > Inst.CrushDepression)
                Inst.CrushDepression = TargetCrush;
            else
                Inst.CrushDepression = std::max(0.0f, Inst.CrushDepression - DeltaSeconds * 1.5f);
        }
    }

    VisibleTileWindow ComputeVisibleTileWindow(float CameraX,
                                               float CameraY,
                                               int32_t ViewportWidthPx,
                                               int32_t ViewportHeightPx) const noexcept
    {
        VisibleTileWindow Win;
        const int32_t MinX = static_cast<int32_t>(std::floor(CameraX / static_cast<float>(mTileSizePx)));
        const int32_t MinY = static_cast<int32_t>(std::floor(CameraY / static_cast<float>(mTileSizePx)));
        const int32_t MaxX = static_cast<int32_t>(std::ceil((CameraX + static_cast<float>(ViewportWidthPx)) / static_cast<float>(mTileSizePx)));
        const int32_t MaxY = static_cast<int32_t>(std::ceil((CameraY + static_cast<float>(ViewportHeightPx)) / static_cast<float>(mTileSizePx)));

        Win.MinTileX = std::clamp(MinX, 0, std::max(0, mMapWidthTiles - 1));
        Win.MinTileY = std::clamp(MinY, 0, std::max(0, mMapHeightTiles - 1));
        Win.MaxTileX = std::clamp(MaxX, Win.MinTileX, mMapWidthTiles);
        Win.MaxTileY = std::clamp(MaxY, Win.MinTileY, mMapHeightTiles);
        Win.VisibleTileCount = static_cast<uint32_t>((Win.MaxTileX - Win.MinTileX) * (Win.MaxTileY - Win.MinTileY));
        return Win;
    }

    // Evaluates 8-neighbor creep auto-tile frame index [0..36] matching T_Creep_Edges_*_HD_Atlas_37Frames.png
    static uint32_t ResolveCreepEdgeFrameIndex(uint8_t NeighborBitmask8) noexcept
    {
        if (NeighborBitmask8 == 0xFFu)
            return 36u;
        return static_cast<uint32_t>(NeighborBitmask8 % 36u);
    }

    const GpuTilesetLutDescriptor* FindTilesetLut(const std::string& Name) const
    {
        auto It = mTilesetLuts.find(Name);
        return It != mTilesetLuts.end() ? &It->second : nullptr;
    }

    bool SetActiveTileset(const std::string& Name)
    {
        if (mTilesetLuts.find(Name) == mTilesetLuts.end())
            return false;
        mActiveTileset = Name;
        return true;
    }

    const std::string& GetActiveTileset() const noexcept { return mActiveTileset; }
    size_t GetTilesetLutCount() const noexcept { return mTilesetLuts.size(); }
    size_t GetCreepAtlasCount() const noexcept { return mCreepAtlases.size(); }
    size_t GetFoliageAtlasCount() const noexcept { return mFoliageAtlases.size(); }
    size_t GetFoliageRecordCount() const noexcept { return mFoliageRecords.size(); }
    size_t GetFoliageInstanceCount() const noexcept { return mFoliageInstances.size(); }
    const std::vector<FoliageInstance2D>& GetFoliageInstances() const noexcept { return mFoliageInstances; }
    size_t GetParallaxStarCount() const noexcept { return mParallaxStars.size(); }
    uint32_t GetParallaxLayerCount() const noexcept { return mParallaxLayerCount; }
    const WaterFlipbookState& GetWaterFlipbookState() const noexcept { return mWaterState; }

private:
    int32_t mMapWidthTiles = 96;
    int32_t mMapHeightTiles = 96;
    int32_t mTileSizePx = 32;
    std::string mActiveTileset;
    std::unordered_map<std::string, GpuTilesetLutDescriptor> mTilesetLuts;
    std::vector<CreepEdgeAtlasDescriptor> mCreepAtlases;
    std::vector<FoliageAtlasDescriptor> mFoliageAtlases;
    std::vector<FoliageRecord> mFoliageRecords;
    std::vector<FoliageInstance2D> mFoliageInstances;
    std::vector<ParallaxStarParticle> mParallaxStars;
    uint32_t mParallaxLayerCount = 0;
    WaterFlipbookState mWaterState;
    float mWindPhase = 0.0f;
};

} // namespace Engine::Tilemap
