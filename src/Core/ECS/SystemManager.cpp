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
    mSystems.push_back(std::move(System));
}

void SystemManager::UpdateAll(float DeltaTime)
{
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

    for (auto& Sys : mSystems)
    {
        if (!MeasureSystems)
        {
            Sys->Update(DeltaTime);
            continue;
        }
        const auto Begin = std::chrono::steady_clock::now();
        Sys->Update(DeltaTime);
        const auto Microseconds =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - Begin).count();
        auto& Sample = Samples[std::string(Sys->GetName())];
        ++Sample.Calls;
        Sample.TotalMicroseconds += (uint64_t)Microseconds;
        Sample.MaximumMicroseconds =
            std::max(Sample.MaximumMicroseconds, (uint64_t)Microseconds);
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
