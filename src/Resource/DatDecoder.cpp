#include "Resource/DatDecoder.h"
#include "Core/Utils/Logger.h"

#include <cstring>
#include <algorithm>

namespace Resource
{

// Helper: read uint16_t from little-endian buffer
static inline uint16_t ReadU16(const uint8_t* Data, size_t Offset)
{
    return Data[Offset] | (Data[Offset + 1] << 8);
}

// Helper: read uint32_t from little-endian buffer
static inline uint32_t ReadU32(const uint8_t* Data, size_t Offset)
{
    return Data[Offset] | (Data[Offset + 1] << 8) |
           (Data[Offset + 2] << 16) | (Data[Offset + 3] << 24);
}

static inline int32_t ReadI32(const uint8_t* Data, size_t Offset)
{
    return (int32_t)ReadU32(Data, Offset);
}

std::vector<UnitEntry> DatDecoder::DecodeUnits(const std::vector<uint8_t>& Data)
{
    std::vector<UnitEntry> Entries;
    constexpr size_t Count = 228;
    if (Data.empty())
    {
        Core::Utils::Logger::Warning("DatDecoder", "units.dat is empty");
        return Entries;
    }

    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        if (I >= Data.size())
            break;

        UnitEntry E;
        E.Id = (uint32_t)I;

        // Brood War units.dat is column-major. The first column is the
        // flingy.dat graphics id, which starts the unit->flingy->sprite->image
        // chain used for both rendering and iscript selection.
        E.FlingyID = Data[I];
        E.SpriteID = E.FlingyID;
        const size_t Subunit1Offset = Count;
        const size_t Subunit2Offset = Count + Count * 2;
        if (Subunit1Offset + I * 2 + 2 <= Data.size())
            E.Subunit1ID = ReadU16(Data.data(), Subunit1Offset + I * 2);
        if (Subunit2Offset + I * 2 + 2 <= Data.size())
            E.Subunit2ID = ReadU16(Data.data(), Subunit2Offset + I * 2);

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " unit entries");
    return Entries;
}

std::vector<WeaponEntry> DatDecoder::DecodeWeapons(const std::vector<uint8_t>& Data)
{
    std::vector<WeaponEntry> Entries;
    const size_t EntrySize = 42;

    size_t Count = Data.size() / EntrySize;
    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        size_t Off = I * EntrySize;
        if (Off + EntrySize > Data.size())
            break;

        WeaponEntry E;
        E.Id = (uint32_t)I;

        // 0-1: DamageAmount
        E.DamageAmount = ReadU16(Data.data(), Off + 0);
        // 2-3: DamageBonus
        E.DamageBonus = ReadU16(Data.data(), Off + 2);
        // 4-5: Cooldown
        E.Cooldown = ReadU16(Data.data(), Off + 4);
        // 6-7: DamageFactor
        E.DamageFactor = ReadU16(Data.data(), Off + 6);
        // 8: WeaponBehavior
        E.WeaponBehavior = Data[Off + 8];
        // 14-15: Range (in pixels)
        E.Range = ReadU16(Data.data(), Off + 14);
        // 26-27: SplashType
        E.SplashType = ReadU16(Data.data(), Off + 26);

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " weapon entries");
    return Entries;
}

std::vector<FlingyEntry> DatDecoder::DecodeFlingy(const std::vector<uint8_t>& Data)
{
    std::vector<FlingyEntry> Entries;
    constexpr size_t Count = 209;
    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        const size_t Off = I * 2;
        if (Off + 2 > Data.size())
            break;

        FlingyEntry E;
        E.Id = (uint32_t)I;
        // First column is sprite.dat id.
        E.SpriteID = ReadU16(Data.data(), Off);

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " flingy entries");
    return Entries;
}

std::vector<SpriteEntry> DatDecoder::DecodeSprites(const std::vector<uint8_t>& Data)
{
    std::vector<SpriteEntry> Entries;
    constexpr size_t Count = 517;
    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        const size_t Off = I * 2;
        if (Off + 2 > Data.size())
            break;

        SpriteEntry E;
        E.Id = (uint32_t)I;
        // First column is image.dat id.
        E.ImageID = ReadU16(Data.data(), Off);

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " sprite entries");
    return Entries;
}

std::vector<ImageEntry> DatDecoder::DecodeImages(const std::vector<uint8_t>& Data)
{
    std::vector<ImageEntry> Entries;
    constexpr size_t Count = 999;
    constexpr size_t ExpectedSize = Count * 38;
    if (Data.size() < ExpectedSize)
    {
        Core::Utils::Logger::Warning("DatDecoder", "images.dat is too small");
        return Entries;
    }
    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        ImageEntry E;
        E.Id = (uint32_t)I;

        // Brood War images.dat is column-major, 999 entries:
        // grp index u32[999], six u8 columns, then seven u32 columns.
        E.GrpFilenameIndex = ReadU32(Data.data(), I * 4);
        E.HasDirectionalFrames = Data[Count * 4 + I] != 0;
        E.IsClickable = Data[Count * 5 + I] != 0;
        E.HasIScriptAnimations = Data[Count * 6 + I] != 0;
        E.AlwaysVisible = Data[Count * 7 + I] != 0;
        E.Modifier = Data[Count * 8 + I];
        E.ColorShift = Data[Count * 9 + I];
        E.IScriptID = ReadU32(Data.data(), Count * 10 + I * 4);

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " image entries");
    return Entries;
}

std::vector<ImageRelationEntry> DatDecoder::DecodeImageRelations(const std::vector<uint8_t>& Data)
{
    std::vector<ImageRelationEntry> Entries;
    constexpr size_t EntrySize = 8;
    if (Data.empty() || Data.size() < EntrySize)
    {
        Core::Utils::Logger::Warning("DatDecoder", "images.rel is empty or too small");
        return Entries;
    }

    if (Data.size() % EntrySize != 0)
    {
        Core::Utils::Logger::Warning("DatDecoder", "images.rel has trailing bytes");
    }

    const size_t Count = Data.size() / EntrySize;
    Entries.reserve(Count);
    for (size_t I = 0; I < Count; ++I)
    {
        const size_t Off = I * EntrySize;
        ImageRelationEntry E;
        E.ImageID = (uint32_t)I;
        E.Kind = ReadI32(Data.data(), Off + 0);
        E.ParentImageID = ReadI32(Data.data(), Off + 4);
        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " image relation entries");
    return Entries;
}

std::vector<ImageTblEntry> DatDecoder::DecodeImageTbl(const std::vector<uint8_t>& Data)
{
    std::vector<ImageTblEntry> Entries;
    if (Data.size() < 2) return Entries;

    uint16_t Count = ReadU16(Data.data(), 0);
    if (Data.size() < 2 + Count * 2) return Entries;

    Entries.reserve(Count);
    for (int I = 0; I < Count; I++)
    {
        ImageTblEntry E;
        E.Index = I + 1; // tbl indices are 1-based (0 = invalid)
        uint16_t StrOff = ReadU16(Data.data(), 2 + I * 2);
        if (StrOff < Data.size())
        {
            E.Filename = std::string(reinterpret_cast<const char*>(Data.data() + StrOff));
        }
        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " image tbl entries");
    return Entries;
}

std::vector<UpgradeEntry> DatDecoder::DecodeUpgrades(const std::vector<uint8_t>& Data)
{
    std::vector<UpgradeEntry> Entries;
    const size_t EntrySize = 18;

    size_t Count = Data.size() / EntrySize;
    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        size_t Off = I * EntrySize;
        if (Off + EntrySize > Data.size())
            break;

        UpgradeEntry E;
        E.Id = (uint32_t)I;
        E.MineralCostBase = ReadU16(Data.data(), Off + 0);
        E.MineralCostFactor = ReadU16(Data.data(), Off + 2);
        E.GasCostBase = ReadU16(Data.data(), Off + 4);
        E.GasCostFactor = ReadU16(Data.data(), Off + 6);
        E.TimeCostBase = ReadU16(Data.data(), Off + 8);
        E.TimeCostFactor = ReadU16(Data.data(), Off + 10);

        Entries.push_back(E);
    }

    return Entries;
}

std::vector<TechEntry> DatDecoder::DecodeTech(const std::vector<uint8_t>& Data)
{
    std::vector<TechEntry> Entries;
    const size_t EntrySize = 16;

    size_t Count = Data.size() / EntrySize;
    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        size_t Off = I * EntrySize;
        if (Off + EntrySize > Data.size())
            break;

        TechEntry E;
        E.Id = (uint32_t)I;
        E.MineralCost = ReadU16(Data.data(), Off + 0);
        E.GasCost = ReadU16(Data.data(), Off + 2);
        E.TimeCost = ReadU16(Data.data(), Off + 4);
        E.RequiredUnit = ReadU32(Data.data(), Off + 8);

        Entries.push_back(E);
    }

    return Entries;
}

} // namespace Resource
