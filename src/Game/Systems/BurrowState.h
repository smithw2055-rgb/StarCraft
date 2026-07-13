#pragma once

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"

namespace Game::Systems::BurrowState
{

inline bool BeginTransition(Core::ECS::EntityManager& EM,
                            Core::ECS::EntityID EntityID,
                            bool ToBurrowed)
{
    using namespace Game::Components;

    auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID);
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
    if (!Burrow || !Unit || !Burrow->CanBurrow || Unit->HitPoints <= 0 ||
        Burrow->IsTransitioning())
    {
        return false;
    }

    const bool FullyBurrowed =
        Burrow->IsBurrowed || Burrow->Phase == BurrowPhase::Burrowed;
    if (ToBurrowed == FullyBurrowed)
        return false;

    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
    {
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {};
    }
    if (auto* Command = EM.GetComponent<CommandComponent>(EntityID))
    {
        Command->CurrentCommand = CommandType::Stop;
        Command->TargetEntityID = -1;
    }
    if (auto* Combat = EM.GetComponent<CombatComponent>(EntityID))
    {
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackTargetEntityID = -1;
        Combat->TargetEntityID = -1;
        Combat->AutoTargetEntityID = -1;
    }

    Burrow->Phase = ToBurrowed
        ? BurrowPhase::Burrowing
        : BurrowPhase::Unburrowing;
    Burrow->BurrowTransitionTime = 2.0f;
    return true;
}

} // namespace Game::Systems::BurrowState
