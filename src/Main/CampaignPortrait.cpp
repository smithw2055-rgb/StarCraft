#include "Main/CampaignPortrait.h"

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
    switch ((int)UnitID)
    {
        case 0: return 1;   // Terran Marine
        case 1: return 3;   // Terran Ghost
        case 2: return 7;   // Terran Vulture
        case 3:
        case 4: return 11;  // Terran Goliath
        case 5:
        case 6:
        case 30:
        case 31: return 13; // Terran Siege Tank
        case 7: return 15;  // SCV
        case 8:
        case 21: return 17; // Wraith / Tom Kazansky
        case 9:
        case 22: return 19; // Science Vessel / Magellan
        case 10:
        case 32: return 5;  // Firebat
        case 11: return 21; // Dropship
        case 12:
        case 27:
        case 28:
        case 29: return 23; // Battlecruiser heroes
        case 16: return 25; // Sarah Kerrigan
        case 19:
        case 20: return 27; // Jim Raynor
        case 23:
        case 24:
        case 25:
        case 26: return 29; // Edmund Duke
        case 17:
        case 18:
        case 100: return 154; // Alan Schezar / Stukov
        case 34: return 148; // Medic
        case 58: return 150; // Valkyrie
        case 99:
        case 104: return 156; // Duran
        case 102: return 152; // DuGalle

        case 35: return 36; // Larva
        case 36: return 38; // Egg
        case 37:
        case 54: return 40; // Zergling
        case 38:
        case 53: return 42; // Hydralisk
        case 39:
        case 48: return 44; // Ultralisk
        case 40: return 46; // Broodling
        case 41: return 48; // Drone
        case 42:
        case 57: return 50; // Overlord
        case 43:
        case 55: return 52; // Mutalisk
        case 44:
        case 56: return 54; // Guardian
        case 45:
        case 49: return 56; // Queen
        case 46:
        case 52: return 58; // Defiler
        case 47: return 60; // Scourge
        case 59:
        case 97: return 62; // Cocoon / Lurker Egg
        case 50: return 64; // Infested Terran
        case 51: return 70; // Infested Kerrigan
        case 62: return 162; // Devourer
        case 103: return 164; // Lurker

        case 64: return 76; // Probe
        case 65: return 78; // Zealot
        case 66: return 80; // Dragoon
        case 67:
        case 87: return 82; // High Templar / Aldaris
        case 68:
        case 76: return 84; // Archon
        case 69: return 86; // Shuttle
        case 70:
        case 80: return 88; // Scout / Mojo
        case 71:
        case 86: return 90; // Arbiter / Danimoth
        case 72:
        case 82: return 92; // Carrier / Gantrithor
        case 73: return 94; // Interceptor
        case 61:
        case 74: return 96; // Dark Templar
        case 77: return 98; // Fenix Zealot
        case 78: return 100; // Fenix Dragoon
        case 75: return 102; // Zeratul
        case 79: return 104; // Tassadar
        case 84: return 106; // Observer
        case 63: return 166; // Dark Archon
        case 60: return 168; // Corsair
        case 88: return 158; // Artanis

        case 89: return 110; // Rhynadon
        case 92: return 114; // Mercenary Gunship
        case 90: return 116; // Bengalaas
        case 93: return 118; // Scantid
        case 95: return 120; // Ragnasaur
        case 96: return 174; // Ursadon
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
