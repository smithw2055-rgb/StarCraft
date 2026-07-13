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

static inline int16_t ReadI16(const uint8_t* Data, size_t Offset)
{
    return static_cast<int16_t>(ReadU16(Data, Offset));
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

static inline uint8_t ReadU8Column(const std::vector<uint8_t>& Data,
                                   size_t Offset,
                                   size_t Index)
{
    return (Offset + Index < Data.size()) ? Data[Offset + Index] : 0;
}

static inline uint16_t ReadU16Column(const std::vector<uint8_t>& Data,
                                     size_t Offset,
                                     size_t Index)
{
    const size_t Pos = Offset + Index * 2;
    return (Pos + 2 <= Data.size()) ? ReadU16(Data.data(), Pos) : 0;
}

static inline uint32_t ReadU32Column(const std::vector<uint8_t>& Data,
                                     size_t Offset,
                                     size_t Index)
{
    const size_t Pos = Offset + Index * 4;
    return (Pos + 4 <= Data.size()) ? ReadU32(Data.data(), Pos) : 0;
}

static inline int32_t ReadI32Column(const std::vector<uint8_t>& Data,
                                    size_t Offset,
                                    size_t Index)
{
    return static_cast<int32_t>(ReadU32Column(Data, Offset, Index));
}

std::vector<UnitEntry> DatDecoder::DecodeUnits(const std::vector<uint8_t>& Data)
{
    std::vector<UnitEntry> Entries;
    constexpr size_t Count = 228;
    constexpr size_t UnitsCount = 106;
    constexpr size_t BuildingsCount = 96;

    size_t Offset = 0;
    const size_t FlingyOffset = Offset; Offset += Count;
    const size_t Subunit1Offset = Offset; Offset += Count * 2;
    const size_t Subunit2Offset = Offset; Offset += Count * 2;
    Offset += BuildingsCount * 2; // infestation unit, indexed from UnitsCount
    const size_t ConstructionImageOffset = Offset; Offset += Count * 4;
    Offset += Count; // unit direction
    const size_t HasShieldOffset = Offset; Offset += Count;
    const size_t ShieldPointsOffset = Offset; Offset += Count * 2;
    const size_t HitPointsOffset = Offset; Offset += Count * 4;
    Offset += Count; // elevation level
    Offset += Count; // unknown1
    Offset += Count; // sublabel
    Offset += Count; // computer idle order
    Offset += Count; // human idle order
    Offset += Count; // return to idle order
    Offset += Count; // attack unit order
    Offset += Count; // attack move order
    const size_t GroundWeaponOffset = Offset; Offset += Count;
    Offset += Count; // max ground hits
    const size_t AirWeaponOffset = Offset; Offset += Count;
    Offset += Count; // max air hits
    Offset += Count; // AI internal
    Offset += Count * 4; // flags
    Offset += Count; // target acquisition range
    const size_t SightRangeOffset = Offset; Offset += Count;
    const size_t ArmorUpgradeOffset = Offset; Offset += Count;
    const size_t UnitSizeOffset = Offset; Offset += Count;
    const size_t ArmorOffset = Offset; Offset += Count;
    Offset += Count; // right click action
    Offset += UnitsCount * 2; // ready sound
    Offset += Count * 2; // first what sound
    Offset += Count * 2; // last what sound
    Offset += UnitsCount * 2; // first pissed sound
    Offset += UnitsCount * 2; // last pissed sound
    Offset += UnitsCount * 2; // first yes sound
    Offset += UnitsCount * 2; // last yes sound
    Offset += Count * 4; // placement size
    Offset += BuildingsCount * 4; // addon position, indexed from UnitsCount
    Offset += Count * 8; // dimensions
    // These offsets are validated against real Brood War assets below and in
    // SpriteImageDataTest. Keep them explicit because several preceding sound
    // table columns have version-specific lengths in extracted assets.
    constexpr size_t PlacementOffset = 0x2C14;
    constexpr size_t AddonPositionOffset = 0x2FA4;
    constexpr size_t DimensionOffset = 0x3124;
    Offset = DimensionOffset + Count * 8;
    const size_t PortraitOffset = Offset; Offset += Count * 2;
    const size_t MineralCostOffset = Offset; Offset += Count * 2;
    const size_t GasCostOffset = Offset; Offset += Count * 2;
    const size_t BuildTimeOffset = Offset; Offset += Count * 2;
    Offset += Count * 2; // unknown2
    Offset += Count; // group flags
    Offset += Count; // supply provided
    const size_t SupplyRequiredOffset = Offset; Offset += Count;
    if (Data.empty())
    {
        Core::Utils::Logger::Warning("DatDecoder", "units.dat is empty");
        return Entries;
    }

    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        UnitEntry E;
        E.Id = (uint32_t)I;

        // Brood War units.dat is column-major. The first column is the
        // flingy.dat graphics id, which starts the unit->flingy->sprite->image
        // chain used for both rendering and iscript selection.
        E.FlingyID = ReadU8Column(Data, FlingyOffset, I);
        E.SpriteID = E.FlingyID;
        E.Subunit1ID = ReadU16Column(Data, Subunit1Offset, I);
        E.Subunit2ID = ReadU16Column(Data, Subunit2Offset, I);
        E.ConstructionImageID = ReadU32Column(Data, ConstructionImageOffset, I);
        E.ShieldAmount = ReadU8Column(Data, HasShieldOffset, I) != 0
            ? ReadU16Column(Data, ShieldPointsOffset, I)
            : 0;
        E.HitPoints = (uint32_t)std::max(0, ReadI32Column(Data, HitPointsOffset, I) / 256);
        E.GroundWeapon = ReadU8Column(Data, GroundWeaponOffset, I);
        E.AirWeapon = ReadU8Column(Data, AirWeaponOffset, I);
        E.SightRange = ReadU8Column(Data, SightRangeOffset, I);
        E.ArmorUpgrade = ReadU8Column(Data, ArmorUpgradeOffset, I);
        E.UnitSize = ReadU8Column(Data, UnitSizeOffset, I);
        E.Armor = ReadU8Column(Data, ArmorOffset, I);
        E.PortraitID = ReadU16Column(Data, PortraitOffset, I);
        E.MineralCost = ReadU16Column(Data, MineralCostOffset, I);
        E.GasCost = ReadU16Column(Data, GasCostOffset, I);
        E.BuildTime = ReadU16Column(Data, BuildTimeOffset, I);
        E.SupplyCost = ReadU8Column(Data, SupplyRequiredOffset, I);
        if (PlacementOffset + I * 4 + 4 <= Data.size())
        {
            E.PlacementWidth = ReadI16(Data.data(), PlacementOffset + I * 4 + 0);
            E.PlacementHeight = ReadI16(Data.data(), PlacementOffset + I * 4 + 2);
        }
        if (I >= UnitsCount && I < UnitsCount + BuildingsCount)
        {
            const size_t BuildingIndex = I - UnitsCount;
            if (AddonPositionOffset + BuildingIndex * 4 + 4 <= Data.size())
            {
                E.AddonPositionX =
                    ReadI16(Data.data(), AddonPositionOffset + BuildingIndex * 4 + 0);
                E.AddonPositionY =
                    ReadI16(Data.data(), AddonPositionOffset + BuildingIndex * 4 + 2);
            }
        }
        if (DimensionOffset + I * 8 + 8 <= Data.size())
        {
            E.DimensionLeft = ReadI16(Data.data(), DimensionOffset + I * 8 + 0);
            E.DimensionUp = ReadI16(Data.data(), DimensionOffset + I * 8 + 2);
            E.DimensionRight = ReadI16(Data.data(), DimensionOffset + I * 8 + 4);
            E.DimensionDown = ReadI16(Data.data(), DimensionOffset + I * 8 + 6);
        }

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " unit entries");
    return Entries;
}

std::vector<WeaponEntry> DatDecoder::DecodeWeapons(const std::vector<uint8_t>& Data)
{
    std::vector<WeaponEntry> Entries;
    constexpr size_t Count = 130;
    if (Data.empty())
        return Entries;

    Entries.reserve(Count);

    size_t Offset = 0;
    const size_t LabelOffset = Offset; Offset += Count * 2;
    const size_t FlingyOffset = Offset; Offset += Count * 4;
    Offset += Count; // unused
    const size_t TargetFlagsOffset = Offset; Offset += Count * 2;
    const size_t MinRangeOffset = Offset; Offset += Count * 4;
    const size_t MaxRangeOffset = Offset; Offset += Count * 4;
    const size_t DamageUpgradeOffset = Offset; Offset += Count;
    const size_t DamageTypeOffset = Offset; Offset += Count;
    const size_t BulletTypeOffset = Offset; Offset += Count;
    Offset += Count; // lifetime
    const size_t HitTypeOffset = Offset; Offset += Count;
    const size_t InnerSplashOffset = Offset; Offset += Count * 2;
    const size_t MediumSplashOffset = Offset; Offset += Count * 2;
    const size_t OuterSplashOffset = Offset; Offset += Count * 2;
    const size_t DamageAmountOffset = Offset; Offset += Count * 2;
    const size_t DamageBonusOffset = Offset; Offset += Count * 2;
    const size_t CooldownOffset = Offset; Offset += Count;
    const size_t BulletCountOffset = Offset; Offset += Count;
    Offset += Count; // attack angle
    Offset += Count; // bullet heading offset
    Offset += Count; // forward offset
    Offset += Count; // upward offset
    Offset += Count * 2; // target error message
    const size_t IconOffset = Offset; Offset += Count * 2;

    for (size_t I = 0; I < Count; ++I)
    {
        WeaponEntry E;
        E.Id = (uint32_t)I;
        E.Label = ReadU16Column(Data, LabelOffset, I);
        E.FlingyID = ReadU32Column(Data, FlingyOffset, I);
        E.TargetFlags = ReadU16Column(Data, TargetFlagsOffset, I);
        E.MinRange = ReadU32Column(Data, MinRangeOffset, I);
        E.Range = (uint16_t)ReadU32Column(Data, MaxRangeOffset, I);
        E.DamageUpgrade = ReadU8Column(Data, DamageUpgradeOffset, I);
        E.DamageType = ReadU8Column(Data, DamageTypeOffset, I);
        E.WeaponBehavior = ReadU8Column(Data, BulletTypeOffset, I);
        E.SplashType = ReadU8Column(Data, HitTypeOffset, I);
        E.InnerSplashRadius = ReadU16Column(Data, InnerSplashOffset, I);
        E.MediumSplashRadius = ReadU16Column(Data, MediumSplashOffset, I);
        E.OuterSplashRadius = ReadU16Column(Data, OuterSplashOffset, I);
        E.DamageAmount = ReadU16Column(Data, DamageAmountOffset, I);
        E.DamageBonus = ReadU16Column(Data, DamageBonusOffset, I);
        E.Cooldown = ReadU8Column(Data, CooldownOffset, I);
        E.BulletCount = ReadU8Column(Data, BulletCountOffset, I);
        E.Icon = ReadU16Column(Data, IconOffset, I);

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
    constexpr size_t SpriteOffset = 0;
    constexpr size_t TopSpeedOffset = SpriteOffset + Count * 2;
    constexpr size_t AccelerationOffset = TopSpeedOffset + Count * 4;
    constexpr size_t HaltDistanceOffset = AccelerationOffset + Count * 2;
    constexpr size_t TurnRateOffset = HaltDistanceOffset + Count * 4;
    constexpr size_t UnusedOffset = TurnRateOffset + Count;
    constexpr size_t MovementTypeOffset = UnusedOffset + Count;
    constexpr size_t RequiredSize = MovementTypeOffset + Count;

    if (Data.size() < RequiredSize)
        return Entries;

    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        FlingyEntry E;
        E.Id = (uint32_t)I;
        E.SpriteID = ReadU16(Data.data(), SpriteOffset + I * 2);
        E.TopSpeed = (int32_t)ReadU32(Data.data(), TopSpeedOffset + I * 4);
        E.Speed = (uint32_t)E.TopSpeed;
        E.Acceleration = (int16_t)ReadU16(Data.data(), AccelerationOffset + I * 2);
        E.HaltDistance = (int32_t)ReadU32(Data.data(), HaltDistanceOffset + I * 4);
        E.TurnRate = Data[TurnRateOffset + I];
        E.Unused = Data[UnusedOffset + I];
        E.MovementType = Data[MovementTypeOffset + I];

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
    constexpr size_t SelectableCount = 387;
    constexpr size_t NonSelectableCount = 130;
    constexpr size_t ImageOffset = 0;
    constexpr size_t HealthBarOffset = ImageOffset + Count * 2;
    constexpr size_t UnknownOffset = HealthBarOffset + SelectableCount;
    constexpr size_t VisibleOffset = UnknownOffset + Count;
    constexpr size_t SelectionCircleOffset = VisibleOffset + Count;
    constexpr size_t SelectionCircleVerticalOffset = SelectionCircleOffset + SelectableCount;
    Entries.reserve(Count);

    for (size_t I = 0; I < Count; ++I)
    {
        const size_t Off = ImageOffset + I * 2;
        if (Off + 2 > Data.size())
            break;

        SpriteEntry E;
        E.Id = (uint32_t)I;
        // First column is image.dat id.
        E.ImageID = ReadU16(Data.data(), Off);
        if (VisibleOffset + I < Data.size())
            E.VisibilitySize = Data[VisibleOffset + I];

        if (I >= NonSelectableCount)
        {
            const size_t SelectableIndex = I - NonSelectableCount;
            if (SelectableIndex < SelectableCount)
            {
                if (HealthBarOffset + SelectableIndex < Data.size())
                    E.HealthBar = Data[HealthBarOffset + SelectableIndex];
                if (SelectionCircleOffset + SelectableIndex < Data.size())
                    E.SelectionCircle = Data[SelectionCircleOffset + SelectableIndex];
                if (SelectionCircleVerticalOffset + SelectableIndex < Data.size())
                    E.SelectionCircleVerticalOffset = Data[SelectionCircleVerticalOffset + SelectableIndex];
            }
        }

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
        E.ShieldFilenameIndex = ReadU32(Data.data(), Count * 14 + I * 4);
        E.AttackFilenameIndex = ReadU32(Data.data(), Count * 18 + I * 4);
        E.DamageFilenameIndex = ReadU32(Data.data(), Count * 22 + I * 4);
        E.SpecialFilenameIndex = ReadU32(Data.data(), Count * 26 + I * 4);
        E.LandingDustFilenameIndex = ReadU32(Data.data(), Count * 30 + I * 4);
        E.LiftOffFilenameIndex = ReadU32(Data.data(), Count * 34 + I * 4);

        Entries.push_back(E);
    }

    Core::Utils::Logger::Info("DatDecoder",
        "Decoded " + std::to_string(Entries.size()) + " image entries");
    return Entries;
}

std::optional<ImageOffsetTable> DatDecoder::DecodeImageOffsets(const std::vector<uint8_t>& Data)
{
    if (Data.size() < 8)
        return std::nullopt;

    const size_t FrameCount = ReadU32(Data.data(), 0);
    const size_t OffsetCount = ReadU32(Data.data(), 4);
    if (FrameCount > (Data.size() - 8) / 4)
        return std::nullopt;

    ImageOffsetTable Table;
    Table.Frames.reserve(FrameCount);
    for (size_t Frame = 0; Frame < FrameCount; ++Frame)
    {
        const size_t FileOffset = ReadU32(Data.data(), 8 + Frame * 4);
        if (FileOffset > Data.size() || OffsetCount > (Data.size() - FileOffset) / 2)
            return std::nullopt;

        auto& Offsets = Table.Frames.emplace_back();
        Offsets.reserve(OffsetCount);
        for (size_t Index = 0; Index < OffsetCount; ++Index)
        {
            const size_t Pos = FileOffset + Index * 2;
            Offsets.push_back({(int)(int8_t)Data[Pos], (int)(int8_t)Data[Pos + 1]});
        }
    }
    return Table;
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
    constexpr size_t Count = 61;
    if (Data.empty())
        return Entries;

    Entries.reserve(Count);

    size_t Offset = 0;
    const size_t MineralBaseOffset = Offset; Offset += Count * 2;
    const size_t MineralFactorOffset = Offset; Offset += Count * 2;
    const size_t GasBaseOffset = Offset; Offset += Count * 2;
    const size_t GasFactorOffset = Offset; Offset += Count * 2;
    const size_t TimeBaseOffset = Offset; Offset += Count * 2;
    const size_t TimeFactorOffset = Offset; Offset += Count * 2;

    for (size_t I = 0; I < Count; ++I)
    {
        UpgradeEntry E;
        E.Id = (uint32_t)I;
        E.MineralCostBase = ReadU16Column(Data, MineralBaseOffset, I);
        E.MineralCostFactor = ReadU16Column(Data, MineralFactorOffset, I);
        E.GasCostBase = ReadU16Column(Data, GasBaseOffset, I);
        E.GasCostFactor = ReadU16Column(Data, GasFactorOffset, I);
        E.TimeCostBase = ReadU16Column(Data, TimeBaseOffset, I);
        E.TimeCostFactor = ReadU16Column(Data, TimeFactorOffset, I);

        Entries.push_back(E);
    }

    return Entries;
}

std::vector<TechEntry> DatDecoder::DecodeTech(const std::vector<uint8_t>& Data)
{
    std::vector<TechEntry> Entries;
    constexpr size_t Count = 44;
    if (Data.empty())
        return Entries;

    Entries.reserve(Count);

    size_t Offset = 0;
    const size_t MineralOffset = Offset; Offset += Count * 2;
    const size_t GasOffset = Offset; Offset += Count * 2;
    const size_t TimeOffset = Offset; Offset += Count * 2;
    Offset += Count * 2; // energy cost
    const size_t RequiredUnitOffset = Offset; Offset += Count * 4;

    for (size_t I = 0; I < Count; ++I)
    {
        TechEntry E;
        E.Id = (uint32_t)I;
        E.MineralCost = ReadU16Column(Data, MineralOffset, I);
        E.GasCost = ReadU16Column(Data, GasOffset, I);
        E.TimeCost = ReadU16Column(Data, TimeOffset, I);
        E.RequiredUnit = ReadU32Column(Data, RequiredUnitOffset, I);

        Entries.push_back(E);
    }

    return Entries;
}

} // namespace Resource
