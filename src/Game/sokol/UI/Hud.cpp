#include "Game/sokol/UI/Hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "Core/ECS/EntityManager.h"
#include "Core/Platform/Platform.h"
#include "Game/MovementTuning.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Render/sokol/Color.h"
#include "Render/sokol/Draw2D.h"
#include "Resource/sokol/ResourceCacheSokol.h"

namespace Game::SokolDemo::UI
{

namespace
{
    using Game::Systems::TerranResearchId;

    constexpr int FhdLogicalWidth = 1920;
    constexpr int FhdLogicalHeight = 1080;
    constexpr int ClassicConsoleSourceWidth = 640;
    constexpr int ClassicConsoleLeftSrcWidth = 176;
    constexpr int ClassicConsoleRightSrcWidth = 216;
    constexpr int TerranConsoleLeftSourceWidth = 1970;
    constexpr int TerranConsoleLeftSourceHeight = 700;
    constexpr int TerranConsoleRightSourceWidth = 2286;
    constexpr int TerranConsoleRightSourceHeight = 700;

    constexpr int ChkScv = 0x07;

    constexpr int ZergDrone = 41;
    constexpr int ZergOverlord = 44;
    constexpr int ZergZergling = 42;
    constexpr int ZergHydralisk = 43;
    constexpr int ZergMutalisk = 46;
    constexpr int ZergScourge = 47;
    constexpr int ZergQueen = 49;
    constexpr int ZergUltralisk = 48;
    constexpr int ZergHatchery = 86;
    constexpr int ZergLair = 87;
    constexpr int ZergHive = 88;

    constexpr int ProtoProbe = 64;
    constexpr int ProtoGateway = 129;
    constexpr int ProtoNexus = 126;
    constexpr int ProtoRoboticsFacility = 134;
    constexpr int ProtoStargate = 135;

    constexpr int IconBuild = 0;
    constexpr int IconBack = 1;
    constexpr int IconMore = 2;
    constexpr int IconCommand = -1;
    constexpr int IconWeapons = 4;
    constexpr int IconArmor = 5;

    constexpr int CmdIconMove = 231;
    constexpr int CmdIconStop = 232;
    constexpr int CmdIconHold = 249;
    constexpr int CmdIconPatrol = 254;
    constexpr int CmdIconAttackMove = 290;
    constexpr int CmdIconFollow = 309;
    constexpr int CmdIconHarvest = 346;

    enum PanelPage : int
    {
        PageNone = 0,
        PageTerranScvMain,
        PageTerranScvBuild1,
        PageTerranScvBuild2,
        PageTerranUnitCommands,
        PageTerranMarine,
        PageTerranFirebat,
        PageTerranMedic,
        PageTerranGhost,
        PageTerranVulture,
        PageTerranGoliath,
        PageTerranSiegeTank,
        PageTerranWraith,
        PageTerranDropship,
        PageTerranScienceVessel,
        PageTerranBattlecruiser,
        PageTerranValkyrie,
        PageTerranPassiveBuilding,
        PageTerranComsatStation,
        PageTerranNuclearSilo,
        PageTerranBunker,
        PageTerranCommandCenter,
        PageTerranBarracks,
        PageTerranFactory,
        PageTerranStarport,
        PageTerranEngineeringBay,
        PageTerranArmory,
        PageTerranAcademy,
        PageTerranScienceFacility,
        PageTerranMachineShop,
        PageTerranControlTower,
        PageTerranCovertOps,
        PageTerranPhysicsLab,
        PageZergDroneMain,
        PageZergDroneBuild,
        PageZergHatchery,
        PageProtossProbeMain,
        PageProtossProbeBuild,
        PageProtossNexus,
        PageProtossGateway,
        PageProtossRoboticsFacility,
        PageProtossStargate
    };

    int FindFirstSelectedEntityId(const std::vector<int>& SelectedIds)
    {
        return SelectedIds.empty() ? -1 : SelectedIds.front();
    }

    int FindSelectedChkUnitId(const EntityManager& Entities, int SelectedEntityId)
    {
        if (SelectedEntityId < 0)
            return -1;
        return Entities.GetEntity(SelectedEntityId).ChkUnitId;
    }

    std::string DisplayLabelForDef(int DefID)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        if (Def.Name == "CommandCenter") return "Command Center";
        if (Def.Name == "SupplyDepot") return "Supply Depot";
        if (Def.Name == "ScienceFacility") return "Science Facility";
        if (Def.Name == "ScienceVessel") return "Science Vessel";
        if (Def.Name == "SiegeTank") return "Siege Tank";
        if (Def.Name == "EngineeringBay") return "Engineering Bay";
        if (Def.Name == "ControlTower") return "Control Tower";
        if (Def.Name == "MachineShop") return "Machine Shop";
        if (Def.Name == "ComsatStation") return "Comsat Station";
        if (Def.Name == "NuclearSilo") return "Nuclear Silo";
        if (Def.Name == "CovertOps") return "Covert Ops";
        if (Def.Name == "PhysicsLab") return "Physics Lab";
        if (Def.Name == "MissileTurret") return "Missile Turret";
        return Def.Name;
    }

    int IconForDef(int DefID)
    {
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        return Def.DefaultIconIndex >= 0 ? Def.DefaultIconIndex : DefID;
    }

    int CommandIconForCommand(HudCommandId CommandId)
    {
        switch (CommandId)
        {
            case HudCommandId::Move: return CmdIconMove;
            case HudCommandId::AttackMove: return CmdIconAttackMove;
            case HudCommandId::Stop: return CmdIconStop;
            case HudCommandId::Hold: return CmdIconHold;
            case HudCommandId::Patrol: return CmdIconPatrol;
            case HudCommandId::Follow: return CmdIconFollow;
            case HudCommandId::Harvest: return CmdIconHarvest;
            default: return -1;
        }
    }

    int CommandIconForResearch(TerranResearchId ResearchId)
    {
        switch (ResearchId)
        {
            case TerranResearchId::InfantryWeapons:
            case TerranResearchId::InfantryArmor:
            case TerranResearchId::StimPacks:
            case TerranResearchId::U238Shells:
                return IconForDef(Game::Races::GetUnitDefID("Marine"));
            case TerranResearchId::VehicleWeapons:
            case TerranResearchId::VehicleArmor:
            case TerranResearchId::SpiderMines:
            case TerranResearchId::IonThrusters:
                return IconForDef(Game::Races::GetUnitDefID("Vulture"));
            case TerranResearchId::SiegeMode:
                return IconForDef(Game::Races::GetUnitDefID("SiegeTank"));
            case TerranResearchId::ShipWeapons:
            case TerranResearchId::ShipArmor:
            case TerranResearchId::CloakingField:
                return IconForDef(Game::Races::GetUnitDefID("Wraith"));
            case TerranResearchId::Lockdown:
            case TerranResearchId::MoebiusReactor:
                return IconForDef(Game::Races::GetUnitDefID("Ghost"));
            case TerranResearchId::YamatoGun:
            case TerranResearchId::ColossusReactor:
                return IconForDef(Game::Races::GetUnitDefID("Battlecruiser"));
            default:
                return -1;
        }
    }

    struct LogicalCanvas
    {
        int X = 0;
        int Y = 0;
        int Width = 0;
        int Height = 0;
        float Scale = 1.0f;
    };

    struct RectI
    {
        int X = 0;
        int Y = 0;
        int W = 0;
        int H = 0;

        bool Contains(int Px, int Py) const
        {
            return Px >= X && Px < X + W && Py >= Y && Py < Y + H;
        }
    };

    constexpr RectI TerranConsoleLeftMinimapOpening = { 440, 169, 522, 516 };
    constexpr int TerranConsoleCenterSourceWidth = 1156;
    constexpr int TerranConsoleCenterSourceHeight = 512;
    constexpr RectI TerranConsoleCenterOpening = { 45, 117, 1010, 393 };
    constexpr int TerranConsolePortraitSourceWidth = 256;
    constexpr int TerranConsolePortraitSourceHeight = 256;
    constexpr RectI TerranConsolePortraitOpening = { 27, 3, 207, 237 };
    constexpr std::array<RectI, 9> TerranConsoleRightCommandSlots =
    {{
        { 1318, 196, 127, 124 }, { 1507, 196, 127, 124 }, { 1694, 196, 127, 124 },
        { 1318, 373, 127, 124 }, { 1507, 373, 127, 124 }, { 1694, 373, 127, 124 },
        { 1318, 549, 127, 124 }, { 1507, 549, 127, 124 }, { 1694, 549, 127, 124 }
    }};
    constexpr int TerranCommandSlotOutsetX = 10;
    constexpr int TerranCommandSlotOutsetY = 12;
    constexpr int TerranCommandSlotYOffset = 18;
    constexpr std::array<int, 3> TerranCommandRowOffsetPx = {{ 3, -3, -8 }};
    constexpr std::array<int, 3> TerranCommandColOffsetPx = {{ 2, 1, 0 }};
    constexpr int TerranCommandWidthAdjustPx = 1;
    constexpr int TerranCommandHeightAdjustPx = -1;
    constexpr size_t TerranPortraitAnchorSlotIndex = 3;
    constexpr int TerranPortraitAnchorOffsetX = -380;
    constexpr int TerranPortraitAnchorOffsetY = 60;
    constexpr int TerranLedInsetPx = 30;
    constexpr int TerranLedOffsetYPx = 10;

    struct HudLayout
    {
        RectI ConsoleRect;
        RectI MinimapRect;
        RectI InfoFrameRect;
        RectI InfoRect;
        RectI PortraitFrameRect;
        RectI AuxiliaryRect;
        RectI PreviewRect;
        RectI QueueRect;
        std::array<RectI, 9> CommandRects = {};

        int HudTop = 0;
        int HudHeight = 0;
    };

    struct AtlasRect
    {
        int X = 0;
        int Y = 0;
        int W = 0;
        int H = 0;
    };

    struct ScvInfoLayout
    {
        RectI PortraitRect;
        RectI HeaderRect;
        RectI StatIconRowRect;
        RectI HpTextRect;
        RectI HpBarRect;
        RectI FooterCardA;
        RectI FooterCardB;
    };

    LogicalCanvas ResolveLogicalCanvas(int VW, int VH)
    {
        LogicalCanvas Canvas = {};
        if (VW <= 0 || VH <= 0)
            return Canvas;

        const float ScaleX = (float)VW / (float)FhdLogicalWidth;
        const float ScaleY = (float)VH / (float)FhdLogicalHeight;
        Canvas.Scale = std::max(0.01f, std::min(ScaleX, ScaleY));
        Canvas.Width = std::max(1, (int)std::lround((float)FhdLogicalWidth * Canvas.Scale));
        Canvas.Height = std::max(1, (int)std::lround((float)FhdLogicalHeight * Canvas.Scale));
        Canvas.X = (VW - Canvas.Width) / 2;
        Canvas.Y = VH - Canvas.Height;
        return Canvas;
    }

    constexpr RectI FhdConsoleAnchor = { 0, 760, 1920, 320 };
    constexpr RectI FhdMinimapAnchor = { 72, 808, 244, 232 };
    constexpr RectI FhdInfoAnchor = { 364, 792, 744, 240 };
    constexpr RectI FhdAuxAnchor = { 1142, 798, 176, 218 };
    constexpr RectI FhdQueueAnchor = { 1152, 918, 156, 98 };
    constexpr std::array<RectI, 9> FhdCommandAnchors =
    {{
        { 1392, 790, 112, 84 }, { 1518, 790, 112, 84 }, { 1644, 790, 112, 84 },
        { 1392, 886, 112, 84 }, { 1518, 886, 112, 84 }, { 1644, 886, 112, 84 },
        { 1392, 982, 112, 84 }, { 1518, 982, 112, 84 }, { 1644, 982, 112, 84 }
    }};
    constexpr AtlasRect AggIconMinerals = { 0, 0, 16, 16 };
    constexpr AtlasRect AggIconGas = { 17, 0, 16, 16 };
    constexpr AtlasRect AggIconSupply = { 34, 0, 16, 16 };

    RectI ScaleLogicalRect(const RectI& Anchor, const LogicalCanvas& Canvas)
    {
        const auto ScaleX = [&](int Value) -> int
        {
            return Canvas.X + (int)std::lround((double)Value * (double)Canvas.Scale);
        };
        const auto ScaleY = [&](int Value) -> int
        {
            return Canvas.Y + (int)std::lround((double)Value * (double)Canvas.Scale);
        };

        const int Left = ScaleX(Anchor.X);
        const int Top = ScaleY(Anchor.Y);
        const int Right = ScaleX(Anchor.X + Anchor.W);
        const int Bottom = ScaleY(Anchor.Y + Anchor.H);
        return { Left, Top, std::max(1, Right - Left), std::max(1, Bottom - Top) };
    }

    RectI InsetRect(const RectI& Rect, int InsetX, int InsetY)
    {
        const int SafeInsetX = std::max(0, std::min(InsetX, std::max(0, Rect.W / 2 - 1)));
        const int SafeInsetY = std::max(0, std::min(InsetY, std::max(0, Rect.H / 2 - 1)));
        return
        {
            Rect.X + SafeInsetX,
            Rect.Y + SafeInsetY,
            std::max(1, Rect.W - SafeInsetX * 2),
            std::max(1, Rect.H - SafeInsetY * 2)
        };
    }

    HudLayout BuildFhdHudLayout(int VW, int VH)
    {
        const auto Canvas = ResolveLogicalCanvas(VW, VH);
        HudLayout Layout = {};
        const int HudHeight = std::max(1, (int)std::lround((float)FhdConsoleAnchor.H * Canvas.Scale));
        Layout.ConsoleRect = { 0, VH - HudHeight, VW, HudHeight };
        const float BaseScale = (float)Layout.ConsoleRect.H /
            (float)std::max(TerranConsoleLeftSourceHeight, TerranConsoleRightSourceHeight);
        const int LeftBaseH = std::max(1, (int)std::lround((float)TerranConsoleLeftSourceHeight * BaseScale));
        const int LeftBaseX = 0;
        const int LeftBaseY = Layout.ConsoleRect.Y + Layout.ConsoleRect.H - LeftBaseH;
        const int RightBaseW = std::max(1, (int)std::lround((float)TerranConsoleRightSourceWidth * BaseScale));
        const int RightBaseH = std::max(1, (int)std::lround((float)TerranConsoleRightSourceHeight * BaseScale));
        const int RightBaseX = VW - RightBaseW;
        const int RightBaseY = Layout.ConsoleRect.Y + Layout.ConsoleRect.H - RightBaseH;

        Layout.MinimapRect =
        {
            LeftBaseX + (int)std::lround((float)TerranConsoleLeftMinimapOpening.X * BaseScale),
            LeftBaseY + (int)std::lround((float)TerranConsoleLeftMinimapOpening.Y * BaseScale),
            std::max(1, (int)std::lround((float)TerranConsoleLeftMinimapOpening.W * BaseScale)),
            std::max(1, (int)std::lround((float)TerranConsoleLeftMinimapOpening.H * BaseScale))
        };

        const int PortraitOuterW = std::max(1, (int)std::lround((float)TerranConsolePortraitSourceWidth * BaseScale));
        const int PortraitOuterH = std::max(1, (int)std::lround((float)TerranConsolePortraitSourceHeight * BaseScale));
        const int CenterOuterW = std::max(1, (int)std::lround((float)TerranConsoleCenterSourceWidth * BaseScale));
        const int CenterOuterH = std::max(1, (int)std::lround((float)TerranConsoleCenterSourceHeight * BaseScale));
        const RectI CenterOuterRect =
        {
            Layout.ConsoleRect.X + (Layout.ConsoleRect.W - CenterOuterW) / 2,
            Layout.ConsoleRect.Y + Layout.ConsoleRect.H - CenterOuterH,
            CenterOuterW,
            CenterOuterH
        };

        Layout.InfoFrameRect = CenterOuterRect;
        Layout.InfoRect =
        {
            CenterOuterRect.X + (int)std::lround((float)TerranConsoleCenterOpening.X * BaseScale),
            CenterOuterRect.Y + (int)std::lround((float)TerranConsoleCenterOpening.Y * BaseScale),
            std::max(1, (int)std::lround((float)TerranConsoleCenterOpening.W * BaseScale)),
            std::max(1, (int)std::lround((float)TerranConsoleCenterOpening.H * BaseScale))
        };
        Layout.QueueRect = ScaleLogicalRect(FhdQueueAnchor, Canvas);
        for (size_t Index = 0; Index < TerranConsoleRightCommandSlots.size(); ++Index)
        {
            const auto& SourceRect = TerranConsoleRightCommandSlots[Index];
            const int Row = (int)(Index / 3u);
            const int Col = (int)(Index % 3u);
            Layout.CommandRects[Index] =
            {
                RightBaseX + (int)std::lround((float)(SourceRect.X - TerranCommandSlotOutsetX) * BaseScale) + TerranCommandColOffsetPx[(size_t)Col],
                RightBaseY + (int)std::lround((float)(SourceRect.Y - TerranCommandSlotOutsetY + TerranCommandSlotYOffset) * BaseScale) + TerranCommandRowOffsetPx[(size_t)Row],
                std::max(1, (int)std::lround((float)(SourceRect.W + TerranCommandSlotOutsetX * 2) * BaseScale) + TerranCommandWidthAdjustPx),
                std::max(1, (int)std::lround((float)(SourceRect.H + TerranCommandSlotOutsetY * 2) * BaseScale) + TerranCommandHeightAdjustPx)
            };
        }
        const RectI& PortraitAnchorSourceRect = TerranConsoleRightCommandSlots[TerranPortraitAnchorSlotIndex];
        Layout.PortraitFrameRect =
        {
            RightBaseX + (int)std::lround((float)(PortraitAnchorSourceRect.X + TerranPortraitAnchorOffsetX) * BaseScale),
            RightBaseY + (int)std::lround((float)(PortraitAnchorSourceRect.Y + TerranPortraitAnchorOffsetY) * BaseScale),
            PortraitOuterW,
            PortraitOuterH
        };
        Layout.AuxiliaryRect =
        {
            Layout.PortraitFrameRect.X + (int)std::lround((float)TerranConsolePortraitOpening.X * BaseScale),
            Layout.PortraitFrameRect.Y + (int)std::lround((float)TerranConsolePortraitOpening.Y * BaseScale),
            std::max(1, (int)std::lround((float)TerranConsolePortraitOpening.W * BaseScale)),
            std::max(1, (int)std::lround((float)TerranConsolePortraitOpening.H * BaseScale))
        };
        const int PreviewInsetX = std::max(10, (int)std::lround(12.0f * BaseScale));
        const int PreviewInsetY = std::max(10, (int)std::lround(10.0f * BaseScale));
        const int PreviewSide =
            std::clamp(std::min((int)std::lround((float)Layout.InfoRect.W * 0.18f),
                                (int)std::lround((float)Layout.InfoRect.H * 0.52f)),
                       std::max(72, (int)std::lround(78.0f * BaseScale)),
                       std::max(84, (int)std::lround(112.0f * BaseScale)));
        Layout.PreviewRect =
        {
            Layout.InfoRect.X + PreviewInsetX,
            Layout.InfoRect.Y + PreviewInsetY,
            PreviewSide,
            PreviewSide
        };
        Layout.HudTop = Layout.ConsoleRect.Y;
        Layout.HudHeight = Layout.ConsoleRect.H;
        return Layout;
    }

    HudLayout ResolveHudLayout(int VW, int VH)
    {
        return BuildFhdHudLayout(VW, VH);
    }

    ScvInfoLayout ResolveScvInfoLayout(const RectI& InfoRect)
    {
        const int Padding = 10;
        const int PortraitSize = std::clamp(InfoRect.H - 96, 72, 108);
        const int PortraitY = InfoRect.Y + std::max(8, (InfoRect.H - PortraitSize - 44) / 2 - 10);
        const int HeaderX = InfoRect.X + Padding + PortraitSize + 14;
        const int HeaderW = std::max(120, InfoRect.W - PortraitSize - Padding * 3);
        const int FooterY = InfoRect.Y + InfoRect.H - 42;
        const int FooterH = 28;
        const int FooterGap = 8;
        const int FooterLeftW = std::max(88, (HeaderW - FooterGap) / 2);
        const int FooterRightW = std::max(88, HeaderW - FooterLeftW - FooterGap);

        ScvInfoLayout Layout = {};
        Layout.PortraitRect = { InfoRect.X + Padding, PortraitY, PortraitSize, PortraitSize };
        Layout.HeaderRect = { HeaderX, InfoRect.Y + 14, HeaderW, std::max(90, PortraitSize - 6) };
        Layout.StatIconRowRect = { HeaderX, Layout.HeaderRect.Y + 62, HeaderW, 16 };
        Layout.HpTextRect = { Layout.PortraitRect.X, Layout.PortraitRect.Y + Layout.PortraitRect.H + 8, Layout.PortraitRect.W, 14 };
        Layout.HpBarRect = { Layout.PortraitRect.X, Layout.HpTextRect.Y + 14, std::max(64, PortraitSize), 10 };
        Layout.FooterCardA = { HeaderX, FooterY, FooterLeftW, FooterH };
        Layout.FooterCardB = { HeaderX + FooterLeftW + FooterGap, FooterY, FooterRightW, FooterH };
        return Layout;
    }

    std::string Ellipsize(const std::string& Text, size_t MaxChars)
    {
        if (Text.size() <= MaxChars || MaxChars <= 3)
            return Text.size() <= MaxChars ? Text : Text.substr(0, MaxChars);
        return Text.substr(0, MaxChars - 3) + "...";
    }

    std::string ResolvePortraitKeyForSelection(int TerranDefId)
    {
        if (TerranDefId < 0)
            return {};

        if (TerranDefId == Game::Races::GetUnitDefID("SCV")) return "tscv";
        if (TerranDefId == Game::Races::GetUnitDefID("Marine")) return "tmarine";
        if (TerranDefId == Game::Races::GetUnitDefID("Firebat")) return "tfirebat";
        if (TerranDefId == Game::Races::GetUnitDefID("Medic")) return "tmedic";
        if (TerranDefId == Game::Races::GetUnitDefID("Ghost")) return "tghost";
        if (TerranDefId == Game::Races::GetUnitDefID("Vulture")) return "tvulture";
        if (TerranDefId == Game::Races::GetUnitDefID("Goliath")) return "tgoliath";
        if (TerranDefId == Game::Races::GetUnitDefID("SiegeTank")) return "ttank";
        if (TerranDefId == Game::Races::GetUnitDefID("Wraith")) return "tphoenix";
        if (TerranDefId == Game::Races::GetUnitDefID("Dropship")) return "tdropshi";
        if (TerranDefId == Game::Races::GetUnitDefID("ScienceVessel")) return "tvessel";
        if (TerranDefId == Game::Races::GetUnitDefID("Battlecruiser")) return "tbattlec";
        if (TerranDefId == Game::Races::GetUnitDefID("Valkyrie")) return "tbomber";
        return {};
    }

    Resource::SokolBridge::CommandButtonSkinSet SkinSetForPanelCategory(int CategoryValue)
    {
        using Resource::SokolBridge::CommandButtonSkinSet;
        switch (CategoryValue)
        {
            case 3: return CommandButtonSkinSet::Protoss;
            case 2: return CommandButtonSkinSet::Zerg;
            case 1:
            case 0:
            default:
                return CommandButtonSkinSet::Terran;
        }
    }

    const Resource::SokolBridge::SokolImage* FindPreferredButtonSkin(
        const std::vector<Resource::SokolBridge::SokolImage>* Skins,
        int PreferredIndex)
    {
        if (!Skins || Skins->empty())
            return nullptr;

        if (PreferredIndex >= 0 && PreferredIndex < (int)Skins->size() && (*Skins)[(size_t)PreferredIndex].Valid())
            return &(*Skins)[(size_t)PreferredIndex];

        for (const auto& Skin : *Skins)
        {
            if (Skin.Valid())
                return &Skin;
        }
        return nullptr;
    }

    bool IsNeutralButtonIcon(const CmdButtonDef& Button)
    {
        return Button.CommandId == HudCommandId::Stop || Button.Kind == ActionKind::Back;
    }

    Render::Color SkinTintForState(bool HasCommand, bool Enabled, bool Hovered, bool Active)
    {
        if (!HasCommand)
            return Render::Color(90, 90, 96, 80);
        if (!Enabled)
            return Render::Color(118, 128, 140, 185);
        if (Active)
            return Render::Color(255, 255, 255, 255);
        if (Hovered)
            return Render::Color(232, 240, 255, 250);
        return Render::Colors::White();
    }

    Render::Color IconTintForState(const CmdButtonDef& Button, bool Enabled, bool Hovered, bool Active)
    {
        const bool Neutral = IsNeutralButtonIcon(Button);
        if (!Enabled)
            return Neutral ? Render::Color(125, 128, 132, 205) : Render::Color(124, 118, 82, 205);
        if (Neutral)
            return Active ? Render::Color(255, 255, 255, 255)
                          : (Hovered ? Render::Color(245, 245, 248, 255)
                                     : Render::Color(230, 230, 236, 255));
        if (Active)
            return Render::Color(255, 246, 138, 255);
        if (Hovered)
            return Render::Color(255, 239, 108, 255);
        return Render::Color(245, 226, 78, 255);
    }

    Render::Color FallbackIconTintForState(bool Enabled, bool Active)
    {
        if (!Enabled)
            return Render::Color(145, 145, 145, 190);
        if (Active)
            return Render::Color(255, 250, 205, 255);
        return Render::Colors::White();
    }

    bool IsLeveledResearch(TerranResearchId ResearchId)
    {
        switch (ResearchId)
        {
            case TerranResearchId::InfantryWeapons:
            case TerranResearchId::InfantryArmor:
            case TerranResearchId::VehicleWeapons:
            case TerranResearchId::VehicleArmor:
            case TerranResearchId::ShipWeapons:
            case TerranResearchId::ShipArmor:
                return true;
            default:
                return false;
        }
    }

    std::string ResearchLabel(int PlayerID,
                              TerranResearchId ResearchId,
                              const std::string& BaseLabel)
    {
        if (!IsLeveledResearch(ResearchId))
            return BaseLabel;

        const int Level = Game::Systems::UpgradeSystem::GetResearchLevel(PlayerID, ResearchId);
        if (Level >= 3)
            return BaseLabel + " Max";
        return BaseLabel + " +" + std::to_string(Level + 1);
    }

    std::string DisplayLabelForChkUnitId(int ChkUnitId)
    {
        switch (ChkUnitId)
        {
            case ZergDrone: return "Drone";
            case ZergOverlord: return "Overlord";
            case ZergZergling: return "Zergling";
            case ZergHydralisk: return "Hydralisk";
            case ZergMutalisk: return "Mutalisk";
            case ZergScourge: return "Scourge";
            case ZergQueen: return "Queen";
            case ZergUltralisk: return "Ultralisk";
            case ZergHatchery: return "Hatchery";
            case ZergLair: return "Lair";
            case ZergHive: return "Hive";
            case ProtoProbe: return "Probe";
            case ProtoGateway: return "Gateway";
            case ProtoNexus: return "Nexus";
            case ProtoRoboticsFacility: return "Robotics Facility";
            case ProtoStargate: return "Stargate";
            case 11:
            case 12:
            case 95:
            case 99:
            case 100:
            case 101:
            case 102:
            case 176:
            case 177:
            case 178:
                return "Mineral Field";
            case 103:
            case 188:
                return "Vespene Geyser";
            default:
                break;
        }
        return "Unit " + std::to_string(ChkUnitId);
    }

    std::string RaceLabel(Game::Components::Race RaceType)
    {
        switch (RaceType)
        {
            case Game::Components::Race::Terran: return "Terran";
            case Game::Components::Race::Zerg: return "Zerg";
            case Game::Components::Race::Protoss: return "Protoss";
        }
        return "Unknown";
    }

    std::string CommandLabel(Game::Components::CommandType CurrentCommand)
    {
        using Game::Components::CommandType;
        switch (CurrentCommand)
        {
            case CommandType::Move: return "Moving";
            case CommandType::AttackMove: return "Attack Move";
            case CommandType::Attack: return "Attacking";
            case CommandType::Patrol: return "Patrolling";
            case CommandType::HoldPosition: return "Holding";
            case CommandType::Stop: return "Idle";
            case CommandType::Harvest: return "Harvesting";
            case CommandType::Build: return "Building";
            case CommandType::Train: return "Training";
            case CommandType::Upgrade: return "Researching";
            case CommandType::UseAbility: return "Using Ability";
            case CommandType::Follow: return "Following";
        }
        return "Idle";
    }

    std::string HarvestStateLabel(Game::Components::HarvestState State)
    {
        using Game::Components::HarvestState;
        switch (State)
        {
            case HarvestState::Idle: return "Idle";
            case HarvestState::MovingToResource: return "To resource";
            case HarvestState::Mining: return "Mining";
            case HarvestState::MovingToDepot: return "Returning";
            case HarvestState::Depositing: return "Depositing";
        }
        return "Idle";
    }

    std::string RoleLabelForScv(int PlayerID)
    {
        if (PlayerID <= 0)
            return "Private";
        return "Private P" + std::to_string(PlayerID);
    }
}

CmdButtonDef::CmdButtonDef()
    : ResearchId(Game::Systems::TerranResearchId::None)
{
}

void Hud::Init(Resource::SokolBridge::ResourceCacheSokol* Cache)
{
    mCache = Cache;
}

void Hud::Shutdown() {}

const Resource::SokolBridge::SokolImage* Hud::GetLegacyIcon(int IconIndex) const
{
    if (!mCache)
        return nullptr;
    auto* Icons = mCache->GetUIIcons();
    if (!Icons || IconIndex < 0 || IconIndex >= (int)Icons->size())
        return nullptr;
    return &(*Icons)[IconIndex];
}

static int LegacyIconForVisual(HudVisualId VisualId)
{
    switch (VisualId)
    {
        case HudVisualId::BasicBuild: return -1;
        case HudVisualId::AdvancedBuild: return -1;
        case HudVisualId::Back: return -1;
        default: return -1;
    }
}

static int CommandIconForVisual(HudVisualId VisualId)
{
    switch (VisualId)
    {
        case HudVisualId::BasicBuild:
            return IconForDef(Game::Races::GetUnitDefID("CommandCenter"));
        case HudVisualId::AdvancedBuild:
            return IconForDef(Game::Races::GetUnitDefID("Factory"));
        default:
            return -1;
    }
}

const Resource::SokolBridge::SokolImage* Hud::GetCommandIcon(int IconIndex) const
{
    if (!mCache)
        return nullptr;
    auto* Icons = mCache->GetCommandIcons();
    if (!Icons || IconIndex < 0 || IconIndex >= (int)Icons->size())
        return nullptr;
    return &(*Icons)[IconIndex];
}

const Resource::SokolBridge::SokolImage* Hud::GetCommandIconMask(int IconIndex) const
{
    if (!mCache)
        return nullptr;
    auto* Icons = mCache->GetCommandIconMasks();
    if (!Icons || IconIndex < 0 || IconIndex >= (int)Icons->size())
        return nullptr;
    return &(*Icons)[IconIndex];
}

const Resource::SokolBridge::SokolImage* Hud::GetFirstValidSpriteFrame(int ChkUnitId) const
{
    if (!mCache || ChkUnitId < 0)
        return nullptr;

    const auto* Frames = mCache->GetSpriteFrames((uint32_t)ChkUnitId);
    if (!Frames || Frames->empty())
        return nullptr;

    for (const auto& Frame : *Frames)
    {
        if (Frame.Valid())
            return &Frame;
    }
    return nullptr;
}

const Resource::SokolBridge::SokolImage* Hud::GetSelectionPreviewImage() const
{
    if (mInfoPanel.PreviewChkUnitId >= 0 && mCache)
    {
        if (const auto* Wireframes = mCache->GetWireframeFrames();
            Wireframes &&
            mInfoPanel.PreviewChkUnitId < (int)Wireframes->size() &&
            (*Wireframes)[(size_t)mInfoPanel.PreviewChkUnitId].Valid())
        {
            return &(*Wireframes)[(size_t)mInfoPanel.PreviewChkUnitId];
        }
    }

    if (mInfoPanel.PreviewDefID >= 0)
    {
        const auto Def = Game::Races::GetUnitDefByID(mInfoPanel.PreviewDefID);
        if (const auto* Icon = GetCommandIcon(IconForDef(mInfoPanel.PreviewDefID));
            Icon && Icon->Valid() && Def.RaceType == Game::Components::Race::Terran)
        {
            return Icon;
        }
    }
    return GetFirstValidSpriteFrame(mInfoPanel.PreviewChkUnitId);
}

const Resource::SokolBridge::SokolImage* Hud::GetButtonImage(const CmdButtonDef& Button) const
{
    if (const int VisualCommandIcon = CommandIconForVisual(Button.VisualId);
        VisualCommandIcon >= 0)
    {
        if (const auto* Icon = GetCommandIcon(VisualCommandIcon); Icon && Icon->Valid())
            return Icon;
    }

    if (const auto* Icon = GetLegacyIcon(LegacyIconForVisual(Button.VisualId)); Icon && Icon->Valid())
        return Icon;

    if (Button.CommandId != HudCommandId::None)
    {
        if (const auto* Icon = GetCommandIcon(CommandIconForCommand(Button.CommandId));
            Icon && Icon->Valid())
        {
            return Icon;
        }
    }

    if (Button.ResearchId != TerranResearchId::None)
    {
        if (const auto* Icon = GetCommandIcon(CommandIconForResearch(Button.ResearchId));
            Icon && Icon->Valid())
        {
            return Icon;
        }
    }

    if (Button.TargetUnitDefID >= 0)
    {
        const auto Def = Game::Races::GetUnitDefByID(Button.TargetUnitDefID);
        if (Def.RaceType == Game::Components::Race::Terran)
        {
            if (const auto* Icon = GetCommandIcon(IconForDef(Button.TargetUnitDefID));
                Icon && Icon->Valid())
            {
                return Icon;
            }
        }

        if (const auto* Icon = GetFirstValidSpriteFrame(Def.ChkUnitID); Icon && Icon->Valid())
            return Icon;
    }

    if (const auto* Icon = GetLegacyIcon(Button.IconIndex); Icon && Icon->Valid())
        return Icon;

    return nullptr;
}

const Resource::SokolBridge::SokolImage* Hud::GetButtonMaskImage(const CmdButtonDef& Button) const
{
    if (const int VisualCommandIcon = CommandIconForVisual(Button.VisualId);
        VisualCommandIcon >= 0)
    {
        if (const auto* Icon = GetCommandIconMask(VisualCommandIcon); Icon && Icon->Valid())
            return Icon;
    }

    if (Button.CommandId != HudCommandId::None)
    {
        if (const auto* Icon = GetCommandIconMask(CommandIconForCommand(Button.CommandId));
            Icon && Icon->Valid())
        {
            return Icon;
        }
    }

    if (Button.ResearchId != TerranResearchId::None)
    {
        if (const auto* Icon = GetCommandIconMask(CommandIconForResearch(Button.ResearchId));
            Icon && Icon->Valid())
        {
            return Icon;
        }
    }

    if (Button.TargetUnitDefID >= 0)
    {
        const auto Def = Game::Races::GetUnitDefByID(Button.TargetUnitDefID);
        if (Def.RaceType == Game::Components::Race::Terran)
        {
            if (const auto* Icon = GetCommandIconMask(IconForDef(Button.TargetUnitDefID));
                Icon && Icon->Valid())
            {
                return Icon;
            }
        }
    }

    return nullptr;
}

bool Hud::CheckMinimapClick(int SX, int SY,
                            int VW, int VH,
                            int MW, int MH,
                            float& OutWX, float& OutWY) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    const auto& MinimapRect = Layout.MinimapRect;
    if (!MinimapRect.Contains(SX, SY))
        return false;
    float U = (float)(SX - MinimapRect.X) / (float)MinimapRect.W;
    float V = (float)(SY - MinimapRect.Y) / (float)MinimapRect.H;
    OutWX = U * (float)(MW * 32);
    OutWY = V * (float)(MH * 32);
    return true;
}

int Hud::CheckCommandPanelClick(int SX, int SY, int VW, int VH) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    for (size_t Index = 0; Index < Layout.CommandRects.size(); ++Index)
    {
        if (Layout.CommandRects[Index].Contains(SX, SY))
            return (int)Index;
    }
    return -1;
}

bool Hud::IsPointInHudBand(int, int SY, int VW, int VH) const
{
    const auto Layout = ResolveHudLayout(VW, VH);
    return SY >= Layout.HudTop || SY < kResourceBarHeight;
}

int Hud::GetHudHeight(int VW, int VH) const
{
    return ResolveHudLayout(VW, VH).HudHeight;
}

void Hud::ResetButtons()
{
    for (auto& Button : mButtonDefs)
        Button = {};
}

void Hud::SetButton(int Index,
                    const std::string& Label,
                    ActionKind Kind,
                    int TargetUnitDefID,
                    HudCommandId CommandId,
                    Game::Systems::TerranResearchId ResearchId,
                    int IconIndex,
                    bool Enabled,
                    int NextMenuPage,
                    HudVisualId VisualId)
{
    if (Index < 0 || Index >= (int)mButtonDefs.size())
        return;
    mButtonDefs[(size_t)Index].Label = Label;
    mButtonDefs[(size_t)Index].Kind = Kind;
    mButtonDefs[(size_t)Index].TargetUnitDefID = TargetUnitDefID;
    mButtonDefs[(size_t)Index].CommandId = CommandId;
    mButtonDefs[(size_t)Index].ResearchId = ResearchId;
    mButtonDefs[(size_t)Index].IconIndex = IconIndex;
    mButtonDefs[(size_t)Index].Enabled = Enabled;
    mButtonDefs[(size_t)Index].NextMenuPage = NextMenuPage;
    mButtonDefs[(size_t)Index].VisualId = VisualId;
}

int Hud::GetDefaultTerranPageForDef(int SelectedTerranDefId) const
{
    const int SCV = Game::Races::GetUnitDefID("SCV");
    const int Marine = Game::Races::GetUnitDefID("Marine");
    const int Firebat = Game::Races::GetUnitDefID("Firebat");
    const int Medic = Game::Races::GetUnitDefID("Medic");
    const int Ghost = Game::Races::GetUnitDefID("Ghost");
    const int Vulture = Game::Races::GetUnitDefID("Vulture");
    const int Goliath = Game::Races::GetUnitDefID("Goliath");
    const int SiegeTank = Game::Races::GetUnitDefID("SiegeTank");
    const int Wraith = Game::Races::GetUnitDefID("Wraith");
    const int Dropship = Game::Races::GetUnitDefID("Dropship");
    const int ScienceVessel = Game::Races::GetUnitDefID("ScienceVessel");
    const int Battlecruiser = Game::Races::GetUnitDefID("Battlecruiser");
    const int Valkyrie = Game::Races::GetUnitDefID("Valkyrie");
    const int CommandCenter = Game::Races::GetUnitDefID("CommandCenter");
    const int ComsatStation = Game::Races::GetUnitDefID("ComsatStation");
    const int NuclearSilo = Game::Races::GetUnitDefID("NuclearSilo");
    const int Bunker = Game::Races::GetUnitDefID("Bunker");
    const int Barracks = Game::Races::GetUnitDefID("Barracks");
    const int Factory = Game::Races::GetUnitDefID("Factory");
    const int Starport = Game::Races::GetUnitDefID("Starport");
    const int EngineeringBay = Game::Races::GetUnitDefID("EngineeringBay");
    const int Armory = Game::Races::GetUnitDefID("Armory");
    const int Academy = Game::Races::GetUnitDefID("Academy");
    const int ScienceFacility = Game::Races::GetUnitDefID("ScienceFacility");
    const int MachineShop = Game::Races::GetUnitDefID("MachineShop");
    const int ControlTower = Game::Races::GetUnitDefID("ControlTower");
    const int CovertOps = Game::Races::GetUnitDefID("CovertOps");
    const int PhysicsLab = Game::Races::GetUnitDefID("PhysicsLab");

    if (SelectedTerranDefId == SCV)
        return PageTerranScvMain;
    if (SelectedTerranDefId == Marine)
        return PageTerranMarine;
    if (SelectedTerranDefId == Firebat)
        return PageTerranFirebat;
    if (SelectedTerranDefId == Medic)
        return PageTerranMedic;
    if (SelectedTerranDefId == Ghost)
        return PageTerranGhost;
    if (SelectedTerranDefId == Vulture)
        return PageTerranVulture;
    if (SelectedTerranDefId == Goliath)
        return PageTerranGoliath;
    if (SelectedTerranDefId == SiegeTank)
        return PageTerranSiegeTank;
    if (SelectedTerranDefId == Wraith)
        return PageTerranWraith;
    if (SelectedTerranDefId == Dropship)
        return PageTerranDropship;
    if (SelectedTerranDefId == ScienceVessel)
        return PageTerranScienceVessel;
    if (SelectedTerranDefId == Battlecruiser)
        return PageTerranBattlecruiser;
    if (SelectedTerranDefId == Valkyrie)
        return PageTerranValkyrie;
    if (SelectedTerranDefId == CommandCenter)
        return PageTerranCommandCenter;
    if (SelectedTerranDefId == ComsatStation)
        return PageTerranComsatStation;
    if (SelectedTerranDefId == NuclearSilo)
        return PageTerranNuclearSilo;
    if (SelectedTerranDefId == Bunker)
        return PageTerranBunker;
    if (SelectedTerranDefId == Barracks)
        return PageTerranBarracks;
    if (SelectedTerranDefId == Factory)
        return PageTerranFactory;
    if (SelectedTerranDefId == Starport)
        return PageTerranStarport;
    if (SelectedTerranDefId == EngineeringBay)
        return PageTerranEngineeringBay;
    if (SelectedTerranDefId == Armory)
        return PageTerranArmory;
    if (SelectedTerranDefId == Academy)
        return PageTerranAcademy;
    if (SelectedTerranDefId == ScienceFacility)
        return PageTerranScienceFacility;
    if (SelectedTerranDefId == MachineShop)
        return PageTerranMachineShop;
    if (SelectedTerranDefId == ControlTower)
        return PageTerranControlTower;
    if (SelectedTerranDefId == CovertOps)
        return PageTerranCovertOps;
    if (SelectedTerranDefId == PhysicsLab)
        return PageTerranPhysicsLab;

    const auto Def = Game::Races::GetUnitDefByID(SelectedTerranDefId);
    return Def.IsBuilding ? PageTerranPassiveBuilding : PageTerranUnitCommands;
}

int Hud::GetDefaultPageForSelection(int SelectedChkUnitId) const
{
    switch (SelectedChkUnitId)
    {
        case ZergDrone: return PageZergDroneMain;
        case ZergHatchery:
        case ZergLair:
        case ZergHive: return PageZergHatchery;
        case ProtoProbe: return PageProtossProbeMain;
        case ProtoNexus: return PageProtossNexus;
        case ProtoGateway: return PageProtossGateway;
        case ProtoRoboticsFacility: return PageProtossRoboticsFacility;
        case ProtoStargate: return PageProtossStargate;
        default: return PageNone;
    }
}

int Hud::ResolveSelectedTerranDefId(int SelectedEntityId,
                                    int SelectedChkUnitId,
                                    const Core::ECS::EntityManager* EcsEntities) const
{
    if (EcsEntities && SelectedEntityId > 0)
    {
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(*EcsEntities);
        if (const auto* Building =
                MutableEM.GetComponent<Game::Components::BuildingComponent>(SelectedEntityId))
        {
            if (Building->BuildingDefID >= 0)
                return Building->BuildingDefID;
        }
    }
    return Game::Races::GetTerranDefIDByChkUnitID(SelectedChkUnitId);
}

void Hud::BuildTerranPage(int SelectedEntityId,
                          int SelectedTerranDefId,
                          const Core::ECS::EntityManager* EcsEntities)
{
    using Game::Systems::UpgradeSystem;

    auto TrainEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::ProductionSystem::CanTrain(*EcsEntities, SelectedEntityId, DefId);
    };

    auto BuildEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::BuildingSystem::CanBuild(
                   *EcsEntities, SelectedEntityId, DefId,
                   Game::Systems::BuildTarget{}, nullptr, false);
    };

    auto PlayerIdForSelection = [&]() -> int
    {
        if (!EcsEntities || SelectedEntityId <= 0)
            return 0;
        auto& MutableEM = const_cast<Core::ECS::EntityManager&>(*EcsEntities);
        if (auto* Player = MutableEM.GetComponent<Game::Components::PlayerComponent>(SelectedEntityId))
            return Player->PlayerID;
        return 0;
    };

    auto SetCommandButton = [&](int Slot, const std::string& Label, HudCommandId CommandId, bool Enabled)
    {
        SetButton(Slot, Label, ActionKind::Command, -1, CommandId,
                  TerranResearchId::None, IconCommand, Enabled);
    };

    auto DisabledAbilityButtonForDef = [&](int Slot, const std::string& Label, int IconDefId)
    {
        SetButton(Slot, Label, ActionKind::None, IconDefId, HudCommandId::None,
                  TerranResearchId::None, IconForDef(IconDefId), false);
    };

    auto DisabledAbilityButtonForResearch = [&](int Slot,
                                                TerranResearchId ResearchId,
                                                const std::string& Label,
                                                int FallbackDefId)
    {
        SetButton(Slot, Label, ActionKind::None, FallbackDefId, HudCommandId::None,
                  ResearchId, IconForDef(FallbackDefId), false);
    };

    auto BuildButtonForDef = [&](int Slot, int DefId)
    {
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Build,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), BuildEnabled(DefId));
    };

    auto TrainButtonForDef = [&](int Slot, int DefId)
    {
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Train,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), TrainEnabled(DefId));
    };

    auto ResearchButton = [&](int Slot,
                              TerranResearchId ResearchId,
                              const std::string& BaseLabel,
                              int IconIndex)
    {
        const int PlayerID = PlayerIdForSelection();
        const bool Enabled = EcsEntities && SelectedEntityId > 0 &&
                             UpgradeSystem::CanStartResearch(*EcsEntities, SelectedEntityId, ResearchId);
        SetButton(Slot,
                  ResearchLabel(PlayerID, ResearchId, BaseLabel),
                  ActionKind::Research,
                  -1,
                  HudCommandId::None,
                  ResearchId,
                  IconIndex,
                  Enabled);
    };

    const int CommandCenter = Game::Races::GetUnitDefID("CommandCenter");
    const int ComsatStation = Game::Races::GetUnitDefID("ComsatStation");
    const int NuclearSilo = Game::Races::GetUnitDefID("NuclearSilo");
    const int SupplyDepot = Game::Races::GetUnitDefID("SupplyDepot");
    const int Refinery = Game::Races::GetUnitDefID("Refinery");
    const int Barracks = Game::Races::GetUnitDefID("Barracks");
    const int EngineeringBay = Game::Races::GetUnitDefID("EngineeringBay");
    const int Bunker = Game::Races::GetUnitDefID("Bunker");
    const int Academy = Game::Races::GetUnitDefID("Academy");
    const int Factory = Game::Races::GetUnitDefID("Factory");
    const int Starport = Game::Races::GetUnitDefID("Starport");
    const int ScienceFacility = Game::Races::GetUnitDefID("ScienceFacility");
    const int Armory = Game::Races::GetUnitDefID("Armory");
    const int MissileTurret = Game::Races::GetUnitDefID("MissileTurret");
    const int Marine = Game::Races::GetUnitDefID("Marine");
    const int Firebat = Game::Races::GetUnitDefID("Firebat");
    const int Medic = Game::Races::GetUnitDefID("Medic");
    const int Ghost = Game::Races::GetUnitDefID("Ghost");
    const int Vulture = Game::Races::GetUnitDefID("Vulture");
    const int SiegeTank = Game::Races::GetUnitDefID("SiegeTank");
    const int Goliath = Game::Races::GetUnitDefID("Goliath");
    const int MachineShop = Game::Races::GetUnitDefID("MachineShop");
    const int Wraith = Game::Races::GetUnitDefID("Wraith");
    const int Dropship = Game::Races::GetUnitDefID("Dropship");
    const int ScienceVessel = Game::Races::GetUnitDefID("ScienceVessel");
    const int Battlecruiser = Game::Races::GetUnitDefID("Battlecruiser");
    const int Valkyrie = Game::Races::GetUnitDefID("Valkyrie");
    const int ControlTower = Game::Races::GetUnitDefID("ControlTower");
    const int SCV = Game::Races::GetUnitDefID("SCV");
    const int CovertOps = Game::Races::GetUnitDefID("CovertOps");
    const int PhysicsLab = Game::Races::GetUnitDefID("PhysicsLab");

    int EffectivePage = mPanelPage;
    if (EffectivePage == PageNone)
        EffectivePage = GetDefaultTerranPageForDef(SelectedTerranDefId);

    switch (EffectivePage)
    {
        case PageTerranScvMain:
            SetCommandButton(0, "Harvest", HudCommandId::Harvest, true);
            SetCommandButton(1, "Move", HudCommandId::Move, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            DisabledAbilityButtonForDef(3, "Repair", SCV);
            SetCommandButton(4, "Hold", HudCommandId::Hold, true);
            SetCommandButton(5, "Patrol", HudCommandId::Patrol, true);
            SetButton(6, "Basic Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconBuild, true, PageTerranScvBuild1, HudVisualId::BasicBuild);
            SetButton(7, "Advanced Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconMore, true, PageTerranScvBuild2, HudVisualId::AdvancedBuild);
            break;

        case PageTerranScvBuild1:
            BuildButtonForDef(0, CommandCenter);
            BuildButtonForDef(1, SupplyDepot);
            BuildButtonForDef(2, Refinery);
            BuildButtonForDef(3, Barracks);
            BuildButtonForDef(4, EngineeringBay);
            BuildButtonForDef(5, Bunker);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageTerranScvMain, HudVisualId::Back);
            break;

        case PageTerranScvBuild2:
            BuildButtonForDef(0, Academy);
            BuildButtonForDef(1, Factory);
            BuildButtonForDef(2, Starport);
            BuildButtonForDef(3, ScienceFacility);
            BuildButtonForDef(4, Armory);
            BuildButtonForDef(5, MissileTurret);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageTerranScvMain, HudVisualId::Back);
            break;

        case PageTerranUnitCommands:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(5, "Follow", HudCommandId::Follow, true);
            break;

        case PageTerranMarine:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            DisabledAbilityButtonForResearch(6, TerranResearchId::StimPacks, "Stim", Marine);
            break;

        case PageTerranFirebat:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            DisabledAbilityButtonForResearch(6, TerranResearchId::StimPacks, "Stim", Firebat);
            break;

        case PageTerranMedic:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Stop", HudCommandId::Stop, true);
            SetCommandButton(2, "Hold", HudCommandId::Hold, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            DisabledAbilityButtonForDef(6, "Heal", Medic);
            break;

        case PageTerranGhost:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            DisabledAbilityButtonForResearch(6, TerranResearchId::Lockdown, "Lockdown", Ghost);
            DisabledAbilityButtonForDef(7, "Cloak", Ghost);
            DisabledAbilityButtonForDef(8, "Nuke", NuclearSilo);
            break;

        case PageTerranVulture:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            DisabledAbilityButtonForResearch(6, TerranResearchId::SpiderMines, "Mines", Vulture);
            break;

        case PageTerranGoliath:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(5, "Follow", HudCommandId::Follow, true);
            break;

        case PageTerranSiegeTank:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Hold", HudCommandId::Hold, true);
            SetCommandButton(4, "Patrol", HudCommandId::Patrol, true);
            DisabledAbilityButtonForResearch(6, TerranResearchId::SiegeMode, "Siege", SiegeTank);
            break;

        case PageTerranWraith:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            DisabledAbilityButtonForResearch(6, TerranResearchId::CloakingField, "Cloak", Wraith);
            break;

        case PageTerranDropship:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Stop", HudCommandId::Stop, true);
            SetCommandButton(2, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(3, "Follow", HudCommandId::Follow, true);
            DisabledAbilityButtonForDef(6, "Load", Dropship);
            DisabledAbilityButtonForDef(7, "Unload", Dropship);
            break;

        case PageTerranScienceVessel:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Stop", HudCommandId::Stop, true);
            SetCommandButton(2, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(3, "Follow", HudCommandId::Follow, true);
            DisabledAbilityButtonForDef(6, "Matrix", ScienceVessel);
            DisabledAbilityButtonForDef(7, "EMP", ScienceVessel);
            DisabledAbilityButtonForDef(8, "Irradiate", ScienceVessel);
            break;

        case PageTerranBattlecruiser:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            DisabledAbilityButtonForResearch(6, TerranResearchId::YamatoGun, "Yamato", Battlecruiser);
            break;

        case PageTerranValkyrie:
            SetCommandButton(0, "Move", HudCommandId::Move, true);
            SetCommandButton(1, "Atk Move", HudCommandId::AttackMove, true);
            SetCommandButton(2, "Stop", HudCommandId::Stop, true);
            SetCommandButton(3, "Patrol", HudCommandId::Patrol, true);
            SetCommandButton(4, "Follow", HudCommandId::Follow, true);
            break;

        case PageTerranPassiveBuilding:
            break;

        case PageTerranComsatStation:
            DisabledAbilityButtonForDef(0, "Scan", ComsatStation);
            break;

        case PageTerranNuclearSilo:
            DisabledAbilityButtonForDef(0, "Launch Nuke", NuclearSilo);
            break;

        case PageTerranBunker:
            DisabledAbilityButtonForDef(0, "Load", Bunker);
            DisabledAbilityButtonForDef(1, "Unload", Bunker);
            break;

        case PageTerranCommandCenter:
            TrainButtonForDef(0, SCV);
            BuildButtonForDef(1, ComsatStation);
            BuildButtonForDef(2, NuclearSilo);
            DisabledAbilityButtonForDef(8, "Lift Off", CommandCenter);
            break;

        case PageTerranBarracks:
            TrainButtonForDef(0, Marine);
            TrainButtonForDef(1, Firebat);
            TrainButtonForDef(2, Medic);
            TrainButtonForDef(3, Ghost);
            DisabledAbilityButtonForDef(8, "Lift Off", Barracks);
            break;

        case PageTerranFactory:
            TrainButtonForDef(0, Vulture);
            TrainButtonForDef(1, SiegeTank);
            TrainButtonForDef(2, Goliath);
            BuildButtonForDef(3, MachineShop);
            DisabledAbilityButtonForDef(8, "Lift Off", Factory);
            break;

        case PageTerranStarport:
            TrainButtonForDef(0, Wraith);
            TrainButtonForDef(1, Dropship);
            TrainButtonForDef(2, ScienceVessel);
            TrainButtonForDef(3, Battlecruiser);
            TrainButtonForDef(4, Valkyrie);
            BuildButtonForDef(5, ControlTower);
            DisabledAbilityButtonForDef(8, "Lift Off", Starport);
            break;

        case PageTerranEngineeringBay:
            ResearchButton(0, TerranResearchId::InfantryWeapons, "Inf Wpn", IconWeapons);
            ResearchButton(1, TerranResearchId::InfantryArmor, "Inf Arm", IconArmor);
            break;

        case PageTerranArmory:
            ResearchButton(0, TerranResearchId::VehicleWeapons, "Veh Wpn", IconWeapons);
            ResearchButton(1, TerranResearchId::VehicleArmor, "Veh Arm", IconArmor);
            ResearchButton(2, TerranResearchId::ShipWeapons, "Ship Wpn", IconWeapons);
            ResearchButton(3, TerranResearchId::ShipArmor, "Ship Arm", IconArmor);
            break;

        case PageTerranAcademy:
            ResearchButton(0, TerranResearchId::StimPacks, "Stim Pack", IconWeapons);
            ResearchButton(1, TerranResearchId::U238Shells, "U-238", IconWeapons);
            break;

        case PageTerranScienceFacility:
            BuildButtonForDef(0, CovertOps);
            BuildButtonForDef(1, PhysicsLab);
            DisabledAbilityButtonForDef(8, "Lift Off", ScienceFacility);
            break;

        case PageTerranMachineShop:
            ResearchButton(0, TerranResearchId::SiegeMode, "Siege Mode", IconWeapons);
            ResearchButton(1, TerranResearchId::SpiderMines, "Mines", IconWeapons);
            break;

        case PageTerranControlTower:
            ResearchButton(0, TerranResearchId::CloakingField, "Cloak", IconWeapons);
            ResearchButton(1, TerranResearchId::IonThrusters, "Boosters", IconWeapons);
            break;

        case PageTerranCovertOps:
            ResearchButton(0, TerranResearchId::Lockdown, "Lockdown", IconWeapons);
            ResearchButton(1, TerranResearchId::MoebiusReactor, "Moebius", IconWeapons);
            break;

        case PageTerranPhysicsLab:
            ResearchButton(0, TerranResearchId::YamatoGun, "Yamato", IconWeapons);
            ResearchButton(1, TerranResearchId::ColossusReactor, "Colossus", IconWeapons);
            break;

        default:
            break;
    }
}

void Hud::BuildZergPage(int SelectedEntityId,
                        int SelectedChkUnitId,
                        const Core::ECS::EntityManager* EcsEntities)
{
    auto TrainEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::ProductionSystem::CanTrain(*EcsEntities, SelectedEntityId, DefId);
    };

    auto BuildEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::BuildingSystem::CanBuild(
                   *EcsEntities, SelectedEntityId, DefId,
                   Game::Systems::BuildTarget{}, nullptr, false);
    };

    auto TrainButtonForDef = [&](int Slot, int DefId)
    {
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Train,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), TrainEnabled(DefId));
    };

    auto BuildButtonForDef = [&](int Slot, int DefId)
    {
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Build,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), BuildEnabled(DefId));
    };

    const int Hatchery = Game::Races::GetUnitDefID("Hatchery");
    const int SpawningPool = Game::Races::GetUnitDefID("Spawning Pool");
    const int Extractor = Game::Races::GetUnitDefID("Extractor");
    const int Drone = Game::Races::GetUnitDefID("Drone");
    const int Zergling = Game::Races::GetUnitDefID("Zergling");
    const int Hydralisk = Game::Races::GetUnitDefID("Hydralisk");
    const int Overlord = Game::Races::GetUnitDefID("Overlord");
    const int Mutalisk = Game::Races::GetUnitDefID("Mutalisk");
    const int Scourge = Game::Races::GetUnitDefID("Scourge");
    const int Queen = Game::Races::GetUnitDefID("Queen");
    const int Ultralisk = Game::Races::GetUnitDefID("Ultralisk");
    const int Defiler = Game::Races::GetUnitDefID("Defiler");

    int EffectivePage = mPanelPage;
    if (EffectivePage == PageNone)
        EffectivePage = GetDefaultPageForSelection(SelectedChkUnitId);

    switch (EffectivePage)
    {
        case PageZergDroneMain:
            SetButton(0, "Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconBuild, true, PageZergDroneBuild);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageZergDroneMain);
            break;

        case PageZergDroneBuild:
            BuildButtonForDef(0, Hatchery);
            BuildButtonForDef(1, SpawningPool);
            BuildButtonForDef(2, Extractor);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageZergDroneMain);
            break;

        case PageZergHatchery:
            TrainButtonForDef(0, Drone);
            TrainButtonForDef(1, Zergling);
            TrainButtonForDef(2, Hydralisk);
            TrainButtonForDef(3, Overlord);
            TrainButtonForDef(4, Mutalisk);
            TrainButtonForDef(5, Scourge);
            TrainButtonForDef(6, Queen);
            TrainButtonForDef(7, Ultralisk);
            TrainButtonForDef(8, Defiler);
            break;

        default:
            break;
    }
}

void Hud::BuildProtossPage(int SelectedEntityId,
                           int SelectedChkUnitId,
                           const Core::ECS::EntityManager* EcsEntities)
{
    auto TrainEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::ProductionSystem::CanTrain(*EcsEntities, SelectedEntityId, DefId);
    };

    auto BuildEnabled = [&](int DefId) -> bool
    {
        return EcsEntities && SelectedEntityId > 0 &&
               Game::Systems::BuildingSystem::CanBuild(
                   *EcsEntities, SelectedEntityId, DefId,
                   Game::Systems::BuildTarget{}, nullptr, false);
    };

    auto TrainButtonForDef = [&](int Slot, int DefId)
    {
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Train,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), TrainEnabled(DefId));
    };

    auto BuildButtonForDef = [&](int Slot, int DefId)
    {
        SetButton(Slot, DisplayLabelForDef(DefId), ActionKind::Build,
                  DefId, HudCommandId::None, TerranResearchId::None,
                  IconForDef(DefId), BuildEnabled(DefId));
    };

    const int Probe = Game::Races::GetUnitDefID("Probe");
    const int Nexus = Game::Races::GetUnitDefID("Nexus");
    const int Pylon = Game::Races::GetUnitDefID("Pylon");
    const int Gateway = Game::Races::GetUnitDefID("Gateway");
    const int Forge = Game::Races::GetUnitDefID("Forge");
    const int Zealot = Game::Races::GetUnitDefID("Zealot");
    const int Dragoon = Game::Races::GetUnitDefID("Dragoon");
    const int HighTemplar = Game::Races::GetUnitDefID("High Templar");
    const int DarkTemplar = Game::Races::GetUnitDefID("Dark Templar");
    const int Shuttle = Game::Races::GetUnitDefID("Shuttle");
    const int Reaver = Game::Races::GetUnitDefID("Reaver");
    const int Observer = Game::Races::GetUnitDefID("Observer");
    const int Scout = Game::Races::GetUnitDefID("Scout");
    const int Corsair = Game::Races::GetUnitDefID("Corsair");
    const int Carrier = Game::Races::GetUnitDefID("Carrier");
    const int Arbiter = Game::Races::GetUnitDefID("Arbiter");

    int EffectivePage = mPanelPage;
    if (EffectivePage == PageNone)
        EffectivePage = GetDefaultPageForSelection(SelectedChkUnitId);

    switch (EffectivePage)
    {
        case PageProtossProbeMain:
            SetButton(0, "Build", ActionKind::OpenSubmenu, -1, HudCommandId::None,
                      TerranResearchId::None, IconBuild, true, PageProtossProbeBuild);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageProtossProbeMain);
            break;

        case PageProtossProbeBuild:
            BuildButtonForDef(0, Nexus);
            BuildButtonForDef(1, Pylon);
            BuildButtonForDef(2, Gateway);
            BuildButtonForDef(3, Forge);
            SetButton(8, "Back", ActionKind::Back, -1, HudCommandId::None,
                      TerranResearchId::None, IconBack, true, PageProtossProbeMain);
            break;

        case PageProtossNexus:
            TrainButtonForDef(0, Probe);
            break;

        case PageProtossGateway:
            TrainButtonForDef(0, Zealot);
            TrainButtonForDef(1, Dragoon);
            TrainButtonForDef(2, HighTemplar);
            TrainButtonForDef(3, DarkTemplar);
            break;

        case PageProtossRoboticsFacility:
            TrainButtonForDef(0, Shuttle);
            TrainButtonForDef(1, Reaver);
            TrainButtonForDef(2, Observer);
            break;

        case PageProtossStargate:
            TrainButtonForDef(0, Scout);
            TrainButtonForDef(1, Corsair);
            TrainButtonForDef(2, Carrier);
            TrainButtonForDef(3, Arbiter);
            break;

        default:
            break;
    }
}

void Hud::ResetInfoPanel()
{
    mInfoPanel = {};
}

void Hud::UpdateInfoPanel(const EntityManager& Entities,
                          int SelectedEntityId,
                          int SelectedChkUnitId,
                          int SelectedTerranDefId,
                          const Core::ECS::EntityManager* EcsEntities)
{
    ResetInfoPanel();
    if (SelectedEntityId < 0)
        return;

    mInfoPanel.HasSelection = true;
    mInfoPanel.SelectedCount = mSelectedCount;

    const auto& SelectedEntity = Entities.GetEntity(SelectedEntityId);
    mInfoPanel.IsBuilding = SelectedEntity.Kind == EntityKind::Building;
    mInfoPanel.IsResource = SelectedEntity.Kind == EntityKind::Resource;
    mInfoPanel.CurrentValue = SelectedEntity.HitPoints;
    mInfoPanel.MaxValue = SelectedEntity.MaxHitPoints;
    mInfoPanel.ResourceAmount = SelectedEntity.ResourceAmount;
    mInfoPanel.PreviewChkUnitId = SelectedChkUnitId;

    int ResolvedDefId = SelectedTerranDefId;
    if (ResolvedDefId >= 0)
    {
        mInfoPanel.Name = DisplayLabelForDef(ResolvedDefId);
        mInfoPanel.IconIndex = IconForDef(ResolvedDefId);
        mInfoPanel.PreviewDefID = ResolvedDefId;
        if (ResolvedDefId == Game::Races::GetUnitDefID("SCV"))
            mInfoPanel.RoleLine = "Private";
    }
    else
    {
        mInfoPanel.Name = DisplayLabelForChkUnitId(SelectedChkUnitId);
    }

    if (!EcsEntities || SelectedEntityId <= 0)
    {
        if (mInfoPanel.IsResource)
            mInfoPanel.TypeLine = "Neutral resource";
        else if (mInfoPanel.IsBuilding)
            mInfoPanel.TypeLine = "Structure";
        else
            mInfoPanel.TypeLine = "Unit";
        return;
    }

    auto& MutableEM = const_cast<Core::ECS::EntityManager&>(*EcsEntities);
    auto* Unit = MutableEM.GetComponent<Game::Components::UnitComponent>(SelectedEntityId);
    auto* Player = MutableEM.GetComponent<Game::Components::PlayerComponent>(SelectedEntityId);
    auto* Building = MutableEM.GetComponent<Game::Components::BuildingComponent>(SelectedEntityId);
    auto* Combat = MutableEM.GetComponent<Game::Components::CombatComponent>(SelectedEntityId);
    auto* Command = MutableEM.GetComponent<Game::Components::CommandComponent>(SelectedEntityId);
    auto* Resource = MutableEM.GetComponent<Game::Components::ResourceComponent>(SelectedEntityId);
    auto* Production = MutableEM.GetComponent<Game::Components::ProductionComponent>(SelectedEntityId);

    if (Unit)
    {
        mInfoPanel.CurrentValue = Unit->HitPoints;
        mInfoPanel.MaxValue = Unit->MaxHitPoints;
        mInfoPanel.ShieldValue = Unit->ShieldPoints;
        mInfoPanel.ShieldMax = Unit->MaxShieldPoints;
        mInfoPanel.EnergyValue = Unit->EnergyPoints;
        mInfoPanel.EnergyMax = Unit->MaxEnergyPoints;
        mInfoPanel.Armor = Unit->Armor;
        mInfoPanel.SightValue = Unit->SightRange;
        mInfoPanel.SpeedValue = (int)std::lround(Game::Tuning::ToWorldMoveSpeed(Unit->Speed));
        mInfoPanel.TypeLine = RaceLabel(Unit->UnitRace) +
                              std::string(mInfoPanel.IsBuilding ? " Structure" : " Unit");
        mInfoPanel.StatLine =
            "Armor " + std::to_string(Unit->Armor) +
            "  Sight " + std::to_string(Unit->SightRange) +
            "  Speed " + std::to_string(mInfoPanel.SpeedValue);
    }
    if (Combat)
    {
        mInfoPanel.GroundDamage = Combat->GroundDamage;
        mInfoPanel.AirDamage = Combat->AirDamage;
        mInfoPanel.RangeValue = (int)std::lround(Combat->AttackRange);
        std::string DamageLine = "Ground " + std::to_string(Combat->GroundDamage);
        if (Combat->AirDamage > 0)
            DamageLine += "  Air " + std::to_string(Combat->AirDamage);
        DamageLine += "  Range " + std::to_string(mInfoPanel.RangeValue);
        mInfoPanel.CombatLine = std::move(DamageLine);
    }
    if (Player)
    {
        mInfoPanel.PlayerID = Player->PlayerID;
        mInfoPanel.TypeLine += "  P" + std::to_string(Player->PlayerID);
        if (ResolvedDefId == Game::Races::GetUnitDefID("SCV"))
            mInfoPanel.RoleLine = RoleLabelForScv(Player->PlayerID);
    }
    if (Command)
    {
        mInfoPanel.StatusLine = CommandLabel(Command->CurrentCommand);
    }
    if (Resource)
    {
        mInfoPanel.IsResource = true;
        mInfoPanel.ResourceAmount = Resource->Amount;
        mInfoPanel.TypeLine = Resource->Type == Game::Components::ResourceType::VespeneGas
            ? "Neutral gas resource"
            : "Neutral mineral resource";
        mInfoPanel.StatusLine = Resource->IsDepleted ? "Depleted" : "Available";
        mInfoPanel.DetailLine = "Remaining " + std::to_string(Resource->Amount);
    }
    if (Building)
    {
        mInfoPanel.IsBuilding = true;
        mInfoPanel.UnderConstruction = Building->UnderConstruction;
        if (Building->UnderConstruction)
        {
            float Progress = 0.0f;
            if (Building->TotalBuildTime > 0.0f)
                Progress = std::clamp(Building->BuildProgress / Building->TotalBuildTime, 0.0f, 1.0f);
            mInfoPanel.Progress = Progress;
            mInfoPanel.StatusLine = "Constructing";
            mInfoPanel.DetailLine = "Construct " +
                std::to_string((int)std::lround(Progress * 100.0f)) + "%";
        }
        else if (Building->IsLifted)
        {
            mInfoPanel.StatusLine = "Lifted";
        }
        else if (mInfoPanel.StatusLine.empty())
        {
            mInfoPanel.StatusLine = "Operational";
        }

        if (Building->AddonID > 0 && MutableEM.IsAlive(Building->AddonID))
        {
            auto* AddonBuilding = MutableEM.GetComponent<Game::Components::BuildingComponent>(Building->AddonID);
            int AddonDefId = AddonBuilding ? AddonBuilding->BuildingDefID : -1;
            if (AddonDefId >= 0)
            {
                mInfoPanel.CargoLine = "Addon " + DisplayLabelForDef(AddonDefId);
            }
        }
    }
    if (auto* Harvest = MutableEM.GetComponent<Game::Components::HarvestComponent>(SelectedEntityId))
    {
        if (Harvest->CarryingAmount > 0)
        {
            const char* CarryLabel =
                Harvest->CarryingType == Game::Components::ResourceType::VespeneGas ? "Gas" :
                Harvest->CarryingType == Game::Components::ResourceType::Minerals ? "Minerals" :
                                                                                     "Cargo";
            mInfoPanel.CarryingAmount = Harvest->CarryingAmount;
            mInfoPanel.CargoLine =
                std::string(CarryLabel) + " " + std::to_string(Harvest->CarryingAmount) +
                "/" + std::to_string(Harvest->CarryCapacity);
        }
        if (mInfoPanel.DetailLine.empty())
            mInfoPanel.DetailLine = "Harvest " + HarvestStateLabel(Harvest->State);
    }
    if (Production && !Production->Queue.empty())
    {
        mInfoPanel.QueueEntries.clear();
        for (size_t Index = 0; Index < Production->Queue.size() && Index < 5; ++Index)
        {
            mInfoPanel.QueueEntries.push_back(
                DisplayLabelForDef(Production->Queue[Index]));
        }
        mInfoPanel.QueueLabel = "Queue";
        if (Production->CurrentTrainingUnitDefID >= 0)
        {
            float Progress = 0.0f;
            if (Production->TrainingTimePerUnit > 0.0f)
            {
                Progress = std::clamp(
                    Production->TrainingProgress / Production->TrainingTimePerUnit,
                    0.0f, 1.0f);
            }
            mInfoPanel.Progress = Progress;
            mInfoPanel.StatusLine = "Training " +
                DisplayLabelForDef(Production->CurrentTrainingUnitDefID);
            mInfoPanel.DetailLine =
                "Queue " + std::to_string((int)Production->Queue.size()) +
                "  " + std::to_string((int)std::lround(Progress * 100.0f)) + "%";
        }
    }

    if (mInfoPanel.StatusLine.empty())
        mInfoPanel.StatusLine = "Ready";
}

void Hud::UpdateContext(const EntityManager& Entities,
                        const std::vector<int>& SelectedIds,
                        const Core::ECS::EntityManager* EcsEntities)
{
    ResetButtons();
    ResetInfoPanel();
    mSelectedCount = (int)SelectedIds.size();

    if (SelectedIds != mLastSelectedIds)
    {
        CancelPendingTarget();
        mLastSelectedIds = SelectedIds;
    }

    int SelectedEntityId = FindFirstSelectedEntityId(SelectedIds);
    int SelectedChkUnitId = FindSelectedChkUnitId(Entities, SelectedEntityId);

    if (SelectedEntityId < 0)
    {
        ActiveCommand = -1;
        mSelectedEntityId = -1;
        mSelectedChkUnitId = -1;
        mSelectedTerranDefId = -1;
        mPanelPage = PageNone;
        mPanelCategory = PanelCategory::None;
        return;
    }

    const int SelectedTerranDefId =
        ResolveSelectedTerranDefId(SelectedEntityId, SelectedChkUnitId, EcsEntities);

    if (SelectedEntityId != mSelectedEntityId ||
        SelectedChkUnitId != mSelectedChkUnitId ||
        SelectedTerranDefId != mSelectedTerranDefId)
    {
        mSelectedEntityId = SelectedEntityId;
        mSelectedChkUnitId = SelectedChkUnitId;
        mSelectedTerranDefId = SelectedTerranDefId;
        mPanelPage = (SelectedTerranDefId >= 0)
            ? GetDefaultTerranPageForDef(SelectedTerranDefId)
            : GetDefaultPageForSelection(SelectedChkUnitId);
    }

    if (SelectedTerranDefId >= 0)
    {
        mPanelCategory = PanelCategory::Terran;
        BuildTerranPage(SelectedEntityId, SelectedTerranDefId, EcsEntities);
        UpdateInfoPanel(Entities, SelectedEntityId, SelectedChkUnitId, SelectedTerranDefId, EcsEntities);
        return;
    }

    switch (SelectedChkUnitId)
    {
        case ZergDrone:
        case ZergHatchery:
        case ZergLair:
        case ZergHive:
            mPanelCategory = PanelCategory::Zerg;
            BuildZergPage(SelectedEntityId, SelectedChkUnitId, EcsEntities);
            break;

        default:
            mPanelCategory = PanelCategory::Protoss;
            BuildProtossPage(SelectedEntityId, SelectedChkUnitId, EcsEntities);
            break;
    }

    UpdateInfoPanel(Entities, SelectedEntityId, SelectedChkUnitId, SelectedTerranDefId, EcsEntities);
}

void Hud::CancelPendingTarget()
{
    mPendingTargetMode = PendingTargetMode::None;
    mPendingBuildDefID = -1;
    mPendingCommandId = HudCommandId::None;
}

void Hud::OnButtonInvoked(int ButtonIndex)
{
    if (ButtonIndex < 0 || ButtonIndex >= (int)mButtonDefs.size())
        return;
    auto& Button = mButtonDefs[(size_t)ButtonIndex];
    if (Button.Label.empty())
        return;

    ActiveCommand = ButtonIndex;
    if (!Button.Enabled)
        return;

    CancelPendingTarget();

    if (Button.Kind == ActionKind::OpenSubmenu || Button.Kind == ActionKind::Back)
    {
        mPanelPage = Button.NextMenuPage;
        return;
    }

    if (Button.Kind == ActionKind::Build)
    {
        auto Def = Game::Races::GetUnitDefByID(Button.TargetUnitDefID);
        if (!Def.IsAddon)
        {
            mPendingBuildDefID = Button.TargetUnitDefID;
            mPendingTargetMode =
                (Button.TargetUnitDefID == Game::Races::GetUnitDefID("Refinery"))
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
                mPendingTargetMode = PendingTargetMode::Move;
                mPendingCommandId = Button.CommandId;
                break;
            case HudCommandId::AttackMove:
                mPendingTargetMode = PendingTargetMode::AttackMove;
                mPendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Patrol:
                mPendingTargetMode = PendingTargetMode::Patrol;
                mPendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Follow:
                mPendingTargetMode = PendingTargetMode::Follow;
                mPendingCommandId = Button.CommandId;
                break;
            case HudCommandId::Harvest:
                mPendingTargetMode = PendingTargetMode::Harvest;
                mPendingCommandId = Button.CommandId;
                break;
            default: break;
        }
    }
}

void Hud::Draw(const EntityManager& Entities,
               const std::vector<int>& SelectedIds,
               int MapWidthTiles, int MapHeightTiles,
               int VW, int VH)
{
    auto& D = Render::g_Draw2D;
    auto ConsoleLeftImg = mCache ? mCache->GetUIConsoleLeft() : Resource::SokolBridge::SokolImage{};
    auto ConsoleCenterImg = mCache ? mCache->GetUIConsoleCenter() : Resource::SokolBridge::SokolImage{};
    auto ConsoleRightImg = mCache ? mCache->GetUIConsoleRight() : Resource::SokolBridge::SokolImage{};
    auto ConsolePortraitFrameImg = mCache ? mCache->GetUIConsolePortraitFrame() : Resource::SokolBridge::SokolImage{};
    auto ConsoleApmImg = mCache ? mCache->GetUIConsoleApm() : Resource::SokolBridge::SokolImage{};
    auto ConsoleTimeImg = mCache ? mCache->GetUIConsoleTime() : Resource::SokolBridge::SokolImage{};
    auto ConsoleImg = mCache ? mCache->GetUIConsole() : Resource::SokolBridge::SokolImage{};
    auto ConsoleOverImg = mCache ? mCache->GetUIConsoleOver() : Resource::SokolBridge::SokolImage{};
    const auto HudTwire = mCache ? mCache->GetHudTwireStrip() : Resource::SokolBridge::SokolImage{};
    const auto HudTunit = mCache ? mCache->GetHudTunitStrip() : Resource::SokolBridge::SokolImage{};
    const auto Layout = ResolveHudLayout(VW, VH);

    auto DrawContainedImage = [&](const Resource::SokolBridge::SokolImage& Image,
                                  const RectI& Rect,
                                  Render::Color Tint)
    {
        if (!Image.Valid() || Rect.W <= 0 || Rect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
            return;

        const float Scale = std::min((float)Rect.W / (float)Image.Width,
                                     (float)Rect.H / (float)Image.Height);
        const float DrawW = std::max(1.0f, std::floor((float)Image.Width * Scale));
        const float DrawH = std::max(1.0f, std::floor((float)Image.Height * Scale));
        const float DrawX = (float)Rect.X + ((float)Rect.W - DrawW) * 0.5f;
        const float DrawY = (float)Rect.Y + ((float)Rect.H - DrawH) * 0.5f;
        D.DrawSpriteFromImage(Image, DrawX, DrawY, DrawW, DrawH, Tint);
    };

    auto DrawContainedAtlas = [&](const Resource::SokolBridge::SokolImage& Image,
                                  const AtlasRect& SourceRect,
                                  const RectI& Rect,
                                  Render::Color Tint)
    {
        if (!Image.Valid() || Rect.W <= 0 || Rect.H <= 0 || SourceRect.W <= 0 || SourceRect.H <= 0)
            return;

        const float Scale = std::min((float)Rect.W / (float)SourceRect.W,
                                     (float)Rect.H / (float)SourceRect.H);
        const int DrawW = std::max(1, (int)std::floor((float)SourceRect.W * Scale));
        const int DrawH = std::max(1, (int)std::floor((float)SourceRect.H * Scale));
        const RectI Dest =
        {
            Rect.X + (Rect.W - DrawW) / 2,
            Rect.Y + (Rect.H - DrawH) / 2,
            DrawW,
            DrawH
        };
        const float U0 = (float)SourceRect.X / (float)Image.Width;
        const float V0 = (float)SourceRect.Y / (float)Image.Height;
        const float U1 = (float)(SourceRect.X + SourceRect.W) / (float)Image.Width;
        const float V1 = (float)(SourceRect.Y + SourceRect.H) / (float)Image.Height;
        D.DrawSprite(Image.Image, Image.View,
                     (float)Dest.X, (float)Dest.Y,
                     (float)Dest.W, (float)Dest.H,
                     U0, V0, U1, V1, Tint, false);
    };

    auto ExpandRectFromOpening = [&](const RectI& OpeningRect,
                                     int SourceWidth,
                                     int SourceHeight,
                                     const RectI& OpeningInSource) -> RectI
    {
        if (OpeningRect.W <= 0 || OpeningRect.H <= 0 || SourceWidth <= 0 || SourceHeight <= 0 ||
            OpeningInSource.W <= 0 || OpeningInSource.H <= 0)
        {
            return OpeningRect;
        }

        const float ScaleX = (float)OpeningRect.W / (float)OpeningInSource.W;
        const float ScaleY = (float)OpeningRect.H / (float)OpeningInSource.H;
        const int OuterW = std::max(1, (int)std::lround((float)SourceWidth * ScaleX));
        const int OuterH = std::max(1, (int)std::lround((float)SourceHeight * ScaleY));
        return
        {
            OpeningRect.X - (int)std::lround((float)OpeningInSource.X * ScaleX),
            OpeningRect.Y - (int)std::lround((float)OpeningInSource.Y * ScaleY),
            OuterW,
            OuterH
        };
    };

    auto DrawAtlasImage = [&](const Resource::SokolBridge::SokolImage& Image,
                              const AtlasRect& SourceRect,
                              const RectI& DestRect,
                              Render::Color Tint)
    {
        if (!Image.Valid() || Image.Width <= 0 || Image.Height <= 0 ||
            SourceRect.W <= 0 || SourceRect.H <= 0 ||
            DestRect.W <= 0 || DestRect.H <= 0)
        {
            return;
        }

        const float U0 = (float)SourceRect.X / (float)Image.Width;
        const float V0 = (float)SourceRect.Y / (float)Image.Height;
        const float U1 = (float)(SourceRect.X + SourceRect.W) / (float)Image.Width;
        const float V1 = (float)(SourceRect.Y + SourceRect.H) / (float)Image.Height;
        D.DrawSprite(Image.Image, Image.View,
                     (float)DestRect.X, (float)DestRect.Y,
                     (float)DestRect.W, (float)DestRect.H,
                     U0, V0, U1, V1, Tint, false);
    };

    auto ComputeCommandIconRect = [&](const RectI& ButtonRect) -> RectI
    {
        const int InsetX = std::max(4, ButtonRect.W / 14);
        const int InsetY = std::max(4, ButtonRect.H / 14);
        const int BottomReserve = 0;
        return
        {
            ButtonRect.X + InsetX,
            ButtonRect.Y + InsetY,
            std::max(8, ButtonRect.W - InsetX * 2),
            std::max(8, ButtonRect.H - InsetY * 2 - BottomReserve)
        };
    };

    auto DrawNineSlice = [&](const Resource::SokolBridge::SokolImage& Image,
                             const RectI& DestRect,
                             int BorderLeft,
                             int BorderTop,
                             int BorderRight,
                             int BorderBottom,
                             Render::Color Tint)
    {
        if (!Image.Valid() || DestRect.W <= 0 || DestRect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
            return;

        const int SrcLeft = std::clamp(BorderLeft, 0, std::max(0, Image.Width - 2));
        const int SrcTop = std::clamp(BorderTop, 0, std::max(0, Image.Height - 2));
        const int SrcRight = std::clamp(BorderRight, 0, std::max(0, Image.Width - SrcLeft - 1));
        const int SrcBottom = std::clamp(BorderBottom, 0, std::max(0, Image.Height - SrcTop - 1));
        const int CenterSrcW = std::max(1, Image.Width - SrcLeft - SrcRight);
        const int CenterSrcH = std::max(1, Image.Height - SrcTop - SrcBottom);

        int DstLeft = std::min(DestRect.W / 2, std::max(1, (int)std::lround((double)SrcLeft * (double)DestRect.H / (double)Image.Height)));
        int DstRight = std::min(DestRect.W - DstLeft, std::max(1, (int)std::lround((double)SrcRight * (double)DestRect.H / (double)Image.Height)));
        int DstTop = std::min(DestRect.H / 2, std::max(1, (int)std::lround((double)SrcTop * (double)DestRect.H / (double)Image.Height)));
        int DstBottom = std::min(DestRect.H - DstTop, std::max(1, (int)std::lround((double)SrcBottom * (double)DestRect.H / (double)Image.Height)));
        if (DstLeft + DstRight >= DestRect.W)
        {
            DstLeft = std::max(1, DestRect.W / 2);
            DstRight = std::max(1, DestRect.W - DstLeft);
        }
        if (DstTop + DstBottom >= DestRect.H)
        {
            DstTop = std::max(1, DestRect.H / 2);
            DstBottom = std::max(1, DestRect.H - DstTop);
        }

        const int DstCenterW = std::max(1, DestRect.W - DstLeft - DstRight);
        const int DstCenterH = std::max(1, DestRect.H - DstTop - DstBottom);

        const AtlasRect TopLeft = { 0, 0, SrcLeft, SrcTop };
        const AtlasRect TopCenter = { SrcLeft, 0, CenterSrcW, SrcTop };
        const AtlasRect TopRight = { SrcLeft + CenterSrcW, 0, SrcRight, SrcTop };
        const AtlasRect MidLeft = { 0, SrcTop, SrcLeft, CenterSrcH };
        const AtlasRect MidCenter = { SrcLeft, SrcTop, CenterSrcW, CenterSrcH };
        const AtlasRect MidRight = { SrcLeft + CenterSrcW, SrcTop, SrcRight, CenterSrcH };
        const AtlasRect BottomLeft = { 0, SrcTop + CenterSrcH, SrcLeft, SrcBottom };
        const AtlasRect BottomCenter = { SrcLeft, SrcTop + CenterSrcH, CenterSrcW, SrcBottom };
        const AtlasRect BottomRight = { SrcLeft + CenterSrcW, SrcTop + CenterSrcH, SrcRight, SrcBottom };

        const int X0 = DestRect.X;
        const int X1 = DestRect.X + DstLeft;
        const int X2 = DestRect.X + DestRect.W - DstRight;
        const int Y0 = DestRect.Y;
        const int Y1 = DestRect.Y + DstTop;
        const int Y2 = DestRect.Y + DestRect.H - DstBottom;

        DrawAtlasImage(Image, TopLeft, { X0, Y0, DstLeft, DstTop }, Tint);
        DrawAtlasImage(Image, TopCenter, { X1, Y0, DstCenterW, DstTop }, Tint);
        DrawAtlasImage(Image, TopRight, { X2, Y0, DstRight, DstTop }, Tint);
        DrawAtlasImage(Image, MidLeft, { X0, Y1, DstLeft, DstCenterH }, Tint);
        DrawAtlasImage(Image, MidCenter, { X1, Y1, DstCenterW, DstCenterH }, Tint);
        DrawAtlasImage(Image, MidRight, { X2, Y1, DstRight, DstCenterH }, Tint);
        DrawAtlasImage(Image, BottomLeft, { X0, Y2, DstLeft, DstBottom }, Tint);
        DrawAtlasImage(Image, BottomCenter, { X1, Y2, DstCenterW, DstBottom }, Tint);
        DrawAtlasImage(Image, BottomRight, { X2, Y2, DstRight, DstBottom }, Tint);
    };

    auto DrawConsoleSlices = [&](const Resource::SokolBridge::SokolImage& Image,
                                 const RectI& DestRect,
                                 Render::Color Tint)
    {
        if (!Image.Valid() || DestRect.W <= 0 || DestRect.H <= 0 || Image.Width <= 0 || Image.Height <= 0)
            return;

        const float SourceWidth = (float)Image.Width;
        const float SourceHeight = (float)Image.Height;
        if (SourceWidth <= 1.0f || SourceHeight <= 1.0f)
            return;

        const float Scale = (float)DestRect.H / SourceHeight;
        const float LeftSrcW = SourceWidth * ((float)ClassicConsoleLeftSrcWidth / (float)ClassicConsoleSourceWidth);
        const float RightSrcW = SourceWidth * ((float)ClassicConsoleRightSrcWidth / (float)ClassicConsoleSourceWidth);
        const float CenterSrcW = std::max(0.0f, SourceWidth - LeftSrcW - RightSrcW);
        if (CenterSrcW <= 1.0f)
        {
            D.DrawSpriteFromImage(Image,
                                  (float)DestRect.X, (float)DestRect.Y,
                                  (float)DestRect.W, (float)DestRect.H,
                                  Tint);
            return;
        }

        float LeftDstW = LeftSrcW * Scale;
        float RightDstW = RightSrcW * Scale;
        if (LeftDstW + RightDstW > (float)DestRect.W)
        {
            const float Shrink = (float)DestRect.W / (LeftDstW + RightDstW);
            LeftDstW *= Shrink;
            RightDstW *= Shrink;
        }
        const float CenterDstW = std::max(0.0f, (float)DestRect.W - LeftDstW - RightDstW);

        D.DrawSprite(Image.Image, Image.View,
                     (float)DestRect.X, (float)DestRect.Y,
                     LeftDstW, (float)DestRect.H,
                     0.0f, 0.0f, LeftSrcW / SourceWidth, 1.0f,
                     Tint, false);

        if (CenterDstW > 0.0f)
        {
            D.DrawSprite(Image.Image, Image.View,
                         (float)DestRect.X + LeftDstW, (float)DestRect.Y,
                         CenterDstW, (float)DestRect.H,
                         LeftSrcW / SourceWidth, 0.0f, (LeftSrcW + CenterSrcW) / SourceWidth, 1.0f,
                         Tint, false);
        }

        D.DrawSprite(Image.Image, Image.View,
                     (float)DestRect.X + LeftDstW + CenterDstW, (float)DestRect.Y,
                     RightDstW, (float)DestRect.H,
                     (LeftSrcW + CenterSrcW) / SourceWidth, 0.0f, 1.0f, 1.0f,
                     Tint, false);
    };

    const bool HasModularConsole =
        ConsoleLeftImg.Valid() &&
        ConsoleCenterImg.Valid() &&
        ConsoleRightImg.Valid() &&
        ConsolePortraitFrameImg.Valid();

    if (HasModularConsole)
    {
        const float BaseScale = (float)Layout.ConsoleRect.H /
            (float)std::max(ConsoleLeftImg.Height, ConsoleRightImg.Height);
        const int LeftW = std::max(1, (int)std::lround((float)ConsoleLeftImg.Width * BaseScale));
        const int LeftH = std::max(1, (int)std::lround((float)ConsoleLeftImg.Height * BaseScale));
        const int RightW = std::max(1, (int)std::lround((float)ConsoleRightImg.Width * BaseScale));
        const int RightH = std::max(1, (int)std::lround((float)ConsoleRightImg.Height * BaseScale));
        const RectI LeftBaseRect =
        {
            0,
            Layout.ConsoleRect.Y + Layout.ConsoleRect.H - LeftH,
            LeftW,
            LeftH
        };
        const RectI RightBaseRect =
        {
            VW - RightW,
            Layout.ConsoleRect.Y + Layout.ConsoleRect.H - RightH,
            RightW,
            RightH
        };

        D.DrawSpriteFromImage(ConsoleLeftImg,
                              (float)LeftBaseRect.X, (float)LeftBaseRect.Y,
                              (float)LeftBaseRect.W, (float)LeftBaseRect.H,
                              Render::Colors::White());
        D.DrawSpriteFromImage(ConsoleRightImg,
                              (float)RightBaseRect.X, (float)RightBaseRect.Y,
                              (float)RightBaseRect.W, (float)RightBaseRect.H,
                              Render::Colors::White());

        const RectI CenterOuterRect = Layout.InfoFrameRect;
        const RectI PortraitOuterRect = Layout.PortraitFrameRect;

        D.DrawSpriteFromImage(ConsoleCenterImg,
                              (float)CenterOuterRect.X, (float)CenterOuterRect.Y,
                              (float)CenterOuterRect.W, (float)CenterOuterRect.H,
                              Render::Colors::White());
        D.DrawSpriteFromImage(ConsolePortraitFrameImg,
                              (float)PortraitOuterRect.X, (float)PortraitOuterRect.Y,
                              (float)PortraitOuterRect.W, (float)PortraitOuterRect.H,
                              Render::Colors::White());

        auto DrawPlaque = [&](const Resource::SokolBridge::SokolImage& Image,
                              const RectI& AnchorRect,
                              bool AlignRight)
        {
            if (!Image.Valid() || Image.Width <= 0 || Image.Height <= 0)
                return;

            const int PlaqueH = std::max(24, std::min(42, AnchorRect.H / 5));
            const int PlaqueW = std::max(PlaqueH * 2, (int)std::lround((float)Image.Width * (float)PlaqueH / (float)Image.Height));
            const int X = AlignRight
                ? (AnchorRect.X + AnchorRect.W - PlaqueW - 8 - TerranLedInsetPx)
                : (AnchorRect.X + 8 + TerranLedInsetPx);
            const int Y = AnchorRect.Y - PlaqueH / 3 + TerranLedOffsetYPx;
            D.DrawSpriteFromImage(Image, (float)X, (float)Y, (float)PlaqueW, (float)PlaqueH, Render::Colors::White());
        };

        DrawPlaque(ConsoleTimeImg, CenterOuterRect, false);
        DrawPlaque(ConsoleApmImg, CenterOuterRect, true);
    }
    else if (ConsoleImg.Valid())
    {
        DrawConsoleSlices(ConsoleImg, Layout.ConsoleRect, Render::Colors::White());
        if (ConsoleOverImg.Valid())
            DrawConsoleSlices(ConsoleOverImg, Layout.ConsoleRect, Render::Colors::White());
    }
    else
    {
        D.DrawRect((float)Layout.ConsoleRect.X, (float)Layout.ConsoleRect.Y,
                   (float)Layout.ConsoleRect.W, (float)Layout.ConsoleRect.H,
                   Render::Color(20, 20, 40, 220));
    }

    const auto* ButtonSkins = mCache
        ? mCache->GetCommandButtonSkins(SkinSetForPanelCategory((int)mPanelCategory))
        : nullptr;
    const auto* NormalButtonSkin = FindPreferredButtonSkin(ButtonSkins, 0);
    mLastHovered = CheckCommandPanelClick((int)Core::Platform::GetMouseX(),
                                          (int)Core::Platform::GetMouseY(),
                                          VW, VH);

    for (int I = 0; I < 9; ++I)
    {
        const RectI& ButtonRect = Layout.CommandRects[(size_t)I];
        const auto& Button = mButtonDefs[(size_t)I];
        bool HasCmd = !Button.Label.empty();
        bool Enabled = Button.Enabled;
        const bool Hovered = I == mLastHovered;
        const bool Active = I == ActiveCommand && HasCmd;
        const Render::Color SkinTint = SkinTintForState(HasCmd, Enabled, Hovered, Active);
        const auto* IconMask = GetButtonMaskImage(Button);
        const auto* Icon = GetButtonImage(Button);
        if (NormalButtonSkin && NormalButtonSkin->Valid())
        {
            D.DrawSpriteFromImage(*NormalButtonSkin,
                                  (float)ButtonRect.X, (float)ButtonRect.Y,
                                  (float)ButtonRect.W, (float)ButtonRect.H,
                                  SkinTint);
        }
        else
        {
            D.DrawRect((float)ButtonRect.X, (float)ButtonRect.Y,
                       (float)ButtonRect.W, (float)ButtonRect.H,
                       HasCmd
                           ? (Enabled ? Render::Color(38, 56, 88, 220) : Render::Color(35, 35, 45, 200))
                           : Render::Color(20, 20, 40, 100));
        }

        if (HasCmd && IconMask && IconMask->Valid())
        {
            const RectI IconRect = ComputeCommandIconRect(ButtonRect);
            DrawContainedImage(*IconMask, IconRect, IconTintForState(Button, Enabled, Hovered, Active));
        }
        else if (HasCmd && Icon && Icon->Valid())
        {
            const RectI IconRect = ComputeCommandIconRect(ButtonRect);
            DrawContainedImage(*Icon, IconRect, FallbackIconTintForState(Enabled, Active));
        }

        const bool HasVisualIcon = (IconMask && IconMask->Valid()) || (Icon && Icon->Valid());
        const bool DrawLabel = HasCmd && !HasVisualIcon;
        if (DrawLabel)
        {
            D.DrawText(Ellipsize(Button.Label, 10),
                       (float)ButtonRect.X + 4.0f,
                       (float)ButtonRect.Y + (float)ButtonRect.H - 14.0f,
                       1.0f,
                       Enabled ? Render::Color(220, 220, 220, 255) : Render::Color(170, 170, 170, 220));
        }
    }

    char Buf[128];
    const auto AggIcons = mCache ? mCache->GetHudAggIcons() : Resource::SokolBridge::SokolImage{};
    auto DrawResourceEntry = [&](int RightX,
                                 const AtlasRect& IconRect,
                                 const char* Value,
                                 const char* FallbackLabel,
                                 Render::Color FallbackTint) -> int
    {
        const int IconSize = 16;
        const int ValueChars = (int)std::strlen(Value);
        const int TextW = std::max(18, ValueChars * 8);
        const int EntryW = IconSize + 5 + TextW;
        const int X = RightX - EntryW;
        const int Y = 2;

        if (AggIcons.Valid())
        {
            DrawAtlasImage(AggIcons, IconRect, { X, Y, IconSize, IconSize }, Render::Colors::White());
        }
        else
        {
            D.DrawRect((float)X + 1.0f, (float)Y + 1.0f, 14.0f, 14.0f, Render::Color(18, 20, 28, 230));
            D.DrawRectLines((float)X + 1.0f, (float)Y + 1.0f, 14.0f, 14.0f, FallbackTint, 1.0f);
            D.DrawText(FallbackLabel, (float)X + 4.0f, (float)Y + 4.0f, 1.0f, FallbackTint);
        }

        D.DrawText(Value, (float)X + (float)IconSize + 5.0f, (float)Y + 3.0f, 1.0f, Render::Color(228, 236, 244, 255));
        return X - 10;
    };

    char MineralBuf[32];
    char GasBuf[32];
    char SupplyBuf[32];
    std::snprintf(MineralBuf, sizeof(MineralBuf), "%d", MineralCount);
    std::snprintf(GasBuf, sizeof(GasBuf), "%d", GasCount);
    std::snprintf(SupplyBuf, sizeof(SupplyBuf), "%d/%d", SupplyUsed, SupplyMax);
    int ResourceRight = VW - 8;
    ResourceRight = DrawResourceEntry(ResourceRight, AggIconSupply, SupplyBuf, "S", Render::Color(220, 210, 120, 255));
    ResourceRight = DrawResourceEntry(ResourceRight, AggIconGas, GasBuf, "G", Render::Color(84, 208, 168, 255));
    DrawResourceEntry(ResourceRight, AggIconMinerals, MineralBuf, "M", Render::Color(96, 180, 255, 255));

    mSelectedCount = (int)SelectedIds.size();
    std::snprintf(Buf, sizeof(Buf), "Sel: %d", mSelectedCount);
    D.DrawText(Buf, 8.0f, 20.0f, 1.5f, Render::Color(180, 200, 220, 255));

    if (mInfoPanel.HasSelection)
    {
        const RectI& InfoRect = Layout.InfoRect;
        const RectI& PreviewRect = Layout.PreviewRect;
        const RectI& QueueRect = Layout.QueueRect;
        const RectI& AuxiliaryRect = Layout.AuxiliaryRect;
        const RectI InfoInnerRect = HasModularConsole
            ? InsetRect(InfoRect, std::max(18, InfoRect.W / 34), std::max(16, InfoRect.H / 10))
            : InfoRect;
        const RectI AuxiliaryInnerRect = HasModularConsole
            ? InsetRect(AuxiliaryRect, std::max(14, AuxiliaryRect.W / 12), std::max(18, AuxiliaryRect.H / 11))
            : AuxiliaryRect;
        const bool ScvPanel =
            mSelectedTerranDefId == Game::Races::GetUnitDefID("SCV") &&
            !mInfoPanel.IsBuilding &&
            !mInfoPanel.IsResource;
        const Entity* SelectedSokolEntity = nullptr;
        if (mSelectedEntityId > 0)
        {
            const auto& Candidate = Entities.GetEntity(mSelectedEntityId);
            if (Candidate.Id == mSelectedEntityId && Candidate.Visible)
                SelectedSokolEntity = &Candidate;
        }
        auto GetFallbackPortraitImage = [&]() -> const Resource::SokolBridge::SokolImage*
        {
            if (SelectedSokolEntity && SelectedSokolEntity->Frames && !SelectedSokolEntity->Frames->empty())
            {
                const auto& Frames = *SelectedSokolEntity->Frames;
                const int CurrentIndex = std::clamp(SelectedSokolEntity->FrameIndex, 0, (int)Frames.size() - 1);
                if (CurrentIndex >= 0 && CurrentIndex < (int)Frames.size() && Frames[(size_t)CurrentIndex].Valid() &&
                    CurrentIndex != 0)
                {
                    return &Frames[(size_t)CurrentIndex];
                }

                std::array<int, 6> ValidFrames = {{ -1, -1, -1, -1, -1, -1 }};
                size_t ValidCount = 0;
                for (size_t Index = 0; Index < Frames.size() && ValidCount < ValidFrames.size(); ++Index)
                {
                    if (Frames[Index].Valid())
                        ValidFrames[ValidCount++] = (int)Index;
                }
                if (ValidCount > 0)
                {
                    const size_t AnimIndex =
                        (size_t)(Core::Platform::GetAbsoluteTime() * 6.0) % ValidCount;
                    return &Frames[(size_t)ValidFrames[AnimIndex]];
                }
            }
            return GetFirstValidSpriteFrame(mInfoPanel.PreviewChkUnitId);
        };
        auto GetPortraitImage = [&]() -> const Resource::SokolBridge::SokolImage*
        {
            if (mCache)
            {
                const std::string PortraitKey = ResolvePortraitKeyForSelection(mSelectedTerranDefId);
                if (!PortraitKey.empty())
                {
                    if (const auto* Clip = mCache->GetPortraitClip(PortraitKey);
                        Clip && !Clip->Frames.empty())
                    {
                        float TotalDuration = 0.0f;
                        for (const auto& Frame : Clip->Frames)
                            TotalDuration += std::max(1.0f / 30.0f, Frame.DurationSeconds);
                        if (TotalDuration <= 0.0f)
                            TotalDuration = (float)Clip->Frames.size() * (1.0f / 12.0f);

                        float Cursor = std::fmod((float)Core::Platform::GetAbsoluteTime(), TotalDuration);
                        for (const auto& Frame : Clip->Frames)
                        {
                            const float Duration = std::max(1.0f / 30.0f, Frame.DurationSeconds);
                            if (Cursor <= Duration)
                                return &Frame.Image;
                            Cursor -= Duration;
                        }
                        return &Clip->Frames.back().Image;
                    }
                }
            }
            return GetFallbackPortraitImage();
        };
        const auto* WireframePreviewImage = GetSelectionPreviewImage();
        const auto* WireframeFrames = mCache ? mCache->GetWireframeFrames() : nullptr;
        const bool PreviewUsesWireframe =
            WireframeFrames &&
            mInfoPanel.PreviewChkUnitId >= 0 &&
            mInfoPanel.PreviewChkUnitId < (int)WireframeFrames->size() &&
            WireframePreviewImage == &(*WireframeFrames)[(size_t)mInfoPanel.PreviewChkUnitId];
        const auto* WireframeBounds =
            (PreviewUsesWireframe && mCache && mInfoPanel.PreviewChkUnitId >= 0)
            ? mCache->GetWireframeOpaqueBounds(mInfoPanel.PreviewChkUnitId)
            : nullptr;
        const auto HpEmptyStrip = mCache ? mCache->GetHudHpBarEmptyStrip() : Resource::SokolBridge::SokolImage{};
        const auto HpStrip = mCache ? mCache->GetHudHpBarStrip() : Resource::SokolBridge::SokolImage{};
        const RectI PortraitPanel = InsetRect(AuxiliaryInnerRect, std::max(6, AuxiliaryInnerRect.W / 14), std::max(8, AuxiliaryInnerRect.H / 14));
        const RectI PreviewPanel = InsetRect(PreviewRect, std::max(6, PreviewRect.W / 10), std::max(6, PreviewRect.H / 10));
        const int ContentGap = std::max(10, InfoInnerRect.W / 52);
        const int HeaderX = PreviewRect.X + PreviewRect.W + ContentGap;
        const int HeaderW = std::max(120, InfoInnerRect.X + InfoInnerRect.W - HeaderX - 10);
        const RectI HeaderRect =
        {
            HeaderX,
            PreviewRect.Y + 2,
            HeaderW,
            std::max(56, PreviewRect.H - 4)
        };
        const int FooterH = 28;
        const int FooterGap = 8;
        const int FooterY = InfoInnerRect.Y + InfoInnerRect.H - FooterH - 8;
        const int StatTop = std::max(PreviewRect.Y + PreviewRect.H + 10, HeaderRect.Y + 60);
        const int StatBottom = std::max(StatTop + 24, FooterY - 8);
        const RectI StatBodyRect =
        {
            InfoInnerRect.X + 12,
            StatTop,
            std::max(120, InfoInnerRect.W - 24),
            std::max(24, StatBottom - StatTop)
        };
        const RectI FooterCardA =
        {
            StatBodyRect.X,
            FooterY,
            std::max(56, (StatBodyRect.W - FooterGap) / 2),
            FooterH
        };
        const RectI FooterCardB =
        {
            FooterCardA.X + FooterCardA.W + FooterGap,
            FooterY,
            std::max(56, StatBodyRect.W - FooterCardA.W - FooterGap),
            FooterH
        };

        if (!HasModularConsole && HudTunit.Valid())
            D.DrawSpriteFromImage(HudTunit, (float)InfoRect.X, (float)InfoRect.Y, (float)InfoRect.W, (float)InfoRect.H, Render::Color(255, 255, 255, 150));
        D.DrawRect((float)InfoInnerRect.X, (float)InfoInnerRect.Y, (float)InfoInnerRect.W, (float)InfoInnerRect.H,
                   HasModularConsole ? Render::Color(4, 5, 8, 180) : Render::Color(5, 5, 10, 90));
        D.DrawRectLines((float)InfoInnerRect.X, (float)InfoInnerRect.Y, (float)InfoInnerRect.W, (float)InfoInnerRect.H,
                        HasModularConsole ? Render::Color(88, 96, 112, 110) : Render::Color(120, 90, 60, 130), 1.0f);
        if (!HasModularConsole && HudTwire.Valid())
            D.DrawSpriteFromImage(HudTwire, (float)AuxiliaryRect.X, (float)AuxiliaryRect.Y, (float)AuxiliaryRect.W, (float)AuxiliaryRect.H, Render::Color(255, 255, 255, 150));
        D.DrawRect((float)AuxiliaryInnerRect.X, (float)AuxiliaryInnerRect.Y,
                   (float)AuxiliaryInnerRect.W, (float)AuxiliaryInnerRect.H,
                   HasModularConsole ? Render::Color(8, 8, 12, 120) : Render::Color(6, 6, 12, 100));
        D.DrawRectLines((float)AuxiliaryInnerRect.X, (float)AuxiliaryInnerRect.Y,
                        (float)AuxiliaryInnerRect.W, (float)AuxiliaryInnerRect.H,
                        HasModularConsole ? Render::Color(86, 96, 116, 100) : Render::Color(120, 90, 60, 120), 1.0f);

        D.DrawRect((float)PreviewPanel.X, (float)PreviewPanel.Y,
                   (float)PreviewPanel.W, (float)PreviewPanel.H,
                   Render::Color(10, 10, 14, 208));
        D.DrawRectLines((float)PreviewPanel.X, (float)PreviewPanel.Y,
                        (float)PreviewPanel.W, (float)PreviewPanel.H,
                        Render::Color(92, 112, 128, 150), 1.0f);
        D.DrawRect((float)PortraitPanel.X, (float)PortraitPanel.Y,
                   (float)PortraitPanel.W, (float)PortraitPanel.H,
                   Render::Color(10, 10, 14, 205));
        D.DrawRectLines((float)PortraitPanel.X, (float)PortraitPanel.Y,
                        (float)PortraitPanel.W, (float)PortraitPanel.H,
                        Render::Color(95, 150, 90, 160), 1.0f);

        const RectI PreviewImageRect =
        {
            PreviewPanel.X + 6,
            PreviewPanel.Y + 6,
            std::max(8, PreviewPanel.W - 12),
            std::max(8, PreviewPanel.H - 12)
        };
        if (WireframePreviewImage && WireframePreviewImage->Valid())
        {
            if (WireframeBounds && WireframeBounds->Valid())
            {
                DrawContainedAtlas(*WireframePreviewImage,
                                   { WireframeBounds->X, WireframeBounds->Y, WireframeBounds->W, WireframeBounds->H },
                                   PreviewImageRect,
                                   Render::Colors::White());
            }
            else
            {
                DrawContainedImage(*WireframePreviewImage, PreviewImageRect, Render::Colors::White());
            }
        }

        if (const auto* PortraitImage = GetPortraitImage(); PortraitImage && PortraitImage->Valid())
        {
            DrawContainedImage(*PortraitImage,
                               { PortraitPanel.X + 6, PortraitPanel.Y + 6, std::max(8, PortraitPanel.W - 12), std::max(8, PortraitPanel.H - 12) },
                               Render::Colors::White());
        }

        auto DrawFooterCard = [&](const RectI& CardRect,
                                  const std::string& Text,
                                  Render::Color Tint,
                                  const AtlasRect* ResourceIcon = nullptr,
                                  const Resource::SokolBridge::SokolImage* CommandMask = nullptr)
        {
            if (Text.empty())
                return;
            D.DrawRect((float)CardRect.X, (float)CardRect.Y,
                       (float)CardRect.W, (float)CardRect.H,
                       Render::Color(24, 24, 32, 230));
            D.DrawRectLines((float)CardRect.X, (float)CardRect.Y,
                            (float)CardRect.W, (float)CardRect.H,
                            Render::Color(100, 120, 110, 150), 1.0f);
            const RectI IconRect = { CardRect.X + 5, CardRect.Y + 7, 14, 14 };
            if (ResourceIcon && AggIcons.Valid())
                DrawAtlasImage(AggIcons, *ResourceIcon, IconRect, Render::Colors::White());
            else if (CommandMask && CommandMask->Valid())
                DrawContainedImage(*CommandMask, IconRect, Tint);

            D.DrawText(Ellipsize(Text, std::max<size_t>(8, (CardRect.W - 24) / 7)),
                       (float)CardRect.X + 23.0f, (float)CardRect.Y + 7.0f, 1.0f, Tint);
        };

        auto DrawBar = [&](int& BarY,
                           const char* Label,
                           int Value,
                           int MaxValue,
                           Render::Color FillColor)
        {
            if (MaxValue <= 0 || BarY + 8 > FooterY - 6)
                return;
            const int BarLabelW = 30;
            const int BarW = std::max(64, StatBodyRect.W - BarLabelW - 8);
            const float Frac = std::clamp((float)Value / (float)MaxValue, 0.0f, 1.0f);
            D.DrawText(Label, (float)StatBodyRect.X, (float)BarY - 1.0f, 1.0f, Render::Color(180, 180, 190, 255));
            D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
                       Render::Color(28, 28, 38, 230));
            if (HpEmptyStrip.Valid() && std::strcmp(Label, "HP") == 0)
            {
                DrawAtlasImage(HpEmptyStrip, { 0, 0, HpEmptyStrip.Width, HpEmptyStrip.Height },
                               { StatBodyRect.X + BarLabelW, BarY, BarW, 8 },
                               Render::Colors::White());
            }
            if (HpStrip.Valid() && std::strcmp(Label, "HP") == 0)
            {
                const int FilledW = std::max(1, (int)std::floor((float)BarW * Frac));
                DrawAtlasImage(HpStrip, { 0, 0, HpStrip.Width, HpStrip.Height },
                               { StatBodyRect.X + BarLabelW, BarY, FilledW, 8 },
                               Render::Colors::White());
            }
            else
            {
                D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW * Frac, 8.0f, FillColor);
            }
            D.DrawRectLines((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
                            Render::Color(100, 100, 110, 180), 1.0f);
            std::snprintf(Buf, sizeof(Buf), "%d/%d", Value, MaxValue);
            D.DrawText(Buf, (float)StatBodyRect.X + (float)BarLabelW + (float)BarW - 48.0f, (float)BarY - 1.0f, 1.0f,
                       Render::Color(220, 220, 230, 255));
            BarY += 14;
        };

        const size_t NameChars = std::max<size_t>(10, (size_t)std::floor((float)HeaderRect.W / 8.5f));
        const size_t DetailChars = std::max<size_t>(12, (size_t)std::floor((float)HeaderRect.W / 7.2f));
        const auto* MineralHdIcon = GetLegacyIcon(0);
        const auto* GasHdIcon = GetLegacyIcon(2);
        const auto* ArmorIcon = GetLegacyIcon(IconArmor);
        const auto* HarvestMask = GetCommandIconMask(CommandIconForCommand(HudCommandId::Harvest));
        const auto* MoveMask = GetCommandIconMask(CommandIconForCommand(HudCommandId::Move));

        if (ScvPanel)
        {
            D.DrawText(Ellipsize(mInfoPanel.Name, NameChars),
                       (float)HeaderRect.X, (float)HeaderRect.Y + 4.0f, 1.5f,
                       Render::Color(230, 230, 235, 255));
            D.DrawText(Ellipsize(mInfoPanel.RoleLine.empty() ? "Private" : mInfoPanel.RoleLine, DetailChars),
                       (float)HeaderRect.X, (float)HeaderRect.Y + 22.0f, 1.1f,
                       Render::Color(210, 210, 220, 255));
            std::snprintf(Buf, sizeof(Buf), "Kills: %d", mInfoPanel.KillCount);
            D.DrawText(Buf, (float)HeaderRect.X, (float)HeaderRect.Y + 38.0f, 1.0f,
                       Render::Color(220, 220, 225, 255));
            if (!mInfoPanel.StatusLine.empty())
            {
                D.DrawText(Ellipsize(mInfoPanel.StatusLine, DetailChars),
                           (float)HeaderRect.X + 150.0f, (float)HeaderRect.Y + 38.0f, 1.0f,
                           Render::Color(235, 220, 150, 255));
            }
            if (!mInfoPanel.DetailLine.empty())
            {
                D.DrawText(Ellipsize(mInfoPanel.DetailLine, DetailChars),
                           (float)HeaderRect.X, (float)HeaderRect.Y + 54.0f, 1.0f,
                           Render::Color(180, 210, 180, 255));
            }

            if (mInfoPanel.CarryingAmount > 0)
            {
                const bool CarryingGas = mInfoPanel.CargoLine.find("Gas") != std::string::npos;
                const auto* CargoHdIcon = CarryingGas ? GasHdIcon : MineralHdIcon;
                const RectI CargoIconRect = { HeaderRect.X + HeaderRect.W - 26, HeaderRect.Y + 4, 18, 18 };
                if (CargoHdIcon && CargoHdIcon->Valid())
                    DrawContainedImage(*CargoHdIcon, CargoIconRect, Render::Colors::White());
                std::snprintf(Buf, sizeof(Buf), "%d", mInfoPanel.CarryingAmount);
                D.DrawText(Buf, (float)CargoIconRect.X - 26.0f, (float)CargoIconRect.Y + 3.0f, 1.0f,
                           CarryingGas ? Render::Color(84, 208, 168, 255) : Render::Color(96, 180, 255, 255));
            }

            auto DrawMiniStat = [&](int X,
                                    const Resource::SokolBridge::SokolImage* Icon,
                                    const Resource::SokolBridge::SokolImage* Mask,
                                    const std::string& Value,
                                    Render::Color Tint) -> int
            {
                const RectI IconRect = { X, HeaderRect.Y + HeaderRect.H - 16, 14, 14 };
                if (Icon && Icon->Valid())
                    DrawContainedImage(*Icon, IconRect, Render::Colors::White());
                else if (Mask && Mask->Valid())
                    DrawContainedImage(*Mask, IconRect, Tint);
                D.DrawText(Value, (float)X + 18.0f, (float)HeaderRect.Y + (float)HeaderRect.H - 13.0f, 0.95f,
                           Render::Color(215, 220, 225, 255));
                return X + 18 + std::max(18, (int)Value.size() * 8) + 12;
            };

            int StatX = HeaderRect.X;
            StatX = DrawMiniStat(StatX, ArmorIcon, nullptr, std::to_string(mInfoPanel.Armor), Render::Color(215, 215, 215, 255));
            StatX = DrawMiniStat(StatX, nullptr, HarvestMask, std::to_string(mInfoPanel.CarryingAmount), Render::Color(228, 214, 96, 255));
            DrawMiniStat(StatX, nullptr, MoveMask, std::to_string(mInfoPanel.SpeedValue), Render::Color(214, 224, 238, 255));

            int BarY = StatBodyRect.Y;
            DrawBar(BarY, "HP", mInfoPanel.CurrentValue, mInfoPanel.MaxValue, Render::Color(80, 200, 95, 255));

            const bool CarryingGas = mInfoPanel.CargoLine.find("Gas") != std::string::npos;
            const AtlasRect* CargoIcon = mInfoPanel.CargoLine.empty() ? nullptr : (CarryingGas ? &AggIconGas : &AggIconMinerals);
            DrawFooterCard(FooterCardA,
                           mInfoPanel.CargoLine.empty() ? mInfoPanel.StatusLine : mInfoPanel.CargoLine,
                           Render::Color(170, 220, 210, 255),
                           CargoIcon,
                           mInfoPanel.CargoLine.empty() ? MoveMask : nullptr);
            if (!mInfoPanel.DetailLine.empty() && mInfoPanel.DetailLine != mInfoPanel.StatusLine)
            {
                DrawFooterCard(FooterCardB, mInfoPanel.DetailLine, Render::Color(200, 200, 185, 255), nullptr, HarvestMask);
            }
        }
        else
        {
            D.DrawText(Ellipsize(mInfoPanel.Name, NameChars), (float)HeaderRect.X, (float)HeaderRect.Y + 2.0f, 1.9f,
                       Render::Color(240, 220, 180, 255));
            if (!mInfoPanel.TypeLine.empty())
            {
                D.DrawText(Ellipsize(mInfoPanel.TypeLine, DetailChars), (float)HeaderRect.X, (float)HeaderRect.Y + 22.0f, 1.2f,
                           Render::Color(170, 185, 210, 255));
            }
            if (!mInfoPanel.StatusLine.empty())
            {
                D.DrawText(Ellipsize(mInfoPanel.StatusLine, DetailChars), (float)HeaderRect.X, (float)HeaderRect.Y + 38.0f, 1.2f,
                           Render::Color(220, 210, 140, 255));
            }
            if (!mInfoPanel.StatLine.empty())
            {
                D.DrawText(Ellipsize(mInfoPanel.StatLine, DetailChars), (float)HeaderRect.X, (float)HeaderRect.Y + 54.0f, 1.0f,
                           Render::Color(188, 192, 204, 255));
            }
            if (!mInfoPanel.CombatLine.empty())
            {
                D.DrawText(Ellipsize(mInfoPanel.CombatLine, DetailChars), (float)HeaderRect.X + (float)HeaderRect.W * 0.45f, (float)HeaderRect.Y + 54.0f, 1.0f,
                           Render::Color(214, 195, 160, 255));
            }

            int BarY = StatBodyRect.Y;
            DrawBar(BarY, "HP", mInfoPanel.CurrentValue, mInfoPanel.MaxValue, Render::Color(80, 200, 95, 255));
            DrawBar(BarY, "SH", mInfoPanel.ShieldValue, mInfoPanel.ShieldMax, Render::Color(70, 150, 255, 255));
            DrawBar(BarY, "EN", mInfoPanel.EnergyValue, mInfoPanel.EnergyMax, Render::Color(245, 200, 70, 255));
            if (mInfoPanel.Progress >= 0.0f)
            {
                float Progress = std::clamp(mInfoPanel.Progress, 0.0f, 1.0f);
                const int BarLabelW = 30;
                const int BarW = std::max(64, StatBodyRect.W - BarLabelW - 8);
                D.DrawText("PR", (float)StatBodyRect.X, (float)BarY - 1.0f, 1.0f, Render::Color(180, 180, 190, 255));
                D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
                           Render::Color(28, 28, 38, 230));
                D.DrawRect((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW * Progress, 8.0f,
                           Render::Color(220, 160, 70, 255));
                D.DrawRectLines((float)StatBodyRect.X + (float)BarLabelW, (float)BarY, (float)BarW, 8.0f,
                                Render::Color(100, 100, 110, 180), 1.0f);
                std::snprintf(Buf, sizeof(Buf), "%d%%", (int)std::lround(Progress * 100.0f));
                D.DrawText(Buf, (float)StatBodyRect.X + (float)BarLabelW + (float)BarW - 34.0f, (float)BarY - 1.0f, 1.0f,
                           Render::Color(220, 220, 230, 255));
                BarY += 14;
            }

            const bool CarryingGas = mInfoPanel.CargoLine.find("Gas") != std::string::npos;
            const AtlasRect* CargoIcon = mInfoPanel.CargoLine.empty() ? nullptr : (CarryingGas ? &AggIconGas : &AggIconMinerals);
            DrawFooterCard(FooterCardA,
                           mInfoPanel.CargoLine.empty() ? mInfoPanel.StatusLine : mInfoPanel.CargoLine,
                           Render::Color(175, 214, 210, 255),
                           CargoIcon,
                           nullptr);
            if (!mInfoPanel.DetailLine.empty())
            {
                DrawFooterCard(FooterCardB, mInfoPanel.DetailLine, Render::Color(196, 196, 184, 255), nullptr, nullptr);
            }
            else if (!mInfoPanel.CombatLine.empty() && mInfoPanel.CombatLine != mInfoPanel.StatLine)
            {
                DrawFooterCard(FooterCardB, mInfoPanel.CombatLine, Render::Color(214, 195, 160, 255), nullptr, nullptr);
            }

            if (!mInfoPanel.QueueEntries.empty())
            {
                D.DrawText(mInfoPanel.QueueLabel.empty() ? "Queue" : mInfoPanel.QueueLabel,
                           (float)QueueRect.X, (float)QueueRect.Y - 12.0f, 1.1f,
                           Render::Color(200, 200, 180, 255));
                const int QueueEntryHeight = 18;
                for (size_t I = 0; I < mInfoPanel.QueueEntries.size() && I < 5; ++I)
                {
                    const int EntryY = QueueRect.Y + (int)I * (QueueEntryHeight + 4);
                    D.DrawRect((float)QueueRect.X, (float)EntryY, (float)QueueRect.W, (float)QueueEntryHeight,
                               I == 0 ? Render::Color(70, 58, 28, 220) : Render::Color(26, 26, 38, 220));
                    D.DrawRectLines((float)QueueRect.X, (float)EntryY, (float)QueueRect.W, (float)QueueEntryHeight,
                                    Render::Color(110, 110, 120, 180), 1.0f);
                    D.DrawText(Ellipsize(mInfoPanel.QueueEntries[I], std::max<size_t>(10, QueueRect.W / 7)),
                               (float)QueueRect.X + 4.0f, (float)EntryY + 4.0f, 1.0f,
                               Render::Color(225, 225, 225, 255));
                }
            }

            if (mInfoPanel.SelectedCount > 1)
            {
                std::snprintf(Buf, sizeof(Buf), "Group: %d", mInfoPanel.SelectedCount);
                D.DrawText(Buf, (float)HeaderRect.X + (float)HeaderRect.W - 86.0f, (float)HeaderRect.Y + 2.0f, 1.0f,
                           Render::Color(180, 200, 220, 255));
            }
        }
    }

    D.DrawRectLines((float)Layout.MinimapRect.X - 1, (float)Layout.MinimapRect.Y - 1,
                    (float)Layout.MinimapRect.W + 2, (float)Layout.MinimapRect.H + 2,
                    Render::Color(100, 100, 120, 150), 1.0f);
    float MapPxW = (float)(MapWidthTiles * 32);
    float MapPxH = (float)(MapHeightTiles * 32);
    if (MapPxW > 0 && MapPxH > 0)
    {
        for (const auto& Entity : Entities.GetAll())
        {
            if (!Entity.Visible)
                continue;
            float U = Entity.X / MapPxW;
            float V = Entity.Y / MapPxH;
            int PX = Layout.MinimapRect.X + (int)std::lround(U * (float)Layout.MinimapRect.W);
            int PY = Layout.MinimapRect.Y + (int)std::lround(V * (float)Layout.MinimapRect.H);
            Render::Color Dot = (Entity.PlayerId == 0) ? Render::Color(80, 200, 255, 255)
                                                       : Render::Color(220, 60, 60, 255);
            D.DrawRect((float)PX, (float)PY, 2.0f, 2.0f, Dot);
        }
    }
}

} // namespace Game::SokolDemo::UI
