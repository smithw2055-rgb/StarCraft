#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/Math/Vec2.h"
#include "Resource/Cv5Decoder.h"
#include "Resource/RawColor.h"
#include "Resource/Vf4Decoder.h"

namespace Render {

struct StarParticle2D {
    int32_t BaseX = 0;
    int32_t BaseY = 0;
    uint32_t SpriteID = 0;
};

struct ParallaxStarLayer {
    uint32_t LayerIndex = 0;
    float ParallaxScrollFactor = 1.0f;
    std::vector<StarParticle2D> Stars;
};

struct WaterNormalFlipbookConfig {
    int32_t AtlasWidth = 1152;
    int32_t AtlasHeight = 640;
    int32_t Columns = 9;
    int32_t Rows = 5;
    int32_t TotalFrames = 45;
    int32_t CellWidth = 128;
    int32_t CellHeight = 128;
};

struct TileCellGPUData {
    uint16_t MegaTileID = 0;
    uint8_t WalkableRatio255 = 0; // 0..255 (walkable_subtiles / 16.0 * 255)
    bool IsMacroWalkable = false;
};

struct PBRShadedPixel {
    uint8_t R = 0;
    uint8_t G = 0;
    uint8_t B = 0;
    uint8_t A = 0;
};

class EnvironmentAndTerrainPipeline {
public:
    EnvironmentAndTerrainPipeline() = default;
    ~EnvironmentAndTerrainPipeline() = default;

    // Load 04_Terrain_Environment_And_Shaders assets from StarCraft/assets-R
    bool LoadFromAssetsR(const std::string& AssetsRRootPath);

    bool IsLoaded() const { return mLoaded; }

    // 1. Space Parallax Starfield (5 layers, 294 stars from star.spk -> Space_Parallax_Starfield_Particles.json)
    size_t GetStarLayerCount() const { return mStarLayers.size(); }
    size_t GetTotalStarCount() const { return mTotalStars; }
    const std::vector<ParallaxStarLayer>& GetStarLayers() const { return mStarLayers; }
    Core::Math::Vec2 ComputeStarScreenPos(
        size_t LayerIndex,
        size_t StarIndex,
        const Core::Math::Vec2& CameraPos,
        int32_t ViewportWidth = 640,
        int32_t ViewportHeight = 480) const;

    // 2. GPU Tilemap Texture (T_Byways_Map_128x128_TileID_RG_Walkable_B.png)
    int32_t GetTilemapWidth() const { return mTilemapImage.Width; }
    int32_t GetTilemapHeight() const { return mTilemapImage.Height; }
    TileCellGPUData SampleTileCell(int32_t TileX, int32_t TileY) const;

    // Cross-verify GPU Tilemap PNG against raw binary byways.chk MTXM + jungle.cv5 + jungle.vf4
    bool CrossVerifyTilemapWithBinary(
        const std::vector<uint16_t>& RawMtxmGrid,
        int32_t MapWidth,
        int32_t MapHeight,
        const std::vector<Resource::Cv5Entry>& JungleCv5,
        const Resource::Vf4Data& JungleVf4,
        size_t& OutTilesVerified,
        size_t& OutMismatches) const;

    // 3. Water Normal 45-Frame 9x5 Flipbook (T_Water_Normal_45Frames_9x5_Flipbook.png)
    const WaterNormalFlipbookConfig& GetWaterFlipbookConfig() const { return mWaterConfig; }
    bool SampleWaterNormalAtTime(float ElapsedSeconds, int32_t LocalX, int32_t LocalY, float& OutNormalX, float& OutNormalY, float& OutNormalZ) const;

    // 4. Godot 4 (.gdshader) & Unity URP (.shader) Shader Source & Reference 3-Channel PBR + DXT5nm Evaluator
    const std::string& GetGodot4ShaderSource() const { return mGodot4ShaderCode; }
    const std::string& GetUnityURPShaderSource() const { return mUnityURPShaderCode; }
    bool ValidateShaderContracts() const;

    static PBRShadedPixel Evaluate3ChannelPBRPixel(
        const Resource::Graphics::DecodedRgbaImage& BaseColorA,
        const Resource::Graphics::DecodedRgbaImage& NormalTeamAO,
        const Resource::Graphics::DecodedRgbaImage& SpecEmissive,
        int32_t PixelX,
        int32_t PixelY,
        float TeamColorR = 0.12f,
        float TeamColorG = 0.48f,
        float TeamColorB = 1.0f,
        bool FlipX = false);

private:
    bool mLoaded = false;
    size_t mTotalStars = 0;
    std::vector<ParallaxStarLayer> mStarLayers;
    Resource::Graphics::DecodedRgbaImage mTilemapImage;
    Resource::Graphics::DecodedRgbaImage mWaterFlipbookImage;
    WaterNormalFlipbookConfig mWaterConfig;
    std::string mGodot4ShaderCode;
    std::string mUnityURPShaderCode;
};

} // namespace Render
