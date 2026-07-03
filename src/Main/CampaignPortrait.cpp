#include "Main/CampaignPortrait.h"

#include "Game/DataEnums.h"
#include "Resource/GameDataLoader.h"
#include "Resource/TblDecoder.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <vector>

namespace Game::Client::GameLoop
{

namespace
{

std::vector<std::string>& GetPortdataStrings(const Resource::Graphics::ArchiveAssetSourceView& ArchiveSource)
{
    static bool Loaded = false;
    static std::vector<std::string> Strings;
    if (Loaded)
        return Strings;

    Loaded = true;
    static constexpr const char* Paths[] = {
        "arr/portdata.tbl",
        "arr\\portdata.tbl",
        "portdata.tbl"
    };
    for (const char* Path : Paths)
    {
        auto Data = ArchiveSource.ReadFile(Path);
        if (!Data || Data->empty())
            continue;

        Strings = Resource::TblDecoder::DecodeStrings(*Data);
        if (!Strings.empty())
            break;
    }
    return Strings;
}

int CampaignPortdataFidIndexForUnitID(uint32_t UnitID)
{
    using Game::Data::UnitType;
    switch ((int)UnitID)
    {
        case (int)UnitType::Terran_Marine: return 1;
        case (int)UnitType::Terran_Ghost: return 3;
        case (int)UnitType::Terran_Vulture: return 7;
        case (int)UnitType::Terran_Goliath:
        case (int)UnitType::Terran_Goliath_Turret: return 11;
        case (int)UnitType::Terran_Siege_Tank_Tank_Mode:
        case (int)UnitType::Terran_Siege_Tank_Tank_Mode_Turret:
        case (int)UnitType::Terran_Siege_Tank_Siege_Mode:
        case (int)UnitType::Terran_Siege_Tank_Siege_Mode_Turret: return 13;
        case (int)UnitType::Terran_SCV: return 15;
        case (int)UnitType::Terran_Wraith: return 17;
        case (int)UnitType::Terran_Science_Vessel: return 19;
        case (int)UnitType::Hero_Gui_Montag:
        case (int)UnitType::Terran_Firebat: return 5;
        case (int)UnitType::Terran_Dropship: return 21;
        case (int)UnitType::Terran_Battlecruiser:
        case (int)UnitType::Hero_Arcturus_Mengsk:
        case (int)UnitType::Hero_Hyperion:
        case (int)UnitType::Hero_Norad_II: return 23;
        case (int)UnitType::Hero_Sarah_Kerrigan: return 25;
        case (int)UnitType::Hero_Jim_Raynor_Vulture:
        case (int)UnitType::Hero_Jim_Raynor_Marine: return 27;
        case (int)UnitType::Hero_Edmund_Duke_Tank_Mode:
        case (int)UnitType::Hero_Edmund_Duke_Tank_Mode_Turret:
        case (int)UnitType::Hero_Edmund_Duke_Siege_Mode:
        case (int)UnitType::Hero_Edmund_Duke_Siege_Mode_Turret: return 29;
        case (int)UnitType::Hero_Alan_Schezar:
        case (int)UnitType::Hero_Alan_Schezar_Turret:
        case (int)UnitType::Hero_Alexei_Stukov: return 154;
        case (int)UnitType::Hero_Tom_Kazansky: return 17;
        case (int)UnitType::Hero_Magellan: return 19;
        case (int)UnitType::Terran_Medic: return 148;
        case (int)UnitType::Terran_Valkyrie: return 150;
        case (int)UnitType::Hero_Samir_Duran:
        case (int)UnitType::Hero_Infested_Duran: return 156;
        case (int)UnitType::Hero_Gerard_DuGalle: return 152;

        case (int)UnitType::Zerg_Larva: return 36;
        case (int)UnitType::Zerg_Egg: return 38;
        case (int)UnitType::Zerg_Zergling:
        case (int)UnitType::Hero_Devouring_One: return 40;
        case (int)UnitType::Zerg_Hydralisk:
        case (int)UnitType::Hero_Hunter_Killer: return 42;
        case (int)UnitType::Zerg_Ultralisk:
        case (int)UnitType::Hero_Torrasque: return 44;
        case (int)UnitType::Zerg_Broodling: return 46;
        case (int)UnitType::Zerg_Drone: return 48;
        case (int)UnitType::Zerg_Overlord:
        case (int)UnitType::Hero_Yggdrasill: return 50;
        case (int)UnitType::Zerg_Mutalisk:
        case (int)UnitType::Hero_Kukulza_Mutalisk: return 52;
        case (int)UnitType::Zerg_Guardian:
        case (int)UnitType::Hero_Kukulza_Guardian: return 54;
        case (int)UnitType::Zerg_Queen:
        case (int)UnitType::Hero_Matriarch: return 56;
        case (int)UnitType::Zerg_Defiler:
        case (int)UnitType::Hero_Unclean_One: return 58;
        case (int)UnitType::Zerg_Scourge: return 60;
        case (int)UnitType::Zerg_Cocoon:
        case (int)UnitType::Zerg_Lurker_Egg: return 62;
        case (int)UnitType::Zerg_Infested_Terran: return 64;
        case (int)UnitType::Hero_Infested_Kerrigan: return 70;
        case (int)UnitType::Zerg_Devourer: return 162;
        case (int)UnitType::Zerg_Lurker: return 164;

        case (int)UnitType::Protoss_Probe: return 76;
        case (int)UnitType::Protoss_Zealot: return 78;
        case (int)UnitType::Protoss_Dragoon: return 80;
        case (int)UnitType::Protoss_High_Templar:
        case (int)UnitType::Hero_Aldaris: return 82;
        case (int)UnitType::Protoss_Archon:
        case (int)UnitType::Hero_Tassadar_Zeratul_Archon: return 84;
        case (int)UnitType::Protoss_Shuttle: return 86;
        case (int)UnitType::Protoss_Scout:
        case (int)UnitType::Hero_Mojo: return 88;
        case (int)UnitType::Protoss_Arbiter:
        case (int)UnitType::Hero_Danimoth: return 90;
        case (int)UnitType::Protoss_Carrier:
        case (int)UnitType::Hero_Gantrithor: return 92;
        case (int)UnitType::Protoss_Interceptor: return 94;
        case (int)UnitType::Protoss_Dark_Templar:
        case (int)UnitType::Hero_Dark_Templar: return 96;
        case (int)UnitType::Hero_Fenix_Zealot: return 98;
        case (int)UnitType::Hero_Fenix_Dragoon: return 100;
        case (int)UnitType::Hero_Zeratul: return 102;
        case (int)UnitType::Hero_Tassadar: return 104;
        case (int)UnitType::Protoss_Observer: return 106;
        case (int)UnitType::Protoss_Dark_Archon: return 166;
        case (int)UnitType::Protoss_Corsair: return 168;
        case (int)UnitType::Hero_Artanis: return 158;

        case (int)UnitType::Critter_Rhynadon: return 110;
        case (int)UnitType::Special_Mercenary_Gunship: return 114;
        case (int)UnitType::Critter_Bengalaas: return 116;
        case (int)UnitType::Critter_Scantid: return 118;
        case (int)UnitType::Critter_Ragnasaur: return 120;
        case (int)UnitType::Critter_Ursadon: return 174;
        default: return 0;
    }
}

std::string PortraitKeyFromPortdataPath(const std::string& Path)
{
    size_t Sep = Path.find_first_of("\\/");
    if (Sep == std::string::npos || Sep == 0)
        return {};

    std::string Key = Path.substr(0, Sep);
    std::transform(Key.begin(), Key.end(), Key.begin(), [](unsigned char Ch)
    {
        return (char)std::tolower(Ch);
    });
    return Key;
}

bool TextContainsAsciiCaseInsensitive(const std::string& Text, const char* Needle)
{
    std::string LowerText = Text;
    std::transform(LowerText.begin(), LowerText.end(), LowerText.begin(), [](unsigned char Ch)
    {
        return (char)std::tolower(Ch);
    });

    std::string LowerNeedle = Needle;
    std::transform(LowerNeedle.begin(), LowerNeedle.end(), LowerNeedle.begin(), [](unsigned char Ch)
    {
        return (char)std::tolower(Ch);
    });
    return LowerText.find(LowerNeedle) != std::string::npos;
}

} // namespace

std::string CampaignPortraitKeyFromUnitID(
    const Resource::Graphics::DataAssetCacheView& DataAssets,
    const Resource::Graphics::ArchiveAssetSourceView& ArchiveSource,
    uint32_t UnitID)
{
    const auto& Portdata = GetPortdataStrings(ArchiveSource);
    const int PortdataIndex = CampaignPortdataFidIndexForUnitID(UnitID);
    if (PortdataIndex > 0 && PortdataIndex <= (int)Portdata.size())
    {
        std::string Key = PortraitKeyFromPortdataPath(Portdata[(size_t)PortdataIndex - 1u]);
        if (!Key.empty())
            return Key;
    }

    const auto* DataLoader = DataAssets.GetDataLoader();
    const auto* Unit = DataLoader ? DataLoader->FindUnitByID(UnitID) : nullptr;
    if (Unit && Unit->PortraitID > 0 && Unit->PortraitID <= Portdata.size())
        return PortraitKeyFromPortdataPath(Portdata[(size_t)Unit->PortraitID - 1u]);

    return {};
}

std::string CampaignPortraitKeyFromText(const std::string& Text)
{
    if (TextContainsAsciiCaseInsensitive(Text, "SCV")) return "tscv";
    if (TextContainsAsciiCaseInsensitive(Text, "Raynor") || TextContainsAsciiCaseInsensitive(Text, "Jim")) return "ujim";
    if (TextContainsAsciiCaseInsensitive(Text, "Kerrigan")) return "ukerr";
    if (TextContainsAsciiCaseInsensitive(Text, "Mengsk")) return "umengsk";
    if (TextContainsAsciiCaseInsensitive(Text, "Duke")) return "uduke";
    if (TextContainsAsciiCaseInsensitive(Text, "Duran")) return "tsam";
    if (TextContainsAsciiCaseInsensitive(Text, "Stukov")) return "talex";
    if (TextContainsAsciiCaseInsensitive(Text, "Marine")) return "tmarine";
    if (TextContainsAsciiCaseInsensitive(Text, "Firebat")) return "tfirebat";
    if (TextContainsAsciiCaseInsensitive(Text, "Medic")) return "tmedic";
    if (TextContainsAsciiCaseInsensitive(Text, "Zeratul")) return "uzeratul";
    if (TextContainsAsciiCaseInsensitive(Text, "Tassadar")) return "utassadar";
    if (TextContainsAsciiCaseInsensitive(Text, "Artanis")) return "partanis";
    if (TextContainsAsciiCaseInsensitive(Text, "Fenix")) return "ufenzeal";
    return {};
}

std::string CampaignAdvisorPortraitKey(int PlayerID)
{
    if (PlayerID >= 0 && PlayerID <= 3)
        return "tadvisor";
    if (PlayerID >= 4 && PlayerID <= 7)
        return "zadvisor";
    return "padvisor";
}

const Resource::Graphics::PortraitFrame* CampaignPortraitFrameAt(
    const Resource::Graphics::PortraitClip& Clip,
    double TimeSeconds)
{
    if (Clip.Frames.empty())
        return nullptr;

    double Total = 0.0;
    for (const auto& Frame : Clip.Frames)
        Total += std::max(0.01f, Frame.DurationSeconds);
    double T = Total > 0.0 ? std::fmod(TimeSeconds, Total) : 0.0;
    for (const auto& Frame : Clip.Frames)
    {
        T -= std::max(0.01f, Frame.DurationSeconds);
        if (T <= 0.0)
            return &Frame;
    }
    return &Clip.Frames.back();
}

} // namespace Game::Client::GameLoop
