#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Render/GpuTextureHandle.h"
#include "Resource/GpuImage.h"
#include "Resource/SpriteAssetCacheView.h"
#include "Resource/UIAssetCacheView.h"
#include "UI/HudState.h"

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
    void DrawMinimap(float PosX, float PosY, float SizeW, float SizeH) const;

    const HudState& GetState() const { return mState; }
    const HudResourceState& GetResourceState() const { return mState.Resources; }
    void SetResourceState(const HudResourceState& Resources) { mState.Resources = Resources; }
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
    Resource::Graphics::UIAssetCacheView mUIAssets;
    Resource::Graphics::SpriteAssetCacheView mSpriteAssets;

    Render::GpuTextureHandle mMinimapTexture = {};
    std::vector<uint32_t> mMinimapPixels;
    std::vector<uint32_t> mBaseTerrainColors;
    int mMinimapTexWidth = 0;
    int mMinimapTexHeight = 0;
    std::string mMinimapTilesetName;

    static constexpr int kHudHeight = 320;
    static constexpr int kMinimapSize = 244;
    static constexpr int kCommandPanelSize = 112;
    static constexpr int kResourceBarHeight = 20;
};

} // namespace Game::Client::UI
