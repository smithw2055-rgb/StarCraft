#pragma once

#include <string>
#include <vector>

namespace Game::Client::UI
{

struct HudQueueSlotHit
{
    int X = 0;
    int Y = 0;
    int W = 0;
    int H = 0;
    int QueueIndex = -1;
};

struct HudMultiSelectSlot
{
    int EntityId = -1;
    int ChkUnitId = -1;
    int CurrentHp = 0;
    int MaxHp = 1;
    int Shield = 0;
    int ShieldMax = 0;
    int Energy = 0;
    int EnergyMax = 0;
    int WireframeIndex = -1;
    std::string Name;
};

struct HudMultiSelectSlotHit
{
    int X = 0;
    int Y = 0;
    int W = 0;
    int H = 0;
    int SlotIndex = -1;
    int EntityId = -1;
};

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
    std::vector<int> QueueUnitDefIDs;
    std::vector<HudQueueSlotHit> QueueSlotHits;
    std::vector<HudMultiSelectSlot> MultiSelectSlots;
    std::vector<HudMultiSelectSlotHit> MultiSelectSlotHits;
    bool IsTownCenter = false;
    bool IsProductionBuilding = false;
    bool IsTechBuilding = false;
    int ActiveResearchID = -1;
    struct TechUpgradeSlot {
        int ResearchId = 0;
        int Level = 0;
        int MaxLevel = 1;
        std::string Name;
    };
    std::vector<TechUpgradeSlot> TechUpgrades;
    int SupplyUsed = 0;
    int SupplyProvided = 0;
    int SupplyTotal = 0;
    int SupplyMax = 0;

    void Reset()
    {
        HasSelection = false;
        IsBuilding = false;
        IsResource = false;
        UnderConstruction = false;
        SelectedCount = 0;
        IconIndex = -1;
        PreviewDefID = -1;
        PreviewChkUnitId = -1;
        PlayerID = -1;
        CurrentValue = 0;
        MaxValue = 0;
        ShieldValue = 0;
        ShieldMax = 0;
        EnergyValue = 0;
        EnergyMax = 0;
        Armor = 0;
        SightValue = 0;
        SpeedValue = 0;
        GroundDamage = 0;
        AirDamage = 0;
        RangeValue = 0;
        ResourceAmount = 0;
        CarryingAmount = 0;
        KillCount = 0;
        Progress = -1.0f;
        Name.clear();
        RoleLine.clear();
        TypeLine.clear();
        StatusLine.clear();
        StatLine.clear();
        CombatLine.clear();
        DetailLine.clear();
        CargoLine.clear();
        QueueLabel.clear();
        QueueEntries.clear();
        QueueUnitDefIDs.clear();
        QueueSlotHits.clear();
        MultiSelectSlots.clear();
        MultiSelectSlotHits.clear();
        IsTownCenter = false;
        IsProductionBuilding = false;
        IsTechBuilding = false;
        ActiveResearchID = -1;
        TechUpgrades.clear();
        SupplyUsed = 0;
        SupplyProvided = 0;
        SupplyTotal = 0;
        SupplyMax = 0;
    }
};

} // namespace Game::Client::UI
