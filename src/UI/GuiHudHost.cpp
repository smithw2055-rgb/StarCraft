#include "GuiHudHost.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

#include <NsGui/IView.h>
#include <NsGui/IRenderer.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/Grid.h>
#include <NsGui/Border.h>
#include <NsGui/Image.h>
#include <NsGui/TextBlock.h>
#include <NsGui/Canvas.h>
#include <NsGui/Uri.h>
#include <NsGui/BitmapImage.h>
#include <NsGui/Brush.h>
#include <NsGui/SolidColorBrush.h>
#include <NsGui/InputEnums.h>
#include <NsDrawing/Color.h>
#if defined(SOKOL_D3D11)
#include <NsRender/D3D11Factory.h>
#include "third_party/sokol/sokol_gfx.h"
struct ID3D11DeviceContext;
#else
#include <NsRender/GLFactory.h>
#endif

#include "third_party/sokol/sokol_app.h"

namespace Game::Client::UI
{
namespace
{

int ToLogicalPixels(int Physical, float DpiScale)
{
    if (DpiScale <= 1.0f) return Physical;
    return static_cast<int>(std::lround(static_cast<float>(Physical) / DpiScale));
}

float GetDpiScale()
{
    float Scale = sapp_dpi_scale();
    return Scale > 1.0f ? Scale : 1.0f;
}

template<typename T>
T* FindFirstAs(Aero::FrameworkElement* Root, const char* Name1, const char* Name2 = nullptr)
{
    if (auto* Element = Root->FindName(Name1))
        return Aero::DynamicCast<T*>(Element);
    if (Name2)
    {
        if (auto* Element = Root->FindName(Name2))
            return Aero::DynamicCast<T*>(Element);
    }
    return nullptr;
}

template<typename T>
T* FindAs(Aero::FrameworkElement* Root, const char* Name)
{
    if (auto* Element = Root->FindName(Name))
        return Aero::DynamicCast<T*>(Element);
    return nullptr;
}

Aero::Ptr<Aero::SolidColorBrush> MakeColorBrush(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255)
{
    return Aero::MakePtr<Aero::SolidColorBrush>(
        Aero::Color{R, G, B, A});
}

uint32_t HealthBarTint(float Ratio)
{
    if (Ratio > 0.5f) return 0xFF00FF00; // green
    if (Ratio > 0.25f) return 0xFFFFFF00; // yellow
    return 0xFFFF0000; // red
}

} // namespace

GuiHudHost::~GuiHudHost()
{
    Shutdown();
}

bool GuiHudHost::Initialize(const std::string& AssetRoot, int Width, int Height,
                                const std::string& XamlPath)
{
    if (Initialized_) return true;

    AssetRoot_ = AssetRoot;
    DpiScale_ = GetDpiScale();
    PhysicalWidth_ = std::max(1, Width);
    PhysicalHeight_ = std::max(1, Height);
    Width_ = ToLogicalPixels(Width, DpiScale_);
    Height_ = ToLogicalPixels(Height, DpiScale_);

    std::string ResolvedXamlPath = XamlPath.empty() ? "ui/Hud.xaml" : XamlPath;

    std::cout << "[Gui] Initializing xaml='" << ResolvedXamlPath
              << "' assetRoot='" << AssetRoot_
              << "' physical=" << PhysicalWidth_ << "x" << PhysicalHeight_
              << " logical=" << Width_ << "x" << Height_
              << " dpi=" << DpiScale_ << std::endl;

    Aero::GUI::SetLogHandler(
        [](const char*, uint32_t, uint32_t Level, const char*, const char* Message)
        {
            const char* Prefix[] = {"T", "D", "I", "W", "E"};
            std::cout << "[Gui/"
                      << Prefix[std::min(Level, static_cast<uint32_t>(4))]
                      << "] " << Message << std::endl;
        });

    Root_ = Aero::GUI::LoadXaml<Aero::UserControl>(ResolvedXamlPath.c_str());
    if (!Root_)
    {
        std::cerr << "[Gui] Failed to load XAML: " << ResolvedXamlPath << std::endl;
        return false;
    }

    std::cout << "[Gui] Loaded XAML: " << ResolvedXamlPath << std::endl;

    View_ = Aero::GUI::CreateView(Root_);
    if (!View_)
    {
        std::cerr << "[Gui] Failed to create view" << std::endl;
        return false;
    }

    // LCD text uses per-channel operations that don't play well with our simple GL host path.
    View_->SetFlags(Aero::RenderFlags_PPAA);
    View_->SetScale(DpiScale_);
    View_->SetSize(static_cast<uint32_t>(PhysicalWidth_),
                   static_cast<uint32_t>(PhysicalHeight_));

#if defined(SOKOL_D3D11)
    auto* D3DContext = static_cast<ID3D11DeviceContext*>(const_cast<void*>(sg_d3d11_device_context()));
    Device_ = AeroApp::D3D11Factory::CreateDevice(D3DContext, false);
#else
    Device_ = AeroApp::GLFactory::CreateDevice(false);
#endif
    if (!Device_)
    {
        std::cerr << "[Gui] Failed to create GL render device" << std::endl;
        return false;
    }

    View_->GetRenderer()->Init(Device_.GetPtr());

    CacheElements();
    LogMissingElements();
    Initialized_ = true;
    ApplyState();
    View_->Update(0.0);
    CacheRects();

    std::cout << "[Gui] Ready" << std::endl;
    return true;
}

void GuiHudHost::Shutdown()
{
    if (Initialized_ && View_)
    {
        View_->GetRenderer()->Shutdown();
    }
    Initialized_ = false;
    HasLastUpdateTime_ = false;
    LastUpdateTime_ = 0.0;

    Device_.Reset();
    Root_.Reset();
    View_.Reset();

    CacheElements(); // null all cached pointers

}

void GuiHudHost::Resize(int Width, int Height)
{
    if (Width <= 0 || Height <= 0 || !View_) return;

    float Scale = GetDpiScale();
    int LogicalWidth = ToLogicalPixels(Width, Scale);
    int LogicalHeight = ToLogicalPixels(Height, Scale);
    int PhysW = std::max(1, Width);
    int PhysH = std::max(1, Height);

    if (PhysicalWidth_ == PhysW && PhysicalHeight_ == PhysH &&
        Width_ == LogicalWidth && Height_ == LogicalHeight &&
        DpiScale_ == Scale)
        return;

    PhysicalWidth_ = PhysW;
    PhysicalHeight_ = PhysH;
    Width_ = LogicalWidth;
    Height_ = LogicalHeight;
    DpiScale_ = Scale;

    View_->SetScale(DpiScale_);
    View_->SetSize(static_cast<uint32_t>(PhysicalWidth_),
                   static_cast<uint32_t>(PhysicalHeight_));
}

void GuiHudHost::Update(double Time)
{
    if (!Initialized_ || !View_) return;

    double Dt = HasLastUpdateTime_ ? (Time - LastUpdateTime_) : (1.0 / 60.0);
    if (!std::isfinite(Dt) || Dt < 0.0 || Dt > 0.25)
        Dt = 1.0 / 60.0;
    LastUpdateTime_ = Time;
    HasLastUpdateTime_ = true;

    ApplyState();
    View_->Update(Time);
    (void)Dt;
}

void GuiHudHost::RenderOffscreen()
{
    if (!Initialized_ || !View_) return;

    auto* Renderer = View_->GetRenderer();
    Renderer->UpdateRenderTree();
    Renderer->RenderOffscreen();
}

void GuiHudHost::Render()
{
    if (!Initialized_ || !View_) return;

    auto* Renderer = View_->GetRenderer();
    if (!Renderer) return;
    Renderer->Render(false, false);
}

bool GuiHudHost::HandleEvent(const void* EventPtr)
{
    if (!Initialized_ || !View_ || !EventPtr) return false;

    const sapp_event* E = static_cast<const sapp_event*>(EventPtr);
    float X = E->mouse_x;
    float Y = E->mouse_y;

    switch (E->type)
    {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
    {
        Aero::MouseButton Btn = Aero::MouseButton_Left;
        if (E->mouse_button == SAPP_MOUSEBUTTON_RIGHT)
            Btn = Aero::MouseButton_Right;
        else if (E->mouse_button == SAPP_MOUSEBUTTON_MIDDLE)
            Btn = Aero::MouseButton_Middle;
        return View_->MouseButtonDown(X, Y, Btn);
    }
    case SAPP_EVENTTYPE_MOUSE_UP:
    {
        Aero::MouseButton Btn = Aero::MouseButton_Left;
        if (E->mouse_button == SAPP_MOUSEBUTTON_RIGHT)
            Btn = Aero::MouseButton_Right;
        else if (E->mouse_button == SAPP_MOUSEBUTTON_MIDDLE)
            Btn = Aero::MouseButton_Middle;
        return View_->MouseButtonUp(X, Y, Btn);
    }
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        return View_->MouseMove(X, Y);
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        return View_->MouseWheel(X, Y, static_cast<int>(std::lround(E->scroll_y)));
    case SAPP_EVENTTYPE_KEY_DOWN:
        return View_->KeyDown(static_cast<Aero::Key>(E->key_code));
    case SAPP_EVENTTYPE_KEY_UP:
        return View_->KeyUp(static_cast<Aero::Key>(E->key_code));
    case SAPP_EVENTTYPE_CHAR:
        return View_->Char(E->char_code);
    default:
        break;
    }
    return false;
}

void GuiHudHost::SetResources(int Minerals, int Gas, int SupplyUsed, int SupplyMax,
                                  const std::string& TimeText)
{
    Minerals_ = Minerals;
    Gas_ = Gas;
    SupplyUsed_ = SupplyUsed;
    SupplyMax_ = SupplyMax;
    TimeValue_ = TimeText;
}

void GuiHudHost::SetSelection(const std::string& Name, int Count,
                                  float Hp, float MaxHp,
                                  float Shields, float MaxShields,
                                  float Energy, float MaxEnergy,
                                  float Progress,
                                  const std::string& StatsText,
                                  const std::string& QueueLabel,
                                  const std::vector<std::string>& QueueEntries)
{
    UnitName_ = Name;
    UnitCount_ = Count;
    Hp_ = Hp;
    MaxHp_ = MaxHp;
    Shields_ = Shields;
    MaxShields_ = MaxShields;
    Energy_ = Energy;
    MaxEnergy_ = MaxEnergy;
    Progress_ = Progress;
    StatsValue_ = StatsText;
    QueueLabelValue_ = QueueLabel;
    QueueTextValue_ = QueueEntries.empty() ? "" : QueueEntries[0];
    QueueProgressRatio_ = std::isnan(Progress) || Progress < 0.0f ? 0.0f
        : std::clamp(Progress, 0.0f, 1.0f);
}

void GuiHudHost::SetPortraitIcon(const std::string& IconUri)
{
    PortraitIconUri_ = IconUri;
}

void GuiHudHost::SetTooltip(const std::string& Text, float ScreenX, float ScreenY,
                                bool Visible, const ScResourceCosts& Costs)
{
    TooltipValue_ = Text;
    TooltipX_ = ScreenX;
    TooltipY_ = ScreenY;
    TooltipVisible_ = Visible;
    TooltipCosts_ = Costs;
}

void GuiHudHost::SetCommandSlot(int Index, const ScCommandSlot& Slot)
{
    if (Index < 0 || Index >= 9) return;
    CommandStates_[Index].Label = Slot.Label;
    CommandStates_[Index].Tooltip = Slot.Tooltip;
    CommandStates_[Index].IconUri = Slot.IconUri;
    CommandStates_[Index].ResourceCosts = Slot.ResourceCosts;
    CommandStates_[Index].NumberOverlay = Slot.NumberOverlay;
    CommandStates_[Index].Enabled = Slot.Enabled;
    CommandStates_[Index].Visible = Slot.Visible;
    CommandStates_[Index].Active = Slot.Active;
}

void GuiHudHost::SetHoveredCommand(int Index)
{
    HoveredCommandIndex_ = Index;
}

void GuiHudHost::SetPressedCommand(int Index)
{
    PressedCommandIndex_ = Index;
}

void GuiHudHost::SetMultiSelection(const std::vector<ScSelectionInfo>& Units,
                                       int PrimaryHandle)
{
    MultiSelectPrimaryHandle_ = PrimaryHandle;
    for (int i = 0; i < 6; ++i)
    {
        if (i < static_cast<int>(Units.size()))
        {
            MultiSelectStates_[i] = Units[i];
            MultiSelectVisible_[i] = true;
        }
        else
        {
            MultiSelectVisible_[i] = false;
        }
    }
}

int GuiHudHost::HitCommandButton(float X, float Y) const
{
    for (int i = 0; i < 9; ++i)
    {
        if (CommandRects_[i].IsValid() && CommandRects_[i].Contains(X, Y))
            return i;
    }
    return -1;
}

int GuiHudHost::HitMinimapButton(float X, float Y) const
{
    for (int i = 0; i < 3; ++i)
    {
        if (MinimapButtonRects_[i].IsValid() && MinimapButtonRects_[i].Contains(X, Y))
            return i;
    }
    return -1;
}

int GuiHudHost::HitMultiSelection(float X, float Y) const
{
    for (int i = 0; i < 6; ++i)
    {
        if (MultiSelectVisible_[i] && MultiSelectRects_[i].IsValid() &&
            MultiSelectRects_[i].Contains(X, Y))
            return i;
    }
    return -1;
}

bool GuiHudHost::HitMinimap(float X, float Y) const
{
    return MinimapRect_.IsValid() && MinimapRect_.Contains(X, Y);
}

ScreenRect GuiHudHost::GetCommandRect(int Index) const
{
    if (Index < 0 || Index >= 9) return {};
    return CommandRects_[Index];
}

void GuiHudHost::CacheElements()
{
    auto* RootElement = Root_.GetPtr();
    if (!RootElement) return;

    MineralText_ = FindAs<Aero::TextBlock>(RootElement, "MineralText");
    GasText_ = FindAs<Aero::TextBlock>(RootElement, "GasText");
    SupplyText_ = FindAs<Aero::TextBlock>(RootElement, "SupplyText");
    TimeText_ = FindAs<Aero::TextBlock>(RootElement, "TimeText");

    InfoPanel_ = FindAs<Aero::FrameworkElement>(RootElement, "InfoPanel");
    PortraitViewport_ = FindAs<Aero::Border>(RootElement, "PortraitViewport");
    PortraitImage_ = FindAs<Aero::Image>(RootElement, "PortraitImage");
    UnitNameText_ = FindAs<Aero::TextBlock>(RootElement, "UnitNameText");

    HpFill_ = FindAs<Aero::Border>(RootElement, "HpFill");
    HpText_ = FindAs<Aero::TextBlock>(RootElement, "HpText");

    ShieldBarPanel_ = FindAs<Aero::FrameworkElement>(RootElement, "ShieldBarPanel");
    ShieldFill_ = FindAs<Aero::Border>(RootElement, "ShieldFill");
    ShieldText_ = FindAs<Aero::TextBlock>(RootElement, "ShieldText");

    EnergyBarPanel_ = FindAs<Aero::FrameworkElement>(RootElement, "EnergyBarPanel");
    EnergyFill_ = FindAs<Aero::Border>(RootElement, "EnergyFill");
    EnergyText_ = FindAs<Aero::TextBlock>(RootElement, "EnergyText");

    StatsText_ = FindAs<Aero::TextBlock>(RootElement, "StatsText");

    QueuePanel_ = FindAs<Aero::FrameworkElement>(RootElement, "QueuePanel");
    QueueLabel_ = FindAs<Aero::TextBlock>(RootElement, "QueueLabel");
    QueueText_ = FindAs<Aero::TextBlock>(RootElement, "QueueText");
    QueueProgressFill_ = FindAs<Aero::Border>(RootElement, "QueueProgressFill");

    CommandCardPanel_ = FindAs<Aero::FrameworkElement>(RootElement, "CommandCardPanel");

    for (int i = 0; i < 9; ++i)
    {
        char Name[64];
        std::snprintf(Name, sizeof(Name), "CommandButton%d", i);
        CommandButtons_[i] = FindAs<Aero::FrameworkElement>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "CommandIcon%d", i);
        CommandIcons_[i] = FindAs<Aero::Image>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "CommandLabel%d", i);
        CommandLabels_[i] = FindAs<Aero::TextBlock>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "CommandActiveHighlight%d", i);
        CommandActiveHighlights_[i] = FindAs<Aero::Border>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "CommandHoverHighlight%d", i);
        CommandHoverHighlights_[i] = FindAs<Aero::Border>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "CommandPressedBorder%d", i);
        CommandPressedBorders_[i] = FindAs<Aero::Border>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "CommandDisabledBack%d", i);
        CommandDisabledBacks_[i] = FindAs<Aero::Border>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "CommandCooldown%d", i);
        CommandCooldowns_[i] = FindAs<Aero::Border>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "CommandNumber%d", i);
        CommandNumbers_[i] = FindAs<Aero::TextBlock>(RootElement, Name);
    }

    Miniviewport_ = FindAs<Aero::Border>(RootElement, "MinimapViewport");
    MinimapContentViewport_ = FindAs<Aero::Border>(RootElement, "MinimapContentViewport");
    MiniMapTerrainButton_ = FindAs<Aero::FrameworkElement>(RootElement, "MiniMapTerrainButton");
    MiniMapAllyButton_ = FindAs<Aero::FrameworkElement>(RootElement, "MiniMapAllyButton");
    MiniMapFogButton_ = FindAs<Aero::FrameworkElement>(RootElement, "MiniMapFogButton");

    TooltipPanel_ = FindAs<Aero::Border>(RootElement, "TooltipPanel");
    TooltipText_ = FindAs<Aero::TextBlock>(RootElement, "TooltipText");
    TooltipCostRow_ = FindAs<Aero::FrameworkElement>(RootElement, "TooltipCostRow");
    TooltipMineralCost_ = FindAs<Aero::FrameworkElement>(RootElement, "TooltipMineralCost");
    TooltipMineralCostText_ = FindAs<Aero::TextBlock>(RootElement, "TooltipMineralCostText");
    TooltipGasCost_ = FindAs<Aero::FrameworkElement>(RootElement, "TooltipGasCost");
    TooltipGasCostText_ = FindAs<Aero::TextBlock>(RootElement, "TooltipGasCostText");

    SelectionPanel_ = FindAs<Aero::FrameworkElement>(RootElement, "SelectionPanel");

    for (int i = 0; i < 6; ++i)
    {
        char Name[64];
        std::snprintf(Name, sizeof(Name), "SelectSlot%d", i);
        SelectSlots_[i] = FindAs<Aero::Border>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "SelectColor%d", i);
        SelectColors_[i] = FindAs<Aero::Border>(RootElement, Name);
        std::snprintf(Name, sizeof(Name), "SelectHp%d", i);
        SelectHps_[i] = FindAs<Aero::Border>(RootElement, Name);
    }
}

void GuiHudHost::LogMissingElements() const
{
    auto LogMissing = [](const char* Name, const void* Ptr)
    {
        if (!Ptr)
            std::cout << "[Gui] Missing element: " << Name << std::endl;
    };

    LogMissing("MineralText", MineralText_);
    LogMissing("GasText", GasText_);
    LogMissing("SupplyText", SupplyText_);
    LogMissing("TimeText", TimeText_);
    LogMissing("InfoPanel", InfoPanel_);
    LogMissing("PortraitViewport", PortraitViewport_);
    LogMissing("PortraitImage", PortraitImage_);
    LogMissing("UnitNameText", UnitNameText_);
    LogMissing("HpFill", HpFill_);
    LogMissing("HpText", HpText_);
    LogMissing("ShieldBarPanel", ShieldBarPanel_);
    LogMissing("ShieldFill", ShieldFill_);
    LogMissing("ShieldText", ShieldText_);
    LogMissing("EnergyBarPanel", EnergyBarPanel_);
    LogMissing("EnergyFill", EnergyFill_);
    LogMissing("EnergyText", EnergyText_);
    LogMissing("StatsText", StatsText_);
    LogMissing("QueuePanel", QueuePanel_);
    LogMissing("CommandCardPanel", CommandCardPanel_);
    LogMissing("MinimapViewport", Miniviewport_);
    LogMissing("TooltipPanel", TooltipPanel_);
    LogMissing("SelectionPanel", SelectionPanel_);

    for (int i = 0; i < 9; ++i)
    {
        char Name[64];
        std::snprintf(Name, sizeof(Name), "CommandButton%d", i);
        LogMissing(Name, CommandButtons_[i]);
    }
}

void GuiHudHost::CacheRects()
{
    if (!Root_) return;

    MinimapRect_ = GetElementRect(Miniviewport_);
    PortraitRect_ = GetElementRect(PortraitViewport_);

    for (int i = 0; i < 9; ++i)
        CommandRects_[i] = GetElementRect(CommandButtons_[i]);

    MinimapButtonRects_[0] = GetElementRect(MiniMapTerrainButton_);
    MinimapButtonRects_[1] = GetElementRect(MiniMapAllyButton_);
    MinimapButtonRects_[2] = GetElementRect(MiniMapFogButton_);

    for (int i = 0; i < 6; ++i)
        MultiSelectRects_[i] = GetElementRect(SelectSlots_[i]);
}

void GuiHudHost::ApplyState()
{
    // --- Resource Bar ---
    if (MineralText_)
        MineralText_->SetText(std::to_string(Minerals_).c_str());
    if (GasText_)
        GasText_->SetText(std::to_string(Gas_).c_str());
    if (SupplyText_)
    {
        std::string SupplyStr = std::to_string(SupplyUsed_) + "/" + std::to_string(SupplyMax_);
        SupplyText_->SetText(SupplyStr.c_str());
    }
    if (TimeText_)
        TimeText_->SetText(TimeValue_.c_str());

    // --- Info Panel ---
    if (UnitNameText_)
    {
        std::string NameStr = UnitName_;
        if (UnitCount_ > 1)
            NameStr += " (" + std::to_string(UnitCount_) + ")";
        UnitNameText_->SetText(NameStr.c_str());
    }

    // HP bar
    float HpRatio = MaxHp_ > 0.0f ? std::clamp(Hp_ / MaxHp_, 0.0f, 1.0f) : 0.0f;
    int HpValue = static_cast<int>(std::max(0.0f, Hp_));
    int HpMaxValue = static_cast<int>(std::max(0.0f, MaxHp_));
    if (HpText_)
        HpText_->SetText((std::to_string(HpValue) + "/" + std::to_string(HpMaxValue)).c_str());
    if (HpFill_)
    {
        HpFill_->SetWidth(160.0f * HpRatio);
        uint32_t Tint = HealthBarTint(HpRatio);
        HpFill_->SetBackground(MakeColorBrush(
            static_cast<uint8_t>((Tint >> 16) & 0xFF),
            static_cast<uint8_t>((Tint >> 8) & 0xFF),
            static_cast<uint8_t>(Tint & 0xFF),
            static_cast<uint8_t>((Tint >> 24) & 0xFF)).GetPtr());
    }

    // Shield bar
    float ShieldRatio = MaxShields_ > 0.0f ? std::clamp(Shields_ / MaxShields_, 0.0f, 1.0f) : 0.0f;
    bool HasShields = MaxShields_ > 0.0f;
    int ShieldValue = static_cast<int>(std::max(0.0f, Shields_));
    int ShieldMaxValue = static_cast<int>(std::max(0.0f, MaxShields_));
    SetElementVisibility(ShieldBarPanel_, HasShields);
    if (ShieldFill_)
        ShieldFill_->SetWidth(160.0f * ShieldRatio);
    if (ShieldText_)
        ShieldText_->SetText((std::to_string(ShieldValue) + "/" + std::to_string(ShieldMaxValue)).c_str());

    // Energy bar
    float EnergyRatio = MaxEnergy_ > 0.0f ? std::clamp(Energy_ / MaxEnergy_, 0.0f, 1.0f) : 0.0f;
    bool HasEnergy = MaxEnergy_ > 0.0f;
    int EnergyValue = static_cast<int>(std::max(0.0f, Energy_));
    int EnergyMaxValue = static_cast<int>(std::max(0.0f, MaxEnergy_));
    SetElementVisibility(EnergyBarPanel_, HasEnergy);
    if (EnergyFill_)
        EnergyFill_->SetWidth(160.0f * EnergyRatio);
    if (EnergyText_)
        EnergyText_->SetText((std::to_string(EnergyValue) + "/" + std::to_string(EnergyMaxValue)).c_str());

    // Stats
    if (StatsText_)
        StatsText_->SetText(StatsValue_.c_str());

    // Queue panel
    bool HasQueue = !QueueLabelValue_.empty() && Progress_ >= 0.0f;
    SetElementVisibility(QueuePanel_, HasQueue);
    if (QueueLabel_)
        QueueLabel_->SetText(QueueLabelValue_.c_str());
    if (QueueText_)
        QueueText_->SetText(QueueTextValue_.c_str());
    if (QueueProgressFill_)
        QueueProgressFill_->SetWidth(268.0f * QueueProgressRatio_);

    // --- Portrait ---
    if (PortraitImage_)
    {
        if (PortraitIconUri_.empty())
        {
            PortraitImage_->SetVisibility(Aero::Visibility_Collapsed);
        }
        else
        {
            PortraitImage_->SetVisibility(Aero::Visibility_Visible);
            PortraitImage_->SetSource(Aero::MakePtr<Aero::BitmapImage>(
                Aero::Uri(PortraitIconUri_.c_str())));
        }
    }

    // --- Command Card ---
    for (int i = 0; i < 9; ++i)
    {
        const auto& Cmd = CommandStates_[i];
        bool Show = Cmd.Visible;
        SetElementVisibility(CommandButtons_[i], Show);

        if (CommandIcons_[i])
        {
            if (Cmd.IconUri != CommandAppliedIconUris_[i])
            {
                CommandAppliedIconUris_[i] = Cmd.IconUri;
                if (Cmd.IconUri.empty())
                {
                    CommandIcons_[i]->SetSource(nullptr);
                    CommandIcons_[i]->SetVisibility(Aero::Visibility_Collapsed);
                }
                else
                {
                    CommandIcons_[i]->SetSource(Aero::MakePtr<Aero::BitmapImage>(
                        Aero::Uri(Cmd.IconUri.c_str())));
                    CommandIcons_[i]->SetVisibility(Aero::Visibility_Visible);
                }
            }
        }

        if (CommandLabels_[i])
        {
            CommandLabels_[i]->SetText(Cmd.Label.c_str());
            CommandLabels_[i]->SetVisibility(
                Cmd.Label.empty() ? Aero::Visibility_Collapsed : Aero::Visibility_Visible);
        }

        // Button states
        SetElementVisibility(CommandActiveHighlights_[i], Cmd.Active);
        SetElementVisibility(CommandHoverHighlights_[i], HoveredCommandIndex_ == i && !Cmd.Active);
        SetElementVisibility(CommandPressedBorders_[i], PressedCommandIndex_ == i);
        SetElementVisibility(CommandDisabledBacks_[i], !Cmd.Enabled);
        SetElementVisibility(CommandCooldowns_[i], false); // NYI

        if (CommandNumbers_[i])
        {
            bool HasNum = Cmd.NumberOverlay >= 0 && Cmd.NumberOverlay <= 9;
            CommandNumbers_[i]->SetVisibility(
                HasNum ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
            if (HasNum)
                CommandNumbers_[i]->SetText(std::to_string(Cmd.NumberOverlay).c_str());
        }
    }

    // --- Multi-Selection ---
    if (SelectionPanel_)
        SelectionPanel_->SetVisibility(Aero::Visibility_Visible);

    for (int i = 0; i < 6; ++i)
    {
        SetElementVisibility(SelectSlots_[i], MultiSelectVisible_[i]);
        if (MultiSelectVisible_[i] && SelectColors_[i])
        {
            uint32_t Color = MultiSelectStates_[i].PlayerColor;
            SelectColors_[i]->SetBackground(MakeColorBrush(
                static_cast<uint8_t>((Color >> 16) & 0xFF),
                static_cast<uint8_t>((Color >> 8) & 0xFF),
                static_cast<uint8_t>(Color & 0xFF),
                static_cast<uint8_t>((Color >> 24) & 0xFF)).GetPtr());
        }
        if (MultiSelectVisible_[i] && SelectHps_[i])
        {
            SelectHps_[i]->SetWidth(44.0f * MultiSelectStates_[i].HpRatio);
            uint32_t Tint = HealthBarTint(MultiSelectStates_[i].HpRatio);
            SelectHps_[i]->SetBackground(MakeColorBrush(
                static_cast<uint8_t>((Tint >> 16) & 0xFF),
                static_cast<uint8_t>((Tint >> 8) & 0xFF),
                static_cast<uint8_t>(Tint & 0xFF),
                static_cast<uint8_t>((Tint >> 24) & 0xFF)).GetPtr());
        }
    }

    // --- Tooltip ---
    if (TooltipPanel_)
    {
        TooltipPanel_->SetVisibility(
            TooltipVisible_ ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
    }
    if (TooltipText_)
        TooltipText_->SetText(TooltipValue_.c_str());

    bool HasCosts = TooltipCosts_.Minerals > 0 || TooltipCosts_.Gas > 0;
    SetElementVisibility(TooltipCostRow_, HasCosts);

    bool HasMinerals = TooltipCosts_.Minerals > 0;
    SetElementVisibility(TooltipMineralCost_, HasMinerals);
    if (TooltipMineralCostText_)
        TooltipMineralCostText_->SetText(
            HasMinerals ? std::to_string(TooltipCosts_.Minerals).c_str() : "");

    bool HasGas = TooltipCosts_.Gas > 0;
    SetElementVisibility(TooltipGasCost_, HasGas);
    if (TooltipGasCostText_)
        TooltipGasCostText_->SetText(
            HasGas ? std::to_string(TooltipCosts_.Gas).c_str() : "");

    if (TooltipVisible_ && TooltipPanel_)
    {
        float Tx = std::clamp(TooltipX_ + 12.0f, 12.0f, static_cast<float>(Width_) - 280.0f);
        float Ty = std::clamp(TooltipY_ - 20.0f, 12.0f, static_cast<float>(Height_) - 150.0f);
        Aero::Canvas::SetLeft(TooltipPanel_, Tx);
        Aero::Canvas::SetTop(TooltipPanel_, Ty);
    }
}

ScreenRect GuiHudHost::GetElementRect(Aero::FrameworkElement* Element) const
{
    if (!Element || !View_) return {};

    Aero::Point ScreenPos = Element->PointToScreen(Aero::Point{0.0f, 0.0f});
    float W = Element->GetActualWidth();
    float H = Element->GetActualHeight();

    return {ScreenPos.x, ScreenPos.y, W, H};
}

void GuiHudHost::SetElementVisibility(Aero::FrameworkElement* Element, bool Visible)
{
    if (!Element) return;
    Element->SetVisibility(Visible ? Aero::Visibility_Visible : Aero::Visibility_Collapsed);
}

} // namespace Game::Client::UI
