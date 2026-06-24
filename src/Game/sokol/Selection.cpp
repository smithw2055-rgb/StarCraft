#include "Game/sokol/Selection.h"

#include <algorithm>

namespace Game::SokolDemo
{

namespace
{
    constexpr size_t kMaxSelectionSize = 12;

    bool IsShiftHeld()
    {
        return Core::Platform::IsKeyDown(225) || // SAPP_KEYCODE_LSHIFT
               Core::Platform::IsKeyDown(229);   // SAPP_KEYCODE_RSHIFT
    }
}

float Selection::PickRadiusForSize(UnitSize Size) const
{
    switch (Size)
    {
        case UnitSize::Small: return 14.0f;
        case UnitSize::Medium: return 18.0f;
        case UnitSize::Large: return 32.0f;
    }
    return 16.0f;
}

bool Selection::EntityHitTest(const Entity& E, float WX, float WY, float& OutScore) const
{
    float HalfW = PickRadiusForSize(E.Size);
    float HalfH = HalfW;
    if (E.Frames && !E.Frames->empty())
    {
        int FrameIndex = std::clamp(E.FrameIndex, 0, (int)E.Frames->size() - 1);
        const auto& Frame = (*E.Frames)[FrameIndex];
        if (Frame.Valid())
        {
            HalfW = std::max(HalfW, (float)Frame.Width * 0.5f);
            HalfH = std::max(HalfH, (float)Frame.Height * 0.5f);
        }
    }
    else if (E.Kind == EntityKind::Building)
    {
        HalfW = std::max(HalfW, 48.0f);
        HalfH = std::max(HalfH, 40.0f);
    }
    else if (E.Kind == EntityKind::Resource)
    {
        HalfW = std::max(HalfW, 28.0f);
        HalfH = std::max(HalfH, 28.0f);
    }

    if (E.Kind == EntityKind::Building)
    {
        HalfW += 28.0f;
        HalfH += 22.0f;
    }
    else if (E.Kind == EntityKind::Resource)
    {
        HalfW += 18.0f;
        HalfH += 18.0f;
    }
    else
    {
        HalfW += 12.0f;
        HalfH += 12.0f;
    }

    float Left = E.X - HalfW;
    float Right = E.X + HalfW;
    float Top = E.Y - HalfH;
    float Bottom = E.Y + HalfH;
    if (WX < Left || WX > Right || WY < Top || WY > Bottom)
        return false;

    float DX = WX - E.X;
    float DY = WY - E.Y;
    float DistanceScore = DX * DX + DY * DY;
    float AreaPenalty = HalfW * HalfH * 0.0025f;
    float KindBias = (E.Kind == EntityKind::Building) ? 12.0f : 0.0f;
    OutScore = DistanceScore + AreaPenalty + KindBias;
    return true;
}

bool Selection::EntityInRect(const Entity& E, float RX, float RY, float RW, float RH) const
{
    float HalfW = PickRadiusForSize(E.Size);
    float HalfH = HalfW;
    if (E.Frames && !E.Frames->empty())
    {
        int FrameIndex = std::clamp(E.FrameIndex, 0, (int)E.Frames->size() - 1);
        const auto& Frame = (*E.Frames)[FrameIndex];
        if (Frame.Valid())
        {
            HalfW = std::max(HalfW, (float)Frame.Width * 0.5f);
            HalfH = std::max(HalfH, (float)Frame.Height * 0.5f);
        }
    }

    float Left = E.X - HalfW;
    float Top = E.Y - HalfH;
    float Right = E.X + HalfW;
    float Bottom = E.Y + HalfH;
    if (Right < RX) return false;
    if (Left > RX + RW) return false;
    if (Bottom < RY) return false;
    if (Top > RY + RH) return false;
    return true;
}

bool Selection::IsMultiSelectable(const Entity& E) const
{
    if (E.Kind != EntityKind::Unit)
        return false;
    if (E.ChkUnitId == 103 || E.ChkUnitId == 188)
        return false;
    return true;
}

bool Selection::IsOwnedByLocalPlayer(const Entity& E) const
{
    return E.PlayerId == mLocalPlayerId;
}

bool Selection::IsVisibleOnCurrentScreen(const Entity& E, const Render::Camera2D& Camera) const
{
    float SX = 0.0f;
    float SY = 0.0f;
    Camera.WorldToScreen(E.X, E.Y, SX, SY);
    return SX >= -64.0f &&
           SX <= (float)Camera.GetViewportWidth() + 64.0f &&
           SY >= -64.0f &&
           SY <= (float)Camera.GetViewportHeight() + 64.0f;
}

bool Selection::CanAppendToSelection(const EntityManager& Entities,
                                     const std::vector<int>& Selection,
                                     const Entity& Candidate) const
{
    if (Selection.size() >= kMaxSelectionSize)
        return false;
    if (!IsMultiSelectable(Candidate))
        return Selection.empty();
    if (Selection.empty())
        return true;

    const Entity& First = Entities.GetEntity(Selection.front());
    if (!IsMultiSelectable(First))
        return false;
    return First.PlayerId == Candidate.PlayerId;
}

void Selection::AppendSelectionEntity(const EntityManager& Entities,
                                      std::vector<int>& Selection,
                                      int EntityId) const
{
    if (EntityId <= 0)
        return;
    if (std::find(Selection.begin(), Selection.end(), EntityId) != Selection.end())
        return;

    const Entity& Candidate = Entities.GetEntity(EntityId);
    if (!CanAppendToSelection(Entities, Selection, Candidate))
        return;

    Selection.push_back(EntityId);
}

void Selection::GetDragRectWorld(float& X, float& Y, float& W, float& H) const
{
    int MinX = std::min(mStartX, mEndX);
    int MaxX = std::max(mStartX, mEndX);
    int MinY = std::min(mStartY, mEndY);
    int MaxY = std::max(mStartY, mEndY);
    X = (float)MinX; Y = (float)MinY; W = (float)(MaxX - MinX); H = (float)(MaxY - MinY);
}

void Selection::GetDragRectScreen(float& X, float& Y, float& W, float& H) const
{
    X = (float)std::min(mStartX, mEndX);
    Y = (float)std::min(mStartY, mEndY);
    W = (float)std::abs(mEndX - mStartX);
    H = (float)std::abs(mEndY - mStartY);
}

int Selection::Count() const { return (int)mSelected.size(); }
int Selection::Get(int Index) const
{
    if (Index < 0 || Index >= (int)mSelected.size()) return -1;
    return mSelected[Index];
}

void Selection::PruneAgainst(const EntityManager& Entities)
{
    std::vector<int> Pruned;
    Pruned.reserve(mSelected.size());
    for (int Id : mSelected)
    {
        bool Keep = false;
        for (const auto& Entity : Entities.GetAll())
        {
            if (Entity.Id == Id && Entity.Visible)
            {
                Keep = true;
                break;
            }
        }
        if (Keep)
            Pruned.push_back(Id);
    }
    mSelected = std::move(Pruned);
}

void Selection::CancelPointerGesture()
{
    mPhase = Phase::Idle;
}

void Selection::Update(const EntityManager& Entities,
                       const Render::Camera2D& Camera)
{
    PruneAgainst(Entities);

    int MX = (int)Core::Platform::GetMouseX();
    int MY = (int)Core::Platform::GetMouseY();

    // Double-click detection
    static double sLastClickTime = 0.0;
    static int sLastClickId = -1;
    static int sLastClickX = 0;
    static int sLastClickY = 0;
    double Now = Core::Platform::GetAbsoluteTime();

    if (Core::Platform::IsMouseButtonPressed(0))
    {
        mPhase = Phase::Dragging;
        mStartX = MX; mStartY = MY;
        mEndX = MX; mEndY = MY;
    }

    if (Core::Platform::IsMouseButtonDown(0) && mPhase == Phase::Dragging)
    {
        mEndX = MX; mEndY = MY;
    }

    if (Core::Platform::IsMouseButtonReleased(0) && mPhase == Phase::Dragging)
    {
        mEndX = MX; mEndY = MY;
        mPhase = Phase::Idle;

        // Determine drag rect in world space
        float MinSX = (float)std::min(mStartX, mEndX);
        float MinSY = (float)std::min(mStartY, mEndY);
        float MaxSX = (float)std::max(mStartX, mEndX);
        float MaxSY = (float)std::max(mStartY, mEndY);

        if (MaxSX - MinSX < 6.0f && MaxSY - MinSY < 6.0f)
        {
            // Single click: only select entities actually hit by the pointer.
            double ClickDelta = Now - sLastClickTime;
            sLastClickTime = Now;

            int Best = -1;
            float BestScore = 1e9f;
            float WX, WY;
            Camera.ScreenToWorld((float)MX, (float)MY, WX, WY);
            for (const auto& E : Entities.GetAll())
            {
                if (!E.Visible) continue;
                float Score = 0.0f;
                if (!EntityHitTest(E, WX, WY, Score))
                    continue;
                if (Score < BestScore)
                {
                    BestScore = Score;
                    Best = E.Id;
                }
            }
            if (Best != -1)
            {
                // Double-click: select all of same type
                int ClickDx = MX - sLastClickX;
                int ClickDy = MY - sLastClickY;
                bool DoubleClick =
                    (ClickDelta < 0.3 &&
                     sLastClickId == Best &&
                     (ClickDx * ClickDx + ClickDy * ClickDy) <= 64);
                if (DoubleClick)
                {
                    const auto& Clicked = Entities.GetEntity(Best);
                    std::vector<int> SameType;
                    for (const auto& E : Entities.GetAll())
                    {
                        if (!E.Visible) continue;
                        if (E.PlayerId != Clicked.PlayerId) continue;
                        if (E.ChkUnitId != Clicked.ChkUnitId) continue;
                        if (!IsMultiSelectable(E)) continue;
                        if (!IsVisibleOnCurrentScreen(E, Camera)) continue;
                        AppendSelectionEntity(Entities, SameType, E.Id);
                    }
                    if (!SameType.empty())
                        mSelected = std::move(SameType);
                    else
                        mSelected = {Best};
                }
                else
                {
                    if (IsShiftHeld())
                    {
                        auto Sel = mSelected;
                        AppendSelectionEntity(Entities, Sel, Best);
                        mSelected = std::move(Sel);
                    }
                    else
                    {
                        mSelected = {Best};
                    }
                }
                sLastClickId = Best;
                sLastClickX = MX;
                sLastClickY = MY;
            }
            else
            {
                if (!IsShiftHeld())
                    mSelected.clear();
                sLastClickId = -1;
                sLastClickX = MX;
                sLastClickY = MY;
            }
        }
        else
        {
            float WX0, WY0, WX1, WY1;
            Camera.ScreenToWorld(MinSX, MinSY, WX0, WY0);
            Camera.ScreenToWorld(MaxSX, MaxSY, WX1, WY1);
            float RX = std::min(WX0, WX1);
            float RY = std::min(WY0, WY1);
            float RW = std::abs(WX1 - WX0);
            float RH = std::abs(WY1 - WY0);

            std::vector<int> PickedUnits;
            std::vector<int> PickedSingles;
            for (const auto& E : Entities.GetAll())
            {
                if (!E.Visible) continue;
                if (!EntityInRect(E, RX, RY, RW, RH)) continue;
                if (!IsOwnedByLocalPlayer(E)) continue;

                if (IsMultiSelectable(E))
                    AppendSelectionEntity(Entities, PickedUnits, E.Id);
                else
                    PickedSingles.push_back(E.Id);
            }

            if (!PickedUnits.empty())
            {
                if (IsShiftHeld())
                {
                    auto Sel = mSelected;
                    for (int Id : PickedUnits)
                        AppendSelectionEntity(Entities, Sel, Id);
                    mSelected = std::move(Sel);
                }
                else
                {
                    mSelected = std::move(PickedUnits);
                }
            }
            else if (!PickedSingles.empty())
            {
                mSelected = {PickedSingles.front()};
            }
            else if (!IsShiftHeld())
            {
                mSelected.clear();
            }
        }
    }
}

} // namespace Game::SokolDemo
