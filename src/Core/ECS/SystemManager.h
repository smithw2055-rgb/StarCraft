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

// === Inlined from src/Core/ECS/SystemManager.cpp ===

#include "Core/Utils/Logger.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace Core::ECS
{

inline void SystemManager::RegisterSystem(std::unique_ptr<ISystem> System)
{
    if (!System)
        return;
    const SystemPhase Phase = System->GetPhase();
    mSystems.push_back({Phase, std::move(System)});
    std::stable_sort(mSystems.begin(), mSystems.end(), [](const RegisteredSystem& A, const RegisteredSystem& B) {
        return static_cast<uint8_t>(A.Phase) < static_cast<uint8_t>(B.Phase);
    });
}

inline void SystemManager::RegisterSystemInPhase(SystemPhase Phase, std::unique_ptr<ISystem> System)
{
    if (!System)
        return;
    mSystems.push_back({Phase, std::move(System)});
    std::stable_sort(mSystems.begin(), mSystems.end(), [](const RegisteredSystem& A, const RegisteredSystem& B) {
        return static_cast<uint8_t>(A.Phase) < static_cast<uint8_t>(B.Phase);
    });
}

inline void SystemManager::UpdatePhase(SystemPhase Phase, float DeltaTime)
{
    mEntityMgr.ActivateWorld();
    bool ExecutedAny = false;
    for (auto& Entry : mSystems)
    {
        if (Entry.Phase != Phase || !Entry.Instance)
            continue;
        Entry.Instance->Update(DeltaTime);
        ExecutedAny = true;
    }

    // Unity DOTS EntityCommandBufferSystem / Godot SceneTree deferred barrier:
    // Flush any structural commands queued during this phase.
    if (ExecutedAny || !mEntityMgr.Commands().IsEmpty())
    {
        mEntityMgr.FlushCommands();
        ++mCommandBarrierFlushCount;
    }
}

inline void SystemManager::UpdateSimulationStep(float FixedDeltaTime)
{
    UpdatePhase(SystemPhase::Input, FixedDeltaTime);
    UpdatePhase(SystemPhase::NetworkLockstep, FixedDeltaTime);
    UpdatePhase(SystemPhase::FixedSimulation, FixedDeltaTime);
    UpdatePhase(SystemPhase::TransformPropagation, FixedDeltaTime);
}

inline void SystemManager::UpdatePresentationFrame(float FrameDeltaTime)
{
    UpdatePhase(SystemPhase::Presentation, FrameDeltaTime);
    UpdatePhase(SystemPhase::RenderExtract, FrameDeltaTime);
}

inline void SystemManager::UpdateAll(float DeltaTime)
{
    mEntityMgr.ActivateWorld();
    const char* DebugPathing = std::getenv("STARCRAFT_DEBUG_PATHING");
    const char* ProfileSystems = std::getenv("STARCRAFT_PROFILE_SYSTEMS");
    const bool MeasureSystems =
        (DebugPathing && DebugPathing[0] != '\0' &&
         DebugPathing[0] != '0') ||
        (ProfileSystems && ProfileSystems[0] != '\0' &&
         ProfileSystems[0] != '0');

    struct ProfileSample
    {
        uint64_t Calls = 0;
        uint64_t TotalMicroseconds = 0;
        uint64_t MaximumMicroseconds = 0;
    };
    static std::unordered_map<std::string, ProfileSample> Samples;
    static auto LastReport = std::chrono::steady_clock::now();

    mLastProfileEntries.clear();
    mLastProfileEntries.reserve(mSystems.size());

    SystemPhase CurrentPhase = mSystems.empty() ? SystemPhase::FixedSimulation : mSystems.front().Phase;

    for (auto& Entry : mSystems)
    {
        auto& Sys = Entry.Instance;
        if (!Sys)
            continue;

        // Phase transition barrier: flush deferred EntityCommandBuffer between phases
        if (Entry.Phase != CurrentPhase)
        {
            mEntityMgr.FlushCommands();
            ++mCommandBarrierFlushCount;
            CurrentPhase = Entry.Phase;
        }

        const auto Begin = std::chrono::steady_clock::now();
        Sys->Update(DeltaTime);
        const auto Microseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - Begin).count();

        mLastProfileEntries.push_back({
            std::string(Sys->GetName()),
            Entry.Phase,
            static_cast<uint64_t>(std::max<int64_t>(0, Microseconds))
        });

        if (MeasureSystems)
        {
            auto& Sample = Samples[std::string(Sys->GetName())];
            ++Sample.Calls;
            Sample.TotalMicroseconds += (uint64_t)Microseconds;
            Sample.MaximumMicroseconds =
                std::max(Sample.MaximumMicroseconds, (uint64_t)Microseconds);
        }
    }

    // Final end-of-schedule barrier flush
    mEntityMgr.FlushCommands();
    ++mCommandBarrierFlushCount;

    if (MeasureSystems)
    {
        const auto Now = std::chrono::steady_clock::now();
        if (Now - LastReport >= std::chrono::seconds(1))
        {
            for (const auto& [Name, Sample] : Samples)
            {
                if (Sample.Calls == 0 || Sample.TotalMicroseconds < 1000)
                    continue;
                Core::Utils::Logger::Info(
                    "Profile",
                    "system name=" + Name +
                    " calls=" + std::to_string(Sample.Calls) +
                    " total_us=" + std::to_string(Sample.TotalMicroseconds) +
                    " average_us=" +
                        std::to_string(Sample.TotalMicroseconds / Sample.Calls) +
                    " max_us=" +
                        std::to_string(Sample.MaximumMicroseconds));
            }
            Samples.clear();
            LastReport = Now;
        }
    }
}

} // namespace Core::ECS
