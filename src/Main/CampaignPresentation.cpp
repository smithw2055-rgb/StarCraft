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
            const float ObjX = 12.0f;
            const float ObjY = (float)Hud.GetResourceBarHeight() + 12.0f;
            const float ObjW = std::min(460.0f, (float)ViewportWidth - 24.0f);
            const float ObjH = std::min(138.0f, 34.0f + Objectives.size() * 17.0f);
            Render::g_Draw2D.DrawRect(ObjX, ObjY, ObjW, ObjH, Render::Color(3, 8, 14, 170));
            Render::g_Draw2D.DrawRectLines(ObjX, ObjY, ObjW, ObjH, Render::Color(72, 112, 140, 210), 1.0f);
            Render::g_Draw2D.DrawText(C.GetMissionName(), ObjX + 12.0f, ObjY + 9.0f, 11.0f,
                Render::Color(235, 224, 170, 255));

            float OY = ObjY + 31.0f;
            size_t Shown = 0;
            for (auto& O : Objectives)
            {
                if (Shown++ >= 6)
                    break;
                std::string Text = O.Complete ? "[X] " : "[ ] ";
                Text += SanitizeCampaignText(O.Description);
                if (Text.size() > 58)
                    Text = Text.substr(0, 55) + "...";
                Render::g_Draw2D.DrawText(Text, ObjX + 12.0f, OY, 1.05f,
                    O.Complete ? Render::Color(100, 220, 100, 255) : Render::Color(220, 220, 200, 255));
                OY += 17.0f;
            }
        }

        const auto& Messages = C.GetMessages();
        for (auto It = Messages.rbegin(); It != Messages.rend(); ++It)
        {
            const auto& M = *It;
            if (M.Text.empty())
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
            if (M.PortraitUnitID != 0)
                PortraitKey = CampaignPortraitKeyFromUnitID(DataAssets, ArchiveSource, M.PortraitUnitID);
            if (PortraitKey.empty())
                PortraitKey = CampaignPortraitKeyFromText(M.Text);
            if (PortraitKey.empty())
                PortraitKey = CampaignAdvisorPortraitKey(M.PlayerID);

            const auto* Clip = UIAssets.GetPortraitClip(PortraitKey);
            const Resource::Graphics::PortraitFrame* Frame =
                Clip ? CampaignPortraitFrameAt(*Clip, Core::Platform::GetAbsoluteTime()) : nullptr;
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
            const auto Lines = WrapCampaignText(M.Text, 74, 3);
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
        Render::g_Draw2D.DrawRect(0, 0, (float)ViewportWidth, (float)ViewportHeight, Render::Color(0, 80, 0, 160));
        Render::g_Draw2D.DrawText("VICTORY", (float)(ViewportWidth / 2 - 60), (float)(ViewportHeight / 2 - 20), 4.0f,
            Render::Color(100, 255, 100, 255));
        Render::g_Draw2D.DrawText("Press R to restart", (float)(ViewportWidth / 2 - 60), (float)(ViewportHeight / 2 + 20), 2.0f,
            Render::Color(200, 220, 200, 255));
        if (Core::Platform::IsKeyPressed(82)) C.Reset();
    }
    if (C.GetState() == Game::Campaign::CampaignState::Defeat)
    {
        Render::g_Draw2D.DrawRect(0, 0, (float)ViewportWidth, (float)ViewportHeight, Render::Color(80, 0, 0, 160));
        Render::g_Draw2D.DrawText("DEFEAT", (float)(ViewportWidth / 2 - 60), (float)(ViewportHeight / 2 - 20), 4.0f,
            Render::Color(255, 80, 80, 255));
        Render::g_Draw2D.DrawText("Press R to restart", (float)(ViewportWidth / 2 - 60), (float)(ViewportHeight / 2 + 20), 2.0f,
            Render::Color(220, 200, 200, 255));
        if (Core::Platform::IsKeyPressed(82)) C.Reset();
    }
}

} // namespace Game::Client::GameLoop
