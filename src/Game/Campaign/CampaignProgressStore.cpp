#include "Game/Campaign/CampaignProgressStore.h"

#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <filesystem>
#include <string>

namespace Game::Campaign
{

namespace
{

constexpr const char* kCampaignProgressPath = "artifacts/campaign_progress.dat";

} // namespace

std::array<std::array<int, 3>, 2> LoadCampaignProgressCounts()
{
    std::array<std::array<int, 3>, 2> Counts = {};
    for (auto& Set : Counts)
        Set.fill(1);

    FILE* File = std::fopen(kCampaignProgressPath, "rb");
    if (!File)
        return Counts;

    int Set = 0;
    int Race = 0;
    int Count = 0;
    while (std::fscanf(File, "%d %d %d", &Set, &Race, &Count) == 3)
    {
        if (Set >= 0 && Set < 2 && Race >= 0 && Race < 3)
            Counts[(size_t)Set][(size_t)Race] = std::clamp(Count, 1, 10);
    }
    std::fclose(File);
    return Counts;
}

void SaveCampaignProgressCount(int Set, int Race, int Count)
{
    if (Set < 0 || Set >= 2 || Race < 0 || Race >= 3)
        return;

    auto Counts = LoadCampaignProgressCounts();
    Counts[(size_t)Set][(size_t)Race] = std::max(Counts[(size_t)Set][(size_t)Race], std::clamp(Count, 1, 10));

    std::error_code EC;
    std::filesystem::create_directories("artifacts", EC);

    FILE* File = std::fopen(kCampaignProgressPath, "wb");
    if (!File)
    {
        Core::Utils::Logger::Warning("CampaignProgress",
            std::string("Unable to write ") + kCampaignProgressPath);
        return;
    }

    for (int S = 0; S < 2; ++S)
    {
        for (int R = 0; R < 3; ++R)
            std::fprintf(File, "%d %d %d\n", S, R, Counts[(size_t)S][(size_t)R]);
    }
    std::fclose(File);
}

} // namespace Game::Campaign
