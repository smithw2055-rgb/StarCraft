#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace Game::Client::UI::ViewModel
{

enum class HudDirtyFlags : uint32_t
{
    None        = 0,
    Resources   = 1u << 0,
    Selection   = 1u << 1,
    CommandCard = 1u << 2,
    InfoPanel   = 1u << 3,
    Minimap     = 1u << 4,
    All         = Resources | Selection | CommandCard | InfoPanel | Minimap
};

inline constexpr HudDirtyFlags operator|(HudDirtyFlags A, HudDirtyFlags B)
{
    return static_cast<HudDirtyFlags>(static_cast<uint32_t>(A) | static_cast<uint32_t>(B));
}

inline constexpr HudDirtyFlags operator&(HudDirtyFlags A, HudDirtyFlags B)
{
    return static_cast<HudDirtyFlags>(static_cast<uint32_t>(A) & static_cast<uint32_t>(B));
}

inline constexpr HudDirtyFlags& operator|=(HudDirtyFlags& A, HudDirtyFlags B)
{
    A = A | B;
    return A;
}

inline constexpr bool HasDirtyFlag(HudDirtyFlags Mask, HudDirtyFlags Flag)
{
    return (static_cast<uint32_t>(Mask) & static_cast<uint32_t>(Flag)) != 0u;
}

// ============================================================================
// ObservableProperty<T>: Reactive MVVM property with version & dirty tracking
// Referenced from Unity UI Toolkit INotifyValueChanged<T> & Godot Control Signals:
// - Suppresses redundant notifications when assigned an equal value
// - Maintains monotonic version counter for fast zero-allocation polling
// - Supports immediate observer callbacks for push-based UI binding
// ============================================================================
template <typename T>
class ObservableProperty
{
public:
    using Callback = std::function<void(const T& OldValue, const T& NewValue)>;
    using SubscriptionId = uint32_t;

    ObservableProperty() = default;
    explicit ObservableProperty(T InitialValue)
        : mValue(std::move(InitialValue))
    {
    }

    const T& Get() const { return mValue; }
    operator const T&() const { return mValue; }

    bool Set(const T& NewValue)
    {
        if (mValue == NewValue)
            return false;

        T OldValue = mValue;
        mValue = NewValue;
        ++mVersion;
        mDirty = true;

        for (const auto& Entry : mListeners)
        {
            if (Entry.Fn)
                Entry.Fn(OldValue, mValue);
        }
        return true;
    }

    SubscriptionId Subscribe(Callback Fn)
    {
        const SubscriptionId Id = mNextSubscriptionId++;
        mListeners.push_back({Id, std::move(Fn)});
        return Id;
    }

    void Unsubscribe(SubscriptionId Id)
    {
        mListeners.erase(
            std::remove_if(mListeners.begin(), mListeners.end(),
                           [Id](const ListenerEntry& E) { return E.Id == Id; }),
            mListeners.end());
    }

    void ClearSubscribers()
    {
        mListeners.clear();
    }

    bool IsDirty() const { return mDirty; }

    bool ConsumeDirty()
    {
        const bool WasDirty = mDirty;
        mDirty = false;
        return WasDirty;
    }

    void MarkDirty()
    {
        ++mVersion;
        mDirty = true;
    }

    uint64_t GetVersion() const { return mVersion; }

private:
    struct ListenerEntry
    {
        SubscriptionId Id = 0;
        Callback Fn;
    };

    T mValue{};
    uint64_t mVersion = 1;
    bool mDirty = true;
    SubscriptionId mNextSubscriptionId = 1;
    std::vector<ListenerEntry> mListeners;
};

} // namespace Game::Client::UI::ViewModel
