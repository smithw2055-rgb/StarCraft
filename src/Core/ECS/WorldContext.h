#pragma once

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace Core::ECS
{

// ============================================================================
// WorldContext (Unity DOTS Singleton Component / Bevy World Resource Container)
// Stores world-scoped state (Economy, Upgrades, Map pointer, FogOfWar, RNG seed)
// per-World instead of relying on global static variables.
// ============================================================================
class WorldContext
{
public:
    template <typename ResourceT, typename... Args>
    ResourceT& EmplaceResource(Args&&... InArgs)
    {
        const std::type_index Key(typeid(ResourceT));
        auto Holder = std::make_unique<ResourceHolder<ResourceT>>(std::forward<Args>(InArgs)...);
        ResourceT& Ref = Holder->Data;
        mResources[Key] = std::move(Holder);
        return Ref;
    }

    template <typename ResourceT>
    ResourceT& SetResource(ResourceT Value)
    {
        return EmplaceResource<ResourceT>(std::move(Value));
    }

    template <typename ResourceT>
    ResourceT& GetOrCreateResource()
    {
        if (auto* Existing = GetResource<ResourceT>())
            return *Existing;
        return EmplaceResource<ResourceT>();
    }

    template <typename ResourceT>
    ResourceT* GetResource()
    {
        const std::type_index Key(typeid(ResourceT));
        auto It = mResources.find(Key);
        if (It == mResources.end() || !It->second)
            return nullptr;
        return &static_cast<ResourceHolder<ResourceT>*>(It->second.get())->Data;
    }

    template <typename ResourceT>
    const ResourceT* GetResource() const
    {
        const std::type_index Key(typeid(ResourceT));
        auto It = mResources.find(Key);
        if (It == mResources.end() || !It->second)
            return nullptr;
        return &static_cast<const ResourceHolder<ResourceT>*>(It->second.get())->Data;
    }

    template <typename ResourceT>
    bool HasResource() const
    {
        return GetResource<ResourceT>() != nullptr;
    }

    template <typename ResourceT>
    void RemoveResource()
    {
        mResources.erase(std::type_index(typeid(ResourceT)));
    }

    void Clear()
    {
        mResources.clear();
    }

    size_t GetResourceCount() const
    {
        return mResources.size();
    }

private:
    struct IResourceHolder
    {
        virtual ~IResourceHolder() = default;
    };

    template <typename ResourceT>
    struct ResourceHolder final : public IResourceHolder
    {
        template <typename... Args>
        explicit ResourceHolder(Args&&... InArgs)
            : Data(std::forward<Args>(InArgs)...)
        {
        }

        ResourceT Data;
    };

    std::unordered_map<std::type_index, std::unique_ptr<IResourceHolder>> mResources;
};

} // namespace Core::ECS
