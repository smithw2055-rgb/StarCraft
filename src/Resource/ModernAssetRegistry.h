#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Resource/GameDataLoader.h"
#include "Game/IScriptVM.h"

namespace Resource {

// Generational AssetHandle<T> (Godot RID / Unity Addressable Asset Reference)
template<typename T>
struct AssetHandle {
    uint32_t Index = UINT32_MAX;
    uint32_t Generation = 0;

    bool IsValid() const { return Index != UINT32_MAX && Generation != 0; }
    bool operator==(const AssetHandle<T>& Other) const {
        return Index == Other.Index && Generation == Other.Generation;
    }
};

struct ModernWeaponAsset {
    uint32_t WeaponID = 0;
    uint32_t ProjectileFlingyID = 0;
    std::string ProjectileHDAnim;
    uint16_t DamageAmount = 0;
    uint16_t DamageBonusPerUpgrade = 0;
    uint16_t DamageFactor = 1;
    std::string DamageType;
    uint16_t CooldownTicks = 0;
    float CooldownSeconds24Fps = 0.0f;
    uint32_t MinRangePx = 0;
    uint32_t MaxRangePx = 0;
    uint16_t SplashRadiiPx[3] = {0, 0, 0};
    int16_t MuzzleSpawnOffsetPx[2] = {0, 0};
};

struct ModernSoundAsset {
    uint32_t SoundID = 0;
    std::string WavPath;
    uint8_t Priority = 0;
    uint8_t Flags = 0;
    uint16_t VolumePct = 0;
};

struct ModernSocketMountEntry {
    int32_t RawFrameIndex = 0;
    int32_t FrameSet = 0;
    int32_t DirectionIndex = 0;
    int16_t PixelOffsetSD[2] = {0, 0};
    int16_t PixelOffsetHD4x[2] = {0, 0};
    float EngineLocalPosUnits[2] = {0.0f, 0.0f};
};

struct ModernSocket2DTrack {
    std::string SocketAssetName;
    uint32_t NumRawFrames = 0;
    uint32_t NumSocketsPerFrame = 0;
    bool Directional17Way = true;
    std::vector<ModernSocketMountEntry> FrameSocketTable;

    bool SampleOffset(int32_t FrameSet, int32_t Direction17, int16_t& OutSDX, int16_t& OutSDY, int16_t& OutHDX, int16_t& OutHDY) const;
};

struct ModernHDImageAsset {
    uint32_t ImageID = 0;
    std::string HDAnimFile;
    std::string SemanticName;
    std::string LegacyGrpPath;
    bool Directional17WayFlip = false;
    bool Clickable = false;
    bool UseFullIScript = false;
    bool DrawIfCloaked = false;
    uint8_t DrawFunctionID = 0;
    std::string DrawMode;
    uint8_t RemappingID = 0;
    std::string BlendRemapMode;
    uint32_t IScriptID = 0;
    int32_t RoleFlags = 1;
    int32_t ParentImageID = -1;
    std::string ShieldOverlay;
    std::string AttackLoFile;
    std::string DamageLoFile;
    std::string SpecialLoFile;
    std::string LandingDustLoFile;
    std::string LiftoffDustLoFile;
};

struct ModernUnitDataAsset {
    uint32_t UnitID = 0;
    std::string UnitName;
    std::string Faction;
    std::string ModernAssetPath;

    // Stats
    uint32_t MaxHP = 0;
    bool ShieldsEnabled = false;
    uint16_t MaxShields = 0;
    uint8_t ElevationSortLayer = 0;
    uint8_t InitialDirection = 0;
    uint32_t SpecialAbilityBitmask = 0;

    // Locomotion
    uint16_t FlingyID = 0;
    uint16_t SpriteID = 0;
    uint32_t TopSpeedRaw = 0;
    float TopSpeedPxPerSec = 0.0f;
    int16_t AccelerationRaw = 0;
    float HaltDistancePx = 0.0f;
    float TurnRadiusDegPerTick = 0.0f;
    std::string LocomotionMode;

    // Visual Renderer
    uint32_t ImageID = 0;
    std::string PrimaryHDAnim;
    std::string ConstructionHDAnim;
    uint32_t IScriptID = 0;
    bool Directional17WayFlip = false;
    std::string DrawMode;
    std::string BlendRemapMode;
    uint16_t HealthBarWidthPx = 0;
    uint16_t SelectionCircleIndex = 0;
    uint16_t SelectionCircleYOffset = 0;
    bool HasTurretAttachment = false;
    uint16_t TurretUnitID = 228;

    // Combat
    bool HasGroundWeapon = false;
    uint32_t GroundWeaponID = 130;
    uint8_t MaxGroundHits = 0;
    bool HasAirWeapon = false;
    uint32_t AirWeaponID = 130;
    uint8_t MaxAirHits = 0;
};

struct ModernSpriteFrameKey {
    uint32_t Tick = 0;
    std::string Mode;
    int32_t FrameSetIndex = 0;
};

struct ModernAudioNotifyKey {
    uint32_t Tick = 0;
    std::string Type;
    uint32_t SoundID = 0;
    uint32_t SoundIDMax = 0;
    std::string AudioClip;
};

struct ModernCombatNotifyKey {
    uint32_t Tick = 0;
    std::string Notify; // "attackwith", "attackmelee", "domissiledmg", "useweapon", "gotorepeatattk", "castspell"
    int32_t Param0 = 0;
};

struct ModernSocketVfxSpawnKey {
    uint32_t Tick = 0;
    std::string SpawnMode; // "imgol", "imgul", "sprol", "lowsprul", "imgoluselo"
    std::string TargetAsset;
    int16_t OffsetOrLoSlot[2] = {0, 0};
};

struct ModernRootMotionKey {
    uint32_t Tick = 0;
    std::string Action; // "move", "setvertpos", "sethorpos", "setfldirect", "setflspeed", "turncwise", "turnccwise"
    int32_t Param0 = 0;
};

struct ModernFsmSignalKey {
    uint32_t Tick = 0;
    std::string SignalOrBranch; // "nobrkcodestart", "nobrkcodeend", "sigorder", "orderdone", "randcondjmp"
    int32_t Param0 = 0;
};

struct ModernAnimationClip {
    std::string ClipName;
    uint32_t EntryBytecodeOffset = 0;
    uint32_t DurationTicks24Fps = 0;
    float DurationSeconds = 0.0f;
    bool IsLooping = false;
    bool DestroysEntityOnEnd = false;
    size_t SpriteFrameKeys = 0;
    size_t AudioNotifyKeys = 0;
    size_t CombatNotifyKeys = 0;
    size_t SocketVfxSpawnKeys = 0;
    size_t RootMotionKeys = 0;
    size_t FsmSignalKeys = 0;

    std::vector<ModernSpriteFrameKey> SpriteFrameTrack;
    std::vector<ModernAudioNotifyKey> AudioNotifyTrack;
    std::vector<ModernCombatNotifyKey> CombatNotifyTrack;
    std::vector<ModernSocketVfxSpawnKey> SocketVfxSpawnTrack;
    std::vector<ModernRootMotionKey> RootMotionTrack;
    std::vector<ModernFsmSignalKey> FsmSignalTrack;
};

struct ModernIScriptControllerAsset {
    std::string ControllerName;
    uint32_t IScriptID = 0;
    uint32_t ScpeHeaderOffset = 0;
    uint32_t ScpeHeaderType = 0;
    uint32_t SupportedAnimationSlots = 0;
    std::vector<uint32_t> BoundUnitIDs;
    std::vector<uint32_t> BoundImageIDs;
    std::unordered_map<std::string, ModernAnimationClip> Clips;
};

struct ModernPBRAtlasSlice {
    uint32_t FrameIndex = 0;
    uint32_t FrameSet = 0;
    uint32_t Direction17 = 0;
    int32_t AtlasRectPx[4] = {0, 0, 0, 0};
    int32_t CanvasOffsetPx[2] = {0, 0};
    float NormalizedPivot[2] = {0.5f, 0.5f};
};

struct ModernPBRPrefabBundle {
    std::string PrefabName;
    uint32_t ImageID = 0;
    std::string LegacyAnimSource;
    int32_t AtlasResolution[2] = {0, 0};
    int32_t ReferenceCanvasSizeSD[2] = {0, 0};
    int32_t ReferenceCanvasSizeHD4x[2] = {0, 0};
    std::vector<std::string> Original7LayersPresent;
    std::string BaseColorTexturePath;
    std::string NormalTeamAOTexturePath;
    std::string SpecEmissiveTexturePath;
    std::string ShaderName;
    std::string DrawMode;
    std::string BlendRemapMode;
    bool Directional17WayFlip = false;
    int32_t RoleFlags = 1;
    int32_t ParentImageID = -1;
    std::vector<ModernPBRAtlasSlice> SpriteAtlasSlices;
    bool TexturesVerifiedOnDisk = false;
};

struct AssetCrossVerificationReport {
    bool Passed = false;
    size_t UnitsChecked = 0;
    size_t UnitMismatches = 0;
    size_t WeaponsChecked = 0;
    size_t WeaponMismatches = 0;
    size_t ImagesChecked = 0;
    size_t ImageMismatches = 0;
    size_t ImageRelationsChecked = 0;
    size_t ImageRelationMismatches = 0;
    size_t IScriptControllersChecked = 0;
    size_t IScriptMismatches = 0;
    size_t PBRPrefabsChecked = 0;
    size_t PBRTexturesVerified = 0;
    std::vector<std::string> Diagnostics;
};

class ModernAssetRegistry {
public:
    ModernAssetRegistry() = default;
    ~ModernAssetRegistry() = default;

    // Load all converted modern assets from StarCraft/assets-R directory
    bool LoadFromAssetsRDirectory(const std::string& AssetsRRootPath);

    bool IsLoaded() const { return mIsLoaded; }

    size_t GetUnitCount() const { return mUnits.size(); }
    size_t GetWeaponCount() const { return mWeapons.size(); }
    size_t GetSoundCount() const { return mSounds.size(); }
    size_t GetHDImageCount() const { return mHDImages.size(); }
    size_t GetSocketTrackCount() const { return mSocketTracks.size(); }
    size_t GetIScriptControllerCount() const { return mIScriptControllers.size(); }
    size_t GetTotalAnimationClipCount() const { return mTotalAnimationClips; }
    size_t GetPBRPrefabCount() const { return mPBRPrefabs.size(); }

    AssetHandle<ModernUnitDataAsset> GetUnitHandle(uint32_t UnitID) const {
        auto It = mUnitIndexByID.find(UnitID);
        if (It == mUnitIndexByID.end()) return {};
        return { static_cast<uint32_t>(It->second), mGeneration };
    }
    AssetHandle<ModernWeaponAsset> GetWeaponHandle(uint32_t WeaponID) const {
        auto It = mWeaponIndexByID.find(WeaponID);
        if (It == mWeaponIndexByID.end()) return {};
        return { static_cast<uint32_t>(It->second), mGeneration };
    }
    AssetHandle<ModernHDImageAsset> GetHDImageHandle(uint32_t ImageID) const {
        auto It = mHDImageIndexByID.find(ImageID);
        if (It == mHDImageIndexByID.end()) return {};
        return { static_cast<uint32_t>(It->second), mGeneration };
    }
    AssetHandle<ModernPBRPrefabBundle> GetPBRPrefabHandleByImageID(uint32_t ImageID) const {
        auto It = mPBRPrefabIndexByImageID.find(ImageID);
        if (It == mPBRPrefabIndexByImageID.end()) return {};
        return { static_cast<uint32_t>(It->second), mGeneration };
    }

    const ModernUnitDataAsset* ResolveUnit(AssetHandle<ModernUnitDataAsset> Handle) const {
        if (!Handle.IsValid() || Handle.Generation != mGeneration || Handle.Index >= mUnits.size()) return nullptr;
        return &mUnits[Handle.Index];
    }
    const ModernWeaponAsset* ResolveWeapon(AssetHandle<ModernWeaponAsset> Handle) const {
        if (!Handle.IsValid() || Handle.Generation != mGeneration || Handle.Index >= mWeapons.size()) return nullptr;
        return &mWeapons[Handle.Index];
    }
    const ModernHDImageAsset* ResolveHDImage(AssetHandle<ModernHDImageAsset> Handle) const {
        if (!Handle.IsValid() || Handle.Generation != mGeneration || Handle.Index >= mHDImages.size()) return nullptr;
        return &mHDImages[Handle.Index];
    }
    const ModernPBRPrefabBundle* ResolvePBRPrefab(AssetHandle<ModernPBRPrefabBundle> Handle) const {
        if (!Handle.IsValid() || Handle.Generation != mGeneration || Handle.Index >= mPBRPrefabs.size()) return nullptr;
        return &mPBRPrefabs[Handle.Index];
    }

    const ModernUnitDataAsset* FindUnitByID(uint32_t UnitID) const {
        return ResolveUnit(GetUnitHandle(UnitID));
    }
    const ModernWeaponAsset* FindWeaponByID(uint32_t WeaponID) const {
        return ResolveWeapon(GetWeaponHandle(WeaponID));
    }
    const ModernSoundAsset* FindSoundByID(uint32_t SoundID) const {
        auto It = mSoundIndexByID.find(SoundID);
        return It != mSoundIndexByID.end() ? &mSounds[It->second] : nullptr;
    }
    const ModernHDImageAsset* FindHDImageByID(uint32_t ImageID) const {
        return ResolveHDImage(GetHDImageHandle(ImageID));
    }
    const ModernSocket2DTrack* FindSocketTrack(const std::string& Name) const {
        auto It = mSocketTrackIndexByName.find(Name);
        return It != mSocketTrackIndexByName.end() ? &mSocketTracks[It->second] : nullptr;
    }
    const ModernIScriptControllerAsset* FindIScriptControllerByID(uint32_t IScriptID) const {
        auto It = mIScriptIndexByID.find(IScriptID);
        return It != mIScriptIndexByID.end() ? &mIScriptControllers[It->second] : nullptr;
    }
    const ModernPBRPrefabBundle* FindPBRPrefabByImageID(uint32_t ImageID) const {
        return ResolvePBRPrefab(GetPBRPrefabHandleByImageID(ImageID));
    }

    const std::vector<ModernUnitDataAsset>& GetUnits() const { return mUnits; }
    const std::vector<ModernWeaponAsset>& GetWeapons() const { return mWeapons; }
    const std::vector<ModernSoundAsset>& GetSounds() const { return mSounds; }
    const std::vector<ModernHDImageAsset>& GetHDImages() const { return mHDImages; }
    const std::vector<ModernSocket2DTrack>& GetSocketTracks() const { return mSocketTracks; }
    const std::vector<ModernIScriptControllerAsset>& GetIScriptControllers() const { return mIScriptControllers; }
    const std::vector<ModernPBRPrefabBundle>& GetPBRPrefabs() const { return mPBRPrefabs; }

    // Cross-verify 1:1 fidelity between StarCraft/assets-R and raw binary StarCraft/assets
    AssetCrossVerificationReport CrossVerifyWithBinaryAssets(
        const GameDataLoader& BinaryLoader,
        const Game::IScriptDatabase* BinaryIScriptDB = nullptr) const;

private:
    bool LoadUnitsJson(const std::string& Path);
    bool LoadWeaponsJson(const std::string& Path);
    bool LoadSoundsJson(const std::string& Path);
    bool LoadHDImagesJson(const std::string& Path);
    bool LoadSocketsJson(const std::string& Path);
    bool LoadIScriptControllersJson(const std::string& Path);
    bool LoadPBRPrefabs(const std::string& BundlesDirPath);

    bool mIsLoaded = false;
    uint32_t mGeneration = 1;
    size_t mTotalAnimationClips = 0;

    std::vector<ModernUnitDataAsset> mUnits;
    std::vector<ModernWeaponAsset> mWeapons;
    std::vector<ModernSoundAsset> mSounds;
    std::vector<ModernHDImageAsset> mHDImages;
    std::vector<ModernSocket2DTrack> mSocketTracks;
    std::vector<ModernIScriptControllerAsset> mIScriptControllers;
    std::vector<ModernPBRPrefabBundle> mPBRPrefabs;

    std::unordered_map<uint32_t, size_t> mUnitIndexByID;
    std::unordered_map<uint32_t, size_t> mWeaponIndexByID;
    std::unordered_map<uint32_t, size_t> mSoundIndexByID;
    std::unordered_map<uint32_t, size_t> mHDImageIndexByID;
    std::unordered_map<std::string, size_t> mSocketTrackIndexByName;
    std::unordered_map<uint32_t, size_t> mIScriptIndexByID;
    std::unordered_map<uint32_t, size_t> mPBRPrefabIndexByImageID;
};

} // namespace Resource
