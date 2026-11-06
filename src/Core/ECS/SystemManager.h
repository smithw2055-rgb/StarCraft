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
};

class SystemManager
{
public:
    void RegisterSystem(std::unique_ptr<ISystem> System);
    void RegisterSystemInPhase(SystemPhase Phase, std::unique_ptr<ISystem> System);

    void UpdateAll(float DeltaTime);
    void UpdatePhase(SystemPhase Phase, float DeltaTime);

    EntityManager& GetEntityManager() { return mEntityMgr; }
    const EntityManager& GetEntityManager() const { return mEntityMgr; }

    WorldContext& GetContext() { return mEntityMgr.Context(); }
    const WorldContext& GetContext() const { return mEntityMgr.Context(); }

    EventBus& GetEventBus() { return mEntityMgr.Events(); }
    const EventBus& GetEventBus() const { return mEntityMgr.Events(); }

    size_t GetSystemCount() const { return mSystems.size(); }
    const std::vector<SystemProfileEntry>& GetLastProfileEntries() const { return mLastProfileEntries; }

private:
    struct RegisteredSystem
    {
        SystemPhase Phase = SystemPhase::FixedSimulation;
        std::unique_ptr<ISystem> Instance;
    };

    EntityManager mEntityMgr;
    std::vector<RegisteredSystem> mSystems;
    std::vector<SystemProfileEntry> mLastProfileEntries;
};

} // namespace Core::ECS
