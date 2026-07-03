#include "UI/Hud.h"

#include <vector>

#include "Game/Components.h"

namespace Game::Client::UI
{

namespace
{
    constexpr int ZergDrone = 41;
    constexpr int ZergHatchery = 86;
    constexpr int ZergLair = 87;
    constexpr int ZergHive = 88;

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
}

void Hud::UpdateContext(Core::ECS::EntityManager& Entities,
                        const std::vector<int>& SelectedIds)
{
    ResetButtons();
    ResetInfoPanel();
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
        mState.ActiveCommand = -1;
        mState.Selection.SelectedEntityId = -1;
        mState.Selection.SelectedChkUnitId = -1;
        mState.Selection.SelectedTerranDefId = -1;
        mState.CommandPanel.PanelPage = PageNone;
        mState.CommandPanel.PanelCategory = HudPanelCategory::None;
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
    }

    if (SelectedTerranDefId >= 0)
    {
        mState.CommandPanel.PanelCategory = HudPanelCategory::Terran;
        BuildTerranPage(SelectedEntityId, SelectedTerranDefId, &Entities);
        UpdateInfoPanel(Entities, SelectedEntityId, SelectedChkUnitId, SelectedTerranDefId, &Entities);
        return;
    }

    switch (SelectedChkUnitId)
    {
        case ZergDrone:
        case ZergHatchery:
        case ZergLair:
        case ZergHive:
            mState.CommandPanel.PanelCategory = HudPanelCategory::Zerg;
            BuildZergPage(SelectedEntityId, SelectedChkUnitId, &Entities);
            break;

        default:
            mState.CommandPanel.PanelCategory = HudPanelCategory::Protoss;
            BuildProtossPage(SelectedEntityId, SelectedChkUnitId, &Entities);
            break;
    }

    UpdateInfoPanel(Entities, SelectedEntityId, SelectedChkUnitId, SelectedTerranDefId, &Entities);
}

} // namespace Game::Client::UI