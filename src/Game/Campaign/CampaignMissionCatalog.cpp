#include "Game/Campaign/CampaignMissionCatalog.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Game::Campaign
{

namespace
{

constexpr const char* kCampaignMissionActionPrefix = "campaign_mission_";

} // namespace

CampaignMissionRef ResolveCampaignMission(const std::string& Action)
{
    CampaignMissionRef Ref;
    if (!IsCampaignMissionAction(Action))
        return Ref;

    bool Expansion = false;
    int Race = 0;
    int Mission = 0;
    std::string Spec = Action.substr(std::strlen(kCampaignMissionActionPrefix));
    size_t FirstSep = Spec.find('_');
    if (FirstSep == std::string::npos)
    {
        Mission = std::max(0, std::atoi(Spec.c_str()));
    }
    else
    {
        std::string CampaignSet = Spec.substr(0, FirstSep);
        Expansion = CampaignSet == "expansion";
        std::string Rest = Spec.substr(FirstSep + 1);
        size_t SecondSep = Rest.find('_');
        if (SecondSep == std::string::npos)
        {
            Mission = std::max(0, std::atoi(Rest.c_str()));
        }
        else
        {
            Race = std::clamp(std::atoi(Rest.substr(0, SecondSep).c_str()), 0, 2);
            Mission = std::max(0, std::atoi(Rest.substr(SecondSep + 1).c_str()));
        }
    }

    return ResolveCampaignMission(Expansion, Race, Mission);
}

CampaignMissionRef ResolveCampaignMission(bool Expansion, int Race, int Mission)
{
    static constexpr const char* OriginalFolders[3] = { "Terran", "Zerg", "Protoss" };
    static constexpr const char* ExpansionFolders[3] = { "EXPTerran", "EXPZerg", "EXPProtoss" };
    static constexpr const char* OriginalMissions[3][10] = {
        { "Terran01", "Terran02", "Terran03", "Terran04", "Terran05",
          "Terran06", "Terran07", "Terran08", "Terran09", "Terran10" },
        { "Zerg01", "Zerg02", "Zerg03", "Zerg04", "Zerg05",
          "Zerg06", "Zerg07", "Zerg08", "Zerg09", "Zerg10" },
        { "Protoss01", "Protoss02", "Protoss03", "Protoss04", "Protoss05",
          "Protoss06", "Protoss07", "Protoss08", "Protoss09", "Protoss10" }
    };
    static constexpr const char* ExpansionMissions[3][10] = {
        { "Terran01", "Terran02", "Terran03", "Terran04", "Terran05a",
          "Terran05b", "Terran06", "Terran07", "Terran08", "Terran08" },
        { "Zerg01", "Zerg02", "Zerg03", "Zerg04a", "Zerg05",
          "Zerg06", "Zerg07", "Zerg08", "Zerg09", "Zerg10" },
        { "Protoss01", "Protoss02", "Protoss03", "Protoss04", "Protoss05",
          "Protoss06", "Protoss07", "Protoss08", "Protoss08", "Protoss08" }
    };

    Race = std::clamp(Race, 0, 2);
    Mission = std::clamp(Mission, 0, 9);
    const char* Folder = Expansion ? ExpansionFolders[Race] : OriginalFolders[Race];
    const char* MissionFolder = Expansion ? ExpansionMissions[Race][Mission] : OriginalMissions[Race][Mission];
    CampaignMissionRef Ref;
    Ref.Valid = true;
    Ref.Expansion = Expansion;
    Ref.Race = Race;
    Ref.Mission = Mission;
    Ref.Path = std::string("assets/locales/zhCN/Assets/campaign/") + Folder + "/" +
        MissionFolder + "/staredit/scenario.chk";
    return Ref;
}

std::string BuildCampaignMissionAction(bool Expansion, int Race, int Mission)
{
    Race = std::clamp(Race, 0, 2);
    Mission = std::clamp(Mission, 0, 9);
    return std::string(kCampaignMissionActionPrefix) +
        (Expansion ? "expansion_" : "original_") +
        std::to_string(Race) + "_" + std::to_string(Mission);
}

bool IsCampaignMissionAction(const std::string& Action)
{
    return Action.rfind(kCampaignMissionActionPrefix, 0) == 0;
}

} // namespace Game::Campaign
