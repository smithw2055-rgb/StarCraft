#include "Main/GameLoop/GameSaveState.h"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace Game::Client::GameLoop
{

namespace
{
    void WriteHexLine(FILE* File, const char* Prefix, const void* Data, size_t ByteLen)
    {
        std::fprintf(File, "%s %zu ", Prefix, ByteLen);
        const auto* Bytes = static_cast<const uint8_t*>(Data);
        for (size_t i = 0; i < ByteLen; ++i)
            std::fprintf(File, "%02X", Bytes[i]);
        std::fprintf(File, "\n");
    }

    bool DecodeHexBytes(const char* HexStr, void* OutData, size_t ExpectedBytes)
    {
        auto HexNibble = [](char C) -> int {
            if (C >= '0' && C <= '9') return C - '0';
            if (C >= 'A' && C <= 'F') return 10 + (C - 'A');
            if (C >= 'a' && C <= 'f') return 10 + (C - 'a');
            return -1;
        };
        auto* OutBytes = static_cast<uint8_t*>(OutData);
        for (size_t i = 0; i < ExpectedBytes; ++i)
        {
            const int Hi = HexNibble(HexStr[i * 2]);
            const int Lo = HexNibble(HexStr[i * 2 + 1]);
            if (Hi < 0 || Lo < 0)
                return false;
            OutBytes[i] = static_cast<uint8_t>((Hi << 4) | Lo);
        }
        return true;
    }

    template <typename T>
    void AppendPod(std::vector<uint8_t>& Buf, const T& Val)
    {
        static_assert(std::is_trivially_copyable_v<T>, "AppendPod requires trivially copyable type");
        const auto* Ptr = reinterpret_cast<const uint8_t*>(&Val);
        Buf.insert(Buf.end(), Ptr, Ptr + sizeof(T));
    }

    template <typename T>
    void AppendVector(std::vector<uint8_t>& Buf, const std::vector<T>& Vec)
    {
        static_assert(std::is_trivially_copyable_v<T>, "AppendVector requires trivially copyable element");
        const uint32_t Count = static_cast<uint32_t>(Vec.size());
        AppendPod(Buf, Count);
        if (Count > 0)
        {
            const auto* Ptr = reinterpret_cast<const uint8_t*>(Vec.data());
            Buf.insert(Buf.end(), Ptr, Ptr + Count * sizeof(T));
        }
    }

    template <typename T>
    bool ReadPod(const std::vector<uint8_t>& Buf, size_t& Cursor, T& Out)
    {
        static_assert(std::is_trivially_copyable_v<T>, "ReadPod requires trivially copyable type");
        if (Cursor + sizeof(T) > Buf.size())
            return false;
        std::memcpy(&Out, Buf.data() + Cursor, sizeof(T));
        Cursor += sizeof(T);
        return true;
    }

    template <typename T>
    bool ReadVector(const std::vector<uint8_t>& Buf, size_t& Cursor, std::vector<T>& Out)
    {
        static_assert(std::is_trivially_copyable_v<T>, "ReadVector requires trivially copyable element");
        uint32_t Count = 0;
        if (!ReadPod(Buf, Cursor, Count))
            return false;
        const size_t ByteCount = static_cast<size_t>(Count) * sizeof(T);
        if (Cursor + ByteCount > Buf.size())
            return false;
        Out.resize(Count);
        if (Count > 0)
            std::memcpy(Out.data(), Buf.data() + Cursor, ByteCount);
        Cursor += ByteCount;
        return true;
    }

    void SerializeCommandComponent(std::vector<uint8_t>& Buf, const Game::Components::CommandComponent& Cmd)
    {
        AppendVector(Buf, Cmd.CommandQueue);
        AppendPod(Buf, Cmd.CurrentCommand);
        AppendPod(Buf, Cmd.TargetPosition);
        AppendPod(Buf, Cmd.TargetEntityID);
        AppendPod(Buf, Cmd.PatrolAnchor);
        AppendPod(Buf, Cmd.HasPatrolAnchor);
        AppendVector(Buf, Cmd.Orders);
        AppendPod(Buf, Cmd.CurrentOrderIndex);
        AppendPod(Buf, Cmd.RallyPoint);
        AppendPod(Buf, Cmd.HasRallyPoint);
        AppendPod(Buf, Cmd.RallyDisplayTimer);
    }

    bool DeserializeCommandComponent(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::CommandComponent& Cmd)
    {
        return ReadVector(Buf, Cursor, Cmd.CommandQueue) &&
               ReadPod(Buf, Cursor, Cmd.CurrentCommand) &&
               ReadPod(Buf, Cursor, Cmd.TargetPosition) &&
               ReadPod(Buf, Cursor, Cmd.TargetEntityID) &&
               ReadPod(Buf, Cursor, Cmd.PatrolAnchor) &&
               ReadPod(Buf, Cursor, Cmd.HasPatrolAnchor) &&
               ReadVector(Buf, Cursor, Cmd.Orders) &&
               ReadPod(Buf, Cursor, Cmd.CurrentOrderIndex) &&
               ReadPod(Buf, Cursor, Cmd.RallyPoint) &&
               ReadPod(Buf, Cursor, Cmd.HasRallyPoint) &&
               ReadPod(Buf, Cursor, Cmd.RallyDisplayTimer);
    }

    void SerializePathfindingComponent(std::vector<uint8_t>& Buf, const Game::Components::PathfindingComponent& Path)
    {
        AppendVector(Buf, Path.Path);
        AppendPod(Buf, Path.PathIndex);
        AppendPod(Buf, Path.NodeTolerance);
        AppendPod(Buf, Path.RequestedDestination);
        AppendPod(Buf, Path.RequestedTargetEntityID);
        AppendPod(Buf, Path.RequestedIgnoreEntityID);
        AppendPod(Buf, Path.RequestedAdditionalIgnoreEntityID);
        AppendPod(Buf, Path.RepathFailCount);
        AppendPod(Buf, Path.LastRepathTime);
        AppendPod(Buf, Path.BlockedByEntityID);
        AppendPod(Buf, Path.LastPathStatus);
        AppendPod(Buf, Path.LastPathEndReason);
        AppendPod(Buf, Path.ResolvedPathDestination);
        AppendPod(Buf, Path.NavigationVersion);
        AppendPod(Buf, Path.LastPathExpandedCells);
        AppendPod(Buf, Path.CurrentVelocity);
        AppendPod(Buf, Path.FixedPosition);
        AppendPod(Buf, Path.FixedVelocity);
        AppendPod(Buf, Path.HasFixedPosition);
        AppendPod(Buf, Path.CurrentSpeed);
        AppendPod(Buf, Path.TargetSpeed);
        AppendPod(Buf, Path.Acceleration);
        AppendPod(Buf, Path.TurnRate);
        AppendPod(Buf, Path.FacingAngle);
        AppendPod(Buf, Path.FriendlyYieldVector);
        AppendPod(Buf, Path.FriendlyYieldAnchor);
        AppendPod(Buf, Path.HasFriendlyYieldAnchor);
        AppendPod(Buf, Path.IsAir);
        AppendPod(Buf, Path.CollisionRadius);
        AppendPod(Buf, Path.HasCollisionBounds);
        AppendPod(Buf, Path.CollisionLeft);
        AppendPod(Buf, Path.CollisionUp);
        AppendPod(Buf, Path.CollisionRight);
        AppendPod(Buf, Path.CollisionDown);
        AppendPod(Buf, Path.HasRecentMoveFailure);
        AppendPod(Buf, Path.RecentFailedMoveTarget);
        AppendPod(Buf, Path.RecentMoveFailureCooldown);
        AppendPod(Buf, Path.StallTimer);
        AppendPod(Buf, Path.LastWaypointDistance);
        AppendPod(Buf, Path.HasLastWaypointDistance);
        AppendPod(Buf, Path.DestinationStallTimer);
        AppendPod(Buf, Path.LastDestinationDistance);
        AppendPod(Buf, Path.HasLastDestinationDistance);
        AppendPod(Buf, Path.StallTicks);
        AppendPod(Buf, Path.DestinationStallTicks);
        AppendPod(Buf, Path.RecentMoveFailureTicks);
        AppendPod(Buf, Path.ConsecutiveMoveFailures);
        AppendPod(Buf, Path.RecentMoveFailureNavigationVersion);
    }

    bool DeserializePathfindingComponent(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::PathfindingComponent& Path)
    {
        return ReadVector(Buf, Cursor, Path.Path) &&
               ReadPod(Buf, Cursor, Path.PathIndex) &&
               ReadPod(Buf, Cursor, Path.NodeTolerance) &&
               ReadPod(Buf, Cursor, Path.RequestedDestination) &&
               ReadPod(Buf, Cursor, Path.RequestedTargetEntityID) &&
               ReadPod(Buf, Cursor, Path.RequestedIgnoreEntityID) &&
               ReadPod(Buf, Cursor, Path.RequestedAdditionalIgnoreEntityID) &&
               ReadPod(Buf, Cursor, Path.RepathFailCount) &&
               ReadPod(Buf, Cursor, Path.LastRepathTime) &&
               ReadPod(Buf, Cursor, Path.BlockedByEntityID) &&
               ReadPod(Buf, Cursor, Path.LastPathStatus) &&
               ReadPod(Buf, Cursor, Path.LastPathEndReason) &&
               ReadPod(Buf, Cursor, Path.ResolvedPathDestination) &&
               ReadPod(Buf, Cursor, Path.NavigationVersion) &&
               ReadPod(Buf, Cursor, Path.LastPathExpandedCells) &&
               ReadPod(Buf, Cursor, Path.CurrentVelocity) &&
               ReadPod(Buf, Cursor, Path.FixedPosition) &&
               ReadPod(Buf, Cursor, Path.FixedVelocity) &&
               ReadPod(Buf, Cursor, Path.HasFixedPosition) &&
               ReadPod(Buf, Cursor, Path.CurrentSpeed) &&
               ReadPod(Buf, Cursor, Path.TargetSpeed) &&
               ReadPod(Buf, Cursor, Path.Acceleration) &&
               ReadPod(Buf, Cursor, Path.TurnRate) &&
               ReadPod(Buf, Cursor, Path.FacingAngle) &&
               ReadPod(Buf, Cursor, Path.FriendlyYieldVector) &&
               ReadPod(Buf, Cursor, Path.FriendlyYieldAnchor) &&
               ReadPod(Buf, Cursor, Path.HasFriendlyYieldAnchor) &&
               ReadPod(Buf, Cursor, Path.IsAir) &&
               ReadPod(Buf, Cursor, Path.CollisionRadius) &&
               ReadPod(Buf, Cursor, Path.HasCollisionBounds) &&
               ReadPod(Buf, Cursor, Path.CollisionLeft) &&
               ReadPod(Buf, Cursor, Path.CollisionUp) &&
               ReadPod(Buf, Cursor, Path.CollisionRight) &&
               ReadPod(Buf, Cursor, Path.CollisionDown) &&
               ReadPod(Buf, Cursor, Path.HasRecentMoveFailure) &&
               ReadPod(Buf, Cursor, Path.RecentFailedMoveTarget) &&
               ReadPod(Buf, Cursor, Path.RecentMoveFailureCooldown) &&
               ReadPod(Buf, Cursor, Path.StallTimer) &&
               ReadPod(Buf, Cursor, Path.LastWaypointDistance) &&
               ReadPod(Buf, Cursor, Path.HasLastWaypointDistance) &&
               ReadPod(Buf, Cursor, Path.DestinationStallTimer) &&
               ReadPod(Buf, Cursor, Path.LastDestinationDistance) &&
               ReadPod(Buf, Cursor, Path.HasLastDestinationDistance) &&
               ReadPod(Buf, Cursor, Path.StallTicks) &&
               ReadPod(Buf, Cursor, Path.DestinationStallTicks) &&
               ReadPod(Buf, Cursor, Path.RecentMoveFailureTicks) &&
               ReadPod(Buf, Cursor, Path.ConsecutiveMoveFailures) &&
               ReadPod(Buf, Cursor, Path.RecentMoveFailureNavigationVersion);
    }

    void SerializeBuildingComponent(std::vector<uint8_t>& Buf, const Game::Components::BuildingComponent& Bldg)
    {
        AppendPod(Buf, Bldg.IsBuilding);
        AppendPod(Buf, Bldg.BuildProgress);
        AppendPod(Buf, Bldg.TotalBuildTime);
        AppendPod(Buf, Bldg.ConstructionHitPointAccumulator);
        AppendPod(Buf, Bldg.ConstructionShieldAccumulator);
        AppendPod(Buf, Bldg.IsAddon);
        AppendPod(Buf, Bldg.CanLiftOff);
        AppendPod(Buf, Bldg.IsLifted);
        AppendPod(Buf, Bldg.FlightPhase);
        AppendPod(Buf, Bldg.FlightAnimationComplete);
        AppendPod(Buf, Bldg.FlightHeightTransitionActive);
        AppendPod(Buf, Bldg.FlightHeightTransitionElapsed);
        AppendPod(Buf, Bldg.FlightVisualHeight);
        AppendPod(Buf, Bldg.RequiresCreep);
        AppendPod(Buf, Bldg.RequiresPylon);
        AppendPod(Buf, Bldg.HasPylonPower);
        AppendPod(Buf, Bldg.UnderConstruction);
        AppendPod(Buf, Bldg.ProtossPhase);
        AppendPod(Buf, Bldg.ProtossPhaseSignal);
        AppendPod(Buf, Bldg.ProtossPhaseElapsed);
        AppendPod(Buf, Bldg.IsOrganic);
        AppendPod(Buf, Bldg.BuilderUnitID);
        AppendVector(Buf, Bldg.AssistantBuilderUnitIDs);
        AppendPod(Buf, Bldg.AddonID);
        AppendPod(Buf, Bldg.BuildingDefID);
        AppendPod(Buf, Bldg.LarvaCount);
        AppendPod(Buf, Bldg.LarvaTimer);
        AppendPod(Buf, Bldg.BuildTimeRemaining);
        AppendPod(Buf, Bldg.ResearchingID);
        AppendPod(Buf, Bldg.IsResearching);
        AppendPod(Buf, Bldg.ResearchPlayer);
        AppendPod(Buf, Bldg.BurnDamageAccumulator);
    }

    bool DeserializeBuildingComponent(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::BuildingComponent& Bldg)
    {
        return ReadPod(Buf, Cursor, Bldg.IsBuilding) &&
               ReadPod(Buf, Cursor, Bldg.BuildProgress) &&
               ReadPod(Buf, Cursor, Bldg.TotalBuildTime) &&
               ReadPod(Buf, Cursor, Bldg.ConstructionHitPointAccumulator) &&
               ReadPod(Buf, Cursor, Bldg.ConstructionShieldAccumulator) &&
               ReadPod(Buf, Cursor, Bldg.IsAddon) &&
               ReadPod(Buf, Cursor, Bldg.CanLiftOff) &&
               ReadPod(Buf, Cursor, Bldg.IsLifted) &&
               ReadPod(Buf, Cursor, Bldg.FlightPhase) &&
               ReadPod(Buf, Cursor, Bldg.FlightAnimationComplete) &&
               ReadPod(Buf, Cursor, Bldg.FlightHeightTransitionActive) &&
               ReadPod(Buf, Cursor, Bldg.FlightHeightTransitionElapsed) &&
               ReadPod(Buf, Cursor, Bldg.FlightVisualHeight) &&
               ReadPod(Buf, Cursor, Bldg.RequiresCreep) &&
               ReadPod(Buf, Cursor, Bldg.RequiresPylon) &&
               ReadPod(Buf, Cursor, Bldg.HasPylonPower) &&
               ReadPod(Buf, Cursor, Bldg.UnderConstruction) &&
               ReadPod(Buf, Cursor, Bldg.ProtossPhase) &&
               ReadPod(Buf, Cursor, Bldg.ProtossPhaseSignal) &&
               ReadPod(Buf, Cursor, Bldg.ProtossPhaseElapsed) &&
               ReadPod(Buf, Cursor, Bldg.IsOrganic) &&
               ReadPod(Buf, Cursor, Bldg.BuilderUnitID) &&
               ReadVector(Buf, Cursor, Bldg.AssistantBuilderUnitIDs) &&
               ReadPod(Buf, Cursor, Bldg.AddonID) &&
               ReadPod(Buf, Cursor, Bldg.BuildingDefID) &&
               ReadPod(Buf, Cursor, Bldg.LarvaCount) &&
               ReadPod(Buf, Cursor, Bldg.LarvaTimer) &&
               ReadPod(Buf, Cursor, Bldg.BuildTimeRemaining) &&
               ReadPod(Buf, Cursor, Bldg.ResearchingID) &&
               ReadPod(Buf, Cursor, Bldg.IsResearching) &&
               ReadPod(Buf, Cursor, Bldg.ResearchPlayer) &&
               ReadPod(Buf, Cursor, Bldg.BurnDamageAccumulator);
    }

    void SerializeResourceComponent(std::vector<uint8_t>& Buf, const Game::Components::ResourceComponent& Res)
    {
        AppendPod(Buf, Res.Type);
        AppendPod(Buf, Res.Amount);
        AppendPod(Buf, Res.MaxAmount);
        AppendPod(Buf, Res.IsDepleted);
        AppendPod(Buf, Res.ActiveGathererEntityID);
        AppendVector(Buf, Res.QueuedGatherers);
    }

    bool DeserializeResourceComponent(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::ResourceComponent& Res)
    {
        return ReadPod(Buf, Cursor, Res.Type) &&
               ReadPod(Buf, Cursor, Res.Amount) &&
               ReadPod(Buf, Cursor, Res.MaxAmount) &&
               ReadPod(Buf, Cursor, Res.IsDepleted) &&
               ReadPod(Buf, Cursor, Res.ActiveGathererEntityID) &&
               ReadVector(Buf, Cursor, Res.QueuedGatherers);
    }

    void SerializeBulletComponent(std::vector<uint8_t>& Buf, const Game::Components::BulletComponent& Bul)
    {
        AppendPod(Buf, Bul.SourceEntityID);
        AppendPod(Buf, Bul.Behavior);
        AppendPod(Buf, Bul.Splash);
        AppendPod(Buf, Bul.SplashInnerRadius);
        AppendPod(Buf, Bul.SplashMediumRadius);
        AppendPod(Buf, Bul.SplashRadius);
        AppendPod(Buf, Bul.BaseDamage);
        AppendPod(Buf, Bul.DamageKind);
        AppendPod(Buf, Bul.Speed);
        AppendPod(Buf, Bul.MaxLifetime);
        AppendPod(Buf, Bul.LifetimeRemaining);
        AppendPod(Buf, Bul.DamageTickAccumulator);
        AppendPod(Buf, Bul.Velocity);
        AppendPod(Buf, Bul.TargetPos);
        AppendPod(Buf, Bul.TargetPositionOffset);
        AppendPod(Buf, Bul.SplashLineStart);
        AppendPod(Buf, Bul.SplashLineEnd);
        AppendPod(Buf, Bul.TargetEntityID);
        AppendPod(Buf, Bul.PreviousBounceTargetEntityID);
        AppendPod(Buf, Bul.RemainingBounces);
        AppendPod(Buf, Bul.BounceDamageDivisor);
        AppendPod(Buf, Bul.CanTargetGround);
        AppendPod(Buf, Bul.CanTargetAir);
        AppendPod(Buf, Bul.HasHit);
        AppendPod(Buf, Bul.DamageUpgradeBonus);
        AppendPod(Buf, Bul.HitCount);
        AppendPod(Buf, Bul.IsRangedAttack);
        AppendPod(Buf, Bul.MissedAtLaunch);
        AppendPod(Buf, Bul.PlayImpactAnimation);
        AppendPod(Buf, Bul.AppliesAcidSpores);
        AppendPod(Buf, Bul.SelfDestructSourceOnHit);
        AppendPod(Buf, Bul.ImpactAtProjectilePosition);
        AppendPod(Buf, Bul.FollowTargetPositionOffset);
        AppendPod(Buf, Bul.DamageDrivenByIScript);
        AppendVector(Buf, Bul.IScriptHitEntityIDs);
        AppendPod(Buf, Bul.ReachedIScriptDamageEndpoint);
        AppendPod(Buf, Bul.IScriptEndpointGraceRemaining);
        AppendPod(Buf, Bul.AwaitingLaunchSignal);
        AppendPod(Buf, Bul.VisualAnimationOverride);
    }

    bool DeserializeBulletComponent(const std::vector<uint8_t>& Buf, size_t& Cursor, Game::Components::BulletComponent& Bul)
    {
        return ReadPod(Buf, Cursor, Bul.SourceEntityID) &&
               ReadPod(Buf, Cursor, Bul.Behavior) &&
               ReadPod(Buf, Cursor, Bul.Splash) &&
               ReadPod(Buf, Cursor, Bul.SplashInnerRadius) &&
               ReadPod(Buf, Cursor, Bul.SplashMediumRadius) &&
               ReadPod(Buf, Cursor, Bul.SplashRadius) &&
               ReadPod(Buf, Cursor, Bul.BaseDamage) &&
               ReadPod(Buf, Cursor, Bul.DamageKind) &&
               ReadPod(Buf, Cursor, Bul.Speed) &&
               ReadPod(Buf, Cursor, Bul.MaxLifetime) &&
               ReadPod(Buf, Cursor, Bul.LifetimeRemaining) &&
               ReadPod(Buf, Cursor, Bul.DamageTickAccumulator) &&
               ReadPod(Buf, Cursor, Bul.Velocity) &&
               ReadPod(Buf, Cursor, Bul.TargetPos) &&
               ReadPod(Buf, Cursor, Bul.TargetPositionOffset) &&
               ReadPod(Buf, Cursor, Bul.SplashLineStart) &&
               ReadPod(Buf, Cursor, Bul.SplashLineEnd) &&
               ReadPod(Buf, Cursor, Bul.TargetEntityID) &&
               ReadPod(Buf, Cursor, Bul.PreviousBounceTargetEntityID) &&
               ReadPod(Buf, Cursor, Bul.RemainingBounces) &&
               ReadPod(Buf, Cursor, Bul.BounceDamageDivisor) &&
               ReadPod(Buf, Cursor, Bul.CanTargetGround) &&
               ReadPod(Buf, Cursor, Bul.CanTargetAir) &&
               ReadPod(Buf, Cursor, Bul.HasHit) &&
               ReadPod(Buf, Cursor, Bul.DamageUpgradeBonus) &&
               ReadPod(Buf, Cursor, Bul.HitCount) &&
               ReadPod(Buf, Cursor, Bul.IsRangedAttack) &&
               ReadPod(Buf, Cursor, Bul.MissedAtLaunch) &&
               ReadPod(Buf, Cursor, Bul.PlayImpactAnimation) &&
               ReadPod(Buf, Cursor, Bul.AppliesAcidSpores) &&
               ReadPod(Buf, Cursor, Bul.SelfDestructSourceOnHit) &&
               ReadPod(Buf, Cursor, Bul.ImpactAtProjectilePosition) &&
               ReadPod(Buf, Cursor, Bul.FollowTargetPositionOffset) &&
               ReadPod(Buf, Cursor, Bul.DamageDrivenByIScript) &&
               ReadVector(Buf, Cursor, Bul.IScriptHitEntityIDs) &&
               ReadPod(Buf, Cursor, Bul.ReachedIScriptDamageEndpoint) &&
               ReadPod(Buf, Cursor, Bul.IScriptEndpointGraceRemaining) &&
               ReadPod(Buf, Cursor, Bul.AwaitingLaunchSignal) &&
               ReadPod(Buf, Cursor, Bul.VisualAnimationOverride);
    }

    void SerializeEntityRecord(std::vector<uint8_t>& Buf, const Core::ECS::EntityStateRecord& Rec)
    {
        Buf.clear();
        uint32_t Flags = 0;
        if (Rec.HasTransform)       Flags |= (1u << 0);
        if (Rec.HasUnit)            Flags |= (1u << 1);
        if (Rec.HasPlayer)          Flags |= (1u << 2);
        if (Rec.HasCombat)          Flags |= (1u << 3);
        if (Rec.HasRender)          Flags |= (1u << 4);
        if (Rec.HasCommand)         Flags |= (1u << 5);
        if (Rec.HasPathfinding)     Flags |= (1u << 6);
        if (Rec.HasBuilding)        Flags |= (1u << 7);
        if (Rec.HasResource)        Flags |= (1u << 8);
        if (Rec.HasHarvest)         Flags |= (1u << 9);
        if (Rec.HasProduction)      Flags |= (1u << 10);
        if (Rec.HasBullet)          Flags |= (1u << 11);
        if (Rec.HasStatusEffect)    Flags |= (1u << 12);
        if (Rec.HasCloak)           Flags |= (1u << 13);
        if (Rec.HasBurrow)          Flags |= (1u << 14);
        if (Rec.HasLoadedUnits)     Flags |= (1u << 15);
        if (Rec.HasMorph)           Flags |= (1u << 16);
        if (Rec.HasSiegeTransition) Flags |= (1u << 17);
        if (Rec.HasSpiderMine)      Flags |= (1u << 18);
        if (Rec.HasNuke)            Flags |= (1u << 19);
        if (Rec.HasArchonWarp)      Flags |= (1u << 20);

        AppendPod(Buf, Rec.Id);
        AppendPod(Buf, Rec.Generation);
        AppendPod(Buf, Flags);

        if (Rec.HasTransform)       AppendPod(Buf, Rec.Transform);
        if (Rec.HasUnit)            AppendPod(Buf, Rec.Unit);
        if (Rec.HasPlayer)          AppendPod(Buf, Rec.Player);
        if (Rec.HasCombat)          AppendPod(Buf, Rec.Combat);
        if (Rec.HasRender)          AppendPod(Buf, Rec.Render);
        if (Rec.HasCommand)         SerializeCommandComponent(Buf, Rec.Command);
        if (Rec.HasPathfinding)     SerializePathfindingComponent(Buf, Rec.Pathfinding);
        if (Rec.HasBuilding)        SerializeBuildingComponent(Buf, Rec.Building);
        if (Rec.HasResource)        SerializeResourceComponent(Buf, Rec.Resource);
        if (Rec.HasHarvest)         AppendPod(Buf, Rec.Harvest);
        if (Rec.HasProduction)      AppendPod(Buf, Rec.Production);
        if (Rec.HasBullet)          SerializeBulletComponent(Buf, Rec.Bullet);
        if (Rec.HasStatusEffect)    AppendPod(Buf, Rec.StatusEffect);
        if (Rec.HasCloak)           AppendPod(Buf, Rec.Cloak);
        if (Rec.HasBurrow)          AppendPod(Buf, Rec.Burrow);
        if (Rec.HasLoadedUnits)     AppendPod(Buf, Rec.LoadedUnits);
        if (Rec.HasMorph)           AppendPod(Buf, Rec.Morph);
        if (Rec.HasSiegeTransition) AppendPod(Buf, Rec.SiegeTransition);
        if (Rec.HasSpiderMine)      AppendPod(Buf, Rec.SpiderMine);
        if (Rec.HasNuke)            AppendPod(Buf, Rec.Nuke);
        if (Rec.HasArchonWarp)      AppendPod(Buf, Rec.ArchonWarp);
    }

    bool DeserializeEntityRecord(const std::vector<uint8_t>& Buf, Core::ECS::EntityStateRecord& Rec)
    {
        size_t Cursor = 0;
        uint32_t Flags = 0;
        if (!ReadPod(Buf, Cursor, Rec.Id) ||
            !ReadPod(Buf, Cursor, Rec.Generation) ||
            !ReadPod(Buf, Cursor, Flags))
        {
            return false;
        }

        if ((Flags & (1u << 0))  && !(Rec.HasTransform = true,       ReadPod(Buf, Cursor, Rec.Transform)))       return false;
        if ((Flags & (1u << 1))  && !(Rec.HasUnit = true,            ReadPod(Buf, Cursor, Rec.Unit)))            return false;
        if ((Flags & (1u << 2))  && !(Rec.HasPlayer = true,          ReadPod(Buf, Cursor, Rec.Player)))          return false;
        if ((Flags & (1u << 3))  && !(Rec.HasCombat = true,          ReadPod(Buf, Cursor, Rec.Combat)))          return false;
        if ((Flags & (1u << 4))  && !(Rec.HasRender = true,          ReadPod(Buf, Cursor, Rec.Render)))          return false;
        if ((Flags & (1u << 5))  && !(Rec.HasCommand = true,         DeserializeCommandComponent(Buf, Cursor, Rec.Command)))         return false;
        if ((Flags & (1u << 6))  && !(Rec.HasPathfinding = true,     DeserializePathfindingComponent(Buf, Cursor, Rec.Pathfinding))) return false;
        if ((Flags & (1u << 7))  && !(Rec.HasBuilding = true,        DeserializeBuildingComponent(Buf, Cursor, Rec.Building)))       return false;
        if ((Flags & (1u << 8))  && !(Rec.HasResource = true,        DeserializeResourceComponent(Buf, Cursor, Rec.Resource)))       return false;
        if ((Flags & (1u << 9))  && !(Rec.HasHarvest = true,         ReadPod(Buf, Cursor, Rec.Harvest)))         return false;
        if ((Flags & (1u << 10)) && !(Rec.HasProduction = true,      ReadPod(Buf, Cursor, Rec.Production)))      return false;
        if ((Flags & (1u << 11)) && !(Rec.HasBullet = true,          DeserializeBulletComponent(Buf, Cursor, Rec.Bullet)))           return false;
        if ((Flags & (1u << 12)) && !(Rec.HasStatusEffect = true,    ReadPod(Buf, Cursor, Rec.StatusEffect)))    return false;
        if ((Flags & (1u << 13)) && !(Rec.HasCloak = true,           ReadPod(Buf, Cursor, Rec.Cloak)))           return false;
        if ((Flags & (1u << 14)) && !(Rec.HasBurrow = true,          ReadPod(Buf, Cursor, Rec.Burrow)))          return false;
        if ((Flags & (1u << 15)) && !(Rec.HasLoadedUnits = true,     ReadPod(Buf, Cursor, Rec.LoadedUnits)))     return false;
        if ((Flags & (1u << 16)) && !(Rec.HasMorph = true,           ReadPod(Buf, Cursor, Rec.Morph)))           return false;
        if ((Flags & (1u << 17)) && !(Rec.HasSiegeTransition = true, ReadPod(Buf, Cursor, Rec.SiegeTransition))) return false;
        if ((Flags & (1u << 18)) && !(Rec.HasSpiderMine = true,      ReadPod(Buf, Cursor, Rec.SpiderMine)))      return false;
        if ((Flags & (1u << 19)) && !(Rec.HasNuke = true,            ReadPod(Buf, Cursor, Rec.Nuke)))            return false;
        if ((Flags & (1u << 20)) && !(Rec.HasArchonWarp = true,      ReadPod(Buf, Cursor, Rec.ArchonWarp)))      return false;

        return Cursor == Buf.size();
    }
}

bool WriteGameSave(FILE* File, const GameSaveSnapshot& Snapshot)
{
    if (!File)
        return false;

    std::fprintf(File, "SCVS 1\n");
    std::fprintf(File, "map %s\n", Snapshot.MapPath.c_str());
    std::fprintf(File, "paused %d\n", Snapshot.Paused ? 1 : 0);
    std::fprintf(File, "minerals %d\n", Snapshot.Resources.MineralCount);
    std::fprintf(File, "gas %d\n", Snapshot.Resources.GasCount);
    std::fprintf(File, "supply_used %d\n", Snapshot.Resources.SupplyUsed);
    std::fprintf(File, "supply_max %d\n", Snapshot.Resources.SupplyMax);

    if (Snapshot.HasWorldSnapshot)
    {
        std::fprintf(File, "has_world_snapshot 1\n");
        std::fprintf(File, "sim_tick %llu\n", static_cast<unsigned long long>(Snapshot.SimulationTick));
        std::fprintf(File, "world_frame %u\n", Snapshot.WorldSnapshot.FrameNumber);
        std::fprintf(File, "world_hash %u\n", Snapshot.WorldSnapshot.StateHashFNV1a);
        if (Snapshot.WorldSnapshot.HasUpgradesResource)
        {
            WriteHexLine(File, "world_upgrades",
                         &Snapshot.WorldSnapshot.Upgrades,
                         sizeof(Snapshot.WorldSnapshot.Upgrades));
        }
        if (Snapshot.WorldSnapshot.HasEconomyResource)
        {
            WriteHexLine(File, "world_economy",
                         &Snapshot.WorldSnapshot.Economy,
                         sizeof(Snapshot.WorldSnapshot.Economy));
        }
        std::fprintf(File, "world_entities %zu\n", Snapshot.WorldSnapshot.Entities.size());
        std::vector<uint8_t> EntityBlob;
        EntityBlob.reserve(1024);
        for (const auto& Rec : Snapshot.WorldSnapshot.Entities)
        {
            SerializeEntityRecord(EntityBlob, Rec);
            WriteHexLine(File, "entity_pod", EntityBlob.data(), EntityBlob.size());
        }
    }
    return true;
}

bool ReadGameSave(FILE* File, GameSaveSnapshot& Snapshot)
{
    if (!File)
        return false;

    std::vector<char> LineBuf(262144, 0);
    if (!std::fgets(LineBuf.data(), static_cast<int>(LineBuf.size()), File) ||
        std::strncmp(LineBuf.data(), "SCVS 1", 6) != 0)
        return false;

    int Paused = Snapshot.Paused ? 1 : 0;
    int Minerals = Snapshot.Resources.MineralCount;
    int Gas = Snapshot.Resources.GasCount;
    int SupplyUsed = Snapshot.Resources.SupplyUsed;
    int SupplyMax = Snapshot.Resources.SupplyMax;
    int HasWorldSnap = 0;
    unsigned long long SimTick = 0;
    unsigned int WorldFrame = 0;
    unsigned int WorldHash = 0;

    Snapshot.HasWorldSnapshot = false;
    Snapshot.WorldSnapshot = {};
    std::vector<uint8_t> EntityBlob;

    while (std::fgets(LineBuf.data(), static_cast<int>(LineBuf.size()), File))
    {
        const char* Line = LineBuf.data();
        if (std::strncmp(Line, "map ", 4) == 0)
        {
            std::string Path(Line + 4);
            while (!Path.empty() && (Path.back() == '\n' || Path.back() == '\r'))
                Path.pop_back();
            Snapshot.MapPath = std::move(Path);
            continue;
        }
        if (std::sscanf(Line, "paused %d", &Paused) == 1) continue;
        if (std::sscanf(Line, "minerals %d", &Minerals) == 1) continue;
        if (std::sscanf(Line, "gas %d", &Gas) == 1) continue;
        if (std::sscanf(Line, "supply_used %d", &SupplyUsed) == 1) continue;
        if (std::sscanf(Line, "supply_max %d", &SupplyMax) == 1) continue;
        if (std::sscanf(Line, "has_world_snapshot %d", &HasWorldSnap) == 1)
        {
            Snapshot.HasWorldSnapshot = (HasWorldSnap != 0);
            continue;
        }
        if (std::sscanf(Line, "sim_tick %llu", &SimTick) == 1)
        {
            Snapshot.SimulationTick = static_cast<uint64_t>(SimTick);
            continue;
        }
        if (std::sscanf(Line, "world_frame %u", &WorldFrame) == 1)
        {
            Snapshot.WorldSnapshot.FrameNumber = WorldFrame;
            continue;
        }
        if (std::sscanf(Line, "world_hash %u", &WorldHash) == 1)
        {
            Snapshot.WorldSnapshot.StateHashFNV1a = WorldHash;
            continue;
        }
        size_t ByteLen = 0;
        int Offset = 0;
        if (std::sscanf(Line, "world_upgrades %zu %n", &ByteLen, &Offset) == 1 &&
            ByteLen == sizeof(Snapshot.WorldSnapshot.Upgrades))
        {
            if (DecodeHexBytes(Line + Offset, &Snapshot.WorldSnapshot.Upgrades, ByteLen))
                Snapshot.WorldSnapshot.HasUpgradesResource = true;
            continue;
        }
        if (std::sscanf(Line, "world_economy %zu %n", &ByteLen, &Offset) == 1 &&
            ByteLen == sizeof(Snapshot.WorldSnapshot.Economy))
        {
            if (DecodeHexBytes(Line + Offset, &Snapshot.WorldSnapshot.Economy, ByteLen))
                Snapshot.WorldSnapshot.HasEconomyResource = true;
            continue;
        }
        if (std::sscanf(Line, "entity_pod %zu %n", &ByteLen, &Offset) == 1 && ByteLen > 0)
        {
            EntityBlob.resize(ByteLen);
            if (DecodeHexBytes(Line + Offset, EntityBlob.data(), ByteLen))
            {
                Core::ECS::EntityStateRecord Rec{};
                if (DeserializeEntityRecord(EntityBlob, Rec))
                    Snapshot.WorldSnapshot.Entities.push_back(std::move(Rec));
            }
            continue;
        }
    }

    Snapshot.Paused = Paused != 0;
    Snapshot.Resources.MineralCount = Minerals;
    Snapshot.Resources.GasCount = Gas;
    Snapshot.Resources.SupplyUsed = SupplyUsed;
    Snapshot.Resources.SupplyMax = SupplyMax;
    return true;
}

} // namespace Game::Client::GameLoop
