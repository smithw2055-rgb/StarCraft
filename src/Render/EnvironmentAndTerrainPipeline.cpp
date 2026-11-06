#include "Render/EnvironmentAndTerrainPipeline.h"
#include "Core/Utils/JsonLite.h"
#include "Resource/RgbaImageFileDecoder.h"

#include <fstream>
#include <sstream>

namespace Render {

using Core::Utils::JsonParser;
using Core::Utils::JsonValue;

static std::string ReadTextFile(const std::string& Path) {
    std::ifstream File(Path, std::ios::binary);
    if (!File.is_open()) return {};
    std::ostringstream Buffer;
    Buffer << File.rdbuf();
    return Buffer.str();
}

bool EnvironmentAndTerrainPipeline::LoadFromAssetsR(const std::string& AssetsRRootPath)
{
    mStarLayers.clear();
    mTotalStars = 0;
    mLoaded = false;

    const std::string EnvDir = AssetsRRootPath + "/04_Terrain_Environment_And_Shaders";

    // 1. Load Space_Parallax_Starfield_Particles.json
    JsonValue StarRoot;
    if (!JsonParser::ParseFile(EnvDir + "/Space_Parallax_Starfield_Particles.json", StarRoot)) {
        return false;
    }
    for (const auto& LayerVal : StarRoot["layers"].AsArray()) {
        ParallaxStarLayer Layer;
        Layer.LayerIndex = LayerVal["layer_index"].AsUInt32();
        Layer.ParallaxScrollFactor = LayerVal["parallax_scroll_factor"].AsFloat(1.0f);
        for (const auto& SVal : LayerVal["stars"].AsArray()) {
            StarParticle2D Star;
            Star.BaseX = SVal["x"].AsInt();
            Star.BaseY = SVal["y"].AsInt();
            Star.SpriteID = SVal["sprite_id"].AsUInt32();
            Layer.Stars.push_back(Star);
            ++mTotalStars;
        }
        mStarLayers.push_back(std::move(Layer));
    }

    // 2. Decode T_Byways_Map_128x128_TileID_RG_Walkable_B.png
    auto TilemapOpt = Resource::Graphics::DecodeRgbaImageFile(
        EnvDir + "/T_Byways_Map_128x128_TileID_RG_Walkable_B.png");
    if (!TilemapOpt.has_value()) return false;
    mTilemapImage = std::move(*TilemapOpt);

    // 3. Decode T_Water_Normal_45Frames_9x5_Flipbook.png
    auto WaterOpt = Resource::Graphics::DecodeRgbaImageFile(
        EnvDir + "/T_Water_Normal_45Frames_9x5_Flipbook.png");
    if (!WaterOpt.has_value()) return false;
    mWaterFlipbookImage = std::move(*WaterOpt);
    mWaterConfig.AtlasWidth = mWaterFlipbookImage.Width;
    mWaterConfig.AtlasHeight = mWaterFlipbookImage.Height;
    mWaterConfig.CellWidth = mWaterConfig.AtlasWidth / mWaterConfig.Columns;
    mWaterConfig.CellHeight = mWaterConfig.AtlasHeight / mWaterConfig.Rows;

    // 4. Load Godot 4 & Unity URP Shader files
    mGodot4ShaderCode = ReadTextFile(EnvDir + "/StarCraft_HD_Deferred_Sprite_Godot4.gdshader");
    mUnityURPShaderCode = ReadTextFile(EnvDir + "/StarCraft_HD_Deferred_Sprite_Unity_URP.shader");
    if (mGodot4ShaderCode.empty() || mUnityURPShaderCode.empty()) return false;

    mLoaded = true;
    return true;
}

Core::Math::Vec2 EnvironmentAndTerrainPipeline::ComputeStarScreenPos(
    size_t LayerIndex,
    size_t StarIndex,
    const Core::Math::Vec2& CameraPos,
    int32_t ViewportWidth,
    int32_t ViewportHeight) const
{
    if (LayerIndex >= mStarLayers.size()) return {0.0f, 0.0f};
    const auto& Layer = mStarLayers[LayerIndex];
    if (StarIndex >= Layer.Stars.size()) return {0.0f, 0.0f};

    const auto& Star = Layer.Stars[StarIndex];
    const float ScrollX = static_cast<float>(Star.BaseX) - CameraPos.X * Layer.ParallaxScrollFactor;
    const float ScrollY = static_cast<float>(Star.BaseY) - CameraPos.Y * Layer.ParallaxScrollFactor;

    float WrapX = std::fmod(ScrollX, static_cast<float>(ViewportWidth));
    if (WrapX < 0.0f) WrapX += static_cast<float>(ViewportWidth);

    float WrapY = std::fmod(ScrollY, static_cast<float>(ViewportHeight));
    if (WrapY < 0.0f) WrapY += static_cast<float>(ViewportHeight);

    return { WrapX, WrapY };
}

TileCellGPUData EnvironmentAndTerrainPipeline::SampleTileCell(int32_t TileX, int32_t TileY) const
{
    TileCellGPUData Out{};
    if (TileX < 0 || TileY < 0 || TileX >= mTilemapImage.Width || TileY >= mTilemapImage.Height) {
        return Out;
    }
    const size_t Offset = (static_cast<size_t>(TileY) * static_cast<size_t>(mTilemapImage.Width) + static_cast<size_t>(TileX)) * 4u;
    const uint8_t R = mTilemapImage.Rgba[Offset + 0];
    const uint8_t G = mTilemapImage.Rgba[Offset + 1];
    const uint8_t B = mTilemapImage.Rgba[Offset + 2];

    Out.MegaTileID = static_cast<uint16_t>(R) | (static_cast<uint16_t>(G) << 8);
    Out.WalkableRatio255 = B;
    Out.IsMacroWalkable = (B >= 128);
    return Out;
}

bool EnvironmentAndTerrainPipeline::CrossVerifyTilemapWithBinary(
    const std::vector<uint16_t>& RawMtxmGrid,
    int32_t MapWidth,
    int32_t MapHeight,
    const std::vector<Resource::Cv5Entry>& JungleCv5,
    const Resource::Vf4Data& JungleVf4,
    size_t& OutTilesVerified,
    size_t& OutMismatches) const
{
    OutTilesVerified = 0;
    OutMismatches = 0;

    if (MapWidth != mTilemapImage.Width || MapHeight != mTilemapImage.Height) {
        ++OutMismatches;
        return false;
    }
    if (RawMtxmGrid.size() < static_cast<size_t>(MapWidth) * static_cast<size_t>(MapHeight)) {
        ++OutMismatches;
        return false;
    }

    for (int32_t Y = 0; Y < MapHeight; ++Y) {
        for (int32_t X = 0; X < MapWidth; ++X) {
            const uint16_t MtxmVal = RawMtxmGrid[static_cast<size_t>(Y) * static_cast<size_t>(MapWidth) + static_cast<size_t>(X)];
            const uint16_t GroupIdx = (MtxmVal >> 4) & 0x7FF;
            const uint16_t SubIdx = MtxmVal & 0x0F;

            uint16_t ExpectedMegaTileID = 0;
            if (GroupIdx < JungleCv5.size()) {
                ExpectedMegaTileID = JungleCv5[GroupIdx].MegaTileIndices[SubIdx];
            }

            uint8_t ExpectedWalkable255 = 0;
            if (ExpectedMegaTileID < JungleVf4.Entries.size()) {
                const auto& Vf4Entry = JungleVf4.Entries[ExpectedMegaTileID];
                int WalkableSubtiles = 0;
                for (int k = 0; k < 16; ++k) {
                    if (Vf4Entry.IsMinitleWalkable(k)) ++WalkableSubtiles;
                }
                ExpectedWalkable255 = static_cast<uint8_t>((static_cast<double>(WalkableSubtiles) / 16.0) * 255.0);
            }

            const TileCellGPUData Sampled = SampleTileCell(X, Y);
            if (Sampled.MegaTileID != ExpectedMegaTileID || Sampled.WalkableRatio255 != ExpectedWalkable255) {
                ++OutMismatches;
            } else {
                ++OutTilesVerified;
            }
        }
    }
    return OutMismatches == 0 && OutTilesVerified == static_cast<size_t>(MapWidth * MapHeight);
}

bool EnvironmentAndTerrainPipeline::SampleWaterNormalAtTime(
    float ElapsedSeconds,
    int32_t LocalX,
    int32_t LocalY,
    float& OutNormalX,
    float& OutNormalY,
    float& OutNormalZ) const
{
    if (!mLoaded || mWaterFlipbookImage.Rgba.empty()) return false;

    const int32_t FrameIdx = static_cast<int32_t>(std::floor(ElapsedSeconds * 15.0f)) % mWaterConfig.TotalFrames;
    const int32_t Col = FrameIdx % mWaterConfig.Columns;
    const int32_t Row = FrameIdx / mWaterConfig.Columns;

    const int32_t WrappedX = ((LocalX % mWaterConfig.CellWidth) + mWaterConfig.CellWidth) % mWaterConfig.CellWidth;
    const int32_t WrappedY = ((LocalY % mWaterConfig.CellHeight) + mWaterConfig.CellHeight) % mWaterConfig.CellHeight;

    const int32_t AtlasX = Col * mWaterConfig.CellWidth + WrappedX;
    const int32_t AtlasY = Row * mWaterConfig.CellHeight + WrappedY;
    const size_t Offset = (static_cast<size_t>(AtlasY) * static_cast<size_t>(mWaterFlipbookImage.Width) + static_cast<size_t>(AtlasX)) * 4u;

    const float NX = (static_cast<float>(mWaterFlipbookImage.Rgba[Offset + 0]) / 255.0f) * 2.0f - 1.0f;
    const float NY = (static_cast<float>(mWaterFlipbookImage.Rgba[Offset + 1]) / 255.0f) * 2.0f - 1.0f;
    const float NZ = std::sqrt(std::max(1.0f - (NX * NX + NY * NY), 0.1f));
    const float InvLen = 1.0f / std::sqrt(NX * NX + NY * NY + NZ * NZ);

    OutNormalX = NX * InvLen;
    OutNormalY = NY * InvLen;
    OutNormalZ = NZ * InvLen;
    return true;
}

bool EnvironmentAndTerrainPipeline::ValidateShaderContracts() const
{
    const bool GodotOK =
        mGodot4ShaderCode.find("tex_basecolor_a") != std::string::npos &&
        mGodot4ShaderCode.find("tex_normal_rg_team_b_ao_a") != std::string::npos &&
        mGodot4ShaderCode.find("tex_spec_r_emissive_gba") != std::string::npos &&
        mGodot4ShaderCode.find("team_color") != std::string::npos;

    const bool UnityOK =
        mUnityURPShaderCode.find("_BaseColorA") != std::string::npos &&
        mUnityURPShaderCode.find("_NormalTeamAO") != std::string::npos &&
        mUnityURPShaderCode.find("_SpecEmissive") != std::string::npos &&
        mUnityURPShaderCode.find("_TeamColor") != std::string::npos;

    return GodotOK && UnityOK;
}

PBRShadedPixel EnvironmentAndTerrainPipeline::Evaluate3ChannelPBRPixel(
    const Resource::Graphics::DecodedRgbaImage& BaseColorA,
    const Resource::Graphics::DecodedRgbaImage& NormalTeamAO,
    const Resource::Graphics::DecodedRgbaImage& SpecEmissive,
    int32_t PixelX,
    int32_t PixelY,
    float TeamColorR,
    float TeamColorG,
    float TeamColorB,
    bool FlipX)
{
    PBRShadedPixel Out{};
    if (PixelX < 0 || PixelY < 0 || PixelX >= BaseColorA.Width || PixelY >= BaseColorA.Height) {
        return Out;
    }

    const int32_t SampleX = FlipX ? (BaseColorA.Width - 1 - PixelX) : PixelX;
    const size_t Offset = (static_cast<size_t>(PixelY) * static_cast<size_t>(BaseColorA.Width) + static_cast<size_t>(SampleX)) * 4u;

    const float BaseR = static_cast<float>(BaseColorA.Rgba[Offset + 0]) / 255.0f;
    const float BaseG = static_cast<float>(BaseColorA.Rgba[Offset + 1]) / 255.0f;
    const float BaseB = static_cast<float>(BaseColorA.Rgba[Offset + 2]) / 255.0f;
    const float BaseAlpha = static_cast<float>(BaseColorA.Rgba[Offset + 3]) / 255.0f;
    if (BaseAlpha < 0.01f) return Out;

    const float NtaR = static_cast<float>(NormalTeamAO.Rgba[Offset + 0]) / 255.0f;
    const float NtaG = static_cast<float>(NormalTeamAO.Rgba[Offset + 1]) / 255.0f;
    const float TeamMask = static_cast<float>(NormalTeamAO.Rgba[Offset + 2]) / 255.0f;
    const float RawAO = static_cast<float>(NormalTeamAO.Rgba[Offset + 3]) / 255.0f;

    const float SpecMask = static_cast<float>(SpecEmissive.Rgba[Offset + 0]) / 255.0f;
    const float EmissiveR = static_cast<float>(SpecEmissive.Rgba[Offset + 1]) / 255.0f;
    const float EmissiveG = static_cast<float>(SpecEmissive.Rgba[Offset + 2]) / 255.0f;
    const float EmissiveB = static_cast<float>(SpecEmissive.Rgba[Offset + 3]) / 255.0f;

    // 1. Team Color blend: mix(base.rgb, base.rgb * team_color, nta.b)
    const float AlbedoR = BaseR * (1.0f - TeamMask) + (BaseR * TeamColorR) * TeamMask;
    const float AlbedoG = BaseG * (1.0f - TeamMask) + (BaseG * TeamColorG) * TeamMask;
    const float AlbedoB = BaseB * (1.0f - TeamMask) + (BaseB * TeamColorB) * TeamMask;

    // 2. DXT5nm Normal reconstruction
    const float FlipSign = FlipX ? -1.0f : 1.0f;
    const float NXY_X = (NtaR * 2.0f - 1.0f) * FlipSign;
    const float NXY_Y = (NtaG * 2.0f - 1.0f);
    const float NZ = std::sqrt(std::max(1.0f - (NXY_X * NXY_X + NXY_Y * NXY_Y), 0.1f));
    const float NLen = std::sqrt(NXY_X * NXY_X + NXY_Y * NXY_Y + NZ * NZ);
    const float NX = NXY_X / NLen;
    const float NY = NXY_Y / NLen;
    const float N_Z = NZ / NLen;

    // 3. Blinn-Phong lighting with directional light (0.37, 0.09, 0.92)
    float LX = 0.37f, LY = 0.09f, LZ = 0.92f;
    const float LLen = std::sqrt(LX * LX + LY * LY + LZ * LZ);
    LX /= LLen; LY /= LLen; LZ /= LLen;

    float HX = LX, HY = LY, HZ = LZ + 1.0f;
    const float HLen = std::sqrt(HX * HX + HY * HY + HZ * HZ);
    HX /= HLen; HY /= HLen; HZ /= HLen;

    const float Lambert = std::clamp(NX * LX + NY * LY + N_Z * LZ, 0.15f, 1.0f);
    const float NdotH = std::max(NX * HX + NY * HY + N_Z * HZ, 0.0f);
    const float Spec = std::pow(NdotH, 16.0f) * SpecMask;
    const float AO = RawAO * 0.75f + 0.25f;

    const float LitR = std::clamp((AlbedoR * Lambert + Spec) * AO + EmissiveR, 0.0f, 1.0f);
    const float LitG = std::clamp((AlbedoG * Lambert + Spec) * AO + EmissiveG, 0.0f, 1.0f);
    const float LitB = std::clamp((AlbedoB * Lambert + Spec) * AO + EmissiveB, 0.0f, 1.0f);

    Out.R = static_cast<uint8_t>(std::lround(LitR * 255.0f));
    Out.G = static_cast<uint8_t>(std::lround(LitG * 255.0f));
    Out.B = static_cast<uint8_t>(std::lround(LitB * 255.0f));
    Out.A = BaseColorA.Rgba[Offset + 3];
    return Out;
}

} // namespace Render
