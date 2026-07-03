#include "Client/Selection.h"

#include <algorithm>
#include <cmath>

namespace Game::Client
{

namespace
{
    constexpr size_t kMaxSelectionSize = 12;

    bool IsShiftHeld()
    {
        return Core::Platform::IsKeyDown(225) ||
               Core::Platform::IsKeyDown(229);
    }

    bool IsVisible(Core::ECS::EntityManager& Entities, int EntityId)
    {
        if (!Entities.IsAlive(EntityId))
            return false;
        auto* Render = Entities.GetComponent<Game::Components::RenderComponent>(EntityId);
        auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
        return Render && Render->Visible && Trans;
    }

    bool IsResource(Core::ECS::EntityManager& Entities, int EntityId)
    {
        return Entities.GetComponent<Game::Components::ResourceComponent>(EntityId) != nullptr;
    }

    bool IsBuilding(Core::ECS::EntityManager& Entities, int EntityId)
    {
        return Entities.GetComponent<Game::Components::BuildingComponent>(EntityId) != nullptr;
    }

    bool IsEffect(Core::ECS::EntityManager& Entities, int EntityId)
    {
        return Entities.GetComponent<Game::Components::EffectComponent>(EntityId) != nullptr;
    }
}

float Selection::PickRadiusForSize(Game::Components::UnitSize Size) const
{
    switch (Size)
    {
        case Game::Components::UnitSize::Small: return 14.0f;
        case Game::Components::UnitSize::Medium: return 18.0f;
        case Game::Components::UnitSize::Large: return 32.0f;
    }
    return 16.0f;
}

bool Selection::EntityHitTest(Core::ECS::EntityManager& Entities, int EntityId,
                              float WX, float WY, float& OutScore) const
{
    if (IsEffect(Entities, EntityId) || !IsVisible(Entities, EntityId))
        return false;

    auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
    auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
    if (!Trans || !Unit)
        return false;

    float HalfW = PickRadiusForSize(Unit->SizeType);
    float HalfH = HalfW;
    if (IsBuilding(Entities, EntityId))
    {
        HalfW = std::max(HalfW, 64.0f);
        HalfH = std::max(HalfH, 48.0f);
    }
    else if (IsResource(Entities, EntityId))
    {
        HalfW = std::max(HalfW, 36.0f);
        HalfH = std::max(HalfH, 30.0f);
    }

    if (IsBuilding(Entities, EntityId))
    {
        HalfW += 28.0f;
        HalfH += 22.0f;
    }
    else if (IsResource(Entities, EntityId))
    {
        HalfW += 18.0f;
        HalfH += 18.0f;
    }
    else
    {
        HalfW += 12.0f;
        HalfH += 12.0f;
    }

    const float Left = Trans->Position.X - HalfW;
    const float Right = Trans->Position.X + HalfW;
    const float Top = Trans->Position.Y - HalfH;
    const float Bottom = Trans->Position.Y + HalfH;
    if (WX < Left || WX > Right || WY < Top || WY > Bottom)
        return false;

    const float DX = WX - Trans->Position.X;
    const float DY = WY - Trans->Position.Y;
    const float AreaPenalty = HalfW * HalfH * 0.0025f;
    const float KindBias = IsBuilding(Entities, EntityId) ? 12.0f : 0.0f;
    OutScore = DX * DX + DY * DY + AreaPenalty + KindBias;
    return true;
}

bool Selection::EntityInRect(Core::ECS::EntityManager& Entities, int EntityId,
                             float RX, float RY, float RW, float RH) const
{
    if (IsEffect(Entities, EntityId) || !IsVisible(Entities, EntityId))
        return false;

    auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
    auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
    if (!Trans || !Unit)
        return false;

    float HalfW = PickRadiusForSize(Unit->SizeType);
    float HalfH = HalfW;
    if (IsBuilding(Entities, EntityId))
    {
        HalfW = std::max(HalfW, 64.0f);
        HalfH = std::max(HalfH, 48.0f);
    }
    else if (IsResource(Entities, EntityId))
    {
        HalfW = std::max(HalfW, 36.0f);
        HalfH = std::max(HalfH, 30.0f);
    }

    const float Left = Trans->Position.X - HalfW;
    const float Top = Trans->Position.Y - HalfH;
    const float Right = Trans->Position.X + HalfW;
    const float Bottom = Trans->Position.Y + HalfH;
    return !(Right < RX || Left > RX + RW || Bottom < RY || Top > RY + RH);
}

bool Selection::IsMultiSelectable(Core::ECS::EntityManager& Entities, int EntityId) const
{
    if (!IsVisible(Entities, EntityId) || IsBuilding(Entities, EntityId) || IsResource(Entities, EntityId))
        return false;
    auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
    if (!Unit)
        return false;
    return Unit->ChkUnitID != 103 && Unit->ChkUnitID != 188;
}

bool Selection::IsOwnedByLocalPlayer(Core::ECS::EntityManager& Entities, int EntityId) const
{
    auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityId);
    return Player && Player->PlayerID == mLocalPlayerId;
}

bool Selection::IsVisibleOnCurrentScreen(Core::ECS::EntityManager& Entities, int EntityId,
                                         const Render::Camera2D& Camera) const
{
    auto* Trans = Entities.GetComponent<Game::Components::TransformComponent>(EntityId);
    if (!Trans)
        return false;

    float SX = 0.0f;
    float SY = 0.0f;
    Camera.WorldToScreen(Trans->Position.X, Trans->Position.Y, SX, SY);
    return SX >= -64.0f &&
           SX <= (float)Camera.GetViewportWidth() + 64.0f &&
           SY >= -64.0f &&
           SY <= (float)Camera.GetViewportHeight() + 64.0f;
}

bool Selection::CanAppendToSelection(Core::ECS::EntityManager& Entities,
                                     const std::vector<int>& Selection,
                                     int CandidateId) const
{
    if (Selection.size() >= kMaxSelectionSize)
        return false;
    if (!IsMultiSelectable(Entities, CandidateId))
        return Selection.empty();
    if (Selection.empty())
        return true;
    if (!IsMultiSelectable(Entities, Selection.front()))
        return false;

    auto* FirstPlayer = Entities.GetComponent<Game::Components::PlayerComponent>(Selection.front());
    auto* CandidatePlayer = Entities.GetComponent<Game::Components::PlayerComponent>(CandidateId);
    return FirstPlayer && CandidatePlayer && FirstPlayer->PlayerID == CandidatePlayer->PlayerID;
}

void Selection::AppendSelectionEntity(Core::ECS::EntityManager& Entities,
                                      std::vector<int>& Selection,
                                      int EntityId) const
{
    if (EntityId <= 0 || std::find(Selection.begin(), Selection.end(), EntityId) != Selection.end())
        return;
    if (!CanAppendToSelection(Entities, Selection, EntityId))
        return;
    Selection.push_back(EntityId);
}

void Selection::GetDragRectWorld(float& X, float& Y, float& W, float& H) const
{
    const int MinX = std::min(mStartX, mEndX);
    const int MaxX = std::max(mStartX, mEndX);
    const int MinY = std::min(mStartY, mEndY);
    const int MaxY = std::max(mStartY, mEndY);
    X = (float)MinX;
    Y = (float)MinY;
    W = (float)(MaxX - MinX);
    H = (float)(MaxY - MinY);
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
    if (Index < 0 || Index >= (int)mSelected.size())
        return -1;
    return mSelected[Index];
}

void Selection::PruneAgainst(Core::ECS::EntityManager& Entities)
{
    std::vector<int> Pruned;
    Pruned.reserve(mSelected.size());
    for (int Id : mSelected)
    {
        if (IsVisible(Entities, Id))
            Pruned.push_back(Id);
    }
    mSelected = std::move(Pruned);
}

void Selection::CancelPointerGesture()
{
    mPhase = Phase::Idle;
}

void Selection::Update(Core::ECS::EntityManager& Entities,
                       const Render::Camera2D& Camera)
{
    PruneAgainst(Entities);

    const int MX = (int)Core::Platform::GetMouseX();
    const int MY = (int)Core::Platform::GetMouseY();

    static double sLastClickTime = 0.0;
    static int sLastClickId = -1;
    static int sLastClickX = 0;
    static int sLastClickY = 0;
    const double Now = Core::Platform::GetAbsoluteTime();

    if (Core::Platform::IsMouseButtonPressed(0))
    {
        mPhase = Phase::Dragging;
        mStartX = MX;
        mStartY = MY;
        mEndX = MX;
        mEndY = MY;
    }

    if (Core::Platform::IsMouseButtonDown(0) && mPhase == Phase::Dragging)
    {
        mEndX = MX;
        mEndY = MY;
    }

    if (!Core::Platform::IsMouseButtonReleased(0) || mPhase != Phase::Dragging)
        return;

    mEndX = MX;
    mEndY = MY;
    mPhase = Phase::Idle;

    const float MinSX = (float)std::min(mStartX, mEndX);
    const float MinSY = (float)std::min(mStartY, mEndY);
    const float MaxSX = (float)std::max(mStartX, mEndX);
    const float MaxSY = (float)std::max(mStartY, mEndY);

    if (MaxSX - MinSX < 6.0f && MaxSY - MinSY < 6.0f)
    {
        const double ClickDelta = Now - sLastClickTime;
        sLastClickTime = Now;

        int Best = -1;
        float BestScore = 1e9f;
        float WX = 0.0f;
        float WY = 0.0f;
        Camera.ScreenToWorld((float)MX, (float)MY, WX, WY);
        for (int EntityId : Entities.GetEntitiesWith<Game::Components::TransformComponent,
                                                     Game::Components::RenderComponent,
                                                     Game::Components::UnitComponent>())
        {
            float Score = 0.0f;
            if (!EntityHitTest(Entities, EntityId, WX, WY, Score))
                continue;
            if (Score < BestScore)
            {
                BestScore = Score;
                Best = EntityId;
            }
        }

        if (Best != -1)
        {
            const int ClickDx = MX - sLastClickX;
            const int ClickDy = MY - sLastClickY;
            const bool DoubleClick =
                ClickDelta < 0.3 &&
                sLastClickId == Best &&
                (ClickDx * ClickDx + ClickDy * ClickDy) <= 64;

            if (DoubleClick)
            {
                auto* ClickedUnit = Entities.GetComponent<Game::Components::UnitComponent>(Best);
                auto* ClickedPlayer = Entities.GetComponent<Game::Components::PlayerComponent>(Best);
                std::vector<int> SameType;
                if (ClickedUnit && ClickedPlayer)
                {
                    for (int EntityId : Entities.GetEntitiesWith<Game::Components::TransformComponent,
                                                                 Game::Components::RenderComponent,
                                                                 Game::Components::UnitComponent>())
                    {
                        auto* Unit = Entities.GetComponent<Game::Components::UnitComponent>(EntityId);
                        auto* Player = Entities.GetComponent<Game::Components::PlayerComponent>(EntityId);
                        if (!Unit || !Player)
                            continue;
                        if (Player->PlayerID != ClickedPlayer->PlayerID)
                            continue;
                        if (Unit->ChkUnitID != ClickedUnit->ChkUnitID)
                            continue;
                        if (!IsMultiSelectable(Entities, EntityId))
                            continue;
                        if (!IsVisibleOnCurrentScreen(Entities, EntityId, Camera))
                            continue;
                        AppendSelectionEntity(Entities, SameType, EntityId);
                    }
                }
                mSelected = SameType.empty() ? std::vector<int>{Best} : std::move(SameType);
            }
            else if (IsShiftHeld())
            {
                auto Sel = mSelected;
                AppendSelectionEntity(Entities, Sel, Best);
                mSelected = std::move(Sel);
            }
            else
            {
                mSelected = {Best};
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
        return;
    }

    float WX0 = 0.0f, WY0 = 0.0f, WX1 = 0.0f, WY1 = 0.0f;
    Camera.ScreenToWorld(MinSX, MinSY, WX0, WY0);
    Camera.ScreenToWorld(MaxSX, MaxSY, WX1, WY1);
    const float RX = std::min(WX0, WX1);
    const float RY = std::min(WY0, WY1);
    const float RW = std::abs(WX1 - WX0);
    const float RH = std::abs(WY1 - WY0);

    std::vector<int> PickedUnits;
    std::vector<int> PickedSingles;
    for (int EntityId : Entities.GetEntitiesWith<Game::Components::TransformComponent,
                                                 Game::Components::RenderComponent,
                                                 Game::Components::UnitComponent>())
    {
        if (!EntityInRect(Entities, EntityId, RX, RY, RW, RH))
            continue;
        if (!IsOwnedByLocalPlayer(Entities, EntityId))
            continue;

        if (IsMultiSelectable(Entities, EntityId))
            AppendSelectionEntity(Entities, PickedUnits, EntityId);
        else
            PickedSingles.push_back(EntityId);
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

void Selection::SaveControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities)
{
    if (GroupIndex < 0 || GroupIndex >= kNumControlGroups)
        return;
    auto& Group = mControlGroups[GroupIndex];
    Group.clear();
    for (int EntityId : mSelected)
    {
        if (IsVisible(Entities, EntityId) && IsOwnedByLocalPlayer(Entities, EntityId))
            Group.push_back(EntityId);
        if ((int)Group.size() >= kMaxSelection)
            break;
    }
}

void Selection::RecallControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities)
{
    if (GroupIndex < 0 || GroupIndex >= kNumControlGroups)
        return;
    const auto& Group = mControlGroups[GroupIndex];
    if (Group.empty())
        return;

    mSelected.clear();
    for (int EntityId : Group)
    {
        if (IsMultiSelectable(Entities, EntityId) &&
            IsOwnedByLocalPlayer(Entities, EntityId) &&
            IsVisible(Entities, EntityId))
        {
            AppendSelectionEntity(Entities, mSelected, EntityId);
        }
        if ((int)mSelected.size() >= kMaxSelection)
            break;
    }
}

void Selection::AppendControlGroup(int GroupIndex, Core::ECS::EntityManager& Entities)
{
    if (GroupIndex < 0 || GroupIndex >= kNumControlGroups)
        return;
    auto& Group = mControlGroups[GroupIndex];
    for (int EntityId : mSelected)
    {
        if (!IsVisible(Entities, EntityId) ||
            !IsOwnedByLocalPlayer(Entities, EntityId) ||
            !IsMultiSelectable(Entities, EntityId))
        {
            continue;
        }

        if (std::find(Group.begin(), Group.end(), EntityId) != Group.end())
            continue;

        Group.push_back(EntityId);
        if ((int)Group.size() >= kMaxSelection)
            break;
    }
}

bool Selection::IsControlGroupEmpty(int GroupIndex) const
{
    if (GroupIndex < 0 || GroupIndex >= kNumControlGroups)
        return true;
    return mControlGroups[GroupIndex].empty();
}

} // namespace Game::Client
