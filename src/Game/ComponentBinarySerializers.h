#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"

namespace Core::ECS
{

template <>
struct CustomComponentBinarySerializer<Game::Components::CommandComponent>
{
    static constexpr bool kSupported = true;

    static void Serialize(const Game::Components::CommandComponent& Cmd, std::vector<uint8_t>& Buf)
    {
        Detail::AppendVectorPodBytes(Buf, Cmd.CommandQueue);
        Detail::AppendPodBytes(Buf, Cmd.CurrentCommand);
        Detail::AppendPodBytes(Buf, Cmd.TargetPosition);
        Detail::AppendPodBytes(Buf, Cmd.TargetEntityID);
        Detail::AppendPodBytes(Buf, Cmd.PatrolAnchor);
        Detail::AppendPodBytes(Buf, Cmd.HasPatrolAnchor);
        Detail::AppendVectorPodBytes(Buf, Cmd.Orders);
        Detail::AppendPodBytes(Buf, Cmd.CurrentOrderIndex);
        Detail::AppendPodBytes(Buf, Cmd.RallyPoint);
        Detail::AppendPodBytes(Buf, Cmd.HasRallyPoint);
        Detail::AppendPodBytes(Buf, Cmd.RallyDisplayTimer);
    }

    static bool Deserialize(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::CommandComponent& Cmd)
    {
        return Detail::ReadVectorPodBytes(Buf, Cursor, Cmd.CommandQueue) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.CurrentCommand) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.TargetPosition) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.TargetEntityID) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.PatrolAnchor) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.HasPatrolAnchor) &&
               Detail::ReadVectorPodBytes(Buf, Cursor, Cmd.Orders) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.CurrentOrderIndex) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.RallyPoint) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.HasRallyPoint) &&
               Detail::ReadPodBytes(Buf, Cursor, Cmd.RallyDisplayTimer);
    }
};

template <>
struct CustomComponentBinarySerializer<Game::Components::PathfindingComponent>
{
    static constexpr bool kSupported = true;

    static void Serialize(const Game::Components::PathfindingComponent& Path, std::vector<uint8_t>& Buf)
    {
        Detail::AppendVectorPodBytes(Buf, Path.Path);
        Detail::AppendPodBytes(Buf, Path.PathIndex);
        Detail::AppendPodBytes(Buf, Path.NodeTolerance);
        Detail::AppendPodBytes(Buf, Path.RequestedDestination);
        Detail::AppendPodBytes(Buf, Path.RequestedTargetEntityID);
        Detail::AppendPodBytes(Buf, Path.RequestedIgnoreEntityID);
        Detail::AppendPodBytes(Buf, Path.RequestedAdditionalIgnoreEntityID);
        Detail::AppendPodBytes(Buf, Path.RepathFailCount);
        Detail::AppendPodBytes(Buf, Path.LastRepathTime);
        Detail::AppendPodBytes(Buf, Path.BlockedByEntityID);
        Detail::AppendPodBytes(Buf, Path.LastPathStatus);
        Detail::AppendPodBytes(Buf, Path.LastPathEndReason);
        Detail::AppendPodBytes(Buf, Path.ResolvedPathDestination);
        Detail::AppendPodBytes(Buf, Path.NavigationVersion);
        Detail::AppendPodBytes(Buf, Path.LastPathExpandedCells);
        Detail::AppendPodBytes(Buf, Path.CurrentVelocity);
        Detail::AppendPodBytes(Buf, Path.FixedPosition);
        Detail::AppendPodBytes(Buf, Path.FixedVelocity);
        Detail::AppendPodBytes(Buf, Path.HasFixedPosition);
        Detail::AppendPodBytes(Buf, Path.CurrentSpeed);
        Detail::AppendPodBytes(Buf, Path.TargetSpeed);
        Detail::AppendPodBytes(Buf, Path.Acceleration);
        Detail::AppendPodBytes(Buf, Path.TurnRate);
        Detail::AppendPodBytes(Buf, Path.FacingAngle);
        Detail::AppendPodBytes(Buf, Path.FriendlyYieldVector);
        Detail::AppendPodBytes(Buf, Path.FriendlyYieldAnchor);
        Detail::AppendPodBytes(Buf, Path.HasFriendlyYieldAnchor);
        Detail::AppendPodBytes(Buf, Path.IsAir);
        Detail::AppendPodBytes(Buf, Path.CollisionRadius);
        Detail::AppendPodBytes(Buf, Path.HasCollisionBounds);
        Detail::AppendPodBytes(Buf, Path.CollisionLeft);
        Detail::AppendPodBytes(Buf, Path.CollisionUp);
        Detail::AppendPodBytes(Buf, Path.CollisionRight);
        Detail::AppendPodBytes(Buf, Path.CollisionDown);
        Detail::AppendPodBytes(Buf, Path.HasRecentMoveFailure);
        Detail::AppendPodBytes(Buf, Path.RecentFailedMoveTarget);
        Detail::AppendPodBytes(Buf, Path.RecentMoveFailureCooldown);
        Detail::AppendPodBytes(Buf, Path.StallTimer);
        Detail::AppendPodBytes(Buf, Path.LastWaypointDistance);
        Detail::AppendPodBytes(Buf, Path.HasLastWaypointDistance);
        Detail::AppendPodBytes(Buf, Path.DestinationStallTimer);
        Detail::AppendPodBytes(Buf, Path.LastDestinationDistance);
        Detail::AppendPodBytes(Buf, Path.HasLastDestinationDistance);
        Detail::AppendPodBytes(Buf, Path.StallTicks);
        Detail::AppendPodBytes(Buf, Path.DestinationStallTicks);
        Detail::AppendPodBytes(Buf, Path.RecentMoveFailureTicks);
        Detail::AppendPodBytes(Buf, Path.ConsecutiveMoveFailures);
        Detail::AppendPodBytes(Buf, Path.RecentMoveFailureNavigationVersion);
    }

    static bool Deserialize(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::PathfindingComponent& Path)
    {
        return Detail::ReadVectorPodBytes(Buf, Cursor, Path.Path) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.PathIndex) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.NodeTolerance) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RequestedDestination) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RequestedTargetEntityID) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RequestedIgnoreEntityID) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RequestedAdditionalIgnoreEntityID) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RepathFailCount) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.LastRepathTime) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.BlockedByEntityID) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.LastPathStatus) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.LastPathEndReason) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.ResolvedPathDestination) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.NavigationVersion) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.LastPathExpandedCells) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.CurrentVelocity) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.FixedPosition) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.FixedVelocity) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.HasFixedPosition) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.CurrentSpeed) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.TargetSpeed) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.Acceleration) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.TurnRate) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.FacingAngle) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.FriendlyYieldVector) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.FriendlyYieldAnchor) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.HasFriendlyYieldAnchor) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.IsAir) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.CollisionRadius) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.HasCollisionBounds) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.CollisionLeft) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.CollisionUp) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.CollisionRight) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.CollisionDown) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.HasRecentMoveFailure) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RecentFailedMoveTarget) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RecentMoveFailureCooldown) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.StallTimer) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.LastWaypointDistance) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.HasLastWaypointDistance) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.DestinationStallTimer) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.LastDestinationDistance) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.HasLastDestinationDistance) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.StallTicks) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.DestinationStallTicks) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RecentMoveFailureTicks) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.ConsecutiveMoveFailures) &&
               Detail::ReadPodBytes(Buf, Cursor, Path.RecentMoveFailureNavigationVersion);
    }
};

template <>
struct CustomComponentBinarySerializer<Game::Components::BuildingComponent>
{
    static constexpr bool kSupported = true;

    static void Serialize(const Game::Components::BuildingComponent& Bldg, std::vector<uint8_t>& Buf)
    {
        Detail::AppendPodBytes(Buf, Bldg.IsBuilding);
        Detail::AppendPodBytes(Buf, Bldg.BuildProgress);
        Detail::AppendPodBytes(Buf, Bldg.TotalBuildTime);
        Detail::AppendPodBytes(Buf, Bldg.ConstructionHitPointAccumulator);
        Detail::AppendPodBytes(Buf, Bldg.ConstructionShieldAccumulator);
        Detail::AppendPodBytes(Buf, Bldg.IsAddon);
        Detail::AppendPodBytes(Buf, Bldg.CanLiftOff);
        Detail::AppendPodBytes(Buf, Bldg.IsLifted);
        Detail::AppendPodBytes(Buf, Bldg.FlightPhase);
        Detail::AppendPodBytes(Buf, Bldg.FlightAnimationComplete);
        Detail::AppendPodBytes(Buf, Bldg.FlightHeightTransitionActive);
        Detail::AppendPodBytes(Buf, Bldg.FlightHeightTransitionElapsed);
        Detail::AppendPodBytes(Buf, Bldg.FlightVisualHeight);
        Detail::AppendPodBytes(Buf, Bldg.RequiresCreep);
        Detail::AppendPodBytes(Buf, Bldg.RequiresPylon);
        Detail::AppendPodBytes(Buf, Bldg.HasPylonPower);
        Detail::AppendPodBytes(Buf, Bldg.UnderConstruction);
        Detail::AppendPodBytes(Buf, Bldg.ProtossPhase);
        Detail::AppendPodBytes(Buf, Bldg.ProtossPhaseSignal);
        Detail::AppendPodBytes(Buf, Bldg.ProtossPhaseElapsed);
        Detail::AppendPodBytes(Buf, Bldg.IsOrganic);
        Detail::AppendPodBytes(Buf, Bldg.BuilderUnitID);
        Detail::AppendVectorPodBytes(Buf, Bldg.AssistantBuilderUnitIDs);
        Detail::AppendPodBytes(Buf, Bldg.AddonID);
        Detail::AppendPodBytes(Buf, Bldg.BuildingDefID);
        Detail::AppendPodBytes(Buf, Bldg.LarvaCount);
        Detail::AppendPodBytes(Buf, Bldg.LarvaTimer);
        Detail::AppendPodBytes(Buf, Bldg.BuildTimeRemaining);
        Detail::AppendPodBytes(Buf, Bldg.ResearchingID);
        Detail::AppendPodBytes(Buf, Bldg.IsResearching);
        Detail::AppendPodBytes(Buf, Bldg.ResearchPlayer);
        Detail::AppendPodBytes(Buf, Bldg.BurnDamageAccumulator);
    }

    static bool Deserialize(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::BuildingComponent& Bldg)
    {
        return Detail::ReadPodBytes(Buf, Cursor, Bldg.IsBuilding) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.BuildProgress) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.TotalBuildTime) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.ConstructionHitPointAccumulator) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.ConstructionShieldAccumulator) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.IsAddon) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.CanLiftOff) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.IsLifted) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.FlightPhase) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.FlightAnimationComplete) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.FlightHeightTransitionActive) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.FlightHeightTransitionElapsed) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.FlightVisualHeight) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.RequiresCreep) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.RequiresPylon) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.HasPylonPower) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.UnderConstruction) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.ProtossPhase) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.ProtossPhaseSignal) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.ProtossPhaseElapsed) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.IsOrganic) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.BuilderUnitID) &&
               Detail::ReadVectorPodBytes(Buf, Cursor, Bldg.AssistantBuilderUnitIDs) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.AddonID) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.BuildingDefID) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.LarvaCount) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.LarvaTimer) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.BuildTimeRemaining) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.ResearchingID) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.IsResearching) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.ResearchPlayer) &&
               Detail::ReadPodBytes(Buf, Cursor, Bldg.BurnDamageAccumulator);
    }
};

template <>
struct CustomComponentBinarySerializer<Game::Components::ResourceComponent>
{
    static constexpr bool kSupported = true;

    static void Serialize(const Game::Components::ResourceComponent& Res, std::vector<uint8_t>& Buf)
    {
        Detail::AppendPodBytes(Buf, Res.Type);
        Detail::AppendPodBytes(Buf, Res.Amount);
        Detail::AppendPodBytes(Buf, Res.MaxAmount);
        Detail::AppendPodBytes(Buf, Res.IsDepleted);
        Detail::AppendPodBytes(Buf, Res.ActiveGathererEntityID);
        Detail::AppendVectorPodBytes(Buf, Res.QueuedGatherers);
    }

    static bool Deserialize(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::ResourceComponent& Res)
    {
        return Detail::ReadPodBytes(Buf, Cursor, Res.Type) &&
               Detail::ReadPodBytes(Buf, Cursor, Res.Amount) &&
               Detail::ReadPodBytes(Buf, Cursor, Res.MaxAmount) &&
               Detail::ReadPodBytes(Buf, Cursor, Res.IsDepleted) &&
               Detail::ReadPodBytes(Buf, Cursor, Res.ActiveGathererEntityID) &&
               Detail::ReadVectorPodBytes(Buf, Cursor, Res.QueuedGatherers);
    }
};

template <>
struct CustomComponentBinarySerializer<Game::Components::BulletComponent>
{
    static constexpr bool kSupported = true;

    static void Serialize(const Game::Components::BulletComponent& Bul, std::vector<uint8_t>& Buf)
    {
        Detail::AppendPodBytes(Buf, Bul.SourceEntityID);
        Detail::AppendPodBytes(Buf, Bul.Behavior);
        Detail::AppendPodBytes(Buf, Bul.Splash);
        Detail::AppendPodBytes(Buf, Bul.SplashInnerRadius);
        Detail::AppendPodBytes(Buf, Bul.SplashMediumRadius);
        Detail::AppendPodBytes(Buf, Bul.SplashRadius);
        Detail::AppendPodBytes(Buf, Bul.BaseDamage);
        Detail::AppendPodBytes(Buf, Bul.DamageKind);
        Detail::AppendPodBytes(Buf, Bul.Speed);
        Detail::AppendPodBytes(Buf, Bul.MaxLifetime);
        Detail::AppendPodBytes(Buf, Bul.LifetimeRemaining);
        Detail::AppendPodBytes(Buf, Bul.DamageTickAccumulator);
        Detail::AppendPodBytes(Buf, Bul.Velocity);
        Detail::AppendPodBytes(Buf, Bul.TargetPos);
        Detail::AppendPodBytes(Buf, Bul.TargetPositionOffset);
        Detail::AppendPodBytes(Buf, Bul.SplashLineStart);
        Detail::AppendPodBytes(Buf, Bul.SplashLineEnd);
        Detail::AppendPodBytes(Buf, Bul.TargetEntityID);
        Detail::AppendPodBytes(Buf, Bul.PreviousBounceTargetEntityID);
        Detail::AppendPodBytes(Buf, Bul.RemainingBounces);
        Detail::AppendPodBytes(Buf, Bul.BounceDamageDivisor);
        Detail::AppendPodBytes(Buf, Bul.CanTargetGround);
        Detail::AppendPodBytes(Buf, Bul.CanTargetAir);
        Detail::AppendPodBytes(Buf, Bul.HasHit);
        Detail::AppendPodBytes(Buf, Bul.DamageUpgradeBonus);
        Detail::AppendPodBytes(Buf, Bul.HitCount);
        Detail::AppendPodBytes(Buf, Bul.IsRangedAttack);
        Detail::AppendPodBytes(Buf, Bul.MissedAtLaunch);
        Detail::AppendPodBytes(Buf, Bul.PlayImpactAnimation);
        Detail::AppendPodBytes(Buf, Bul.AppliesAcidSpores);
        Detail::AppendPodBytes(Buf, Bul.SelfDestructSourceOnHit);
        Detail::AppendPodBytes(Buf, Bul.ImpactAtProjectilePosition);
        Detail::AppendPodBytes(Buf, Bul.FollowTargetPositionOffset);
        Detail::AppendPodBytes(Buf, Bul.DamageDrivenByIScript);
        Detail::AppendVectorPodBytes(Buf, Bul.IScriptHitEntityIDs);
        Detail::AppendPodBytes(Buf, Bul.ReachedIScriptDamageEndpoint);
        Detail::AppendPodBytes(Buf, Bul.IScriptEndpointGraceRemaining);
        Detail::AppendPodBytes(Buf, Bul.AwaitingLaunchSignal);
        Detail::AppendPodBytes(Buf, Bul.VisualAnimationOverride);
    }

    static bool Deserialize(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::BulletComponent& Bul)
    {
        return Detail::ReadPodBytes(Buf, Cursor, Bul.SourceEntityID) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.Behavior) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.Splash) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.SplashInnerRadius) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.SplashMediumRadius) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.SplashRadius) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.BaseDamage) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.DamageKind) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.Speed) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.MaxLifetime) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.LifetimeRemaining) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.DamageTickAccumulator) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.Velocity) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.TargetPos) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.TargetPositionOffset) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.SplashLineStart) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.SplashLineEnd) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.TargetEntityID) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.PreviousBounceTargetEntityID) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.RemainingBounces) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.BounceDamageDivisor) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.CanTargetGround) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.CanTargetAir) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.HasHit) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.DamageUpgradeBonus) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.HitCount) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.IsRangedAttack) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.MissedAtLaunch) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.PlayImpactAnimation) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.AppliesAcidSpores) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.SelfDestructSourceOnHit) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.ImpactAtProjectilePosition) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.FollowTargetPositionOffset) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.DamageDrivenByIScript) &&
               Detail::ReadVectorPodBytes(Buf, Cursor, Bul.IScriptHitEntityIDs) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.ReachedIScriptDamageEndpoint) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.IScriptEndpointGraceRemaining) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.AwaitingLaunchSignal) &&
               Detail::ReadPodBytes(Buf, Cursor, Bul.VisualAnimationOverride);
    }
};

template <>
struct CustomComponentBinarySerializer<Game::Components::PendingTransportBoardingComponent>
{
    static constexpr bool kSupported = true;

    static void Serialize(const Game::Components::PendingTransportBoardingComponent& Comp, std::vector<uint8_t>& Buf)
    {
        Detail::AppendVectorPodBytes(Buf, Comp.PassengerIDs);
        Detail::AppendPodBytes(Buf, Comp.CurrentIndex);
    }

    static bool Deserialize(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::PendingTransportBoardingComponent& Comp)
    {
        return Detail::ReadVectorPodBytes(Buf, Cursor, Comp.PassengerIDs) &&
               Detail::ReadPodBytes(Buf, Cursor, Comp.CurrentIndex);
    }
};

} // namespace Core::ECS
