#include "MainGui.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Core/ECS/EntityManager.h"
#include "UI/HudPortrait.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <cmath>

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
                           double Time)
{
    Host_.SetResources(Minerals_, Gas_, SupplyUsed_, SupplyMax_, TimeText_);

    UpdateSelectionInfo(Entities, SelectedIds);

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

void MainGui::HandleEvent(const void* Event)
{
    Host_.HandleEvent(Event);
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

void MainGui::UpdateSelectionInfo(Core::ECS::EntityManager& Entities,
                                        const std::vector<int>& SelectedIds)
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
        return;
    }

    int PrimaryId = SelectedIds[0];
    auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(PrimaryId);
    if (!Unit)
    {
        Host_.SetSelection("No unit selected", 0, 0, 0, 0, 0, 0, 0, -1.0f,
                           "", "", {});
        return;
    }

    // Build selection info
    std::string UnitName = "Unit";
    float Hp = static_cast<float>(Unit->HitPoints);
    float MaxHp = static_cast<float>(Unit->MaxHitPoints);
    float Shields = static_cast<float>(Unit->ShieldPoints);
    float MaxShields = static_cast<float>(Unit->MaxShieldPoints);
    float Energy = static_cast<float>(Unit->EnergyPoints);
    float MaxEnergy = static_cast<float>(Unit->MaxEnergyPoints);
    int Count = static_cast<int>(SelectedIds.size());

    Host_.SetSelection(UnitName, Count, Hp, MaxHp, Shields, MaxShields,
                       Energy, MaxEnergy, -1.0f, "", "", {});

    // Set basic command card
    UpdateCommandCard(Entities, PrimaryId, Unit->ChkUnitID);

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
        Info.Name = "Unit";
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
                                      int SelectedChkUnitId)
{
    (void)Entities;

    // Default command buttons for any selection: basic unit commands
    // StarCraft layout: Move, Stop, Attack, Patrol, Hold Position, etc.
    ScCommandSlot MoveCmd;
    MoveCmd.Label = "Move";
    MoveCmd.IconUri = "";
    MoveCmd.Enabled = true;
    MoveCmd.Visible = true;
    MoveCmd.NumberOverlay = 0;
    Host_.SetCommandSlot(0, MoveCmd);

    ScCommandSlot StopCmd;
    StopCmd.Label = "Stop";
    StopCmd.IconUri = "";
    StopCmd.Enabled = true;
    StopCmd.Visible = true;
    StopCmd.NumberOverlay = 1;
    Host_.SetCommandSlot(1, StopCmd);

    ScCommandSlot AttackCmd;
    AttackCmd.Label = "Attack";
    AttackCmd.IconUri = "";
    AttackCmd.Enabled = true;
    AttackCmd.Visible = true;
    AttackCmd.NumberOverlay = 2;
    Host_.SetCommandSlot(2, AttackCmd);

    ScCommandSlot PatrolCmd;
    PatrolCmd.Label = "Patrol";
    PatrolCmd.IconUri = "";
    PatrolCmd.Enabled = true;
    PatrolCmd.Visible = true;
    PatrolCmd.NumberOverlay = 3;
    Host_.SetCommandSlot(3, PatrolCmd);

    ScCommandSlot HoldCmd;
    HoldCmd.Label = "Hold";
    HoldCmd.IconUri = "";
    HoldCmd.Enabled = true;
    HoldCmd.Visible = true;
    Host_.SetCommandSlot(4, HoldCmd);

    // Fill remaining slots (5-8) with empty
    ScCommandSlot EmptySlot;
    for (int i = 5; i < 9; ++i)
        Host_.SetCommandSlot(i, EmptySlot);
}

std::string MainGui::GetPortraitUri(int ChkUnitId) const
{
    const int UnitDefId = Game::Races::GetUnitDefIDByChkUnitID(ChkUnitId);
    const std::string PortraitKey = ResolvePortraitKeyForSelection(UnitDefId);
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
