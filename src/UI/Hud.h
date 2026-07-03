#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Resource/GpuImage.h"
#include "Resource/SpriteAssetCacheView.h"
#include "Resource/UIAssetCacheView.h"
#include "UI/HudState.h"

namespace Game::Client::UI
{

struct HudLayout;

class Hud
{
public:
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
    void DrawMinimap(float PosX, float PosY, float SizeW, float SizeH) const;

    const HudState& GetState() const { return mState; }
    const HudResourceState& GetResourceState() const { return mState.Resources; }
    void SetResourceState(const HudResourceState& Resources) { mState.Resources = Resources; }

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
                   HudVisualId VisualId = HudVisualId::None);
    void ResetButtons();
    void DrawMinimapOverlay(Core::ECS::EntityManager& Entities,
                            const HudLayout& Layout,
                            int MapWidthTiles,
                            int MapHeightTiles) const;
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
    void DrawInfoPanel(Core::ECS::EntityManager& Entities,
                       const HudLayout& Layout,
                       bool HasModularConsole,
                       const Resource::Graphics::GpuImage& AggIcons);

    HudState mState;
    Resource::Graphics::UIAssetCacheView mUIAssets;
    Resource::Graphics::SpriteAssetCacheView mSpriteAssets;

    static constexpr int kHudHeight = 320;
    static constexpr int kMinimapSize = 244;
    static constexpr int kCommandPanelSize = 112;
    static constexpr int kResourceBarHeight = 20;
};

} // namespace Game::Client::UI





