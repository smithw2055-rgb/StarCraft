#pragma once

#include "UI/HudInfoPanelState.h"

#include <string>
#include <vector>

namespace Game::Client::UI
{

struct MainGuiSelectionPresentation
{
    std::string Name;
    int Count = 0;
    float Hp = 0.0f;
    float MaxHp = 0.0f;
    float Shields = 0.0f;
    float MaxShields = 0.0f;
    float Energy = 0.0f;
    float MaxEnergy = 0.0f;
    float Progress = -1.0f;
    std::string StatsText;
    std::string QueueLabel;
    std::vector<std::string> QueueEntries;
};

inline void AppendMainGuiInfoLine(std::string& Text, const std::string& Line)
{
    if (Line.empty())
        return;
    if (!Text.empty())
        Text += " | ";
    Text += Line;
}

inline MainGuiSelectionPresentation BuildMainGuiSelectionPresentation(
    const HudInfoPanelState& Info)
{
    MainGuiSelectionPresentation Presentation;
    Presentation.Name = Info.Name.empty() ? "Unit" : Info.Name;
    Presentation.Count = Info.SelectedCount;
    Presentation.Hp = static_cast<float>(Info.CurrentValue);
    Presentation.MaxHp = static_cast<float>(Info.MaxValue);
    Presentation.Shields = static_cast<float>(Info.ShieldValue);
    Presentation.MaxShields = static_cast<float>(Info.ShieldMax);
    Presentation.Energy = static_cast<float>(Info.EnergyValue);
    Presentation.MaxEnergy = static_cast<float>(Info.EnergyMax);
    Presentation.Progress = Info.Progress;
    Presentation.QueueLabel = Info.QueueLabel;
    Presentation.QueueEntries = Info.QueueEntries;

    if (!Presentation.QueueEntries.empty() && Presentation.Progress < 0.0f)
        Presentation.Progress = 0.0f;

    AppendMainGuiInfoLine(Presentation.StatsText, Info.StatusLine);
    AppendMainGuiInfoLine(Presentation.StatsText, Info.StatLine);
    AppendMainGuiInfoLine(Presentation.StatsText, Info.CombatLine);
    AppendMainGuiInfoLine(Presentation.StatsText, Info.CargoLine);
    AppendMainGuiInfoLine(Presentation.StatsText, Info.DetailLine);
    return Presentation;
}

} // namespace Game::Client::UI
