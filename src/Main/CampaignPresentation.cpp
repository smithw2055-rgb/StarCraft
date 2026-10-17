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
