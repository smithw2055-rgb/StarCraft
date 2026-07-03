#pragma once

#include <string>
#include <vector>

namespace Game::Client::UI
{

struct HudInfoPanelState
{
    bool HasSelection = false;
    bool IsBuilding = false;
    bool IsResource = false;
    bool UnderConstruction = false;
    int SelectedCount = 0;
    int IconIndex = -1;
    int PreviewDefID = -1;
    int PreviewChkUnitId = -1;
    int PlayerID = -1;
    int CurrentValue = 0;
    int MaxValue = 0;
    int ShieldValue = 0;
    int ShieldMax = 0;
    int EnergyValue = 0;
    int EnergyMax = 0;
    int Armor = 0;
    int SightValue = 0;
    int SpeedValue = 0;
    int GroundDamage = 0;
    int AirDamage = 0;
    int RangeValue = 0;
    int ResourceAmount = 0;
    int CarryingAmount = 0;
    int KillCount = 0;
    float Progress = -1.0f;
    std::string Name;
    std::string RoleLine;
    std::string TypeLine;
    std::string StatusLine;
    std::string StatLine;
    std::string CombatLine;
    std::string DetailLine;
    std::string CargoLine;
    std::string QueueLabel;
    std::vector<std::string> QueueEntries;
};

} // namespace Game::Client::UI
