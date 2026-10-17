#include "UI/GlueCampaignData.h"

#include "Game/Campaign/CampaignMissionCatalog.h"
#include "Game/Trigger/SCRTriggerParser.h"
#include "Main/CampaignAudio.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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

std::string CleanDialogueString(std::string Text)
{
    while (!Text.empty() && (Text.front() == '\r' || Text.front() == '\n' || Text.front() == ' ' || Text.front() == '\t'))
        Text.erase(Text.begin());
    while (!Text.empty() && (Text.back() == '\r' || Text.back() == '\n' || Text.back() == ' ' || Text.back() == '\t'))
        Text.pop_back();
    return Text;
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

std::string GetBriefingSpeakerName(int UnitID, int Race, bool Chinese)
{
    switch (UnitID)
    {
        case 16: return Chinese ? "莎拉·凯瑞甘" : "Sarah Kerrigan";
        case 19:
        case 20: return Chinese ? "吉姆·雷诺" : "Jim Raynor";
        case 23: return Chinese ? "埃德蒙·杜克将军" : "General Edmund Duke";
        case 27:
        case 28:
        case 29: return Chinese ? "阿克图尔斯·蒙斯克" : "Arcturus Mengsk";
        case 34: return Chinese ? "战地医官" : "Field Medic";
        case 51: return Chinese ? "刀锋女王 凯瑞甘" : "Kerrigan, Queen of Blades";
        case 67: return Chinese ? "高阶圣堂武士" : "High Templar";
        case 75: return Chinese ? "泽拉图" : "Zeratul";
        case 77:
        case 78: return Chinese ? "菲尼克斯" : "Fenix";
        case 79: return Chinese ? "塔萨达" : "Executor Tassadar";
        case 87: return Chinese ? "阿达瑞斯" : "Judicator Aldaris";
        case 88: return Chinese ? "阿坦尼斯" : "Artanis";
        case 98: return Chinese ? "拉莎加尔" : "Matriarch Raszagal";
        case 99:
        case 104: return Chinese ? "萨米尔·杜兰" : "Samir Duran";
        case 100: return Chinese ? "阿列克谢·斯图科夫中将" : "Vice Admiral Alexei Stukov";
        case 102: return Chinese ? "杰拉德·杜加尔上将" : "Admiral Gerard DuGalle";
        case 106: return Chinese ? "战地副官" : "Terran Adjutant";
        case 148: return Chinese ? "主宰" : "The Overmind";
        case 154: return Chinese ? "副官" : "Protoss Advisor";
        default:
            if (Race == 0) return Chinese ? "副官" : "Terran Advisor";
            if (Race == 1) return Chinese ? "脑虫" : "Cerebrate";
            return Chinese ? "执行官" : "Protoss Advisor";
    }
}

std::string GetBriefingPortraitKey(int UnitID, int Race)
{
    switch (UnitID)
    {
        case 16: return "ukerr";
        case 19:
        case 20: return "ujim";
        case 23: return "uduke";
        case 27:
        case 28:
        case 29: return "umengsk";
        case 34: return "tmedic";
        case 51: return "uikerr";
        case 67: return "ptemplar";
        case 75: return "uzeratul";
        case 77: return "ufenzeal";
        case 78: return "ufendrag";
        case 79: return "utassadar";
        case 87: return "padvisor";
        case 88: return "partanis";
        case 98: return "uzeratul";
        case 99:
        case 104: return "tsam";
        case 100: return "talex";
        case 102: return "tdugal";
        case 106: return "tadvisor";
        case 148: return "zadvisor";
        case 154: return "padvisor";
        default:
            if (Race == 0) return "tadvisor";
            if (Race == 1) return "zadvisor";
            return "padvisor";
    }
}

std::string GetBriefingWebmPath(const std::string& PortraitKey, bool Speaking)
{
    if (PortraitKey.empty())
        return {};

    const std::filesystem::path Dir = std::filesystem::path("assets") / "portrait" / PortraitKey;
    if (!std::filesystem::exists(Dir))
        return {};

    const std::string Needle = Speaking ? "tlk" : "fid";
    std::string Fallback;
    for (const auto& Entry : std::filesystem::directory_iterator(Dir))
    {
        if (!Entry.is_regular_file())
            continue;
        const std::string Name = Entry.path().filename().string();
        if (Entry.path().extension() != ".webm")
            continue;
        if (Fallback.empty())
            Fallback = "portrait/" + PortraitKey + "/" + Name;

        std::string Lower = Name;
        std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char C) { return (char)std::tolower(C); });
        if (Lower.find(Needle) != std::string::npos)
            return "portrait/" + PortraitKey + "/" + Name;
    }
    return Fallback;
}

CampaignBriefingInfo LoadCampaignBriefingInfo(bool Expansion, int Race, int Mission, bool Chinese)
{
    CampaignBriefingInfo Info;
    const auto MissionRef = Game::Campaign::ResolveCampaignMission(Expansion, Race, Mission);
    Info.MapPath = MissionRef.Path;
    const auto Bytes = ReadBinaryFile(MissionRef.Path);
    if (Bytes.empty())
        return Info;

    Game::Trigger::SCRTriggerData Data;
    Game::Trigger::SCRTriggerParser Parser;
    if (!Parser.Parse(Bytes, Data))
        return Info;

    Info.Title = CleanDialogueString(Data.GetString(Data.ScenarioNameStringID));
    Info.Description = CleanDialogueString(Data.GetString(Data.ScenarioDescriptionStringID));

    int SlotUnits[4] = { -1, -1, -1, -1 };
    for (const auto& Trigger : Data.BriefingTriggers)
    {
        for (const auto& Action : Trigger.Actions)
        {
            if (Action.ActionType == 0)
                break;
            if (Action.Disabled())
                continue;

            switch (Action.ActionType)
            {
            case 4: // Mission Objectives
            {
                std::string Obj = CleanDialogueString(Data.GetString(Action.StringID));
                if (!Obj.empty())
                {
                    if (!Info.Objectives.empty())
                        Info.Objectives += "\n";
                    Info.Objectives += Obj;
                }
                break;
            }
            case 5: // Show Portrait
            {
                if (Action.PlayerID < 4)
                    SlotUnits[Action.PlayerID] = (int)Action.UnitID;
                break;
            }
            case 6: // Hide Portrait
            {
                if (Action.PlayerID < 4)
                    SlotUnits[Action.PlayerID] = -1;
                break;
            }
            case 8: // Transmission
            {
                BriefingDialogueLine Line;
                Line.Slot = Action.PlayerID < 4 ? (int)Action.PlayerID : 0;
                Line.UnitID = SlotUnits[Line.Slot];
                Line.PortraitKey = GetBriefingPortraitKey(Line.UnitID, Race);
                Line.Speaker = GetBriefingSpeakerName(Line.UnitID, Race, Chinese);
                Line.Text = CleanDialogueString(Data.GetString(Action.StringID));
                Line.WavPath = Data.GetWavPath(Action.WavID);
                Line.DurationMs = Action.Time;
                Line.IsTransmission = true;
                if (!Line.WavPath.empty())
                    Line.ResolvedAudioFile = Game::Client::GameLoop::ResolveCampaignSoundFile(MissionRef.Path, Line.WavPath);

                if (!Line.Text.empty())
                    Info.Lines.push_back(std::move(Line));
                break;
            }
            case 3: // Text Message
            {
                std::string Msg = CleanDialogueString(Data.GetString(Action.StringID));
                if (!Msg.empty())
                {
                    BriefingDialogueLine Line;
                    Line.Slot = Action.PlayerID < 4 ? (int)Action.PlayerID : 0;
                    Line.UnitID = SlotUnits[Line.Slot];
                    Line.PortraitKey = GetBriefingPortraitKey(Line.UnitID, Race);
                    Line.Speaker = GetBriefingSpeakerName(Line.UnitID, Race, Chinese);
                    Line.Text = Msg;
                    Line.DurationMs = Action.Time;
                    Line.IsTransmission = false;
                    Info.Lines.push_back(std::move(Line));
                }
                break;
            }
            default:
                break;
            }
        }
    }

    if (Info.Objectives.empty())
        Info.Objectives = CleanDialogueString(FirstMissionObjective(Data));

    Info.Text = Info.Description;
    if (!Info.Objectives.empty())
    {
        if (!Info.Text.empty())
            Info.Text += "\n\n";
        Info.Text += Info.Objectives;
    }

    Info.Status = std::to_string(Data.Triggers.size()) + " triggers, " +
        std::to_string(Data.BriefingTriggers.size()) + " briefing triggers, " +
        std::to_string(Info.Lines.size()) + " dialogue lines";

    return Info;
}

} // namespace Game::Client::UI::Glue

