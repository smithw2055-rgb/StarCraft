#pragma once

#include <algorithm>
#include <limits>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Main/GameLoop/CursorTypes.h"

namespace Game::Client::GameLoop
{

struct ResourceClickFootprint
{
    float HalfW = 32.0f;
    float HalfH = 24.0f;
};

inline ResourceClickFootprint ResourceClickFootprintFor(
    Game::Components::ResourceType Type,
    int ChkUnitID)
{
    if (Type == Game::Components::ResourceType::VespeneGas || IsGeyserUnit(ChkUnitID))
        return {70.0f, 48.0f};

    if (Type == Game::Components::ResourceType::Minerals || IsMineralUnit(ChkUnitID))
        return {50.0f, 34.0f};

    return {32.0f, 24.0f};
}

inline float ResourceClickScore(const Core::Math::Vec2& ResourceCenter,
                                const Core::Math::Vec2& ClickPosition,
                                Game::Components::ResourceType Type,
                                int ChkUnitID)
{
    const ResourceClickFootprint Footprint = ResourceClickFootprintFor(Type, ChkUnitID);
    const float DX = ClickPosition.X - ResourceCenter.X;
    const float DY = ClickPosition.Y - ResourceCenter.Y;
    const float NX = DX / std::max(1.0f, Footprint.HalfW);
    const float NY = DY / std::max(1.0f, Footprint.HalfH);
    const float EllipseScore = NX * NX + NY * NY;
    if (EllipseScore > 1.0f)
        return std::numeric_limits<float>::max();
    return EllipseScore;
}

inline float EntityClickScore(Core::ECS::EntityManager& EM,
                              Core::ECS::EntityID EntityID,
                              const Core::Math::Vec2& ClickPosition)
{
    auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
    auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
    if (!Transform || !Unit || Unit->HitPoints <= 0)
        return std::numeric_limits<float>::max();

    float Left = -16.0f;
    float Top = -16.0f;
    float Right = 16.0f;
    float Bottom = 16.0f;

    if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
        Path && Path->HasCollisionBounds)
    {
        Left = -(float)Path->CollisionLeft;
        Top = -(float)Path->CollisionUp;
        Right = (float)Path->CollisionRight + 1.0f;
        Bottom = (float)Path->CollisionDown + 1.0f;
    }
    else
    {
        switch (Unit->SizeType)
        {
            case Game::Components::UnitSize::Small:
                Left = Top = -14.0f;
                Right = Bottom = 14.0f;
                break;
            case Game::Components::UnitSize::Medium:
                Left = Top = -20.0f;
                Right = Bottom = 20.0f;
                break;
            case Game::Components::UnitSize::Large:
                Left = Top = -28.0f;
                Right = Bottom = 28.0f;
                break;
        }
    }

    const float WorldLeft = Transform->Position.X + Left;
    const float WorldTop = Transform->Position.Y + Top;
    const float WorldRight = Transform->Position.X + Right;
    const float WorldBottom = Transform->Position.Y + Bottom;
    if (ClickPosition.X < WorldLeft || ClickPosition.X > WorldRight ||
        ClickPosition.Y < WorldTop || ClickPosition.Y > WorldBottom)
    {
        return std::numeric_limits<float>::max();
    }

    const float CenterX = (WorldLeft + WorldRight) * 0.5f;
    const float CenterY = (WorldTop + WorldBottom) * 0.5f;
    const float DX = ClickPosition.X - CenterX;
    const float DY = ClickPosition.Y - CenterY;
    const float AreaPenalty = (WorldRight - WorldLeft) * (WorldBottom - WorldTop) * 0.00025f;
    return DX * DX + DY * DY + AreaPenalty;
}

} // namespace Game::Client::GameLoop
