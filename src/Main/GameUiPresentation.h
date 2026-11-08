#pragma once

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Main/CampaignRuntime.h"
// === Consolidated from src/Main/GameCampaignOverlay.h ===
#include "Client/ClientRuntime.h"
#include "Main/CampaignPresentation.h"
#include "Main/CampaignRuntime.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline void RenderActiveCampaignOverlay(CampaignRuntime& Campaign,
                                        UI::Hud& Hud,
                                        Game::Client::ClientRuntime& Client,
                                        int ViewportWidth,
                                        int ViewportHeight)
{
    if (!Campaign.IsActive())
        return;

    RenderCampaignOverlay(
        Campaign.Controller(),
        Hud,
        Client.GetDataAssets(),
        Client.GetArchiveSource(),
        Client.GetUIAssets(),
        ViewportWidth,
        ViewportHeight);
}

inline bool IsAiObserverMatchActive()
{
    const char* Value = std::getenv("STARCRAFT_AI_OBSERVER_MATCH");
    return Value && Value[0] != '\0' && Value[0] != '0';
}

inline void RenderActiveCampaignOverlay(CampaignRuntime& Campaign,
                                        UI::Hud& Hud,
                                        Game::Client::ClientRuntime& Client,
                                        GameSession& Session,
                                        int ViewportWidth,
                                        int ViewportHeight)
{
    if (IsAiObserverMatchActive())
    {
        if (Session.IsMatchOver())
        {
            RenderMatchGameOverOverlay(
                Session.GetMatchOutcome(),
                ViewportWidth,
                ViewportHeight,
                Session.GetMatchElapsedSeconds());
        }
        return;
    }

    if (Campaign.IsActive())
    {
        RenderCampaignOverlay(
            Campaign.Controller(),
            Hud,
            Client.GetDataAssets(),
            Client.GetArchiveSource(),
            Client.GetUIAssets(),
            ViewportWidth,
            ViewportHeight);
    }
    else if (Session.IsMatchOver())
    {
        RenderMatchGameOverOverlay(
            Session.GetMatchOutcome(),
            ViewportWidth,
            ViewportHeight,
            Session.GetMatchElapsedSeconds());
    }
}

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameHudPresentation.h ===
#include "Client/ClientRuntime.h"
#include "Client/WorldEffects.h"
#include "Game/Components.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Main/GameLoop/CursorTypes.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

inline uint32_t PackHudColor(Core::Color Color)
{
    return (static_cast<uint32_t>(Color.A) << 24) |
        (static_cast<uint32_t>(Color.R) << 16) |
        (static_cast<uint32_t>(Color.G) << 8) |
        static_cast<uint32_t>(Color.B);
}

inline Core::Color MinimapRelationshipColor(
    const Game::Components::PlayerComponent* Player,
    int LocalPlayerID,
    const Game::Systems::FogOfWarSystem* Fog)
{
    if (!Player || Player->PlayerID < 0 || Player->PlayerID >= 8)
        return Core::Color(190, 190, 150, 255);
    if (Player->PlayerID == LocalPlayerID)
        return Core::Color(80, 200, 255, 255);
    if (Player->IsRescuePassive || (Fog && Fog->AreAllied(LocalPlayerID, Player->PlayerID)))
        return Core::Color(80, 220, 120, 255);
    return Core::Color(235, 64, 64, 255);
}

inline void SyncMinimapDotsToHud(UI::Hud& Hud,
                                 Core::ECS::EntityManager& Entities,
                                 int LocalPlayerID,
                                 const Game::Systems::FogOfWarSystem* Fog)
{
    std::vector<UI::HudMinimapDot> Dots;
    Entities.ForEachEntityWith<Game::Components::TransformComponent,
                               Game::Components::RenderComponent,
                               Game::Components::UnitComponent>([&](int EntityId)
    {
        auto* Transform = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        auto* Render = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
        if (!Transform || !Render || !Unit || !Render->Visible || Unit->HitPoints <= 0)
            return;

        const auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityId);
        const int PlayerID = Player ? Player->PlayerID : 11;
        const bool IsMineral = (Unit->ChkUnitID >= 176 && Unit->ChkUnitID <= 178);
        const bool IsGeyser = (Unit->ChkUnitID == 188);
        const bool IsResource = IsMineral || IsGeyser;
        const auto* Build = Entities.GetComponent<Game::Components::BuildingComponent>(EntityId);
        const bool IsBuilding = (Build != nullptr && Build->IsBuilding) || IsBuildingChkId(Unit->ChkUnitID);

        if (Fog)
        {
            const int TileX = (int)(Transform->Position.X / 32.0f);
            const int TileY = (int)(Transform->Position.Y / 32.0f);

            if (IsResource)
            {
                if (!Fog->IsExplored(TileX, TileY, LocalPlayerID))
                    return;
            }
            else if (PlayerID == LocalPlayerID || Fog->AreAllied(LocalPlayerID, PlayerID))
            {
                // Self or Allied units always visible
            }
            else
            {
                // Enemy
                if (IsBuilding)
                {
                    if (!Fog->IsExplored(TileX, TileY, LocalPlayerID))
                        return;
                }
                else
                {
                    // Enemy mobile unit: visible only in active sight
                    if (!Fog->IsVisible(TileX, TileY, LocalPlayerID))
                        return;
                }
            }
        }

        Core::Color DotColor;
        uint8_t DotW = 2;
        uint8_t DotH = 2;

        if (IsMineral)
        {
            DotColor = Core::Color(0, 228, 252, 255); // Cyan
            DotW = 2;
            DotH = 2;
        }
        else if (IsGeyser)
        {
            DotColor = Core::Color(0, 228, 0, 255); // Green
            DotW = 3;
            DotH = 3;
        }
        else
        {
            if (Hud.GetMinimapUseAllianceColors())
            {
                if (PlayerID == LocalPlayerID || (Fog && Fog->AreAllied(LocalPlayerID, PlayerID)))
                    DotColor = Core::Color(80, 220, 120, 255);
                else
                    DotColor = Core::Color(235, 64, 64, 255);
            }
            else
            {
                DotColor = MinimapRelationshipColor(Player, LocalPlayerID, Fog);
            }

            if (IsBuilding)
            {
                // Town halls (Command Center, Nexus, Hatchery/Lair/Hive) get 5x5 footprint
                const bool IsTownHall = (Unit->ChkUnitID == 106 || Unit->ChkUnitID == 154 ||
                                         Unit->ChkUnitID == 131 || Unit->ChkUnitID == 132 || Unit->ChkUnitID == 133);
                DotW = IsTownHall ? 5 : 4;
                DotH = IsTownHall ? 5 : 4;
            }
        }

        Dots.push_back({Transform->Position, PackHudColor(DotColor), DotW, DotH});
    });
    Hud.SetMinimapDots(Dots);
}

inline void SyncCampaignPingsToHud(UI::Hud& Hud,
                                   Game::Client::ClientRuntime& Client,
                                   int MapWidthTiles,
                                   int MapHeightTiles)
{
    Hud.SetMinimapMapSize(MapWidthTiles, MapHeightTiles);

    std::vector<UI::HudMinimapPing> CampaignPings;
    const auto& ClientPings = Client.GetCampaignPings();
    CampaignPings.reserve(ClientPings.size());
    for (const auto& Ping : ClientPings)
        CampaignPings.push_back({Ping.Position, Ping.TimeRemaining});
    Hud.SetCampaignPings(CampaignPings);
}

inline void RenderNativeHudFrame(UI::Hud& Hud,
                                 GameSession& Session,
                                 Game::Client::ClientRuntime& Client,
                                 int ViewportWidth,
                                 int ViewportHeight)
{
    float CamX = 0.0f;
    float CamY = 0.0f;
    float CamW = 0.0f;
    float CamH = 0.0f;
    Client.GetVisibleWorldRect(CamX, CamY, CamW, CamH);

    Hud.SetMatchElapsedSeconds(Session.GetMatchElapsedSeconds());
    Hud.SetMinimapViewportRect(CamX, CamY, CamW, CamH);
    Hud.UpdateMinimapTerrain(
        &Session.GetEcsMap(),
        &Session.GetMapData(),
        Session.GetFogSystem(),
        Session.GetLocalPlayerID());

    Hud.UpdateContext(Session.Entities(), Client.GetSelectedIds());
    SyncCampaignPingsToHud(Hud, Client, Session.GetMapData().Width, Session.GetMapData().Height);
    SyncMinimapDotsToHud(
        Hud, Session.Entities(), Session.GetLocalPlayerID(), Session.GetFogSystem());
    Hud.Draw(Session.Entities(),
             Client.GetSelectedIds(),
             Session.GetMapData().Width,
             Session.GetMapData().Height,
             ViewportWidth,
             ViewportHeight);
}

} // namespace Game::Client::GameLoop

#include "Main/GameMenuPresentation.h"
#include "Main/GameMenuRuntime.h"
#include "Main/GameSession.h"
#include "Render/Draw2D.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{
inline void RenderGameplayWorldFrame(ClientRuntime& Client, GameSession& Session, float InterpAlpha = 1.0f)
{
    Client.BeginWorldFrame();

    if (Session.IsMapLoaded())
    {
        Client.RenderWorld(Session.GetMapData(), Session.Entities(), Session.IsMapLoaded(), InterpAlpha);
        Client.RenderWorldEffects(Render::g_Draw2D);
    }

    Client.EndWorldFrame();
}


inline void RenderGameplayUiFrame(
    ClientRuntime& Client,
    GameSession& Session,
    CampaignRuntime& Campaign,
    GameMenuRuntime& Menu,
    UI::Hud& Hud,
    bool DrawNativeHud)
{
    const int ViewportWidth = Core::Platform::GetWindowWidth();
    const int ViewportHeight = Core::Platform::GetWindowHeight();

    Render::g_Draw2D.BeginFrameScreen();
    if (DrawNativeHud)
        RenderNativeHudFrame(Hud, Session, Client, ViewportWidth, ViewportHeight);

    RenderHudMenuButton(ViewportWidth, ViewportHeight, Menu.IsOpen());

    Client.RenderSelectionDragOverlay();

    RenderActiveCampaignOverlay(Campaign, Hud, Client, Session, ViewportWidth, ViewportHeight);

    const int MouseX = (int)Core::Platform::GetMouseX();
    const int MouseY = (int)Core::Platform::GetMouseY();

    RenderGameMenuPresentationLayer(
        ViewportWidth,
        ViewportHeight,
        MouseX,
        MouseY,
        Menu,
        Campaign.IsScenarioPaused());

    Core::ECS::EntityManager* EcsEM = Session.IsEcsInitialized() ? &Session.Entities() : nullptr;
    Client.RenderCursor(MouseX, MouseY, Hud, EcsEM, Session.IsEcsInitialized(), Render::g_Draw2D);

    Render::g_Draw2D.EndFrame();
}

} // namespace Game::Client::GameLoop
