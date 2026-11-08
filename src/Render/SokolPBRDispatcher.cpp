#include "Render/SokolPBRDispatcher.h"
#include "Render/Private/SokolGpuInterop.h"
#include "Resource/RgbaImageFileDecoder.h"

#include <algorithm>

namespace Render {

void SokolPBRDispatcher::Shutdown()
{
    const bool GfxValid = sg_isvalid();
    auto DestroyHandle = [&](GpuTextureHandle& H) {
        if (GfxValid) {
            if (H.View.id && H.View.id != mFallbackTexture.View.id)   { sg_destroy_view(ToSokol(H.View)); }
            if (H.Image.id && H.Image.id != mFallbackTexture.Image.id) { sg_destroy_image(ToSokol(H.Image)); }
        }
        H.View = {};
        H.Image = {};
    };
    for (auto& [Id, Set] : mPBRTextureSets) {
        DestroyHandle(Set.BaseColorA);
        DestroyHandle(Set.NormalTeamAO);
        DestroyHandle(Set.SpecEmissive);
    }
    mPBRTextureSets.clear();
    if (GfxValid) {
        if (mFallbackTexture.View.id)  { sg_destroy_view(ToSokol(mFallbackTexture.View)); }
        if (mFallbackTexture.Image.id) { sg_destroy_image(ToSokol(mFallbackTexture.Image)); }
    }
    mFallbackTexture.View = {};
    mFallbackTexture.Image = {};
}

GpuTextureHandle SokolPBRDispatcher::CreateSokolTextureFromPngFile(
    const std::string& PngPath,
    int32_t* OutWidth,
    int32_t* OutHeight)
{
    if (PngPath.empty()) return {};
    auto DecodedOpt = Resource::Graphics::DecodeRgbaImageFile(PngPath);
    if (!DecodedOpt.has_value() || DecodedOpt->Width <= 0 || DecodedOpt->Height <= 0) {
        return {};
    }

    sg_image_desc IDesc = {};
    IDesc.width = DecodedOpt->Width;
    IDesc.height = DecodedOpt->Height;
    IDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    IDesc.data.mip_levels[0] = { DecodedOpt->Rgba.data(), DecodedOpt->Rgba.size() };
    IDesc.label = PngPath.c_str();

    GpuTextureHandle Handle;
    Handle.Image = FromSokol(sg_make_image(&IDesc));
    if (Handle.Image.id == 0) return {};

    sg_view_desc VDesc = {};
    VDesc.texture.image = ToSokol(Handle.Image);
    VDesc.label = PngPath.c_str();
    Handle.View = FromSokol(sg_make_view(&VDesc));

    if (OutWidth)  *OutWidth = DecodedOpt->Width;
    if (OutHeight) *OutHeight = DecodedOpt->Height;
    return Handle;
}

size_t SokolPBRDispatcher::UploadPBRPrefabsToGpu(const Resource::ModernAssetRegistry& Registry)
{
    Shutdown();

    // Create 1x1 white fallback texture
    {
        uint8_t White[4] = { 255, 255, 255, 255 };
        sg_image_desc IDesc = {};
        IDesc.width = 1;
        IDesc.height = 1;
        IDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
        IDesc.data.mip_levels[0] = { White, 4 };
        IDesc.label = "pbr-fallback-1x1";
        mFallbackTexture.Image = FromSokol(sg_make_image(&IDesc));

        sg_view_desc VDesc = {};
        VDesc.texture.image = ToSokol(mFallbackTexture.Image);
        mFallbackTexture.View = FromSokol(sg_make_view(&VDesc));
    }

    size_t UploadedCount = 0;
    for (const auto& Prefab : Registry.GetPBRPrefabs()) {
        GpuPBRTextureSet Set;
        Set.ImageID = Prefab.ImageID;
        Set.BaseColorA = CreateSokolTextureFromPngFile(Prefab.BaseColorTexturePath, &Set.Width, &Set.Height);
        if (!Set.BaseColorA.Valid()) {
            Set.BaseColorA = mFallbackTexture;
            Set.Width = std::max(1, Prefab.AtlasResolution[0]);
            Set.Height = std::max(1, Prefab.AtlasResolution[1]);
        }
        if (!Prefab.NormalTeamAOTexturePath.empty()) {
            Set.NormalTeamAO = CreateSokolTextureFromPngFile(Prefab.NormalTeamAOTexturePath);
            if (!Set.NormalTeamAO.Valid()) {
                Set.NormalTeamAO = mFallbackTexture;
            }
        }
        if (!Prefab.SpecEmissiveTexturePath.empty()) {
            Set.SpecEmissive = CreateSokolTextureFromPngFile(Prefab.SpecEmissiveTexturePath);
            if (!Set.SpecEmissive.Valid()) {
                Set.SpecEmissive = mFallbackTexture;
            }
        }

        if (Set.IsValid()) {
            mPBRTextureSets[Prefab.ImageID] = Set;
            ++UploadedCount;
        }
    }
    return UploadedCount;
}

const GpuPBRTextureSet* SokolPBRDispatcher::FindGpuPBRTextureSet(uint32_t ImageID) const
{
    auto It = mPBRTextureSets.find(ImageID);
    return It != mPBRTextureSets.end() ? &It->second : nullptr;
}

Color SokolPBRDispatcher::ResolveTeamColor(uint8_t TeamColorID)
{
    switch (TeamColorID % 8) {
        case 0: return Color(244, 4, 4, 255);     // Red
        case 1: return Color(12, 72, 204, 255);   // Blue
        case 2: return Color(44, 180, 148, 255);  // Teal
        case 3: return Color(136, 64, 156, 255);  // Purple
        case 4: return Color(248, 140, 20, 255);  // Orange
        case 5: return Color(112, 48, 20, 255);   // Brown
        case 6: return Color(204, 224, 208, 255); // White
        default: return Color(252, 252, 56, 255); // Yellow
    }
}

SokolDispatchTelemetry SokolPBRDispatcher::FlushQueueToSokol(
    const RenderQueue2D& Queue,
    Draw2D& DrawBackend,
    const Camera2D& Camera)
{
    SokolDispatchTelemetry Telemetry{};
    if (!sg_stats_enabled()) {
        sg_enable_stats();
    }
    const sg_stats StatsBefore = sg_query_stats();

    DrawBackend.BeginFrame(Camera);

    for (const auto& Cmd : Queue.GetCommands()) {
        ++Telemetry.CommandsSubmitted;

        const GpuPBRTextureSet* GpuSet = FindGpuPBRTextureSet(Cmd.ImageID);
        const GpuTextureHandle& BaseTex = (GpuSet && GpuSet->BaseColorA.Valid()) ? GpuSet->BaseColorA : mFallbackTexture;
        const GpuTextureHandle* NtaTex  = (GpuSet && GpuSet->NormalTeamAO.Valid()) ? &GpuSet->NormalTeamAO : nullptr;
        const GpuTextureHandle* SpecTex = (GpuSet && GpuSet->SpecEmissive.Valid()) ? &GpuSet->SpecEmissive : nullptr;

        const float W = (GpuSet && GpuSet->Width > 0) ? static_cast<float>(GpuSet->Width) : 64.0f;
        const float H = (GpuSet && GpuSet->Height > 0) ? static_cast<float>(GpuSet->Height) : 64.0f;
        const float X = Cmd.WorldPosition.X - W * 0.5f;
        const float Y = Cmd.WorldPosition.Y - H * 0.5f;

        int BlendModeInt = 0;
        if (Cmd.Material.BlendMode == PBRBlendMode::AdditiveEmissive) {
            BlendModeInt = 1;
            ++Telemetry.AdditiveSpritesDispatched;
        } else if (Cmd.Material.BlendMode == PBRBlendMode::AlphaBlendShadow) {
            BlendModeInt = 2;
            ++Telemetry.ShadowSpritesDispatched;
        } else {
            BlendModeInt = 0;
            ++Telemetry.PBRSpritesDispatched;
        }

        const Color TeamCol = ResolveTeamColor(Cmd.TeamColorID);
        DrawBackend.DrawPBRSprite(
            BaseTex,
            NtaTex,
            SpecTex,
            X, Y, W, H,
            0.0f, 0.0f, 1.0f, 1.0f,
            Color(255, 255, 255, 255),
            TeamCol,
            Cmd.FlipHorizontal,
            BlendModeInt);
    }

    DrawBackend.EndFrame();

    const sg_stats StatsAfter = sg_query_stats();
    Telemetry.SokolDrawCallsExecuted = StatsAfter.cur_frame.num_draw - StatsBefore.cur_frame.num_draw;
    Telemetry.SokolElementsDrawn = Telemetry.CommandsSubmitted * 6;
    Telemetry.SokolUniformUploads = StatsAfter.cur_frame.num_apply_uniforms - StatsBefore.cur_frame.num_apply_uniforms;
    Telemetry.SokolBindingsApplied = StatsAfter.cur_frame.num_apply_bindings - StatsBefore.cur_frame.num_apply_bindings;
    return Telemetry;
}

} // namespace Render
