#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Core/Math/Vec2.h"
#include "Engine/Node2D.h"
#include "Resource/ModernAssetRegistry.h"

namespace Render {

enum class SceneNodeRole : uint8_t {
    RootUnit = 0,
    MainSprite = 1,
    ShadowOverlay = 2,
    TurretSubunit = 3,
    SocketAttachment2D = 4,
    SelectionCircle = 5,
    ShieldOverlay = 6,
    GlowOverlay = 7
};

// ============================================================================
// SceneNode2D (Phase 38 Unification):
// - Unifies directly onto Engine::Node2D so transform state, 32-way direction
//   symmetry, frame sets, image IDs, and Z-index layering live in the single
//   general-purpose Engine::Node2D base class.
// - Adds only the StarCraft-specific ModernSocket2DTrack sampling binding.
// ============================================================================
class SceneNode2D : public Engine::Node2D {
public:
    explicit SceneNode2D(std::string Name = "Node2D", SceneNodeRole Role = SceneNodeRole::RootUnit)
        : Engine::Node2D(std::move(Name), static_cast<uint32_t>(Role))
    {
    }

    ~SceneNode2D() override = default;

    void Reset(std::string Name, SceneNodeRole Role) {
        ResetNode2D(std::move(Name), static_cast<uint32_t>(Role));
        mBoundSocketTrack = nullptr;
        mUseHD4xSocketScale = false;
        mSceneChildren.clear();
    }

    void RecycleTreeToPool(std::vector<std::unique_ptr<SceneNode2D>>& Pool) {
        for (auto& Child : mSceneChildren) {
            if (Child) {
                Child->RecycleTreeToPool(Pool);
                Pool.push_back(std::move(Child));
            }
        }
        mSceneChildren.clear();
    }

    SceneNodeRole GetRole() const noexcept {
        return static_cast<SceneNodeRole>(GetTag());
    }

    void BindSocketTrack(const Resource::ModernSocket2DTrack* Track, bool UseHD4xScale = false) {
        mBoundSocketTrack = Track;
        mUseHD4xSocketScale = UseHD4xScale;
    }

    SceneNode2D* AddChild(std::unique_ptr<SceneNode2D> Child) {
        if (!Child) return nullptr;
        SceneNode2D* RawPtr = Child.get();
        mSceneChildren.push_back(std::move(Child));
        return RawPtr;
    }

    SceneNode2D* FindChild(const std::string& Name) const {
        for (const auto& Child : mSceneChildren) {
            if (Child->GetName() == Name) return Child.get();
            if (auto* Found = Child->FindChild(Name)) return Found;
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<SceneNode2D>>& GetChildren() const {
        return mSceneChildren;
    }

    void PropagateTransforms(const Core::Math::Vec2& ParentWorldPos = {0.0f, 0.0f},
                             int32_t ParentFrameSet = 0,
                             int32_t ParentDir17 = 0) {
        if (mBoundSocketTrack) {
            int16_t SDX = 0, SDY = 0, HDX = 0, HDY = 0;
            if (mBoundSocketTrack->SampleOffset(ParentFrameSet, ParentDir17, SDX, SDY, HDX, HDY)) {
                if (mUseHD4xSocketScale) {
                    ForceLocalPositionSilent({static_cast<float>(HDX), static_cast<float>(HDY)});
                } else {
                    ForceLocalPositionSilent({static_cast<float>(SDX), static_cast<float>(SDY)});
                }
            }
        }

        const Core::Math::Vec2 LocalPos = GetLocalPosition();
        const Core::Math::Vec2 WorldPos{
            ParentWorldPos.X + LocalPos.X,
            ParentWorldPos.Y + LocalPos.Y
        };
        SetWorldPositionResolved(WorldPos);

        for (auto& Child : mSceneChildren) {
            if (Child->GetFollowParentDirection()) {
                Child->SetDirection32(GetDirection32());
            }
            Child->PropagateTransforms(WorldPos, GetFrameSet(), GetDirection17());
        }
    }

private:
    std::vector<std::unique_ptr<SceneNode2D>> mSceneChildren;
    const Resource::ModernSocket2DTrack* mBoundSocketTrack = nullptr;
    bool mUseHD4xSocketScale = false;
};

} // namespace Render
