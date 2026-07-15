#pragma once

#include "Client/ClientRuntime.h"
#include "Client/WorldEffects.h"
#include "Game/Components.h"
#include "Game/Systems/FogOfWarSystem.h"
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
        const Core::Color DotColor =
            MinimapRelationshipColor(Player, LocalPlayerID, Fog);
        Dots.push_back({Transform->Position, PackHudColor(DotColor)});
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
