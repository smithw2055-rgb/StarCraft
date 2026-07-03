#pragma once

#include <string>

namespace Game::Client::UI::Glue
{

struct CampaignBriefingInfo
{
    std::string Title;
    std::string Text;
    std::string Status;
};

const char* CampaignSetName(bool Expansion);
const char* CampaignRaceName(int Race);
CampaignBriefingInfo LoadCampaignBriefingInfo(bool Expansion, int Race, int Mission);

} // namespace Game::Client::UI::Glue
