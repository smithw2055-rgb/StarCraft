#include "UI/GlueCampaignData.h"

#include "Game/Campaign/CampaignMissionCatalog.h"
#include "Game/Trigger/SCRTriggerParser.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <vector>

namespace Game::Client::UI::Glue
{

namespace
{

std::vector<uint8_t> ReadBinaryFile(const std::string& Path)
{
    std::ifstream File(Path, std::ios::binary);
    if (!File.good())
        return {};
    File.seekg(0, std::ios::end);
    std::streamoff Size = File.tellg();
    if (Size <= 0)
        return {};
    File.seekg(0, std::ios::beg);
    std::vector<uint8_t> Data((size_t)Size);
    File.read(reinterpret_cast<char*>(Data.data()), Size);
    return File.good() ? Data : std::vector<uint8_t>{};
}

std::string FirstMissionObjective(const Game::Trigger::SCRTriggerData& Data)
{
    for (const auto& Trigger : Data.Triggers)
    {
        for (const auto& Action : Trigger.Actions)
        {
            if (Action.ActionType == 0)
                break;
            if (Action.ActionType == 12 && !Action.Disabled())
            {
                std::string Text = Data.GetString(Action.StringID);
                if (!Text.empty())
                    return Text;
            }
        }
    }
    return {};
}

} // namespace

const char* CampaignSetName(bool Expansion)
{
    return Expansion ? "Brood War" : "Original";
}

const char* CampaignRaceName(int Race)
{
    static constexpr const char* Names[3] = { "Terran", "Zerg", "Protoss" };
    return Names[std::max(0, std::min(Race, 2))];
}

CampaignBriefingInfo LoadCampaignBriefingInfo(bool Expansion, int Race, int Mission)
{
    CampaignBriefingInfo Info;
    const auto MissionRef = Game::Campaign::ResolveCampaignMission(Expansion, Race, Mission);
    const auto Bytes = ReadBinaryFile(MissionRef.Path);
    if (Bytes.empty())
        return Info;

    Game::Trigger::SCRTriggerData Data;
    Game::Trigger::SCRTriggerParser Parser;
    if (!Parser.Parse(Bytes, Data))
        return Info;

    Info.Title = Data.GetString(Data.ScenarioNameStringID);
    Info.Text = Data.GetString(Data.ScenarioDescriptionStringID);
    std::string Objective = FirstMissionObjective(Data);
    if (!Objective.empty())
    {
        if (!Info.Text.empty())
            Info.Text += "\n";
        Info.Text += Objective;
    }
    Info.Status = std::to_string(Data.Triggers.size()) + " triggers, " +
        std::to_string(Data.Locations.size()) + " locations";
    return Info;
}

} // namespace Game::Client::UI::Glue
