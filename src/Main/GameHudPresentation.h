#pragma once

#include "Client/ClientRuntime.h"
#include "Client/WorldEffects.h"
#include "Game/Components.h"
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

inline void SyncMinimapDotsToHud(UI::Hud& Hud, Core::ECS::EntityManager& Entities)
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
        const Core::Color DotColor = Player
            ? Player->PlayerColor
            : Core::Color(220, 60, 60, 255);
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
    SyncMinimapDotsToHud(Hud, Session.Entities());
    Hud.Draw(Session.Entities(),
             Client.GetSelectedIds(),
             Session.GetMapData().Width,
             Session.GetMapData().Height,
             ViewportWidth,
             ViewportHeight);
}

} // namespace Game::Client::GameLoop
