#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Engine
{

// ============================================================================
// Transform2D: General-Purpose 2D Local/World Transform
// Referenced from Godot 4 Node2D / Transform2D & Unity Transform:
// - Supports Position, Scale, RotationRadians, and ZIndex hierarchy composition.
// ============================================================================
struct Transform2D
{
    Core::Math::Vec2 Position{0.0f, 0.0f};
    Core::Math::Vec2 Scale{1.0f, 1.0f};
    float RotationRadians = 0.0f;
    int32_t ZIndex = 0;
    bool ZAsRelative = true;

    static Transform2D Combine(const Transform2D& Parent, const Transform2D& Local) noexcept
    {
        Transform2D Out;
        const float CosR = std::cos(Parent.RotationRadians);
        const float SinR = std::sin(Parent.RotationRadians);
        const float ScaledX = Local.Position.X * Parent.Scale.X;
        const float ScaledY = Local.Position.Y * Parent.Scale.Y;

        Out.Position.X = Parent.Position.X + (ScaledX * CosR - ScaledY * SinR);
        Out.Position.Y = Parent.Position.Y + (ScaledX * SinR + ScaledY * CosR);
        Out.Scale.X = Parent.Scale.X * Local.Scale.X;
        Out.Scale.Y = Parent.Scale.Y * Local.Scale.Y;
        Out.RotationRadians = Parent.RotationRadians + Local.RotationRadians;
        Out.ZIndex = Local.ZAsRelative ? (Parent.ZIndex + Local.ZIndex) : Local.ZIndex;
        Out.ZAsRelative = Local.ZAsRelative;
        return Out;
    }
};

// ============================================================================
// Node2D: General-Purpose Hierarchical 2D Scene Graph Node
// Referenced from Godot 4 Node2D & Unity GameObject/Transform:
// - Supports dirty-flagged local-to-world transform propagation so subtrees
//   only recompute world transforms when a parent or local transform changes.
// - Supports named 2D Socket mount points (e.g. turret mounts, muzzle flashes,
//   carried harvest payloads, exhaust trails).
// - Designed for zero-allocation object pooling via SceneTree2D.
// ============================================================================
class Node2D
{
public:
    explicit Node2D(std::string Name = "Node2D", uint32_t Tag = 0)
        : mName(std::move(Name)), mTag(Tag)
    {
    }

    virtual ~Node2D() = default;

    void Reset(std::string Name = "Node2D", uint32_t Tag = 0)
    {
        mName = std::move(Name);
        mTag = Tag;
        mBoundEntityId = 0;
        mParent = nullptr;
        mLocalTransform = Transform2D{};
        mWorldTransform = Transform2D{};
        mTransformDirty = true;
        mVisible = true;
        mActiveSocketName.clear();
        mSockets.clear();
        mChildren.clear();
    }

    const std::string& GetName() const noexcept { return mName; }
    void SetName(std::string Name) { mName = std::move(Name); }

    uint32_t GetTag() const noexcept { return mTag; }
    void SetTag(uint32_t Tag) noexcept { mTag = Tag; }

    int32_t GetBoundEntityId() const noexcept { return mBoundEntityId; }
    void SetBoundEntityId(int32_t EntityId) noexcept { mBoundEntityId = EntityId; }

    void SetLocalPosition(Core::Math::Vec2 Pos) noexcept
    {
        if (mLocalTransform.Position.X != Pos.X || mLocalTransform.Position.Y != Pos.Y)
        {
            mLocalTransform.Position = Pos;
            MarkTransformDirty();
        }
    }

    void SetLocalScale(Core::Math::Vec2 Scale) noexcept
    {
        if (mLocalTransform.Scale.X != Scale.X || mLocalTransform.Scale.Y != Scale.Y)
        {
            mLocalTransform.Scale = Scale;
            MarkTransformDirty();
        }
    }

    void SetLocalRotation(float Radians) noexcept
    {
        if (mLocalTransform.RotationRadians != Radians)
        {
            mLocalTransform.RotationRadians = Radians;
            MarkTransformDirty();
        }
    }

    void SetLocalZIndex(int32_t ZIndex, bool Relative = true) noexcept
    {
        if (mLocalTransform.ZIndex != ZIndex || mLocalTransform.ZAsRelative != Relative)
        {
            mLocalTransform.ZIndex = ZIndex;
            mLocalTransform.ZAsRelative = Relative;
            MarkTransformDirty();
        }
    }

    const Transform2D& GetLocalTransform() const noexcept { return mLocalTransform; }

    const Transform2D& GetWorldTransform()
    {
        if (mTransformDirty)
            UpdateWorldTransform(false);
        return mWorldTransform;
    }

    Core::Math::Vec2 GetWorldPosition()
    {
        return GetWorldTransform().Position;
    }

    int32_t GetWorldZIndex()
    {
        return GetWorldTransform().ZIndex;
    }

    void SetVisible(bool Visible) noexcept { mVisible = Visible; }
    bool IsVisible() const noexcept { return mVisible; }
    bool IsVisibleInTree() const noexcept
    {
        if (!mVisible)
            return false;
        return mParent ? mParent->IsVisibleInTree() : true;
    }

    // Named 2D Socket Mount API (Godot Marker2D / BoneAttachment2D pattern)
    void DefineSocket(std::string SocketName, Core::Math::Vec2 LocalOffset)
    {
        mSockets[std::move(SocketName)] = LocalOffset;
        MarkTransformDirty();
    }

    bool GetSocketOffset(const std::string& SocketName, Core::Math::Vec2& OutOffset) const
    {
        auto It = mSockets.find(SocketName);
        if (It == mSockets.end())
            return false;
        OutOffset = It->second;
        return true;
    }

    void BindToParentSocket(std::string SocketName)
    {
        mActiveSocketName = std::move(SocketName);
        MarkTransformDirty();
    }

    const std::string& GetBoundParentSocket() const noexcept { return mActiveSocketName; }

    Node2D* AddChild(std::unique_ptr<Node2D> Child)
    {
        if (!Child)
            return nullptr;
        Child->mParent = this;
        Child->MarkTransformDirty();
        Node2D* Raw = Child.get();
        mChildren.push_back(std::move(Child));
        return Raw;
    }

    Node2D* FindChild(std::string_view Name, bool Recursive = true) const
    {
        for (const auto& Child : mChildren)
        {
            if (!Child)
                continue;
            if (Child->mName == Name)
                return Child.get();
            if (Recursive)
            {
                if (Node2D* Found = Child->FindChild(Name, true))
                    return Found;
            }
        }
        return nullptr;
    }

    void MarkTransformDirty() noexcept
    {
        if (mTransformDirty)
            return;
        mTransformDirty = true;
        for (auto& Child : mChildren)
        {
            if (Child)
                Child->MarkTransformDirty();
        }
    }

    uint32_t PropagateTransforms(bool ForceParentDirty = false)
    {
        uint32_t RecomputedCount = 0;
        const bool RecomputeSelf = mTransformDirty || ForceParentDirty;
        if (RecomputeSelf)
        {
            UpdateWorldTransform(false);
            ++RecomputedCount;
        }
        for (auto& Child : mChildren)
        {
            if (Child)
                RecomputedCount += Child->PropagateTransforms(RecomputeSelf);
        }
        return RecomputedCount;
    }

    void RecycleSubtree(std::vector<std::unique_ptr<Node2D>>& Pool)
    {
        for (auto& Child : mChildren)
        {
            if (Child)
            {
                Child->RecycleSubtree(Pool);
                Pool.push_back(std::move(Child));
            }
        }
        mChildren.clear();
    }

    Node2D* GetParent() const noexcept { return mParent; }
    const std::vector<std::unique_ptr<Node2D>>& GetChildren() const noexcept { return mChildren; }
    bool IsTransformDirty() const noexcept { return mTransformDirty; }

private:
    void UpdateWorldTransform(bool /*RecursiveChildren*/)
    {
        if (!mParent)
        {
            mWorldTransform = mLocalTransform;
        }
        else
        {
            Transform2D EffectiveLocal = mLocalTransform;
            if (!mActiveSocketName.empty())
            {
                Core::Math::Vec2 SocketOffset{0.0f, 0.0f};
                if (mParent->GetSocketOffset(mActiveSocketName, SocketOffset))
                {
                    EffectiveLocal.Position.X += SocketOffset.X;
                    EffectiveLocal.Position.Y += SocketOffset.Y;
                }
            }
            mWorldTransform = Transform2D::Combine(mParent->GetWorldTransform(), EffectiveLocal);
        }
        mTransformDirty = false;
    }

    std::string mName;
    uint32_t mTag = 0;
    int32_t mBoundEntityId = 0;
    Node2D* mParent = nullptr;
    Transform2D mLocalTransform{};
    Transform2D mWorldTransform{};
    bool mTransformDirty = true;
    bool mVisible = true;
    std::string mActiveSocketName;
    std::unordered_map<std::string, Core::Math::Vec2> mSockets;
    std::vector<std::unique_ptr<Node2D>> mChildren;
};

// ============================================================================
// SceneTree2D: Object-Pooled 2D Scene Graph Container
// Referenced from Godot 4 SceneTree:
// - Owns a root Node2D and an internal Node2D object pool so dynamic entity
//   hierarchies can be constructed, updated, and recycled with zero steady-state
//   heap allocations.
// ============================================================================
class SceneTree2D
{
public:
    SceneTree2D()
        : mRoot("Root", 0)
    {
        mNodePool.reserve(512);
    }

    Node2D& GetRoot() noexcept { return mRoot; }
    const Node2D& GetRoot() const noexcept { return mRoot; }

    std::unique_ptr<Node2D> AcquireNode(std::string Name = "Node2D", uint32_t Tag = 0)
    {
        if (!mNodePool.empty())
        {
            auto Node = std::move(mNodePool.back());
            mNodePool.pop_back();
            Node->Reset(std::move(Name), Tag);
            ++mPoolReuses;
            return Node;
        }
        ++mPoolAllocations;
        return std::make_unique<Node2D>(std::move(Name), Tag);
    }

    Node2D* SpawnChild(Node2D& Parent, std::string Name = "Node2D", uint32_t Tag = 0)
    {
        return Parent.AddChild(AcquireNode(std::move(Name), Tag));
    }

    void ClearAndRecycleToPool()
    {
        mRoot.RecycleSubtree(mNodePool);
    }

    uint32_t PropagateAllTransforms()
    {
        return mRoot.PropagateTransforms(false);
    }

    size_t GetPooledNodeCount() const noexcept { return mNodePool.size(); }
    uint64_t GetPoolReuses() const noexcept { return mPoolReuses; }
    uint64_t GetPoolAllocations() const noexcept { return mPoolAllocations; }

private:
    Node2D mRoot;
    std::vector<std::unique_ptr<Node2D>> mNodePool;
    uint64_t mPoolReuses = 0;
    uint64_t mPoolAllocations = 0;
};

} // namespace Engine
