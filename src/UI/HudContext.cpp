#include "UI/Hud.h"

#include <vector>

#include "Game/Components.h"
#include "Game/Races/RaceDef.h"

namespace Game::Client::UI
{

namespace
{
    int FindFirstSelectedEntityId(const std::vector<int>& SelectedIds)
    {
        return SelectedIds.empty() ? -1 : SelectedIds.front();
    }

    int FindSelectedChkUnitId(Core::ECS::EntityManager& Entities, int SelectedEntityId)
    {
        if (SelectedEntityId < 0)
            return -1;
        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(SelectedEntityId);
        return Unit ? Unit->ChkUnitID : -1;
    }

    Game::Components::Race FindSelectedRace(Core::ECS::EntityManager& Entities,
                                            int SelectedEntityId,
                                            int SelectedChkUnitId)
    {
        if (SelectedEntityId >= 0)
        {
            if (auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(SelectedEntityId))
                return Unit->UnitRace;
        }
        return Game::Races::GetRaceByChkUnitID(
            SelectedChkUnitId, Game::Components::Race::Terran);
    }
}

void Hud::UpdateContext(Core::ECS::EntityManager& Entities,
                        const std::vector<int>& SelectedIds)
{
    using ViewModel::HudDirtyFlags;
    using ViewModel::HasDirtyFlag;

    const HudDirtyFlags Dirty = mViewModel.SyncWithWorld(
        Entities,
        SelectedIds,
        mState.CommandPanel.PanelPage,
        mState.ActiveCommand);

    const bool NeedCommandRebuild =
        !mUseReactiveBinding ||
        HasDirtyFlag(Dirty, HudDirtyFlags::Selection | HudDirtyFlags::CommandCard);
    const bool NeedInfoRebuild =
        !mUseReactiveBinding ||
        HasDirtyFlag(Dirty, HudDirtyFlags::Selection | HudDirtyFlags::InfoPanel);

    if (!NeedCommandRebuild && !NeedInfoRebuild)
    {
        mViewModel.RecordCommandCardSkip();
        mViewModel.RecordInfoPanelSkip();
        return;
    }

    mState.Selection.SelectedCount = (int)SelectedIds.size();

    if (SelectedIds != mState.Selection.LastSelectedIds)
    {
        CancelPendingTarget();
        mState.Selection.LastSelectedIds = SelectedIds;
    }

    int SelectedEntityId = FindFirstSelectedEntityId(SelectedIds);
    int SelectedChkUnitId = FindSelectedChkUnitId(Entities, SelectedEntityId);

    if (SelectedEntityId < 0)
    {
        if (NeedCommandRebuild)
        {
            ResetButtons();
            mState.ActiveCommand = -1;
            mState.CommandPanel.PanelPage = PageNone;
            mState.CommandPanel.PanelCategory = HudPanelCategory::None;
            mViewModel.RecordCommandCardRebuild();
        }
        else
        {
            mViewModel.RecordCommandCardSkip();
        }

        if (NeedInfoRebuild)
        {
            ResetInfoPanel();
            mViewModel.RecordInfoPanelRebuild();
        }
        else
        {
            mViewModel.RecordInfoPanelSkip();
        }

        mState.Selection.SelectedEntityId = -1;
        mState.Selection.SelectedChkUnitId = -1;
        mState.Selection.SelectedTerranDefId = -1;
        mViewModel.ClearDirtyFlags(
            HudDirtyFlags::Selection | HudDirtyFlags::CommandCard | HudDirtyFlags::InfoPanel);
        return;
    }

    const int SelectedTerranDefId =
        ResolveSelectedTerranDefId(SelectedEntityId, SelectedChkUnitId, &Entities);

    if (SelectedEntityId != mState.Selection.SelectedEntityId ||
        SelectedChkUnitId != mState.Selection.SelectedChkUnitId ||
        SelectedTerranDefId != mState.Selection.SelectedTerranDefId)
    {
        mState.Selection.SelectedEntityId = SelectedEntityId;
        mState.Selection.SelectedChkUnitId = SelectedChkUnitId;
        mState.Selection.SelectedTerranDefId = SelectedTerranDefId;
        mState.CommandPanel.PanelPage = (SelectedTerranDefId >= 0)
            ? GetDefaultTerranPageForDef(SelectedTerranDefId)
            : GetDefaultPageForSelection(SelectedChkUnitId);
        mViewModel.CommandPanelPage.Set(mState.CommandPanel.PanelPage);
    }

    if (NeedCommandRebuild)
    {
        ResetButtons();
        if (SelectedTerranDefId >= 0)
        {
            mState.CommandPanel.PanelCategory = HudPanelCategory::Terran;
            BuildTerranPage(SelectedEntityId, SelectedTerranDefId, &Entities);
        }
        else
        {
            switch (FindSelectedRace(Entities, SelectedEntityId, SelectedChkUnitId))
            {
                case Game::Components::Race::Zerg:
                    mState.CommandPanel.PanelCategory = HudPanelCategory::Zerg;
                    BuildZergPage(SelectedEntityId, SelectedChkUnitId, &Entities);
                    break;

                case Game::Components::Race::Protoss:
                    mState.CommandPanel.PanelCategory = HudPanelCategory::Protoss;
                    BuildProtossPage(SelectedEntityId, SelectedChkUnitId, &Entities);
                    break;

                default:
                    mState.CommandPanel.PanelCategory = HudPanelCategory::None;
                    break;
            }
        }
        mViewModel.RecordCommandCardRebuild();
    }
    else
    {
        mViewModel.RecordCommandCardSkip();
    }

    if (NeedInfoRebuild)
    {
        ResetInfoPanel();
        UpdateInfoPanel(Entities, SelectedEntityId, SelectedChkUnitId, SelectedTerranDefId, &Entities);
        mViewModel.RecordInfoPanelRebuild();
    }
    else
    {
        mViewModel.RecordInfoPanelSkip();
    }

    mViewModel.ClearDirtyFlags(
        HudDirtyFlags::Selection | HudDirtyFlags::CommandCard | HudDirtyFlags::InfoPanel);
}

} // namespace Game::Client::UI
