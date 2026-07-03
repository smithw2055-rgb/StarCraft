#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Components.h"
#include "Core/Audio/AudioSystem.h"

#include <algorithm>
#include <cmath>

namespace Game::Systems
{

using namespace Game::Components;

std::unordered_map<int, AnimationSystem::UnitAnimData> AnimationSystem::sAnimDB;
Game::IScriptDatabase AnimationSystem::sIScriptDB;
bool AnimationSystem::sTriedLoadIScript = false;

AnimState AnimationSystem::GetAnimStateFromComponents(
    const CommandComponent* Cmd, const CombatComponent* Combat,
    const PathfindingComponent* Path, const UnitComponent* Unit,
    const BuildingComponent* Bldg, const MorphComponent* Morph)
{
    if (Morph && Morph->IsMorphing) return AnimState::Morphing;
    if (Bldg && Bldg->UnderConstruction) return AnimState::Constructing;
    if (Combat && Combat->CooldownRemaining > Combat->AttackCooldown * 0.5f)
        return AnimState::Attacking;
    if (Cmd && (Cmd->CurrentCommand == CommandType::Attack ||
                Cmd->CurrentCommand == CommandType::AttackMove))
        return AnimState::Attacking;
    if (Path && !Path->Path.empty() && Path->PathIndex < (int)Path->Path.size())
        return AnimState::Walking;
    if (Cmd && (Cmd->CurrentCommand == CommandType::Move ||
                Cmd->CurrentCommand == CommandType::Harvest))
        return AnimState::Walking;
    return AnimState::Idle;
}

void AnimationSystem::RegisterUnitAnim(int ChkUnitID, const std::vector<AnimClip>& Clips)
{
    sAnimDB[ChkUnitID] = {Clips};
}

void AnimationSystem::EnsureIScriptLoaded()
{
    if (sTriedLoadIScript)
        return;
    sTriedLoadIScript = true;
    sIScriptDB.Load("assets/scripts/iscript.bin", "assets/arr/images.dat");
}

bool AnimationSystem::HasIScriptAnimationForImage(int ImageID, int AnimID)
{
    EnsureIScriptLoaded();
    if (!sIScriptDB.IsLoaded())
        return false;
    const auto* ImageInfo = sIScriptDB.FindImageInfo(ImageID);
    if (!ImageInfo || ImageInfo->IScriptID < 0)
        return false;
    const auto* Script = sIScriptDB.FindScript(ImageInfo->IScriptID);
    return Script &&
           AnimID >= 0 &&
           AnimID < (int)Script->AnimationPC.size() &&
           Script->AnimationPC[(size_t)AnimID] > 0;
}

void AnimationSystem::UpdateScriptEffects(float DeltaTime)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto Effects = EM.GetEntitiesWith<TransformComponent, RenderComponent, EffectComponent>();
    for (int ID : Effects)
    {
        auto* Effect = EM.GetComponent<EffectComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        if (!Effect || !Trans)
            continue;

        if (Effect->FollowParent && Effect->ParentEntityID > 0)
        {
            if (auto* ParentTrans = EM.GetComponent<TransformComponent>(Effect->ParentEntityID))
                Trans->Position = ParentTrans->Position + Effect->Offset;
        }

        Effect->LifetimeRemaining -= DeltaTime;
        if (Effect->LifetimeRemaining <= 0.0f)
            EM.DestroyEntity(ID);
    }
}

void AnimationSystem::UpdateImageTrees(float DeltaTime)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<ImageTreeComponent>();
    for (int ID : Entities)
    {
        auto* Tree = EM.GetComponent<ImageTreeComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Player = EM.GetComponent<PlayerComponent>(ID);
        if (!Tree)
            continue;
        const size_t InitialCount = Tree->Nodes.size();
        for (size_t I = 0; I < InitialCount && I < Tree->Nodes.size(); ++I)
        {
            auto& Node = Tree->Nodes[I];
            if (Node.ParentIndex >= 0 &&
                (Node.ParentIndex >= (int)Tree->Nodes.size() ||
                 !Tree->Nodes[(size_t)Node.ParentIndex].Visible))
            {
                Node.Visible = false;
            }
            if (Node.LifetimeRemaining >= 0.0f)
            {
                Node.LifetimeRemaining -= DeltaTime;
                if (Node.LifetimeRemaining <= 0.0f)
                {
                    Node.Visible = false;
                    MarkImageSubtreeHidden(*Tree, (int)I);
                }
            }
            if (Node.Visible && Node.VM.IsRunning())
            {
                constexpr float kIScriptFrameSeconds = 1.0f / 24.0f;
                Node.StepAccumulator += DeltaTime;
                bool Ran = false;
                while (Node.StepAccumulator >= kIScriptFrameSeconds)
                {
                    Node.StepAccumulator -= kIScriptFrameSeconds;
                    Ran = Node.VM.ExecuteFrame() || Ran;
                    if (!Node.VM.IsRunning())
                        break;
                }
                if (!Ran && Node.AnimID == Game::IScriptAnims::Anim_Init)
                    Node.VM.ExecuteFrame();
                Node.FrameIndex = std::max(0, Node.VM.GetCurrentFrame());
                Node.Flipped = Node.VM.GetContext().FlipState;
                if (Trans)
                {
                    const Core::Math::Vec2 NodeOffset = Node.Offset;
                    const float NodeLifetime = Node.LifetimeRemaining;
                    ConsumeImageNodeEvents(ID, (int)I, NodeOffset, NodeLifetime, *Trans, Player);
                }
            }
        }
        // ParentIndex stores vector indices, so keep node slots stable even after
        // a temporary overlay expires. AddImageTreeNode can reuse hidden slots.
    }
}

void AnimationSystem::ClearImageTree(int EntityID)
{
    if (auto* Tree = mSystemMgr.GetEntityManager().GetComponent<ImageTreeComponent>(EntityID))
        Tree->Nodes.clear();
}

void AnimationSystem::MarkImageSubtreeHidden(ImageTreeComponent& Tree, int ParentIndex)
{
    if (ParentIndex < 0)
        return;
    for (size_t I = 0; I < Tree.Nodes.size(); ++I)
    {
        if (Tree.Nodes[I].ParentIndex == ParentIndex && Tree.Nodes[I].Visible)
        {
            Tree.Nodes[I].Visible = false;
            MarkImageSubtreeHidden(Tree, (int)I);
        }
    }
}

void AnimationSystem::AddImageTreeNode(int EntityID,
                                       int ImageID,
                                       int OffsetX,
                                       int OffsetY,
                                       bool DrawAbove,
                                       bool Flipped,
                                       float Lifetime,
                                       bool UseSpriteID)
{
    AddImageTreeNode(EntityID, -1, ImageID, Core::Math::Vec2{(float)OffsetX, (float)OffsetY},
                     DrawAbove, Flipped, Lifetime, UseSpriteID);
}

void AnimationSystem::AddImageTreeNode(int EntityID,
                                       int ParentIndex,
                                       int ImageID,
                                       Core::Math::Vec2 Offset,
                                       bool DrawAbove,
                                       bool Flipped,
                                       float Lifetime,
                                       bool UseSpriteID)
{
    if (ImageID < 0)
        return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto* Tree = EM.GetComponent<ImageTreeComponent>(EntityID);
    if (!Tree)
        Tree = &EM.AddComponent<ImageTreeComponent>(EntityID);

    for (auto& Node : Tree->Nodes)
    {
        if ((int)Node.ImageID == ImageID &&
            Node.ParentIndex == ParentIndex &&
            Node.Offset.X == Offset.X &&
            Node.Offset.Y == Offset.Y &&
            Node.DrawAbove == DrawAbove &&
            Node.UseSpriteID == UseSpriteID)
        {
            Node.Visible = true;
            Node.Flipped = Flipped;
            Node.LifetimeRemaining = Lifetime;
            if (!Node.UseSpriteID && !Node.VM.IsRunning())
                StartImageNodeScript(Node);
            return;
        }
    }

    for (auto& Node : Tree->Nodes)
    {
        if (!Node.Visible)
        {
            Node = ImageSubnode{};
            Node.ImageID = (uint32_t)ImageID;
            Node.ParentIndex = ParentIndex;
            Node.Offset = Offset;
            Node.DrawAbove = DrawAbove;
            Node.Flipped = Flipped;
            Node.UseSpriteID = UseSpriteID;
            Node.Visible = true;
            Node.LifetimeRemaining = Lifetime;
            if (!Node.UseSpriteID)
                StartImageNodeScript(Node);
            return;
        }
    }

    if (Tree->Nodes.size() >= 64)
        return;

    ImageSubnode Node;
    Node.ImageID = (uint32_t)ImageID;
    Node.ParentIndex = ParentIndex;
    Node.FrameIndex = 0;
    Node.TotalFrames = 1;
    Node.Offset = Offset;
    Node.DrawAbove = DrawAbove;
    Node.Flipped = Flipped;
    Node.UseSpriteID = UseSpriteID;
    Node.Visible = true;
    Node.LifetimeRemaining = Lifetime;
    if (!Node.UseSpriteID)
        StartImageNodeScript(Node);
    Tree->Nodes.push_back(Node);
}

void AnimationSystem::StartImageNodeScript(ImageSubnode& Node)
{
    EnsureIScriptLoaded();
    if (!sIScriptDB.IsLoaded())
        return;

    const auto* ImageInfo = sIScriptDB.FindImageInfo((int)Node.ImageID);
    if (!ImageInfo || ImageInfo->IScriptID < 0)
        return;
    const auto* Script = sIScriptDB.FindScript(ImageInfo->IScriptID);
    if (!Script ||
        Game::IScriptAnims::Anim_Init >= (int)Script->AnimationPC.size() ||
        Script->AnimationPC[(size_t)Game::IScriptAnims::Anim_Init] <= 0)
        return;

    Node.ScriptID = ImageInfo->IScriptID;
    Node.AnimID = Game::IScriptAnims::Anim_Init;
    Node.StepAccumulator = 0.0f;
    Node.VM.LoadScript(Node.AnimID, &sIScriptDB.GetProgram().ProgramData,
                       Script->AnimationPC[(size_t)Node.AnimID]);
    Node.VM.GetContext().ImageID = (int)Node.ImageID;
    Node.VM.GetContext().FrameIndex = Node.FrameIndex;
}

void AnimationSystem::ConsumeImageNodeEvents(int EntityID,
                                             int NodeIndex,
                                             Core::Math::Vec2 NodeOffset,
                                             float NodeLifetime,
                                             const TransformComponent& Trans,
                                             const PlayerComponent* Player)
{
    auto* Tree = mSystemMgr.GetEntityManager().GetComponent<ImageTreeComponent>(EntityID);
    if (!Tree || NodeIndex < 0 || NodeIndex >= (int)Tree->Nodes.size())
        return;

    auto Events = Tree->Nodes[(size_t)NodeIndex].VM.ConsumeEvents();
    for (const auto& Event : Events)
    {
        switch (Event.Type)
        {
            case Game::IScriptEventType::PlaySound:
            case Game::IScriptEventType::PlayRandomSound:
            case Game::IScriptEventType::PlaySoundBetween:
                if (mAudio && Event.A >= 0)
                {
                    int UnitType = Player ? Player->PlayerID : -1;
                    mAudio->PlaySound(Event.A,
                                      Trans.Position.X + NodeOffset.X,
                                      Trans.Position.Y + NodeOffset.Y,
                                      0, UnitType);
                }
                break;
            case Game::IScriptEventType::CreateImageOverlay:
                AddImageTreeNode(EntityID, NodeIndex, Event.A,
                                 Core::Math::Vec2{(float)Event.B, (float)Event.C},
                                 Event.D != 0, false, NodeLifetime);
                break;
            case Game::IScriptEventType::CreateSpriteOverlay:
                AddImageTreeNode(EntityID, NodeIndex, Event.A,
                                 Core::Math::Vec2{(float)Event.B, (float)Event.C},
                                 Event.D != 0, false, NodeLifetime, true);
                break;
            default:
                break;
        }
    }
}

void AnimationSystem::SpawnIScriptOverlay(int ParentEntityID,
                                          const TransformComponent& ParentTrans,
                                          int ImageID,
                                          int OffsetX,
                                          int OffsetY,
                                          float Lifetime,
                                          bool FollowParent)
{
    if (ImageID < 0)
        return;

    auto& EM = mSystemMgr.GetEntityManager();
    int EffectID = EM.CreateEntity();
    auto& Trans = EM.AddComponent<TransformComponent>(EffectID);
    auto& Render = EM.AddComponent<RenderComponent>(EffectID);
    auto& Effect = EM.AddComponent<EffectComponent>(EffectID);

    Effect.ParentEntityID = ParentEntityID;
    Effect.Offset = {(float)OffsetX, (float)OffsetY};
    Effect.FollowParent = FollowParent;
    Effect.LifetimeRemaining = std::max(0.05f, Lifetime);

    Trans.Position = ParentTrans.Position + Effect.Offset;
    Render.ImageID = (uint32_t)ImageID;
    Render.FrameIndex = 0;
    Render.TotalFrames = 1;
    Render.Layer = RenderLayer::Effects;
    Render.ZOrder = 1;
}

void AnimationSystem::ConsumeIScriptEvents(int EntityID,
                                           EntityScriptState& State,
                                           const TransformComponent& Trans,
                                           const PlayerComponent* Player,
                                           CombatComponent* Combat)
{
    auto Events = State.VM.ConsumeEvents();
    const bool TransientOverlay = Combat && Combat->PendingAttackFrame;
    for (const auto& Event : Events)
    {
        switch (Event.Type)
        {
            case Game::IScriptEventType::PlaySound:
            case Game::IScriptEventType::PlayRandomSound:
            case Game::IScriptEventType::PlaySoundBetween:
                if (mAudio && Event.A >= 0)
                {
                    int UnitType = Player ? Player->PlayerID : -1;
                    mAudio->PlaySound(Event.A, Trans.Position.X, Trans.Position.Y, 0, UnitType);
                }
                break;
            case Game::IScriptEventType::CreateImageOverlay:
                AddImageTreeNode(EntityID, Event.A, Event.B, Event.C,
                                 Event.D != 0, false,
                                 TransientOverlay ? 0.35f : -1.0f);
                break;
            case Game::IScriptEventType::CreateSpriteOverlay:
                AddImageTreeNode(EntityID, Event.A, Event.B, Event.C,
                                 Event.D != 0, false,
                                 TransientOverlay ? 0.35f : -1.0f,
                                 true);
                break;
            case Game::IScriptEventType::WarpOverlay:
                SpawnIScriptOverlay(EntityID, Trans, Event.A, 0, 0, 1.0f, true);
                break;
            case Game::IScriptEventType::CreateGasOverlays:
                break;
            case Game::IScriptEventType::Attack:
            case Game::IScriptEventType::AttackMelee:
            case Game::IScriptEventType::UseWeapon:
            case Game::IScriptEventType::DoMissileDamage:
            case Game::IScriptEventType::DoGroundDamage:
            case Game::IScriptEventType::CastSpell:
                if (Combat && Combat->PendingAttackFrame && mBulletSystem)
                    CombatSystem::ResolvePendingAttackFrame(mSystemMgr.GetEntityManager(), *mBulletSystem, EntityID);
                break;
            case Game::IScriptEventType::OrderSignal:
            case Game::IScriptEventType::OrderDone:
            case Game::IScriptEventType::FlightState:
            case Game::IScriptEventType::SpawnFrame:
            case Game::IScriptEventType::TemporaryGraphicState:
                break;
        }
    }
}

bool AnimationSystem::UpdateIScriptAnimation(int EntityID,
                                             float DeltaTime,
                                             RenderComponent& Render,
                                             const TransformComponent& Trans,
                                             const PlayerComponent* Player,
                                             CombatComponent* Combat,
                                             const BuildingComponent* Bldg,
                                             const ProductionComponent* Prod)
{
    auto& State = mScriptStates[EntityID];
    if (Bldg && Bldg->UnderConstruction)
    {
        State.WasUnderConstruction = true;
        return false;
    }
    if (!Bldg && !(Combat && Combat->PendingAttackFrame))
        return false;

    const bool IsEffect = mSystemMgr.GetEntityManager().GetComponent<EffectComponent>(EntityID) != nullptr;
    if (IsEffect)
        return false;

    const bool IsWorking =
        Prod && (Prod->CurrentTrainingUnitDefID >= 0 || !Prod->Queue.empty());
    const bool IsPendingAttack = Combat && Combat->PendingAttackFrame;

    EnsureIScriptLoaded();
    if (!sIScriptDB.IsLoaded())
        return false;

    const auto* ImageInfo = sIScriptDB.FindImageInfo((int)Render.ImageID);
    if (!ImageInfo || ImageInfo->IScriptID < 0)
        return false;

    const auto* Script = sIScriptDB.FindScript(ImageInfo->IScriptID);
    if (!Script)
        return false;

    int TargetAnim = Game::IScriptAnims::Anim_Init;
    auto HasAnim = [&](int AnimID) -> bool
    {
        return AnimID >= 0 &&
               AnimID < (int)Script->AnimationPC.size() &&
               Script->AnimationPC[(size_t)AnimID] > 0;
    };

    if (IsPendingAttack && HasAnim(Game::IScriptAnims::Anim_GndAttkInit))
    {
        TargetAnim = Game::IScriptAnims::Anim_GndAttkInit;
    }
    else if (IsPendingAttack && HasAnim(Game::IScriptAnims::Anim_AirAttkInit))
    {
        TargetAnim = Game::IScriptAnims::Anim_AirAttkInit;
    }
    else if (!Bldg)
    {
        return false;
    }
    else if (IsWorking && HasAnim(Game::IScriptAnims::Anim_IsWorking))
    {
        TargetAnim = Game::IScriptAnims::Anim_IsWorking;
    }
    else if (State.WasWorking && HasAnim(Game::IScriptAnims::Anim_WorkingToIdle))
    {
        TargetAnim = Game::IScriptAnims::Anim_WorkingToIdle;
    }
    else if (Bldg && HasAnim(Game::IScriptAnims::Anim_Init))
    {
        TargetAnim = Game::IScriptAnims::Anim_Init;
    }
    else if (Bldg)
    {
        return false;
    }
    else
    {
        return false;
    }

    const bool IsTransientAnim =
        IsPendingAttack ||
        IsWorking ||
        State.WasWorking;
    if (State.ImageID == (int)Render.ImageID &&
        State.ScriptID == ImageInfo->IScriptID &&
        State.AnimID == TargetAnim &&
        !State.VM.IsRunning() &&
        !IsTransientAnim)
    {
        return true;
    }

    const bool NeedStart =
        State.ImageID != (int)Render.ImageID ||
        State.ScriptID != ImageInfo->IScriptID ||
        State.AnimID != TargetAnim ||
        !State.VM.IsRunning();

    if (NeedStart)
    {
        const int EntryPC = Script->AnimationPC[(size_t)TargetAnim];
        State.VM.LoadScript(TargetAnim, &sIScriptDB.GetProgram().ProgramData, EntryPC);
        State.VM.GetContext().ImageID = (int)Render.ImageID;
        State.VM.GetContext().FrameIndex = std::clamp(Render.FrameIndex, 0, std::max(0, Render.TotalFrames - 1));
        State.ImageID = (int)Render.ImageID;
        State.ScriptID = ImageInfo->IScriptID;
        State.AnimID = TargetAnim;
        State.StepAccumulator = 0.0f;
        ClearImageTree(EntityID);
    }

    if (Combat && Combat->PendingAttackFrame)
    {
        auto& Ctx = State.VM.GetContext();
        Ctx.IsAttacking = true;
        if (Combat->PendingAttackTargetEntityID >= 0)
        {
            if (auto* TargetTrans = mSystemMgr.GetEntityManager().GetComponent<TransformComponent>(Combat->PendingAttackTargetEntityID))
            {
                const auto Delta = TargetTrans->Position - Trans.Position;
                Ctx.TargetRangePixels = (int)Delta.Length();
                float Angle = std::atan2(Delta.Y, Delta.X);
                Ctx.CurrentDirection = (int)std::lround((Angle + 3.14159265359f) * 256.0f / 6.28318530718f) & 0xff;
                Ctx.TargetArc = 0;
            }
        }
    }

    constexpr float kIScriptFrameSeconds = 1.0f / 24.0f;
    State.StepAccumulator += DeltaTime;
    bool Ran = false;
    while (State.StepAccumulator >= kIScriptFrameSeconds)
    {
        State.StepAccumulator -= kIScriptFrameSeconds;
        Ran = State.VM.ExecuteFrame() || Ran;
        if (!State.VM.IsRunning())
            break;
    }
    if (!Ran && NeedStart)
        State.VM.ExecuteFrame();

    ConsumeIScriptEvents(EntityID, State, Trans, Player, Combat);

    Render.FrameIndex = std::clamp(State.VM.GetCurrentFrame(), 0, std::max(0, Render.TotalFrames - 1));
    Render.Flipped = State.VM.GetContext().FlipState;

    if (!State.VM.IsRunning() &&
        State.AnimID == Game::IScriptAnims::Anim_WorkingToIdle)
    {
        State.WasWorking = false;
        State.AnimID = -1;
    }
    else if (!State.VM.IsRunning() &&
             State.AnimID == Game::IScriptAnims::Anim_Built)
    {
        State.WasUnderConstruction = false;
        State.AnimID = -1;
    }
    else
    {
        State.WasWorking = IsWorking;
        if (!Bldg || !Bldg->UnderConstruction)
            State.WasUnderConstruction = false;
    }

    return true;
}

void AnimationSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;
    auto& EM = mSystemMgr.GetEntityManager();
    UpdateScriptEffects(DeltaTime);
    UpdateImageTrees(DeltaTime);

    auto Entities = EM.GetEntitiesWith<TransformComponent, RenderComponent>();
    for (int Eid : Entities)
    {
        auto* Render = EM.GetComponent<RenderComponent>(Eid);
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Trans = EM.GetComponent<TransformComponent>(Eid);
        if (!Render || !Trans) continue;

        auto* Cmd = EM.GetComponent<CommandComponent>(Eid);
        auto* Combat = EM.GetComponent<CombatComponent>(Eid);
        auto* Path = EM.GetComponent<PathfindingComponent>(Eid);
        auto* Bldg = EM.GetComponent<BuildingComponent>(Eid);
        auto* Morph = EM.GetComponent<MorphComponent>(Eid);
        auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);

        if (UpdateIScriptAnimation(Eid, DeltaTime, *Render, *Trans, Player, Combat, Bldg, Prod))
            continue;

        AnimState State = GetAnimStateFromComponents(Cmd, Combat, Path, Unit, Bldg, Morph);

        // Look up animation clip
        int ChkID = Unit ? Unit->ChkUnitID : 0;
        auto It = sAnimDB.find(ChkID);
        if (It == sAnimDB.end()) It = sAnimDB.find(0); // default

        const AnimClip* BestClip = nullptr;
        if (It != sAnimDB.end())
        {
            for (auto& Clip : It->second.Clips)
            {
                if (Clip.State == State)
                {
                    BestClip = &Clip;
                    if (State == AnimState::Walking && Clip.Flipped && Trans && Path && !Path->Path.empty())
                    {
                        Render->Flipped = (Path->Path[Path->PathIndex].X < Trans->Position.X);
                    }
                    break;
                }
            }
        }

        if (BestClip)
        {
            const int ClipFrameCount = std::max(1, BestClip->FrameCount);
            Render->TotalFrames = std::max(Render->TotalFrames, BestClip->StartFrame + ClipFrameCount);
            float CycleSpeed = BestClip->FrameDuration > 0.0f ? (1.0f / BestClip->FrameDuration) : 6.0f;
            int LocalFrame = Render->FrameIndex - BestClip->StartFrame;
            if (LocalFrame < 0 || LocalFrame >= ClipFrameCount)
                LocalFrame = 0;
            float frameProgress = (float)LocalFrame + DeltaTime * CycleSpeed;

            if (BestClip->Loop)
            {
                while (frameProgress >= (float)ClipFrameCount)
                    frameProgress -= (float)ClipFrameCount;
                Render->FrameIndex = BestClip->StartFrame + (int)frameProgress;
            }
            else
            {
                Render->FrameIndex = BestClip->StartFrame + std::min(ClipFrameCount - 1, (int)frameProgress);
            }
        }
        else
        {
            Render->TotalFrames = std::max(1, Render->TotalFrames);
            // No clip data yet: hold the frame selected by movement/building state.
            // Buildings need their real iscript idle/working sequences; full-GRP
            // cycling runs into construction/working/death frames and looks wrong.
            Render->FrameIndex = std::clamp(Render->FrameIndex, 0, Render->TotalFrames - 1);
        }
    }
}

} // namespace Game::Systems
