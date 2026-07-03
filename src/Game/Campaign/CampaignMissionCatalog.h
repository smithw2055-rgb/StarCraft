#pragma once

#include <string>

namespace Game::Campaign
{

struct CampaignMissionRef
{
    bool Valid = false;
    bool Expansion = false;
    int Race = 0;
    int Mission = 0;
    std::string Path;
};

CampaignMissionRef ResolveCampaignMission(const std::string& Action);
CampaignMissionRef ResolveCampaignMission(bool Expansion, int Race, int Mission);
std::string BuildCampaignMissionAction(bool Expansion, int Race, int Mission);
bool IsCampaignMissionAction(const std::string& Action);

} // namespace Game::Campaign
