#pragma once

namespace Game::Client::UI
{
class Hud;
struct HudResourceState;
}

namespace Game::Client::GameLoop
{

void ResetPlayerResourceState();
UI::HudResourceState CaptureHudResourceState(const UI::Hud& Hud);
void RestorePlayerResourcesFromHudState(UI::Hud& Hud, const UI::HudResourceState& State, int PlayerId = 0);
void SyncHudFromPlayerResources(UI::Hud& Hud, int PlayerId = 0);

} // namespace Game::Client::GameLoop
