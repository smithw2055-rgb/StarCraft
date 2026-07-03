#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace Resource
{

// Forwad decls
class GameDataLoader;

struct UnitEntry
{
    uint32_t Id = 0;
    std::string Name;
    uint16_t SpriteID = 0;
    uint16_t PortraitID = 0;
    uint32_t HitPoints = 100;
    uint16_t ShieldAmount = 0;
    uint16_t Armor = 0;
    uint8_t ArmorUpgrade = 0;
    uint16_t BuildTime = 60;
    uint32_t MineralCost = 50;
    uint32_t GasCost = 0;
    uint16_t SupplyCost = 1;
    uint16_t FlingyID = 0;
    uint16_t Subunit1ID = 228;
    uint16_t Subunit2ID = 228;
    uint16_t WeaponID = 0;
    uint16_t WeaponID2 = 0;
    uint32_t RequiredTech = 0;
    uint32_t RequiredUnits = 0;  // bitmask
    uint16_t UnitSize = 0;
    uint16_t GroundWeapon = 0;
    uint16_t AirWeapon = 0;
    uint8_t Plagueable = 1;
    uint8_t Hallucinable = 1;
    uint8_t IsBuilding = 0;
};

struct WeaponEntry
{
    uint32_t Id = 0;
    std::string Name;
    uint16_t DamageAmount = 0;
    uint16_t DamageBonus = 0;
    uint16_t DamageFactor = 0;
    uint16_t Cooldown = 30;
    uint16_t Range = 32;
    uint16_t SplashType = 0;
    uint16_t TargetFlags = 0;
    uint16_t WeaponBehavior = 0;
};

struct FlingyEntry
{
    uint32_t Id = 0;
    uint32_t Speed = 0;
    uint32_t Acceleration = 0;
    uint32_t TurnRate = 0;
    uint32_t SpriteID = 0;
    uint32_t TopSpeed = 0;
};

struct SpriteEntry
{
    uint32_t Id = 0;
    uint32_t ImageID = 0;
    uint32_t HealthBar = 0;
    uint32_t VisibilitySize = 0;
    uint32_t SelectionCircle = 0;
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
    static std::vector<ImageRelationEntry> DecodeImageRelations(const std::vector<uint8_t>& Data);
    static std::vector<ImageTblEntry> DecodeImageTbl(const std::vector<uint8_t>& Data);
    static std::vector<UpgradeEntry> DecodeUpgrades(const std::vector<uint8_t>& Data);
    static std::vector<TechEntry> DecodeTech(const std::vector<uint8_t>& Data);
};

} // namespace Resource
