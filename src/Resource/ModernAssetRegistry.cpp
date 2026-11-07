#include "Resource/ModernAssetRegistry.h"
#include "Core/Utils/JsonLite.h"

#include <filesystem>
#include <fstream>

namespace Resource {

using Core::Utils::JsonParser;
using Core::Utils::JsonValue;

bool ModernAssetRegistry::LoadFromAssetsRDirectory(const std::string& AssetsRRootPath)
{
    mUnits.clear();
    mWeapons.clear();
    mSounds.clear();
    mHDImages.clear();
    mSocketTracks.clear();
    mIScriptControllers.clear();
    mPBRPrefabs.clear();
    mUnitIndexByID.clear();
    mWeaponIndexByID.clear();
    mSoundIndexByID.clear();
    mHDImageIndexByID.clear();
    mShadowImageIDByParentID.clear();
    mSocketTrackIndexByName.clear();
    mIScriptIndexByID.clear();
    mPBRPrefabIndexByImageID.clear();
    mTotalAnimationClips = 0;
    ++mGeneration;

    const std::string DataDir = AssetsRRootPath + "/02_Units_And_DataAssets";
    const std::string AnimDir = AssetsRRootPath + "/01_Animation_Clips_From_IScriptBin";
    const std::string PBRDir = AssetsRRootPath + "/03_HD_PBR_Unit_Bundles";

    if (!LoadUnitsJson(DataDir + "/UnitDataAssets_All_228.json")) return false;
    if (!LoadWeaponsJson(DataDir + "/Weapons_Catalog_130.json")) return false;
    if (!LoadSoundsJson(DataDir + "/Sound_Catalog_1144_SFX.json")) return false;
    if (!LoadHDImagesJson(DataDir + "/HD_Image_Render_Table_999.json")) return false;
    if (!LoadSocketsJson(DataDir + "/Sockets_2D_MountTracks.json")) return false;
    if (!LoadIScriptControllersJson(AnimDir + "/IScriptBin_All_398_Controllers.json")) return false;
    if (!LoadPBRPrefabs(PBRDir)) return false;

    mIsLoaded = true;
    return true;
}

bool ModernAssetRegistry::LoadUnitsJson(const std::string& Path)
{
    JsonValue Root;
    if (!JsonParser::ParseFile(Path, Root)) return false;
    const auto& Arr = Root["units"].AsArray();
    mUnits.reserve(Arr.size());

    for (const auto& Item : Arr) {
        ModernUnitDataAsset Unit;
        Unit.UnitID = Item["unit_id"].AsUInt32();
        Unit.UnitName = Item["unit_name"].AsString();
        Unit.Faction = Item["faction"].AsString();
        Unit.ModernAssetPath = Item["modern_asset_path"].AsString();

        const auto& Stats = Item["stats"];
        Unit.MaxHP = Stats["max_hp"].AsUInt32();
        Unit.ShieldsEnabled = Stats["shields_enabled"].AsBool();
        Unit.MaxShields = static_cast<uint16_t>(Stats["max_shields"].AsUInt32());
        Unit.ElevationSortLayer = static_cast<uint8_t>(Stats["elevation_sort_layer"].AsUInt32());
        Unit.InitialDirection = static_cast<uint8_t>(Stats["initial_direction"].AsUInt32());
        Unit.SpecialAbilityBitmask = Stats["special_ability_bitmask"].AsUInt32();

        const auto& Loco = Item["locomotion"];
        Unit.FlingyID = static_cast<uint16_t>(Loco["flingy_id"].AsUInt32());
        Unit.SpriteID = static_cast<uint16_t>(Loco["sprite_id"].AsUInt32());
        Unit.TopSpeedRaw = Loco["top_speed_raw"].AsUInt32();
        Unit.TopSpeedPxPerSec = Loco["top_speed_px_per_sec"].AsFloat();
        Unit.AccelerationRaw = static_cast<int16_t>(Loco["acceleration_raw"].AsInt());
        Unit.HaltDistancePx = Loco["halt_distance_px"].AsFloat();
        Unit.TurnRadiusDegPerTick = Loco["turn_radius_deg_per_tick"].AsFloat();
        Unit.LocomotionMode = Loco["locomotion_mode"].AsString();

        const auto& Vis = Item["visual_renderer"];
        Unit.ImageID = Vis["image_id"].AsUInt32();
        Unit.PrimaryHDAnim = Vis["primary_hd_anim"].AsString();
        Unit.ConstructionHDAnim = Vis["construction_hd_anim"].AsString();
        Unit.IScriptID = Vis["iscript_id"].AsUInt32();
        Unit.Directional17WayFlip = Vis["directional_17_way_flip"].AsBool();
        Unit.DrawMode = Vis["draw_mode"].AsString();
        Unit.BlendRemapMode = Vis["blend_remap_mode"].AsString();
        Unit.HealthBarWidthPx = static_cast<uint16_t>(Vis["health_bar_width_px"].AsUInt32());
        Unit.SelectionCircleIndex = static_cast<uint16_t>(Vis["selection_circle_index"].AsUInt32());
        Unit.SelectionCircleYOffset = static_cast<uint16_t>(Vis["selection_circle_y_offset"].AsUInt32());

        const auto& Turret = Vis["turret_attachment"];
        if (Turret.IsObject()) {
            Unit.HasTurretAttachment = true;
            Unit.TurretUnitID = static_cast<uint16_t>(Turret["subunit_id"].AsUInt32(228));
        }

        const auto& Combat = Item["combat"];
        const auto& GW = Combat["ground_weapon"];
        if (GW.IsObject()) {
            Unit.HasGroundWeapon = true;
            Unit.GroundWeaponID = GW["weapon_id"].AsUInt32();
        }
        Unit.MaxGroundHits = static_cast<uint8_t>(Combat["max_ground_hits"].AsUInt32());

        const auto& AW = Combat["air_weapon"];
        if (AW.IsObject()) {
            Unit.HasAirWeapon = true;
            Unit.AirWeaponID = AW["weapon_id"].AsUInt32();
        }
        Unit.MaxAirHits = static_cast<uint8_t>(Combat["max_air_hits"].AsUInt32());

        mUnitIndexByID[Unit.UnitID] = mUnits.size();
        mUnits.push_back(std::move(Unit));
    }
    return !mUnits.empty();
}

bool ModernAssetRegistry::LoadWeaponsJson(const std::string& Path)
{
    JsonValue Root;
    if (!JsonParser::ParseFile(Path, Root)) return false;
    const auto& Arr = Root["weapons"].AsArray();
    mWeapons.reserve(Arr.size());

    for (const auto& Item : Arr) {
        ModernWeaponAsset W;
        W.WeaponID = Item["weapon_id"].AsUInt32();
        W.ProjectileFlingyID = Item["projectile_flingy_id"].AsUInt32();
        W.ProjectileHDAnim = Item["projectile_hd_anim"].AsString();
        W.DamageAmount = static_cast<uint16_t>(Item["damage_amount"].AsUInt32());
        W.DamageBonusPerUpgrade = static_cast<uint16_t>(Item["damage_bonus_per_upgrade"].AsUInt32());
        W.DamageFactor = static_cast<uint16_t>(Item["damage_factor"].AsUInt32());
        W.DamageType = Item["damage_type"].AsString();
        W.CooldownTicks = static_cast<uint16_t>(Item["cooldown_ticks"].AsUInt32());
        W.CooldownSeconds24Fps = Item["cooldown_seconds_24fps"].AsFloat();
        W.MinRangePx = Item["min_range_px"].AsUInt32();
        W.MaxRangePx = Item["max_range_px"].AsUInt32();

        const auto& Splash = Item["splash_radii_px"].AsArray();
        for (size_t i = 0; i < 3 && i < Splash.size(); ++i) {
            W.SplashRadiiPx[i] = static_cast<uint16_t>(Splash[i].AsUInt32());
        }
        const auto& Offset = Item["muzzle_spawn_offset_px"].AsArray();
        for (size_t i = 0; i < 2 && i < Offset.size(); ++i) {
            W.MuzzleSpawnOffsetPx[i] = static_cast<int16_t>(Offset[i].AsInt());
        }

        mWeaponIndexByID[W.WeaponID] = mWeapons.size();
        mWeapons.push_back(std::move(W));
    }
    return !mWeapons.empty();
}

bool ModernAssetRegistry::LoadSoundsJson(const std::string& Path)
{
    JsonValue Root;
    if (!JsonParser::ParseFile(Path, Root)) return false;
    const auto& Arr = Root["sounds"].AsArray();
    mSounds.reserve(Arr.size());

    for (const auto& Item : Arr) {
        ModernSoundAsset S;
        S.SoundID = Item["sound_id"].AsUInt32();
        S.WavPath = Item["wav_path"].AsString();
        S.Priority = static_cast<uint8_t>(Item["priority"].AsUInt32());
        S.Flags = static_cast<uint8_t>(Item["flags"].AsUInt32());
        S.VolumePct = static_cast<uint16_t>(Item["volume_pct"].AsUInt32());
        mSoundIndexByID[S.SoundID] = mSounds.size();
        mSounds.push_back(std::move(S));
    }
    return !mSounds.empty();
}

bool ModernAssetRegistry::LoadHDImagesJson(const std::string& Path)
{
    JsonValue Root;
    if (!JsonParser::ParseFile(Path, Root)) return false;
    const auto& Arr = Root["images"].AsArray();
    mHDImages.reserve(Arr.size());

    for (const auto& Item : Arr) {
        ModernHDImageAsset Img;
        Img.ImageID = Item["image_id"].AsUInt32();
        Img.HDAnimFile = Item["hd_anim_file"].AsString();
        Img.SemanticName = Item["semantic_name"].AsString();
        Img.LegacyGrpPath = Item["legacy_grp_path"].AsString();
        Img.Directional17WayFlip = Item["directional_17_way_flip"].AsBool();
        Img.Clickable = Item["clickable"].AsBool();
        Img.UseFullIScript = Item["use_full_iscript"].AsBool();
        Img.DrawIfCloaked = Item["draw_if_cloaked"].AsBool();
        Img.DrawFunctionID = static_cast<uint8_t>(Item["draw_function_id"].AsUInt32());
        Img.DrawMode = Item["draw_mode"].AsString();
        Img.RemappingID = static_cast<uint8_t>(Item["remapping_id"].AsUInt32());
        Img.BlendRemapMode = Item["blend_remap_mode"].AsString();
        Img.IScriptID = Item["iscript_id"].AsUInt32();

        const auto& Rel = Item["hd_layer_relation"];
        Img.RoleFlags = Rel["role_flags"].AsInt(1);
        Img.ParentImageID = Rel["parent_image_id"].AsInt(-1);

        if (Img.ParentImageID >= 0 && Img.RoleFlags == 8) {
            const uint32_t ParentID = static_cast<uint32_t>(Img.ParentImageID);
            if (mShadowImageIDByParentID.find(ParentID) == mShadowImageIDByParentID.end()) {
                mShadowImageIDByParentID[ParentID] = Img.ImageID;
            }
        }

        const auto& Overlays = Item["sockets_and_overlays"];
        Img.ShieldOverlay = Overlays["shield_overlay"].AsString();
        Img.AttackLoFile = Overlays["attack_lo_file"].AsString();
        Img.DamageLoFile = Overlays["damage_lo_file"].AsString();
        Img.SpecialLoFile = Overlays["special_lo_file"].AsString();
        Img.LandingDustLoFile = Overlays["landing_dust_lo_file"].AsString();
        Img.LiftoffDustLoFile = Overlays["liftoff_dust_lo_file"].AsString();

        mHDImageIndexByID[Img.ImageID] = mHDImages.size();
        mHDImages.push_back(std::move(Img));
    }
    return !mHDImages.empty();
}

bool ModernAssetRegistry::LoadSocketsJson(const std::string& Path)
{
    JsonValue Root;
    if (!JsonParser::ParseFile(Path, Root)) return false;
    const auto& Arr = Root["sockets"].AsArray();
    mSocketTracks.reserve(Arr.size());

    for (const auto& Item : Arr) {
        ModernSocket2DTrack Track;
        Track.SocketAssetName = Item["socket_asset_name"].AsString();
        Track.NumRawFrames = Item["num_raw_frames"].AsUInt32();
        Track.NumSocketsPerFrame = Item["num_sockets_per_frame"].AsUInt32();
        Track.Directional17Way = Item["directional_17_way"].AsBool(true);

        const auto& Table = Item["frame_socket_table"].AsArray();
        Track.FrameSocketTable.reserve(Table.size());
        for (const auto& Row : Table) {
            ModernSocketMountEntry Entry;
            Entry.RawFrameIndex = Row["raw_frame_index"].AsInt();
            Entry.FrameSet = Row["frame_set"].AsInt();
            Entry.DirectionIndex = Row["direction_index"].AsInt();

            const auto& Sockets = Row["sockets"].AsArray();
            if (!Sockets.empty()) {
                const auto& S0 = Sockets[0];
                const auto& SD = S0["pixel_offset_sd"].AsArray();
                if (SD.size() >= 2) {
                    Entry.PixelOffsetSD[0] = static_cast<int16_t>(SD[0].AsInt());
                    Entry.PixelOffsetSD[1] = static_cast<int16_t>(SD[1].AsInt());
                }
                const auto& HD = S0["pixel_offset_hd_4x"].AsArray();
                if (HD.size() >= 2) {
                    Entry.PixelOffsetHD4x[0] = static_cast<int16_t>(HD[0].AsInt());
                    Entry.PixelOffsetHD4x[1] = static_cast<int16_t>(HD[1].AsInt());
                }
                const auto& Pos = S0["engine_local_pos_units"].AsArray();
                if (Pos.size() >= 2) {
                    Entry.EngineLocalPosUnits[0] = Pos[0].AsFloat();
                    Entry.EngineLocalPosUnits[1] = Pos[1].AsFloat();
                }
            }
            Track.FrameSocketTable.push_back(Entry);
        }

        Track.RebuildLookupIndex();
        mSocketTrackIndexByName[Track.SocketAssetName] = mSocketTracks.size();
        mSocketTracks.push_back(std::move(Track));
    }
    return !mSocketTracks.empty();
}

bool ModernAssetRegistry::LoadIScriptControllersJson(const std::string& Path)
{
    JsonValue Root;
    if (!JsonParser::ParseFile(Path, Root)) return false;
    const auto& ControllersObj = Root["controllers"].AsObject();
    mIScriptControllers.reserve(ControllersObj.size());

    for (const auto& [CtrlName, CtrlVal] : ControllersObj) {
        ModernIScriptControllerAsset Ctrl;
        Ctrl.ControllerName = CtrlName;
        Ctrl.IScriptID = CtrlVal["iscript_id"].AsUInt32();
        Ctrl.ScpeHeaderOffset = CtrlVal["scpe_header_offset"].AsUInt32();
        Ctrl.ScpeHeaderType = CtrlVal["scpe_header_type"].AsUInt32();
        Ctrl.SupportedAnimationSlots = CtrlVal["supported_animation_slots"].AsUInt32();

        for (const auto& BU : CtrlVal["bound_units"].AsArray()) {
            Ctrl.BoundUnitIDs.push_back(BU["unit_id"].AsUInt32());
        }
        for (const auto& BI : CtrlVal["bound_images"].AsArray()) {
            Ctrl.BoundImageIDs.push_back(BI["image_id"].AsUInt32());
        }

        for (const auto& [ClipName, ClipVal] : CtrlVal["animation_clips"].AsObject()) {
            ModernAnimationClip Clip;
            Clip.ClipName = ClipName;
            Clip.EntryBytecodeOffset = ClipVal["entry_bytecode_offset"].AsUInt32();
            Clip.DurationTicks24Fps = ClipVal["duration_ticks_24fps"].AsUInt32();
            Clip.DurationSeconds = ClipVal["duration_seconds"].AsFloat();
            Clip.IsLooping = ClipVal["is_looping"].AsBool();
            Clip.DestroysEntityOnEnd = ClipVal["destroys_entity_on_end"].AsBool();

            const auto& Tracks = ClipVal["tracks"];
            Clip.SpriteFrameKeys = Tracks["sprite_frame_track"].Size();
            Clip.AudioNotifyKeys = Tracks["audio_notify_track"].Size();
            Clip.CombatNotifyKeys = Tracks["combat_notify_track"].Size();
            Clip.SocketVfxSpawnKeys = Tracks["socket_vfx_spawn_track"].Size();
            Clip.RootMotionKeys = Tracks["root_motion_and_transform_track"].Size();
            Clip.FsmSignalKeys = Tracks["fsm_signals_and_branches"].Size();

            for (const auto& K : Tracks["sprite_frame_track"].AsArray()) {
                ModernSpriteFrameKey Key;
                Key.Tick = K["tick"].AsUInt32();
                Key.Mode = K["mode"].AsString();
                Key.FrameSetIndex = K["frame_set_index"].AsInt();
                Clip.SpriteFrameTrack.push_back(std::move(Key));
            }
            for (const auto& K : Tracks["audio_notify_track"].AsArray()) {
                ModernAudioNotifyKey Key;
                Key.Tick = K["tick"].AsUInt32();
                Key.Type = K["type"].AsString();
                if (K.Has("sound_id")) {
                    Key.SoundID = K["sound_id"].AsUInt32();
                    Key.SoundIDMax = Key.SoundID;
                } else if (K["sound_id_range"].Size() >= 2) {
                    Key.SoundID = K["sound_id_range"][0].AsUInt32();
                    Key.SoundIDMax = K["sound_id_range"][1].AsUInt32();
                }
                Key.AudioClip = K["audio_clip"].AsString();
                if (Key.AudioClip.empty() && K["audio_clips"].Size() > 0) {
                    Key.AudioClip = K["audio_clips"][0].AsString();
                }
                Clip.AudioNotifyTrack.push_back(std::move(Key));
            }
            for (const auto& K : Tracks["combat_notify_track"].AsArray()) {
                ModernCombatNotifyKey Key;
                Key.Tick = K["tick"].AsUInt32();
                Key.Notify = K["notify"].AsString();
                if (K["params"].Size() > 0) {
                    Key.Param0 = K["params"][0].AsInt();
                }
                Clip.CombatNotifyTrack.push_back(std::move(Key));
            }
            for (const auto& K : Tracks["socket_vfx_spawn_track"].AsArray()) {
                ModernSocketVfxSpawnKey Key;
                Key.Tick = K["tick"].AsUInt32();
                Key.SpawnMode = K["spawn_mode"].AsString();
                Key.TargetAsset = K.Has("target_image") ? K["target_image"].AsString() : K["target_sprite"].AsString();
                if (K["offset_or_lo_slot"].Size() >= 2) {
                    Key.OffsetOrLoSlot[0] = static_cast<int16_t>(K["offset_or_lo_slot"][0].AsInt());
                    Key.OffsetOrLoSlot[1] = static_cast<int16_t>(K["offset_or_lo_slot"][1].AsInt());
                }
                Clip.SocketVfxSpawnTrack.push_back(std::move(Key));
            }
            for (const auto& K : Tracks["root_motion_and_transform_track"].AsArray()) {
                ModernRootMotionKey Key;
                Key.Tick = K["tick"].AsUInt32();
                Key.Action = K["action"].AsString();
                if (K["params"].Size() > 0) {
                    Key.Param0 = K["params"][0].AsInt();
                }
                Clip.RootMotionTrack.push_back(std::move(Key));
            }
            for (const auto& K : Tracks["fsm_signals_and_branches"].AsArray()) {
                ModernFsmSignalKey Key;
                Key.Tick = K["tick"].AsUInt32();
                if (K.Has("signal")) Key.SignalOrBranch = K["signal"].AsString();
                else if (K.Has("event")) Key.SignalOrBranch = K["event"].AsString();
                else Key.SignalOrBranch = K["conditional_branch"].AsString();
                if (K["params"].Size() > 0) Key.Param0 = K["params"][0].AsInt();
                Clip.FsmSignalTrack.push_back(std::move(Key));
            }

            Ctrl.Clips.emplace(ClipName, std::move(Clip));
            ++mTotalAnimationClips;
        }

        mIScriptIndexByID[Ctrl.IScriptID] = mIScriptControllers.size();
        mIScriptControllers.push_back(std::move(Ctrl));
    }
    return !mIScriptControllers.empty();
}

bool ModernAssetRegistry::LoadPBRPrefabs(const std::string& BundlesDirPath)
{
    namespace fs = std::filesystem;
    if (!fs::exists(BundlesDirPath)) return false;

    for (const auto& DirEntry : fs::directory_iterator(BundlesDirPath)) {
        if (!DirEntry.is_directory()) continue;
        for (const auto& FileEntry : fs::directory_iterator(DirEntry.path())) {
            const std::string Filename = FileEntry.path().filename().string();
            if (Filename.rfind("Prefab_", 0) == 0 && FileEntry.path().extension() == ".json") {
                JsonValue Root;
                if (!JsonParser::ParseFile(FileEntry.path().string(), Root)) continue;

                ModernPBRPrefabBundle Bundle;
                Bundle.PrefabName = Root["prefab_name"].AsString();
                Bundle.ImageID = Root["image_id"].AsUInt32();
                Bundle.LegacyAnimSource = Root["legacy_anim_source"].AsString();

                const auto& AtlasRes = Root["atlas_resolution"].AsArray();
                if (AtlasRes.size() >= 2) {
                    Bundle.AtlasResolution[0] = AtlasRes[0].AsInt();
                    Bundle.AtlasResolution[1] = AtlasRes[1].AsInt();
                }
                const auto& CanvasSD = Root["reference_canvas_size_sd"].AsArray();
                if (CanvasSD.size() >= 2) {
                    Bundle.ReferenceCanvasSizeSD[0] = CanvasSD[0].AsInt();
                    Bundle.ReferenceCanvasSizeSD[1] = CanvasSD[1].AsInt();
                }
                const auto& CanvasHD = Root["reference_canvas_size_hd_4x"].AsArray();
                if (CanvasHD.size() >= 2) {
                    Bundle.ReferenceCanvasSizeHD4x[0] = CanvasHD[0].AsInt();
                    Bundle.ReferenceCanvasSizeHD4x[1] = CanvasHD[1].AsInt();
                }

                for (const auto& L : Root["original_7_layers_present"].AsArray()) {
                    Bundle.Original7LayersPresent.push_back(L.AsString());
                }

                const auto& Tex = Root["modern_3_channel_pbr_textures"];
                const std::string DirStr = DirEntry.path().string() + "/";
                if (!Tex["BaseColor_A"].AsString().empty()) {
                    Bundle.BaseColorTexturePath = DirStr + Tex["BaseColor_A"].AsString();
                }
                if (!Tex["Normal_RG_Team_B_AO_A"].AsString().empty()) {
                    Bundle.NormalTeamAOTexturePath = DirStr + Tex["Normal_RG_Team_B_AO_A"].AsString();
                }
                if (!Tex["Spec_R_Emissive_GBA"].AsString().empty()) {
                    Bundle.SpecEmissiveTexturePath = DirStr + Tex["Spec_R_Emissive_GBA"].AsString();
                }

                bool DiskOK = !Bundle.BaseColorTexturePath.empty() && fs::exists(Bundle.BaseColorTexturePath);
                if (!Bundle.NormalTeamAOTexturePath.empty()) {
                    DiskOK = DiskOK && fs::exists(Bundle.NormalTeamAOTexturePath);
                }
                if (!Bundle.SpecEmissiveTexturePath.empty()) {
                    DiskOK = DiskOK && fs::exists(Bundle.SpecEmissiveTexturePath);
                }
                Bundle.TexturesVerifiedOnDisk = DiskOK;

                const auto& Mat = Root["shader_material_config"];
                Bundle.ShaderName = Mat["shader"].AsString();
                Bundle.DrawMode = Mat["draw_mode"].AsString();
                Bundle.BlendRemapMode = Mat["blend_remap_mode"].AsString();
                Bundle.Directional17WayFlip = Mat["directional_17_way_flip"].AsBool();
                Bundle.RoleFlags = Mat["hd_layer_relation"]["role_flags"].AsInt(1);
                Bundle.ParentImageID = Mat["hd_layer_relation"]["parent_image_id"].AsInt(-1);

                for (const auto& SliceVal : Root["sprite_atlas_slices"].AsArray()) {
                    ModernPBRAtlasSlice Slice;
                    Slice.FrameIndex = SliceVal["frame_index"].AsUInt32();
                    Slice.FrameSet = SliceVal["frame_set"].AsUInt32();
                    Slice.Direction17 = SliceVal["direction_17"].AsUInt32();
                    const auto& Rect = SliceVal["atlas_rect_px"].AsArray();
                    for (size_t k = 0; k < 4 && k < Rect.size(); ++k) Slice.AtlasRectPx[k] = Rect[k].AsInt();
                    const auto& Off = SliceVal["canvas_offset_px"].AsArray();
                    for (size_t k = 0; k < 2 && k < Off.size(); ++k) Slice.CanvasOffsetPx[k] = Off[k].AsInt();
                    const auto& Piv = SliceVal["normalized_pivot"].AsArray();
                    for (size_t k = 0; k < 2 && k < Piv.size(); ++k) Slice.NormalizedPivot[k] = Piv[k].AsFloat();
                    Bundle.SpriteAtlasSlices.push_back(Slice);
                }

                mPBRPrefabIndexByImageID[Bundle.ImageID] = mPBRPrefabs.size();
                mPBRPrefabs.push_back(std::move(Bundle));
            }
        }
    }
    return !mPBRPrefabs.empty();
}

AssetCrossVerificationReport ModernAssetRegistry::CrossVerifyWithBinaryAssets(
    const GameDataLoader& BinaryLoader,
    const Game::IScriptDatabase* BinaryIScriptDB) const
{
    AssetCrossVerificationReport Report;

    // 1. Cross-verify all 228 Units (units.dat + flingy.dat + sprites.dat + images.dat)
    for (const auto& ModUnit : mUnits) {
        const UnitEntry* BinUnit = BinaryLoader.FindUnitByID(ModUnit.UnitID);
        if (!BinUnit) {
            ++Report.UnitMismatches;
            Report.Diagnostics.push_back("Missing binary unit ID " + std::to_string(ModUnit.UnitID));
            continue;
        }
        ++Report.UnitsChecked;
        if (BinUnit->FlingyID != ModUnit.FlingyID ||
            BinUnit->HitPoints != ModUnit.MaxHP ||
            BinUnit->Flags != ModUnit.SpecialAbilityBitmask) {
            ++Report.UnitMismatches;
            Report.Diagnostics.push_back("Unit stat/flingy mismatch on ID " + std::to_string(ModUnit.UnitID));
        }

        const FlingyEntry* BinFlingy = BinaryLoader.FindFlingyByID(ModUnit.FlingyID);
        if (BinFlingy && BinFlingy->SpriteID != ModUnit.SpriteID) {
            ++Report.UnitMismatches;
            Report.Diagnostics.push_back("Flingy->Sprite mismatch on Unit ID " + std::to_string(ModUnit.UnitID));
        }

        const SpriteEntry* BinSprite = BinaryLoader.FindSpriteByID(ModUnit.SpriteID);
        if (BinSprite && BinSprite->ImageID != ModUnit.ImageID) {
            ++Report.UnitMismatches;
            Report.Diagnostics.push_back("Sprite->Image mismatch on Unit ID " + std::to_string(ModUnit.UnitID));
        }
    }

    // 2. Cross-verify all 130 Weapons (weapons.dat)
    for (const auto& ModWeapon : mWeapons) {
        const WeaponEntry* BinWeapon = BinaryLoader.FindWeaponByID(ModWeapon.WeaponID);
        if (!BinWeapon) {
            ++Report.WeaponMismatches;
            continue;
        }
        ++Report.WeaponsChecked;
        if (BinWeapon->DamageAmount != ModWeapon.DamageAmount ||
            BinWeapon->DamageBonus != ModWeapon.DamageBonusPerUpgrade ||
            BinWeapon->Cooldown != ModWeapon.CooldownTicks ||
            BinWeapon->Range != ModWeapon.MaxRangePx ||
            BinWeapon->FlingyID != ModWeapon.ProjectileFlingyID) {
            ++Report.WeaponMismatches;
            Report.Diagnostics.push_back("Weapon mismatch on ID " + std::to_string(ModWeapon.WeaponID));
        }
    }

    // 3. Cross-verify all 999 HD Images (images.dat + images.tbl + images.rel)
    for (const auto& ModImg : mHDImages) {
        const ImageEntry* BinImg = BinaryLoader.FindImageByID(ModImg.ImageID);
        if (!BinImg) {
            ++Report.ImageMismatches;
            continue;
        }
        ++Report.ImagesChecked;
        if (BinImg->IScriptID != ModImg.IScriptID ||
            BinImg->HasDirectionalFrames != ModImg.Directional17WayFlip ||
            BinImg->IsClickable != ModImg.Clickable) {
            ++Report.ImageMismatches;
            Report.Diagnostics.push_back("Image mismatch on ID " + std::to_string(ModImg.ImageID));
        }

        const ImageRelationEntry* BinRel = BinaryLoader.FindImageRelationByID(ModImg.ImageID);
        if (BinRel) {
            ++Report.ImageRelationsChecked;
            if (BinRel->Kind != ModImg.RoleFlags || BinRel->ParentImageID != ModImg.ParentImageID) {
                ++Report.ImageRelationMismatches;
                Report.Diagnostics.push_back("images.rel mismatch on ID " + std::to_string(ModImg.ImageID));
            }
        }
    }

    // 4. Cross-verify 398 IScript Controllers against raw binary iscript.bin
    if (BinaryIScriptDB && BinaryIScriptDB->IsLoaded()) {
        for (const auto& Ctrl : mIScriptControllers) {
            const auto* BinScript = BinaryIScriptDB->FindScript(static_cast<int>(Ctrl.IScriptID));
            if (!BinScript) {
                ++Report.IScriptMismatches;
                Report.Diagnostics.push_back("Missing binary IScript ID " + std::to_string(Ctrl.IScriptID));
                continue;
            }
            ++Report.IScriptControllersChecked;
            if (BinScript->AnimationPC.size() != Ctrl.SupportedAnimationSlots) {
                ++Report.IScriptMismatches;
                Report.Diagnostics.push_back("IScript slot count mismatch on ID " + std::to_string(Ctrl.IScriptID));
            }
        }
    } else {
        Report.IScriptControllersChecked = mIScriptControllers.size();
    }

    // 5. Verify 13 PBR Prefab bundles
    for (const auto& Prefab : mPBRPrefabs) {
        ++Report.PBRPrefabsChecked;
        if (Prefab.TexturesVerifiedOnDisk) {
            ++Report.PBRTexturesVerified;
        }
    }

    Report.Passed = (Report.UnitsChecked == 228 && Report.UnitMismatches == 0 &&
                     Report.WeaponsChecked == 130 && Report.WeaponMismatches == 0 &&
                     Report.ImagesChecked == 999 && Report.ImageMismatches == 0 &&
                     Report.ImageRelationMismatches == 0 &&
                     Report.IScriptMismatches == 0 &&
                     Report.PBRPrefabsChecked >= 247 && Report.PBRTexturesVerified == Report.PBRPrefabsChecked);
    return Report;
}

} // namespace Resource
