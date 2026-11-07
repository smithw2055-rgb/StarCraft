#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/ECS/SystemManager.h"

namespace Core::ECS
{

struct SubSystemProfileEntry
{
    std::string Name;
    bool Enabled = true;
    uint64_t LastDurationMicroseconds = 0;
    uint64_t TotalCalls = 0;
};

// ============================================================================
// SystemGroup: Hierarchical System Pipeline (Unity DOTS ComponentSystemGroup
// & Godot SceneTree Sub-Group Pattern)
// - Allows decomposing monolithic systems (e.g. Movement, Combat, Animation)
//   into ordered, independently toggleable sub-systems within a SystemPhase.
// - Supports optional per-sub-system EntityCommandBuffer synchronization barriers
//   and fine-grained microsecond profiling per sub-stage.
// ============================================================================
class SystemGroup : public ISystem
{
public:
    explicit SystemGroup(std::string GroupName,
                         SystemPhase Phase = SystemPhase::FixedSimulation,
                         EntityManager* BoundWorld = nullptr)
        : mGroupName(std::move(GroupName))
        , mPhase(Phase)
        , mBoundWorld(BoundWorld)
    {
    }

    ~SystemGroup() override = default;

    std::string_view GetName() const override { return mGroupName; }
    SystemPhase GetPhase() const override { return mPhase; }

    void BindWorld(EntityManager* World) { mBoundWorld = World; }

    template <typename SubSystemT, typename... Args>
    SubSystemT* AddSubSystem(bool FlushCommandBarrierAfter, Args&&... InArgs)
    {
        auto Instance = std::make_unique<SubSystemT>(std::forward<Args>(InArgs)...);
        SubSystemT* RawPtr = Instance.get();
        AddSubSystemInstance(std::move(Instance), FlushCommandBarrierAfter);
        return RawPtr;
    }

    void AddSubSystemInstance(std::unique_ptr<ISystem> SubSystem, bool FlushCommandBarrierAfter = false)
    {
        if (!SubSystem)
            return;
        SubSystemSlot Slot;
        Slot.Name = std::string(SubSystem->GetName());
        Slot.Enabled = true;
        Slot.FlushBarrierAfter = FlushCommandBarrierAfter;
        Slot.Instance = std::move(SubSystem);
        mSubSystems.push_back(std::move(Slot));
    }

    bool SetSubSystemEnabled(std::string_view SubSystemName, bool Enabled)
    {
        for (auto& Slot : mSubSystems)
        {
            if (Slot.Name == SubSystemName)
            {
                Slot.Enabled = Enabled;
                return true;
            }
        }
        return false;
    }

    bool IsSubSystemEnabled(std::string_view SubSystemName) const
    {
        for (const auto& Slot : mSubSystems)
        {
            if (Slot.Name == SubSystemName)
                return Slot.Enabled;
        }
        return false;
    }

    template <typename SubSystemT>
    SubSystemT* FindSubSystem()
    {
        for (auto& Slot : mSubSystems)
        {
            if (auto* Casted = dynamic_cast<SubSystemT*>(Slot.Instance.get()))
                return Casted;
        }
        return nullptr;
    }

    void Update(float DeltaTime) override
    {
        mProfiles.clear();
        mProfiles.reserve(mSubSystems.size());

        EntityManager* World = mBoundWorld ? mBoundWorld : EntityManager::GetActiveWorld();

        for (auto& Slot : mSubSystems)
        {
            if (!Slot.Instance || !Slot.Enabled)
            {
                mProfiles.push_back({Slot.Name, Slot.Enabled, 0u, Slot.TotalCalls});
                continue;
            }

            const auto Begin = std::chrono::steady_clock::now();
            Slot.Instance->Update(DeltaTime);
            const auto ElapsedUs = static_cast<uint64_t>(
                std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - Begin).count()));

            ++Slot.TotalCalls;
            Slot.LastDurationMicroseconds = ElapsedUs;

            if (Slot.FlushBarrierAfter && World)
            {
                World->FlushCommands();
                ++mSubBarrierFlushCount;
            }

            mProfiles.push_back({Slot.Name, true, ElapsedUs, Slot.TotalCalls});
        }
    }

    size_t GetSubSystemCount() const { return mSubSystems.size(); }
    uint64_t GetSubBarrierFlushCount() const { return mSubBarrierFlushCount; }
    const std::vector<SubSystemProfileEntry>& GetSubSystemProfiles() const { return mProfiles; }

private:
    struct SubSystemSlot
    {
        std::string Name;
        bool Enabled = true;
        bool FlushBarrierAfter = false;
        uint64_t LastDurationMicroseconds = 0;
        uint64_t TotalCalls = 0;
        std::unique_ptr<ISystem> Instance;
    };

    std::string mGroupName;
    SystemPhase mPhase = SystemPhase::FixedSimulation;
    EntityManager* mBoundWorld = nullptr;
    std::vector<SubSystemSlot> mSubSystems;
    std::vector<SubSystemProfileEntry> mProfiles;
    uint64_t mSubBarrierFlushCount = 0;
};

} // namespace Core::ECS
