#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/ECS/EntityManager.h"

namespace Engine::Debug
{

struct PoolMemoryInspectorEntry
{
    uint8_t Slot = 0;
    size_t ActiveInstances = 0;
    size_t ComponentByteSize = 0;
    size_t TotalDensePayloadBytes = 0;
    bool SupportsBinarySerialization = false;
};

struct WorldMemoryReport
{
    size_t LivingEntities = 0;
    size_t ActiveComponentPools = 0;
    size_t TotalComponentInstances = 0;
    size_t TotalDenseComponentBytes = 0;
    std::vector<PoolMemoryInspectorEntry> Pools;
};

struct SystemProfileEntry
{
    std::string SystemName;
    uint64_t InvocationCount = 0;
    double TotalMicroseconds = 0.0;
    double MaxMicroseconds = 0.0;
    double LastMicroseconds = 0.0;
};

// ============================================================================
// EngineTelemetryServer (Phase 42):
// - Runtime inspector and telemetry profiler for the general-purpose Engine
//   foundation (inspired by Godot Debugger / Profiler & Unity Entity Debugger).
// ============================================================================
class EngineTelemetryServer
{
public:
    void RecordSystemSample(const std::string& SystemName, double Microseconds)
    {
        auto& Entry = mSystemProfiles[SystemName];
        if (Entry.SystemName.empty())
            Entry.SystemName = SystemName;
        ++Entry.InvocationCount;
        Entry.LastMicroseconds = Microseconds;
        Entry.TotalMicroseconds += Microseconds;
        Entry.MaxMicroseconds = std::max(Entry.MaxMicroseconds, Microseconds);
        ++mTotalSamplesRecorded;
    }

    const SystemProfileEntry* FindSystemProfile(const std::string& SystemName) const
    {
        auto It = mSystemProfiles.find(SystemName);
        return It != mSystemProfiles.end() ? &It->second : nullptr;
    }

    WorldMemoryReport InspectWorld(const Core::ECS::EntityManager& EM) const
    {
        WorldMemoryReport Report{};
        Report.LivingEntities = EM.GetEntityCount();

        for (size_t Slot = 0; Slot < Core::ECS::MaxComponents; ++Slot)
        {
            const Core::ECS::IComponentPool* Pool = EM.GetPoolBySlot(static_cast<uint8_t>(Slot));
            if (!Pool || Pool->Size() == 0)
                continue;

            PoolMemoryInspectorEntry Entry{};
            Entry.Slot = static_cast<uint8_t>(Slot);
            Entry.ActiveInstances = Pool->Size();
            Entry.ComponentByteSize = Pool->GetComponentByteSize();
            Entry.TotalDensePayloadBytes = Entry.ActiveInstances * Entry.ComponentByteSize;
            Entry.SupportsBinarySerialization = Pool->HasBinarySerializer();

            ++Report.ActiveComponentPools;
            Report.TotalComponentInstances += Entry.ActiveInstances;
            Report.TotalDenseComponentBytes += Entry.TotalDensePayloadBytes;
            Report.Pools.push_back(Entry);
        }
        return Report;
    }

    void Reset()
    {
        mSystemProfiles.clear();
        mTotalSamplesRecorded = 0;
    }

    size_t GetProfiledSystemCount() const noexcept { return mSystemProfiles.size(); }
    uint64_t GetTotalSamplesRecorded() const noexcept { return mTotalSamplesRecorded; }

private:
    std::unordered_map<std::string, SystemProfileEntry> mSystemProfiles;
    uint64_t mTotalSamplesRecorded = 0;
};

} // namespace Engine::Debug
