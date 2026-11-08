#pragma once

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
