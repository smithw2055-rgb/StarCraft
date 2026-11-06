#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Game/DataEnums.h"
#include "Game/Races/RaceDef.h"

namespace Game::Races
{

namespace UnitSemanticFlags
{
    constexpr uint32_t None          = 0;
    constexpr uint32_t Worker        = 1u << 0;
    constexpr uint32_t TownHall      = 1u << 1;
    constexpr uint32_t Larva         = 1u << 2;
    constexpr uint32_t Overlord      = 1u << 3;
    constexpr uint32_t Transport     = 1u << 4;
    constexpr uint32_t Bunker        = 1u << 5;
    constexpr uint32_t MineralField  = 1u << 6;
    constexpr uint32_t VespeneGeyser = 1u << 7;
    constexpr uint32_t GasRefinery   = 1u << 8;
    constexpr uint32_t SiegeTank     = 1u << 9;
    constexpr uint32_t Flyer         = 1u << 10;
    constexpr uint32_t Detector      = 1u << 11;
    constexpr uint32_t Organic       = 1u << 12;
    constexpr uint32_t Mechanical    = 1u << 13;
}

// Lightweight DTO used to synchronize decoded binary DAT records (units.dat / weapons.dat)
// or Mod patches into the runtime UnitPrototypeRegistry without coupling gameplay systems
// to archive readers.
struct UnitDatSyncEntry
{
    int ChkUnitID = -1;
    int HitPoints = -1;
    int ShieldPoints = -1;
    int Armor = -1;
    int MineralCost = -1;
    int GasCost = -1;
    int BuildTimeFrames = -1;
    int SupplyCostRaw = -1;
    int SightRangeTiles = -1;
    int GroundDamage = -1;
    int GroundDamageBonus = -1;
    int AirDamage = -1;
    int AirDamageBonus = -1;
};

// Data-driven Unit Prototype Registry (inspired by Unity ScriptableObject / Godot Resource).
// Centralizes unit definitions, semantic classification flags, and runtime .dat / Mod overrides.
class UnitPrototypeRegistry
{
public:
    static constexpr int kMaxChkUnitIDs = static_cast<int>(Game::Data::UnitType::kCount);

    static UnitPrototypeRegistry& Instance()
    {
        static UnitPrototypeRegistry Registry;
        return Registry;
    }

    UnitPrototypeRegistry()
    {
        RebuildFromDefaults();
    }

    void RebuildFromDefaults()
    {
        mDefs.clear();
        mChkToDefIndex.fill(-1);
        mChkSemanticFlags.fill(0);
        mCustomChkDefs.clear();

        auto AppendRaceDefs = [this](const std::vector<UnitDef>& SourceDefs)
        {
            for (const auto& Def : SourceDefs)
            {
                const int DefIndex = static_cast<int>(mDefs.size());
                mDefs.push_back(Def);
                if (Def.ChkUnitID >= 0 && Def.ChkUnitID < kMaxChkUnitIDs &&
                    mChkToDefIndex[(size_t)Def.ChkUnitID] < 0)
                {
                    mChkToDefIndex[(size_t)Def.ChkUnitID] = DefIndex;
                }
            }
        };

        AppendRaceDefs(GetTerranDefs());
        AppendRaceDefs(GetZergDefs());
        AppendRaceDefs(GetProtossDefs());

        RebuildDefaultSemanticFlags();
    }

    int GetDefCount() const
    {
        return static_cast<int>(mDefs.size());
    }

    const UnitDef* FindByDefID(int DefID) const
    {
        if (DefID >= 0 && DefID < static_cast<int>(mDefs.size()))
            return &mDefs[(size_t)DefID];
        return nullptr;
    }

    const UnitDef* FindByChkUnitID(int ChkUnitID) const
    {
        auto CustomIt = mCustomChkDefs.find(ChkUnitID);
        if (CustomIt != mCustomChkDefs.end())
            return &CustomIt->second;

        if (ChkUnitID >= 0 && ChkUnitID < kMaxChkUnitIDs)
        {
            const int DefID = mChkToDefIndex[(size_t)ChkUnitID];
            if (DefID >= 0 && DefID < static_cast<int>(mDefs.size()))
                return &mDefs[(size_t)DefID];
        }
        return nullptr;
    }

    int FindDefIDByChkUnitID(int ChkUnitID) const
    {
        if (ChkUnitID >= 0 && ChkUnitID < kMaxChkUnitIDs)
            return mChkToDefIndex[(size_t)ChkUnitID];
        return -1;
    }

    int FindDefIDByName(const std::string& Name) const
    {
        for (size_t i = 0; i < mDefs.size(); ++i)
        {
            if (mDefs[i].Name == Name)
                return static_cast<int>(i);
        }
        return -1;
    }

    // Apply a runtime override (e.g. from Mod config or scenario unit settings)
    bool ApplyOverride(int ChkUnitID, const UnitDef& OverrideDef)
    {
        if (ChkUnitID < 0)
            return false;

        if (ChkUnitID < kMaxChkUnitIDs)
        {
            const int DefID = mChkToDefIndex[(size_t)ChkUnitID];
            if (DefID >= 0 && DefID < static_cast<int>(mDefs.size()))
            {
                mDefs[(size_t)DefID] = OverrideDef;
                RefreshSemanticFlagsForDef(mDefs[(size_t)DefID]);
                return true;
            }
        }

        mCustomChkDefs[ChkUnitID] = OverrideDef;
        RefreshSemanticFlagsForDef(OverrideDef);
        return true;
    }

    // Synchronize numeric attributes from decoded units.dat / weapons.dat entries
    int SyncFromDatEntries(const std::vector<UnitDatSyncEntry>& Entries)
    {
        int UpdatedCount = 0;
        for (const auto& Entry : Entries)
        {
            if (Entry.ChkUnitID < 0 || Entry.ChkUnitID >= kMaxChkUnitIDs)
                continue;
            const int DefID = mChkToDefIndex[(size_t)Entry.ChkUnitID];
            if (DefID < 0 || DefID >= static_cast<int>(mDefs.size()))
                continue;

            UnitDef& Def = mDefs[(size_t)DefID];
            if (Entry.HitPoints > 0) Def.HitPoints = Entry.HitPoints;
            if (Entry.ShieldPoints >= 0) Def.ShieldPoints = Entry.ShieldPoints;
            if (Entry.Armor >= 0) Def.Armor = Entry.Armor;
            if (Entry.MineralCost >= 0) Def.MineralCost = Entry.MineralCost;
            if (Entry.GasCost >= 0) Def.GasCost = Entry.GasCost;
            if (Entry.BuildTimeFrames > 0) Def.BuildTime = std::max(1, Entry.BuildTimeFrames / 24);
            if (Entry.SupplyCostRaw >= 0) Def.SupplyCost = (Entry.SupplyCostRaw + 1) / 2;
            if (Entry.SightRangeTiles > 0) Def.SightRange = Entry.SightRangeTiles;
            if (Entry.GroundDamage >= 0) Def.GroundDamage = Entry.GroundDamage;
            if (Entry.GroundDamageBonus >= 0) Def.GroundDamageBonus = Entry.GroundDamageBonus;
            if (Entry.AirDamage >= 0) Def.AirDamage = Entry.AirDamage;
            if (Entry.AirDamageBonus >= 0) Def.AirDamageBonus = Entry.AirDamageBonus;
            ++UpdatedCount;
        }
        return UpdatedCount;
    }

    uint32_t GetSemanticFlags(int ChkUnitID) const
    {
        if (ChkUnitID >= 0 && ChkUnitID < kMaxChkUnitIDs)
            return mChkSemanticFlags[(size_t)ChkUnitID];
        return UnitSemanticFlags::None;
    }

    void SetSemanticFlags(int ChkUnitID, uint32_t Flags)
    {
        if (ChkUnitID >= 0 && ChkUnitID < kMaxChkUnitIDs)
            mChkSemanticFlags[(size_t)ChkUnitID] = Flags;
    }

    void AddSemanticFlags(int ChkUnitID, uint32_t Flags)
    {
        if (ChkUnitID >= 0 && ChkUnitID < kMaxChkUnitIDs)
            mChkSemanticFlags[(size_t)ChkUnitID] |= Flags;
    }

    bool HasSemanticFlag(int ChkUnitID, uint32_t Flag) const
    {
        return (GetSemanticFlags(ChkUnitID) & Flag) != 0;
    }

    bool IsWorker(int ChkUnitID) const
    {
        return HasSemanticFlag(ChkUnitID, UnitSemanticFlags::Worker);
    }

    bool IsTownHall(int ChkUnitID) const
    {
        return HasSemanticFlag(ChkUnitID, UnitSemanticFlags::TownHall);
    }

    bool IsLarva(int ChkUnitID) const
    {
        return HasSemanticFlag(ChkUnitID, UnitSemanticFlags::Larva);
    }

    bool IsOverlord(int ChkUnitID) const
    {
        return HasSemanticFlag(ChkUnitID, UnitSemanticFlags::Overlord);
    }

    bool IsTransport(int ChkUnitID) const
    {
        return HasSemanticFlag(ChkUnitID, UnitSemanticFlags::Transport);
    }

    bool IsBunker(int ChkUnitID) const
    {
        return HasSemanticFlag(ChkUnitID, UnitSemanticFlags::Bunker);
    }

    bool IsMineralField(int ChkUnitID) const
    {
        return ChkUnitID == 100 || HasSemanticFlag(ChkUnitID, UnitSemanticFlags::MineralField);
    }

    bool IsVespeneGeyser(int ChkUnitID) const
    {
        return HasSemanticFlag(ChkUnitID, UnitSemanticFlags::VespeneGeyser);
    }

    bool IsGasRefinery(int ChkUnitID) const
    {
        return HasSemanticFlag(ChkUnitID, UnitSemanticFlags::GasRefinery);
    }

private:
    void RefreshSemanticFlagsForDef(const UnitDef& Def)
    {
        if (Def.ChkUnitID < 0 || Def.ChkUnitID >= kMaxChkUnitIDs)
            return;

        uint32_t Flags = mChkSemanticFlags[(size_t)Def.ChkUnitID];
        if (Def.Name == "SCV" || Def.Name == "Drone" || Def.Name == "Probe")
            Flags |= UnitSemanticFlags::Worker;
        if (Def.Name == "CommandCenter" || Def.Name == "Hatchery" ||
            Def.Name == "Lair" || Def.Name == "Hive" || Def.Name == "Nexus")
            Flags |= UnitSemanticFlags::TownHall;
        if (Def.Name == "Larva")
            Flags |= UnitSemanticFlags::Larva;
        if (Def.Name == "Overlord")
            Flags |= (UnitSemanticFlags::Overlord | UnitSemanticFlags::Transport);
        if (Def.Name == "Dropship" || Def.Name == "Shuttle")
            Flags |= UnitSemanticFlags::Transport;
        if (Def.Name == "Bunker")
            Flags |= UnitSemanticFlags::Bunker;
        if (Def.Name == "Refinery" || Def.Name == "Extractor" || Def.Name == "Assimilator")
            Flags |= UnitSemanticFlags::GasRefinery;
        if (Def.Name == "SiegeTank" || Def.Name == "EdmundDukeSiege")
            Flags |= UnitSemanticFlags::SiegeTank;

        mChkSemanticFlags[(size_t)Def.ChkUnitID] = Flags;
    }

    void RebuildDefaultSemanticFlags()
    {
        for (const auto& Def : mDefs)
            RefreshSemanticFlagsForDef(Def);

        // Canonical CHK unit IDs for resources and alternate unit modes
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Terran_SCV), UnitSemanticFlags::Worker);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Zerg_Drone), UnitSemanticFlags::Worker);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Protoss_Probe), UnitSemanticFlags::Worker);

        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Terran_Command_Center), UnitSemanticFlags::TownHall);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Zerg_Hatchery), UnitSemanticFlags::TownHall);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Zerg_Lair), UnitSemanticFlags::TownHall);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Zerg_Hive), UnitSemanticFlags::TownHall);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Protoss_Nexus), UnitSemanticFlags::TownHall);

        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Zerg_Larva), UnitSemanticFlags::Larva);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Zerg_Overlord),
                         UnitSemanticFlags::Overlord | UnitSemanticFlags::Transport);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Terran_Dropship), UnitSemanticFlags::Transport);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Protoss_Shuttle), UnitSemanticFlags::Transport);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Terran_Bunker), UnitSemanticFlags::Bunker);

        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Resource_Mineral_Field), UnitSemanticFlags::MineralField);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Resource_Mineral_Field_Type_2), UnitSemanticFlags::MineralField);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Resource_Mineral_Field_Type_3), UnitSemanticFlags::MineralField);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Resource_Vespene_Geyser), UnitSemanticFlags::VespeneGeyser);

        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Terran_Refinery), UnitSemanticFlags::GasRefinery);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Zerg_Extractor), UnitSemanticFlags::GasRefinery);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Protoss_Assimilator), UnitSemanticFlags::GasRefinery);

        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Terran_Siege_Tank_Tank_Mode), UnitSemanticFlags::SiegeTank);
        AddSemanticFlags(static_cast<int>(Game::Data::UnitType::Terran_Siege_Tank_Siege_Mode), UnitSemanticFlags::SiegeTank);

        for (int ChkID = 0; ChkID < kMaxChkUnitIDs; ++ChkID)
        {
            if (Game::Data::IsAirUnitType(ChkID))
                AddSemanticFlags(ChkID, UnitSemanticFlags::Flyer);
            if (IsDetectorChkUnitID(ChkID))
                AddSemanticFlags(ChkID, UnitSemanticFlags::Detector);
            if (IsOrganicChkUnitID(ChkID))
                AddSemanticFlags(ChkID, UnitSemanticFlags::Organic);
            if (IsMechanicalChkUnitID(ChkID))
                AddSemanticFlags(ChkID, UnitSemanticFlags::Mechanical);
        }
    }

    std::vector<UnitDef> mDefs;
    std::array<int, kMaxChkUnitIDs> mChkToDefIndex{};
    std::array<uint32_t, kMaxChkUnitIDs> mChkSemanticFlags{};
    std::unordered_map<int, UnitDef> mCustomChkDefs;
};

inline bool IsWorkerChkUnitID(int ChkUnitID)
{
    return UnitPrototypeRegistry::Instance().IsWorker(ChkUnitID);
}

inline bool IsTownHallChkUnitID(int ChkUnitID)
{
    return UnitPrototypeRegistry::Instance().IsTownHall(ChkUnitID);
}

inline bool IsLarvaChkUnitID(int ChkUnitID)
{
    return UnitPrototypeRegistry::Instance().IsLarva(ChkUnitID);
}

inline bool IsOverlordChkUnitID(int ChkUnitID)
{
    return UnitPrototypeRegistry::Instance().IsOverlord(ChkUnitID);
}

inline bool IsTransportChkUnitID(int ChkUnitID)
{
    return UnitPrototypeRegistry::Instance().IsTransport(ChkUnitID);
}

inline bool IsBunkerChkUnitID(int ChkUnitID)
{
    return UnitPrototypeRegistry::Instance().IsBunker(ChkUnitID);
}

} // namespace Game::Races
