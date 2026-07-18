#pragma once

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"

namespace Game::Systems::BurrowState
{

inline void SetVisualBurrowed(Core::ECS::EntityManager& EM,
                              Core::ECS::EntityID EntityID,
                              bool IsBurrowed)
{
    using namespace Game::Components;

    if (auto* Render = EM.GetComponent<RenderComponent>(EntityID))
        Render->Visible = !IsBurrowed;

    auto* Tree = EM.GetComponent<ImageTreeComponent>(EntityID);
    if (!Tree)
        return;

    if (IsBurrowed)
    {
        // OpenBW leaves images.dat always_visible layers alone, but hides the
        // ordinary image tree (including shadows) when sigorder 4 completes.
        // Remember only durable layers so a later UnBurrow cannot resurrect
        // expired dust, blood, damage, or attack overlays.
        for (auto& Node : Tree->Nodes)
        {
            if (Node.AlwaysVisible)
                continue;
            if (Node.Visible && Node.PersistentSpriteAttachment)
                Node.HiddenByBurrow = true;
            Node.Visible = false;
        }
        return;
    }

    // BeginTransition(Unburrow) exposes the authored root animation first.
    // Restore persistent attachments only after sigorder 4 commits Surface.
    const auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID);
    if (Burrow && (Burrow->IsBurrowed ||
                   Burrow->Phase != BurrowPhase::Surface))
    {
        return;
    }
    for (auto& Node : Tree->Nodes)
    {
        if (!Node.HiddenByBurrow)
            continue;
        Node.Visible = true;
        Node.HiddenByBurrow = false;
    }
}

inline bool CompleteTransition(Core::ECS::EntityManager& EM,
                               Core::ECS::EntityID EntityID)
{
    using namespace Game::Components;

    auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID);
    if (!Burrow || !Burrow->IsTransitioning())
        return false;

    const bool ToBurrowed = Burrow->Phase == BurrowPhase::Burrowing;
    Burrow->Phase = ToBurrowed ? BurrowPhase::Burrowed : BurrowPhase::Surface;
    Burrow->IsBurrowed = ToBurrowed;
    Burrow->BurrowTransitionTime = 0.0f;

    auto* Cloak = EM.GetComponent<CloakComponent>(EntityID);
    if (ToBurrowed)
    {
        if (!Cloak)
            Cloak = &EM.AddComponent<CloakComponent>(EntityID);
        Cloak->IsBurrowed = true;
        Cloak->IsCloaked = true;
    }
    else if (Cloak)
    {
        Cloak->IsBurrowed = false;
        if (!Cloak->IsPassiveCloak)
            Cloak->IsCloaked = false;
    }
    SetVisualBurrowed(EM, EntityID, ToBurrowed);
    return true;
}

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
    if (!ToBurrowed)
        SetVisualBurrowed(EM, EntityID, false);
    return true;
}

} // namespace Game::Systems::BurrowState
