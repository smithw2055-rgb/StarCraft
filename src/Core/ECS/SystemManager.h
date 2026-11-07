#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <string_view>

#include "Core/ECS/EntityManager.h"

namespace Core::ECS
{

// ============================================================================
// SystemPhase (Unity PlayerLoop / Godot MainLoop Staged Schedule)
// ============================================================================
enum class SystemPhase : uint8_t
{
    Input = 0,
    NetworkLockstep,
    FixedSimulation,
    TransformPropagation,
    Presentation,
    RenderExtract,
    Count
};

struct SystemProfileEntry
{
    std::string Name;
    SystemPhase Phase = SystemPhase::FixedSimulation;
    uint64_t LastDurationMicroseconds = 0;
};

class ISystem
{
public:
    virtual ~ISystem() = default;
    virtual void Update(float DeltaTime) = 0;
    virtual std::string_view GetName() const = 0;
    virtual SystemPhase GetPhase() const { return SystemPhase::FixedSimulation; }

    // Hierarchical introspection hooks for SystemGroup traversal
    virtual size_t GetSubSystemCount() const { return 0; }
    virtual ISystem* GetSubSystemAt(size_t /*Index*/) { return nullptr; }
    virtual const ISystem* GetSubSystemAt(size_t /*Index*/) const { return nullptr; }
};

class SystemManager
{
public:
    void RegisterSystem(std::unique_ptr<ISystem> System);
    void RegisterSystemInPhase(SystemPhase Phase, std::unique_ptr<ISystem> System);

    // Executes all phases in deterministic order with EntityCommandBuffer sync barriers
    void UpdateAll(float DeltaTime);

    // Executes a single SystemPhase and flushes the World EntityCommandBuffer barrier
    void UpdatePhase(SystemPhase Phase, float DeltaTime);

    // Unity PlayerLoop / Godot physics_process vs process split:
    // Advances deterministic simulation phases (Input -> NetworkLockstep -> FixedSimulation -> TransformPropagation)
    void UpdateSimulationStep(float FixedDeltaTime);

    // Advances variable-rate presentation & render extraction phases (Presentation -> RenderExtract)
    void UpdatePresentationFrame(float FrameDeltaTime);

    template <typename SystemT>
    SystemT* FindSystem()
    {
        for (auto& Entry : mSystems)
        {
            if (auto* Found = FindSystemRecursive<SystemT>(Entry.Instance.get()))
                return Found;
        }
        return nullptr;
    }

    template <typename SystemT>
    const SystemT* FindSystem() const
    {
        for (const auto& Entry : mSystems)
        {
            if (const auto* Found = FindSystemRecursive<SystemT>(Entry.Instance.get()))
                return Found;
        }
        return nullptr;
    }

    EntityManager& GetEntityManager() { return mEntityMgr; }
    const EntityManager& GetEntityManager() const { return mEntityMgr; }

    WorldContext& GetContext() { return mEntityMgr.Context(); }
    const WorldContext& GetContext() const { return mEntityMgr.Context(); }

    EventBus& GetEventBus() { return mEntityMgr.Events(); }
    const EventBus& GetEventBus() const { return mEntityMgr.Events(); }

    size_t GetSystemCount() const { return mSystems.size(); }
    uint64_t GetCommandBarrierFlushCount() const { return mCommandBarrierFlushCount; }
    const std::vector<SystemProfileEntry>& GetLastProfileEntries() const { return mLastProfileEntries; }

private:
    template <typename SystemT>
    static SystemT* FindSystemRecursive(ISystem* Node)
    {
        if (!Node)
            return nullptr;
        if (auto* Casted = dynamic_cast<SystemT*>(Node))
            return Casted;
        const size_t SubCount = Node->GetSubSystemCount();
        for (size_t i = 0; i < SubCount; ++i)
        {
            if (auto* Found = FindSystemRecursive<SystemT>(Node->GetSubSystemAt(i)))
                return Found;
        }
        return nullptr;
    }

    template <typename SystemT>
    static const SystemT* FindSystemRecursive(const ISystem* Node)
    {
        if (!Node)
            return nullptr;
        if (const auto* Casted = dynamic_cast<const SystemT*>(Node))
            return Casted;
        const size_t SubCount = Node->GetSubSystemCount();
        for (size_t i = 0; i < SubCount; ++i)
        {
            if (const auto* Found = FindSystemRecursive<SystemT>(Node->GetSubSystemAt(i)))
                return Found;
        }
        return nullptr;
    }

    struct RegisteredSystem
    {
        SystemPhase Phase = SystemPhase::FixedSimulation;
        std::unique_ptr<ISystem> Instance;
    };

    EntityManager mEntityMgr;
    std::vector<RegisteredSystem> mSystems;
    std::vector<SystemProfileEntry> mLastProfileEntries;
    uint64_t mCommandBarrierFlushCount = 0;
};

} // namespace Core::ECS
