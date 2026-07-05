#include "MainGui.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Core/ECS/EntityManager.h"
#include "UI/Hud.h"
#include "UI/HudMinimapGeometry.h"
#include "UI/MainGuiSelectionPresentation.h"
#include "UI/HudPortrait.h"
#include "UI/HudText.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <cmath>

#include "third_party/sokol/sokol_app.h"

namespace Game::Client::UI
{

bool MainGui::Init(const std::string& AssetRoot, int ScreenW, int ScreenH,
                         const std::string& XamlPath)
{
    AssetRoot_ = AssetRoot;
    return Host_.Initialize(AssetRoot, ScreenW, ScreenH, XamlPath);
}

void MainGui::Shutdown()
{
    Host_.Shutdown();
}

void MainGui::Update(Core::ECS::EntityManager& Entities,
                            const std::vector<int>& SelectedIds,
                            const Hud& HudState,
                            double Time)
{
    Host_.SetResourceIconUris(HudState.GetLegacyIconUri(0),
                              HudState.GetLegacyIconUri(2),
                              HudState.GetLegacyIconUri(3));
    Host_.SetResources(Minerals_, Gas_, SupplyUsed_, SupplyMax_, TimeText_);
    const auto& MinimapState = HudState.GetState().Minimap;
    Host_.SetMinimapDots(MinimapState.UnitDots);
    Host_.SetCampaignPings(MinimapState.CampaignPings,
                           MinimapState.MapWidth,
                           MinimapState.MapHeight);

    UpdateSelectionInfo(Entities, SelectedIds, HudState);

    Host_.Update(Time);
}

void MainGui::RenderOffscreen()
{
    Host_.RenderOffscreen();
}

void MainGui::Render()
{
    Host_.Render();
}

void MainGui::Resize(int Width, int Height)
{
    Host_.Resize(Width, Height);
}

bool MainGui::HandleEvent(const void* Event)
{
    if (!Event)
        return false;

    const sapp_event* E = static_cast<const sapp_event*>(Event);
    const float X = E->mouse_x;
    const float Y = E->mouse_y;
    LastMouseX_ = X;
    LastMouseY_ = Y;
    bool Handled = Host_.HandleEvent(Event);

    switch (E->type)
    {
    case SAPP_EVENTTYPE_MOUSE_MOVE:
    {
        HoveredCommandIndex_ = Host_.HitCommandButton(X, Y);
        Host_.SetHoveredCommand(HoveredCommandIndex_);
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_DOWN:
    {
        if (E->mouse_button == SAPP_MOUSEBUTTON_LEFT)
        {
            const int Hit = Host_.HitCommandButton(X, Y);
            PressedCommandIndex_ = Hit;
            Host_.SetPressedCommand(Hit);
            Handled = Handled || Hit >= 0;
        }
        break;
    }
    case SAPP_EVENTTYPE_MOUSE_UP:
    {
        if (E->mouse_button == SAPP_MOUSEBUTTON_LEFT)
        {
            const int Hit = Host_.HitCommandButton(X, Y);
            if (Hit >= 0 && Hit == PressedCommandIndex_)
                ClickedCommandIndex_ = Hit;
            PressedCommandIndex_ = -1;
            Host_.SetPressedCommand(-1);
            Handled = Handled || Hit >= 0;
        }
        break;
    }
    default:
        break;
    }

    return Handled;
}

int MainGui::HitCommandButton(float X, float Y) const
{
    return Host_.HitCommandButton(X, Y);
}

int MainGui::HitMinimapButton(float X, float Y) const
{
    return Host_.HitMinimapButton(X, Y);
}

int MainGui::HitMultiSelection(float X, float Y) const
{
    return Host_.HitMultiSelection(X, Y);
}

bool MainGui::HitMinimap(float X, float Y) const
{
    return Host_.HitMinimap(X, Y);
}

bool MainGui::CheckMinimapClick(float X, float Y,
                                int MapWidthTiles,
                                int MapHeightTiles,
                                float& OutWorldX,
                                float& OutWorldY) const
{
    const ScreenRect Rect = Host_.GetMinimapRect();
    if (!Rect.IsValid())
        return false;

    const RectI MinimapFrame{
        static_cast<int>(std::lround(Rect.X)),
        static_cast<int>(std::lround(Rect.Y)),
        std::max(1, static_cast<int>(std::lround(Rect.W))),
        std::max(1, static_cast<int>(std::lround(Rect.H)))
    };
    return MinimapScreenToWorld(MinimapFrame,
                                MapWidthTiles,
                                MapHeightTiles,
                                static_cast<int>(std::lround(X)),
                                static_cast<int>(std::lround(Y)),
                                OutWorldX,
                                OutWorldY);
}

bool MainGui::IsPointInHud(float X, float Y) const
{
    if (!Host_.IsActive()) return false;

    if (Host_.HitMinimap(X, Y)) return true;
    if (Host_.HitCommandButton(X, Y) >= 0) return true;
    if (Host_.HitMinimapButton(X, Y) >= 0) return true;
    if (Host_.HitMultiSelection(X, Y) >= 0) return true;

    const float HudTop = static_cast<float>(std::max(0, Host_.GetViewportHeight() - Host_.GetHudHeight()));
    return Y >= HudTop;
}

void MainGui::SetResources(int Minerals, int Gas, int SupplyUsed, int SupplyMax,
                                 const std::string& TimeText)
{
    Minerals_ = Minerals;
    Gas_ = Gas;
    SupplyUsed_ = SupplyUsed;
    SupplyMax_ = SupplyMax;
    TimeText_ = TimeText;
}

void MainGui::CancelPendingTarget()
{
    PendingTargetMode_ = TargetMode::None;
    PendingBuildDefID_ = -1;
    PendingCommandId_ = -1;
}

int MainGui::ConsumeClickedCommand()
{
    const int Clicked = ClickedCommandIndex_;
    ClickedCommandIndex_ = -1;
    return Clicked;
}

void MainGui::UpdateSelectionInfo(Core::ECS::EntityManager& Entities,
                                        const std::vector<int>& SelectedIds,
                                        const Hud& HudState)
{
    if (SelectedIds.empty())
    {
        Host_.SetSelection("No unit selected", 0, 0, 0, 0, 0, 0, 0, -1.0f,
                           "", "", {});
        Host_.SetPortraitIcon("");
        Host_.SetMultiSelection({}, -1);

        ScCommandSlot EmptySlot;
        for (int i = 0; i < 9; ++i)
            Host_.SetCommandSlot(i, EmptySlot);
        Host_.SetTooltip("", LastMouseX_, LastMouseY_, false);
        return;
    }

    int PrimaryId = SelectedIds[0];
    auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(PrimaryId);
    if (!Unit)
    {
        Host_.SetSelection("No unit selected", 0, 0, 0, 0, 0, 0, 0, -1.0f,
                           "", "", {});
        Host_.SetTooltip("", LastMouseX_, LastMouseY_, false);
        return;
    }

    auto Presentation =
        BuildMainGuiSelectionPresentation(HudState.GetState().InfoPanel);
    if (Presentation.Count <= 0)
        Presentation.Count = static_cast<int>(SelectedIds.size());
    if (Presentation.MaxHp <= 0.0f)
    {
        Presentation.Hp = static_cast<float>(Unit->HitPoints);
        Presentation.MaxHp = static_cast<float>(Unit->MaxHitPoints);
        Presentation.Shields = static_cast<float>(Unit->ShieldPoints);
        Presentation.MaxShields = static_cast<float>(Unit->MaxShieldPoints);
        Presentation.Energy = static_cast<float>(Unit->EnergyPoints);
        Presentation.MaxEnergy = static_cast<float>(Unit->MaxEnergyPoints);
    }

    Host_.SetSelection(Presentation.Name,
                       Presentation.Count,
                       Presentation.Hp,
                       Presentation.MaxHp,
                       Presentation.Shields,
                       Presentation.MaxShields,
                       Presentation.Energy,
                       Presentation.MaxEnergy,
                       Presentation.Progress,
                       Presentation.StatsText,
                       Presentation.QueueLabel,
                       Presentation.QueueEntries);

    // Set basic command card
    UpdateCommandCard(Entities, PrimaryId, Unit->ChkUnitID, HudState);

    // Set portrait
    std::string PortraitUri = GetPortraitUri(Unit->ChkUnitID);
    Host_.SetPortraitIcon(PortraitUri);

    // Multi-selection
    std::vector<ScSelectionInfo> MultiInfo;
    for (int Idx : SelectedIds)
    {
        auto* SelUnit = Entities.GetComponent<Game::Components::UnitComponent>(Idx);
        if (!SelUnit)
            continue;
        ScSelectionInfo Info;
        Info.HandleId = Idx;
        const int DefId = Game::Races::GetUnitDefIDByChkUnitID(SelUnit->ChkUnitID);
        Info.Name = DefId >= 0
            ? DisplayLabelForDef(DefId)
            : DisplayLabelForChkUnitId(SelUnit->ChkUnitID);
        Info.PortraitUri = HudState.GetUnitIconUriForChkUnitId(SelUnit->ChkUnitID);
        Info.HpRatio = SelUnit->MaxHitPoints > 0
            ? static_cast<float>(SelUnit->HitPoints) / static_cast<float>(SelUnit->MaxHitPoints)
            : 0.0f;
        Info.PlayerColor = 0xFF4488FF;
        MultiInfo.push_back(Info);
    }
    Host_.SetMultiSelection(MultiInfo, PrimaryId);
}

void MainGui::UpdateCommandCard(Core::ECS::EntityManager& Entities,
                                       int SelectedEntityId,
                                       int SelectedChkUnitId,
                                       const Hud& HudState)
{
    (void)Entities;
    (void)SelectedEntityId;
    (void)SelectedChkUnitId;

    const auto& Buttons = HudState.GetButtonDefs();
    for (int i = 0; i < 9; ++i)
    {
        ScCommandSlot Slot;
        const auto& Def = Buttons[(size_t)i];
        Slot.Label = Def.Label;
        Slot.Tooltip = Def.Label;
        Slot.IconUri = HudState.GetButtonIconUri(Def);
        Slot.Enabled = Def.Enabled;
        Slot.Visible = !Def.Label.empty();
        Slot.NumberOverlay = Slot.Visible ? i : -1;
        Slot.ResourceCosts.Minerals = Def.MineralCost;
        Slot.ResourceCosts.Gas = Def.GasCost;
        Slot.ResourceCosts.Supply = Def.SupplyCost;
        Slot.Active = HudState.GetActiveCommandIndex() == i;
        Host_.SetCommandSlot(i, Slot);
    }

    if (HoveredCommandIndex_ >= 0 && HoveredCommandIndex_ < 9)
    {
        const auto& Def = Buttons[(size_t)HoveredCommandIndex_];
        if (!Def.Label.empty())
        {
            ScResourceCosts Costs;
            Costs.Minerals = Def.MineralCost;
            Costs.Gas = Def.GasCost;
            Costs.Supply = Def.SupplyCost;
            Host_.SetTooltip(Def.Label, LastMouseX_, LastMouseY_, true, Costs);
            return;
        }
    }

    Host_.SetTooltip("", LastMouseX_, LastMouseY_, false);
}

std::string MainGui::GetPortraitUri(int ChkUnitId) const
{
    const std::string PortraitKey = ResolvePortraitKeyForChkUnitId(ChkUnitId);
    if (PortraitKey.empty() || AssetRoot_.empty())
        return {};

    const std::filesystem::path PortraitDir =
        std::filesystem::path(AssetRoot_) / "portrait" / PortraitKey;
    if (!std::filesystem::exists(PortraitDir) || !std::filesystem::is_directory(PortraitDir))
        return {};

    std::vector<std::filesystem::path> IdleClips;
    for (const auto& Entry : std::filesystem::directory_iterator(PortraitDir))
    {
        if (!Entry.is_regular_file())
            continue;

        std::string FileName = Entry.path().filename().string();
        std::string Ext = Entry.path().extension().string();
        std::transform(FileName.begin(), FileName.end(), FileName.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });
        std::transform(Ext.begin(), Ext.end(), Ext.begin(),
            [](unsigned char C) { return (char)std::tolower(C); });

        if (Ext == ".webm" && FileName.find("fid") != std::string::npos)
            IdleClips.push_back(Entry.path());
    }

    if (IdleClips.empty())
        return {};

    std::sort(IdleClips.begin(), IdleClips.end(), [](const auto& A, const auto& B)
    {
        return A.filename().string() < B.filename().string();
    });

    std::string Relative = "portrait/" + PortraitKey + "/" + IdleClips.front().filename().string();
    std::replace(Relative.begin(), Relative.end(), '\\', '/');
    return Relative;
}

} // namespace Game::Client::UI
