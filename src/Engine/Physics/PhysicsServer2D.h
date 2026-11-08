#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/Math/FixedPoint.h"
#include "Core/Math/Vec2.h"
#include "Core/Spatial/DeterministicSpatialGrid2D.h"

namespace Engine::Physics
{

namespace CollisionLayer
{
    constexpr uint32_t GroundUnit  = 1u << 0;
    constexpr uint32_t AirUnit     = 1u << 1;
    constexpr uint32_t Building    = 1u << 2;
    constexpr uint32_t Resource    = 1u << 3;
    constexpr uint32_t Projectile  = 1u << 4;
    constexpr uint32_t TriggerZone = 1u << 5;
    constexpr uint32_t All         = 0xFFFFFFFFu;
} // namespace CollisionLayer

enum class ShapeKind2D : uint8_t
{
    Circle = 0,
    AABB = 1
};

struct PhysicsBody2D
{
    Core::ECS::EntityID Entity = Core::ECS::InvalidEntity;
    ShapeKind2D Shape = ShapeKind2D::Circle;
    Core::Math::Vec2 Position{0.0f, 0.0f};
    float Radius = 12.0f;
    Core::Math::Vec2 HalfExtents{12.0f, 12.0f};
    uint32_t LayerMask = CollisionLayer::GroundUnit;
    bool IsStatic = false;
    bool IsTrigger = false;
};

struct RaycastHit2D
{
    bool Hit = false;
    Core::ECS::EntityID Entity = Core::ECS::InvalidEntity;
    float Distance = 0.0f;
    Core::Math::Vec2 Point{0.0f, 0.0f};
    Core::Math::Vec2 Normal{0.0f, 0.0f};
};

struct TriggerOverlapPair2D
{
    Core::ECS::EntityID TriggerEntity = Core::ECS::InvalidEntity;
    Core::ECS::EntityID OtherEntity = Core::ECS::InvalidEntity;
};

struct PhysicsServerTelemetry
{
    uint64_t BroadphaseRebuilds = 0;
    uint64_t OverlapQueriesExecuted = 0;
    uint64_t RaycastsExecuted = 0;
    uint64_t SeparationSolvesExecuted = 0;
};

// ============================================================================
// PhysicsServer2D (Phase 47):
// Referenced from Godot 4 PhysicsServer2D & Unity DOTS Physics Spatial Query:
// - Provides deterministic 2D spatial broadphase (backed by CSR/cell spatial grid
//   DeterministicSpatialGrid2D), narrowphase Circle/AABB overlap queries,
//   analytic 2D raycasting, trigger region overlap detection, and deterministic
//   fixed-point / floating-point crowd separation impulse solving.
// ============================================================================
class PhysicsServer2D
{
public:
    void Clear()
    {
        mBodies.clear();
        mEntityToDenseIndex.clear();
        mSpatialGrid.Clear();
        mBroadphaseDirty = false;
        mMaxBodyExtent = 64.0f;
        mTelemetry = {};
    }

    void RegisterOrUpdateBody(const PhysicsBody2D& Body)
    {
        if (Body.Entity == Core::ECS::InvalidEntity)
            return;

        const float BodyExtent = (Body.Shape == ShapeKind2D::Circle)
                                     ? Body.Radius
                                     : std::max(Body.HalfExtents.X, Body.HalfExtents.Y);
        mMaxBodyExtent = std::max(mMaxBodyExtent, BodyExtent);

        auto It = mEntityToDenseIndex.find(Body.Entity);
        if (It != mEntityToDenseIndex.end())
        {
            mBodies[It->second] = Body;
        }
        else
        {
            const size_t Idx = mBodies.size();
            mBodies.push_back(Body);
            mEntityToDenseIndex[Body.Entity] = Idx;
        }
        mBroadphaseDirty = true;
    }

    void RemoveBody(Core::ECS::EntityID Entity)
    {
        auto It = mEntityToDenseIndex.find(Entity);
        if (It == mEntityToDenseIndex.end())
            return;

        const size_t Idx = It->second;
        const size_t LastIdx = mBodies.size() - 1u;
        if (Idx != LastIdx)
        {
            mBodies[Idx] = mBodies[LastIdx];
            mEntityToDenseIndex[mBodies[Idx].Entity] = Idx;
        }
        mBodies.pop_back();
        mEntityToDenseIndex.erase(It);
        mBroadphaseDirty = true;
    }

    const PhysicsBody2D* FindBody(Core::ECS::EntityID Entity) const noexcept
    {
        auto It = mEntityToDenseIndex.find(Entity);
        return It != mEntityToDenseIndex.end() ? &mBodies[It->second] : nullptr;
    }

    void RebuildBroadphase() const
    {
        mSpatialGrid.Clear();
        for (const auto& Body : mBodies)
        {
            mSpatialGrid.Insert(Body.Entity, Body.Position);
        }
        mBroadphaseDirty = false;
        ++mTelemetry.BroadphaseRebuilds;
    }

    size_t OverlapCircle(Core::Math::Vec2 Center,
                         float Radius,
                         uint32_t LayerMask,
                         std::vector<Core::ECS::EntityID>& OutEntities) const
    {
        if (mBroadphaseDirty)
            RebuildBroadphase();
        ++mTelemetry.OverlapQueriesExecuted;

        OutEntities.clear();
        const float MaxQueryMargin = Radius + mMaxBodyExtent;
        std::vector<int> Candidates;
        mSpatialGrid.QueryInto(
            Center.X - MaxQueryMargin,
            Center.Y - MaxQueryMargin,
            Center.X + MaxQueryMargin,
            Center.Y + MaxQueryMargin,
            Candidates);

        for (int CandID : Candidates)
        {
            const auto* Body = FindBody(CandID);
            if (!Body || (Body->LayerMask & LayerMask) == 0u)
                continue;

            if (IntersectsCircleBody(Center, Radius, *Body))
                OutEntities.push_back(Body->Entity);
        }

        std::sort(OutEntities.begin(), OutEntities.end());
        OutEntities.erase(std::unique(OutEntities.begin(), OutEntities.end()), OutEntities.end());
        return OutEntities.size();
    }

    size_t OverlapAABB(Core::Math::Vec2 Min,
                       Core::Math::Vec2 Max,
                       uint32_t LayerMask,
                       std::vector<Core::ECS::EntityID>& OutEntities) const
    {
        if (mBroadphaseDirty)
            RebuildBroadphase();
        ++mTelemetry.OverlapQueriesExecuted;

        OutEntities.clear();
        std::vector<int> Candidates;
        mSpatialGrid.QueryInto(
            Min.X - mMaxBodyExtent,
            Min.Y - mMaxBodyExtent,
            Max.X + mMaxBodyExtent,
            Max.Y + mMaxBodyExtent,
            Candidates);

        for (int CandID : Candidates)
        {
            const auto* Body = FindBody(CandID);
            if (!Body || (Body->LayerMask & LayerMask) == 0u)
                continue;

            Core::Math::Vec2 BodyMin, BodyMax;
            GetBodyBounds(*Body, BodyMin, BodyMax);
            if (BodyMax.X >= Min.X && BodyMin.X <= Max.X &&
                BodyMax.Y >= Min.Y && BodyMin.Y <= Max.Y)
            {
                OutEntities.push_back(Body->Entity);
            }
        }

        std::sort(OutEntities.begin(), OutEntities.end());
        OutEntities.erase(std::unique(OutEntities.begin(), OutEntities.end()), OutEntities.end());
        return OutEntities.size();
    }

    // Analytic 2D raycast against registered Circle and AABB bodies
    bool Raycast2D(Core::Math::Vec2 Origin,
                   Core::Math::Vec2 Direction,
                   float MaxDistance,
                   uint32_t LayerMask,
                   RaycastHit2D& OutHit,
                   Core::ECS::EntityID IgnoreEntity = Core::ECS::InvalidEntity) const
    {
        ++mTelemetry.RaycastsExecuted;
        OutHit = RaycastHit2D{};

        const float DirLen = std::sqrt(Direction.X * Direction.X + Direction.Y * Direction.Y);
        if (DirLen < 1e-6f || MaxDistance <= 0.0f)
            return false;

        const Core::Math::Vec2 UnitDir{Direction.X / DirLen, Direction.Y / DirLen};
        float ClosestT = MaxDistance;
        bool Found = false;

        for (const auto& Body : mBodies)
        {
            if (Body.Entity == IgnoreEntity || (Body.LayerMask & LayerMask) == 0u)
                continue;

            float HitT = 0.0f;
            Core::Math::Vec2 HitNormal{0.0f, 0.0f};
            if (Body.Shape == ShapeKind2D::Circle)
            {
                if (!RayVsCircle(Origin, UnitDir, Body.Position, Body.Radius, ClosestT, HitT, HitNormal))
                    continue;
            }
            else
            {
                Core::Math::Vec2 BMin, BMax;
                GetBodyBounds(Body, BMin, BMax);
                if (!RayVsAABB(Origin, UnitDir, BMin, BMax, ClosestT, HitT, HitNormal))
                    continue;
            }

            if (HitT < ClosestT || (std::fabs(HitT - ClosestT) < 1e-5f && Body.Entity < OutHit.Entity))
            {
                ClosestT = HitT;
                Found = true;
                OutHit.Hit = true;
                OutHit.Entity = Body.Entity;
                OutHit.Distance = HitT;
                OutHit.Point = {Origin.X + UnitDir.X * HitT, Origin.Y + UnitDir.Y * HitT};
                OutHit.Normal = HitNormal;
            }
        }
        return Found;
    }

    // Evaluates all trigger bodies against non-trigger bodies and outputs deterministic pairs
    size_t EvaluateTriggerOverlaps(std::vector<TriggerOverlapPair2D>& OutPairs) const
    {
        if (mBroadphaseDirty)
            RebuildBroadphase();

        OutPairs.clear();
        std::vector<Core::ECS::EntityID> Hits;
        for (const auto& Trigger : mBodies)
        {
            if (!Trigger.IsTrigger)
                continue;

            if (Trigger.Shape == ShapeKind2D::Circle)
            {
                OverlapCircle(Trigger.Position, Trigger.Radius, Trigger.LayerMask, Hits);
            }
            else
            {
                Core::Math::Vec2 BMin, BMax;
                GetBodyBounds(Trigger, BMin, BMax);
                OverlapAABB(BMin, BMax, Trigger.LayerMask, Hits);
            }

            for (Core::ECS::EntityID OtherID : Hits)
            {
                if (OtherID == Trigger.Entity)
                    continue;
                const auto* OtherBody = FindBody(OtherID);
                if (OtherBody && !OtherBody->IsTrigger)
                {
                    OutPairs.push_back({Trigger.Entity, OtherID});
                }
            }
        }
        return OutPairs.size();
    }

    // Deterministic fixed-point / float separation velocity solver for crowded RTS units
    Core::Math::Vec2 SolveDeterministicSeparation(Core::ECS::EntityID SelfEntity,
                                                  float MaxPushSpeed = 64.0f) const
    {
        ++mTelemetry.SeparationSolvesExecuted;
        const auto* Self = FindBody(SelfEntity);
        if (!Self || Self->IsStatic || Self->IsTrigger)
            return {0.0f, 0.0f};

        std::vector<Core::ECS::EntityID> Neighbors;
        OverlapCircle(Self->Position, Self->Radius * 2.5f, Self->LayerMask, Neighbors);

        Core::Math::FixedVec2 FixedAccum = Core::Math::FixedVec2::Zero();

        for (Core::ECS::EntityID OtherID : Neighbors)
        {
            if (OtherID == SelfEntity)
                continue;
            const auto* Other = FindBody(OtherID);
            if (!Other || Other->IsTrigger)
                continue;

            const float CombinedRadius = Self->Radius + Other->Radius;
            const float DX = Self->Position.X - Other->Position.X;
            const float DY = Self->Position.Y - Other->Position.Y;
            const float DistSq = DX * DX + DY * DY;

            if (DistSq < CombinedRadius * CombinedRadius)
            {
                if (DistSq > 1e-4f)
                {
                    const float Dist = std::sqrt(DistSq);
                    const float Strength = ((CombinedRadius - Dist) / CombinedRadius) * MaxPushSpeed;
                    FixedAccum.X += Core::Math::fp8::FromFloat((DX / Dist) * Strength);
                    FixedAccum.Y += Core::Math::fp8::FromFloat((DY / Dist) * Strength);
                }
                else
                {
                    const float Sign = (SelfEntity < OtherID) ? 1.0f : -1.0f;
                    FixedAccum.X += Core::Math::fp8::FromFloat(Sign * MaxPushSpeed);
                }
            }
        }

        Core::Math::Vec2 Result{FixedAccum.X.AsFloat(), FixedAccum.Y.AsFloat()};
        const float LenSq = Result.X * Result.X + Result.Y * Result.Y;
        if (LenSq > MaxPushSpeed * MaxPushSpeed && LenSq > 1e-6f)
        {
            const float Scale = MaxPushSpeed / std::sqrt(LenSq);
            Result.X *= Scale;
            Result.Y *= Scale;
        }
        return Result;
    }

    size_t GetBodyCount() const noexcept { return mBodies.size(); }
    const PhysicsServerTelemetry& GetTelemetry() const noexcept { return mTelemetry; }

private:
    static void GetBodyBounds(const PhysicsBody2D& Body,
                              Core::Math::Vec2& OutMin,
                              Core::Math::Vec2& OutMax) noexcept
    {
        if (Body.Shape == ShapeKind2D::Circle)
        {
            OutMin = {Body.Position.X - Body.Radius, Body.Position.Y - Body.Radius};
            OutMax = {Body.Position.X + Body.Radius, Body.Position.Y + Body.Radius};
        }
        else
        {
            OutMin = {Body.Position.X - Body.HalfExtents.X, Body.Position.Y - Body.HalfExtents.Y};
            OutMax = {Body.Position.X + Body.HalfExtents.X, Body.Position.Y + Body.HalfExtents.Y};
        }
    }

    static bool IntersectsCircleBody(Core::Math::Vec2 Center,
                                     float Radius,
                                     const PhysicsBody2D& Body) noexcept
    {
        if (Body.Shape == ShapeKind2D::Circle)
        {
            const float DX = Center.X - Body.Position.X;
            const float DY = Center.Y - Body.Position.Y;
            const float R = Radius + Body.Radius;
            return (DX * DX + DY * DY) <= (R * R);
        }

        Core::Math::Vec2 BMin, BMax;
        GetBodyBounds(Body, BMin, BMax);
        const float ClosestX = std::clamp(Center.X, BMin.X, BMax.X);
        const float ClosestY = std::clamp(Center.Y, BMin.Y, BMax.Y);
        const float DX = Center.X - ClosestX;
        const float DY = Center.Y - ClosestY;
        return (DX * DX + DY * DY) <= (Radius * Radius);
    }

    static bool RayVsCircle(Core::Math::Vec2 Origin,
                            Core::Math::Vec2 UnitDir,
                            Core::Math::Vec2 Center,
                            float Radius,
                            float MaxT,
                            float& OutT,
                            Core::Math::Vec2& OutNormal) noexcept
    {
        const float OX = Origin.X - Center.X;
        const float OY = Origin.Y - Center.Y;
        const float B = OX * UnitDir.X + OY * UnitDir.Y;
        const float C = (OX * OX + OY * OY) - Radius * Radius;
        if (C > 0.0f && B > 0.0f)
            return false;

        const float Discr = B * B - C;
        if (Discr < 0.0f)
            return false;

        float T = -B - std::sqrt(Discr);
        if (T < 0.0f)
            T = 0.0f;
        if (T > MaxT)
            return false;

        OutT = T;
        const float HitX = Origin.X + UnitDir.X * T - Center.X;
        const float HitY = Origin.Y + UnitDir.Y * T - Center.Y;
        const float Len = std::sqrt(HitX * HitX + HitY * HitY);
        OutNormal = (Len > 1e-5f) ? Core::Math::Vec2{HitX / Len, HitY / Len}
                                  : Core::Math::Vec2{-UnitDir.X, -UnitDir.Y};
        return true;
    }

    static bool RayVsAABB(Core::Math::Vec2 Origin,
                          Core::Math::Vec2 UnitDir,
                          Core::Math::Vec2 Min,
                          Core::Math::Vec2 Max,
                          float MaxT,
                          float& OutT,
                          Core::Math::Vec2& OutNormal) noexcept
    {
        float TMin = 0.0f;
        float TMax = MaxT;
        Core::Math::Vec2 Normal{-UnitDir.X, -UnitDir.Y};

        auto CheckAxis = [&](float Orig, float Dir, float AxisMin, float AxisMax, Core::Math::Vec2 NegN, Core::Math::Vec2 PosN) -> bool {
            if (std::fabs(Dir) < 1e-6f)
                return Orig >= AxisMin && Orig <= AxisMax;

            float InvD = 1.0f / Dir;
            float T0 = (AxisMin - Orig) * InvD;
            float T1 = (AxisMax - Orig) * InvD;
            Core::Math::Vec2 CandN = NegN;
            if (T0 > T1)
            {
                std::swap(T0, T1);
                CandN = PosN;
            }
            if (T0 > TMin)
            {
                TMin = T0;
                Normal = CandN;
            }
            TMax = std::min(TMax, T1);
            return TMin <= TMax;
        };

        if (!CheckAxis(Origin.X, UnitDir.X, Min.X, Max.X, {-1.0f, 0.0f}, {1.0f, 0.0f}))
            return false;
        if (!CheckAxis(Origin.Y, UnitDir.Y, Min.Y, Max.Y, {0.0f, -1.0f}, {0.0f, 1.0f}))
            return false;

        OutT = TMin;
        OutNormal = Normal;
        return true;
    }

    std::vector<PhysicsBody2D> mBodies;
    std::unordered_map<Core::ECS::EntityID, size_t> mEntityToDenseIndex;
    mutable Core::Spatial::DeterministicSpatialGrid2D<64, 32, 256> mSpatialGrid;
    mutable bool mBroadphaseDirty = false;
    float mMaxBodyExtent = 64.0f;
    mutable PhysicsServerTelemetry mTelemetry{};
};

} // namespace Engine::Physics
