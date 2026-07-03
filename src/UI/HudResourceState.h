#pragma once

namespace Game::Client::UI
{

struct HudResourceState
{
    int MineralCount = 50;
    int GasCount = 0;
    int SupplyUsed = 0;
    int SupplyMax = 200;
};

} // namespace Game::Client::UI
