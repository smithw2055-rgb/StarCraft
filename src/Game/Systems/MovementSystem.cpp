#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/MovementTuning.h"
#include "Game/Components.h"
#include "Core/Math/Vec2.h"

#include <unordered_map>
#include <cmath>

namespace Game::Systems
{

struct DirResult
{
    int Offset = 0;
    bool Flipped = false;
};

// Maps movement direction to frame offset using StarCraft's 256-direction system.
// 0=Right, 64=Down, 128=Left, 192=Up
// frame_index_offset = (dir_index + 4) / 8  → 0..32
// Values >16 are mirrored: offset = 32 - offset, flipped = true
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
        int FramesPerDir = TotalFrames / 4;
        if (FramesPerDir < 1) FramesPerDir = 1;
        int DirIndex = 0;
        if (Deg >= -45.0f && Deg < 45.0f) DirIndex = 0;
        else if (Deg >= 45.0f && Deg < 135.0f) DirIndex = 1;
        else if (Deg >= -135.0f && Deg < -45.0f) DirIndex = 3;
        else DirIndex = 2;
        R.Offset = DirIndex * FramesPerDir;
    }
    return R;
}

void MovementSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<Game::Components::TransformComponent,
                                       Game::Components::PathfindingComponent>();

    static std::unordered_map<Core::ECS::EntityID, Core::Math::Vec2> sLastDir;
    static std::unordered_map<Core::ECS::EntityID, float> sWalkTimer;

    static std::unordered_map<Core::ECS::EntityID, Core::Math::Vec2> sPatrolStart;

    for (auto ID : Entities)
    {
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(ID);
        auto* PathComp = EM.GetComponent<Game::Components::PathfindingComponent>(ID);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(ID);
        auto* Render = EM.GetComponent<Game::Components::RenderComponent>(ID);
        auto* Cmd = EM.GetComponent<Game::Components::CommandComponent>(ID);

        if (!Trans || !PathComp) continue;
        auto& Path = PathComp->Path;

        // Handle HoldPosition: clear path, don't move
        if (Cmd && Cmd->CurrentCommand == Game::Components::CommandType::HoldPosition)
        {
            Path.clear();
            PathComp->PathIndex = 0;
            if (Render) { Render->FrameIndex = 0; Render->Flipped = false; }
            continue;
        }

        // Handle Follow: re-issue move toward target if path is empty
        if (Cmd && Cmd->CurrentCommand == Game::Components::CommandType::Follow && Path.empty())
        {
            if (Cmd->TargetEntityID >= 0)
            {
                auto* TTrans = EM.GetComponent<Game::Components::TransformComponent>(Cmd->TargetEntityID);
                if (TTrans)
                {
                    float Dist = Trans->Position.DistanceTo(TTrans->Position);
                    if (Dist > 32.0f) // Keep some distance
                    {
                        Game::Systems::CommandSystem::IssueMove(EM, {ID}, TTrans->Position);
                    }
                }
            }
            continue;
        }

        if (Path.empty())
        {
            // Handle Patrol: when path ends, reverse direction
            if (Cmd && Cmd->CurrentCommand == Game::Components::CommandType::Patrol)
            {
                auto It = sPatrolStart.find(ID);
                Core::Math::Vec2 Origin = (It != sPatrolStart.end())
                    ? It->second : Trans->Position;
                if (Origin.DistanceTo(Cmd->TargetPosition) < 8.0f)
                {
                    // Arrived at destination, go back to origin
                    Game::Systems::CommandSystem::IssueMove(EM, {ID}, Origin);
                }
                else
                {
                    // Go to destination
                    Game::Systems::CommandSystem::IssueMove(EM, {ID}, Cmd->TargetPosition);
                }
            }
            else
            {
                if (Render) Render->FrameIndex = 0;
            }
            continue;
        }

        // Record patrol start when first moving
        if (Cmd && Cmd->CurrentCommand == Game::Components::CommandType::Patrol &&
            sPatrolStart.find(ID) == sPatrolStart.end())
        {
            sPatrolStart[ID] = Trans->Position;
        }

        int TotalFrames = Render ? Render->TotalFrames : 1;
        if (TotalFrames <= 1) TotalFrames = 4;

        bool DidMove = false;
        Core::Math::Vec2 MoveDir;

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

            float Speed = Unit ? Game::Tuning::ToWorldMoveSpeed(Unit->Speed)
                               : Game::Tuning::ToWorldMoveSpeed(2.0f);
            MoveDir = ToTarget / Dist;
            float MoveAmount = Speed * DeltaTime;

            if (MoveAmount > Dist)
            {
                Trans->Position = Waypoint;
                PathComp->PathIndex++;
            }
            else
            {
                Trans->Position.X += MoveDir.X * MoveAmount;
                Trans->Position.Y += MoveDir.Y * MoveAmount;
                DidMove = true;
            }
            break;
        }

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
                sWalkTimer[ID] += DeltaTime * 8.0f;
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
