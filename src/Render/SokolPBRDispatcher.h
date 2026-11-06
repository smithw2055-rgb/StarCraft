#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Render/Camera2D.h"
#include "Render/Draw2D.h"
#include "Render/GpuTextureHandle.h"
#include "Render/RenderQueue2D.h"
#include "Resource/ModernAssetRegistry.h"

namespace Render {

struct GpuPBRTextureSet {
    uint32_t ImageID = 0;
    int32_t Width = 0;
    int32_t Height = 0;
    GpuTextureHandle BaseColorA;
    GpuTextureHandle NormalTeamAO;
    GpuTextureHandle SpecEmissive;

    bool IsValid() const { return BaseColorA.Valid(); }
};

struct SokolDispatchTelemetry {
    size_t CommandsSubmitted = 0;
    size_t PBRSpritesDispatched = 0;
    size_t ShadowSpritesDispatched = 0;
    size_t AdditiveSpritesDispatched = 0;
    uint64_t SokolDrawCallsExecuted = 0;
    uint64_t SokolElementsDrawn = 0;
    uint64_t SokolUniformUploads = 0;
    uint64_t SokolBindingsApplied = 0;
};

// Phase 4: Direct Sokol GPU Texture Cache & RenderQueue2D Dispatcher
class SokolPBRDispatcher {
public:
    SokolPBRDispatcher() = default;
    ~SokolPBRDispatcher() { Shutdown(); }

    void Shutdown();

    // Decode and upload all 13 3-channel PBR Prefab bundles from ModernAssetRegistry into Sokol GPU textures
    size_t UploadPBRPrefabsToGpu(const Resource::ModernAssetRegistry& Registry);

    // Upload a single RGBA buffer as a Sokol sg_image + sg_view pair
    static GpuTextureHandle CreateSokolTextureFromPngFile(const std::string& PngPath, int32_t* OutWidth = nullptr, int32_t* OutHeight = nullptr);

    const GpuPBRTextureSet* FindGpuPBRTextureSet(uint32_t ImageID) const;
    size_t GetUploadedPrefabCount() const { return mPBRTextureSets.size(); }

    // Directly flush a sorted RenderQueue2D into Draw2D / Sokol GPU
    SokolDispatchTelemetry FlushQueueToSokol(
        const RenderQueue2D& Queue,
        Draw2D& DrawBackend,
        const Camera2D& Camera);

    static Color ResolveTeamColor(uint8_t TeamColorID);

private:
    std::unordered_map<uint32_t, GpuPBRTextureSet> mPBRTextureSets;
    GpuTextureHandle mFallbackTexture;
};

} // namespace Render
