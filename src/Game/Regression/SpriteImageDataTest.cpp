#include "Resource/GameDataLoader.h"
#include "Resource/DatDecoder.h"
#include "Resource/GrpDecoder.h"
#include "Resource/GrpToGpu.h"

#include "Game/DataEnums.h"
#include "Game/IScriptVM.h"

#include <algorithm>
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
    Require(Loader.LoadUnitDataRaw(ReadAll("assets/arr/units.dat")),
        "load units.dat");
    Require(Loader.LoadWeaponDataRaw(ReadAll("assets/arr/weapons.dat")),
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

    Game::IScriptDatabase Scripts;
    Require(Scripts.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
        "load project iscript data");

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
            GaussRifle->BulletCount == 1,
        "Gauss Rifle decodes from column-major weapons.dat");
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
    RequireUnitConstructionImage(Loader, 109, 330, "Supply Depot construction image");
    const auto* SupplyDepotConstruction = Loader.FindImageByID(330);
    Require(SupplyDepotConstruction && SupplyDepotConstruction->Modifier == 0 &&
            SupplyDepotConstruction->IScriptID == 140 &&
            Loader.ResolveImageGrpPath(330) == "terran/TBldSml.grp",
        "Supply Depot construction resolves the project TBldSml GRP and iscript");
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
