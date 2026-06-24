#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Game/sokol/Entity.h"
#include "Resource/sokol/GrpToSokol.h"

namespace Core::ECS { class EntityManager; }
namespace Resource::SokolBridge { class ResourceCacheSokol; }
namespace Game::Systems { enum class TerranResearchId : uint8_t; }

namespace Game::SokolDemo::UI
{

enum class ActionKind : uint8_t
{
    None,
    Command,
    Build,
    Train,
    Research,
    OpenSubmenu,
    Back
};

enum class HudCommandId : uint8_t
{
    None,
    Move,
    AttackMove,
    Stop,
    Hold,
    Patrol,
    Follow,
    Harvest
};

enum class PendingTargetMode : uint8_t
{
    None,
    BuildPlacement,
    BuildRefinery,
    Harvest,
    Move,
    AttackMove,
    Patrol,
    Follow
};

enum class HudVisualId : uint8_t
{
    None,
    BasicBuild,
    AdvancedBuild,
    Back
};

struct CmdButtonDef
{
    std::string Label;
    ActionKind Kind = ActionKind::None;
    HudCommandId CommandId = HudCommandId::None;
    Game::Systems::TerranResearchId ResearchId;
    int TargetUnitDefID = -1;
    int IconIndex = -1;
    int NextMenuPage = 0;
    bool Enabled = false;
    HudVisualId VisualId = HudVisualId::None;

    CmdButtonDef();
};

class Hud
{
public:
    void Init(Resource::SokolBridge::ResourceCacheSokol* Cache);
    void Shutdown();

    void UpdateContext(const EntityManager& Entities,
                       const std::vector<int>& SelectedIds,
                       const Core::ECS::EntityManager* EcsEntities = nullptr);

    void Draw(const EntityManager& Entities,
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
    bool HasPendingTarget() const { return mPendingTargetMode != PendingTargetMode::None; }
    PendingTargetMode GetPendingTargetMode() const { return mPendingTargetMode; }
    int GetPendingBuildDefID() const { return mPendingBuildDefID; }
    HudCommandId GetPendingCommandId() const { return mPendingCommandId; }

    const std::array<CmdButtonDef, 9>& GetButtonDefs() const { return mButtonDefs; }
    int GetHudHeight() const { return kHudHeight; }
    int GetHudHeight(int ViewportWidth, int ViewportHeight) const;
    int GetResourceBarHeight() const { return kResourceBarHeight; }

    int MineralCount = 50;
    int GasCount = 0;
    int SupplyUsed = 0;
    int SupplyMax = 200;
    int ActiveCommand = -1;

private:
    enum class PanelCategory : uint8_t
    {
        None,
        Terran,
        Zerg,
        Protoss
    };

    const Resource::SokolBridge::SokolImage* GetLegacyIcon(int IconIndex) const;
    const Resource::SokolBridge::SokolImage* GetCommandIcon(int IconIndex) const;
    const Resource::SokolBridge::SokolImage* GetCommandIconMask(int IconIndex) const;
    const Resource::SokolBridge::SokolImage* GetFirstValidSpriteFrame(int ChkUnitId) const;
    const Resource::SokolBridge::SokolImage* GetSelectionPreviewImage() const;
    const Resource::SokolBridge::SokolImage* GetButtonImage(const CmdButtonDef& Button) const;
    const Resource::SokolBridge::SokolImage* GetButtonMaskImage(const CmdButtonDef& Button) const;

    int GetDefaultPageForSelection(int SelectedChkUnitId) const;
    int GetDefaultTerranPageForDef(int SelectedTerranDefId) const;
    int ResolveSelectedTerranDefId(int SelectedEntityId,
                                   int SelectedChkUnitId,
                                   const Core::ECS::EntityManager* EcsEntities) const;
    void BuildTerranPage(int SelectedEntityId,
                         int SelectedTerranDefId,
                         const Core::ECS::EntityManager* EcsEntities);
    void BuildZergPage(int SelectedEntityId,
                       int SelectedChkUnitId,
                       const Core::ECS::EntityManager* EcsEntities);
    void BuildProtossPage(int SelectedEntityId,
                          int SelectedChkUnitId,
                          const Core::ECS::EntityManager* EcsEntities);
    void ResetInfoPanel();
    void UpdateInfoPanel(const EntityManager& Entities,
                         int SelectedEntityId,
                         int SelectedChkUnitId,
                         int SelectedTerranDefId,
                         const Core::ECS::EntityManager* EcsEntities);
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

    int mSelectedCount = 0;
    int mLastHovered = -1;
    int mSelectedEntityId = -1;
    int mSelectedChkUnitId = -1;
    int mSelectedTerranDefId = -1;
    int mPanelPage = 0;
    PanelCategory mPanelCategory = PanelCategory::None;
    std::array<CmdButtonDef, 9> mButtonDefs;
    std::vector<int> mLastSelectedIds;
    struct InfoPanelState
    {
        bool HasSelection = false;
        bool IsBuilding = false;
        bool IsResource = false;
        bool UnderConstruction = false;
        int SelectedCount = 0;
        int IconIndex = -1;
        int PreviewDefID = -1;
        int PreviewChkUnitId = -1;
        int PlayerID = -1;
        int CurrentValue = 0;
        int MaxValue = 0;
        int ShieldValue = 0;
        int ShieldMax = 0;
        int EnergyValue = 0;
        int EnergyMax = 0;
        int Armor = 0;
        int SightValue = 0;
        int SpeedValue = 0;
        int GroundDamage = 0;
        int AirDamage = 0;
        int RangeValue = 0;
        int ResourceAmount = 0;
        int CarryingAmount = 0;
        int KillCount = 0;
        float Progress = -1.0f;
        std::string Name;
        std::string RoleLine;
        std::string TypeLine;
        std::string StatusLine;
        std::string StatLine;
        std::string CombatLine;
        std::string DetailLine;
        std::string CargoLine;
        std::string QueueLabel;
        std::vector<std::string> QueueEntries;
    } mInfoPanel;
    PendingTargetMode mPendingTargetMode = PendingTargetMode::None;
    int mPendingBuildDefID = -1;
    HudCommandId mPendingCommandId = HudCommandId::None;
    Resource::SokolBridge::ResourceCacheSokol* mCache = nullptr;

    static constexpr int kHudHeight = 320;
    static constexpr int kMinimapSize = 244;
    static constexpr int kCommandPanelSize = 112;
    static constexpr int kResourceBarHeight = 20;
};

} // namespace Game::SokolDemo::UI
