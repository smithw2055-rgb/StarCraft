#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Game::Client::UI::Glue
{

struct BriefingDialogueLine
{
    int Slot = 0;                  // Portrait slot (0..3)
    int UnitID = -1;               // CHK unit ID (character portrait)
    std::string PortraitKey;       // Portrait folder key (e.g. "tadvisor", "ujim", "uduke")
    std::string Speaker;           // Localized/canonical speaker name
    std::string Text;              // Dialogue subtitle line
    std::string WavPath;           // CHK wav path (e.g. "staredit\\wav\\T1B00tad.wav")
    std::string ResolvedAudioFile; // Resolved disk path (.ogg / .wav)
    uint32_t DurationMs = 0;       // Audio / subtitle duration in ms
    bool IsTransmission = false;
};

struct CampaignBriefingInfo
{
    std::string Title;             // Scenario name
    std::string Description;       // Scenario description
    std::string Objectives;        // Mission objectives
    std::string Text;              // Formatted legacy text (Title/Desc/Objectives)
    std::string Status;            // Triggers/locations count summary
    std::string MapPath;           // Absolute or relative path to scenario.chk
    std::vector<BriefingDialogueLine> Lines; // Sequential briefing dialogues
};

const char* CampaignSetName(bool Expansion);
const char* CampaignRaceName(int Race);
std::string GetBriefingSpeakerName(int UnitID, int Race, bool Chinese);
std::string GetBriefingPortraitKey(int UnitID, int Race);
std::string GetBriefingWebmPath(const std::string& PortraitKey, bool Speaking);
CampaignBriefingInfo LoadCampaignBriefingInfo(bool Expansion, int Race, int Mission, bool Chinese = false);

} // namespace Game::Client::UI::Glue

