#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/CombatSystem.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

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
    constexpr int kDirectionalImage = 234;
    constexpr int kGoliathChkUnit = 3;

    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    int FindDirectionalAttackImage()
    {
        Game::IScriptDatabase Database;
        Require(Database.Load("assets/scripts/iscript.bin", "assets/arr/images.dat"),
                "load iscript database for attack direction test");

        for (int ImageID = 0; ImageID < 999; ++ImageID)
        {
            const auto* ImageInfo = Database.FindImageInfo(ImageID);
            if (!ImageInfo || !ImageInfo->HasDirectionalFrames || ImageInfo->IScriptID < 0)
                continue;

            const auto* Script = Database.FindScript(ImageInfo->IScriptID);
            if (!Script ||
                Game::IScriptAnims::Anim_GndAttkInit >= (int)Script->AnimationPC.size() ||
                Game::IScriptAnims::Anim_Init >= (int)Script->AnimationPC.size() ||
                Script->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init] <= 0 ||
                Script->AnimationPC[(size_t)Game::IScriptAnims::Anim_GndAttkInit] <= 0)
            {
                continue;
            }

            return ImageID;
        }

        Require(false, "find directional image with ground attack iscript");
        return -1;
    }

    int SpawnIdleDirectionalUnit(Core::ECS::EntityManager& EM,
                                 float FacingAngle,
                                 int TotalFrames = 256,
                                 int ImageID = kDirectionalImage)
    {
        const int EntityID = EM.CreateEntity();
        auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
        Trans.Position = {128.0f, 128.0f};

        auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.ImageID = (uint32_t)ImageID;
        Render.FrameIndex = 0;
        Render.TotalFrames = TotalFrames;

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

    int SpawnPendingAttackUnit(Core::ECS::EntityManager& EM,
                               int AttackImageID,
                               Core::Math::Vec2 TargetPos)
    {
        const int EntityID = EM.CreateEntity();
        auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
        Trans.Position = {128.0f, 128.0f};

        auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.ImageID = (uint32_t)AttackImageID;
        Render.FrameIndex = 0;
        Render.TotalFrames = 256;

        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = kGoliathChkUnit;

        auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
        Combat.PendingAttackFrame = true;
        Combat.PendingAttackTargetEntityID = -1;
        Combat.PendingAttackTargetPos = TargetPos;
        Combat.AttackCooldown = 1.0f;
        return EntityID;
    }
}

int main()
{
    Require(Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                kDirectionalImage, Game::IScriptAnims::Anim_Init),
            "goliath image has idle/init iscript");
    const int DirectionalAttackImage = FindDirectionalAttackImage();

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

    const int UnknownFrameCount = SpawnIdleDirectionalUnit(EM, 3.14159265359f, 1);
    Anim.Update(1.0f / 24.0f);
    auto* UnknownFrameRender = EM.GetComponent<Game::Components::RenderComponent>(UnknownFrameCount);
    Require(UnknownFrameRender && UnknownFrameRender->TotalFrames > 1,
            "iscript animation expands unknown render frame count before renderer backfills sprites");
    Require(UnknownFrameRender->FrameIndex > 0 || UnknownFrameRender->Flipped,
            "unknown frame count entity keeps the directional iscript frame instead of clamping to zero");

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

    const int AttackEast = SpawnPendingAttackUnit(EM, DirectionalAttackImage, {192.0f, 128.0f});
    const int AttackWest = SpawnPendingAttackUnit(EM, DirectionalAttackImage, {64.0f, 128.0f});
    Anim.Update(1.0f / 24.0f);

    auto* AttackEastRender = EM.GetComponent<Game::Components::RenderComponent>(AttackEast);
    auto* AttackWestRender = EM.GetComponent<Game::Components::RenderComponent>(AttackWest);
    Require(AttackEastRender && AttackWestRender,
            "pending attack units have render components");
    Require(AttackEastRender->FrameIndex != AttackWestRender->FrameIndex ||
                AttackEastRender->Flipped != AttackWestRender->Flipped,
            "pending attack animation uses target position direction when target entity is absent");

    const int FrozenIdleReference =
        SpawnIdleDirectionalUnit(EM, 0.0f, 256, DirectionalAttackImage);
    const int FrozenPendingAttack =
        SpawnPendingAttackUnit(EM, DirectionalAttackImage, {64.0f, 128.0f});
    EM.AddComponent<Game::Components::StatusEffectComponent>(FrozenPendingAttack)
        .AddEffect(Game::Components::StatusEffect::Stasis, 5.0f, -1);
    Anim.Update(1.0f / 24.0f);

    auto* FrozenIdleRender =
        EM.GetComponent<Game::Components::RenderComponent>(FrozenIdleReference);
    auto* FrozenAttackRender =
        EM.GetComponent<Game::Components::RenderComponent>(FrozenPendingAttack);
    Require(FrozenIdleRender && FrozenAttackRender,
            "frozen pending attack comparison units have render components");
    Require(FrozenAttackRender->FrameIndex == FrozenIdleRender->FrameIndex &&
                FrozenAttackRender->Flipped == FrozenIdleRender->Flipped,
            "frozen pending attack uses idle direction instead of attack target direction");

    const int ScriptedChildEntity = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(ScriptedChildEntity)
        .Position = {160.0f, 160.0f};
    auto& Tree =
        EM.AddComponent<Game::Components::ImageTreeComponent>(ScriptedChildEntity);
    Tree.Nodes.emplace_back();
    auto& ChildNode = Tree.Nodes.back();
    ChildNode.ImageID = 500;
    ChildNode.Visible = true;
    ChildNode.AnimID = Game::IScriptAnims::Anim_Init;
    static const std::vector<int> EndProgram = {
        Game::IScriptOpcodes::Opc_PlayFram, 0,
        Game::IScriptOpcodes::Opc_End
    };
    ChildNode.VM.LoadScript(Game::IScriptAnims::Anim_Init, &EndProgram, 0);
    Anim.Update(1.0f / 24.0f);
    Require(!Tree.Nodes.empty() && !Tree.Nodes.front().Visible,
            "image tree child is hidden when its iscript reaches End");

    std::printf("AnimationRuntimeDirectionTest passed\n");
    return 0;
}
