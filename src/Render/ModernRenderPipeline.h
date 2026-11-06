#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Render/RenderQueue2D.h"
#include "Render/SceneNode2D.h"
#include "Render/SokolPBRDispatcher.h"
#include "Resource/ModernAssetRegistry.h"

namespace Render {

// Phase 2 Presentation & RenderExtract System:
// Extracts live ECS entities directly into a hierarchical SceneNode2D tree
// (TransformPropagation phase) and populates a 64-bit SortKey RenderQueue2D
// with 3-channel HD PBR material descriptors (RenderExtract phase),
// replacing the legacy EcsSync -> std::vector<Client::Entity> copy pipeline.
class ModernRenderPipelineSystem final : public Core::ECS::ISystem {
public:
    ModernRenderPipelineSystem(
        Core::ECS::SystemManager& SysMgr,
        const Resource::ModernAssetRegistry* AssetRegistry = nullptr)
        : mSystemMgr(SysMgr), mAssetRegistry(AssetRegistry) {}

    std::string_view GetName() const override { return "ModernRenderPipelineSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::RenderExtract; }

    void SetAssetRegistry(const Resource::ModernAssetRegistry* Registry) { mAssetRegistry = Registry; }

    void Update(float DeltaTime) override {
        (void)DeltaTime;
        ExtractFromECSWorld(mSystemMgr.GetEntityManager());
    }

    size_t ExtractFromECSWorld(Core::ECS::EntityManager& EM) {
        mRenderQueue.Clear();
        mRootNodes.clear();

        EM.ForEachEntityWith<Game::Components::TransformComponent, Game::Components::RenderComponent>(
            [&](Core::ECS::EntityID Entity) {
                const auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(Entity);
                const auto* RenderComp = EM.GetComponent<Game::Components::RenderComponent>(Entity);
                if (!Trans || !RenderComp || !RenderComp->Visible) return;

                const auto* UnitComp = EM.GetComponent<Game::Components::UnitComponent>(Entity);
                const auto* PlayerComp = EM.GetComponent<Game::Components::PlayerComponent>(Entity);
                const uint8_t TeamColorID = PlayerComp ? static_cast<uint8_t>(PlayerComp->PlayerID & 0xFF) : 0;

                uint8_t ElevationLayer = (RenderComp->Layer == Game::Components::RenderLayer::AirUnits) ? 12 : 4;
                uint32_t ImageID = RenderComp->ImageID;
                uint32_t SubunitImageID = RenderComp->SubunitImageID;

                if (mAssetRegistry && UnitComp && UnitComp->ChkUnitID >= 0) {
                    if (const auto* ModUnit = mAssetRegistry->FindUnitByID(static_cast<uint32_t>(UnitComp->ChkUnitID))) {
                        ElevationLayer = ModUnit->ElevationSortLayer;
                        if (ImageID == 0) ImageID = ModUnit->ImageID;
                    }
                }

                auto RootNode = std::make_unique<SceneNode2D>(
                    "ECS_Entity_" + std::to_string(Entity),
                    SceneNodeRole::RootUnit);
                RootNode->SetLocalPosition(Trans->Position + RenderComp->VisualOffset);
                RootNode->SetFrameSet(RenderComp->FrameIndex);

                // 1. Automatic Shadow Child Node (resolved via HD_Image_Render_Table_999 / images.rel)
                if (mAssetRegistry && ImageID != 0) {
                    for (const auto& ImgEntry : mAssetRegistry->GetHDImages()) {
                        if (ImgEntry.ParentImageID == static_cast<int32_t>(ImageID) && ImgEntry.RoleFlags == 8) {
                            auto ShadowNode = std::make_unique<SceneNode2D>("Shadow", SceneNodeRole::ShadowOverlay);
                            ShadowNode->SetImageID(ImgEntry.ImageID);
                            ShadowNode->SetElevationSortLayer(ElevationLayer);
                            RootNode->AddChild(std::move(ShadowNode));
                            break;
                        }
                    }
                }

                // 2. Main Body Sprite Node
                auto BodyNode = std::make_unique<SceneNode2D>("MainBody", SceneNodeRole::MainSprite);
                BodyNode->SetImageID(ImageID != 0 ? ImageID : 189);
                BodyNode->SetElevationSortLayer(ElevationLayer);
                BodyNode->SetFrameSet(RenderComp->FrameIndex);
                SceneNode2D* BodyPtr = RootNode->AddChild(std::move(BodyNode));

                // 3. Subunit Turret Attachment Node (e.g. Siege Tank / Goliath Turret)
                if (SubunitImageID != UINT32_MAX && SubunitImageID != 0) {
                    auto TurretNode = std::make_unique<SceneNode2D>("TurretSubunit", SceneNodeRole::TurretSubunit);
                    TurretNode->SetImageID(SubunitImageID);
                    TurretNode->SetElevationSortLayer(ElevationLayer);
                    TurretNode->SetZIndexOffset(2);
                    BodyPtr->AddChild(std::move(TurretNode));
                }

                // 4. Child ImageTreeComponent overlays / 2D Sockets
                if (const auto* TreeComp = EM.GetComponent<Game::Components::ImageTreeComponent>(Entity)) {
                    for (const auto& Sub : TreeComp->Nodes) {
                        if (!Sub.Visible || Sub.HiddenByBurrow) continue;
                        SceneNodeRole SubRole = Sub.DrawAbove ? SceneNodeRole::GlowOverlay : SceneNodeRole::ShadowOverlay;
                        auto SubNode = std::make_unique<SceneNode2D>("ImageSubnode", SubRole);
                        SubNode->SetImageID(static_cast<uint32_t>(Sub.ImageID));
                        SubNode->SetLocalPosition(Sub.Offset);
                        SubNode->SetElevationSortLayer(ElevationLayer);
                        SubNode->SetZIndexOffset(Sub.DrawAbove ? 5 : -5);
                        BodyPtr->AddChild(std::move(SubNode));
                    }
                }

                RootNode->PropagateTransforms();
                mRenderQueue.ExtractSceneTree(*RootNode, mAssetRegistry, TeamColorID);
                mRootNodes.push_back(std::move(RootNode));
            });

        mRenderQueue.SortAndBatch();
        return mRenderQueue.GetCommands().size();
    }

    const RenderQueue2D& GetRenderQueue() const { return mRenderQueue; }
    RenderBatchStats GetBatchStats() const { return mRenderQueue.ComputeBatchStats(); }
    size_t GetSceneRootCount() const { return mRootNodes.size(); }

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
    Core::ECS::SystemManager& mSystemMgr;
    const Resource::ModernAssetRegistry* mAssetRegistry = nullptr;
    RenderQueue2D mRenderQueue;
    std::vector<std::unique_ptr<SceneNode2D>> mRootNodes;
    SokolPBRDispatcher mSokolDispatcher;
    SokolDispatchTelemetry mLastTelemetry{};
};

} // namespace Render
