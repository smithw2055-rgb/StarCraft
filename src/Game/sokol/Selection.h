#pragma once

#include "Core/Platform/Platform.h"
#include "Game/sokol/Entity.h"
#include "Render/sokol/Camera2D.h"

#include <vector>

namespace Game::SokolDemo
{

class Selection
{
public:
    enum class Phase
    {
        Idle,
        Dragging
    };

    void Update(const EntityManager& Entities,
                const Render::Camera2D& Camera);
    void PruneAgainst(const EntityManager& Entities);
    void CancelPointerGesture();
    void SetLocalPlayerId(int PlayerId) { mLocalPlayerId = PlayerId; }

    bool IsDragging() const { return mPhase == Phase::Dragging; }
    void GetDragRectWorld(float& X, float& Y, float& W, float& H) const;
    void GetDragRectScreen(float& X, float& Y, float& W, float& H) const;

    int Count() const;
    int Get(int Index) const;
    const std::vector<int>& GetSelected() const { return mSelected; }

private:
    Phase mPhase = Phase::Idle;
    int mStartX = 0, mStartY = 0;
    int mEndX = 0, mEndY = 0;
    std::vector<int> mSelected;
    int mLocalPlayerId = 0;

    float PickRadiusForSize(UnitSize Size) const;
    bool EntityHitTest(const Entity& E, float WX, float WY, float& OutScore) const;
    bool EntityInRect(const Entity& E, float RX, float RY, float RW, float RH) const;
    bool IsMultiSelectable(const Entity& E) const;
    bool IsOwnedByLocalPlayer(const Entity& E) const;
    bool IsVisibleOnCurrentScreen(const Entity& E, const Render::Camera2D& Camera) const;
    bool CanAppendToSelection(const EntityManager& Entities,
                              const std::vector<int>& Selection,
                              const Entity& Candidate) const;
    void AppendSelectionEntity(const EntityManager& Entities,
                               std::vector<int>& Selection,
                               int EntityId) const;
};

} // namespace Game::SokolDemo
