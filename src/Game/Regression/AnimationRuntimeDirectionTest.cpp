#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/CombatSystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace Core::Audio
{
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags)
    {
        return -1;
    }
}

namespace Game::Systems
{
    bool CombatSystem::ResolvePendingAttackFrame(Core::ECS::EntityManager&,
                                                 BulletSystem&,
                                                 int)
    {
        return false;
    }
}

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    int SpawnIdleDirectionalUnit(Core::ECS::EntityManager& EM, float FacingAngle)
    {
        constexpr int kGoliathImage = 234;
        constexpr int kGoliathChkUnit = 3;

        const int EntityID = EM.CreateEntity();
        auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
        Trans.Position = {128.0f, 128.0f};

        auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.ImageID = kGoliathImage;
        Render.FrameIndex = 0;
        Render.TotalFrames = 256;

        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = kGoliathChkUnit;

        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
        Path.FacingAngle = FacingAngle;
        Path.CurrentSpeed = 0.0f;
        Path.CurrentVelocity = {0.0f, 0.0f};

        return EntityID;
    }

    int SpawnWalkingDirectionalUnit(Core::ECS::EntityManager& EM, float TargetX)
    {
        int EntityID = SpawnIdleDirectionalUnit(EM, 0.0f);
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        Require(Path && Trans, "walking test unit has transform and path components");
        Path->Path.clear();
        Path->Path.push_back({TargetX, Trans->Position.Y});
        Path->PathIndex = 0;
        Path->CurrentSpeed = 64.0f;
        Path->CurrentVelocity = {TargetX >= Trans->Position.X ? 64.0f : -64.0f, 0.0f};
        return EntityID;
    }
}

int main()
{
    Require(Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                234, Game::IScriptAnims::Anim_Init),
            "goliath image has idle/init iscript");

    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();
    const int EastIdle = SpawnIdleDirectionalUnit(EM, 0.0f);
    const int WestIdle = SpawnIdleDirectionalUnit(EM, 3.14159265359f);

    Game::Systems::AnimationSystem Anim(SysMgr);
    Anim.Update(1.0f / 24.0f);

    const auto* EastRender = EM.GetComponent<Game::Components::RenderComponent>(EastIdle);
    const auto* WestRender = EM.GetComponent<Game::Components::RenderComponent>(WestIdle);
    Require(EastRender && WestRender, "idle directional units have render components");
    Require(EastRender->FrameIndex != WestRender->FrameIndex ||
                EastRender->Flipped != WestRender->Flipped,
            "idle units use facing direction for their standing frame");

    auto* EastPath = EM.GetComponent<Game::Components::PathfindingComponent>(EastIdle);
    EastPath->FacingAngle = 3.14159265359f;
    EastPath->CurrentSpeed = 0.0f;
    EastPath->CurrentVelocity = {0.0f, 0.0f};
    const int BeforeFrame = EastRender->FrameIndex;
    const bool BeforeFlipped = EastRender->Flipped;
    Anim.Update(1.0f / 24.0f);

    Require(EastRender->FrameIndex != BeforeFrame || EastRender->Flipped != BeforeFlipped,
            "idle animation refreshes direction while the iscript is waiting");

    const int WalkEast = SpawnWalkingDirectionalUnit(EM, 192.0f);
    const int WalkWest = SpawnWalkingDirectionalUnit(EM, 64.0f);
    Anim.Update(1.0f / 24.0f);

    auto* WalkEastRender = EM.GetComponent<Game::Components::RenderComponent>(WalkEast);
    auto* WalkWestRender = EM.GetComponent<Game::Components::RenderComponent>(WalkWest);
    Require(WalkEastRender && WalkWestRender, "walking directional units have render components");
    Require(WalkEastRender->FrameIndex != WalkWestRender->FrameIndex ||
                WalkEastRender->Flipped != WalkWestRender->Flipped,
            "walking units use movement direction for their moving frame");

    auto* WalkEastPath = EM.GetComponent<Game::Components::PathfindingComponent>(WalkEast);
    Require(WalkEastPath, "walking unit still has path component");
    WalkEastPath->Path.clear();
    WalkEastPath->PathIndex = 0;
    WalkEastPath->CurrentSpeed = 0.0f;
    WalkEastPath->CurrentVelocity = {0.0f, 0.0f};
    for (int I = 0; I < 24; ++I)
        Anim.Update(1.0f / 24.0f);

    const int StoppedFrame = WalkEastRender->FrameIndex;
    const bool StoppedFlipped = WalkEastRender->Flipped;
    for (int I = 0; I < 4; ++I)
        Anim.Update(1.0f / 24.0f);
    Require(WalkEastRender->FrameIndex == StoppedFrame &&
                WalkEastRender->Flipped == StoppedFlipped,
            "stopped unit settles to a stable idle frame instead of continuing walking");

    std::printf("AnimationRuntimeDirectionTest passed\n");
    return 0;
}
