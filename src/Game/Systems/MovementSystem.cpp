#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/MovementTuning.h"
#include "Game/Components.h"
#include "Core/Math/Vec2.h"

#include <unordered_map>
#include <cmath>
#include <algorithm>

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    struct DirResult
    {
        int Offset = 0;
        bool Flipped = false;
    };

    static DirResult DirectionToFrameOffset(Core::Math::Vec2 Dir, int TotalFrames)
    {
        DirResult R;
        if (TotalFrames <= 1) return R;

        if (TotalFrames >= 17)
        {
            const float MirroredX = std::fabs(Dir.X);
            const float Angle = atan2f(Dir.Y, MirroredX);
            int FacingIndex = (int)std::lround(((Angle + 1.57079632679f) / 3.14159265359f) * 16.0f);
            if (FacingIndex < 0) FacingIndex = 0;
            if (FacingIndex > 16) FacingIndex = 16;
            R.Offset = FacingIndex;
            R.Flipped = Dir.X < 0.0f;
        }
        else
        {
            float Deg = atan2f(Dir.Y, Dir.X) * 180.0f / 3.14159f;
            int FramesPerDir = std::max(1, TotalFrames / 4);
            int DirIndex = 0;
            if (Deg >= -45.0f && Deg < 45.0f) DirIndex = 0;
            else if (Deg >= 45.0f && Deg < 135.0f) DirIndex = 1;
            else if (Deg >= -135.0f && Deg < -45.0f) DirIndex = 3;
            else DirIndex = 2;
            R.Offset = DirIndex * FramesPerDir;
        }
        return R;
    }

    static float ClampSpeed(float Speed, float MaxSpeed)
    {
        if (Speed > MaxSpeed) return MaxSpeed;
        if (Speed < -MaxSpeed) return -MaxSpeed;
        return Speed;
    }

    static Core::Math::Vec2 ClampVelocity(const Core::Math::Vec2& V, float MaxLen)
    {
        float Len = V.Length();
        if (Len > MaxLen && Len > 0.0001f)
            return V.Normalized() * MaxLen;
        return V;
    }

    static constexpr float kRepulseRadius = 32.0f;   // pixels to check neighbors
    static constexpr float kRepulseForce = 80.0f;     // push force scalar
} // anonymous namespace

void MovementSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<TransformComponent, PathfindingComponent>();

    // Clamp delta to prevent large jumps on frame spikes
    float Dt = std::min(DeltaTime, 0.1f);

    // --- Initialize new movement fields for entities missing them ---
    for (auto ID : Entities)
    {
        auto* PathComp = EM.GetComponent<PathfindingComponent>(ID);
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (!PathComp) continue;

        // Set defaults from unit data if not yet initialized
        if (PathComp->TargetSpeed <= 0.0f && Unit)
        {
            PathComp->TargetSpeed = Tuning::ToWorldMoveSpeed(Unit->Speed);
            PathComp->Acceleration = 200.0f;
            PathComp->TurnRate = 8.0f;
            PathComp->CollisionRadius = 12;
            if (Unit->SizeType == UnitSize::Large)
                PathComp->CollisionRadius = 20;
        }
        else if (PathComp->TargetSpeed <= 0.0f)
        {
            PathComp->TargetSpeed = Tuning::ToWorldMoveSpeed(2.0f);
            PathComp->Acceleration = 200.0f;
            PathComp->TurnRate = 6.0f;
            PathComp->CollisionRadius = 16;
        }

        // Per-frame accumulation
        PathComp->RepulseVector = {0.0f, 0.0f};
    }

    // --- Repulse field: collect all unit positions ---
    // For each unit, check nearby units and compute push vectors
    for (auto ID : Entities)
    {
        auto* TransA = EM.GetComponent<TransformComponent>(ID);
        auto* PathA = EM.GetComponent<PathfindingComponent>(ID);
        if (!TransA || !PathA || PathA->IsAir) continue;

        for (auto ID2 : Entities)
        {
            if (ID == ID2) continue;
            auto* TransB = EM.GetComponent<TransformComponent>(ID2);
            auto* PathB = EM.GetComponent<PathfindingComponent>(ID2);
            if (!TransB || !PathB || PathB->IsAir) continue;

            float Dist = TransA->Position.DistanceTo(TransB->Position);
            float MinDist = (float)(PathA->CollisionRadius + PathB->CollisionRadius);

            if (Dist < MinDist && Dist > 0.1f)
            {
                Core::Math::Vec2 Push = (TransA->Position - TransB->Position).Normalized();
                float Overlap = MinDist - Dist;
                float Force = Overlap * kRepulseForce * Dt;
                PathA->RepulseVector = PathA->RepulseVector + Push * Force;
            }
        }
    }

    // --- Movement update ---
    static std::unordered_map<Core::ECS::EntityID, Core::Math::Vec2> sLastDir;
    static std::unordered_map<Core::ECS::EntityID, float> sWalkTimer;
    static std::unordered_map<Core::ECS::EntityID, Core::Math::Vec2> sPatrolStart;

    for (auto ID : Entities)
    {
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* PathComp = EM.GetComponent<PathfindingComponent>(ID);
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Render = EM.GetComponent<RenderComponent>(ID);
        auto* Cmd = EM.GetComponent<CommandComponent>(ID);

        if (!Trans || !PathComp) continue;
        auto& Path = PathComp->Path;

        // Stasis / lockdown / maelstrom: frozen, skip all movement
        auto* Status = EM.GetComponent<StatusEffectComponent>(ID);
        if (Status && (Status->HasEffect(StatusEffect::Stasis) ||
                       Status->HasEffect(StatusEffect::Lockdown) ||
                       Status->HasEffect(StatusEffect::Maelstrom)))
        {
            PathComp->CurrentSpeed = 0.0f;
            PathComp->CurrentVelocity = {0.0f, 0.0f};
            PathComp->Path.clear();
            PathComp->PathIndex = 0;
            continue;
        }

        // HoldPosition: clear path, don't move
        if (Cmd && Cmd->CurrentCommand == CommandType::HoldPosition)
        {
            Path.clear();
            PathComp->PathIndex = 0;
            PathComp->CurrentSpeed = 0.0f;
            PathComp->CurrentVelocity = {0.0f, 0.0f};
            if (Render) { Render->FrameIndex = 0; Render->Flipped = false; }
            continue;
        }

        // Follow: re-issue move toward target if path is empty
        if (Cmd && Cmd->CurrentCommand == CommandType::Follow && Path.empty())
        {
            if (Cmd->TargetEntityID >= 0)
            {
                auto* TTrans = EM.GetComponent<TransformComponent>(Cmd->TargetEntityID);
                if (TTrans)
                {
                    float Dist = Trans->Position.DistanceTo(TTrans->Position);
                    if (Dist > 32.0f)
                        CommandSystem::IssueMove(EM, {ID}, TTrans->Position);
                }
            }
            continue;
        }

        if (Path.empty())
        {
            // Patrol: reverse direction when reaching waypoint
            if (Cmd && Cmd->CurrentCommand == CommandType::Patrol)
            {
                auto It = sPatrolStart.find(ID);
                Core::Math::Vec2 Origin = (It != sPatrolStart.end())
                    ? It->second : Trans->Position;
                if (Origin.DistanceTo(Cmd->TargetPosition) < 8.0f)
                    CommandSystem::IssueMove(EM, {ID}, Origin);
                else
                    CommandSystem::IssueMove(EM, {ID}, Cmd->TargetPosition);
            }
            else
            {
                // Reached destination: advance order queue
                if (Cmd && Cmd->HasPendingOrders())
                    CommandSystem::AdvanceQueue(EM, ID);

                PathComp->CurrentSpeed = 0.0f;
                PathComp->CurrentVelocity = {0.0f, 0.0f};
                if (Render)
                {
                    auto It = sLastDir.find(ID);
                    Core::Math::Vec2 LastDir = (It != sLastDir.end()) ? It->second : Core::Math::Vec2{1, 0};
                    int TotalFrames = Render->TotalFrames > 1 ? Render->TotalFrames : 4;
                    auto DirRes = DirectionToFrameOffset(LastDir, TotalFrames);
                    Render->FrameIndex = DirRes.Offset % std::max(1, TotalFrames);
                    Render->Flipped = DirRes.Flipped;
                }
            }
            continue;
        }

        int TotalFrames = Render ? Render->TotalFrames : 1;
        if (TotalFrames <= 1) TotalFrames = 4;

        // Record patrol start
        if (Cmd && Cmd->CurrentCommand == CommandType::Patrol &&
            sPatrolStart.find(ID) == sPatrolStart.end())
        {
            sPatrolStart[ID] = Trans->Position;
        }

        bool DidMove = false;
        Core::Math::Vec2 MoveDir;

        // Follow next waypoint
        while (PathComp->PathIndex < (int)Path.size())
        {
            Core::Math::Vec2& Waypoint = Path[PathComp->PathIndex];
            Core::Math::Vec2 ToTarget = {Waypoint.X - Trans->Position.X,
                                         Waypoint.Y - Trans->Position.Y};
            float Dist = ToTarget.Length();

            if (Dist < PathComp->NodeTolerance)
            {
                PathComp->PathIndex++;
                continue;
            }

            // Speed management with acceleration
            float TargetSpd = PathComp->TargetSpeed;
            float CurSpd = PathComp->CurrentSpeed;
            if (CurSpd < TargetSpd)
                CurSpd = std::min(TargetSpd, CurSpd + PathComp->Acceleration * Dt);
            else if (CurSpd > TargetSpd)
                CurSpd = std::max(TargetSpd, CurSpd - PathComp->Acceleration * Dt * 2.0f);

            PathComp->CurrentSpeed = CurSpd;

            // Desired move direction (toward waypoint)
            MoveDir = ToTarget.Normalized();

            // Apply repulse vector (push away from nearby units)
            Core::Math::Vec2 ActualMove = MoveDir;
            if (PathComp->RepulseVector.Length() > 0.0f)
            {
                ActualMove = ActualMove + PathComp->RepulseVector;
                ActualMove = ActualMove.Normalized();
            }

            // Turn-rate smoothing
            float DesiredAngle = atan2f(ActualMove.Y, ActualMove.X);
            float Current = PathComp->FacingAngle;
            float Diff = DesiredAngle - Current;
            // Normalize to [-PI, PI]
            while (Diff > 3.14159265359f) Diff -= 6.28318530718f;
            while (Diff < -3.14159265359f) Diff += 6.28318530718f;
            float MaxTurn = PathComp->TurnRate * Dt;
            if (std::fabs(Diff) <= MaxTurn)
                PathComp->FacingAngle = DesiredAngle;
            else
                PathComp->FacingAngle += (Diff > 0.0f ? MaxTurn : -MaxTurn);

            // Move along the actual direction (not facing)
            float MoveAmount = CurSpd * Dt;
            if (MoveAmount > Dist)
            {
                Trans->Position = Waypoint;
                PathComp->PathIndex++;
                PathComp->CurrentVelocity = MoveDir * PathComp->TargetSpeed;
            }
            else
            {
                Trans->Position.X += ActualMove.X * MoveAmount;
                Trans->Position.Y += ActualMove.Y * MoveAmount;
                PathComp->CurrentVelocity = ActualMove * CurSpd;
                DidMove = true;
            }
            break;
        }

        // Update render facing
        if (Render && Render->ImageID != 0)
        {
            if (!DidMove && sLastDir.find(ID) == sLastDir.end())
                sLastDir[ID] = {1, 0};
            auto& LastDir = sLastDir[ID];
            if (DidMove) LastDir = MoveDir;

            auto DirRes = DirectionToFrameOffset(LastDir, TotalFrames);
            int FrameOffset = DirRes.Offset % 17;
            int StripCount = std::max(1, TotalFrames / 17);

            if (DidMove)
            {
                sWalkTimer[ID] += Dt * 8.0f;
                int AnimCycle = ((int)sWalkTimer[ID]) % 2;
                if (StripCount >= 3)
                    Render->FrameIndex = FrameOffset + 17 + AnimCycle * 17;
                else if (StripCount >= 2)
                    Render->FrameIndex = FrameOffset + 17;
                else
                    Render->FrameIndex = FrameOffset;
                Render->Flipped = DirRes.Flipped;
            }
            else
            {
                sWalkTimer[ID] = 0;
                Render->FrameIndex = FrameOffset;
                Render->Flipped = DirRes.Flipped;
            }
        }
        else if (Render)
        {
            Render->FrameIndex = 0;
            Render->Flipped = false;
        }
    }
}

} // namespace Game::Systems
