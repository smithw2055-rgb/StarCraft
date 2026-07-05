#include "UI/Hud.h"

#include <cstddef>

namespace Game::Client::UI
{

CmdButtonDef::CmdButtonDef()
    : ResearchId(Game::Systems::TerranResearchId::None)
{
}

void Hud::ResetButtons()
{
    for (auto& Button : mState.CommandPanel.ButtonDefs)
        Button = {};
}

void Hud::SetButton(int Index,
                    const std::string& Label,
                    ActionKind Kind,
                    int TargetUnitDefID,
                    HudCommandId CommandId,
                    Game::Systems::TerranResearchId ResearchId,
                    int IconIndex,
                    bool Enabled,
                    int NextMenuPage,
                    HudVisualId VisualId,
                    int MineralCost,
                    int GasCost,
                    int SupplyCost)
{
    if (Index < 0 || Index >= (int)mState.CommandPanel.ButtonDefs.size())
        return;
    mState.CommandPanel.ButtonDefs[(size_t)Index].Label = Label;
    mState.CommandPanel.ButtonDefs[(size_t)Index].Kind = Kind;
    mState.CommandPanel.ButtonDefs[(size_t)Index].TargetUnitDefID = TargetUnitDefID;
    mState.CommandPanel.ButtonDefs[(size_t)Index].CommandId = CommandId;
    mState.CommandPanel.ButtonDefs[(size_t)Index].ResearchId = ResearchId;
    mState.CommandPanel.ButtonDefs[(size_t)Index].IconIndex = IconIndex;
    mState.CommandPanel.ButtonDefs[(size_t)Index].Enabled = Enabled;
    mState.CommandPanel.ButtonDefs[(size_t)Index].NextMenuPage = NextMenuPage;
    mState.CommandPanel.ButtonDefs[(size_t)Index].VisualId = VisualId;
    mState.CommandPanel.ButtonDefs[(size_t)Index].MineralCost = MineralCost;
    mState.CommandPanel.ButtonDefs[(size_t)Index].GasCost = GasCost;
    mState.CommandPanel.ButtonDefs[(size_t)Index].SupplyCost = SupplyCost;
}

} // namespace Game::Client::UI
