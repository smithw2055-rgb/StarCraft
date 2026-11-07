#include "Main/CampaignPresentation.h"

#include "Main/CampaignPortrait.h"
#include "Main/CampaignText.h"
#include "Core/Platform/Platform.h"
#include "Game/Campaign/CampaignController.h"
#include "Resource/ArchiveAssetSourceView.h"
#include "Resource/DataAssetCacheView.h"
#include "Resource/UIAssetCacheView.h"
#include "Resource/UIAssetTypes.h"
#include "Render/Color.h"
#include "Render/Draw2D.h"
#include "UI/Hud.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace Game::Client::GameLoop
{

void RenderCampaignOverlay(
    Game::Campaign::CampaignController& Campaign,
    UI::Hud& Hud,
    const Resource::Graphics::DataAssetCacheView& DataAssets,
    const Resource::Graphics::ArchiveAssetSourceView& ArchiveSource,
    const Resource::Graphics::UIAssetCacheView& UIAssets,
    int ViewportWidth,
    int ViewportHeight)
{
    auto& C = Campaign;
    if (C.GetState() == Game::Campaign::CampaignState::Playing)
    {
        const int HudH = Hud.GetHudHeight(ViewportWidth, ViewportHeight);
        const int PlayBottom = std::max(120, ViewportHeight - HudH);
        const auto& Objectives = C.GetObjectives();
        if (!Objectives.empty())
        {
            const float ObjX = 14.0f;
            const float ObjY = (float)Hud.GetResourceBarHeight() + 12.0f;
            const float ObjW = std::min(440.0f, (float)ViewportWidth - 28.0f);
            const float LineH = 20.0f;
            const float ObjH = std::min(160.0f, 38.0f + Objectives.size() * LineH);

            // Sleek high-tech translucent dark container with double border
            Render::g_Draw2D.DrawRect(ObjX, ObjY, ObjW, ObjH, Render::Color(8, 12, 18, 205));
            Render::g_Draw2D.DrawRectLines(ObjX, ObjY, ObjW, ObjH, Render::Color(55, 85, 115, 210), 1.0f);

            // Left tech accent bar (gold / cyan)
            Render::g_Draw2D.DrawRect(ObjX + 1.0f, ObjY + 1.0f, 3.0f, ObjH - 2.0f,
                                      Render::Color(225, 185, 60, 230));

            // Mission Title Header
            std::string Title = C.GetMissionName();
            if (Title.empty() || Title == "StarCraft Scenario")
                Title = "任务目标 (Mission Objectives)";

            Render::g_Draw2D.DrawText(Title, ObjX + 14.0f, ObjY + 8.0f, 12.0f,
                Render::Color(245, 225, 165, 255));

            float OY = ObjY + 32.0f;
            size_t Shown = 0;
            for (auto& O : Objectives)
            {
                if (Shown++ >= 6)
                    break;

                std::string Desc = SanitizeCampaignText(O.Description);
                // Strip redundant trailing periods
                while (!Desc.empty() && (Desc.back() == '.' || Desc.back() == ' '))
                    Desc.pop_back();

                if (Desc.size() > 58)
                    Desc = Desc.substr(0, 55) + "...";

                if (O.Complete)
                {
                    // Glowing green checkmark for completed
                    Render::g_Draw2D.DrawText("[√] ", ObjX + 14.0f, OY, 12.0f,
                        Render::Color(80, 240, 100, 255));
                    Render::g_Draw2D.DrawText(Desc, ObjX + 38.0f, OY, 12.0f,
                        Render::Color(140, 220, 150, 255));
                }
                else
                {
                    // Clean cyan-grey bracket for pending
                    Render::g_Draw2D.DrawText("[  ] ", ObjX + 14.0f, OY, 12.0f,
                        Render::Color(130, 160, 190, 255));
                    Render::g_Draw2D.DrawText(Desc, ObjX + 38.0f, OY, 12.0f,
                        Render::Color(225, 235, 245, 255));
                }
                OY += LineH;
            }
        }

        const auto& Leaderboard = C.GetLeaderboard();
        if (Leaderboard.Active)
        {
            const float BoardW = std::min(300.0f, (float)ViewportWidth - 24.0f);
            const float BoardH = 54.0f;
            const float BoardX = std::max(12.0f, (float)ViewportWidth - BoardW - 12.0f);
            const float BoardY = (float)Hud.GetResourceBarHeight() + 12.0f;
            Render::g_Draw2D.DrawRect(BoardX, BoardY, BoardW, BoardH, Render::Color(3, 8, 14, 170));
            Render::g_Draw2D.DrawRectLines(BoardX, BoardY, BoardW, BoardH,
                Render::Color(72, 112, 140, 210), 1.0f);
            std::string Label = SanitizeCampaignText(Leaderboard.Label);
            if (Label.empty())
                Label = "Leaderboard";
            if (Label.size() > 28)
                Label = Label.substr(0, 25) + "...";
            Render::g_Draw2D.DrawText(Label, BoardX + 12.0f, BoardY + 9.0f, 1.0f,
                Render::Color(235, 224, 170, 255));
            Render::g_Draw2D.DrawText(std::to_string(Leaderboard.CurrentValue),
                BoardX + BoardW - 54.0f, BoardY + 27.0f, 1.8f,
                Render::Color(130, 230, 140, 255));
        }

        const auto& Messages = C.GetMessages();
        for (auto It = Messages.rbegin(); It != Messages.rend(); ++It)
        {
            const auto& M = *It;
            if (M.Text.empty() && M.ActionID != 29)
                continue;

            const float PanelW = std::min(760.0f, (float)ViewportWidth - 32.0f);
            const float PanelH = 96.0f;
            const float PanelX = std::max(16.0f, ((float)ViewportWidth - PanelW) * 0.5f);
            const float PanelY = std::max(96.0f, (float)PlayBottom - PanelH - 16.0f);
            Render::g_Draw2D.DrawRect(PanelX, PanelY, PanelW, PanelH, Render::Color(3, 7, 13, 218));
            Render::g_Draw2D.DrawRectLines(PanelX, PanelY, PanelW, PanelH, Render::Color(94, 136, 170, 235), 1.0f);

            const float PortraitX = PanelX + 12.0f;
            const float PortraitY = PanelY + 12.0f;
            Render::g_Draw2D.DrawRect(PortraitX, PortraitY, 72.0f, 72.0f, Render::Color(10, 16, 24, 255));

            std::string PortraitKey;
            if (M.PortraitUnitID != Game::Campaign::kNoPortraitUnitID)
                PortraitKey = CampaignPortraitKeyFromUnitID(DataAssets, ArchiveSource, M.PortraitUnitID);
            if (PortraitKey.empty())
                PortraitKey = CampaignPortraitKeyFromText(M.Text);
            if (PortraitKey.empty())
                PortraitKey = CampaignAdvisorPortraitKey(M.PlayerID);

            const Resource::Graphics::PortraitFrame* Frame =
                UIAssets.GetPortraitFrame(PortraitKey, Core::Platform::GetAbsoluteTime());
            if (Frame && Frame->Image.Valid())
            {
                const float Pad = 3.0f;
                const float MaxW = 72.0f - Pad * 2.0f;
                const float MaxH = 72.0f - Pad * 2.0f;
                const float ImageAspect = Frame->Image.Height > 0
                    ? (float)Frame->Image.Width / (float)Frame->Image.Height
                    : 1.0f;
                float DrawW = MaxW;
                float DrawH = MaxH;
                if (ImageAspect > 1.0f)
                    DrawH = MaxW / ImageAspect;
                else
                    DrawW = MaxH * ImageAspect;

                Render::g_Draw2D.DrawSpriteFromImage(Frame->Image,
                    PortraitX + Pad + (MaxW - DrawW) * 0.5f,
                    PortraitY + Pad + (MaxH - DrawH) * 0.5f,
                    DrawW,
                    DrawH);
            }
            else
            {
                Render::g_Draw2D.DrawText(CampaignMessageKind(M.ActionID), PortraitX + 6.0f, PortraitY + 14.0f, 0.8f,
                    Render::Color(190, 210, 230, 255));
                Render::g_Draw2D.DrawText(PortraitKey.empty() ? "static" : PortraitKey,
                    PortraitX + 8.0f, PortraitY + 42.0f, 0.9f,
                    Render::Color(235, 224, 170, 255));
            }

            Render::g_Draw2D.DrawRectLines(PortraitX, PortraitY, 72.0f, 72.0f,
                M.ActionID == 8 ? Render::Color(90, 145, 210, 240) : Render::Color(165, 145, 78, 240), 1.0f);

            Render::g_Draw2D.DrawText(CampaignMessageKind(M.ActionID), PanelX + 100.0f, PanelY + 13.0f, 1.0f,
                M.ActionID == 8 ? Render::Color(150, 200, 255, 255) : Render::Color(235, 224, 170, 255));
            const auto Lines = M.Text.empty()
                ? std::vector<std::string>{}
                : WrapCampaignText(M.Text, 74, 3);
            float TextY = PanelY + 34.0f;
            for (const auto& Line : Lines)
            {
                Render::g_Draw2D.DrawText(Line, PanelX + 100.0f, TextY, 1.15f,
                    M.ActionID == 8 ? Render::Color(185, 218, 255, 255) : Render::Color(235, 232, 205, 255));
                TextY += 18.0f;
            }
            break;
        }
    }
    if (C.GetState() == Game::Campaign::CampaignState::Victory)
    {
        RenderMatchGameOverOverlay(MatchOutcome::Victory, ViewportWidth, ViewportHeight, C.GetStateTimer());
    }
    if (C.GetState() == Game::Campaign::CampaignState::Defeat)
    {
        RenderMatchGameOverOverlay(MatchOutcome::Defeat, ViewportWidth, ViewportHeight, C.GetStateTimer());
    }
}

void RenderMatchGameOverOverlay(
    MatchOutcome Outcome,
    int ViewportWidth,
    int ViewportHeight,
    float ElapsedSeconds)
{
    if (Outcome == MatchOutcome::Playing)
        return;

    auto& D = Render::g_Draw2D;

    Render::Color BackdropColor = (Outcome == MatchOutcome::Victory)
        ? Render::Color(0, 36, 12, 175)
        : (Outcome == MatchOutcome::Defeat)
            ? Render::Color(44, 4, 4, 175)
            : Render::Color(16, 22, 32, 175);
    D.DrawRect(0.0f, 0.0f, (float)ViewportWidth, (float)ViewportHeight, BackdropColor);

    const float PanelW = 440.0f;
    const float PanelH = 220.0f;
    const float PanelX = ((float)ViewportWidth - PanelW) * 0.5f;
    const float PanelY = ((float)ViewportHeight - PanelH) * 0.5f;

    D.DrawRect(PanelX, PanelY, PanelW, PanelH, Render::Color(10, 18, 28, 245));
    Render::Color BorderColor = (Outcome == MatchOutcome::Victory)
        ? Render::Color(80, 220, 120, 255)
        : (Outcome == MatchOutcome::Defeat)
            ? Render::Color(240, 80, 80, 255)
            : Render::Color(140, 180, 220, 255);
    D.DrawRectLines(PanelX, PanelY, PanelW, PanelH, BorderColor, 2.0f);

    D.DrawRect(PanelX + 2.0f, PanelY + 2.0f, PanelW - 4.0f, 44.0f, Render::Color(18, 34, 52, 230));

    const char* TitleText = (Outcome == MatchOutcome::Victory)
        ? "VICTORY"
        : (Outcome == MatchOutcome::Defeat) ? "DEFEAT" : "DRAW";
    Render::Color TitleColor = (Outcome == MatchOutcome::Victory)
        ? Render::Color(110, 255, 130, 255)
        : (Outcome == MatchOutcome::Defeat)
            ? Render::Color(255, 95, 95, 255)
            : Render::Color(220, 235, 248, 255);

    D.DrawText(TitleText, PanelX + (PanelW * 0.5f) - 52.0f, PanelY + 12.0f, 2.6f, TitleColor);

    const char* Subtitle = (Outcome == MatchOutcome::Victory)
        ? "All enemy forces and structures destroyed!"
        : (Outcome == MatchOutcome::Defeat)
            ? "Your forces and command structures have been lost!"
            : "All combatants mutually eliminated!";
    D.DrawText(Subtitle, PanelX + 24.0f, PanelY + 62.0f, 1.15f, Render::Color(215, 230, 245, 255));

    const int TotalSecs = std::max(0, (int)ElapsedSeconds);
    const int Mins = TotalSecs / 60;
    const int Secs = TotalSecs % 60;
    char TimeBuffer[32];
    std::snprintf(TimeBuffer, sizeof(TimeBuffer), "Mission Time: %02d:%02d", Mins, Secs);
    D.DrawText(TimeBuffer, PanelX + 24.0f, PanelY + 92.0f, 1.1f, Render::Color(175, 200, 222, 255));

    const float ButtonW = 180.0f;
    const float ButtonH = 34.0f;
    const float ButtonY = PanelY + PanelH - 52.0f;

    const float RestartX = PanelX + 24.0f;
    const float MenuX = PanelX + PanelW - ButtonW - 24.0f;

    const float MouseX = (float)Core::Platform::GetMouseX();
    const float MouseY = (float)Core::Platform::GetMouseY();

    const bool RestartHover = MouseX >= RestartX && MouseX < RestartX + ButtonW &&
                              MouseY >= ButtonY && MouseY < ButtonY + ButtonH;
    const bool MenuHover = MouseX >= MenuX && MouseX < MenuX + ButtonW &&
                           MouseY >= ButtonY && MouseY < ButtonY + ButtonH;

    D.DrawRect(RestartX, ButtonY, ButtonW, ButtonH,
               RestartHover ? Render::Color(36, 80, 112, 240) : Render::Color(20, 38, 56, 220));
    D.DrawRectLines(RestartX, ButtonY, ButtonW, ButtonH,
                    RestartHover ? Render::Color(140, 215, 245, 255) : Render::Color(80, 125, 160, 220), 1.0f);
    D.DrawText("Restart [R]", RestartX + 32.0f, ButtonY + 9.0f, 1.15f, Render::Color(230, 242, 252, 255));

    D.DrawRect(MenuX, ButtonY, ButtonW, ButtonH,
               MenuHover ? Render::Color(36, 80, 112, 240) : Render::Color(20, 38, 56, 220));
    D.DrawRectLines(MenuX, ButtonY, ButtonW, ButtonH,
                    MenuHover ? Render::Color(140, 215, 245, 255) : Render::Color(80, 125, 160, 220), 1.0f);
    D.DrawText("Main Menu [ESC]", MenuX + 22.0f, ButtonY + 9.0f, 1.15f, Render::Color(230, 242, 252, 255));
}

} // namespace Game::Client::GameLoop


// === Consolidated from src/Main/CampaignAudio.cpp ===
#include "Main/CampaignAudio.h"

#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"
#include "Game/Campaign/CampaignController.h"

#include <algorithm>
#include <filesystem>
#include <vector>

namespace Game::Client::GameLoop
{

namespace
{

std::string ReplaceExtension(std::string Path, const char* Ext)
{
    const size_t Dot = Path.find_last_of('.');
    if (Dot == std::string::npos)
        return Path + Ext;
    Path.erase(Dot);
    Path += Ext;
    return Path;
}

} // namespace

std::string ResolveCampaignSoundFile(const std::string& MapPath, std::string SoundPath)
{
    if (SoundPath.empty())
        return {};

    std::replace(SoundPath.begin(), SoundPath.end(), '\\', '/');
    std::vector<std::filesystem::path> Bases;
    const std::filesystem::path MapBase = std::filesystem::path(MapPath).parent_path();
    if (!MapBase.empty())
    {
        Bases.push_back(MapBase);
        if (MapBase.filename() == "staredit")
            Bases.push_back(MapBase.parent_path());
    }
    Bases.push_back(std::filesystem::path("assets"));

    const std::string OggPath = ReplaceExtension(SoundPath, ".ogg");
    const std::string WavPath = ReplaceExtension(SoundPath, ".wav");
    const std::string Candidates[] = { SoundPath, OggPath, WavPath };
    for (const auto& Base : Bases)
    {
        for (const auto& Candidate : Candidates)
        {
            const std::filesystem::path Full = Base / std::filesystem::path(Candidate);
            if (std::filesystem::exists(Full))
                return Full.string();
        }
    }

    return {};
}

void PlayCampaignMessageAudio(
    Core::Audio::AudioSystem& Audio,
    const std::string& MapPath,
    const Game::Campaign::CampaignMessage& Message,
    float FallbackX,
    float FallbackY)
{
    std::string File = ResolveCampaignSoundFile(MapPath, Message.SoundPath);
    if (File.empty())
    {
        Core::Utils::Logger::Warning("CampaignAudio",
            "Unable to resolve campaign audio: " + Message.SoundPath);
        return;
    }

    Core::Math::Vec2 Pos = Message.Position;
    if (Pos.X == 0.0f && Pos.Y == 0.0f)
    {
        Pos.X = FallbackX;
        Pos.Y = FallbackY;
    }

    const int Channel = Audio.PlayFile(File, Pos.X, Pos.Y, 20,
        Core::Audio::SoundFlags::PerSound);
    if (Channel >= 0)
        Core::Utils::Logger::Info("CampaignAudio", "Playing " + File);
}

} // namespace Game::Client::GameLoop


// === Consolidated from src/Main/CampaignPortrait.cpp ===
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


// === Consolidated from src/Main/CampaignText.cpp ===
#include "Main/CampaignText.h"

#include <algorithm>

namespace Game::Client::GameLoop
{

std::string SanitizeCampaignText(std::string Text)
{
    for (char& Ch : Text)
    {
        if (Ch == '\r' || Ch == '\n' || Ch == '\t')
            Ch = ' ';
    }

    while (Text.find("  ") != std::string::npos)
        Text.erase(Text.find("  "), 1);
    return Text;
}

std::vector<std::string> WrapCampaignText(const std::string& Text, size_t MaxChars, size_t MaxLines)
{
    std::vector<std::string> Lines;
    std::string Clean = SanitizeCampaignText(Text);
    if (MaxChars == 0)
        MaxChars = 1;
    size_t Pos = 0;
    while (Pos < Clean.size() && Lines.size() < MaxLines)
    {
        size_t Remaining = Clean.size() - Pos;
        size_t Take = std::min(MaxChars, Remaining);
        size_t End = Pos + Take;
        if (End < Clean.size())
        {
            size_t Space = Clean.rfind(' ', End);
            if (Space != std::string::npos && Space > Pos + MaxChars / 2)
                End = Space;
            else
            {
                // Prevent slicing in the middle of a multi-byte UTF-8 sequence
                while (End > Pos && (static_cast<uint8_t>(Clean[End]) & 0xC0) == 0x80)
                    --End;
            }
        }

        if (End <= Pos)
        {
            End = Pos + 1;
            while (End < Clean.size() &&
                   (static_cast<uint8_t>(Clean[End]) & 0xC0) == 0x80)
                ++End;
        }

        std::string Line = Clean.substr(Pos, End - Pos);
        while (!Line.empty() && Line.front() == ' ')
            Line.erase(Line.begin());
        while (!Line.empty() && Line.back() == ' ')
            Line.pop_back();
        if (!Line.empty())
            Lines.push_back(Line);
        Pos = End;
        while (Pos < Clean.size() && Clean[Pos] == ' ')
            ++Pos;
    }

    if (Pos < Clean.size() && !Lines.empty())
    {
        std::string& Last = Lines.back();
        if (MaxChars > 3 && Last.size() + 3 > MaxChars && Last.size() > 3)
        {
            size_t Cut = MaxChars - 3;
            while (Cut > 0 && (static_cast<uint8_t>(Last[Cut]) & 0xC0) == 0x80)
                --Cut;
            Last.resize(Cut);
            Last += "...";
        }
        else if (MaxChars <= 3)
        {
            Last += "...";
        }
    }
    return Lines;
}

const char* CampaignMessageKind(uint8_t ActionID)
{
    switch (ActionID)
    {
        case 7: return "TRANSMISSION";
        case 8: return "AUDIO";
        case 29: return "PORTRAIT";
        case 41: return "SCENARIO";
        default: return "BRIEFING";
    }
}

} // namespace Game::Client::GameLoop
