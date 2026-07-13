#include "UI/Hud.h"

#include "Game/Races/RaceDef.h"

#include <cstddef>

namespace Game::Client::UI
{

namespace
{
    bool IsGasBuildingDefID(int DefID)
    {
        return DefID == Game::Races::GetUnitDefID("Refinery") ||
               DefID == Game::Races::GetUnitDefID("Extractor") ||
               DefID == Game::Races::GetUnitDefID("Assimilator");
    }
}

void Hud::CancelPendingTarget()
{
    mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::None;
    mState.CommandPanel.PendingBuildDefID = -1;
    mState.CommandPanel.PendingCommandId = HudCommandId::None;
    mState.ActiveCommand = -1;
}

void Hud::OnButtonInvoked(int ButtonIndex)
{
    if (ButtonIndex < 0 || ButtonIndex >= (int)mState.CommandPanel.ButtonDefs.size())
        return;
    auto& Button = mState.CommandPanel.ButtonDefs[(size_t)ButtonIndex];
    if (Button.Label.empty())
        return;
    if (!Button.Enabled)
        return;

    CancelPendingTarget();
    mState.ActiveCommand = ButtonIndex;

    if (Button.Kind == ActionKind::OpenSubmenu || Button.Kind == ActionKind::Back)
    {
        mState.CommandPanel.PanelPage = Button.NextMenuPage;
        mState.ActiveCommand = -1;
        return;
    }

    if (Button.Kind == ActionKind::Cancel)
    {
        mState.ActiveCommand = -1;
        return;
    }

    if (Button.Kind == ActionKind::Build)
    {
        auto Def = Game::Races::GetUnitDefByID(Button.TargetUnitDefID);
        if (!Def.IsAddon)
        {
            mState.CommandPanel.PendingBuildDefID = Button.TargetUnitDefID;
            mState.CommandPanel.PendingTargetModeValue =
                IsGasBuildingDefID(Button.TargetUnitDefID)
                    ? PendingTargetMode::BuildRefinery
                    : PendingTargetMode::BuildPlacement;
        }
        return;
    }

    if (Button.Kind == ActionKind::Command)
    {
        switch (Button.CommandId)
        {
            case HudCommandId::Move:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Move;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::AttackMove:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::AttackMove;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Patrol:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Patrol;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Follow:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Follow;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Harvest:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Harvest;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Land:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Land;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            default: break;
        }
    }
}

} // namespace Game::Client::UI
