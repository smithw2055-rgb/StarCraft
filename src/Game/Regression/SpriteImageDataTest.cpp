#include "Resource/GameDataLoader.h"
#include "Resource/DatDecoder.h"
#include "Resource/GrpDecoder.h"
#include "Resource/LightTable.h"
#include "Resource/GrpToGpu.h"
#include "Resource/TblDecoder.h"

#include "Core/Audio/AudioSystem.h"
#include "Game/DataEnums.h"
#include "Game/IScriptVM.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <array>
#include <string>
#include <vector>

namespace
{
    struct AnimationProbe
    {
        bool Present = false;
        bool StillRunning = false;
        int FrameChanges = 0;
        int VisualEvents = 0;
        int FirstVisualImage = -1;
        std::vector<int> VisualImages;
    };

    AnimationProbe ProbeAnimation(const Game::IScriptDatabase& Scripts,
                                  uint32_t ImageID,
                                  int AnimationID)
    {
        AnimationProbe Result;
        const auto* Image = Scripts.FindImageInfo((int)ImageID);
        const auto* Script = Image && Image->IScriptID >= 0
            ? Scripts.FindScript(Image->IScriptID)
            : nullptr;
        if (!Script || AnimationID < 0 ||
            AnimationID >= (int)Script->AnimationPC.size() ||
            Script->AnimationPC[(size_t)AnimationID] <= 0)
        {
            return Result;
        }

        Result.Present = true;
        Game::IScriptVM VM;
        VM.LoadScript(AnimationID, &Scripts.GetProgram().ProgramData,
                      Script->AnimationPC[(size_t)AnimationID]);
        VM.GetContext().ImageID = (int)ImageID;
        int PreviousFrame = VM.GetCurrentFrame();
        for (int Step = 0; Step < 256 && VM.IsRunning(); ++Step)
        {
            VM.ExecuteFrame();
            if (VM.GetCurrentFrame() != PreviousFrame)
            {
                ++Result.FrameChanges;
                PreviousFrame = VM.GetCurrentFrame();
            }
            for (const auto& Event : VM.ConsumeEvents())
            {
                if (Event.Type == Game::IScriptEventType::CreateImageOverlay ||
                    Event.Type == Game::IScriptEventType::CreateImageOverlayLo ||
                    Event.Type == Game::IScriptEventType::CreateImageOverlaySpecialOffset ||
                    Event.Type == Game::IScriptEventType::CreateSpriteOverlay ||
                    Event.Type == Game::IScriptEventType::CreateSpriteOverlayLo)
                {
                    if (Result.FirstVisualImage < 0)
                        Result.FirstVisualImage = Event.A;
                    if (std::find(Result.VisualImages.begin(), Result.VisualImages.end(),
                                  Event.A) == Result.VisualImages.end())
                    {
                        Result.VisualImages.push_back(Event.A);
                    }
                    ++Result.VisualEvents;
                }
            }
        }
        Result.StillRunning = VM.IsRunning();
        return Result;
    }

    std::vector<int> ProbeAnimationSounds(const Game::IScriptDatabase& Scripts,
                                          uint32_t ImageID,
                                          int AnimationID)
    {
        std::vector<int> Result;
        const auto* Image = Scripts.FindImageInfo((int)ImageID);
        const auto* Script = Image && Image->IScriptID >= 0
            ? Scripts.FindScript(Image->IScriptID)
            : nullptr;
        if (!Script || AnimationID < 0 ||
            AnimationID >= (int)Script->AnimationPC.size() ||
            Script->AnimationPC[(size_t)AnimationID] <= 0)
        {
            return Result;
        }

        Game::IScriptVM VM;
        VM.LoadScript(AnimationID, &Scripts.GetProgram().ProgramData,
                      Script->AnimationPC[(size_t)AnimationID]);
        VM.GetContext().ImageID = (int)ImageID;
        VM.GetContext().HasDirectionalFrames = Image->HasDirectionalFrames;
        for (int Step = 0; Step < 512 && VM.IsRunning(); ++Step)
        {
            VM.ExecuteFrame();
            for (const auto& Event : VM.ConsumeEvents())
            {
                if (Event.Type == Game::IScriptEventType::PlaySound ||
                    Event.Type == Game::IScriptEventType::PlayRandomSound ||
                    Event.Type == Game::IScriptEventType::PlaySoundBetween)
                {
                    Result.push_back(Event.A);
                }
            }
        }
        return Result;
    }

    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    std::vector<uint8_t> ReadAll(const std::string& Path)
    {
        std::ifstream File(Path, std::ios::binary);
        Require(File.good(), ("open " + Path).c_str());
        return std::vector<uint8_t>(
            std::istreambuf_iterator<char>(File),
            std::istreambuf_iterator<char>());
    }

    void RequireUnitBalance(const Resource::GameDataLoader& Loader,
                            uint32_t ChkUnitID,
                            uint32_t HitPoints,
                            uint16_t ShieldAmount,
                            uint16_t Armor,
                            uint8_t ArmorUpgrade,
                            uint16_t UnitSize,
                            uint16_t GroundWeapon,
                            uint8_t MaxGroundHits,
                            uint16_t AirWeapon,
                            uint8_t MaxAirHits,
                            const char* Label)
    {
        const auto* Unit = Loader.FindUnitByID(ChkUnitID);
        const std::string EntryMessage = std::string(Label) + " units.dat entry";
        Require(Unit != nullptr, EntryMessage.c_str());

        const std::string BalanceMessage = std::string(Label) + " units.dat balance sentinel";
        Require(Unit->HitPoints == HitPoints &&
                Unit->ShieldAmount == ShieldAmount &&
                Unit->Armor == Armor &&
                Unit->ArmorUpgrade == ArmorUpgrade &&
                Unit->UnitSize == UnitSize &&
                Unit->GroundWeapon == GroundWeapon &&
                Unit->MaxGroundHits == MaxGroundHits &&
                Unit->AirWeapon == AirWeapon &&
                Unit->MaxAirHits == MaxAirHits,
            BalanceMessage.c_str());
    }

    void RequireWeaponBalance(const Resource::GameDataLoader& Loader,
                              uint32_t WeaponID,
                              uint16_t DamageAmount,
                              uint16_t DamageBonus,
                              uint16_t DamageFactor,
                              uint8_t DamageUpgrade,
                              uint8_t DamageType,
                              uint16_t WeaponBehavior,
                              uint16_t SplashType,
                              uint16_t InnerSplashRadius,
                              uint16_t MediumSplashRadius,
                              uint16_t OuterSplashRadius,
                              const char* Label)
    {
        const auto* Weapon = Loader.FindWeaponByID(WeaponID);
        const std::string EntryMessage = std::string(Label) + " weapons.dat entry";
        Require(Weapon != nullptr, EntryMessage.c_str());

        const std::string BalanceMessage = std::string(Label) + " weapons.dat balance sentinel";
        Require(Weapon->DamageAmount == DamageAmount &&
                Weapon->DamageBonus == DamageBonus &&
                Weapon->DamageFactor == DamageFactor &&
                Weapon->BulletCount == DamageFactor &&
                Weapon->DamageUpgrade == DamageUpgrade &&
                Weapon->DamageType == DamageType &&
                Weapon->WeaponBehavior == WeaponBehavior &&
                Weapon->SplashType == SplashType &&
                Weapon->InnerSplashRadius == InnerSplashRadius &&
                Weapon->MediumSplashRadius == MediumSplashRadius &&
                Weapon->OuterSplashRadius == OuterSplashRadius,
            BalanceMessage.c_str());
    }

    std::optional<std::vector<uint8_t>> TryReadAll(const std::string& Path)
    {
        std::ifstream File(Path, std::ios::binary);
        if (!File.good())
            return std::nullopt;

        return std::vector<uint8_t>(
            std::istreambuf_iterator<char>(File),
            std::istreambuf_iterator<char>());
    }

    std::vector<uint8_t> ReadSpriteAsset(std::string Path)
    {
        std::replace(Path.begin(), Path.end(), '\\', '/');
        if (auto Data = TryReadAll("assets/" + Path))
            return *Data;
        if (auto Data = TryReadAll("assets/unit/" + Path))
            return *Data;
        if (auto Data = TryReadAll("assets/arr/" + Path))
            return *Data;
        if (auto Data = TryReadAll("assets/data/" + Path))
            return *Data;

        Require(false, ("open sprite asset " + Path).c_str());
        return {};
    }

    bool HasTeamColorPixels(const Resource::GrpSprite& Sprite)
    {
        for (const auto& Frame : Sprite.Frames)
        {
            const int PixelCount = Frame.Width * Frame.Height;
            for (int P = 0; P < PixelCount; ++P)
            {
                const uint8_t Index = Sprite.RawFrameData[(size_t)Frame.Offset + (size_t)P];
                if (Index >= 8 && Index < 16)
                    return true;
            }
        }
        return false;
    }

    void RequireUnitMainImage(Resource::GameDataLoader& Loader,
                              uint32_t ChkUnitID,
                              const char* Name)
    {
        const uint32_t ImageID = Loader.ResolveUnitImageID(ChkUnitID);
        Require(ImageID != UINT32_MAX, Name);

        const auto* Relation = Loader.FindImageRelationByID(ImageID);
        Require(!Relation || !Relation->IsShadow(),
            "unit main image must not resolve to a shadow relation");

        const std::string Path = Loader.ResolveImageGrpPath(ImageID);
        Require(!Path.empty(), "unit main image resolves to a GRP path");
    }

    void RequireUnitSubunitImage(Resource::GameDataLoader& Loader,
                                 uint32_t ChkUnitID,
                                 const char* Name)
    {
        const uint32_t ImageID = Loader.ResolveUnitSubunitImageID(ChkUnitID);
        Require(ImageID != UINT32_MAX, Name);
        Require(!Loader.ResolveImageGrpPath(ImageID).empty(),
            "unit subunit image resolves to a GRP path");
    }

    void RequireUnitConstructionImage(Resource::GameDataLoader& Loader,
                                      uint32_t ChkUnitID,
                                      uint32_t ExpectedImageID,
                                      const char* Name)
    {
        const uint32_t ImageID = Loader.ResolveUnitConstructionImageID(ChkUnitID);
        Require(ImageID == ExpectedImageID, Name);

        const auto* Relation = Loader.FindImageRelationByID(ImageID);
        Require(!Relation || !Relation->IsShadow(),
            "unit construction image must not resolve to a shadow relation");

        const std::string Path = Loader.ResolveImageGrpPath(ImageID);
        Require(!Path.empty(), "unit construction image resolves to a GRP path");

        auto Sprite = Resource::GrpDecoder::DecodeGrp(ReadSpriteAsset(Path));
        Require(Sprite.has_value() && !Sprite->Frames.empty(),
            "unit construction GRP decodes to visible frames");
    }

    void RequireUnitTeamColorMaskSource(Resource::GameDataLoader& Loader,
                                        uint32_t ChkUnitID,
                                        const char* Name)
    {
        const uint32_t ImageID = Loader.ResolveUnitImageID(ChkUnitID);
        Require(ImageID != UINT32_MAX, Name);

        const std::string Path = Loader.ResolveImageGrpPath(ImageID);
        Require(!Path.empty(), "team-color unit image resolves to a GRP path");

        auto Sprite = Resource::GrpDecoder::DecodeGrp(ReadSpriteAsset(Path));
        Require(Sprite.has_value(), "team-color unit GRP decodes");
        Require(HasTeamColorPixels(*Sprite), "team-color unit GRP contains palette remap indices");
    }

    void RequireUnitIScriptAnimations(Resource::GameDataLoader& Loader,
                                      const Game::IScriptDatabase& Scripts,
                                      uint32_t ChkUnitID,
                                      std::initializer_list<int> AnimationIDs,
                                      const char* Name)
    {
        const uint32_t ImageID = Loader.ResolveUnitImageID(ChkUnitID);
        Require(ImageID != UINT32_MAX, Name);

        const auto* ImageInfo = Scripts.FindImageInfo((int)ImageID);
        Require(ImageInfo && ImageInfo->IScriptID >= 0,
            "unit main image resolves to an iscript entry");
        const auto* Script = ImageInfo ? Scripts.FindScript(ImageInfo->IScriptID) : nullptr;
        Require(Script, "unit main image resolves to an iscript program");

        for (int AnimationID : AnimationIDs)
        {
            if (AnimationID < 0 ||
                AnimationID >= (int)Script->AnimationPC.size() ||
                Script->AnimationPC[(size_t)AnimationID] <= 0)
            {
                std::fprintf(stderr,
                    "Missing project iscript animation: chk=%u image=%u script=%d anim=%d\n",
                    ChkUnitID, ImageID, ImageInfo->IScriptID, AnimationID);
            }
            Require(AnimationID >= 0 &&
                    AnimationID < (int)Script->AnimationPC.size() &&
                    Script->AnimationPC[(size_t)AnimationID] > 0,
                Name);
        }
    }

    void RequireUnitLacksIScriptAnimation(Resource::GameDataLoader& Loader,
                                          const Game::IScriptDatabase& Scripts,
                                          uint32_t ChkUnitID,
                                          int AnimationID,
                                          const char* Name)
    {
        const uint32_t ImageID = Loader.ResolveUnitImageID(ChkUnitID);
        const auto* ImageInfo = Scripts.FindImageInfo((int)ImageID);
        const auto* Script = ImageInfo && ImageInfo->IScriptID >= 0
            ? Scripts.FindScript(ImageInfo->IScriptID)
            : nullptr;
        Require(Script, "unit main image resolves to an iscript program");
        Require(AnimationID < 0 ||
                AnimationID >= (int)Script->AnimationPC.size() ||
                Script->AnimationPC[(size_t)AnimationID] <= 0,
            Name);
    }

    void RequireUnitIScriptOrderSignal(Resource::GameDataLoader& Loader,
                                       const Game::IScriptDatabase& Scripts,
                                       uint32_t ChkUnitID,
                                       int AnimationID,
                                       int SignalMask,
                                       const char* Name)
    {
        const uint32_t ImageID = Loader.ResolveUnitImageID(ChkUnitID);
        const auto* ImageInfo = Scripts.FindImageInfo((int)ImageID);
        const auto* Script = ImageInfo && ImageInfo->IScriptID >= 0
            ? Scripts.FindScript(ImageInfo->IScriptID)
            : nullptr;
        Require(Script && AnimationID >= 0 &&
                AnimationID < (int)Script->AnimationPC.size() &&
                Script->AnimationPC[(size_t)AnimationID] > 0,
            Name);

        Game::IScriptVM VM;
        VM.LoadScript(AnimationID,
                      &Scripts.GetProgram().ProgramData,
                      Script->AnimationPC[(size_t)AnimationID]);
        VM.GetContext().ImageID = (int)ImageID;
        bool FoundSignal = false;
        for (int Step = 0; Step < 512 && VM.IsRunning() && !FoundSignal; ++Step)
        {
            VM.ExecuteFrame();
            for (const auto& Event : VM.ConsumeEvents())
            {
                if (Event.Type == Game::IScriptEventType::OrderSignal &&
                    (Event.A & SignalMask) != 0)
                {
                    FoundSignal = true;
                    break;
                }
            }
        }
        if (!FoundSignal)
        {
            std::fprintf(stderr,
                "Missing project iscript order signal: chk=%u image=%u script=%d anim=%d mask=0x%x\n",
                ChkUnitID, ImageID, ImageInfo ? ImageInfo->IScriptID : -1,
                AnimationID, SignalMask);
        }
        Require(FoundSignal, Name);
    }

    void RequireWeaponIScriptAnimations(Resource::GameDataLoader& Loader,
                                        const Game::IScriptDatabase& Scripts,
                                        uint32_t ChkUnitID,
                                        bool TargetIsAir,
                                        std::initializer_list<int> AnimationIDs,
                                        const char* Name)
    {
        const auto* Unit = Loader.FindUnitByID(ChkUnitID);
        Require(Unit, Name);
        const uint32_t WeaponID = TargetIsAir ? Unit->AirWeapon : Unit->GroundWeapon;
        const auto* Weapon = Unit ? Loader.FindWeaponByID(WeaponID) : nullptr;
        const auto* Flingy = Weapon ? Loader.FindFlingyByID(Weapon->FlingyID) : nullptr;
        const auto* Sprite = Flingy ? Loader.FindSpriteByID(Flingy->SpriteID) : nullptr;
        const auto* ImageInfo = Sprite ? Scripts.FindImageInfo((int)Sprite->ImageID) : nullptr;
        const auto* Script = ImageInfo && ImageInfo->IScriptID >= 0
            ? Scripts.FindScript(ImageInfo->IScriptID)
            : nullptr;
        Require(Script, Name);
        for (int AnimationID : AnimationIDs)
        {
            Require(AnimationID >= 0 &&
                    AnimationID < (int)Script->AnimationPC.size() &&
                    Script->AnimationPC[(size_t)AnimationID] > 0,
                Name);
        }
    }

    void RequireSelectionCircleImage(Resource::GameDataLoader& Loader,
                                     uint32_t CircleIndex,
                                     int ExpectedPixels)
    {
        constexpr uint32_t SelectionCircleBaseImageId = 561;
        const uint32_t ImageID = SelectionCircleBaseImageId + CircleIndex;
        const auto* Relation = Loader.FindImageRelationByID(ImageID);
        Require(!Relation || !Relation->IsShadow(),
            "selection circle image must not resolve to a shadow relation");

        const std::string Path = Loader.ResolveImageGrpPath(ImageID);
        Require(!Path.empty(), "selection circle image resolves to a GRP path");
        Require(ExpectedPixels > 0, "selection circle expected size is valid");
    }
}

int main()
{
    Resource::GameDataLoader Loader;
    const auto UnitsDat = ReadAll("assets/arr/units.dat");
    const auto WeaponsDat = ReadAll("assets/arr/weapons.dat");
    Require(UnitsDat.size() == 19876,
        "project units.dat has the complete Brood War column layout");
    Require(WeaponsDat.size() == 5460,
        "project weapons.dat has the complete Brood War column layout");
    Require(Loader.LoadUnitDataRaw(UnitsDat),
        "load units.dat");
    Require(Loader.LoadWeaponDataRaw(WeaponsDat),
        "load weapons.dat");
    Require(Loader.LoadFlingyDataRaw(ReadAll("assets/arr/flingy.dat")),
        "load flingy.dat");
    Require(Loader.LoadSpriteDataRaw(ReadAll("assets/arr/sprites.dat")),
        "load sprites.dat");
    Require(Loader.LoadImageDataRaw(ReadAll("assets/arr/images.dat")),
        "load images.dat");
    Require(Loader.LoadImageTblDataRaw(ReadAll("assets/arr/images.tbl")),
        "load images.tbl");
    Require(Loader.LoadImageRelationDataRaw(ReadAll("assets/images.rel")),
        "load images.rel");

    RequireUnitBalance(Loader, 0, 40, 0, 0, 0, 1, 0, 1, 0, 1, "Marine");
    RequireUnitBalance(Loader, 3, 125, 0, 1, 1, 3, 130, 0, 130, 0, "Goliath");
    const auto* Goliath = Loader.FindUnitByID(3);
    const auto* GoliathTurret = Goliath ? Loader.FindUnitByID(Goliath->Subunit1ID) : nullptr;
    Require(Goliath && Goliath->Subunit1ID == 4 && GoliathTurret &&
            GoliathTurret->GroundWeapon == 7 && GoliathTurret->MaxGroundHits == 1 &&
            GoliathTurret->AirWeapon == 8 && GoliathTurret->MaxAirHits == 1,
        "Goliath resolves ground and air weapons through subunit 4");
    RequireUnitBalance(Loader, 58, 200, 0, 2, 2, 3, 130, 0, 103, 4, "Valkyrie");
    RequireUnitBalance(Loader, 65, 100, 60, 1, 5, 1, 64, 2, 130, 0, "Zealot");
    RequireUnitBalance(Loader, 103, 125, 0, 1, 3, 2, 109, 1, 130, 0, "Lurker");
    RequireUnitBalance(Loader, 72, 300, 150, 4, 6, 3, 130, 0, 130, 0, "Carrier");
    RequireUnitBalance(Loader, 73, 40, 40, 0, 6, 1, 79, 1, 79, 1, "Interceptor");
    RequireUnitBalance(Loader, 83, 100, 80, 0, 5, 3, 130, 0, 130, 0, "Reaver");
    RequireUnitBalance(Loader, 85, 20, 10, 0, 5, 1, 82, 1, 130, 0, "Scarab");
    RequireUnitBalance(Loader, 50, 60, 0, 0, 3, 1, 54, 1, 130, 0, "Infested Terran");
    RequireUnitBalance(Loader, 62, 250, 0, 2, 4, 3, 130, 0, 104, 1, "Devourer");
    RequireUnitBalance(Loader, 131, 1250, 0, 1, 60, 3, 130, 0, 130, 0, "Hatchery");
    RequireUnitBalance(Loader, 133, 2500, 0, 1, 60, 3, 130, 0, 130, 0, "Hive");
    RequireUnitBalance(Loader, 154, 750, 750, 1, 60, 3, 130, 0, 130, 0, "Nexus");
    RequireUnitBalance(Loader, 146, 300, 0, 2, 60, 3, 53, 1, 130, 0, "Sunken Colony");

    RequireWeaponBalance(Loader, 0, 6, 1, 1, 7, 3, 2, 1, 0, 0, 0,
        "Gauss Rifle");
    RequireWeaponBalance(Loader, 7, 12, 1, 1, 8, 3, 2, 1, 0, 0, 0,
        "Goliath Twin Autocannons");
    RequireWeaponBalance(Loader, 8, 10, 2, 2, 8, 1, 1, 1, 0, 0, 0,
        "Goliath Hellfire Missiles");
    RequireWeaponBalance(Loader, 103, 6, 1, 2, 9, 1, 8, 24, 5, 50, 100,
        "Valkyrie Halo Rockets");
    RequireWeaponBalance(Loader, 64, 8, 1, 1, 13, 3, 5, 1, 0, 0, 0,
        "Zealot Psi Blades");
    RequireWeaponBalance(Loader, 109, 20, 2, 1, 11, 3, 9, 3, 20, 20, 20,
        "Lurker Subterranean Spines");
    RequireWeaponBalance(Loader, 79, 6, 1, 1, 14, 3, 0, 1, 0, 0, 0,
        "Interceptor Pulse Cannon");
    RequireWeaponBalance(Loader, 82, 100, 25, 1, 35, 3, 5, 3, 20, 40, 60,
        "Reaver Scarab");
    RequireWeaponBalance(Loader, 54, 500, 0, 1, 60, 1, 5, 2, 20, 40, 60,
        "Infested Terran Suicide");
    RequireWeaponBalance(Loader, 104, 25, 2, 1, 12, 1, 0, 18, 0, 0, 0,
        "Devourer Corrosive Acid");
    RequireWeaponBalance(Loader, 53, 40, 0, 1, 60, 1, 2, 1, 0, 0, 0,
        "Sunken Colony Tentacle");
    Require(Loader.FindWeaponByID(130) == nullptr,
        "weapon id 130 remains the units.dat no-weapon sentinel");

    Game::IScriptDatabase Scripts;
    Require(Scripts.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
        "load project iscript data");

    const std::array<uint32_t, 16> ProtossBuildings = {{
        154, 156, 157, 160, 166, 162, 164, 172,
        155, 167, 163, 165, 171, 169, 170, 159
    }};
    for (uint32_t ChkUnitID : ProtossBuildings)
    {
        RequireUnitIScriptAnimations(
            Loader, Scripts, ChkUnitID,
            {Game::IScriptAnims::Anim_Init, Game::IScriptAnims::Anim_WarpIn},
            "Protoss building exposes Init and WarpIn construction animations");
    }

    constexpr uint32_t ProtossWarpAnchorImage = 211;
    Require(Loader.ResolveImageGrpPath(ProtossWarpAnchorImage) ==
            "protoss/pb1Glow.grp",
        "Protoss construction begins with the canonical Warp Anchor image");
    const auto* WarpAnchorInfo =
        Scripts.FindImageInfo((int)ProtossWarpAnchorImage);
    const auto* WarpAnchorScript =
        WarpAnchorInfo && WarpAnchorInfo->IScriptID >= 0
            ? Scripts.FindScript(WarpAnchorInfo->IScriptID)
            : nullptr;
    Require(WarpAnchorScript &&
            Game::IScriptAnims::Anim_Init <
                (int)WarpAnchorScript->AnimationPC.size() &&
            WarpAnchorScript->AnimationPC[
                (size_t)Game::IScriptAnims::Anim_Init] > 0 &&
            Game::IScriptAnims::Anim_SpecialState1 <
                (int)WarpAnchorScript->AnimationPC.size() &&
            WarpAnchorScript->AnimationPC[
                (size_t)Game::IScriptAnims::Anim_SpecialState1] > 0,
        "Protoss Warp Anchor exposes Init and closing animations");

    const auto RiftInit = ProbeAnimation(
        Scripts, ProtossWarpAnchorImage, Game::IScriptAnims::Anim_Init);
    Require(RiftInit.Present,
        "Protoss Warp Anchor Init is playable");
    Game::IScriptVM WarpAnchorClosingVM;
    WarpAnchorClosingVM.LoadScript(
        Game::IScriptAnims::Anim_SpecialState1,
        &Scripts.GetProgram().ProgramData,
        WarpAnchorScript->AnimationPC[
            (size_t)Game::IScriptAnims::Anim_SpecialState1]);
    bool WarpAnchorClosingSignaled = false;
    for (int Step = 0;
         Step < 512 && WarpAnchorClosingVM.IsRunning() &&
         !WarpAnchorClosingSignaled;
         ++Step)
    {
        WarpAnchorClosingVM.ExecuteFrame();
        for (const auto& Event : WarpAnchorClosingVM.ConsumeEvents())
            if (Event.Type == Game::IScriptEventType::OrderSignal &&
                (Event.A & 1) != 0)
                WarpAnchorClosingSignaled = true;
    }
    Require(WarpAnchorClosingSignaled,
        "Protoss Warp Anchor signals the materialization transition");

    const auto* MaterializationScript = Scripts.FindScript(193);
    Require(MaterializationScript &&
            Game::IScriptAnims::Anim_Init <
                (int)MaterializationScript->AnimationPC.size() &&
            MaterializationScript->AnimationPC[
                (size_t)Game::IScriptAnims::Anim_Init] > 0,
        "Protoss materialization script 193 exposes Init");
    Game::IScriptVM MaterializationVM;
    MaterializationVM.LoadScript(
        Game::IScriptAnims::Anim_Init,
        &Scripts.GetProgram().ProgramData,
        MaterializationScript->AnimationPC[
            (size_t)Game::IScriptAnims::Anim_Init]);
    std::vector<int> MaterializationFrames;
    bool MaterializationSignaled = false;
    for (int Step = 0;
         Step < 1024 && MaterializationVM.IsRunning() && !MaterializationSignaled;
         ++Step)
    {
        MaterializationVM.ExecuteFrame();
        for (const auto& Event : MaterializationVM.ConsumeEvents())
        {
            if (Event.Type == Game::IScriptEventType::WarpOverlay)
                MaterializationFrames.push_back(Event.A);
            if (Event.Type == Game::IScriptEventType::OrderSignal &&
                (Event.A & 1) != 0)
                MaterializationSignaled = true;
        }
    }
    Require(MaterializationFrames.size() > 8 && MaterializationSignaled,
        "script 193 advances texture.grp frames before signaling WarpIn");
    Require(std::is_sorted(MaterializationFrames.begin(), MaterializationFrames.end()),
        "script 193 materialization frames advance in authored order");

    uint32_t WarpTextureImageID = UINT32_MAX;
    for (uint32_t ImageID = 0; ImageID < 999; ++ImageID)
    {
        std::string Path = Loader.ResolveImageGrpPath(ImageID);
        std::replace(Path.begin(), Path.end(), '\\', '/');
        std::transform(Path.begin(), Path.end(), Path.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        if (Path == "protoss/texture.grp")
        {
            WarpTextureImageID = ImageID;
            break;
        }
    }
    Require(WarpTextureImageID != UINT32_MAX,
        "Protoss construction resolves the original warp texture image");
    const auto WarpTexture = Resource::GrpDecoder::DecodeGrp(
        ReadSpriteAsset(Loader.ResolveImageGrpPath(WarpTextureImageID)));
    Require(WarpTexture && WarpTexture->Frames.size() > 8,
        "Protoss warp texture provides an animated frame sequence");
    std::vector<uint8_t> PreviousWarpFrame;
    int ChangingFrames = 0;
    for (const auto& Frame : WarpTexture->Frames)
    {
        const int Pixels = Frame.Width * Frame.Height;
        Require(Frame.Width == WarpTexture->Width &&
                Frame.Height == WarpTexture->Height,
            "Protoss warp texture stores full-size replacement frames");
        std::vector<uint8_t> Current(
            WarpTexture->RawFrameData.begin() + Frame.Offset,
            WarpTexture->RawFrameData.begin() + Frame.Offset + Pixels);
        Require(std::count_if(Current.begin(), Current.end(),
                    [](uint8_t Index) { return Index != 0; }) == Pixels,
            "Protoss warp texture frame covers the full modifier surface");
        if (!PreviousWarpFrame.empty() && Current != PreviousWarpFrame)
            ++ChangingFrames;
        PreviousWarpFrame = std::move(Current);
    }
    Require(ChangingFrames > 8,
        "Protoss warp texture continuously rewrites the construction surface");

    const uint32_t CommandCenterMainImage = Loader.ResolveUnitImageID(106);
    const uint32_t SupplyDepotMainImage = Loader.ResolveUnitImageID(109);
    Require(CommandCenterMainImage == 275u && SupplyDepotMainImage == 278u,
        "Terran Command Center and Supply Depot resolve canonical main images");
    const auto CommandCenterBuilt = ProbeAnimation(
        Scripts, CommandCenterMainImage, Game::IScriptAnims::Anim_Built);
    const auto CommandCenterWorking = ProbeAnimation(
        Scripts, CommandCenterMainImage, Game::IScriptAnims::Anim_IsWorking);
    const auto SupplyDepotBuilt = ProbeAnimation(
        Scripts, SupplyDepotMainImage, Game::IScriptAnims::Anim_Built);
    const auto SupplyDepotWorking = ProbeAnimation(
        Scripts, SupplyDepotMainImage, Game::IScriptAnims::Anim_IsWorking);
    Require(CommandCenterBuilt.StillRunning && SupplyDepotBuilt.StillRunning,
        "Terran Built scripts are looping states and cannot block production state selection");
    Require(CommandCenterWorking.Present &&
            CommandCenterWorking.FirstVisualImage == 276,
        "Command Center IsWorking creates the control-panel image");
    Require(SupplyDepotWorking.Present && SupplyDepotWorking.VisualEvents == 0,
        "Supply Depot IsWorking is a real but visually empty script entry");
    const auto SupplyDepotMachinery = ProbeAnimation(
        Scripts, 279, Game::IScriptAnims::Anim_Init);
    Require(SupplyDepotBuilt.FirstVisualImage == 279 &&
            SupplyDepotMachinery.FrameChanges > 1,
        "Supply Depot Built child owns the animated completed machinery frames");
    const uint32_t BarracksMainImage = Loader.ResolveUnitImageID(111);
    Require(BarracksMainImage == 266u &&
            ProbeAnimation(Scripts, BarracksMainImage,
                Game::IScriptAnims::Anim_IsWorking).FrameChanges > 8,
        "Barracks production uses the canonical animated IsWorking root state");
    const auto EngineeringBuilt = ProbeAnimation(
        Scripts, Loader.ResolveUnitImageID(122), Game::IScriptAnims::Anim_Built);
    const auto EngineeringMachineryWorking = ProbeAnimation(
        Scripts, 323, Game::IScriptAnims::Anim_IsWorking);
    Require(EngineeringBuilt.FirstVisualImage == 323 &&
            EngineeringMachineryWorking.FrameChanges > 8,
        "Engineering Bay Built child exposes its animated research state");
    for (uint32_t ConnectorImage : {272u, 313u, 282u, 289u, 302u, 294u})
    {
        Require(ProbeAnimation(Scripts, ConnectorImage,
                    Game::IScriptAnims::Anim_Init).Present &&
                ProbeAnimation(Scripts, ConnectorImage,
                    Game::IScriptAnims::Anim_LiftOff).Present &&
                ProbeAnimation(Scripts, ConnectorImage,
                    Game::IScriptAnims::Anim_Landing).Present,
            "Terran add-on bridge exposes independent Init/LiftOff/Landing states");
    }

    struct TerranBuildingAuditEntry
    {
        uint32_t ChkUnitID;
        const char* Name;
    };
    const std::array<TerranBuildingAuditEntry, 18> TerranBuildings = {{
        {106, "CommandCenter"}, {107, "ComsatStation"},
        {108, "NuclearSilo"}, {109, "SupplyDepot"},
        {110, "Refinery"}, {111, "Barracks"}, {112, "Academy"},
        {113, "Factory"}, {114, "Starport"}, {115, "ControlTower"},
        {116, "ScienceFacility"}, {117, "CovertOps"},
        {118, "PhysicsLab"}, {120, "MachineShop"},
        {122, "EngineeringBay"}, {123, "Armory"},
        {124, "MissileTurret"}, {125, "Bunker"}
    }};
    const std::array<int, 11> AuditedAnimations = {{
        Game::IScriptAnims::Anim_Init,
        Game::IScriptAnims::Anim_SpecialState1,
        Game::IScriptAnims::Anim_SpecialState2,
        Game::IScriptAnims::Anim_AlmostBuilt,
        Game::IScriptAnims::Anim_Built,
        Game::IScriptAnims::Anim_Landing,
        Game::IScriptAnims::Anim_LiftOff,
        Game::IScriptAnims::Anim_IsWorking,
        Game::IScriptAnims::Anim_WorkingToIdle,
        Game::IScriptAnims::Anim_Disable,
        Game::IScriptAnims::Anim_Enable
    }};
    for (const auto& Building : TerranBuildings)
    {
        const uint32_t MainImage = Loader.ResolveUnitImageID(Building.ChkUnitID);
        const uint32_t ConstructionImage =
            Loader.ResolveUnitConstructionImageID(Building.ChkUnitID);
        Require(MainImage != UINT32_MAX,
            (std::string(Building.Name) + " resolves a main image").c_str());
        Require(!Loader.ResolveImageGrpPath(MainImage).empty(),
            (std::string(Building.Name) + " resolves a main GRP").c_str());
        Require(ConstructionImage != UINT32_MAX,
            (std::string(Building.Name) + " resolves construction image metadata").c_str());
        Require(ProbeAnimation(Scripts, MainImage,
                    Game::IScriptAnims::Anim_Init).Present,
            (std::string(Building.Name) + " exposes Init").c_str());
        Require(ProbeAnimation(Scripts, MainImage,
                    Game::IScriptAnims::Anim_AlmostBuilt).Present,
            (std::string(Building.Name) + " exposes AlmostBuilt").c_str());
        for (int AnimationID : AuditedAnimations)
        {
            const auto Probe = ProbeAnimation(Scripts, MainImage, AnimationID);
            for (int VisualImage : Probe.VisualImages)
            {
                const uint32_t ChildImage = (uint32_t)VisualImage;
                Require(!Loader.ResolveImageGrpPath(ChildImage).empty(),
                    (std::string(Building.Name) + " child image resolves a GRP").c_str());
                Require(ProbeAnimation(Scripts, ChildImage,
                            Game::IScriptAnims::Anim_Init).Present,
                    (std::string(Building.Name) + " child image exposes Init").c_str());
            }
        }
    }
    const std::array<uint32_t, 37> TerranBuildingChildren = {{
        276, 277, 272, 273, 274, 313, 314, 315, 279, 280, 308,
        267, 264, 265, 286, 287, 320, 321, 282, 283, 284, 310,
        311, 289, 290, 291, 302, 303, 294, 295, 323, 324, 269,
        270, 297, 298, 305
    }};
    for (uint32_t ImageID : TerranBuildingChildren)
    {
        Require(!Loader.ResolveImageGrpPath(ImageID).empty(),
            "Terran building child image resolves a GRP");
        Require(ProbeAnimation(Scripts, ImageID,
                    Game::IScriptAnims::Anim_Init).Present,
            "Terran building child image exposes Init");
    }

    const auto Flingy = Resource::DatDecoder::DecodeFlingy(ReadAll("assets/arr/flingy.dat"));
    Require(Flingy.size() == 209, "flingy.dat decodes all entries");
    Require(Flingy[0].SpriteID == 130 &&
            Flingy[0].TopSpeed == 1707 &&
            Flingy[0].Acceleration == 107 &&
            Flingy[0].HaltDistance == 13616 &&
            Flingy[0].TurnRate == 40 &&
            Flingy[0].Unused == 30 &&
            Flingy[0].MovementType == 0,
        "flingy.dat column-major movement fields match the Brood War asset");

    const uint32_t GeyserImageID = Loader.ResolveUnitImageID(188);
    Require(GeyserImageID == 344, "Vespene geyser resolves to its main image");
    for (uint32_t CarryImageID : {397u, 399u, 401u, 403u})
    {
        Require(Loader.FindImageByID(CarryImageID) != nullptr,
            "carried mineral/gas image exists in images.dat");
        Require(!Loader.ResolveImageGrpPath(CarryImageID).empty(),
            "carried mineral/gas image resolves to a real GRP resource");
    }
    const uint32_t ScvImageID = Loader.ResolveUnitImageID(7);
    Require(ScvImageID == 247u, "SCV resolves to its canonical main image");
    const auto* MineralChunkImage = Loader.FindImageByID(397u);
    Require(MineralChunkImage && MineralChunkImage->HasDirectionalFrames,
        "carried mineral chunk exposes directional frames");
    const std::string ScvSpecialLoPath =
        Loader.ResolveImageOverlayPath(ScvImageID, 2);
    Require(!ScvSpecialLoPath.empty(),
        "SCV main image exposes the special LO table used by carried resources");
    const auto ScvCarryOffsets = Resource::DatDecoder::DecodeImageOffsets(
        ReadSpriteAsset(ScvSpecialLoPath));
    Require(ScvCarryOffsets && ScvCarryOffsets->Frames.size() >= 17,
        "SCV carried-resource LO table covers directional main frames");
    const size_t ScvCarryUsableFrames = (size_t)std::count_if(
        ScvCarryOffsets->Frames.begin(), ScvCarryOffsets->Frames.end(),
        [](const std::vector<Resource::ImageOffset>& Frame) {
            return !Frame.empty() &&
                (Frame[0].X != 127 || Frame[0].Y != 127);
        });
    Require(ScvCarryUsableFrames >= 17,
        "SCV special LO slot 0 provides real two-arm carry positions across directions");
    const std::string GeyserLoPath = Loader.ResolveImageOverlayPath(GeyserImageID, 2);
    Require(GeyserLoPath == "neutral/geyser.los",
        "geyser special overlay resolves through images.dat and images.tbl");
    const auto GeyserOffsets = Resource::DatDecoder::DecodeImageOffsets(
        ReadAll("assets/unit/" + GeyserLoPath));
    Require(GeyserOffsets && GeyserOffsets->Frames.size() == 8 &&
            GeyserOffsets->Frames[0].size() == 5,
        "geyser special overlay decodes eight frames with five smoke slots");
    Require(GeyserOffsets->Frames[0][0].X == 11 &&
            GeyserOffsets->Frames[0][0].Y == -19 &&
            GeyserOffsets->Frames[0][2].X == 34 &&
            GeyserOffsets->Frames[0][2].Y == 2,
        "geyser LO coordinates preserve signed byte offsets");

    const uint32_t BattlecruiserImageID = Loader.ResolveUnitImageID(12);
    Require(BattlecruiserImageID == 218,
        "Battlecruiser resolves to the canonical main image");
    const auto* BattlecruiserImage = Loader.FindImageByID(BattlecruiserImageID);
    const auto* BattlecruiserShadow = Loader.FindImageByID(219);
    const auto* BattlecruiserShadowRelation = Loader.FindImageRelationByID(219);
    Require(BattlecruiserImage && BattlecruiserImage->HasDirectionalFrames &&
            BattlecruiserShadow && BattlecruiserShadow->HasDirectionalFrames,
        "Battlecruiser main image and shadow both use directional GRP frames");
    Require(BattlecruiserShadowRelation && BattlecruiserShadowRelation->IsShadow() &&
            BattlecruiserShadowRelation->ParentImageID == (int32_t)BattlecruiserImageID,
        "images.rel links the real Battlecruiser shadow to its main image");
    const std::string BattlecruiserSpecialLoPath =
        Loader.ResolveImageOverlayPath(BattlecruiserImageID, 2);
    Require(BattlecruiserSpecialLoPath == "terran/BattleCr.lol",
        "Battlecruiser main image resolves its special overlay LO table");
    const auto BattlecruiserSpecialOffsets = Resource::DatDecoder::DecodeImageOffsets(
        ReadAll("assets/unit/" + BattlecruiserSpecialLoPath));
    Require(BattlecruiserSpecialOffsets &&
            !BattlecruiserSpecialOffsets->Frames.empty() &&
            !BattlecruiserSpecialOffsets->Frames.front().empty(),
        "real BattleCr.lol offsets decode for directional flight overlays");

    const auto* Marine = Loader.FindUnitByID(0);
    Require(Marine, "Marine units.dat entry");
    Require(Marine->HitPoints == 40 &&
            Marine->ShieldAmount == 0 &&
            Marine->GroundWeapon == 0 &&
            Marine->AirWeapon == 0 &&
            Marine->SightRange == 7 &&
            Marine->MineralCost == 50 &&
            Marine->SupplyCost == 2,
        "Marine balance fields decode from column-major units.dat");
    Require(Marine->DimensionLeft == 8 && Marine->DimensionUp == 9 &&
            Marine->DimensionRight == 8 && Marine->DimensionDown == 10,
        "Marine dimensions decode to 17x20");
    Require(Marine->DimensionWidth() == 17 && Marine->DimensionHeight() == 20,
        "Marine derived dimensions match BW unit bounds");

    const auto* SCV = Loader.FindUnitByID(7);
    Require(SCV, "SCV units.dat entry");
    Require(SCV->HitPoints == 60 &&
            SCV->GroundWeapon == 13 &&
            SCV->AirWeapon == 130 &&
            SCV->SightRange == 7,
        "SCV balance fields decode from column-major units.dat");
    Require(SCV->DimensionWidth() == 23 && SCV->DimensionHeight() == 23,
        "SCV dimensions decode to 23x23");

    const auto* CommandCenter = Loader.FindUnitByID(106);
    Require(CommandCenter, "Command Center units.dat entry");
    Require(CommandCenter->HitPoints == 1500 &&
            CommandCenter->Armor == 1 &&
            CommandCenter->SightRange == 10,
        "Command Center balance fields decode from column-major units.dat");
    Require(CommandCenter->PlacementWidth == 128 &&
            CommandCenter->PlacementHeight == 96,
        "Command Center placement size decodes from the project units.dat table");
    Require(CommandCenter->DimensionLeft == 58 &&
            CommandCenter->DimensionUp == 41 &&
            CommandCenter->DimensionRight == 58 &&
            CommandCenter->DimensionDown == 41,
        "Command Center dimensions decode to 117x83");
    const uint32_t CommandCenterImageID = Loader.ResolveUnitImageID(106);
    const std::string CommandCenterDamageLoPath =
        Loader.ResolveImageOverlayPath(CommandCenterImageID, 1);
    Require(!CommandCenterDamageLoPath.empty(),
        "project Command Center image exposes a damage-overlay LO table");
    const auto CommandCenterDamageOffsets = Resource::DatDecoder::DecodeImageOffsets(
        ReadSpriteAsset(CommandCenterDamageLoPath));
    Require(CommandCenterDamageOffsets && !CommandCenterDamageOffsets->Frames.empty(),
        "project Command Center damage-overlay offsets decode");
    const auto& CommandCenterDamageFrame = CommandCenterDamageOffsets->Frames.front();
    const size_t CommandCenterDamageSlots = (size_t)std::count_if(
        CommandCenterDamageFrame.begin(), CommandCenterDamageFrame.end(),
        [](const Resource::ImageOffset& Offset) {
            return Offset.X != 127 || Offset.Y != 127;
        });
    Require(CommandCenterDamageSlots > 0 && CommandCenterDamageSlots <= 22,
        "project Command Center damage LO table contains usable flame slots");
    for (uint32_t DamageImageID : {450u, 472u})
    {
        Require(!Loader.ResolveImageGrpPath(DamageImageID).empty(),
            "project damage flame image resolves to a renderable GRP");
        const auto* DamageImageInfo = Scripts.FindImageInfo((int)DamageImageID);
        const auto* DamageScript = DamageImageInfo && DamageImageInfo->IScriptID >= 0
            ? Scripts.FindScript(DamageImageInfo->IScriptID)
            : nullptr;
        Require(DamageScript &&
                Game::IScriptAnims::Anim_Init < (int)DamageScript->AnimationPC.size() &&
                DamageScript->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init] > 0,
            "project damage flame image exposes its own Init animation");
    }
    const auto* ComsatStation = Loader.FindUnitByID(107);
    Require(ComsatStation &&
            ComsatStation->PlacementWidth == 64 &&
            ComsatStation->PlacementHeight == 64 &&
            ComsatStation->AddonPositionX == 128 &&
            ComsatStation->AddonPositionY == 32,
        "project units.dat exposes the Terran add-on placement offset");
    Require(-CommandCenter->PlacementWidth / 2 + ComsatStation->AddonPositionX +
                ComsatStation->PlacementWidth / 2 == 96 &&
            -CommandCenter->PlacementHeight / 2 + ComsatStation->AddonPositionY +
                ComsatStation->PlacementHeight / 2 == 16,
        "Command Center and Comsat resolve the authored +96,+16 center anchor");

    const auto* MarineSprite = Loader.FindSpriteByID(235);
    Require(MarineSprite, "Marine sprites.dat entry");
    Require(Loader.ResolveUnitSpriteID(0) == 235,
        "Marine unit resolves through flingy.dat to sprite 235");
    Require(MarineSprite->SelectionCircle == 0 &&
            MarineSprite->SelectionCircleVerticalOffset == 9,
        "Marine selection circle decode");

    const auto* CommandCenterSprite = Loader.FindSpriteByID(252);
    Require(CommandCenterSprite, "Command Center sprites.dat entry");
    Require(CommandCenterSprite->HealthBar == 110 &&
            CommandCenterSprite->SelectionCircle == 8 &&
            CommandCenterSprite->SelectionCircleVerticalOffset == 6,
        "Command Center sprite selection metadata decode");

    const auto* GaussRifle = Loader.FindWeaponByID(0);
    Require(GaussRifle, "Gauss Rifle weapons.dat entry");
    Require(GaussRifle->DamageAmount == 6 &&
            GaussRifle->DamageBonus == 1 &&
            GaussRifle->Cooldown == 15 &&
            GaussRifle->Range == 128 &&
            GaussRifle->DamageUpgrade == 7 &&
            GaussRifle->DamageType == 3 &&
            GaussRifle->WeaponBehavior == 2 &&
            GaussRifle->SplashType == 1 &&
            GaussRifle->DamageFactor == 1 &&
            GaussRifle->BulletCount == GaussRifle->DamageFactor &&
            GaussRifle->Lifetime == 255 &&
            GaussRifle->AttackAngle == 16 &&
            GaussRifle->BulletHeadingOffset == 0 &&
            GaussRifle->ForwardOffset == 0 &&
            GaussRifle->UpwardOffset == 0,
        "Gauss Rifle decodes from column-major weapons.dat");
    const auto* HellfireMissiles = Loader.FindWeaponByID(8);
    Require(HellfireMissiles && HellfireMissiles->WeaponBehavior == 1 &&
            HellfireMissiles->Lifetime == 30 &&
            HellfireMissiles->BulletCount == 2 &&
            HellfireMissiles->AttackAngle == 32 &&
            HellfireMissiles->BulletHeadingOffset == 64 &&
            HellfireMissiles->ForwardOffset == 5 &&
            HellfireMissiles->UpwardOffset == 0,
        "Goliath missiles preserve DAT tracking, lifetime, twin-shot, and launch offsets");
    Require(Goliath && Goliath->AirWeapon == 130 && GoliathTurret &&
            GoliathTurret->AirWeapon == 8 &&
            (int)GoliathTurret->MaxAirHits *
                    (int)HellfireMissiles->BulletCount == 2,
        "projectile resolver must follow Goliath body to turret and emit two missiles");
    const auto* Valkyrie = Loader.FindUnitByID(58);
    const auto* HaloRockets = Loader.FindWeaponByID(103);
    Require(Valkyrie && HaloRockets && Valkyrie->AirWeapon == 103 &&
            Valkyrie->MaxAirHits == 4 && HaloRockets->BulletCount == 2 &&
            (int)Valkyrie->MaxAirHits * (int)HaloRockets->BulletCount == 8,
        "Valkyrie DAT attack-event and bullet counts resolve to eight distinct rockets");
    const auto* GaussFlingy = Loader.FindFlingyByID(GaussRifle->FlingyID);
    Require(GaussFlingy, "Gauss Rifle resolves weapons.dat to flingy.dat");
    const auto* GaussSprite = GaussFlingy
        ? Loader.FindSpriteByID(GaussFlingy->SpriteID)
        : nullptr;
    Require(GaussSprite, "Gauss Rifle resolves flingy.dat to sprites.dat");
    const std::string GaussImagePath = GaussSprite
        ? Loader.ResolveImageGrpPath(GaussSprite->ImageID)
        : std::string{};
    Require(!GaussImagePath.empty(),
        "Gauss Rifle resolves sprites.dat through images.dat/images.tbl to a GRP path");
    const auto GaussFrames = Resource::GrpDecoder::DecodeGrp(ReadSpriteAsset(GaussImagePath));
    Require(GaussFrames && !GaussFrames->Frames.empty(),
        "Gauss Rifle projectile GRP decodes to renderable weapon frames");
    RequireWeaponIScriptAnimations(
        Loader, Scripts, 0, false,
        {Game::IScriptAnims::Anim_Init,
         Game::IScriptAnims::Anim_GndAttkInit,
         Game::IScriptAnims::Anim_Death},
        "project Gauss Rifle projectile exposes flight and impact animations");
    Require(ProbeAnimationSounds(
                Scripts, Loader.ResolveUnitImageID(0),
                Game::IScriptAnims::Anim_GndAttkInit) ==
            std::vector<int>{69} &&
            ProbeAnimationSounds(
                Scripts, Loader.ResolveUnitImageID(0),
                Game::IScriptAnims::Anim_AirAttkInit) ==
            std::vector<int>{69},
        "Marine ground and air attack scripts emit the authored sound 69");
    Require(ProbeAnimationSounds(
                Scripts, GaussSprite->ImageID,
                Game::IScriptAnims::Anim_Init).empty(),
        "Marine sound belongs to the unit firing frame, not its impact projectile");

    const auto* Dragoon = Loader.FindUnitByID(66);
    const auto* DragoonWeapon = Dragoon
        ? Loader.FindWeaponByID(Dragoon->GroundWeapon)
        : nullptr;
    const auto* DragoonFlingy = DragoonWeapon
        ? Loader.FindFlingyByID(DragoonWeapon->FlingyID)
        : nullptr;
    const auto* DragoonProjectileSprite = DragoonFlingy
        ? Loader.FindSpriteByID(DragoonFlingy->SpriteID)
        : nullptr;
    Require(Dragoon && DragoonWeapon && DragoonFlingy && DragoonProjectileSprite,
        "Dragoon Phase Disruptor resolves to a projectile image");
    Require(ProbeAnimationSounds(
                Scripts, DragoonProjectileSprite->ImageID,
                Game::IScriptAnims::Anim_Init) == std::vector<int>{99},
        "Dragoon projectile Init emits the authored launch sound 99");
    Require(ProbeAnimationSounds(
                Scripts, Loader.ResolveUnitImageID(66),
                Game::IScriptAnims::Anim_GndAttkInit).empty(),
        "Dragoon launch sound belongs to the projectile Init, not the unit body");

    const auto* Hydralisk = Loader.FindUnitByID(38);
    const auto* HydraliskWeapon = Hydralisk
        ? Loader.FindWeaponByID(Hydralisk->GroundWeapon)
        : nullptr;
    const auto* HydraliskFlingy = HydraliskWeapon
        ? Loader.FindFlingyByID(HydraliskWeapon->FlingyID)
        : nullptr;
    const auto* HydraliskProjectileSprite = HydraliskFlingy
        ? Loader.FindSpriteByID(HydraliskFlingy->SpriteID)
        : nullptr;
    Require(Hydralisk && HydraliskWeapon && HydraliskFlingy &&
            HydraliskProjectileSprite,
        "Hydralisk Needle Spines resolve to a projectile image");
    Require(ProbeAnimationSounds(
                Scripts, Loader.ResolveUnitImageID(38),
                Game::IScriptAnims::Anim_GndAttkInit) ==
            std::vector<int>{64} &&
            ProbeAnimationSounds(
                Scripts, Loader.ResolveUnitImageID(38),
                Game::IScriptAnims::Anim_AirAttkInit) ==
            std::vector<int>{64},
        "Hydralisk ground and air attack scripts emit the authored sound 64");
    Require(ProbeAnimationSounds(
                Scripts, HydraliskProjectileSprite->ImageID,
                Game::IScriptAnims::Anim_Init).empty(),
        "Hydralisk sound belongs to the unit firing frame, not its projectile");

    const auto SoundPaths = Resource::TblDecoder::DecodeStrings(
        ReadAll("assets/arr/sfxdata.tbl"));
    const auto SoundDat = ReadAll("assets/arr/sfxdata.dat");
    const auto ResolveSoundID = [&](uint32_t SoundID)
    {
        Require(SoundID * 4 + 4 <= SoundDat.size(),
            "sound id fits the sfxdata filename-index column");
        const size_t Offset = (size_t)SoundID * 4;
        const uint32_t OneBasedPathIndex =
            (uint32_t)SoundDat[Offset] |
            ((uint32_t)SoundDat[Offset + 1] << 8) |
            ((uint32_t)SoundDat[Offset + 2] << 16) |
            ((uint32_t)SoundDat[Offset + 3] << 24);
        return Core::Audio::ResolveArchiveSoundPath(
            SoundPaths, OneBasedPathIndex);
    };
    Require(ResolveSoundID(69) == "sound/Bullet/TMaFir00.wav",
        "Marine sound 69 resolves through the one-based TBL index");
    Require(ResolveSoundID(99) == "sound/Bullet/DragBull.wav",
        "Dragoon sound 99 resolves through the one-based TBL index");
    Require(ResolveSoundID(64) == "sound/Bullet/SpoogHit.wav",
        "Hydralisk sound 64 resolves through the one-based TBL index");

    constexpr uint32_t ScvMiningWeaponID = 14;
    const auto* ScvWeapon = Loader.FindWeaponByID(ScvMiningWeaponID);
    const auto* ScvFlingy = ScvWeapon ? Loader.FindFlingyByID(ScvWeapon->FlingyID) : nullptr;
    const auto* ScvSprite = ScvFlingy ? Loader.FindSpriteByID(ScvFlingy->SpriteID) : nullptr;
    Require(ScvWeapon && ScvFlingy && ScvSprite &&
            ScvWeapon->FlingyID == 141 && ScvFlingy->SpriteID == 340 &&
            ScvSprite->ImageID == 526,
        "Fusion Cutter follows the project DAT chain to SCV spark image 526");
    const std::string ScvSparkPath = Loader.ResolveImageGrpPath(ScvSprite->ImageID);
    Require(ScvSparkPath == "bullet/scvspark.grp",
        "Fusion Cutter resolves to the project unit/bullet/scvspark.grp material");
    const auto ScvSparkFrames = Resource::GrpDecoder::DecodeGrp(ReadSpriteAsset(ScvSparkPath));
    Require(ScvSparkFrames && ScvSparkFrames->Frames.size() > 1,
        "project SCV spark GRP decodes to animated frames");
    const auto* ScvBaseImage = Loader.FindImageByID(247);
    const auto* ScvShadowImage = Loader.FindImageByID(248);
    const auto* ScvGlowImage = Loader.FindImageByID(249);
    const auto* ScvShadowRelation = Loader.FindImageRelationByID(248);
    const auto* ScvGlowRelation = Loader.FindImageRelationByID(249);
    Require(ScvBaseImage && ScvBaseImage->Modifier == 0 &&
            Loader.ResolveImageGrpPath(247) == "terran/SCV.grp",
        "project SCV base image uses its classic GRP without a blend modifier");
    Require(ScvShadowImage && ScvShadowImage->Modifier == 10 &&
            ScvShadowRelation && ScvShadowRelation->IsShadow() &&
            ScvShadowRelation->ParentImageID == 247,
        "project SCV shadow keeps its shadow modifier and base-image relation");
    Require(ScvGlowImage && ScvGlowImage->Modifier == 9 &&
            ScvGlowImage->ColorShift == 1 &&
            Loader.ResolveImageGrpPath(249) == "thingy/tscGlow.grp" &&
            ScvGlowRelation && ScvGlowRelation->IsGlowOverlay() &&
            ScvGlowRelation->ParentImageID == 247,
        "project SCV walking glow keeps its light-table modifier and base-image relation");
    const auto ScvGlowFrames = Resource::GrpDecoder::DecodeGrp(
        ReadSpriteAsset(Loader.ResolveImageGrpPath(249)));
    Require(ScvGlowFrames && !ScvGlowFrames->Frames.empty(),
        "project SCV walking glow GRP decodes to a renderable dynamic overlay");
    Require(Resource::Graphics::DecodeLightTableMaskAlpha(1, 63, 192) == 0 &&
            Resource::Graphics::DecodeLightTableMaskAlpha(10, 63, 192) <
                Resource::Graphics::DecodeLightTableMaskAlpha(46, 63, 192) &&
            Resource::Graphics::DecodeLightTableMaskAlpha(46, 63, 192) < 192 &&
            Resource::Graphics::DecodeLightTableMaskAlpha(1, 1, 192) == 192,
        "modifier 9 light-table indices decode as a graded mask instead of uniform white alpha");

    Resource::Graphics::ColorPalette SyntheticPalette = {};
    for (int I = 0; I < 256; ++I)
    {
        SyntheticPalette.Colors[I].R = static_cast<uint8_t>(I);
        SyntheticPalette.Colors[I].G = static_cast<uint8_t>(I);
        SyntheticPalette.Colors[I].B = static_cast<uint8_t>(I);
        SyntheticPalette.Colors[I].A = 255;
    }
    std::vector<uint8_t> SyntheticLightTable(256 * 3, 0);
    for (int I = 0; I < 256; ++I)
    {
        SyntheticLightTable[I] = static_cast<uint8_t>(I);
        SyntheticLightTable[256 + I] = 200;
        SyntheticLightTable[512 + I] = static_cast<uint8_t>((I + 200) / 2);
    }
    const auto FittedLightTable = Resource::Graphics::BuildLightTableRgba(
        SyntheticLightTable, 256, 3, SyntheticPalette);
    Require(FittedLightTable[1].A == 0,
        "identity light-table rows remain transparent in the RGBA approximation");
    Require(FittedLightTable[2].A == 255 &&
            FittedLightTable[2].R == 200 && FittedLightTable[2].G == 200 &&
            FittedLightTable[2].B == 200,
        "constant light-table rows become opaque fitted colors");
    Require(FittedLightTable[3].A >= 126 && FittedLightTable[3].A <= 130 &&
            FittedLightTable[3].R >= 198 && FittedLightTable[3].R <= 202,
        "half-blend light-table rows preserve their source color and alpha");

    const auto JungleOFirePcx = Resource::Graphics::DecodeIndexedPcx(
        ReadAll("assets/Tileset/jungle/ofire.pcx"));
    const auto JungleWpeData = ReadAll("assets/Tileset/jungle.wpe");
    Require(JungleOFirePcx.Valid() && JungleOFirePcx.Width == 256 &&
            JungleOFirePcx.Height == 63 && JungleWpeData.size() >= 1024,
        "project jungle orange-fire lookup table and WPE palette decode");
    Resource::Graphics::ColorPalette JunglePalette = {};
    for (int I = 0; I < 256; ++I)
    {
        JunglePalette.Colors[I].R = JungleWpeData[(size_t)I * 4u + 0u];
        JunglePalette.Colors[I].G = JungleWpeData[(size_t)I * 4u + 1u];
        JunglePalette.Colors[I].B = JungleWpeData[(size_t)I * 4u + 2u];
        JunglePalette.Colors[I].A = 255;
    }
    const auto JungleOFire = Resource::Graphics::BuildLightTableRgba(
        JungleOFirePcx.Pixels, JungleOFirePcx.Width,
        JungleOFirePcx.Height, JunglePalette);
    Require(JungleOFire[1].A < 8 && JungleOFire[63].A < 8,
        "jungle orange-fire endpoint rows stay transparent instead of becoming white discs");
    Require(JungleOFire[32].A > 128 &&
            (JungleOFire[32].R < 250 || JungleOFire[32].G < 250 ||
             JungleOFire[32].B < 250),
        "jungle orange-fire middle rows produce a colored visible glow");

    const auto Upgrades = Resource::DatDecoder::DecodeUpgrades(ReadAll("assets/arr/upgrades.dat"));
    Require(Upgrades.size() > 0, "upgrades.dat decodes");
    Require(Upgrades[0].MineralCostBase == 100 &&
            Upgrades[0].MineralCostFactor == 75 &&
            Upgrades[0].GasCostBase == 100 &&
            Upgrades[0].GasCostFactor == 75 &&
            Upgrades[0].TimeCostBase == 4000 &&
            Upgrades[0].TimeCostFactor == 480,
        "upgrade costs decode from column-major upgrades.dat");

    const auto Tech = Resource::DatDecoder::DecodeTech(ReadAll("assets/arr/techdata.dat"));
    Require(Tech.size() > 0, "techdata.dat decodes");
    Require(Tech[0].MineralCost == 100 &&
            Tech[0].GasCost == 100 &&
            Tech[0].TimeCost == 1200 &&
            Tech[0].RequiredUnit == 0xffffffffu,
        "tech costs decode from column-major techdata.dat");

    constexpr std::array<int, 10> SelectionCirclePixels =
        {22, 32, 48, 62, 72, 94, 110, 122, 146, 224};
    for (size_t I = 0; I < SelectionCirclePixels.size(); ++I)
    {
        RequireSelectionCircleImage(Loader, (uint32_t)I, SelectionCirclePixels[I]);
    }

    RequireUnitMainImage(Loader, 7, "SCV image");
    RequireUnitMainImage(Loader, 106, "Command Center image");
    RequireUnitMainImage(Loader, 5, "Tank image");
    RequireUnitMainImage(Loader, 12, "Battlecruiser image");
    RequireUnitMainImage(Loader, 124, "Missile Turret image");
    RequireUnitSubunitImage(Loader, 3, "Goliath head image");
    RequireUnitSubunitImage(Loader, 5, "Tank turret image");
    RequireUnitSubunitImage(Loader, 30, "Sieged Tank turret image");
    RequireUnitConstructionImage(Loader, 109, 330, "Supply Depot construction image");
    const auto* SupplyDepotConstruction = Loader.FindImageByID(330);
    Require(SupplyDepotConstruction && SupplyDepotConstruction->Modifier == 0 &&
            SupplyDepotConstruction->IScriptID == 140 &&
            Loader.ResolveImageGrpPath(330) == "terran/TBldSml.grp",
        "Supply Depot construction resolves the project TBldSml GRP and iscript");
    const auto* RefineryUnit = Loader.FindUnitByID(110);
    Require(RefineryUnit && RefineryUnit->ConstructionImageID == 0,
        "Refinery keeps the canonical zero construction-image sentinel");
    RequireUnitConstructionImage(
        Loader, 110, Loader.ResolveUnitImageID(110),
        "Refinery zero construction-image sentinel falls back to its main image");
    const auto* AssimilatorUnit = Loader.FindUnitByID(157);
    Require(AssimilatorUnit && AssimilatorUnit->ConstructionImageID == 0 &&
            Loader.ResolveUnitConstructionImageID(157) ==
                Loader.ResolveUnitImageID(157),
        "Assimilator zero construction-image sentinel also falls back to its main image");
    RequireUnitTeamColorMaskSource(Loader, 0, "Marine team-color source");
    RequireUnitTeamColorMaskSource(Loader, 7, "SCV team-color source");

    for (uint32_t ChkUnitID : {37u, 38u, 41u, 46u, 50u, 51u, 53u, 54u, 103u})
    {
        RequireUnitIScriptAnimations(
            Loader, Scripts, ChkUnitID,
            {Game::IScriptAnims::Anim_Burrow, Game::IScriptAnims::Anim_UnBurrow},
            "burrow-capable project unit has Burrow and UnBurrow animations");
        RequireUnitIScriptOrderSignal(
            Loader, Scripts, ChkUnitID, Game::IScriptAnims::Anim_Burrow, 0x04,
            "project Burrow animation emits the completion signal");
        RequireUnitIScriptOrderSignal(
            Loader, Scripts, ChkUnitID, Game::IScriptAnims::Anim_UnBurrow, 0x04,
            "project UnBurrow animation emits the completion signal");
    }
    RequireUnitIScriptAnimations(
        Loader, Scripts, 39u,
        {Game::IScriptAnims::Anim_Init},
        "Ultralisk project image resolves without assuming a Burrow animation");
    RequireUnitLacksIScriptAnimation(
        Loader, Scripts, 39u, Game::IScriptAnims::Anim_Burrow,
        "Ultralisk project image does not expose a Burrow animation");

    for (uint32_t ChkUnitID : {106u, 111u, 113u, 114u, 116u})
    {
        RequireUnitIScriptAnimations(
            Loader, Scripts, ChkUnitID,
            {Game::IScriptAnims::Anim_LiftOff, Game::IScriptAnims::Anim_Landing},
            "liftable project building has LiftOff and Landing animations");
        RequireUnitIScriptOrderSignal(
            Loader, Scripts, ChkUnitID, Game::IScriptAnims::Anim_LiftOff, 0x10,
            "project LiftOff animation emits the flight completion signal");
        RequireUnitIScriptOrderSignal(
            Loader, Scripts, ChkUnitID, Game::IScriptAnims::Anim_Landing, 0x10,
            "project Landing animation emits the flight completion signal");
    }
    for (uint32_t ChkUnitID : {107u, 108u, 115u, 117u, 118u, 120u})
    {
        RequireUnitIScriptAnimations(
            Loader, Scripts, ChkUnitID,
            {Game::IScriptAnims::Anim_LiftOff, Game::IScriptAnims::Anim_Landing},
            "project Terran add-on has detach and reconnect animations");
    }

    Require(Game::Data::IsAirUnitType(11), "Dropship is treated as air");
    Require(Game::Data::IsAirUnitType(12), "Battlecruiser is treated as air");
    Require(Game::Data::IsAirUnitType(58), "Valkyrie raw CHK id is treated as air");
    Require(!Game::Data::IsAirUnitType(7), "SCV is treated as ground");
    Require(!Game::Data::IsAirUnitType(106), "Command Center is treated as ground");

    size_t ShadowCount = 0;
    size_t ShadowWithParentCount = 0;
    size_t DecodableShadowCount = 0;
    for (const auto& Relation : Loader.GetImageRelations())
    {
        if (!Relation.IsShadow())
            continue;
        ++ShadowCount;
        if (Relation.HasParent())
            ++ShadowWithParentCount;

        if (DecodableShadowCount == 0)
        {
            const std::string Path = Loader.ResolveImageGrpPath(Relation.ImageID);
            if (!Path.empty())
            {
                auto Sprite = Resource::GrpDecoder::DecodeGrp(ReadSpriteAsset(Path));
                if (Sprite.has_value() && !Sprite->Frames.empty())
                    ++DecodableShadowCount;
            }
        }
    }

    Require(ShadowCount > 0, "images.rel contains shadow relations");
    Require(ShadowWithParentCount == ShadowCount,
        "shadow relations are parented overlays, not standalone main images");
    Require(DecodableShadowCount > 0,
        "shadow overlay relation resolves to decodable GRP frames");

    std::printf("SpriteImageDataTest passed\n");
    return 0;
}
