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
                // Landing is immediate at the building's current position.
                // It is not a world-target command.
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::None;
                mState.CommandPanel.PendingCommandId = HudCommandId::None;
                mState.ActiveCommand = -1;
                break;
            case HudCommandId::Rally:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Rally;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Scan:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Scan;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Load:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Load;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::RechargeShields:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::RechargeShields;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::PsionicStorm:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::PsionicStorm;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::YamatoGun:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::YamatoGun;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Lockdown:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Lockdown;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::EMPShockwave:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::EMPShockwave;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Hallucination:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Hallucination;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::StasisField:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::StasisField;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Recall:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Recall;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::DarkSwarm:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::DarkSwarm;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Plague:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Plague;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Ensnare:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Ensnare;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::SpawnBroodling:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::SpawnBroodling;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Parasite:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Parasite;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Consume:
                mState.CommandPanel.PendingTargetModeValue = PendingTargetMode::Consume;
                mState.CommandPanel.PendingCommandId = Button.CommandId;
                break;
            default: break;
        }
    }
}

} // namespace Game::Client::UI
