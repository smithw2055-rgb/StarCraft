#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "GuiHudHost.h"

namespace Core::ECS { class EntityManager; }

namespace Game::Client::UI
{
class Hud;

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
                const Hud& HudState,
                double Time);
    void RenderOffscreen();
    void Render();
    void Resize(int Width, int Height);
    bool HandleEvent(const void* Event);
    bool IsPointInHudEvent(const void* Event) const;

    bool IsActive() const { return Host_.IsActive(); }

    int HitCommandButton(float X, float Y) const;
    int HitMinimapButton(float X, float Y) const;
    int HitMultiSelection(float X, float Y) const;
    bool HitMinimap(float X, float Y) const;
    bool CheckMinimapClick(float X, float Y,
                           int MapWidthTiles,
                           int MapHeightTiles,
                           float& OutWorldX,
                           float& OutWorldY) const;
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
    int ConsumeClickedCommand();

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
                           int SelectedChkUnitId,
                           const Hud& HudState);
    void UpdateSelectionInfo(Core::ECS::EntityManager& Entities,
                             const std::vector<int>& SelectedIds,
                             const Hud& HudState);
    std::string GetPortraitUri(int ChkUnitId) const;

    GuiHudHost Host_;
    std::string AssetRoot_;

    int Minerals_ = 50;
    int Gas_ = 0;
    int SupplyUsed_ = 0;
    int SupplyMax_ = 200;
    std::string TimeText_ = "00:00";

    int HoveredCommandIndex_ = -1;
    int PressedCommandIndex_ = -1;
    int ClickedCommandIndex_ = -1;
    float LastMouseX_ = 0.0f;
    float LastMouseY_ = 0.0f;
    TargetMode PendingTargetMode_ = TargetMode::None;
    int PendingBuildDefID_ = -1;
    int PendingCommandId_ = -1;
};

} // namespace Game::Client::UI
