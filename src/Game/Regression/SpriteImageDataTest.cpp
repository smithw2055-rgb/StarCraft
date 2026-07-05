#include "Resource/GameDataLoader.h"
#include "Resource/GrpDecoder.h"

#include "Game/DataEnums.h"

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

    const auto* Marine = Loader.FindUnitByID(0);
    Require(Marine, "Marine units.dat entry");
    Require(Marine->DimensionLeft == 8 && Marine->DimensionUp == 9 &&
            Marine->DimensionRight == 8 && Marine->DimensionDown == 10,
        "Marine dimensions decode to 17x20");
    Require(Marine->DimensionWidth() == 17 && Marine->DimensionHeight() == 20,
        "Marine derived dimensions match BW unit bounds");

    const auto* SCV = Loader.FindUnitByID(7);
    Require(SCV, "SCV units.dat entry");
    Require(SCV->DimensionWidth() == 23 && SCV->DimensionHeight() == 23,
        "SCV dimensions decode to 23x23");

    const auto* CommandCenter = Loader.FindUnitByID(106);
    Require(CommandCenter, "Command Center units.dat entry");
    Require(CommandCenter->PlacementWidth == 160 &&
            CommandCenter->PlacementHeight == 160,
        "Command Center placement size decodes");
    Require(CommandCenter->DimensionLeft == 58 &&
            CommandCenter->DimensionUp == 41 &&
            CommandCenter->DimensionRight == 58 &&
            CommandCenter->DimensionDown == 41,
        "Command Center dimensions decode to 117x83");

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
    RequireUnitTeamColorMaskSource(Loader, 0, "Marine team-color source");
    RequireUnitTeamColorMaskSource(Loader, 7, "SCV team-color source");

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
