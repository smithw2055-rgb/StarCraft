#include "UI/Hud.h"

#include "UI/HudText.h"

namespace Game::Client::UI
{

void Hud::DrawInfoPanel(Core::ECS::EntityManager& Entities,
                        const HudLayout& Layout,
                        bool HasModularConsole,
                        const Resource::Graphics::GpuImage& AggIcons)
{
    if (!mState.InfoPanel.HasSelection)
        return;

    const bool ScvPanel =
        mState.InfoPanel.SelectedCount <= 1 &&
        mState.Selection.SelectedTerranDefId == Game::Races::GetUnitDefID("SCV") &&
        !mState.InfoPanel.IsBuilding &&
        !mState.InfoPanel.IsResource;

    DrawInfoPanelFrame(Layout, HasModularConsole);
    DrawInfoPanelPortraits(Entities, Layout, HasModularConsole);

    if (ScvPanel)
    {
        DrawScvInfoPanel(Layout, HasModularConsole, AggIcons);
    }
    else
    {
        DrawStandardInfoPanel(Layout, HasModularConsole, AggIcons);
    }
}

} // namespace Game::Client::UI






