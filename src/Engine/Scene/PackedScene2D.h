#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Engine/Node2D.h"

namespace Engine::Scene
{

struct PackedSocket2D
{
    std::string Name;
    Core::Math::Vec2 LocalOffset{0.0f, 0.0f};
};

struct PackedNode2DDescriptor
{
    std::string Name = "Node2D";
    uint32_t RoleTag = 0;
    uint32_t ImageID = 0;
    uint8_t ElevationSortLayer = 4;
    int16_t ZIndexOffset = 0;
    Core::Math::Vec2 LocalPosition{0.0f, 0.0f};
    std::string BindToParentSocket;
    std::vector<PackedSocket2D> Sockets;
    std::vector<PackedNode2DDescriptor> Children;
};

struct PrefabSpawnInstance
{
    Core::ECS::EntityID Entity = Core::ECS::InvalidEntity;
    Engine::Node2D* RootSceneNode = nullptr;
};

// ============================================================================
// PackedScene2D (Phase 41):
// - Declarative entity & Node2D scene hierarchy template inspired by Godot 4
//   PackedScene and Unity Prefab.
// - Bundles an arbitrary hierarchy of PackedNode2DDescriptors (root, shadow,
//   main body sprite, turret subunit, 2D sockets) together with type-erased
//   ECS component initializers so entities can be spawned purely from data.
// ============================================================================
class PackedScene2D
{
public:
    using ComponentInitializerFn = std::function<void(Core::ECS::EntityManager&,
                                                      Core::ECS::EntityID,
                                                      const Core::Math::Vec2& SpawnPos,
                                                      int32_t OwnerPlayerID)>;

    uint32_t PrefabID = 0;
    std::string SemanticName;
    PackedNode2DDescriptor RootNodeDescriptor;

    void AddComponentInitializer(ComponentInitializerFn InitFn)
    {
        if (InitFn)
            mComponentInitializers.push_back(std::move(InitFn));
    }

    PrefabSpawnInstance Instantiate(Core::ECS::EntityManager& EM,
                                    Engine::SceneTree2D& SceneTree,
                                    const Core::Math::Vec2& WorldPosition,
                                    int32_t OwnerPlayerID = 0) const
    {
        PrefabSpawnInstance Out{};
        Out.Entity = EM.CreateEntity();

        for (const auto& InitFn : mComponentInitializers)
        {
            InitFn(EM, Out.Entity, WorldPosition, OwnerPlayerID);
        }

        Out.RootSceneNode = InstantiateNodeRecursive(
            SceneTree, SceneTree.GetRoot(), RootNodeDescriptor, static_cast<int32_t>(Out.Entity));
        if (Out.RootSceneNode)
        {
            Out.RootSceneNode->SetLocalPosition(WorldPosition);
        }
        return Out;
    }

    size_t GetInitializerCount() const noexcept { return mComponentInitializers.size(); }

private:
    static Engine::Node2D* InstantiateNodeRecursive(
        Engine::SceneTree2D& Tree,
        Engine::Node2D& Parent,
        const PackedNode2DDescriptor& Desc,
        int32_t BoundEntityId)
    {
        Engine::Node2D* Node = Tree.SpawnChild(Parent, Desc.Name, Desc.RoleTag);
        if (!Node)
            return nullptr;

        Node->SetBoundEntityId(BoundEntityId);
        Node->SetLocalPosition(Desc.LocalPosition);
        Node->SetImageID(Desc.ImageID);
        Node->SetElevationSortLayer(Desc.ElevationSortLayer);
        Node->SetZIndexOffset(Desc.ZIndexOffset);

        if (!Desc.BindToParentSocket.empty())
            Node->BindToParentSocket(Desc.BindToParentSocket);

        for (const auto& Sock : Desc.Sockets)
            Node->DefineSocket(Sock.Name, Sock.LocalOffset);

        for (const auto& ChildDesc : Desc.Children)
            InstantiateNodeRecursive(Tree, *Node, ChildDesc, BoundEntityId);

        return Node;
    }

    std::vector<ComponentInitializerFn> mComponentInitializers;
};

} // namespace Engine::Scene
