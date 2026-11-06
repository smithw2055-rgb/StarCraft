#include "Core/ECS/SystemManager.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace Core::ECS
{

void SystemManager::RegisterSystem(std::unique_ptr<ISystem> System)
{
    if (!System)
        return;
    const SystemPhase Phase = System->GetPhase();
    mSystems.push_back({Phase, std::move(System)});
    std::stable_sort(mSystems.begin(), mSystems.end(), [](const RegisteredSystem& A, const RegisteredSystem& B) {
        return static_cast<uint8_t>(A.Phase) < static_cast<uint8_t>(B.Phase);
    });
}

void SystemManager::RegisterSystemInPhase(SystemPhase Phase, std::unique_ptr<ISystem> System)
{
    if (!System)
        return;
    mSystems.push_back({Phase, std::move(System)});
    std::stable_sort(mSystems.begin(), mSystems.end(), [](const RegisteredSystem& A, const RegisteredSystem& B) {
        return static_cast<uint8_t>(A.Phase) < static_cast<uint8_t>(B.Phase);
    });
}

void SystemManager::UpdatePhase(SystemPhase Phase, float DeltaTime)
{
    for (auto& Entry : mSystems)
    {
        if (Entry.Phase != Phase || !Entry.Instance)
            continue;
        Entry.Instance->Update(DeltaTime);
    }
}

void SystemManager::UpdateAll(float DeltaTime)
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

    for (auto& Entry : mSystems)
    {
        auto& Sys = Entry.Instance;
        if (!Sys)
            continue;

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
