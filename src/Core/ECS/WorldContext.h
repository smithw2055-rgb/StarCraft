#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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
//
// Modern Engine Optimization:
// Uses compile-time monotonic type slot IDs with an O(1) flat array fast-path
// (kMaxFastResources = 128) and an overflow map fallback, eliminating per-lookup
// std::type_index hashing and unordered_map bucket probing on hot paths.
// ============================================================================
class WorldContext
{
public:
    static constexpr size_t kMaxFastResources = 128;

    WorldContext() = default;
    ~WorldContext() = default;

    WorldContext(const WorldContext&) = delete;
    WorldContext& operator=(const WorldContext&) = delete;
    WorldContext(WorldContext&&) noexcept = default;
    WorldContext& operator=(WorldContext&&) noexcept = default;

    template <typename ResourceT, typename... Args>
    ResourceT& EmplaceResource(Args&&... InArgs)
    {
        const size_t Slot = GetFastResourceSlot<ResourceT>();
        auto Holder = std::make_unique<ResourceHolder<ResourceT>>(std::forward<Args>(InArgs)...);
        ResourceT& Ref = Holder->Data;
        if (Slot < kMaxFastResources)
        {
            if (!mFastSlots[Slot])
                ++mFastSlotCount;
            mFastSlots[Slot] = std::move(Holder);
        }
        else
        {
            mOverflowResources[std::type_index(typeid(ResourceT))] = std::move(Holder);
        }
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
        const size_t Slot = GetFastResourceSlot<ResourceT>();
        if (Slot < kMaxFastResources)
        {
            auto& Ptr = mFastSlots[Slot];
            return Ptr ? &static_cast<ResourceHolder<ResourceT>*>(Ptr.get())->Data : nullptr;
        }
        auto It = mOverflowResources.find(std::type_index(typeid(ResourceT)));
        if (It == mOverflowResources.end() || !It->second)
            return nullptr;
        return &static_cast<ResourceHolder<ResourceT>*>(It->second.get())->Data;
    }

    template <typename ResourceT>
    const ResourceT* GetResource() const
    {
        const size_t Slot = GetFastResourceSlot<ResourceT>();
        if (Slot < kMaxFastResources)
        {
            const auto& Ptr = mFastSlots[Slot];
            return Ptr ? &static_cast<const ResourceHolder<ResourceT>*>(Ptr.get())->Data : nullptr;
        }
        auto It = mOverflowResources.find(std::type_index(typeid(ResourceT)));
        if (It == mOverflowResources.end() || !It->second)
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
        const size_t Slot = GetFastResourceSlot<ResourceT>();
        if (Slot < kMaxFastResources)
        {
            if (mFastSlots[Slot])
            {
                mFastSlots[Slot].reset();
                if (mFastSlotCount > 0)
                    --mFastSlotCount;
            }
            return;
        }
        mOverflowResources.erase(std::type_index(typeid(ResourceT)));
    }

    void Clear()
    {
        for (auto& Slot : mFastSlots)
            Slot.reset();
        mFastSlotCount = 0;
        mOverflowResources.clear();
    }

    size_t GetResourceCount() const
    {
        return mFastSlotCount + mOverflowResources.size();
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

    inline static size_t sNextFastResourceSlot = 0;

    template <typename ResourceT>
    static size_t GetFastResourceSlot()
    {
        static const size_t sSlot = sNextFastResourceSlot++;
        return sSlot;
    }

    std::array<std::unique_ptr<IResourceHolder>, kMaxFastResources> mFastSlots{};
    size_t mFastSlotCount = 0;
    std::unordered_map<std::type_index, std::unique_ptr<IResourceHolder>> mOverflowResources;
};

} // namespace Core::ECS
