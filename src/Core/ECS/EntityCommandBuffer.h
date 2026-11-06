#pragma once

#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace Core::ECS
{

class EntityManager;
using EntityID = int;

// Deferred structural modification buffer inspired by Unity DOTS EntityCommandBuffer
// and Godot call_deferred. Allows systems to queue entity/component mutations during
// iteration and play them back deterministically at stage boundaries.
class EntityCommandBuffer
{
public:
    using CommandFn = std::function<void(EntityManager&)>;

    void Defer(CommandFn Command)
    {
        if (Command)
            mCommands.push_back(std::move(Command));
    }

    template <typename Fn>
    void CreateEntity(Fn&& OnCreated)
    {
        mCommands.push_back([Callback = std::forward<Fn>(OnCreated)](EntityManager& EM) mutable {
            Callback(EM);
        });
    }

    void DestroyEntity(EntityID Id);

    template <typename T>
    void AddComponent(EntityID Id, const T& InitialValue = T{});

    template <typename T>
    void RemoveComponent(EntityID Id);

    void Playback(EntityManager& EM)
    {
        if (mCommands.empty())
            return;
        std::vector<CommandFn> Pending;
        Pending.swap(mCommands);
        for (auto& Cmd : Pending)
        {
            if (Cmd)
                Cmd(EM);
        }
    }

    void Clear()
    {
        mCommands.clear();
    }

    bool IsEmpty() const
    {
        return mCommands.empty();
    }

    size_t Size() const
    {
        return mCommands.size();
    }

private:
    std::vector<CommandFn> mCommands;
};

} // namespace Core::ECS
