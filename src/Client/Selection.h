#pragma once

// === Consolidated from src/Client/SelectionFootprint.h ===
#include <algorithm>
#include <cmath>

namespace Game::Client
{

struct BuildingFootprintTiles
{
    float W = 3.0f;
    float H = 2.0f;
};

struct BuildingFootprintSpec
{
    float CenterX = 0.0f;
    float CenterY = 0.0f;
    float RadiusX = 24.0f;
    float RadiusY = 8.0f;
};

inline BuildingFootprintSpec SelectionEllipseFromVisualBounds(float X,
                                                              float Y,
                                                              float FrameHalfW,
                                                              float FrameHalfH,
                                                              float FallbackRadius)
{
    float Radius = FallbackRadius;
    if (FrameHalfW > 0.0f && FrameHalfH > 0.0f)
        Radius = std::sqrt(FrameHalfW * FrameHalfW + FrameHalfH * FrameHalfH);

    Radius = std::max(8.0f, Radius);
    return {
        X,
        Y,
        Radius,
        std::max(4.0f, Radius * 0.5f)
    };
}

inline BuildingFootprintSpec SelectionEllipseFromVisualRadius(float X,
                                                              float Y,
                                                              float VisualRadius,
                                                              float FallbackRadius)
{
    float Radius = VisualRadius > 0.0f ? VisualRadius : FallbackRadius;
    Radius = std::max(8.0f, Radius);
    return {
        X,
        Y,
        Radius,
        std::max(4.0f, Radius * 0.5f)
    };
}

inline BuildingFootprintSpec SelectionEllipseFromDatDimensions(float X,
                                                               float Y,
                                                               int DimensionWidth,
                                                               int DimensionHeight,
                                                               float FallbackRadius)
{
    if (DimensionWidth > 0 && DimensionHeight > 0)
    {
        return {
            X,
            Y,
            std::max(8.0f, (float)DimensionWidth * 0.5f),
            std::max(4.0f, (float)DimensionHeight * 0.5f)
        };
    }

    const float Radius = std::max(8.0f, FallbackRadius);
    return {
        X,
        Y,
        Radius,
        std::max(4.0f, Radius * 0.5f)
    };
}

inline BuildingFootprintSpec SelectionEllipseFromDatBounds(float X,
                                                           float Y,
                                                           int Left,
                                                           int Up,
                                                           int Right,
                                                           int Down,
                                                           float FallbackRadius)
{
    const int Width = Left + Right + 1;
    const int Height = Up + Down + 1;
    if (Width > 0 && Height > 0)
    {
        return {
            X + ((float)Right + 1.0f - (float)Left) * 0.5f,
            Y + ((float)Down + 1.0f - (float)Up) * 0.5f,
            std::max(8.0f, (float)Width * 0.5f),
            std::max(4.0f, (float)Height * 0.5f)
        };
    }

    return SelectionEllipseFromDatDimensions(X, Y, Width, Height, FallbackRadius);
}

inline int SelectionCirclePixelSize(int SelectionCircle)
{
    static constexpr int kSizes[] = {22, 32, 48, 62, 72, 94, 110, 122, 146, 224};
    if (SelectionCircle < 0 || SelectionCircle >= (int)(sizeof(kSizes) / sizeof(kSizes[0])))
        return 0;
    return kSizes[SelectionCircle];
}

inline BuildingFootprintSpec SelectionEllipseFromSpriteCircle(float X,
                                                              float Y,
                                                              int SelectionCircle,
                                                              int VerticalOffset,
                                                              float FallbackRadius)
{
    const int PixelSize = SelectionCirclePixelSize(SelectionCircle);
    if (PixelSize > 0)
    {
        const float RadiusX = (float)PixelSize * 0.5f;
        return {
            X,
            Y + (float)VerticalOffset,
            std::max(8.0f, RadiusX),
            std::max(4.0f, RadiusX * 0.5f)
        };
    }

    const float Radius = std::max(8.0f, FallbackRadius);
    return {
        X,
        Y + (float)VerticalOffset,
        Radius,
        std::max(4.0f, Radius * 0.5f)
    };
}

inline BuildingFootprintTiles BuildingFootprintSize(int ChkUnitID)
{
    switch (ChkUnitID)
    {
        case 106: // Terran Command Center
        case 111: // Terran Barracks
        case 113: // Terran Factory
        case 114: // Terran Starport
        case 116: // Terran Science Facility
        case 131: // Zerg Hatchery
        case 132: // Zerg Lair
        case 133: // Zerg Hive
        case 154: // Protoss Nexus
        case 160: // Protoss Gateway
        case 167: // Protoss Stargate
            return {4.0f, 3.0f};
        case 109: // Terran Supply Depot
        case 112: // Terran Academy
        case 122: // Terran Engineering Bay
        case 123: // Terran Armory
        case 125: // Terran Bunker
        case 134: // Zerg Nydus Canal
        case 135: // Zerg Hydralisk Den
        case 136: // Zerg Defiler Mound
        case 138: // Zerg Queen's Nest
        case 139: // Zerg Evolution Chamber
        case 140: // Zerg Ultralisk Cavern
        case 142: // Zerg Spawning Pool
        case 155: // Protoss Robotics Facility
        case 159: // Protoss Observatory
        case 163: // Protoss Citadel of Adun
        case 164: // Protoss Cybernetics Core
        case 165: // Protoss Templar Archives
        case 166: // Protoss Forge
        case 169: // Protoss Fleet Beacon
        case 170: // Protoss Arbiter Tribunal
        case 171: // Protoss Robotics Support Bay
            return {3.0f, 2.0f};
        case 110: // Terran Refinery
        case 149: // Zerg Extractor
        case 157: // Protoss Assimilator
            return {4.0f, 2.0f};
        case 107: // Terran Comsat Station
        case 108: // Terran Nuclear Silo
        case 115: // Terran Control Tower
        case 117: // Terran Covert Ops
        case 118: // Terran Physics Lab
        case 120: // Terran Machine Shop
        case 124: // Terran Missile Turret
        case 137: // Zerg Greater Spire
        case 141: // Zerg Spire
        case 156: // Protoss Pylon
        case 162: // Protoss Photon Cannon
        case 172: // Protoss Shield Battery
            return {2.0f, 2.0f};
        default:
            return {3.0f, 2.0f};
    }
}

inline BuildingFootprintSpec BuildingFootprintForSelection(float X,
                                                           float Y,
                                                           int ChkUnitID,
                                                           float FrameHalfW = 0.0f,
                                                           float FrameHalfH = 0.0f)
{
    const BuildingFootprintTiles Tiles = BuildingFootprintSize(ChkUnitID);
    const float FallbackHalfW = Tiles.W * 16.0f;
    const float FallbackHalfH = Tiles.H * 16.0f;
    const float FallbackRadius =
        std::sqrt(FallbackHalfW * FallbackHalfW + FallbackHalfH * FallbackHalfH);
    return SelectionEllipseFromVisualBounds(X, Y, FrameHalfW, FrameHalfH, FallbackRadius);
}

} // namespace Game::Client

// === End src/Client/SelectionFootprint.h ===

#include "Core/Platform/Platform.h"
#include "Core/ECS/EntityManager.h"
#include "Engine/Input/CameraAndSelectionServer2D.h"
#include "Game/Components.h"
#include "Resource/UIAssetCacheView.h"
#include "Render/Camera2D.h"

#include <vector>
#include <array>

namespace Game::Client
{

class Selection
{
public:
    enum class Phase
    {
        Idle,
        Dragging
    };

    static constexpr int kMaxSelection = 12;
    static constexpr int kNumControlGroups = 10;

    void Update(Core::ECS::EntityManager& Entities,
                const Render::Camera2D& Camera);
    void PruneAgainst(Core::ECS::EntityManager& Entities);
    void CancelPointerGesture();
    void ResetClickTracking();
    void ResetSessionState();
    void Init(Resource::Graphics::DataAssetCacheView DataAssets) { mDataAssets = DataAssets; }
    void SetLocalPlayerId(int PlayerId) { mLocalPlayerId = PlayerId; }
    int GetLocalPlayerId() const { return mLocalPlayerId; }

    bool IsDragging() const { return mPhase == Phase::Dragging; }
    void GetDragRectWorld(float& X, float& Y, float& W, float& H) const;
    void GetDragRectScreen(float& X, float& Y, float& W, float& H) const;
    int PickEntityAtWorld(Core::ECS::EntityManager& Entities, float WorldX, float WorldY) const;
    std::vector<int> CollectRectSelection(Core::ECS::EntityManager& Entities,
                                          float WorldX,
                                          float WorldY,
                                          float WorldW,
                                          float WorldH) const;

    int Count() const;
    int Get(int Index) const;
    const std::vector<int>& GetSelected() const { return mSelected; }
    const std::vector<Core::ECS::EntityHandle>& GetSelectedHandles() const { return mSelectedHandles; }
    void SetSelectedForDebug(std::vector<int> Selected);
    void SetSelectedWithHandles(const Core::ECS::EntityManager& Entities, std::vector<int> Selected);

    // Control groups
    void SaveControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities);
    void RecallControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities);
    void AppendControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities);
    bool IsControlGroupEmpty(int GroupIndex) const;

    using UnitClickSoundCallback = std::function<void(int EntityID, bool IsPissed)>;
    static void SetUnitClickSoundCallback(UnitClickSoundCallback Callback);

    // Phase 74 Engine CameraAndSelectionServer2D Convergence
    void BindEngineSelectionServer(Engine::Input::CameraAndSelectionServer2D* Server) noexcept
    {
        mBoundSelectionServer = Server;
    }

    Engine::Input::CameraAndSelectionServer2D* GetBoundEngineSelectionServer() const noexcept
    {
        return mBoundSelectionServer;
    }

    void SyncToEngineSelectionServer(const Core::ECS::EntityManager& Entities) const
    {
        if (mBoundSelectionServer)
            mBoundSelectionServer->SetActiveSelection(Entities, mSelected);
    }

private:
    Phase mPhase = Phase::Idle;
    int mStartX = 0, mStartY = 0;
    int mEndX = 0, mEndY = 0;
    std::vector<int> mSelected;
    std::vector<Core::ECS::EntityHandle> mSelectedHandles;
    int mLocalPlayerId = 0;
    Resource::Graphics::DataAssetCacheView mDataAssets;
    Engine::Input::CameraAndSelectionServer2D* mBoundSelectionServer = nullptr;

    // Control groups: 10 groups of up to 12 entity IDs + generational EntityHandles
    std::array<std::vector<int>, kNumControlGroups> mControlGroups;
    std::array<std::vector<Core::ECS::EntityHandle>, kNumControlGroups> mControlGroupHandles;

    // Encapsulated per-instance click & double-click tracking state
    int mConsecutiveClickCount = 0;
    int mLastClickedEntity = -1;
    double mLastEntityClickTime = 0.0;
    double mLastClickTime = 0.0;
    int mLastClickId = -1;
    int mLastClickX = 0;
    int mLastClickY = 0;

    void SyncSelectedHandles(const Core::ECS::EntityManager& Entities);

    float PickRadiusForSize(Game::Components::UnitSize Size) const;
    bool EntityHitTest(Core::ECS::EntityManager& Entities, int EntityId, float WX, float WY, float& OutScore) const;
    bool EntityInRect(Core::ECS::EntityManager& Entities, int EntityId, float RX, float RY, float RW, float RH) const;
    bool IsMultiSelectable(Core::ECS::EntityManager& Entities, int EntityId) const;
    bool IsOwnedByLocalPlayer(Core::ECS::EntityManager& Entities, int EntityId) const;
    bool IsVisibleOnCurrentScreen(Core::ECS::EntityManager& Entities, int EntityId, const Render::Camera2D& Camera) const;
    bool CanAppendToSelection(Core::ECS::EntityManager& Entities,
                              const std::vector<int>& Selection,
                              int CandidateId) const;
    void AppendSelectionEntity(Core::ECS::EntityManager& Entities,
                               std::vector<int>& Selection,
                               int EntityId) const;
};

} // namespace Game::Client
