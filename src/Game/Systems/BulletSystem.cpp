#include "Game/Systems/BulletSystem.h"
#include "Game/Pathfinding/WorldSpatialIndexSystem.h"
#include "Game/IScriptVM.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/GeyserState.h"

#include <algorithm>
#include <cmath>

namespace Game::Systems
{

using namespace Game::Components;

BulletSystem::VisualResolver BulletSystem::sVisualResolver;

void BulletSystem::SetVisualResolver(VisualResolver Resolver)
{
    sVisualResolver = std::move(Resolver);
}

namespace
{
    static thread_local Game::Pathfinding::NavigationSpatialHash sBulletSpatialHash;
    static const Game::Pathfinding::NavigationSpatialHash* sActiveBulletSpatialHash = nullptr;
    struct BulletWorldStateResource
    {
        int ActiveHaloRockets = 0;
    };

    template<typename Fn>
    static void ForNearbyUnits(Core::ECS::EntityManager& EM,
                               float MinX, float MinY, float MaxX, float MaxY,
                               Fn&& Handler)
    {
        if (sActiveBulletSpatialHash && !sActiveBulletSpatialHash->Empty())
        {
            static thread_local std::vector<int> sCandidateBuffer;
            sActiveBulletSpatialHash->QueryInto(MinX, MinY, MaxX, MaxY, sCandidateBuffer);
            for (int UID : sCandidateBuffer)
                Handler(UID);
        }
        else
        {
            for (int UID : EM.GetEntitiesWith<TransformComponent, UnitComponent>())
                Handler(UID);
        }
    }
    static float SplashFalloff(float Distance,
                               float InnerRadius,
                               float MediumRadius,
                               float OuterRadius)
    {
        if (OuterRadius <= 0.0f) return 1.0f;
        if (InnerRadius <= 0.0f)
            InnerRadius = OuterRadius / 3.0f;
        if (MediumRadius <= 0.0f)
            MediumRadius = OuterRadius * (2.0f / 3.0f);
        InnerRadius = std::min(InnerRadius, OuterRadius);
        MediumRadius = std::clamp(MediumRadius, InnerRadius, OuterRadius);
        if (Distance > OuterRadius) return 0.0f;
        if (Distance <= InnerRadius) return 1.0f;
        if (Distance <= MediumRadius) return 0.5f;
        return 0.25f;
    }

    static float DistanceToSegment(const Core::Math::Vec2& Point,
                                   const Core::Math::Vec2& A,
                                   const Core::Math::Vec2& B)
    {
        const Core::Math::Vec2 Segment = B - A;
        const float LengthSquared = Segment.X * Segment.X + Segment.Y * Segment.Y;
        if (LengthSquared <= 0.0001f)
            return Point.DistanceTo(A);
        const Core::Math::Vec2 FromA = Point - A;
        const float Projection = std::clamp(
            (FromA.X * Segment.X + FromA.Y * Segment.Y) / LengthSquared,
            0.0f, 1.0f);
        return Point.DistanceTo(A + Segment * Projection);
    }

    struct CollisionBounds
    {
        float Left = 0.0f;
        float Top = 0.0f;
        float Right = 0.0f;
        float Bottom = 0.0f;
    };

    static CollisionBounds BoundsForEntity(Core::ECS::EntityManager& EM,
                                           int EntityID,
                                           const TransformComponent& Transform)
    {
        if (const auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            if (Path->HasCollisionBounds)
            {
                return {
                    Transform.Position.X - (float)Path->CollisionLeft,
                    Transform.Position.Y - (float)Path->CollisionUp,
                    Transform.Position.X + (float)Path->CollisionRight + 1.0f,
                    Transform.Position.Y + (float)Path->CollisionDown + 1.0f};
            }
            if (Path->CollisionRadius > 0)
            {
                const float Radius = (float)Path->CollisionRadius;
                return {
                    Transform.Position.X - Radius,
                    Transform.Position.Y - Radius,
                    Transform.Position.X + Radius,
                    Transform.Position.Y + Radius};
            }
        }
        return {Transform.Position.X, Transform.Position.Y,
                Transform.Position.X, Transform.Position.Y};
    }

    static Core::Math::Vec2 AppearAtTargetUnitPosition(
        Core::ECS::EntityManager& EM,
        int TargetEntityID,
        const Core::Math::Vec2& SourcePosition,
        const Core::Math::Vec2& TargetPosition)
    {
        // OpenBW's get_bullet_appear_at_target_pos shrinks the target's inner
        // unit bounds to their central half, then clips the ray from that
        // rectangle's center toward the attacker. Hydralisk Needle Spines use
        // this point for image 504, so spawning at Transform center visibly
        // puts every hit too deep inside large units/buildings.
        float Left = TargetPosition.X - 16.0f;
        float Right = TargetPosition.X + 16.0f;
        float Top = TargetPosition.Y - 16.0f;
        float Bottom = TargetPosition.Y + 16.0f;
        if (const auto* Path =
                EM.GetComponent<PathfindingComponent>(TargetEntityID))
        {
            if (Path->HasCollisionBounds)
            {
                Left = TargetPosition.X - (float)Path->CollisionLeft;
                Right = TargetPosition.X + (float)Path->CollisionRight;
                Top = TargetPosition.Y - (float)Path->CollisionUp;
                Bottom = TargetPosition.Y + (float)Path->CollisionDown;
            }
            else if (Path->CollisionRadius > 0)
            {
                const float Radius = (float)Path->CollisionRadius;
                Left = TargetPosition.X - Radius;
                Right = TargetPosition.X + Radius;
                Top = TargetPosition.Y - Radius;
                Bottom = TargetPosition.Y + Radius;
            }
        }

        const float MarginX = std::floor(std::max(0.0f, Right - Left) * 0.25f);
        const float MarginY = std::floor(std::max(0.0f, Bottom - Top) * 0.25f);
        Left += MarginX;
        Right -= MarginX;
        Top += MarginY;
        Bottom -= MarginY;
        const Core::Math::Vec2 Center{
            Left + std::floor((Right - Left) * 0.5f),
            Top + std::floor((Bottom - Top) * 0.5f)};
        const Core::Math::Vec2 Ray = SourcePosition - Center;
        float T = 1.0f;
        if (Ray.X < -0.0001f && SourcePosition.X < Left)
            T = std::min(T, (Left - Center.X) / Ray.X);
        else if (Ray.X > 0.0001f && SourcePosition.X > Right)
            T = std::min(T, (Right - Center.X) / Ray.X);
        if (Ray.Y < -0.0001f && SourcePosition.Y < Top)
            T = std::min(T, (Top - Center.Y) / Ray.Y);
        else if (Ray.Y > 0.0001f && SourcePosition.Y > Bottom)
            T = std::min(T, (Bottom - Center.Y) / Ray.Y);
        T = std::clamp(T, 0.0f, 1.0f);
        return Center + Ray * T;
    }

    static float DistanceToBounds(const Core::Math::Vec2& Point,
                                  const CollisionBounds& Bounds)
    {
        const float GapX = Point.X < Bounds.Left
            ? Bounds.Left - Point.X
            : (Point.X > Bounds.Right ? Point.X - Bounds.Right : 0.0f);
        const float GapY = Point.Y < Bounds.Top
            ? Bounds.Top - Point.Y
            : (Point.Y > Bounds.Bottom ? Point.Y - Bounds.Bottom : 0.0f);
        return std::sqrt(GapX * GapX + GapY * GapY);
    }

    static float Cross(const Core::Math::Vec2& A,
                       const Core::Math::Vec2& B,
                       const Core::Math::Vec2& C)
    {
        return (B.X - A.X) * (C.Y - A.Y) -
               (B.Y - A.Y) * (C.X - A.X);
    }

    static bool SegmentIntersectsSegment(const Core::Math::Vec2& A,
                                         const Core::Math::Vec2& B,
                                         const Core::Math::Vec2& C,
                                         const Core::Math::Vec2& D)
    {
        constexpr float Epsilon = 0.0001f;
        const float C1 = Cross(A, B, C);
        const float C2 = Cross(A, B, D);
        const float C3 = Cross(C, D, A);
        const float C4 = Cross(C, D, B);
        const auto Between = [=](float V, float Min, float Max)
        {
            return V >= std::min(Min, Max) - Epsilon &&
                   V <= std::max(Min, Max) + Epsilon;
        };
        if (std::abs(C1) <= Epsilon && Between(C.X, A.X, B.X) &&
            Between(C.Y, A.Y, B.Y)) return true;
        if (std::abs(C2) <= Epsilon && Between(D.X, A.X, B.X) &&
            Between(D.Y, A.Y, B.Y)) return true;
        if (std::abs(C3) <= Epsilon && Between(A.X, C.X, D.X) &&
            Between(A.Y, C.Y, D.Y)) return true;
        if (std::abs(C4) <= Epsilon && Between(B.X, C.X, D.X) &&
            Between(B.Y, C.Y, D.Y)) return true;
        return ((C1 > 0.0f) != (C2 > 0.0f)) &&
               ((C3 > 0.0f) != (C4 > 0.0f));
    }

    static float DistanceBetweenSegments(const Core::Math::Vec2& A,
                                         const Core::Math::Vec2& B,
                                         const Core::Math::Vec2& C,
                                         const Core::Math::Vec2& D)
    {
        if (SegmentIntersectsSegment(A, B, C, D))
            return 0.0f;
        return std::min({
            DistanceToSegment(A, C, D),
            DistanceToSegment(B, C, D),
            DistanceToSegment(C, A, B),
            DistanceToSegment(D, A, B)});
    }

    static float DistanceSegmentToBounds(const Core::Math::Vec2& A,
                                         const Core::Math::Vec2& B,
                                         const CollisionBounds& Bounds)
    {
        if (DistanceToBounds(A, Bounds) <= 0.0f ||
            DistanceToBounds(B, Bounds) <= 0.0f)
            return 0.0f;
        const Core::Math::Vec2 TL{Bounds.Left, Bounds.Top};
        const Core::Math::Vec2 TR{Bounds.Right, Bounds.Top};
        const Core::Math::Vec2 BR{Bounds.Right, Bounds.Bottom};
        const Core::Math::Vec2 BL{Bounds.Left, Bounds.Bottom};
        return std::min({
            DistanceBetweenSegments(A, B, TL, TR),
            DistanceBetweenSegments(A, B, TR, BR),
            DistanceBetweenSegments(A, B, BR, BL),
            DistanceBetweenSegments(A, B, BL, TL)});
    }

    static bool IsAirTarget(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
            if (Path->IsAir)
                return true;
        if (auto* Combat = EM.GetComponent<CombatComponent>(EntityID))
            return Combat->MyElevation == Elevation::Air;
        return false;
    }

    static DamageType WeaponDamageKindForTarget(Core::ECS::EntityManager& EM,
                                                const CombatComponent& SourceCombat,
                                                int TargetEntityID)
    {
        if (TargetEntityID >= 0 && IsAirTarget(EM, TargetEntityID))
            return SourceCombat.AirDmgType;
        return SourceCombat.GroundDmgType;
    }

    static bool IsRangedAttack(const CombatComponent& Combat)
    {
        return Combat.IsRanged || Combat.WeaponBeh != WeaponBehavior::Melee;
    }

    static bool CanDarkSwarmDeflect(WeaponBehavior Behavior, SplashType Splash)
    {
        if (Splash != SplashType::None)
            return false;
        return Behavior == WeaponBehavior::FlyDirect ||
               Behavior == WeaponBehavior::Missile ||
               Behavior == WeaponBehavior::Bounce ||
               Behavior == WeaponBehavior::AppearOnTarget;
    }

    static bool IsProtectedByDarkSwarm(Core::ECS::EntityManager& EM, int TargetEntityID)
    {
        if (IsAirTarget(EM, TargetEntityID))
            return false;
        if (EM.GetComponent<BuildingComponent>(TargetEntityID))
            return false;

        auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
        if (Status && Status->HasEffect(StatusEffect::DarkSwarm))
            return true;

        if (!EM.HasAnyComponent<SpellAreaComponent>())
            return false;

        const auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetEntityID);
        if (!TargetTrans)
            return false;
        for (int AreaID : EM.GetEntitiesWith<TransformComponent, SpellAreaComponent>())
        {
            const auto* AreaTrans = EM.GetComponent<TransformComponent>(AreaID);
            const auto* Area = EM.GetComponent<SpellAreaComponent>(AreaID);
            if (AreaTrans && Area && Area->Effect == StatusEffect::DarkSwarm &&
                Area->DurationRemaining > 0.0f &&
                TargetTrans->Position.DistanceTo(AreaTrans->Position) <= Area->Radius)
            {
                return true;
            }
        }
        return false;
    }

    static bool IsInvulnerableByStatus(Core::ECS::EntityManager& EM, int TargetEntityID)
    {
        if (auto* Unit = EM.GetComponent<UnitComponent>(TargetEntityID))
        {
            if (Unit->IsInvincible)
                return true;
        }

        auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
        return Status && Status->HasEffect(StatusEffect::Stasis);
    }

    static void ApplyAcidSporesAt(Core::ECS::EntityManager& EM,
                                  const Core::Math::Vec2& Position,
                                  int SourcePlayerID)
    {
        constexpr float Radius = 64.0f;
        constexpr float SporeDurationSeconds = 50.0f;

        ForNearbyUnits(
            EM, Position.X - Radius, Position.Y - Radius,
            Position.X + Radius, Position.Y + Radius,
            [&](int EntityID)
            {
                auto* Transform = EM.GetComponent<TransformComponent>(EntityID);
                auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
                const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
                if (!Transform || !Unit || !Player || Unit->HitPoints <= 0 ||
                    Player->PlayerID == SourcePlayerID ||
                    CombatSystem::AreAllied(Player->PlayerID, SourcePlayerID) ||
                    Player->IsRescuePassive ||
                    EM.GetComponent<BuildingComponent>(EntityID) ||
                    !IsAirTarget(EM, EntityID) ||
                    IsInvulnerableByStatus(EM, EntityID) ||
                    Transform->Position.DistanceTo(Position) > Radius ||
                    Unit->AcidSporeCount >= 9)
                {
                    return;
                }

                for (float& Timer : Unit->AcidSporeTimers)
                {
                    if (Timer <= 0.0f)
                    {
                        Timer = SporeDurationSeconds;
                        ++Unit->AcidSporeCount;
                        break;
                    }
                }
            });
    }

    static CombatComponent BulletDamageCombat(DamageType DamageKind, bool IsRangedAttack)
    {
        CombatComponent Combat;
        Combat.GroundDmgType = DamageKind;
        Combat.AirDmgType = DamageKind;
        Combat.WeaponBeh = IsRangedAttack ? WeaponBehavior::FlyDirect : WeaponBehavior::Melee;
        Combat.IsRanged = IsRangedAttack;
        Combat.CanTargetGround = true;
        Combat.CanTargetAir = true;
        return Combat;
    }

    // weapons.dat BulletKind values (Game::BulletKind).  Keep these local to
    // the simulation bridge so the resource callback remains a small POD.
    constexpr int kBulletFly = 0;
    constexpr int kBulletFollowTarget = 1;
    constexpr int kBulletAppearAtTargetUnit = 2;
    constexpr int kBulletPersistAtTargetPos = 3;
    constexpr int kBulletAppearAtTargetPos = 4;
    constexpr int kBulletAppearAtSourceUnit = 5;
    constexpr int kBulletSelfDestruct = 6;
    constexpr int kBulletBounce = 7;
    constexpr int kBulletAttackTargetPos = 8;
    constexpr int kBulletExtendToMaxRange = 9;
    constexpr uint32_t kValkyrieHaloRocketImageID = 965u;
    constexpr int kMaxActiveHaloRockets = 80;

    static WeaponBehavior ResolveDatBulletBehavior(
        const CombatComponent& SourceCombat, int BulletType)
    {
        if (BulletType == kBulletSelfDestruct)
            return WeaponBehavior::AppearOnTarget;
        if (BulletType == kBulletAppearAtSourceUnit &&
            SourceCombat.WeaponBeh != WeaponBehavior::Lob)
        {
            return WeaponBehavior::AppearOnTarget;
        }
        // These abstractions intentionally differ from the firing unit's DAT
        // entry and must retain their gameplay implementation: Reaver scarabs
        // are lobbed from the Reaver, close-range line attacks remain melee,
        // and suicide units travel before destroying their source entity.
        if (SourceCombat.WeaponBeh == WeaponBehavior::Lob ||
            SourceCombat.WeaponBeh == WeaponBehavior::Melee ||
            SourceCombat.SelfDestructOnAttack)
        {
            return SourceCombat.WeaponBeh;
        }

        switch (BulletType)
        {
            case kBulletFly: return WeaponBehavior::FlyDirect;
            case kBulletFollowTarget: return WeaponBehavior::Missile;
            case kBulletAppearAtTargetUnit:
            case kBulletAppearAtTargetPos:
                return WeaponBehavior::AppearOnTarget;
            case kBulletPersistAtTargetPos: return WeaponBehavior::Persist;
            case kBulletBounce: return WeaponBehavior::Bounce;
            case kBulletAttackTargetPos:
            case kBulletExtendToMaxRange:
                return WeaponBehavior::FlyDirect;
            case kBulletAppearAtSourceUnit:
            case kBulletSelfDestruct:
            default:
                return SourceCombat.WeaponBeh;
        }
    }

    static Core::Math::Vec2 Rotate(const Core::Math::Vec2& V, float Radians)
    {
        const float C = std::cos(Radians);
        const float S = std::sin(Radians);
        return {V.X * C - V.Y * S, V.X * S + V.Y * C};
    }

    static int DirectionToIScriptDirection(Core::Math::Vec2 Direction)
    {
        if (Direction.Length() <= 0.001f)
            return -1;
        float Angle = std::atan2(Direction.X, -Direction.Y);
        if (Angle < 0.0f)
            Angle += 6.28318530718f;
        return (int)std::lround(Angle * 256.0f / 6.28318530718f) & 0xff;
    }

    const Core::Math::Vec2 kNearTargetOffsets[14] = {
        {-1.0f, -1.0f}, {-51.0f, -31.0f}, {-7.0f, -12.0f},
        {36.0f, 52.0f}, {49.0f, -25.0f}, {-40.0f, 46.0f},
        {4.0f, 19.0f}, {-28.0f, -50.0f}, {-50.0f, 15.0f},
        {54.0f, 13.0f}, {-11.0f, 53.0f}, {-15.0f, 9.0f},
        {19.0f, -50.0f}, {17.0f, -8.0f}};

    const Core::Math::Vec2 kFlightProxyOffsets[8] = {
        {-18.0f, -20.0f}, {-18.0f, 20.0f},
        {-30.0f, -10.0f}, {-30.0f, 10.0f},
        {-42.0f, -28.0f}, {-42.0f, 28.0f},
        {-54.0f, -14.0f}, {-54.0f, 14.0f}};
}

int BulletSystem::SpawnBullet(Core::ECS::EntityManager& EM,
                               const CombatComponent& SourceCombat,
                               const TransformComponent& SourceTrans,
                               const PlayerComponent& SourcePlayer,
                               int SourceEntityID,
                               int BaseDamage,
                               int DamageUpgradeBonus,
                               float RangePixels,
                               int TargetEntityID,
                               const Core::Math::Vec2& TargetPosIn,
                               int HitCount,
                               bool ForceMissAtLaunch)
{
    if (BaseDamage <= 0) return -1;

    int BulletID = EM.CreateEntity();
    auto& BulletTrans = EM.AddComponent<TransformComponent>(BulletID);
    auto& BulletComp = EM.AddComponent<BulletComponent>(BulletID);
    auto& BulletPlayer = EM.AddComponent<PlayerComponent>(BulletID);

    BulletPlayer = SourcePlayer;
    BulletComp.SourceEntityID = SourceEntityID;
    BulletComp.Behavior = SourceCombat.WeaponBeh;
    BulletComp.Splash = SourceCombat.Splash;
    BulletComp.SplashInnerRadius = SourceCombat.SplashInnerRadius;
    BulletComp.SplashMediumRadius = SourceCombat.SplashMediumRadius;
    BulletComp.SplashRadius = SourceCombat.SplashRadius;
    BulletComp.BaseDamage = BaseDamage;
    BulletComp.DamageKind = WeaponDamageKindForTarget(EM, SourceCombat, TargetEntityID);
    BulletComp.DamageUpgradeBonus = DamageUpgradeBonus;
    BulletComp.HitCount = std::max(1, HitCount);
    BulletComp.IsRangedAttack = IsRangedAttack(SourceCombat);
    BulletComp.MissedAtLaunch = ForceMissAtLaunch ||
        (BulletComp.IsRangedAttack && TargetEntityID >= 0 &&
         CanDarkSwarmDeflect(SourceCombat.WeaponBeh, SourceCombat.Splash) &&
         IsProtectedByDarkSwarm(EM, TargetEntityID));
    BulletComp.SelfDestructSourceOnHit = SourceCombat.SelfDestructOnAttack;
    BulletComp.TargetEntityID = TargetEntityID;
    BulletComp.SplashLineStart = SourceTrans.Position;
    BulletComp.CanTargetGround = SourceCombat.CanTargetGround;
    BulletComp.CanTargetAir = SourceCombat.CanTargetAir;
    Core::Math::Vec2 TargetPos = TargetPosIn;
    if (TargetEntityID >= 0)
    {
        auto* TTarget = EM.GetComponent<TransformComponent>(TargetEntityID);
        if (TTarget) TargetPos = TTarget->Position;
    }

    std::optional<VisualSpec> Visual;
    if (sVisualResolver)
    {
        if (const auto* SourceUnit = EM.GetComponent<UnitComponent>(SourceEntityID))
            Visual = sVisualResolver(SourceUnit->ChkUnitID, IsAirTarget(EM, TargetEntityID));
    }
    if (Visual && Visual->ImageID == kValkyrieHaloRocketImageID)
    {
        auto& BulletState = EM.Context().GetOrCreateResource<BulletWorldStateResource>();
        if (BulletState.ActiveHaloRockets >= kMaxActiveHaloRockets)
        {
            EM.DestroyEntity(BulletID);
            return -1;
        }
        ++BulletState.ActiveHaloRockets;
    }
    if (Visual)
    {
        BulletComp.Behavior = ResolveDatBulletBehavior(
            SourceCombat, Visual->BulletType);
        BulletComp.PlayImpactAnimation = Visual->HasImpactAnimation;
        BulletComp.AppliesAcidSpores = Visual->AppliesAcidSpores;
        BulletComp.DamageDrivenByIScript =
            SourceCombat.Splash == SplashType::Line &&
            Visual->BulletType == kBulletExtendToMaxRange &&
            Visual->HasInitAnimation;
        if (Visual->Speed > 0.0f)
            BulletComp.Speed = Visual->Speed;
    }
    if (Visual && Visual->BulletType == kBulletAppearAtTargetUnit &&
        TargetEntityID >= 0)
    {
        TargetPos = AppearAtTargetUnitPosition(
            EM, TargetEntityID, SourceTrans.Position, TargetPos);
    }

    const int VisualProjectileCount =
        (!mSuppressProjectileExpansion && Visual)
            ? std::clamp(Visual->ProjectileCount, 1, std::max(1, HitCount))
            : 1;
    const int HitsPerProjectile = std::max(1, HitCount) / VisualProjectileCount;
    const int ExtraHits = std::max(1, HitCount) % VisualProjectileCount;
    BulletComp.HitCount = HitsPerProjectile + (ExtraHits > 0 ? 1 : 0);

    Core::Math::Vec2 Aim = TargetPos - SourceTrans.Position;
    const Core::Math::Vec2 AimDirection = Aim.Length() > 0.001f
        ? Aim.Normalized()
        : Core::Math::Vec2{1.0f, 0.0f};

    const bool ExtendsToMaximumRange =
        (Visual && Visual->BulletType == kBulletExtendToMaxRange) ||
        (SourceCombat.Splash == SplashType::Line &&
         SourceCombat.WeaponBeh == WeaponBehavior::AppearOnTarget);
    const float LineLength = ExtendsToMaximumRange
        ? RangePixels + 20.0f
        : std::max(RangePixels, Aim.Length());
    BulletComp.SplashLineEnd =
        SourceTrans.Position + AimDirection * LineLength;

    if (Visual && Visual->BulletType == kBulletAttackTargetPos)
    {
        int& Index = mNextNearTargetPositionIndex[SourceEntityID];
        BulletComp.TargetPositionOffset = kNearTargetOffsets[Index % 14];
        TargetPos = TargetPos + BulletComp.TargetPositionOffset;
        Index = (Index + 1) % 14;
        BulletComp.ImpactAtProjectilePosition = true;
        BulletComp.FollowTargetPositionOffset = true;
    }
    else if (ExtendsToMaximumRange)
    {
        TargetPos = BulletComp.SplashLineEnd;
        BulletComp.ImpactAtProjectilePosition = true;
    }

    if (BulletComp.MissedAtLaunch)
    {
        TargetPos = TargetPos - AimDirection * 30.0f;
        BulletComp.ImpactAtProjectilePosition = true;
    }
    bool SpawnAtTarget =
        BulletComp.Behavior == WeaponBehavior::AppearOnTarget ||
        BulletComp.Behavior == WeaponBehavior::Persist;
    if (Visual && Visual->BulletType >= 0)
    {
        SpawnAtTarget =
            Visual->BulletType == kBulletAppearAtTargetUnit ||
            Visual->BulletType == kBulletPersistAtTargetPos ||
            Visual->BulletType == kBulletAppearAtTargetPos ||
            Visual->BulletType == kBulletSelfDestruct;
    }

    BulletTrans.Position = SpawnAtTarget ? TargetPos : SourceTrans.Position;
    if (!SpawnAtTarget && Visual)
    {
        BulletTrans.Position = BulletTrans.Position +
            AimDirection * Visual->ForwardOffset;
        BulletTrans.Position.Y -= Visual->UpwardOffset;
    }

    BulletComp.TargetPos = TargetPos;
    if (BulletComp.Behavior == WeaponBehavior::Bounce)
    {
        BulletComp.RemainingBounces = 3;
        BulletComp.BounceDamageDivisor = 1;
    }

    Core::Math::Vec2 Dir = TargetPos - BulletTrans.Position;
    float Dist = Dir.Length();
    Core::Math::Vec2 LaunchDirection = Dist > 0.001f
        ? Dir.Normalized()
        : AimDirection;
    if (Visual && std::abs(Visual->HeadingOffsetRadians) > 0.0001f)
    {
        bool& Clockwise = mNextHeadingOffsetClockwise[SourceEntityID];
        const float SignedOffset = Clockwise
            ? Visual->HeadingOffsetRadians
            : -Visual->HeadingOffsetRadians;
        Clockwise = !Clockwise;
        LaunchDirection = Rotate(LaunchDirection, SignedOffset);
    }

    switch (BulletComp.Behavior)
    {
        case WeaponBehavior::Melee:
        {
            BulletComp.LifetimeRemaining = 0.0f;
            break;
        }
        case WeaponBehavior::AppearOnTarget:
        {
            BulletComp.MaxLifetime = 0.35f;
            BulletComp.LifetimeRemaining = 0.35f;
            BulletComp.Velocity = LaunchDirection * BulletComp.Speed;
            break;
        }
        case WeaponBehavior::FlyDirect:
        case WeaponBehavior::Missile:
        case WeaponBehavior::Bounce:
        {
            float BulletSpeed = Visual && Visual->Speed > 0.0f
                ? Visual->Speed
                : std::max(300.0f, Dist / 0.5f);
            BulletComp.Speed = BulletSpeed;
            BulletComp.Velocity = LaunchDirection * BulletSpeed;
            const float TravelLifetime =
                (Dist > 0.0f) ? (Dist / BulletSpeed) + 0.2f : 0.5f;
            BulletComp.MaxLifetime =
                Visual && Visual->LifetimeSeconds > 0.0f
                    ? Visual->LifetimeSeconds
                    : TravelLifetime;
            BulletComp.LifetimeRemaining = BulletComp.MaxLifetime;
            break;
        }
        case WeaponBehavior::Lob:
        {
            float LobSpeed = std::max(300.0f, Dist / 0.8f);
            BulletComp.Speed = LobSpeed;
            if (Dist > 0.0f)
                BulletComp.Velocity = Dir.Normalized() * LobSpeed;
            else
                BulletComp.Velocity = {LobSpeed, 0.0f};
            BulletComp.MaxLifetime = (Dist > 0.0f) ? (Dist / LobSpeed) : 0.5f;
            BulletComp.LifetimeRemaining = BulletComp.MaxLifetime;
            break;
        }
        case WeaponBehavior::Persist:
        {
            BulletComp.MaxLifetime =
                Visual && Visual->LifetimeSeconds > 0.0f
                    ? Visual->LifetimeSeconds
                    : 2.0f;
            BulletComp.LifetimeRemaining = BulletComp.MaxLifetime;
            const int LifetimeFrames = std::max(
                1, (int)std::lround(BulletComp.MaxLifetime * 24.0f));
            int FramesUntilFirstTick = LifetimeFrames % 7;
            if (FramesUntilFirstTick == 0)
                FramesUntilFirstTick = 7;
            BulletComp.DamageTickAccumulator =
                (float)(7 - FramesUntilFirstTick) / 24.0f;
            BulletComp.Velocity = {0.0f, 0.0f};
            break;
        }
    }

    if (Visual && Visual->ImageID != UINT32_MAX &&
        BulletComp.Behavior != WeaponBehavior::Melee)
    {
        BulletComp.AwaitingLaunchSignal = Visual->HasInitAnimation;
        auto& Render = EM.AddComponent<RenderComponent>(BulletID);
        Render.ImageID = Visual->ImageID;
        Render.TotalFrames = 1;
        Render.Layer = RenderLayer::Effects;

        auto& Direction = EM.AddComponent<PathfindingComponent>(BulletID);
        Direction.CurrentVelocity = BulletComp.Velocity;
        Direction.FacingAngle = std::atan2(BulletComp.Velocity.Y, BulletComp.Velocity.X);
        Direction.IsAir = true;
    }

    if (Visual && Visual->FlightProxyImageID != UINT32_MAX &&
        BulletComp.Behavior != WeaponBehavior::Melee)
    {
        const int ProxyID = EM.CreateEntity();
        auto& ProxyTrans = EM.AddComponent<TransformComponent>(ProxyID);
        auto& ProxyRender = EM.AddComponent<RenderComponent>(ProxyID);
        auto& ProxyEffect = EM.AddComponent<EffectComponent>(ProxyID);
        auto& ProxyUnit = EM.AddComponent<UnitComponent>(ProxyID);

        int& OffsetIndex = mNextFlightProxyOffsetIndex[SourceEntityID];
        ProxyEffect.Offset =
            kFlightProxyOffsets[OffsetIndex %
                (int)(sizeof(kFlightProxyOffsets) /
                      sizeof(kFlightProxyOffsets[0]))];
        ++OffsetIndex;

        ProxyTrans.Position = BulletTrans.Position + ProxyEffect.Offset;
        ProxyRender.ImageID = Visual->FlightProxyImageID;
        ProxyRender.TotalFrames = 1;
        ProxyRender.Layer = RenderLayer::Effects;
        ProxyRender.ZOrder = 1;

        ProxyEffect.ParentEntityID = BulletID;
        ProxyEffect.FollowParent = true;
        ProxyEffect.LifetimeRemaining =
            std::max(0.25f, BulletComp.MaxLifetime + 0.25f);
        ProxyEffect.InitialAnimation = Game::IScriptAnims::Anim_Init;
        ProxyEffect.DestroyWhenAnimationCompletes = false;
        ProxyEffect.DrawAbove = true;
        ProxyEffect.IScriptDirection =
            DirectionToIScriptDirection(BulletComp.Velocity);

        ProxyUnit.ChkUnitID = -1;
        ProxyUnit.HitPoints = 0;
        ProxyUnit.MaxHitPoints = 1;
        ProxyUnit.SupplyCost = 0;
        if (const auto* ExistingBulletPlayer =
                EM.GetComponent<PlayerComponent>(BulletID))
        {
            EM.AddComponent<PlayerComponent>(ProxyID) = *ExistingBulletPlayer;
        }
    }

    // weapons.dat bullet_count creates separate bullets. Split the already
    // selected attack hit-count across them so twin missiles get independent
    // trajectories and armor applications without multiplying total damage
    // (notably, Valkyrie's aggregate eight-hit abstraction stays eight hits).
    if (VisualProjectileCount > 1)
    {
        const bool PreviousSuppression = mSuppressProjectileExpansion;
        mSuppressProjectileExpansion = true;
        for (int ProjectileIndex = 1;
             ProjectileIndex < VisualProjectileCount; ++ProjectileIndex)
        {
            const int ProjectileHits = HitsPerProjectile +
                (ProjectileIndex < ExtraHits ? 1 : 0);
            SpawnBullet(EM, SourceCombat, SourceTrans, SourcePlayer,
                        SourceEntityID, BaseDamage, DamageUpgradeBonus,
                        RangePixels, TargetEntityID, TargetPosIn,
                        ProjectileHits, ForceMissAtLaunch);
        }
        mSuppressProjectileExpansion = PreviousSuppression;
    }

    if (!mSuppressProjectileExpansion && Visual &&
        Visual->BulletType == kBulletSelfDestruct &&
        SourceEntityID >= 0 && EM.IsAlive(SourceEntityID))
    {
        if (auto* LaunchBullet = EM.GetComponent<BulletComponent>(BulletID))
            LaunchBullet->SelfDestructSourceOnHit = false;
        if (auto* SourceUnit = EM.GetComponent<UnitComponent>(SourceEntityID))
            SourceUnit->HitPoints = 0;
        RecordUnitDeath(EM, SourceEntityID, -1, false);
        EM.DestroyEntity(SourceEntityID);
    }

    return BulletID;
}

void BulletSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();

    auto& SharedSpatial = Game::Pathfinding::SpatialIndexResource::AcquireUpToDate(EM);
    sActiveBulletSpatialHash = &SharedSpatial.AllUnitsHash;
    struct BulletSpatialScope
    {
        ~BulletSpatialScope() { sActiveBulletSpatialHash = nullptr; }
    } Scope;

    auto& BulletState = EM.Context().GetOrCreateResource<BulletWorldStateResource>();
    BulletState.ActiveHaloRockets = 0;
    static thread_local std::vector<int> sBulletsScratch;
    const auto& CachedBullets = EM.GetEntitiesWithCached<TransformComponent, BulletComponent>();
    sBulletsScratch.assign(CachedBullets.begin(), CachedBullets.end());
    for (int ID : sBulletsScratch)
    {
        if (!EM.IsAlive(ID))
            continue;
        const auto* Render = EM.GetComponent<RenderComponent>(ID);
        if (Render && Render->ImageID == kValkyrieHaloRocketImageID)
            ++BulletState.ActiveHaloRockets;
        UpdateBullet(EM, ID, DeltaTime);
    }

    for (auto It = mNextHeadingOffsetClockwise.begin(); It != mNextHeadingOffsetClockwise.end(); )
    {
        if (!EM.IsAlive(It->first))
            It = mNextHeadingOffsetClockwise.erase(It);
        else
            ++It;
    }
    for (auto It = mNextNearTargetPositionIndex.begin(); It != mNextNearTargetPositionIndex.end(); )
    {
        if (!EM.IsAlive(It->first))
            It = mNextNearTargetPositionIndex.erase(It);
        else
            ++It;
    }
    for (auto It = mNextFlightProxyOffsetIndex.begin(); It != mNextFlightProxyOffsetIndex.end(); )
    {
        if (!EM.IsAlive(It->first))
            It = mNextFlightProxyOffsetIndex.erase(It);
        else
            ++It;
    }
}

bool BulletSystem::ResolveIScriptDamageFrame(int BulletEntityID)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* Bullet = EM.GetComponent<BulletComponent>(BulletEntityID);
    auto* BulletTrans = EM.GetComponent<TransformComponent>(BulletEntityID);
    auto* BulletPlayer = EM.GetComponent<PlayerComponent>(BulletEntityID);
    if (!Bullet || !BulletTrans || !Bullet->DamageDrivenByIScript)
        return false;

    // The opcode remains authoritative even for an attack that missed at
    // launch; consuming it without damage prevents the endpoint fallback from
    // turning the miss back into a hit.
    if (Bullet->MissedAtLaunch)
        return true;

    const int SourcePlayerID = BulletPlayer ? BulletPlayer->PlayerID : -1;
    const float HitRadius =
        (Bullet->SplashInnerRadius > 0.0f
             ? Bullet->SplashInnerRadius
             : Bullet->SplashRadius) * 32.0f;
    if (HitRadius <= 0.0f)
        return true;

    const float MinX = BulletTrans->Position.X - HitRadius - 32.0f;
    const float MaxX = BulletTrans->Position.X + HitRadius + 32.0f;
    const float MinY = BulletTrans->Position.Y - HitRadius - 32.0f;
    const float MaxY = BulletTrans->Position.Y + HitRadius + 32.0f;

    ForNearbyUnits(EM, MinX, MinY, MaxX, MaxY, [&](int TargetID)
    {
        if (TargetID == Bullet->SourceEntityID ||
            std::find(Bullet->IScriptHitEntityIDs.begin(),
                      Bullet->IScriptHitEntityIDs.end(), TargetID) !=
                Bullet->IScriptHitEntityIDs.end())
        {
            return;
        }

        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
        auto* TargetPlayer = EM.GetComponent<PlayerComponent>(TargetID);
        if (!TargetTrans || !TargetUnit || TargetUnit->HitPoints <= 0 ||
            !TargetPlayer || TargetPlayer->PlayerID == SourcePlayerID ||
            (CombatSystem::AreAllied(TargetPlayer->PlayerID, SourcePlayerID) && Bullet->TargetEntityID != TargetID) ||
            TargetPlayer->IsRescuePassive ||
            IsProtectedResourceNode(EM, TargetID) ||
            IsInvulnerableByStatus(EM, TargetID))
        {
            return;
        }

        const bool TargetIsAir = IsAirTarget(EM, TargetID);
        if (TargetIsAir ? !Bullet->CanTargetAir : !Bullet->CanTargetGround)
            return;
        if (DistanceToBounds(BulletTrans->Position,
                             BoundsForEntity(EM, TargetID, *TargetTrans)) >
            HitRadius)
        {
            return;
        }

        Bullet->IScriptHitEntityIDs.push_back(TargetID);
        const CombatComponent DamageCombat =
            BulletDamageCombat(Bullet->DamageKind, Bullet->IsRangedAttack);
        CombatSystem::ApplyDamageRaw(
            EM, TargetID, DamageCombat, Bullet->SourceEntityID,
            (Bullet->BaseDamage + Bullet->DamageUpgradeBonus) * 256,
            std::max(1, Bullet->HitCount), false);
    });
    return true;
}

void BulletSystem::UpdateBullet(Core::ECS::EntityManager& EM, int BulletID, float DeltaTime)
{
    auto* Bullet = EM.GetComponent<BulletComponent>(BulletID);
    auto* Trans = EM.GetComponent<TransformComponent>(BulletID);
    if (!Bullet || !Trans) return;

    const auto SyncRenderDirection = [&]()
    {
        if (auto* Direction = EM.GetComponent<PathfindingComponent>(BulletID))
        {
            Direction->CurrentVelocity = Bullet->Velocity;
            if (Bullet->Velocity.Length() > 0.001f)
            {
                Direction->FacingAngle =
                    std::atan2(Bullet->Velocity.Y, Bullet->Velocity.X);
            }
        }
    };
    if (Bullet->AwaitingLaunchSignal)
    {
        SyncRenderDirection();
        return;
    }

    const bool UsesLifetime = Bullet->Behavior != WeaponBehavior::Bounce;
    const float ActiveDeltaTime = UsesLifetime
        ? std::min(DeltaTime, std::max(0.0f, Bullet->LifetimeRemaining))
        : DeltaTime;
    if (UsesLifetime)
        Bullet->LifetimeRemaining -= DeltaTime;

    switch (Bullet->Behavior)
    {
        case WeaponBehavior::Melee:
        {
            BulletHit(EM, BulletID);
            return;
        }
        case WeaponBehavior::AppearOnTarget:
        {
            if (!Bullet->HasHit && Bullet->LifetimeRemaining <= Bullet->MaxLifetime - 0.01f)
            {
                BulletHit(EM, BulletID);
            }
            if (!EM.IsAlive(BulletID))
                return;
            Bullet = EM.GetComponent<BulletComponent>(BulletID);
            if (!Bullet || Bullet->LifetimeRemaining <= 0.0f)
            {
                EM.DestroyEntity(BulletID);
                return;
            }
            SyncRenderDirection();
            return;
        }
        case WeaponBehavior::Persist:
        {
            // OpenBW's persist-at-target bullets invoke bullet_hit every seven
            // simulation frames.  Tying damage to Update() made storms and
            // other persistent visuals deal more damage at higher frame rates.
            constexpr float DamageTickSeconds = 7.0f / 24.0f;
            Bullet->DamageTickAccumulator += ActiveDeltaTime;
            while (Bullet->DamageTickAccumulator + 0.000001f >=
                   DamageTickSeconds)
            {
                Bullet->DamageTickAccumulator = std::max(
                    0.0f,
                    Bullet->DamageTickAccumulator - DamageTickSeconds);
                int SourcePlayerID = -1;
                if (auto* P = EM.GetComponent<PlayerComponent>(BulletID))
                    SourcePlayerID = P->PlayerID;

                ApplySplashDamage(EM, Trans->Position,
                                  Bullet->SplashInnerRadius * 32.0f,
                                  Bullet->SplashMediumRadius * 32.0f,
                                  Bullet->SplashRadius * 32.0f,
                                  Bullet->BaseDamage, Bullet->DamageKind,
                                  Bullet->DamageUpgradeBonus, SourcePlayerID,
                                  Bullet->SourceEntityID, Bullet->Splash,
                                  Bullet->CanTargetGround, Bullet->CanTargetAir,
                                  Bullet->IsRangedAttack, Bullet->HitCount, -1,
                                  Bullet->SplashLineStart, Bullet->SplashLineEnd,
                                  true);
            }
            if (Bullet->LifetimeRemaining <= 0.0f)
            {
                SpawnImpactVisual(EM, BulletID, Trans->Position);
                EM.DestroyEntity(BulletID);
            }
            else
                SyncRenderDirection();
            return;
        }
        case WeaponBehavior::FlyDirect:
        case WeaponBehavior::Missile:
        case WeaponBehavior::Bounce:
        case WeaponBehavior::Lob:
        {
            Core::Math::Vec2 TargetPos = Bullet->TargetPos;
            if ((Bullet->Behavior == WeaponBehavior::Missile ||
                 Bullet->Behavior == WeaponBehavior::Bounce ||
                 Bullet->FollowTargetPositionOffset) &&
                !Bullet->MissedAtLaunch &&
                Bullet->TargetEntityID >= 0)
            {
                auto* TTarget = EM.GetComponent<TransformComponent>(Bullet->TargetEntityID);
                if (TTarget)
                {
                    TargetPos = TTarget->Position +
                        Bullet->TargetPositionOffset;
                    Bullet->TargetPos = TargetPos;
                }
            }

            Core::Math::Vec2 ToTarget = TargetPos - Trans->Position;
            float Dist = ToTarget.Length();

            const float TravelDistance = Bullet->Speed * ActiveDeltaTime;
            if (Dist <= std::max(1.0f, TravelDistance))
            {
                Trans->Position = TargetPos;
                if (Dist > 0.001f)
                    Bullet->Velocity = ToTarget.Normalized() * Bullet->Speed;
                SyncRenderDirection();
                if (Bullet->DamageDrivenByIScript)
                {
                    if (!Bullet->ReachedIScriptDamageEndpoint)
                    {
                        // The controller moves before AnimationSystem runs.
                        // Hold it at the endpoint long enough for the current
                        // grdsprol/dogrddamage loop to author the final visible
                        // spine and its matching local hit before destruction.
                        Bullet->ReachedIScriptDamageEndpoint = true;
                        Bullet->IScriptEndpointGraceRemaining = 2.0f / 24.0f;
                        return;
                    }
                    Bullet->IScriptEndpointGraceRemaining -= DeltaTime;
                    if (Bullet->IScriptEndpointGraceRemaining > 0.0f)
                        return;
                }
                BulletHit(EM, BulletID);
                return;
            }

            if (UsesLifetime && Bullet->LifetimeRemaining <= 0.0f)
            {
                // DAT lifetime expiration runs the projectile Death script at
                // its current position; it must not visually teleport to a
                // still-moving target.
                Bullet->ImpactAtProjectilePosition = true;
                if (Dist > 0.001f && ActiveDeltaTime > 0.0f)
                {
                    Bullet->Velocity =
                        ToTarget.Normalized() * Bullet->Speed;
                    Trans->Position = Trans->Position +
                        Bullet->Velocity * ActiveDeltaTime;
                }
                SyncRenderDirection();
                SpawnImpactVisual(EM, BulletID, Trans->Position);
                EM.DestroyEntity(BulletID);
                return;
            }

            Core::Math::Vec2 Dir = ToTarget.Normalized();
            Bullet->Velocity = Dir * Bullet->Speed;
            Trans->Position = Trans->Position +
                Bullet->Velocity * ActiveDeltaTime;
            SyncRenderDirection();
            break;
        }
        default: break;
    }
}

void BulletSystem::BulletHit(Core::ECS::EntityManager& EM, int BulletID)
{
    auto* Bullet = EM.GetComponent<BulletComponent>(BulletID);
    auto* Trans = EM.GetComponent<TransformComponent>(BulletID);
    auto* BPlayer = EM.GetComponent<PlayerComponent>(BulletID);
    if (!Bullet || !Trans || Bullet->HasHit) return;

    Bullet->HasHit = true;

    int SourcePlayerID = BPlayer ? BPlayer->PlayerID : -1;

    Core::Math::Vec2 ImpactPos = Trans->Position;
    int DirectHitID = Bullet->TargetEntityID;

    if (DirectHitID >= 0 &&
        (Bullet->Behavior == WeaponBehavior::Melee ||
         Bullet->Behavior == WeaponBehavior::AppearOnTarget) &&
        !Bullet->ImpactAtProjectilePosition)
    {
        auto* TTarget = EM.GetComponent<TransformComponent>(DirectHitID);
        if (TTarget) ImpactPos = TTarget->Position;
    }

    const bool HasSplash =
        Bullet->Splash != SplashType::None && Bullet->SplashRadius > 0.0f;

    // This geometric impact is the simulation's single damage authority.
    // A projectile Death IScript may emit DoMissileDamage for presentation,
    // but the independent impact entity has no pending Combat attack and must
    // never apply the weapon a second time.

    if (Bullet->Behavior == WeaponBehavior::Bounce)
    {
        bool DirectHitResolved = false;
        if (DirectHitID >= 0)
        {
            auto* TargetUnit = EM.GetComponent<UnitComponent>(DirectHitID);
            const bool TargetIsAir = IsAirTarget(EM, DirectHitID);
            if (TargetUnit && TargetUnit->HitPoints > 0 &&
                !IsProtectedResourceNode(EM, DirectHitID) &&
                !IsInvulnerableByStatus(EM, DirectHitID) &&
                (TargetIsAir ? Bullet->CanTargetAir : Bullet->CanTargetGround))
            {
                DirectHitResolved = true;
                if (!Bullet->MissedAtLaunch)
                {
                    const CombatComponent BulletCombat =
                        BulletDamageCombat(Bullet->DamageKind, Bullet->IsRangedAttack);
                    const int RawDamage =
                        (Bullet->BaseDamage + Bullet->DamageUpgradeBonus) * 256 /
                        std::max(1, Bullet->BounceDamageDivisor);
                    CombatSystem::ApplyDamageRaw(
                        EM, DirectHitID, BulletCombat, Bullet->SourceEntityID,
                        RawDamage, 1, false);
                }
            }
        }

        --Bullet->RemainingBounces;
        int NextTargetID = -1;
        float BestLeft = 0.0f;
        float BestTop = 0.0f;
        const auto* BounceOwner =
            EM.GetComponent<UnitComponent>(Bullet->SourceEntityID);
        const bool OwnerCanRetarget =
            Bullet->SourceEntityID >= 0 &&
            EM.IsAlive(Bullet->SourceEntityID) && BounceOwner &&
            BounceOwner->HitPoints > 0;
        if (DirectHitResolved && Bullet->RemainingBounces > 0 &&
            OwnerCanRetarget)
        {
            ForNearbyUnits(
                EM, ImpactPos.X - 128.0f, ImpactPos.Y - 128.0f,
                ImpactPos.X + 128.0f, ImpactPos.Y + 128.0f,
                [&](int CandidateID)
            {
                if (CandidateID == DirectHitID ||
                    CandidateID == Bullet->PreviousBounceTargetEntityID ||
                    CandidateID == Bullet->SourceEntityID)
                {
                    return;
                }

                auto* CandidateUnit = EM.GetComponent<UnitComponent>(CandidateID);
                auto* CandidateTrans = EM.GetComponent<TransformComponent>(CandidateID);
                auto* CandidatePlayer = EM.GetComponent<PlayerComponent>(CandidateID);
                if (!CandidateUnit || CandidateUnit->HitPoints <= 0 ||
                    !CandidateTrans || !CandidatePlayer ||
                    CandidatePlayer->PlayerID == SourcePlayerID ||
                    CombatSystem::AreAllied(CandidatePlayer->PlayerID, SourcePlayerID) ||
                    CandidatePlayer->IsRescuePassive ||
                    IsProtectedResourceNode(EM, CandidateID) ||
                    IsInvulnerableByStatus(EM, CandidateID))
                {
                    return;
                }

                const bool CandidateIsAir = IsAirTarget(EM, CandidateID);
                if ((CandidateIsAir && !Bullet->CanTargetAir) ||
                    (!CandidateIsAir && !Bullet->CanTargetGround))
                {
                    return;
                }

                if (const auto* Cloak =
                        EM.GetComponent<CloakComponent>(CandidateID);
                    Cloak && (Cloak->IsCloaked || Cloak->IsBurrowed))
                {
                    if (SourcePlayerID < 0 ||
                        !CombatSystem::IsTargetDetectedForPlayer(EM, CandidateID, SourcePlayerID))
                    {
                        return;
                    }
                }
                if (SourcePlayerID >= 0 && SourcePlayerID < 8)
                {
                    if (const auto* Vision =
                            EM.GetComponent<VisionComponent>(CandidateID);
                        Vision && !Vision->VisibleToPlayer[SourcePlayerID])
                    {
                        return;
                    }
                }

                float Left = CandidateTrans->Position.X - 16.0f;
                float Right = CandidateTrans->Position.X + 16.0f;
                float Top = CandidateTrans->Position.Y - 16.0f;
                float Bottom = CandidateTrans->Position.Y + 16.0f;
                if (const auto* Path =
                        EM.GetComponent<PathfindingComponent>(CandidateID))
                {
                    if (Path->HasCollisionBounds)
                    {
                        Left = CandidateTrans->Position.X -
                               (float)Path->CollisionLeft;
                        Right = CandidateTrans->Position.X +
                                (float)Path->CollisionRight;
                        Top = CandidateTrans->Position.Y -
                              (float)Path->CollisionUp;
                        Bottom = CandidateTrans->Position.Y +
                                 (float)Path->CollisionDown;
                    }
                    else if (Path->CollisionRadius > 0)
                    {
                        const float Radius = (float)Path->CollisionRadius;
                        Left = CandidateTrans->Position.X - Radius;
                        Right = CandidateTrans->Position.X + Radius;
                        Top = CandidateTrans->Position.Y - Radius;
                        Bottom = CandidateTrans->Position.Y + Radius;
                    }
                }

                if (Right < ImpactPos.X - 96.0f ||
                    Left > ImpactPos.X + 96.0f ||
                    Bottom < ImpactPos.Y - 96.0f ||
                    Top > ImpactPos.Y + 96.0f)
                {
                    return;
                }

                if (NextTargetID < 0 || Left < BestLeft ||
                    (Left == BestLeft &&
                     (Top < BestTop ||
                      (Top == BestTop && CandidateID < NextTargetID))))
                {
                    NextTargetID = CandidateID;
                    BestLeft = Left;
                    BestTop = Top;
                }
            });
        }

        if (NextTargetID >= 0)
        {
            auto* NextTrans = EM.GetComponent<TransformComponent>(NextTargetID);
            if (NextTrans)
            {
                Bullet->PreviousBounceTargetEntityID = DirectHitID;
                Bullet->TargetEntityID = NextTargetID;
                Bullet->TargetPos = NextTrans->Position;
                Bullet->BounceDamageDivisor *= 3;
                Bullet->HasHit = false;
                Bullet->LifetimeRemaining = 4.0f;
                Bullet->VisualAnimationOverride =
                    Game::IScriptAnims::Anim_SpecialState1;
                Trans->Position = ImpactPos;

                const Core::Math::Vec2 ToTarget =
                    NextTrans->Position - ImpactPos;
                const float Distance = ToTarget.Length();
                Bullet->Speed = std::max(300.0f, Bullet->Speed);
                Bullet->Velocity = Distance > 0.0f
                    ? ToTarget.Normalized() * Bullet->Speed
                    : Core::Math::Vec2{Bullet->Speed, 0.0f};
                return;
            }
        }

        SpawnImpactVisual(EM, BulletID, ImpactPos);
        EM.DestroyEntity(BulletID);
        return;
    }

    SpawnImpactVisual(EM, BulletID, ImpactPos);

    if (DirectHitID >= 0 && !HasSplash)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(DirectHitID);
        if (TargetUnit && TargetUnit->HitPoints > 0 &&
            !IsProtectedResourceNode(EM, DirectHitID))
        {
            if (!IsInvulnerableByStatus(EM, DirectHitID) &&
                !Bullet->MissedAtLaunch)
            {
                const CombatComponent BulletCombat =
                    BulletDamageCombat(Bullet->DamageKind, Bullet->IsRangedAttack);
                CombatSystem::ApplyDamageRaw(
                    EM, DirectHitID, BulletCombat, Bullet->SourceEntityID,
                    (Bullet->BaseDamage + Bullet->DamageUpgradeBonus) * 256,
                    Bullet->HitCount, false);
            }
        }
    }

    if (HasSplash && !Bullet->DamageDrivenByIScript)
    {
        ApplySplashDamage(EM, ImpactPos,
                          Bullet->SplashInnerRadius * 32.0f,
                          Bullet->SplashMediumRadius * 32.0f,
                          Bullet->SplashRadius * 32.0f,
                          Bullet->BaseDamage, Bullet->DamageKind,
                          Bullet->DamageUpgradeBonus, SourcePlayerID,
                          Bullet->SourceEntityID, Bullet->Splash,
                          Bullet->CanTargetGround, Bullet->CanTargetAir,
                          Bullet->IsRangedAttack, Bullet->HitCount,
                          DirectHitID, Bullet->SplashLineStart,
                          Bullet->SplashLineEnd);
    }

    if (Bullet->AppliesAcidSpores && !Bullet->MissedAtLaunch)
        ApplyAcidSporesAt(EM, ImpactPos, SourcePlayerID);

    if (Bullet->SelfDestructSourceOnHit &&
        Bullet->SourceEntityID >= 0 &&
        EM.IsAlive(Bullet->SourceEntityID))
    {
        if (auto* SourceUnit = EM.GetComponent<UnitComponent>(Bullet->SourceEntityID))
            SourceUnit->HitPoints = 0;
        RecordUnitDeath(EM, Bullet->SourceEntityID, -1, false);
        EM.DestroyEntity(Bullet->SourceEntityID);
    }

    const bool DeferDestruction =
        Bullet->Behavior == WeaponBehavior::AppearOnTarget &&
        Bullet->LifetimeRemaining > 0.0f &&
        EM.GetComponent<RenderComponent>(BulletID) != nullptr;

    if (!DeferDestruction)
    {
        EM.DestroyEntity(BulletID);
    }
}

void BulletSystem::SpawnImpactVisual(Core::ECS::EntityManager& EM,
                                     int BulletID,
                                     const Core::Math::Vec2& ImpactPosition)
{
    const auto* Bullet = EM.GetComponent<BulletComponent>(BulletID);
    const auto* BulletRender = EM.GetComponent<RenderComponent>(BulletID);
    if (!Bullet || !BulletRender || !Bullet->PlayImpactAnimation)
        return;

    const int EffectID = EM.CreateEntity();
    auto& EffectTrans = EM.AddComponent<TransformComponent>(EffectID);
    EffectTrans.Position = ImpactPosition;

    auto& EffectRender = EM.AddComponent<RenderComponent>(EffectID);
    EffectRender = *BulletRender;
    EffectRender.Layer = RenderLayer::Effects;
    EffectRender.FrameIndex = 0;
    EffectRender.Visible = true;

    auto& Effect = EM.AddComponent<EffectComponent>(EffectID);
    Effect.LifetimeRemaining = 8.0f;
    Effect.ParentEntityID = -1;
    Effect.FollowParent = false;
    Effect.InitialAnimation = Game::IScriptAnims::Anim_Death;
    Effect.DestroyWhenAnimationCompletes = true;
    Effect.DrawAbove = true;
    Effect.IScriptDirection = DirectionToIScriptDirection(Bullet->Velocity);

    // AnimationSystem selects the projectile image's real Death entry from
    // Render.ImageID.  The unit payload only supplies the generic dying state;
    // it is not a selectable gameplay unit because the entity is an effect.
    auto& EffectUnit = EM.AddComponent<UnitComponent>(EffectID);
    EffectUnit.ChkUnitID = -1;
    EffectUnit.HitPoints = 0;
    EffectUnit.MaxHitPoints = 1;
    EffectUnit.SupplyCost = 0;

    if (const auto* BulletPlayer = EM.GetComponent<PlayerComponent>(BulletID))
        EM.AddComponent<PlayerComponent>(EffectID) = *BulletPlayer;
    if (const auto* BulletTree = EM.GetComponent<ImageTreeComponent>(BulletID))
    {
        // OpenBW runs Death on the complete projectile sprite, not only its
        // main image. Preserve attached image layers so AnimationSystem can
        // transition them together instead of popping them at impact.
        EM.AddComponent<ImageTreeComponent>(EffectID) = *BulletTree;
    }
}

void BulletSystem::ApplySplashDamage(Core::ECS::EntityManager& EM,
                                      const Core::Math::Vec2& Center,
                                      float InnerRadius,
                                      float MediumRadius,
                                      float OuterRadius,
                                      int BaseDamage,
                                      DamageType DamageKind,
                                      int DamageUpgradeBonus,
                                       int SourcePlayerID,
                                       int SourceEntityID,
                                       SplashType Splash,
                                       bool CanTargetGround,
                                       bool CanTargetAir,
                                       bool IsRangedAttack,
                                       int HitCount,
                                       int DirectHitID,
                                       const Core::Math::Vec2& LineStart,
                                       const Core::Math::Vec2& LineEnd,
                                       bool CanDamageSelf)
{
    if (OuterRadius <= 0.0f)
        return;

    const bool EnemyOnly = Splash == SplashType::EnemyRadial ||
                           Splash == SplashType::Line ||
                           Splash == SplashType::Air;

    float MinX = Center.X - OuterRadius - 32.0f;
    float MaxX = Center.X + OuterRadius + 32.0f;
    float MinY = Center.Y - OuterRadius - 32.0f;
    float MaxY = Center.Y + OuterRadius + 32.0f;
    if (Splash == SplashType::Line)
    {
        MinX = std::min(LineStart.X, LineEnd.X) - OuterRadius - 32.0f;
        MaxX = std::max(LineStart.X, LineEnd.X) + OuterRadius + 32.0f;
        MinY = std::min(LineStart.Y, LineEnd.Y) - OuterRadius - 32.0f;
        MaxY = std::max(LineStart.Y, LineEnd.Y) + OuterRadius + 32.0f;
    }

    ForNearbyUnits(EM, MinX, MinY, MaxX, MaxY, [&](int UID)
    {
        auto* UP = EM.GetComponent<PlayerComponent>(UID);
        auto* UT = EM.GetComponent<TransformComponent>(UID);
        auto* UU = EM.GetComponent<UnitComponent>(UID);
        if (!UP || !UT || !UU || UU->HitPoints <= 0) return;
        if (IsProtectedResourceNode(EM, UID)) return;
        if (UID == SourceEntityID && !CanDamageSelf) return;
        if (UID != DirectHitID && EnemyOnly &&
            (UP->PlayerID == SourcePlayerID || CombatSystem::AreAllied(UP->PlayerID, SourcePlayerID) || UP->IsRescuePassive)) return;
        if (IsInvulnerableByStatus(EM, UID)) return;

        const bool TargetIsAir = IsAirTarget(EM, UID);
        if (Splash == SplashType::Air && !TargetIsAir) return;
        if (TargetIsAir ? !CanTargetAir : !CanTargetGround) return;

        const CollisionBounds TargetBounds = BoundsForEntity(EM, UID, *UT);

        float Distance = 0.0f;
        int DamageDivisor = 1;
        if (Splash == SplashType::Line)
        {
            Distance = DistanceSegmentToBounds(
                LineStart, LineEnd, TargetBounds);
            if (Distance > OuterRadius) return;
        }
        else
        {
            Distance = DistanceToBounds(Center, TargetBounds);
            if (Distance > OuterRadius) return;
            if (Splash == SplashType::Air)
            {
                float EffectiveMedium = MediumRadius > 0.0f
                    ? MediumRadius
                    : OuterRadius * (2.0f / 3.0f);
                EffectiveMedium = std::min(EffectiveMedium, OuterRadius);
                DamageDivisor = UID == DirectHitID
                    ? 1
                    : (Distance <= EffectiveMedium ? 2 : 4);
            }
            else
            {
                const float Falloff = SplashFalloff(
                    Distance, InnerRadius, MediumRadius, OuterRadius);
                if (Falloff <= 0.0f) return;
                DamageDivisor = Falloff >= 1.0f
                    ? 1
                    : (Falloff >= 0.5f ? 2 : 4);
            }
        }

        const int DamageRaw =
            (BaseDamage + DamageUpgradeBonus) * 256 /
            std::max(1, DamageDivisor);
        const CombatComponent SplashCombat = BulletDamageCombat(DamageKind, IsRangedAttack);
        CombatSystem::ApplyDamageRaw(
            EM, UID, SplashCombat, SourceEntityID, DamageRaw,
            std::max(1, HitCount), false);
    });
}

} // namespace Game::Systems
