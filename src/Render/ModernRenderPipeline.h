#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Render/RenderQueue2D.h"
#include "Render/SceneNode2D.h"
#include "Render/SokolPBRDispatcher.h"
#include "Resource/ModernAssetRegistry.h"

namespace Render {

struct ModernRenderPipelinePoolStats {
    uint64_t TotalExtractions = 0;
    uint64_t NodesReusedFromPool = 0;
    uint64_t NodesAllocatedNew = 0;
    uint64_t NodesAllocatedLastFrame = 0;
    uint64_t NodesReusedLastFrame = 0;
};

// Phase 2 & Phase 49 Presentation & RenderExtract System:
// - Zero dependency on Game/Components.h.
// - Extracts live ECS entities directly into a hierarchical SceneNode2D tree
//   and populates a 64-bit SortKey RenderQueue2D with 3-channel HD PBR material descriptors.
class ModernRenderPipelineSystem final : public Core::ECS::ISystem {
public:
    ModernRenderPipelineSystem(
        Core::ECS::SystemManager& SysMgr,
        const Resource::ModernAssetRegistry* AssetRegistry = nullptr)
        : mSystemMgr(SysMgr), mAssetRegistry(AssetRegistry)
    {
        mNodePool.reserve(512);
    }

    std::string_view GetName() const override { return "ModernRenderPipelineSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::RenderExtract; }

    void SetAssetRegistry(const Resource::ModernAssetRegistry* Registry) { mAssetRegistry = Registry; }

    void Update(float DeltaTime) override {
        (void)DeltaTime;
        ExtractFromECSWorld(mSystemMgr.GetEntityManager());
    }

    size_t ExtractFromECSWorld(Core::ECS::EntityManager& EM) {
        mRenderQueue.Clear();
        RecycleActiveSceneNodes();

        ++mPoolStats.TotalExtractions;
        mPoolStats.NodesAllocatedLastFrame = 0;
        mPoolStats.NodesReusedLastFrame = 0;

        const auto& Hooks = Core::ECS::EngineAdapterHooks::Instance();
        if (Hooks.ExtractRenderEntities) {
            Hooks.ExtractRenderEntities(
                EM,
                [this](Core::ECS::EntityID Entity,
                       float WorldX,
                       float WorldY,
                       int32_t FrameIndex,
                       uint32_t ImageID,
                       uint32_t SubunitImageID,
                       uint8_t ElevationLayer,
                       int32_t ChkUnitID,
                       uint8_t TeamColorID,
                       const std::vector<Core::ECS::ExtractedOverlaySubnode>& Overlays) {
                    if (mAssetRegistry && ChkUnitID >= 0) {
                        if (const auto* ModUnit = mAssetRegistry->FindUnitByID(static_cast<uint32_t>(ChkUnitID))) {
                            ElevationLayer = ModUnit->ElevationSortLayer;
                            if (ImageID == 0) ImageID = ModUnit->ImageID;
                        }
                    }

                    auto RootNode = AcquireNode(
                        "ECS_Entity_" + std::to_string(Entity),
                        SceneNodeRole::RootUnit);
                    RootNode->SetLocalPosition({WorldX, WorldY});
                    RootNode->SetFrameSet(FrameIndex);

                    // 1. Automatic Shadow Child Node resolved in O(1) via pre-indexed ParentImageID -> ShadowImageID map
                    if (mAssetRegistry && ImageID != 0) {
                        const uint32_t ShadowImageID = mAssetRegistry->FindShadowImageIDByParent(ImageID);
                        if (ShadowImageID != 0) {
                            auto ShadowNode = AcquireNode("Shadow", SceneNodeRole::ShadowOverlay);
                            ShadowNode->SetImageID(ShadowImageID);
                            ShadowNode->SetElevationSortLayer(ElevationLayer);
                            RootNode->AddChild(std::move(ShadowNode));
                        }
                    }

                    // 2. Main Body Sprite Node
                    auto BodyNode = AcquireNode("MainBody", SceneNodeRole::MainSprite);
                    BodyNode->SetImageID(ImageID != 0 ? ImageID : 189);
                    BodyNode->SetElevationSortLayer(ElevationLayer);
                    BodyNode->SetFrameSet(FrameIndex);
                    SceneNode2D* BodyPtr = RootNode->AddChild(std::move(BodyNode));

                    // 3. Subunit Turret Attachment Node (e.g. Siege Tank / Goliath Turret)
                    if (SubunitImageID != UINT32_MAX && SubunitImageID != 0) {
                        auto TurretNode = AcquireNode("TurretSubunit", SceneNodeRole::TurretSubunit);
                        TurretNode->SetImageID(SubunitImageID);
                        TurretNode->SetElevationSortLayer(ElevationLayer);
                        TurretNode->SetZIndexOffset(2);
                        BodyPtr->AddChild(std::move(TurretNode));
                    }

                    // 4. Child ImageTreeComponent overlays / 2D Sockets
                    for (const auto& Sub : Overlays) {
                        SceneNodeRole SubRole = Sub.DrawAbove ? SceneNodeRole::GlowOverlay : SceneNodeRole::ShadowOverlay;
                        auto SubNode = AcquireNode("ImageSubnode", SubRole);
                        SubNode->SetImageID(Sub.ImageID);
                        SubNode->SetLocalPosition({Sub.OffsetX, Sub.OffsetY});
                        SubNode->SetElevationSortLayer(ElevationLayer);
                        SubNode->SetZIndexOffset(Sub.DrawAbove ? 5 : -5);
                        BodyPtr->AddChild(std::move(SubNode));
                    }

                    RootNode->PropagateTransforms();
                    mRenderQueue.ExtractSceneTree(*RootNode, mAssetRegistry, TeamColorID);
                    mRootNodes.push_back(std::move(RootNode));
                });
        }

        mRenderQueue.SortAndBatch();
        return mRenderQueue.GetCommands().size();
    }

    const RenderQueue2D& GetRenderQueue() const { return mRenderQueue; }
    RenderBatchStats GetBatchStats() const { return mRenderQueue.ComputeBatchStats(); }
    size_t GetSceneRootCount() const { return mRootNodes.size(); }
    size_t GetPooledNodeCount() const { return mNodePool.size(); }
    const ModernRenderPipelinePoolStats& GetPoolStats() const { return mPoolStats; }

    // Phase 4: Direct Sokol GPU Flush
    size_t InitializeSokolPBRResources() {
        if (!mAssetRegistry) return 0;
        return mSokolDispatcher.UploadPBRPrefabsToGpu(*mAssetRegistry);
    }

    SokolDispatchTelemetry ExtractAndFlushToSokol(
        Core::ECS::EntityManager& EM,
        Draw2D& DrawBackend,
        const Camera2D& Camera)
    {
        ExtractFromECSWorld(EM);
        mLastTelemetry = mSokolDispatcher.FlushQueueToSokol(mRenderQueue, DrawBackend, Camera);
        return mLastTelemetry;
    }

    const SokolPBRDispatcher& GetSokolDispatcher() const { return mSokolDispatcher; }
    const SokolDispatchTelemetry& GetLastSokolTelemetry() const { return mLastTelemetry; }

private:
    std::unique_ptr<SceneNode2D> AcquireNode(std::string Name, SceneNodeRole Role) {
        if (!mNodePool.empty()) {
            auto Node = std::move(mNodePool.back());
            mNodePool.pop_back();
            Node->Reset(std::move(Name), Role);
            ++mPoolStats.NodesReusedFromPool;
            ++mPoolStats.NodesReusedLastFrame;
            return Node;
        }
        ++mPoolStats.NodesAllocatedNew;
        ++mPoolStats.NodesAllocatedLastFrame;
        return std::make_unique<SceneNode2D>(std::move(Name), Role);
    }

    void RecycleActiveSceneNodes() {
        for (auto& Root : mRootNodes) {
            if (Root) {
                Root->RecycleTreeToPool(mNodePool);
                mNodePool.push_back(std::move(Root));
            }
        }
        mRootNodes.clear();
    }

    Core::ECS::SystemManager& mSystemMgr;
    const Resource::ModernAssetRegistry* mAssetRegistry = nullptr;
    RenderQueue2D mRenderQueue;
    std::vector<std::unique_ptr<SceneNode2D>> mRootNodes;
    std::vector<std::unique_ptr<SceneNode2D>> mNodePool;
    ModernRenderPipelinePoolStats mPoolStats{};
    SokolPBRDispatcher mSokolDispatcher;
    SokolDispatchTelemetry mLastTelemetry{};
};

} // namespace Render
