#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <NsCore/Ptr.h>
#include <NsGui/IView.h>
#include <NsGui/UserControl.h>
#include <NsRender/RenderDevice.h>

#include "UI/HudMinimapState.h"

namespace Aero {
class Border;
class Image;
class FrameworkElement;
class RenderDevice;
class TextBlock;
class Canvas;
}

namespace Game::Client::UI
{

namespace Glue
{
class GlueAnimatedImage;
}

struct ScreenRect
{
    float X = 0.0f;
    float Y = 0.0f;
    float W = 0.0f;
    float H = 0.0f;

    bool IsValid() const { return W > 0.0f && H > 0.0f; }
    bool Contains(float Px, float Py) const
    {
        return Px >= X && Px <= X + W && Py >= Y && Py <= Y + H;
    }
};

struct ScResourceCosts
{
    int Minerals = 0;
    int Gas = 0;
    int Supply = 0;
};

struct ScCommandSlot
{
    std::string Label;
    std::string Tooltip;
    std::string IconUri;
    ScResourceCosts ResourceCosts;
    int NumberOverlay = -1;
    bool Enabled = false;
    bool Visible = false;
    bool Active = false;
};

struct ScSelectionInfo
{
    int HandleId = -1;
    std::string Name;
    std::string PortraitUri;
    float HpRatio = 1.0f;
    uint32_t PlayerColor = 0xFF4488FF;
};

class GuiHudHost
{
public:
    GuiHudHost() = default;
    ~GuiHudHost();

    bool Initialize(const std::string& AssetRoot, int Width, int Height,
                    const std::string& XamlPath = "ui/Hud.xaml");
    void Shutdown();
    void Resize(int Width, int Height);
    void Update(double Time);
    void RenderOffscreen();
    void Render();
    bool HandleEvent(const void* Event);

    void SetResources(int Minerals, int Gas, int SupplyUsed, int SupplyMax,
                      const std::string& TimeText = "");
    void SetResourceIconUris(const std::string& MineralUri,
                             const std::string& GasUri,
                             const std::string& SupplyUri);
    void SetSelection(const std::string& Name, int Count,
                      float Hp, float MaxHp,
                      float Shields, float MaxShields,
                      float Energy, float MaxEnergy,
                      float Progress = -1.0f,
                      const std::string& StatsText = "",
                      const std::string& QueueLabel = "",
                      const std::vector<std::string>& QueueEntries = {});
    void SetPortraitIcon(const std::string& IconUri);
    void SetTooltip(const std::string& Text, float ScreenX, float ScreenY,
                    bool Visible, const ScResourceCosts& Costs = ScResourceCosts{});
    void SetCommandSlot(int Index, const ScCommandSlot& Slot);
    void SetHoveredCommand(int Index);
    void SetPressedCommand(int Index);
    void SetMultiSelection(const std::vector<ScSelectionInfo>& Units, int PrimaryHandle);
    void SetMinimapDots(const std::vector<HudMinimapDot>& Dots);
    void SetCampaignPings(const std::vector<HudMinimapPing>& Pings,
                          int MapWidthTiles,
                          int MapHeightTiles);

    int HitCommandButton(float X, float Y) const;
    int HitMinimapButton(float X, float Y) const;
    int HitMultiSelection(float X, float Y) const;
    bool HitMinimap(float X, float Y) const;

    ScreenRect GetMinimapRect() const { return MinimapRect_; }
    ScreenRect GetPortraitRect() const { return PortraitRect_; }
    ScreenRect GetCommandRect(int Index) const;

    bool IsActive() const { return Initialized_; }

    int GetHudHeight() const { return HudHeight_; }
    int GetViewportHeight() const { return Height_; }

private:
    static constexpr int kHudHeight = 280;
    static constexpr int kMinimapSize = 236;
    static constexpr int kMaxCampaignPingVisuals = 4;

    void CacheElements();
    void LogMissingElements() const;
    void CacheRects();
    void ApplyState();
    void ApplyCampaignPings();
    void UpdateLayoutMetrics();

    ScreenRect GetElementRect(Aero::FrameworkElement* Element) const;
    void SetElementVisibility(Aero::FrameworkElement* Element, bool Visible);

    std::string AssetRoot_;

    Aero::Ptr<Aero::IView> View_;
    Aero::Ptr<Aero::RenderDevice> Device_;
    Aero::Ptr<Aero::UserControl> Root_;
    Aero::TextBlock* MineralText_ = nullptr;
    Aero::TextBlock* GasText_ = nullptr;
    Aero::TextBlock* SupplyText_ = nullptr;
    Aero::Image* MineralIcon_ = nullptr;
    Aero::Image* GasIcon_ = nullptr;
    Aero::Image* SupplyIcon_ = nullptr;
    Aero::TextBlock* TimeText_ = nullptr;
    Aero::FrameworkElement* BottomHudScale_ = nullptr;

    Aero::FrameworkElement* InfoPanel_ = nullptr;
    Aero::Border* PortraitViewport_ = nullptr;
    Aero::Image* PortraitImage_ = nullptr;
    Glue::GlueAnimatedImage* PortraitVideo_ = nullptr;
    Aero::TextBlock* UnitNameText_ = nullptr;

    Aero::Border* HpFill_ = nullptr;
    Aero::TextBlock* HpText_ = nullptr;

    Aero::FrameworkElement* ShieldBarPanel_ = nullptr;
    Aero::Border* ShieldFill_ = nullptr;
    Aero::TextBlock* ShieldText_ = nullptr;

    Aero::FrameworkElement* EnergyBarPanel_ = nullptr;
    Aero::Border* EnergyFill_ = nullptr;
    Aero::TextBlock* EnergyText_ = nullptr;

    Aero::TextBlock* StatsText_ = nullptr;

    Aero::FrameworkElement* QueuePanel_ = nullptr;
    Aero::TextBlock* QueueLabel_ = nullptr;
    Aero::TextBlock* QueueText_ = nullptr;
    Aero::Border* QueueProgressFill_ = nullptr;

    Aero::FrameworkElement* CommandCardPanel_ = nullptr;
    std::array<Aero::FrameworkElement*, 9> CommandButtons_{};
    std::array<Aero::Image*, 9> CommandIcons_{};
    std::array<Aero::TextBlock*, 9> CommandLabels_{};
    std::array<Aero::Border*, 9> CommandActiveHighlights_{};
    std::array<Aero::Border*, 9> CommandHoverHighlights_{};
    std::array<Aero::Border*, 9> CommandPressedBorders_{};
    std::array<Aero::Border*, 9> CommandDisabledBacks_{};
    std::array<Aero::Border*, 9> CommandCooldowns_{};
    std::array<Aero::TextBlock*, 9> CommandNumbers_{};

    Aero::Border* Miniviewport_ = nullptr;
    Aero::FrameworkElement* MinimapContentViewport_ = nullptr;
    std::array<Aero::Border*, kMaxCampaignPingVisuals> MinimapPings_{};
    Aero::FrameworkElement* MiniMapTerrainButton_ = nullptr;
    Aero::FrameworkElement* MiniMapAllyButton_ = nullptr;
    Aero::FrameworkElement* MiniMapFogButton_ = nullptr;

    Aero::Border* TooltipPanel_ = nullptr;
    Aero::TextBlock* TooltipText_ = nullptr;
    Aero::FrameworkElement* TooltipCostRow_ = nullptr;
    Aero::FrameworkElement* TooltipMineralCost_ = nullptr;
    Aero::Image* TooltipMineralIcon_ = nullptr;
    Aero::TextBlock* TooltipMineralCostText_ = nullptr;
    Aero::FrameworkElement* TooltipGasCost_ = nullptr;
    Aero::Image* TooltipGasIcon_ = nullptr;
    Aero::TextBlock* TooltipGasCostText_ = nullptr;
    Aero::FrameworkElement* TooltipSupplyCost_ = nullptr;
    Aero::Image* TooltipSupplyIcon_ = nullptr;
    Aero::TextBlock* TooltipSupplyCostText_ = nullptr;

    Aero::FrameworkElement* SelectionPanel_ = nullptr;
    std::array<Aero::Border*, 12> SelectSlots_{};
    std::array<Aero::Image*, 12> SelectImages_{};
    std::array<Aero::Border*, 12> SelectColors_{};
    std::array<Aero::Border*, 12> SelectHps_{};

    int Width_ = 0;
    int Height_ = 0;
    int PhysicalWidth_ = 0;
    int PhysicalHeight_ = 0;
    float DpiScale_ = 1.0f;
    int HudHeight_ = kHudHeight;
    bool Initialized_ = false;
    bool HasLastUpdateTime_ = false;
    double LastUpdateTime_ = 0.0;

    int Minerals_ = 50;
    int Gas_ = 0;
    int SupplyUsed_ = 0;
    int SupplyMax_ = 200;
    std::string TimeValue_ = "00:00";
    std::string MineralIconUri_;
    std::string GasIconUri_;
    std::string SupplyIconUri_;
    std::string AppliedMineralIconUri_;
    std::string AppliedGasIconUri_;
    std::string AppliedSupplyIconUri_;
    std::string AppliedTooltipMineralIconUri_;
    std::string AppliedTooltipGasIconUri_;
    std::string AppliedTooltipSupplyIconUri_;

    std::string UnitName_ = "No unit selected";
    int UnitCount_ = 0;
    float Hp_ = 0.0f;
    float MaxHp_ = 0.0f;
    float Shields_ = 0.0f;
    float MaxShields_ = 0.0f;
    float Energy_ = 0.0f;
    float MaxEnergy_ = 0.0f;
    float Progress_ = -1.0f;
    std::string StatsValue_;
    std::string QueueLabelValue_;
    std::string QueueTextValue_;
    float QueueProgressRatio_ = 0.0f;
    std::string PortraitIconUri_;
    std::string AppliedPortraitImageUri_;
    std::string AppliedPortraitVideoUri_;
    bool AppliedPortraitVideoReady_ = false;

    bool TooltipVisible_ = false;
    std::string TooltipValue_;
    ScResourceCosts TooltipCosts_;
    float TooltipX_ = 0.0f;
    float TooltipY_ = 0.0f;

    struct CmdState
    {
        std::string Label;
        std::string Tooltip;
        std::string IconUri;
        ScResourceCosts ResourceCosts;
        int NumberOverlay = -1;
        bool Enabled = false;
        bool Visible = false;
        bool Active = false;
    };
    std::array<CmdState, 9> CommandStates_{};
    std::array<std::string, 9> CommandAppliedIconUris_{};
    std::array<ScreenRect, 9> CommandRects_{};
    int HoveredCommandIndex_ = -1;
    int PressedCommandIndex_ = -1;

    std::array<ScSelectionInfo, 12> MultiSelectStates_{};
    std::array<std::string, 12> MultiSelectAppliedImageUris_{};
    std::array<bool, 12> MultiSelectVisible_{};
    std::array<ScreenRect, 12> MultiSelectRects_{};
    int MultiSelectPrimaryHandle_ = -1;

    std::vector<HudMinimapPing> CampaignPings_;
    int CampaignMapWidth_ = 0;
    int CampaignMapHeight_ = 0;

    ScreenRect MinimapRect_;
    ScreenRect PortraitRect_;
    std::array<ScreenRect, 3> MinimapButtonRects_{};
};

} // namespace Game::Client::UI
