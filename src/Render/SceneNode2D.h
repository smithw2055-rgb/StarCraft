#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Core/Math/Vec2.h"
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

// Godot-style Node2D hierarchical scene node for 2D RTS presentation
class SceneNode2D {
public:
    explicit SceneNode2D(std::string Name = "Node2D", SceneNodeRole Role = SceneNodeRole::RootUnit)
        : mName(std::move(Name)), mRole(Role) {}
    virtual ~SceneNode2D() = default;

    // Resets node state for object-pool reuse without freeing internal buffers
    void Reset(std::string Name, SceneNodeRole Role) {
        mName = std::move(Name);
        mRole = Role;
        mParent = nullptr;
        mLocalPosition = {0.0f, 0.0f};
        mWorldPosition = {0.0f, 0.0f};
        mDirection32 = 0;
        mDirection17 = 0;
        mFlipHorizontal = false;
        mFollowParentDirection = false;
        mFrameSet = 0;
        mImageID = 0;
        mElevationSortLayer = 4;
        mZIndexOffset = 0;
        mVisible = true;
        mBoundSocketTrack = nullptr;
        mUseHD4xSocketScale = false;
        mChildren.clear();
    }

    // Recursively returns child nodes to an object pool before clearing mChildren
    void RecycleTreeToPool(std::vector<std::unique_ptr<SceneNode2D>>& Pool) {
        for (auto& Child : mChildren) {
            if (Child) {
                Child->RecycleTreeToPool(Pool);
                Pool.push_back(std::move(Child));
            }
        }
        mChildren.clear();
    }

    const std::string& GetName() const { return mName; }
    SceneNodeRole GetRole() const { return mRole; }

    void SetLocalPosition(const Core::Math::Vec2& Pos) { mLocalPosition = Pos; }
    const Core::Math::Vec2& GetLocalPosition() const { return mLocalPosition; }
    const Core::Math::Vec2& GetWorldPosition() const { return mWorldPosition; }

    void SetDirection32(int32_t Dir32) {
        mDirection32 = ((Dir32 % 32) + 32) % 32;
        if (mDirection32 <= 16) {
            mDirection17 = mDirection32;
            mFlipHorizontal = false;
        } else {
            mDirection17 = 32 - mDirection32;
            mFlipHorizontal = true;
        }
    }
    int32_t GetDirection32() const { return mDirection32; }
    int32_t GetDirection17() const { return mDirection17; }
    bool IsFlippedHorizontally() const { return mFlipHorizontal; }

    void SetFrameSet(int32_t FrameSet) { mFrameSet = FrameSet; }
    int32_t GetFrameSet() const { return mFrameSet; }

    void SetImageID(uint32_t ImageID) { mImageID = ImageID; }
    uint32_t GetImageID() const { return mImageID; }

    void SetElevationSortLayer(uint8_t Layer) { mElevationSortLayer = Layer; }
    uint8_t GetElevationSortLayer() const { return mElevationSortLayer; }

    void SetZIndexOffset(int16_t Offset) { mZIndexOffset = Offset; }
    int16_t GetZIndexOffset() const { return mZIndexOffset; }

    void SetVisible(bool Visible) { mVisible = Visible; }
    bool IsVisible() const { return mVisible; }

    void BindSocketTrack(const Resource::ModernSocket2DTrack* Track, bool UseHD4xScale = false) {
        mBoundSocketTrack = Track;
        mUseHD4xSocketScale = UseHD4xScale;
    }

    SceneNode2D* AddChild(std::unique_ptr<SceneNode2D> Child) {
        if (!Child) return nullptr;
        Child->mParent = this;
        SceneNode2D* RawPtr = Child.get();
        mChildren.push_back(std::move(Child));
        return RawPtr;
    }

    SceneNode2D* FindChild(const std::string& Name) const {
        for (const auto& Child : mChildren) {
            if (Child->GetName() == Name) return Child.get();
            if (auto* Found = Child->FindChild(Name)) return Found;
        }
        return nullptr;
    }

    const std::vector<std::unique_ptr<SceneNode2D>>& GetChildren() const { return mChildren; }

    // Propagates transforms and 2D Socket mount tracks down the hierarchy
    void PropagateTransforms(const Core::Math::Vec2& ParentWorldPos = {0.0f, 0.0f}, int32_t ParentFrameSet = 0, int32_t ParentDir17 = 0) {
        if (mBoundSocketTrack) {
            int16_t SDX = 0, SDY = 0, HDX = 0, HDY = 0;
            if (mBoundSocketTrack->SampleOffset(ParentFrameSet, ParentDir17, SDX, SDY, HDX, HDY)) {
                if (mUseHD4xSocketScale) {
                    mLocalPosition = { static_cast<float>(HDX), static_cast<float>(HDY) };
                } else {
                    mLocalPosition = { static_cast<float>(SDX), static_cast<float>(SDY) };
                }
            }
        }

        mWorldPosition = {
            ParentWorldPos.X + mLocalPosition.X,
            ParentWorldPos.Y + mLocalPosition.Y
        };

        for (auto& Child : mChildren) {
            if (Child->mFollowParentDirection) {
                Child->SetDirection32(mDirection32);
            }
            Child->PropagateTransforms(mWorldPosition, mFrameSet, mDirection17);
        }
    }

    void SetFollowParentDirection(bool Follow) { mFollowParentDirection = Follow; }
    bool GetFollowParentDirection() const { return mFollowParentDirection; }

private:
    std::string mName;
    SceneNodeRole mRole = SceneNodeRole::RootUnit;
    SceneNode2D* mParent = nullptr;
    std::vector<std::unique_ptr<SceneNode2D>> mChildren;

    Core::Math::Vec2 mLocalPosition{0.0f, 0.0f};
    Core::Math::Vec2 mWorldPosition{0.0f, 0.0f};
    int32_t mDirection32 = 0;
    int32_t mDirection17 = 0;
    bool mFlipHorizontal = false;
    bool mFollowParentDirection = false;
    int32_t mFrameSet = 0;
    uint32_t mImageID = 0;
    uint8_t mElevationSortLayer = 4;
    int16_t mZIndexOffset = 0;
    bool mVisible = true;

    const Resource::ModernSocket2DTrack* mBoundSocketTrack = nullptr;
    bool mUseHD4xSocketScale = false;
};

} // namespace Render
