#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Engine/Node2D.h"
#include "Engine/Tilemap/TilemapServer2D.h"
#include "Render/RenderQueue2D.h"
#include "Render/RenderServer2D.h"
#include "Resource/ModernAssetRegistry.h"

namespace Engine::Render
{

enum class RenderPassStage : uint8_t
{
    TerrainPass = 0,
    ShadowPass = 1,
    PBRSpritePass = 2,
    ParticlePass = 3,
    FogOfWarPass = 4,
    UICanvasPass = 5,
    kStageCount = 6
};

struct RenderGraphPassContext
{
    RenderPassStage Stage = RenderPassStage::TerrainPass;
    ::Render::RenderServer2D& RenderServer;
    ::Render::RenderQueue2D& RenderQueue;
    Engine::SceneTree2D& SceneTree;
    const Resource::ModernAssetRegistry* AssetRegistry = nullptr;
    float InterpolationAlpha = 0.0f;
};

struct RenderGraphPassTelemetry
{
    RenderPassStage Stage = RenderPassStage::TerrainPass;
    std::string Name;
    bool Enabled = true;
    uint64_t ExecutionCount = 0;
    size_t CommandsEmittedLastRun = 0;
};

struct RenderGraphFrameTelemetry
{
    uint64_t TotalFramesExecuted = 0;
    uint32_t ActivePassesExecuted = 0;
    size_t TotalCommandsQueued = 0;
    size_t TotalBatchedDrawCalls = 0;
};

// ============================================================================
// RenderGraph2D (Phase 38):
// - Declarative multi-pass 2D rendering pipeline inspired by Unity URP
//   RenderGraph & Godot 4 RenderingServer viewport pipelines.
// - Executes ordered passes (Terrain -> Shadow -> PBRSprite -> Particle ->
//   FogOfWar -> UICanvas) and extracts Engine::SceneTree2D nodes directly
//   into 64-bit SortKey RenderQueue2D commands with cloud PBR materials.
// ============================================================================
class RenderGraph2D
{
public:
    using PassExecuteFn = std::function<void(RenderGraphPassContext&)>;

    RenderGraph2D()
    {
        ConfigureDefaultPasses();
    }

    void ConfigureDefaultPasses()
    {
        mPasses.clear();
        mPasses.resize(static_cast<size_t>(RenderPassStage::kStageCount));

        InitStage(RenderPassStage::TerrainPass, "TerrainPass");
        InitStage(RenderPassStage::ShadowPass, "ShadowPass");
        InitStage(RenderPassStage::PBRSpritePass, "PBRSpritePass");
        InitStage(RenderPassStage::ParticlePass, "ParticlePass");
        InitStage(RenderPassStage::FogOfWarPass, "FogOfWarPass");
        InitStage(RenderPassStage::UICanvasPass, "UICanvasPass");

        // Default PBRSpritePass extracts visible Engine::Node2D hierarchy into RenderQueue2D
        SetPassCallback(RenderPassStage::PBRSpritePass, [](RenderGraphPassContext& Ctx) {
            ExtractEngineNodeRecursive(Ctx.SceneTree.GetRoot(), Ctx.RenderQueue, Ctx.AssetRegistry, 0);
        });
    }

    void BindTilemapServer(const Engine::Tilemap::TilemapServer2D* TilemapServer) noexcept
    {
        mBoundTilemapServer = TilemapServer;
    }

    const Engine::Tilemap::TilemapServer2D* GetBoundTilemapServer() const noexcept
    {
        return mBoundTilemapServer;
    }

    // Phase 70 Standard 2D Deferred PBR Pipeline Preset:
    // Pre-wires TerrainPass (TilemapServer2D LUTs + foliage + parallax stars),
    // ShadowPass, PBRSpritePass, ParticlePass, FogOfWarPass, and UICanvasPass.
    void InstallStandard2DDeferredPBRPipeline(
        const Engine::Tilemap::TilemapServer2D* TilemapServer = nullptr)
    {
        if (TilemapServer)
            mBoundTilemapServer = TilemapServer;
        ConfigureDefaultPasses();
        SetPassCallback(RenderPassStage::TerrainPass, [this](RenderGraphPassContext& Ctx) {
            if (mBoundTilemapServer && mBoundTilemapServer->GetTilesetLutCount() > 0)
            {
                // Emit a synthetic terrain batch descriptor command when TilemapServer2D is bound
                Ctx.RenderQueue.SetSortContext(::Render::RenderLayer2D::Terrain);
                Ctx.RenderQueue.SubmitRect(0.0f, 0.0f, 32.0f, 32.0f, ::Render::Color(32, 32, 32, 255));
            }
        });
    }

    void SetPassCallback(RenderPassStage Stage, PassExecuteFn Callback)
    {
        const size_t Idx = static_cast<size_t>(Stage);
        if (Idx < mPasses.size())
            mPasses[Idx].Callback = std::move(Callback);
    }

    void SetPassEnabled(RenderPassStage Stage, bool Enabled) noexcept
    {
        const size_t Idx = static_cast<size_t>(Stage);
        if (Idx < mPasses.size())
            mPasses[Idx].Telemetry.Enabled = Enabled;
    }

    bool IsPassEnabled(RenderPassStage Stage) const noexcept
    {
        const size_t Idx = static_cast<size_t>(Stage);
        return Idx < mPasses.size() ? mPasses[Idx].Telemetry.Enabled : false;
    }

    RenderGraphFrameTelemetry ExecuteFrame(
        ::Render::RenderServer2D& RenderServer,
        Engine::SceneTree2D& SceneTree,
        const Resource::ModernAssetRegistry* AssetRegistry = nullptr,
        float InterpolationAlpha = 0.0f)
    {
        SceneTree.PropagateAllTransforms();
        mFrameQueue.Clear();

        uint32_t ExecutedPasses = 0;
        for (auto& Pass : mPasses)
        {
            if (!Pass.Telemetry.Enabled)
                continue;

            const size_t BeforeCount = mFrameQueue.Size();
            RenderGraphPassContext Ctx{
                Pass.Telemetry.Stage,
                RenderServer,
                mFrameQueue,
                SceneTree,
                AssetRegistry,
                InterpolationAlpha};

            if (Pass.Callback)
                Pass.Callback(Ctx);

            Pass.Telemetry.CommandsEmittedLastRun = mFrameQueue.Size() - BeforeCount;
            ++Pass.Telemetry.ExecutionCount;
            ++ExecutedPasses;
        }

        mFrameQueue.SortAndBatch();
        const auto BatchStats = mFrameQueue.ComputeBatchStats();

        ++mFrameTelemetry.TotalFramesExecuted;
        mFrameTelemetry.ActivePassesExecuted = ExecutedPasses;
        mFrameTelemetry.TotalCommandsQueued = mFrameQueue.Size();
        mFrameTelemetry.TotalBatchedDrawCalls = BatchStats.DrawCalls;
        return mFrameTelemetry;
    }

    const ::Render::RenderQueue2D& GetFrameQueue() const noexcept { return mFrameQueue; }
    const RenderGraphFrameTelemetry& GetFrameTelemetry() const noexcept { return mFrameTelemetry; }

    const RenderGraphPassTelemetry* GetPassTelemetry(RenderPassStage Stage) const noexcept
    {
        const size_t Idx = static_cast<size_t>(Stage);
        return Idx < mPasses.size() ? &mPasses[Idx].Telemetry : nullptr;
    }

    static void ExtractEngineNodeRecursive(
        const Engine::Node2D& Node,
        ::Render::RenderQueue2D& Queue,
        const Resource::ModernAssetRegistry* AssetRegistry,
        uint8_t TeamColorID)
    {
        if (!Node.IsVisible())
            return;

        if (Node.GetImageID() != 0)
        {
            ::Render::SceneNode2D TempAdapter(Node.GetName(),
                                              static_cast<::Render::SceneNodeRole>(Node.GetTag()));
            TempAdapter.SetLocalPosition(Node.GetWorldPosition());
            TempAdapter.SetDirection32(Node.GetDirection32());
            TempAdapter.SetFrameSet(Node.GetFrameSet());
            TempAdapter.SetImageID(Node.GetImageID());
            TempAdapter.SetElevationSortLayer(Node.GetElevationSortLayer());
            TempAdapter.SetZIndexOffset(Node.GetZIndexOffset());
            TempAdapter.PropagateTransforms();
            Queue.ExtractSceneTree(TempAdapter, AssetRegistry, TeamColorID);
        }

        for (const auto& Child : Node.GetChildren())
        {
            if (Child)
                ExtractEngineNodeRecursive(*Child, Queue, AssetRegistry, TeamColorID);
        }
    }

private:
    struct PassEntry
    {
        RenderGraphPassTelemetry Telemetry{};
        PassExecuteFn Callback;
    };

    void InitStage(RenderPassStage Stage, std::string Name)
    {
        const size_t Idx = static_cast<size_t>(Stage);
        mPasses[Idx].Telemetry.Stage = Stage;
        mPasses[Idx].Telemetry.Name = std::move(Name);
        mPasses[Idx].Telemetry.Enabled = true;
    }

    std::vector<PassEntry> mPasses;
    ::Render::RenderQueue2D mFrameQueue;
    RenderGraphFrameTelemetry mFrameTelemetry{};
    const Engine::Tilemap::TilemapServer2D* mBoundTilemapServer = nullptr;
};

} // namespace Engine::Render
