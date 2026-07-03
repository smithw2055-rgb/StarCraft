#include "Main/CampaignText.h"

#include <algorithm>

namespace Game::Client::GameLoop
{

std::string SanitizeCampaignText(std::string Text)
{
    for (char& Ch : Text)
    {
        if (Ch == '\r' || Ch == '\n' || Ch == '\t')
            Ch = ' ';
    }

    while (Text.find("  ") != std::string::npos)
        Text.erase(Text.find("  "), 1);
    return Text;
}

std::vector<std::string> WrapCampaignText(const std::string& Text, size_t MaxChars, size_t MaxLines)
{
    std::vector<std::string> Lines;
    std::string Clean = SanitizeCampaignText(Text);
    size_t Pos = 0;
    while (Pos < Clean.size() && Lines.size() < MaxLines)
    {
        size_t Remaining = Clean.size() - Pos;
        size_t Take = std::min(MaxChars, Remaining);
        size_t End = Pos + Take;
        if (End < Clean.size())
        {
            size_t Space = Clean.rfind(' ', End);
            if (Space != std::string::npos && Space > Pos + MaxChars / 2)
                End = Space;
        }

        std::string Line = Clean.substr(Pos, End - Pos);
        while (!Line.empty() && Line.front() == ' ')
            Line.erase(Line.begin());
        while (!Line.empty() && Line.back() == ' ')
            Line.pop_back();
        if (!Line.empty())
            Lines.push_back(Line);
        Pos = End;
        while (Pos < Clean.size() && Clean[Pos] == ' ')
            ++Pos;
    }

    if (Pos < Clean.size() && !Lines.empty())
    {
        std::string& Last = Lines.back();
        if (Last.size() + 3 > MaxChars && Last.size() > 3)
            Last.resize(MaxChars - 3);
        Last += "...";
    }
    return Lines;
}

const char* CampaignMessageKind(uint8_t ActionID)
{
    switch (ActionID)
    {
        case 7: return "TRANSMISSION";
        case 8: return "AUDIO";
        case 41: return "PORTRAIT";
        default: return "BRIEFING";
    }
}

} // namespace Game::Client::GameLoop
