#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Core::ECS
{

using SubscriptionID = uint64_t;
constexpr size_t MaxEventTypes = 64;

inline uint8_t NextEventTypeID = 0;

template <typename T>
uint8_t GetEventTypeID()
{
    static uint8_t ID = NextEventTypeID++;
    return ID;
}

class IEventChannel
{
public:
    virtual ~IEventChannel() = default;
    virtual void ClearQueue() = 0;
    virtual void ClearSubscribers() = 0;
    virtual size_t QueuedCount() const = 0;
};

template <typename EventT>
class EventChannel final : public IEventChannel
{
public:
    using HandlerFn = std::function<void(const EventT&)>;

    struct SubscriberEntry
    {
        SubscriptionID Id = 0;
        HandlerFn Handler;
    };

    SubscriptionID Subscribe(HandlerFn Handler)
    {
        const SubscriptionID Id = ++mLastSubscriptionId;
        mSubscribers.push_back({Id, std::move(Handler)});
        return Id;
    }

    bool Unsubscribe(SubscriptionID Id)
    {
        auto It = std::remove_if(
            mSubscribers.begin(),
            mSubscribers.end(),
            [Id](const SubscriberEntry& Entry) { return Entry.Id == Id; });
        if (It == mSubscribers.end())
            return false;
        mSubscribers.erase(It, mSubscribers.end());
        return true;
    }

    void PublishImmediate(const EventT& Event)
    {
        // Copy subscriber list size/snapshot in case a handler subscribes/unsubscribes
        const size_t Count = mSubscribers.size();
        for (size_t i = 0; i < Count && i < mSubscribers.size(); ++i)
        {
            if (mSubscribers[i].Handler)
                mSubscribers[i].Handler(Event);
        }
    }

    void Enqueue(const EventT& Event)
    {
        mQueue.push_back(Event);
    }

    void Enqueue(EventT&& Event)
    {
        mQueue.push_back(std::move(Event));
    }

    void PublishAndEnqueue(const EventT& Event)
    {
        mQueue.push_back(Event);
        PublishImmediate(mQueue.back());
    }

    const std::vector<EventT>& GetQueue() const
    {
        return mQueue;
    }

    std::vector<EventT> DrainQueue()
    {
        std::vector<EventT> Out;
        Out.swap(mQueue);
        return Out;
    }

    void DispatchQueued()
    {
        for (const EventT& Ev : mQueue)
            PublishImmediate(Ev);
        mQueue.clear();
    }

    void ClearQueue() override
    {
        mQueue.clear();
    }

    void ClearSubscribers() override
    {
        mSubscribers.clear();
    }

    size_t QueuedCount() const override
    {
        return mQueue.size();
    }

    size_t SubscriberCount() const
    {
        return mSubscribers.size();
    }

private:
    SubscriptionID mLastSubscriptionId = 0;
    std::vector<SubscriberEntry> mSubscribers;
    std::vector<EventT> mQueue;
};

// Modern dual-mode EventBus inspired by Godot Signals (immediate Subscribe/Publish)
// and Unity DOTS / Bevy Events<T> (frame-buffered Enqueue/Read queues).
class EventBus
{
public:
    template <typename EventT>
    SubscriptionID Subscribe(std::function<void(const EventT&)> Handler)
    {
        return GetOrCreateChannel<EventT>()->Subscribe(std::move(Handler));
    }

    template <typename EventT>
    bool Unsubscribe(SubscriptionID Id)
    {
        auto* Channel = FindChannel<EventT>();
        return Channel ? Channel->Unsubscribe(Id) : false;
    }

    // Immediate signal dispatch (Godot Signal style)
    template <typename EventT>
    void Publish(const EventT& Event)
    {
        GetOrCreateChannel<EventT>()->PublishAndEnqueue(Event);
    }

    // Buffered event queue (Unity DOTS / Bevy EventReader style)
    template <typename EventT>
    void Enqueue(const EventT& Event)
    {
        GetOrCreateChannel<EventT>()->Enqueue(Event);
    }

    template <typename EventT>
    void Enqueue(EventT&& Event)
    {
        GetOrCreateChannel<EventT>()->Enqueue(std::move(Event));
    }

    // Both enqueues into the frame buffer and notifies immediate signal subscribers
    template <typename EventT>
    void Emit(const EventT& Event)
    {
        GetOrCreateChannel<EventT>()->PublishAndEnqueue(Event);
    }

    template <typename EventT>
    const std::vector<EventT>& Read() const
    {
        static const std::vector<EventT> kEmpty;
        const auto* Channel = FindChannel<EventT>();
        return Channel ? Channel->GetQueue() : kEmpty;
    }

    template <typename EventT>
    std::vector<EventT> Consume()
    {
        auto* Channel = FindChannel<EventT>();
        return Channel ? Channel->DrainQueue() : std::vector<EventT>{};
    }

    template <typename EventT>
    void DispatchQueued()
    {
        if (auto* Channel = FindChannel<EventT>())
            Channel->DispatchQueued();
    }

    template <typename EventT>
    void ClearQueue()
    {
        if (auto* Channel = FindChannel<EventT>())
            Channel->ClearQueue();
    }

    void ClearAllQueues()
    {
        for (auto& Channel : mChannels)
        {
            if (Channel)
                Channel->ClearQueue();
        }
    }

    template <typename EventT>
    const std::vector<EventT>& ReadEvents() const { return Read<EventT>(); }

    template <typename EventT>
    std::vector<EventT> DrainEvents() { return Consume<EventT>(); }

    using SubscriptionID = Core::ECS::SubscriptionID;

    void ClearBufferedEvents() { ClearAllQueues(); }

    size_t GetTotalBufferedEventCount() const
    {
        size_t Total = 0;
        for (const auto& Channel : mChannels)
        {
            if (Channel)
                Total += Channel->QueuedCount();
        }
        return Total;
    }

    void Reset()
    {
        for (auto& Channel : mChannels)
        {
            if (Channel)
            {
                Channel->ClearQueue();
                Channel->ClearSubscribers();
            }
        }
    }

private:
    template <typename EventT>
    EventChannel<EventT>* GetOrCreateChannel()
    {
        const uint8_t ID = GetEventTypeID<EventT>();
        if (!mChannels[ID])
            mChannels[ID] = std::make_unique<EventChannel<EventT>>();
        return static_cast<EventChannel<EventT>*>(mChannels[ID].get());
    }

    template <typename EventT>
    EventChannel<EventT>* FindChannel()
    {
        const uint8_t ID = GetEventTypeID<EventT>();
        if (ID >= MaxEventTypes || !mChannels[ID])
            return nullptr;
        return static_cast<EventChannel<EventT>*>(mChannels[ID].get());
    }

    template <typename EventT>
    const EventChannel<EventT>* FindChannel() const
    {
        const uint8_t ID = GetEventTypeID<EventT>();
        if (ID >= MaxEventTypes || !mChannels[ID])
            return nullptr;
        return static_cast<const EventChannel<EventT>*>(mChannels[ID].get());
    }

    std::array<std::unique_ptr<IEventChannel>, MaxEventTypes> mChannels;
};

} // namespace Core::ECS
