#pragma once

#include "Core/Platform/Platform.h"
#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Resource/DataAssetCacheView.h"
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
    void Init(Resource::Graphics::DataAssetCacheView DataAssets) { mDataAssets = DataAssets; }
    void SetLocalPlayerId(int PlayerId) { mLocalPlayerId = PlayerId; }

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

    // Control groups
    void SaveControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities);
    void RecallControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities);
    void AppendControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities);
    bool IsControlGroupEmpty(int GroupIndex) const;

private:
    Phase mPhase = Phase::Idle;
    int mStartX = 0, mStartY = 0;
    int mEndX = 0, mEndY = 0;
    std::vector<int> mSelected;
    int mLocalPlayerId = 0;
    Resource::Graphics::DataAssetCacheView mDataAssets;

    // Control groups: 10 groups of up to 12 entity IDs each
    std::array<std::vector<int>, kNumControlGroups> mControlGroups;

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
