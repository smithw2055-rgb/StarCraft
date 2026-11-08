#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Render/GpuTextureHandle.h"
#include "Resource/UIAssetTypes.h"
#include "Resource/UIAssetCacheView.h"
#include "UI/HudState.h"
#include "UI/ViewModel/HudViewModel.h"

namespace Game::Map { class GameMap; }
namespace Game::Client { struct GameMap; }
namespace Game::Systems { class FogOfWarSystem; }

namespace Game::Client::UI
{

struct HudLayout;

class Hud
{
public:
    Hud() = default;
    ~Hud() = default;

    void Init(Resource::Graphics::UIAssetCacheView UIAssets,
              Resource::Graphics::SpriteAssetCacheView SpriteAssets);
    void Shutdown();

    void UpdateContext(Core::ECS::EntityManager& Entities,
                       const std::vector<int>& SelectedIds);

    void Draw(Core::ECS::EntityManager& Entities,
              const std::vector<int>& SelectedIds,
              int MapWidthTiles, int MapHeightTiles,
              int ViewportWidth, int ViewportHeight);

    int CheckCommandPanelClick(int ScreenX, int ScreenY, int ViewportWidth, int ViewportHeight) const;
    int CheckQueueSlotClick(int ScreenX, int ScreenY) const;
    int CheckMultiSelectSlotClick(int ScreenX, int ScreenY, int& OutEntityId) const;
    int CheckMinimapButtonClick(int ScreenX, int ScreenY, int ViewportWidth, int ViewportHeight) const;
    void ToggleMinimapTerrain();
    void ToggleMinimapAllianceColors();
    void ToggleMinimapBeacons();
    bool GetMinimapShowTerrain() const { return mState.Minimap.ShowTerrain; }
    bool GetMinimapUseAllianceColors() const { return mState.Minimap.UseAllianceColors; }
    bool GetMinimapShowBeacons() const { return mState.Minimap.ShowBeacons; }
    void SetMatchElapsedSeconds(float Seconds) { mState.MatchElapsedSeconds = Seconds; }
    bool CheckMinimapClick(int ScreenX, int ScreenY,
                           int ViewportWidth, int ViewportHeight,
                           int MapWidthTiles, int MapHeightTiles,
                           float& OutWorldX, float& OutWorldY) const;
    bool IsPointInHudBand(int ScreenX, int ScreenY, int ViewportWidth, int ViewportHeight) const;

    void OnButtonInvoked(int ButtonIndex);
    void BeginCommandTarget(PendingTargetMode Mode, HudCommandId CommandId);
    void CancelPendingTarget();
    bool HasPendingTarget() const { return mState.CommandPanel.PendingTargetModeValue != PendingTargetMode::None; }
    PendingTargetMode GetPendingTargetMode() const { return mState.CommandPanel.PendingTargetModeValue; }
    int GetPendingBuildDefID() const { return mState.CommandPanel.PendingBuildDefID; }
    HudCommandId GetPendingCommandId() const { return mState.CommandPanel.PendingCommandId; }
    int GetActiveCommandIndex() const { return mState.ActiveCommand; }

    const std::array<CmdButtonDef, 9>& GetButtonDefs() const { return mState.CommandPanel.ButtonDefs; }
    int GetHudHeight() const { return kHudHeight; }
    int GetHudHeight(int ViewportWidth, int ViewportHeight) const;
    int GetResourceBarHeight() const { return kResourceBarHeight; }

    // Minimap
    void SetMinimapState(float CamX, float CamY, float CamZoom,
                         int VpWidth, int VpHeight,
                         int MapW, int MapH,
                         const int* TileData,
                         const bool* FogVisible, const bool* FogExplored);
    void SetMinimapViewportRect(float CamWorldX, float CamWorldY, float CamWorldW, float CamWorldH);
    void UpdateMinimapTerrain(const Game::Map::GameMap* EcsMap,
                             const Game::Client::GameMap* ClientMap,
                             const Game::Systems::FogOfWarSystem* Fog,
                             int LocalPlayerID);
    void SetCampaignPings(const std::vector<HudMinimapPing>& Pings)
    {
        mState.Minimap.CampaignPings = Pings;
    }
    void SetMinimapDots(const std::vector<HudMinimapDot>& Dots)
    {
        mState.Minimap.UnitDots = Dots;
    }
    void SetMinimapMapSize(int MapW, int MapH)
    {
        mState.Minimap.MapWidth = MapW;
        mState.Minimap.MapHeight = MapH;
    }

    const HudState& GetState() const { return mState; }
    const HudResourceState& GetResourceState() const { return mState.Resources; }
    void SetResourceState(const HudResourceState& Resources)
    {
        mState.Resources = Resources;
        mViewModel.SetResourceState(Resources);
    }

    ViewModel::HudViewModel& GetViewModel() { return mViewModel; }
    const ViewModel::HudViewModel& GetViewModel() const { return mViewModel; }
    void SetUseReactiveBinding(bool Enabled) { mUseReactiveBinding = Enabled; }
    bool GetUseReactiveBinding() const { return mUseReactiveBinding; }

    std::string GetLegacyIconUri(int IconIndex) const;
    std::string GetButtonIconUri(const CmdButtonDef& Button) const;
    std::string GetUnitIconUriForChkUnitId(int ChkUnitId) const;

private:
    const Resource::Graphics::GpuImage* GetLegacyIcon(int IconIndex) const;
    const Resource::Graphics::GpuImage* GetCommandIcon(int IconIndex) const;
    const Resource::Graphics::GpuImage* GetCommandIconMask(int IconIndex) const;
    const Resource::Graphics::GpuImage* GetFirstValidSpriteFrame(int ChkUnitId) const;
    const Resource::Graphics::GpuImage* GetSelectionPreviewImage() const;
    const Resource::Graphics::GpuImage* GetAnimatedSelectionFallbackImage(Core::ECS::EntityManager& Entities) const;
    const Resource::Graphics::GpuImage* GetButtonImage(const CmdButtonDef& Button) const;
    const Resource::Graphics::GpuImage* GetButtonMaskImage(const CmdButtonDef& Button) const;

    int GetDefaultPageForSelection(int SelectedChkUnitId) const;
    int GetDefaultTerranPageForDef(int SelectedTerranDefId) const;
    int ResolveSelectedTerranDefId(int SelectedEntityId,
                                   int SelectedChkUnitId,
                                   Core::ECS::EntityManager* EcsEntities) const;
    void BuildTerranPage(int SelectedEntityId,
                         int SelectedTerranDefId,
                         Core::ECS::EntityManager* EcsEntities);
    void BuildZergPage(int SelectedEntityId,
                       int SelectedChkUnitId,
                       Core::ECS::EntityManager* EcsEntities);
    void BuildProtossPage(int SelectedEntityId,
                          int SelectedChkUnitId,
                       Core::ECS::EntityManager* EcsEntities);
    void ResetInfoPanel();
    void UpdateInfoPanel(Core::ECS::EntityManager& Entities,
                         int SelectedEntityId,
                         int SelectedChkUnitId,
                         int SelectedTerranDefId,
                         Core::ECS::EntityManager* EcsEntities);
    void SetButton(int Index,
                   const std::string& Label,
                   ActionKind Kind,
                   int TargetUnitDefID,
                   HudCommandId CommandId,
                   Game::Systems::TerranResearchId ResearchId,
                   int IconIndex,
                   bool Enabled,
                   int NextMenuPage = 0,
                   HudVisualId VisualId = HudVisualId::None,
                   int MineralCost = 0,
                   int GasCost = 0,
                   int SupplyCost = 0,
                   char Hotkey = '\0');
    void ResetButtons();
    void RebuildBaseTerrainColors(int MapW, int MapH,
                                  const Game::Map::GameMap* EcsMap,
                                  const Game::Client::GameMap* ClientMap);
    void DrawMinimapOverlay(Core::ECS::EntityManager& Entities,
                            const HudLayout& Layout,
                            int MapWidthTiles,
                            int MapHeightTiles);
    bool DrawConsoleChrome(const HudLayout& Layout, int ViewportWidth);
    void DrawCommandPanel(const HudLayout& Layout, int ViewportWidth, int ViewportHeight);
    void DrawResourceBar(int ViewportWidth, const Resource::Graphics::GpuImage& AggIcons);
    void DrawInfoPanelFrame(const HudLayout& Layout, bool HasModularConsole);
    void DrawInfoPanelPortraits(Core::ECS::EntityManager& Entities,
                                const HudLayout& Layout,
                                bool HasModularConsole);
    void DrawScvInfoPanel(const HudLayout& Layout,
                          bool HasModularConsole,
                          const Resource::Graphics::GpuImage& AggIcons);
    void DrawStandardInfoPanel(const HudLayout& Layout,
                               bool HasModularConsole,
                               const Resource::Graphics::GpuImage& AggIcons);
    void DrawMultiSelectPanel(const HudLayout& Layout, bool HasModularConsole);
    void DrawMinimapButtons(const HudLayout& Layout) const;
    void DrawInfoPanel(Core::ECS::EntityManager& Entities,
                       const HudLayout& Layout,
                       bool HasModularConsole,
                       const Resource::Graphics::GpuImage& AggIcons);

    HudState mState;
    ViewModel::HudViewModel mViewModel;
    bool mUseReactiveBinding = true;
    Resource::Graphics::UIAssetCacheView mUIAssets;
    Resource::Graphics::SpriteAssetCacheView mSpriteAssets;

    Render::GpuTextureHandle mMinimapTexture = {};
    std::vector<uint32_t> mMinimapPixels;
    std::vector<uint32_t> mBaseTerrainColors;
    int mMinimapTexWidth = 0;
    int mMinimapTexHeight = 0;
    std::string mMinimapTilesetName;
    uint32_t mLastFogRevision = 0;
    int mLastLocalPlayerID = -1;
    double mLastMinimapTerrainTime = -1.0;

    static constexpr int kHudHeight = 320;
    static constexpr int kMinimapSize = 244;
    static constexpr int kCommandPanelSize = 112;
    static constexpr int kResourceBarHeight = 20;
};

} // namespace Game::Client::UI

// === Consolidated from src/UI/HudPortrait.h ===


#include <string>

#include "Core/Platform/Platform.h"
#include "Game/Races/RaceDef.h"
#include "Resource/UIAssetCacheView.h"

namespace Game::Client::UI
{

inline std::string ResolvePortraitKeyForChkUnitId(int ChkUnitId)
{
    switch (ChkUnitId)
    {
        case 0: return "tmarine";
        case 1: return "tghost";
        case 2: return "tvulture";
        case 3:
        case 4: return "tgoliath";
        case 5:
        case 6:
        case 30:
        case 31: return "ttank";
        case 7: return "tscv";
        case 8:
        case 21: return "tphoenix";
        case 9:
        case 22: return "tvessel";
        case 10:
        case 32: return "tfirebat";
        case 11: return "tdropshi";
        case 12:
        case 27:
        case 28:
        case 29: return "tbattlec";
        case 16: return "ukerr";
        case 19:
        case 20: return "ujim";
        case 23:
        case 24:
        case 25:
        case 26: return "uduke";
        case 17:
        case 18:
        case 100: return "talex";
        case 34: return "tmedic";
        case 58: return "tbomber";
        case 99:
        case 104: return "tsam";
        case 102: return "tdugal";

        case 35: return "zlarva";
        case 36: return "zegg";
        case 37:
        case 54: return "zzergl";
        case 38:
        case 53: return "zhydra";
        case 39:
        case 48: return "zultra";
        case 40: return "zbrood";
        case 41: return "zdrone";
        case 42:
        case 57: return "zover";
        case 43:
        case 55: return "zmutalid";
        case 44:
        case 56: return "zguard";
        case 45:
        case 49: return "zqueen";
        case 46:
        case 52: return "zdefiler";
        case 47: return "zavenger";
        case 59:
        case 97: return "zegg";
        case 50: return "zinfestt";
        case 51: return "ukerr";
        case 62: return "zdevour";
        case 103: return "zlurker";

        case 64: return "pprobe";
        case 65: return "pzealot";
        case 66: return "pdragoon";
        case 67:
        case 87: return "ptemplar";
        case 68:
        case 76: return "parchon";
        case 69: return "pshuttle";
        case 70:
        case 80: return "pscout";
        case 71:
        case 86: return "parbiter";
        case 72:
        case 82: return "pcarrier";
        case 73: return "pinterce";
        case 61:
        case 74: return "udtemplar";
        case 77: return "ufenzeal";
        case 78: return "ufendrag";
        case 75: return "uzeratul";
        case 79: return "utassadar";
        case 84: return "pwitness";
        case 63: return "pdarchon";
        case 60: return "pcorsair";
        case 88: return "partanis";

        case 89: return "nredcrit";
        case 92: return "nbiker";
        case 90: return "njungcrit";
        case 93: return "ngastank";
        case 95: return "nlavacrit";
        case 96: return "nicecrit";

        // Terran Buildings -> Terran Advisor
        case 106: case 107: case 108: case 109: case 110:
        case 111: case 112: case 113: case 114: case 115:
        case 116: case 117: case 118: case 119: case 120:
        case 121: case 122: case 123: case 124: case 125:
        case 126: case 127: case 128: case 129: case 130:
            return "tadvisor";

        // Zerg Buildings -> Zerg Advisor
        case 131: case 132: case 133: case 134: case 135:
        case 136: case 137: case 138: case 139: case 140:
        case 141: case 142: case 143: case 144: case 145:
        case 146: case 147: case 148: case 149: case 150:
        case 151: case 152: case 153:
            return "zadvisor";

        // Protoss Buildings -> Protoss Advisor
        case 154: case 155: case 156: case 157: case 158:
        case 159: case 160: case 161: case 162: case 163:
        case 164: case 165: case 166: case 167: case 168:
        case 169: case 170: case 171: case 172:
            return "padvisor";

        // Resources
        case 176: case 177: case 178: return "nore";
        case 188: return "ngassac";
        default: return {};
    }
}

inline std::string ResolvePortraitKeyForSelection(int TerranDefId)
{
    if (TerranDefId < 0)
        return {};

    const auto Def = Game::Races::GetUnitDefByID(TerranDefId);
    if (Def.Name == "Unknown")
        return {};

    return ResolvePortraitKeyForChkUnitId(Def.ChkUnitID);
}

inline const Resource::Graphics::GpuImage* ResolvePortraitFrameForSelection(
    const Resource::Graphics::UIAssetCacheView& UIAssets,
    int TerranDefId,
    const Resource::Graphics::GpuImage* FallbackImage)
{
    const std::string PortraitKey = ResolvePortraitKeyForSelection(TerranDefId);
    const auto* Frame = PortraitKey.empty()
        ? nullptr
        : UIAssets.GetPortraitFrame(PortraitKey, Core::Platform::GetAbsoluteTime());
    if (Frame && Frame->Image.Valid())
        return &Frame->Image;

    return FallbackImage;
}

inline const Resource::Graphics::GpuImage* ResolvePortraitFrameForChkUnitId(
    const Resource::Graphics::UIAssetCacheView& UIAssets,
    int ChkUnitId,
    const Resource::Graphics::GpuImage* FallbackImage)
{
    const std::string PortraitKey = ResolvePortraitKeyForChkUnitId(ChkUnitId);
    const auto* Frame = PortraitKey.empty()
        ? nullptr
        : UIAssets.GetPortraitFrame(PortraitKey, Core::Platform::GetAbsoluteTime());
    if (Frame && Frame->Image.Valid())
        return &Frame->Image;

    return FallbackImage;
}

} // namespace Game::Client::UI
