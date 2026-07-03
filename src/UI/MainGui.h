#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "GuiHudHost.h"

namespace Core::ECS { class EntityManager; }

namespace Game::Client::UI
{

class MainGui
{
public:
    MainGui() = default;
    ~MainGui() { Shutdown(); }

    bool Init(const std::string& AssetRoot, int ScreenW, int ScreenH,
              const std::string& XamlPath = "ui/Hud.xaml");
    void Shutdown();
    void Update(Core::ECS::EntityManager& Entities,
                const std::vector<int>& SelectedIds,
                double Time);
    void RenderOffscreen();
    void Render();
    void Resize(int Width, int Height);
    void HandleEvent(const void* Event);

    bool IsActive() const { return Host_.IsActive(); }

    int HitCommandButton(float X, float Y) const;
    int HitMinimapButton(float X, float Y) const;
    int HitMultiSelection(float X, float Y) const;
    bool HitMinimap(float X, float Y) const;
    bool IsPointInHud(float X, float Y) const;

    void SetResources(int Minerals, int Gas, int SupplyUsed, int SupplyMax,
                      const std::string& TimeText = "");
    bool HasPendingTarget() const { return PendingTargetMode_ != TargetMode::None; }
    int GetPendingBuildDefID() const { return PendingBuildDefID_; }
    int GetPendingCommandId() const { return PendingCommandId_; }
    void CancelPendingTarget();

    const GuiHudHost& GetHost() const { return Host_; }
    GuiHudHost& GetHost() { return Host_; }
    int GetHudHeight() const { return Host_.GetHudHeight(); }

    int GetHoveredCommand() const { return HoveredCommandIndex_; }
    int GetClickedCommand() const { return ClickedCommandIndex_; }
    void ClearClickedCommand() { ClickedCommandIndex_ = -1; }

private:
    enum class TargetMode : uint8_t
    {
        None,
        BuildPlacement,
        Harvest,
        Move,
        AttackMove,
    };

    void UpdateCommandCard(Core::ECS::EntityManager& Entities,
                           int SelectedEntityId,
                           int SelectedChkUnitId);
    void UpdateSelectionInfo(Core::ECS::EntityManager& Entities,
                             const std::vector<int>& SelectedIds);
    std::string GetPortraitUri(int ChkUnitId) const;

    GuiHudHost Host_;
    std::string AssetRoot_;

    int Minerals_ = 50;
    int Gas_ = 0;
    int SupplyUsed_ = 0;
    int SupplyMax_ = 200;
    std::string TimeText_ = "00:00";

    int HoveredCommandIndex_ = -1;
    int ClickedCommandIndex_ = -1;
    TargetMode PendingTargetMode_ = TargetMode::None;
    int PendingBuildDefID_ = -1;
    int PendingCommandId_ = -1;
};

} // namespace Game::Client::UI
