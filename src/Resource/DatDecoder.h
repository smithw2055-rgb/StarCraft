#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <unordered_map>

namespace Resource
{

// Forwad decls
class GameDataLoader;

namespace UnitFlags
{
    constexpr uint32_t Building = 0x00000001;
    constexpr uint32_t Addon = 0x00000002;
    constexpr uint32_t Flyer = 0x00000004;
    constexpr uint32_t Worker = 0x00000008;
    constexpr uint32_t Subunit = 0x00000010;
    constexpr uint32_t FlyingBuilding = 0x00000020;
    constexpr uint32_t Hero = 0x00000040;
    constexpr uint32_t RegensHP = 0x00000080;
    constexpr uint32_t AnimatedResource = 0x00000100;
    constexpr uint32_t Cloaking = 0x00000200;
    constexpr uint32_t TwoUnitsInOneEgg = 0x00000400;
    constexpr uint32_t SingleEntity = 0x00000800;
    constexpr uint32_t ResourceDepot = 0x00001000;
    constexpr uint32_t ResourceContainer = 0x00002000;
    constexpr uint32_t RoboticUnit = 0x00004000;
    constexpr uint32_t Detector = 0x00008000;
    constexpr uint32_t Organic = 0x00010000;
    constexpr uint32_t CreepBuilding = 0x00020000;
    constexpr uint32_t Unused = 0x00040000;
    constexpr uint32_t RequiresPsi = 0x00080000;
    constexpr uint32_t Burrowable = 0x00100000;
    constexpr uint32_t Spellcaster = 0x00200000;
    constexpr uint32_t PermanentCloak = 0x00400000;
    constexpr uint32_t PickupItem = 0x00800000;
    constexpr uint32_t IgnoreOverlay = 0x01000000;
    constexpr uint32_t MediumOverlay = 0x02000000;
    constexpr uint32_t LargeOverlay = 0x04000000;
    constexpr uint32_t AutoAttackAndMove = 0x08000000;
    constexpr uint32_t CanAttack = 0x10000000;
    constexpr uint32_t Invincible = 0x20000000;
    constexpr uint32_t Mechanical = 0x40000000;
    constexpr uint32_t ProducesUnits = 0x80000000;
}

struct UnitEntry
{
    uint32_t Id = 0;
    std::string Name;
    uint32_t Flags = 0;
    uint16_t SpriteID = 0;
    uint16_t PortraitID = 0;
    uint32_t HitPoints = 100;
    uint16_t ShieldAmount = 0;
    uint16_t Armor = 0;
    uint8_t ArmorUpgrade = 0;
    uint8_t SightRange = 0;
    uint16_t BuildTime = 60;
    uint32_t MineralCost = 50;
    uint32_t GasCost = 0;
    uint16_t SupplyCost = 1;
    uint16_t FlingyID = 0;
    uint16_t Subunit1ID = 228;
    uint16_t Subunit2ID = 228;
    uint32_t ConstructionImageID = UINT32_MAX;
    int16_t PlacementWidth = 0;
    int16_t PlacementHeight = 0;
    int16_t AddonPositionX = 0;
    int16_t AddonPositionY = 0;
    int16_t DimensionLeft = 0;
    int16_t DimensionUp = 0;
    int16_t DimensionRight = 0;
    int16_t DimensionDown = 0;
    uint16_t WeaponID = 0;
    uint16_t WeaponID2 = 0;
    uint32_t RequiredTech = 0;
    uint32_t RequiredUnits = 0;  // bitmask
    uint16_t UnitSize = 0;
    uint16_t GroundWeapon = 0;
    uint8_t MaxGroundHits = 0;
    uint16_t AirWeapon = 0;
    uint8_t MaxAirHits = 0;
    uint8_t Plagueable = 1;
    uint8_t Hallucinable = 1;
    uint8_t IsBuilding = 0;

    bool HasBuildingFlag() const { return (Flags & UnitFlags::Building) != 0 || IsBuilding != 0; }
    bool IsAddon() const { return (Flags & UnitFlags::Addon) != 0; }
    bool IsFlyer() const { return (Flags & UnitFlags::Flyer) != 0; }
    bool IsWorker() const { return (Flags & UnitFlags::Worker) != 0; }
    bool IsSubunit() const { return (Flags & UnitFlags::Subunit) != 0; }
    bool IsFlyingBuilding() const { return (Flags & UnitFlags::FlyingBuilding) != 0; }
    bool IsHero() const { return (Flags & UnitFlags::Hero) != 0; }
    bool IsRegenerating() const { return (Flags & UnitFlags::RegensHP) != 0; }
    bool IsResourceDepot() const { return (Flags & UnitFlags::ResourceDepot) != 0; }
    bool IsRobotic() const { return (Flags & UnitFlags::RoboticUnit) != 0; }
    bool IsDetector() const { return (Flags & UnitFlags::Detector) != 0; }
    bool IsOrganic() const { return (Flags & UnitFlags::Organic) != 0; }
    bool IsRequiresCreep() const { return (Flags & UnitFlags::CreepBuilding) != 0; }
    bool IsRequiresPsi() const { return (Flags & UnitFlags::RequiresPsi) != 0; }
    bool CanBurrow() const { return (Flags & UnitFlags::Burrowable) != 0; }
    bool IsSpellcaster() const { return (Flags & UnitFlags::Spellcaster) != 0; }
    bool IsMechanical() const { return (Flags & UnitFlags::Mechanical) != 0; }

    int DimensionWidth() const { return (int)DimensionLeft + (int)DimensionRight + 1; }
    int DimensionHeight() const { return (int)DimensionUp + (int)DimensionDown + 1; }
};

struct OrderEntry
{
    uint32_t Id = 0;
    uint16_t Label = 0;
    uint8_t TargetsEnemies = 0;
    uint8_t Background = 0;
    uint8_t ValidForTurret = 0;
    uint8_t CanBeInterrupted = 0;
    uint8_t CanBeQueued = 0;
    uint8_t CanBeObstructed = 0;
    uint8_t Weapon = 0;
    uint8_t TechType = 0;
    uint8_t Animation = 0;
    int16_t Highlight = 0;
    uint16_t DepIndex = 0;
    uint8_t TargetOrder = 0;
};

struct WeaponEntry
{
    uint32_t Id = 0;
    std::string Name;
    uint16_t Label = 0;
    uint32_t FlingyID = 0;
    uint16_t TargetFlags = 0;
    uint32_t MinRange = 0;
    uint16_t DamageAmount = 0;
    uint16_t DamageBonus = 0;
    uint16_t DamageFactor = 0;
    uint16_t Cooldown = 30;
    uint16_t Range = 32;
    uint8_t DamageUpgrade = 0;
    uint8_t DamageType = 0;
    uint16_t SplashType = 0;
    uint8_t Lifetime = 0;
    uint16_t InnerSplashRadius = 0;
    uint16_t MediumSplashRadius = 0;
    uint16_t OuterSplashRadius = 0;
    uint16_t WeaponBehavior = 0;
    uint8_t BulletCount = 1;
    uint8_t AttackAngle = 0;
    int8_t BulletHeadingOffset = 0;
    int8_t ForwardOffset = 0;
    int8_t UpwardOffset = 0;
    uint16_t Icon = 0;
};

struct FlingyEntry
{
    uint32_t Id = 0;
    uint32_t Speed = 0;
    int16_t Acceleration = 0;
    uint8_t TurnRate = 0;
    uint32_t SpriteID = 0;
    int32_t TopSpeed = 0;
    int32_t HaltDistance = 0;
    uint8_t Unused = 0;
    uint8_t MovementType = 0;
};

struct SpriteEntry
{
    uint32_t Id = 0;
    uint32_t ImageID = 0;
    uint32_t HealthBar = 0;
    uint32_t VisibilitySize = 0;
    uint32_t SelectionCircle = 0;
    uint32_t SelectionCircleVerticalOffset = 0;
};

struct ImageEntry
{
    uint32_t Id = 0;
    std::string GrpFile;       // resolved GRP file path
    uint32_t GrpID = 0;       // index into grp list
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Frames = 0;
    uint32_t GrpFilenameIndex = 0;  // index into images.tbl for GRP filename
    bool HasDirectionalFrames = false;
    bool IsClickable = false;
    bool HasIScriptAnimations = false;
    bool AlwaysVisible = false;
    uint8_t Modifier = 0;
    uint8_t ColorShift = 0;
    uint32_t IScriptID = 0;
    uint32_t ShieldFilenameIndex = 0;
    uint32_t AttackFilenameIndex = 0;
    uint32_t DamageFilenameIndex = 0;
    uint32_t SpecialFilenameIndex = 0;
    uint32_t LandingDustFilenameIndex = 0;
    uint32_t LiftOffFilenameIndex = 0;
};

struct ImageOffset
{
    int X = 0;
    int Y = 0;
};

struct ImageOffsetTable
{
    std::vector<std::vector<ImageOffset>> Frames;
};

enum class ImageRelationKind : int32_t
{
    RootOrStandalone = 1,
    DeathOrTransition = 4,
    Shadow = 8,
    GlowOverlay = 16,
    LinkedVariant = 512
};

struct ImageRelationEntry
{
    uint32_t ImageID = 0;
    int32_t Kind = 0;
    int32_t ParentImageID = -1;

    bool HasParent() const { return ParentImageID >= 0; }
    bool IsRootOrStandalone() const { return Kind == (int32_t)ImageRelationKind::RootOrStandalone; }
    bool IsDeathOrTransition() const { return Kind == (int32_t)ImageRelationKind::DeathOrTransition; }
    bool IsShadow() const { return Kind == (int32_t)ImageRelationKind::Shadow; }
    bool IsGlowOverlay() const { return Kind == (int32_t)ImageRelationKind::GlowOverlay; }
    bool IsLinkedVariant() const { return Kind == (int32_t)ImageRelationKind::LinkedVariant; }
};

// images.tbl is a string table: (uint16_t count) + (uint16_t offsets[count]) + strings
struct ImageTblEntry
{
    uint32_t Index = 0;
    std::string Filename;
};

struct UpgradeEntry
{
    uint32_t Id = 0;
    uint32_t MineralCostBase = 0;
    uint32_t MineralCostFactor = 0;
    uint32_t GasCostBase = 0;
    uint32_t GasCostFactor = 0;
    uint32_t TimeCostBase = 0;
    uint32_t TimeCostFactor = 0;
};

struct TechEntry
{
    uint32_t Id = 0;
    uint32_t MineralCost = 0;
    uint32_t GasCost = 0;
    uint32_t TimeCost = 0;
    uint32_t RequiredUnit = 0;
};

class DatDecoder
{
public:
    static std::vector<UnitEntry> DecodeUnits(const std::vector<uint8_t>& Data);
    static std::vector<WeaponEntry> DecodeWeapons(const std::vector<uint8_t>& Data);
    static std::vector<FlingyEntry> DecodeFlingy(const std::vector<uint8_t>& Data);
    static std::vector<SpriteEntry> DecodeSprites(const std::vector<uint8_t>& Data);
    static std::vector<ImageEntry> DecodeImages(const std::vector<uint8_t>& Data);
    static std::optional<ImageOffsetTable> DecodeImageOffsets(const std::vector<uint8_t>& Data);
    static std::vector<ImageRelationEntry> DecodeImageRelations(const std::vector<uint8_t>& Data);
    static std::vector<ImageTblEntry> DecodeImageTbl(const std::vector<uint8_t>& Data);
    static std::vector<UpgradeEntry> DecodeUpgrades(const std::vector<uint8_t>& Data);
    static std::vector<TechEntry> DecodeTech(const std::vector<uint8_t>& Data);
    static std::vector<OrderEntry> DecodeOrders(const std::vector<uint8_t>& Data);
};

} // namespace Resource
