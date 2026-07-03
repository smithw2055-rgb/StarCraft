#pragma once

#include <array>

namespace Game::Campaign
{

std::array<std::array<int, 3>, 2> LoadCampaignProgressCounts();
void SaveCampaignProgressCount(int Set, int Race, int Count);

} // namespace Game::Campaign
