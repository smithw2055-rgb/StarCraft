#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Components.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Game::Systems
{

using namespace Game::Components;

namespace
{
    bool DebugIScriptRuntimeEnabled()
    {
        const char* Value = std::getenv("STARCRAFT_DEBUG_ISCRIPT_RUNTIME");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    int DirectionToIScriptDirection(Core::Math::Vec2 Direction)
    {
        if (Direction.Length() <= 0.001f)
            Direction = {1.0f, 0.0f};
        const float Angle = std::atan2(Direction.X, -Direction.Y);
        float Normalized = Angle;
        if (Normalized < 0.0f)
            Normalized += 6.28318530718f;
        return (int)std::lround(Normalized * 256.0f / 6.28318530718f) & 0xff;
    }

    Core::Math::Vec2 QuantizeDirectionToEightWay(Core::Math::Vec2 Direction)
    {
        if (Direction.Length() <= 0.001f)
            return {1.0f, 0.0f};
        static const Core::Math::Vec2 Directions[8] = {
            {1.0f, 0.0f},
            {0.7071f, 0.7071f},
            {0.0f, 1.0f},
            {-0.7071f, 0.7071f},
            {-1.0f, 0.0f},
            {-0.7071f, -0.7071f},
            {0.0f, -1.0f},
            {0.7071f, -0.7071f}
        };
        const Core::Math::Vec2 Normalized = Direction.Normalized();
        float BestDot = -2.0f;
        Core::Math::Vec2 Best = Directions[0];
        for (const auto& Candidate : Directions)
        {
            const float Dot = Normalized.Dot(Candidate);
            if (Dot > BestDot)
            {
                BestDot = Dot;
                Best = Candidate;
            }
        }
        return Best;
    }

    Core::Math::Vec2 ResolveMovementDirection(const TransformComponent& Trans,
                                              const PathfindingComponent* Path)
    {
        if (!Path)
            return {std::cos(0.0f), std::sin(0.0f)};
        if (Path->CurrentVelocity.Length() > 0.1f)
            return Path->CurrentVelocity.Normalized();
        if (!Path->Path.empty() && Path->PathIndex < (int)Path->Path.size())
        {
            const auto Delta = Path->Path[(size_t)Path->PathIndex] - Trans.Position;
            if (Delta.Length() > 0.001f)
                return Delta.Normalized();
        }
        return {std::cos(Path->FacingAngle), std::sin(Path->FacingAngle)};
    }

    Core::Math::Vec2 ResolveAttackDirection(Core::ECS::EntityManager& EM,
                                            const TransformComponent& Trans,
                                            const CombatComponent* Combat)
    {
        if (!Combat)
            return {std::cos(0.0f), std::sin(0.0f)};

        auto DirectionToEntity = [&](int EntityID) -> Core::Math::Vec2
        {
            if (EntityID < 0)
                return {};
            if (auto* TargetTrans = EM.GetComponent<TransformComponent>(EntityID))
            {
                const auto Delta = TargetTrans->Position - Trans.Position;
                if (Delta.Length() > 0.001f)
                    return Delta.Normalized();
            }
            return {};
        };

        if (Combat->PendingAttackFrame)
        {
            const auto PendingEntityDirection = DirectionToEntity(Combat->PendingAttackTargetEntityID);
            if (PendingEntityDirection.Length() > 0.001f)
                return PendingEntityDirection;

            const auto PendingPosDelta = Combat->PendingAttackTargetPos - Trans.Position;
            if (PendingPosDelta.Length() > 0.001f)
                return PendingPosDelta.Normalized();
        }

        const auto TargetEntityDirection = DirectionToEntity(Combat->TargetEntityID);
        if (TargetEntityDirection.Length() > 0.001f)
            return TargetEntityDirection;

        return {std::cos(0.0f), std::sin(0.0f)};
    }

    void ApplyAttackDirection(Core::ECS::EntityManager& EM,
                              const TransformComponent& Trans,
                              const CombatComponent* Combat,
                              Game::IScriptVM::SpriteContext& Ctx)
    {
        const auto Direction = QuantizeDirectionToEightWay(
            ResolveAttackDirection(EM, Trans, Combat));
        Ctx.CurrentDirection = DirectionToIScriptDirection(Direction);
        Ctx.FlDirect = Ctx.CurrentDirection;

        if (Combat && Combat->PendingAttackFrame)
        {
            Core::Math::Vec2 Delta = Combat->PendingAttackTargetPos - Trans.Position;
            if (Combat->PendingAttackTargetEntityID >= 0)
            {
                if (auto* TargetTrans = EM.GetComponent<TransformComponent>(Combat->PendingAttackTargetEntityID))
                    Delta = TargetTrans->Position - Trans.Position;
            }
            if (Delta.Length() > 0.001f)
                Ctx.TargetRangePixels = (int)Delta.Length();
            Ctx.TargetArc = 0;
        }
    }

    bool IsFrozenByStatus(const StatusEffectComponent* Status)
    {
        return Status && (Status->HasEffect(StatusEffect::Stasis) ||
                          Status->HasEffect(StatusEffect::Lockdown) ||
                          Status->HasEffect(StatusEffect::Maelstrom));
    }

    Core::Math::Vec2 ResolveAnimationDirection(Core::ECS::EntityManager& EM,
                                               AnimState DesiredState,
                                               const TransformComponent& Trans,
                                               const PathfindingComponent* Path,
                                               const CombatComponent* Combat)
    {
        if (DesiredState == AnimState::Attacking || (Combat && Combat->PendingAttackFrame))
            return QuantizeDirectionToEightWay(ResolveAttackDirection(EM, Trans, Combat));
        return QuantizeDirectionToEightWay(ResolveMovementDirection(Trans, Path));
    }
}

std::unordered_map<int, AnimationSystem::UnitAnimData> AnimationSystem::sAnimDB;
Game::IScriptDatabase AnimationSystem::sIScriptDB;
bool AnimationSystem::sTriedLoadIScript = false;
AnimationSystem::OverlayOffsetResolver AnimationSystem::sOverlayOffsetResolver;

void AnimationSystem::SetOverlayOffsetResolver(OverlayOffsetResolver Resolver)
{
    sOverlayOffsetResolver = std::move(Resolver);
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
    if (!Script)
        return false;

    auto HasAnim = [&](int CandidateAnimID) -> bool
    {
        return CandidateAnimID >= 0 &&
               CandidateAnimID < (int)Script->AnimationPC.size() &&
               Script->AnimationPC[(size_t)CandidateAnimID] > 0;
    };
    return ResolvePlayableIScriptAnimation(AnimID, HasAnim) >= 0;
}

int AnimationSystem::DirectionalFrameIndex(int BaseFrame,
                                           int Direction,
                                           int TotalFrames,
                                           bool HasDirectionalFrames,
                                           bool* OutFlipped)
{
    if (OutFlipped)
        *OutFlipped = false;
    if (!HasDirectionalFrames || TotalFrames <= 0)
        return std::clamp(BaseFrame, 0, std::max(0, TotalFrames - 1));

    const int Dir32 = ((Direction & 0xff) + 4) / 8;
    const int ClampedDir32 = Dir32 >= 32 ? 0 : Dir32;
    const bool Flip = ClampedDir32 > 16;
    const int DirectionFrame = Flip ? 32 - ClampedDir32 : ClampedDir32;
    if (OutFlipped)
        *OutFlipped = Flip;
    return std::clamp(BaseFrame + DirectionFrame, 0, std::max(0, TotalFrames - 1));
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
        auto* Combat = EM.GetComponent<CombatComponent>(ID);
        auto* RootRender = EM.GetComponent<RenderComponent>(ID);
        const auto RootState = mScriptStates.find(ID);
        if (!Tree)
            continue;
        const size_t InitialCount = Tree->Nodes.size();
        for (size_t I = 0; I < InitialCount && I < Tree->Nodes.size(); ++I)
        {
            auto& Node = Tree->Nodes[I];
            if (Node.Visible && Node.UseSpecialOffset && RootRender && sOverlayOffsetResolver)
            {
                const auto Offset = sOverlayOffsetResolver(
                    RootRender->ImageID, 2, (size_t)std::max(0, RootRender->FrameIndex),
                    0, RootRender->Flipped);
                if (Offset)
                    Node.Offset = *Offset;
            }
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
                if (Node.InheritSpriteDirection && RootState != mScriptStates.end())
                {
                    const auto& RootContext = RootState->second.VM.GetContext();
                    auto& NodeContext = Node.VM.GetContext();
                    NodeContext.CurrentDirection = RootContext.CurrentDirection;
                    NodeContext.FlDirect = RootContext.FlDirect;
                    Node.VM.RefreshDirectionalFrame();
                }
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
                if (Node.InheritSpriteDirection && RootState != mScriptStates.end())
                {
                    const auto& RootContext = RootState->second.VM.GetContext();
                    auto& NodeContext = Node.VM.GetContext();
                    NodeContext.CurrentDirection = RootContext.CurrentDirection;
                    NodeContext.FlDirect = RootContext.FlDirect;
                    Node.VM.RefreshDirectionalFrame();
                }
                const auto& NodeContext = Node.VM.GetContext();
                if (NodeContext.HorPosSet)
                    Node.Offset.X = (float)NodeContext.HorPos;
                if (NodeContext.VertPosSet)
                    Node.Offset.Y = (float)NodeContext.VertPos;
                Node.FrameIndex = std::max(0, Node.VM.GetCurrentFrame());
                Node.Flipped = Node.VM.GetContext().FlipState;
                if (Trans)
                {
                    const Core::Math::Vec2 NodeOffset = Node.Offset;
                    const float NodeLifetime = Node.LifetimeRemaining;
                    ConsumeImageNodeEvents(
                        ID, (int)I, NodeOffset, NodeLifetime, *Trans, Player, Combat);
                    if (!Node.VM.IsRunning())
                    {
                        Node.Visible = false;
                        MarkImageSubtreeHidden(*Tree, (int)I);
                    }
                }
            }
        }
        // ParentIndex stores vector indices, so keep node slots stable even after
        // a temporary overlay expires. AddImageTreeNode can reuse hidden slots.
    }
}

void AnimationSystem::ClearImageTree(int EntityID, bool PreservePersistentAttachments)
{
    if (auto* Tree = mSystemMgr.GetEntityManager().GetComponent<ImageTreeComponent>(EntityID))
    {
        if (!PreservePersistentAttachments)
        {
            Tree->Nodes.clear();
            return;
        }

        for (size_t I = 0; I < Tree->Nodes.size(); ++I)
        {
            if (!Tree->Nodes[I].PersistentSpriteAttachment)
            {
                Tree->Nodes[I].Visible = false;
                MarkImageSubtreeHidden(*Tree, (int)I);
            }
        }
    }
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
                                       bool UseSpriteID,
                                       bool PersistentSpriteAttachment,
                                       bool UseSpecialOffset)
{
    AddImageTreeNode(EntityID, -1, ImageID, Core::Math::Vec2{(float)OffsetX, (float)OffsetY},
                     DrawAbove, Flipped, Lifetime, UseSpriteID,
                     PersistentSpriteAttachment, UseSpecialOffset);
}

void AnimationSystem::AddImageTreeNode(int EntityID,
                                       int ParentIndex,
                                       int ImageID,
                                       Core::Math::Vec2 Offset,
                                       bool DrawAbove,
                                       bool Flipped,
                                       float Lifetime,
                                       bool UseSpriteID,
                                       bool PersistentSpriteAttachment,
                                       bool UseSpecialOffset)
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
            ((Node.UseSpecialOffset && UseSpecialOffset) ||
             (Node.CreationOffset.X == Offset.X && Node.CreationOffset.Y == Offset.Y)) &&
            Node.DrawAbove == DrawAbove &&
            Node.UseSpriteID == UseSpriteID)
        {
            Node.Visible = true;
            Node.Flipped = Flipped;
            Node.LifetimeRemaining = Lifetime;
            Node.PersistentSpriteAttachment =
                Node.PersistentSpriteAttachment || PersistentSpriteAttachment;
            Node.UseSpecialOffset = Node.UseSpecialOffset || UseSpecialOffset;
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
            Node.CreationOffset = Offset;
            Node.DrawAbove = DrawAbove;
            Node.Flipped = Flipped;
            Node.UseSpriteID = UseSpriteID;
            Node.Visible = true;
            Node.LifetimeRemaining = Lifetime;
            Node.PersistentSpriteAttachment = PersistentSpriteAttachment;
            Node.UseSpecialOffset = UseSpecialOffset;
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
    Node.CreationOffset = Offset;
    Node.DrawAbove = DrawAbove;
    Node.Flipped = Flipped;
    Node.UseSpriteID = UseSpriteID;
    Node.Visible = true;
    Node.LifetimeRemaining = Lifetime;
    Node.PersistentSpriteAttachment = PersistentSpriteAttachment;
    Node.UseSpecialOffset = UseSpecialOffset;
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
    Node.VM.GetContext().HasDirectionalFrames = ImageInfo->HasDirectionalFrames;
    Node.InheritSpriteDirection = ImageInfo->HasDirectionalFrames;
}

void AnimationSystem::CreateGasOverlay(int EntityID,
                                       int ParentIndex,
                                       int SourceImageID,
                                       int SourceFrame,
                                       bool SourceFlipped,
                                       int OffsetIndex,
                                       float Lifetime)
{
    if (!sOverlayOffsetResolver || SourceImageID < 0 || SourceFrame < 0 || OffsetIndex < 0)
        return;

    auto& EM = mSystemMgr.GetEntityManager();
    const auto* Resource = EM.GetComponent<ResourceComponent>(EntityID);
    if (!Resource)
        return;

    constexpr int VespeneGeyserSmoke1 = 430;
    constexpr int VespeneGeyserSmoke1Overlay = 435;
    const auto Offset = sOverlayOffsetResolver((uint32_t)SourceImageID, 2,
                                               (size_t)SourceFrame, (size_t)OffsetIndex,
                                               SourceFlipped);
    if (!Offset)
        return;

    const int OverlayImage = (Resource->Amount > 0
        ? VespeneGeyserSmoke1
        : VespeneGeyserSmoke1Overlay) + OffsetIndex;
    AddImageTreeNode(EntityID, ParentIndex, OverlayImage, *Offset,
                     true, false, Lifetime);
}

void AnimationSystem::ConsumeImageNodeEvents(int EntityID,
                                             int NodeIndex,
                                             Core::Math::Vec2 NodeOffset,
                                             float NodeLifetime,
                                             const TransformComponent& Trans,
                                             const PlayerComponent* Player,
                                             CombatComponent* Combat)
{
    auto* Tree = mSystemMgr.GetEntityManager().GetComponent<ImageTreeComponent>(EntityID);
    if (!Tree || NodeIndex < 0 || NodeIndex >= (int)Tree->Nodes.size())
        return;

    const int SourceImageID = (int)Tree->Nodes[(size_t)NodeIndex].ImageID;
    const int SourceFrame = Tree->Nodes[(size_t)NodeIndex].FrameIndex;
    const bool SourceFlipped = Tree->Nodes[(size_t)NodeIndex].Flipped;
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
                                      0, UnitType,
                                      Core::Audio::SoundFlags::PerSound);
                }
                break;
            case Game::IScriptEventType::CreateImageOverlay:
                AddImageTreeNode(EntityID, NodeIndex, Event.A,
                                 Core::Math::Vec2{(float)Event.B, (float)Event.C},
                                 Event.D != 0, false, NodeLifetime);
                break;
            case Game::IScriptEventType::CreateImageOverlaySpecialOffset:
                AddImageTreeNode(EntityID, NodeIndex, Event.A, {},
                                 Event.D != 0, false, NodeLifetime,
                                 false, false, true);
                break;
            case Game::IScriptEventType::CreateSpriteOverlay:
                AddImageTreeNode(EntityID, NodeIndex, Event.A,
                                 Core::Math::Vec2{(float)Event.B, (float)Event.C},
                                 Event.D != 0, false, NodeLifetime, true);
                break;
            case Game::IScriptEventType::CreateGasOverlays:
                CreateGasOverlay(EntityID, NodeIndex, SourceImageID, SourceFrame,
                                 SourceFlipped, Event.A, NodeLifetime);
                break;
            case Game::IScriptEventType::FlightState:
                break;
            case Game::IScriptEventType::Attack:
            case Game::IScriptEventType::AttackMelee:
            case Game::IScriptEventType::UseWeapon:
            case Game::IScriptEventType::DoMissileDamage:
            case Game::IScriptEventType::DoGroundDamage:
            case Game::IScriptEventType::CastSpell:
                if (Combat && Combat->PendingAttackFrame && mBulletSystem)
                    CombatSystem::ResolvePendingAttackFrame(
                        mSystemMgr.GetEntityManager(), *mBulletSystem, EntityID);
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
                    mAudio->PlaySound(Event.A, Trans.Position.X, Trans.Position.Y,
                                      0, UnitType,
                                      Core::Audio::SoundFlags::PerSound);
                }
                break;
            case Game::IScriptEventType::CreateImageOverlay:
            {
                const bool PersistentAttachment =
                    State.AnimID == Game::IScriptAnims::Anim_Init;
                AddImageTreeNode(EntityID, Event.A, Event.B, Event.C,
                                 Event.D != 0, false,
                                 TransientOverlay ? 0.35f : -1.0f,
                                 false, PersistentAttachment);
                break;
            }
            case Game::IScriptEventType::CreateImageOverlaySpecialOffset:
            {
                const bool PersistentAttachment =
                    State.AnimID == Game::IScriptAnims::Anim_Init;
                AddImageTreeNode(EntityID, Event.A, 0, 0,
                                 Event.D != 0, false,
                                 TransientOverlay ? 0.35f : -1.0f,
                                 false, PersistentAttachment, true);
                break;
            }
            case Game::IScriptEventType::CreateSpriteOverlay:
                AddImageTreeNode(EntityID, Event.A, Event.B, Event.C,
                                 Event.D != 0, false,
                                 TransientOverlay ? 0.35f : -1.0f,
                                 true);
                break;
            case Game::IScriptEventType::WarpOverlay:
                // OpenBW stores the packed operand as image modifier data; it is
                // not an image id and must not create a standalone overlay.
                break;
            case Game::IScriptEventType::CreateGasOverlays:
            {
                const auto* Render = mSystemMgr.GetEntityManager().GetComponent<RenderComponent>(EntityID);
                if (Render)
                {
                    CreateGasOverlay(EntityID, -1, State.ImageID, Render->FrameIndex,
                                     Render->Flipped, Event.A, -1.0f);
                }
                break;
            }
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
            case Game::IScriptEventType::SpawnFrame:
            case Game::IScriptEventType::TemporaryGraphicState:
                break;
            case Game::IScriptEventType::FlightState:
                break;
        }
    }
}

bool AnimationSystem::UpdateIScriptAnimation(int EntityID,
                                             float DeltaTime,
                                             AnimState DesiredState,
                                             RenderComponent& Render,
                                             const TransformComponent& Trans,
                                             const PathfindingComponent* Path,
                                             const PlayerComponent* Player,
                                             CombatComponent* Combat,
                                             const BuildingComponent* Bldg,
                                             const StatusEffectComponent* Status,
                                             const ProductionComponent* Prod)
{
    auto& State = mScriptStates[EntityID];

    const bool IsEffect = mSystemMgr.GetEntityManager().GetComponent<EffectComponent>(EntityID) != nullptr;
    if (IsEffect && !mSystemMgr.GetEntityManager().GetComponent<UnitComponent>(EntityID))
        return false;

    const bool IsBuilding = Bldg != nullptr;
    const bool UnderConstruction = Bldg && Bldg->UnderConstruction;
    const float ConstructionProgress =
        (Bldg && Bldg->TotalBuildTime > 0.0f)
            ? std::clamp(Bldg->BuildProgress / Bldg->TotalBuildTime, 0.0f, 1.0f)
            : 0.0f;
    const bool IsWorking =
        Prod && (Prod->CurrentTrainingUnitDefID >= 0 || !Prod->Queue.empty());
    const bool IsFrozen = IsFrozenByStatus(Status);
    const bool IsPendingAttack =
        Combat && Combat->PendingAttackFrame && !IsFrozen;

    EnsureIScriptLoaded();
    if (!sIScriptDB.IsLoaded())
        return false;

    const auto* ImageInfo = sIScriptDB.FindImageInfo((int)Render.ImageID);
    if (!ImageInfo || ImageInfo->IScriptID < 0)
        return false;

    const auto* Script = sIScriptDB.FindScript(ImageInfo->IScriptID);
    if (!Script)
        return false;

    static int DebugIScriptRuntimeLogs = 80;

    auto HasAnim = [&](int AnimID) -> bool
    {
        return AnimID >= 0 &&
               AnimID < (int)Script->AnimationPC.size() &&
               Script->AnimationPC[(size_t)AnimID] > 0;
    };

    if (UnderConstruction)
        State.HasPlayedBuilt = false;
    const bool NeedsInitialBuilt =
        IsBuilding &&
        !UnderConstruction &&
        !State.HasPlayedBuilt &&
        HasAnim(Game::IScriptAnims::Anim_Built);

    int TargetAnim = SelectIScriptAnimationForState(
        DesiredState,
        IsBuilding,
        UnderConstruction,
        ConstructionProgress,
        State.WasUnderConstruction || NeedsInitialBuilt,
        IsWorking,
        State.WasWorking,
        State.WasWalking,
        IsPendingAttack,
        HasAnim);
    if (TargetAnim < 0)
        return false;

    const bool IsTransientAnim =
        IsPendingAttack ||
        UnderConstruction ||
        IsWorking ||
        State.WasWorking ||
        (State.WasUnderConstruction && TargetAnim == Game::IScriptAnims::Anim_Built) ||
        TargetAnim == Game::IScriptAnims::Anim_WalkingToIdle;
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
    const bool ImageOrScriptChanged =
        State.ImageID != (int)Render.ImageID ||
        State.ScriptID != ImageInfo->IScriptID;

    if (NeedStart)
    {
        const int PreviousAnim = State.AnimID;
        const int EntryPC = Script->AnimationPC[(size_t)TargetAnim];
        State.VM.LoadScript(TargetAnim, &sIScriptDB.GetProgram().ProgramData, EntryPC);
        auto& Ctx = State.VM.GetContext();
        Ctx.ImageID = (int)Render.ImageID;
        Ctx.BaseFrameIndex = 0;
        Ctx.FrameIndex = 0;
        Ctx.HasDirectionalFrames = ImageInfo->HasDirectionalFrames;
        Ctx.IsAttacking = false;
        State.ImageID = (int)Render.ImageID;
        State.ScriptID = ImageInfo->IScriptID;
        State.AnimID = TargetAnim;
        State.StepAccumulator = 0.0f;
        if (ShouldClearImageTreeForAnimRestart(
                IsBuilding, ImageOrScriptChanged, PreviousAnim, TargetAnim, UnderConstruction))
        {
            ClearImageTree(EntityID, !ImageOrScriptChanged);
        }
    }

    auto& Ctx = State.VM.GetContext();
    Ctx.LiftOffState = Bldg && Bldg->IsLifted;
    if (!IsBuilding)
    {
        const Core::Math::Vec2 AnimDirection =
            ResolveAnimationDirection(
                mSystemMgr.GetEntityManager(),
                DesiredState,
                Trans,
                Path,
                IsFrozen ? nullptr : Combat);
        Ctx.CurrentDirection = DirectionToIScriptDirection(AnimDirection);
        Ctx.FlDirect = Ctx.CurrentDirection;
        if (Ctx.HasDirectionalFrames)
            State.VM.RefreshDirectionalFrame();
    }

    if (IsPendingAttack)
    {
        Ctx.IsAttacking = true;
        ApplyAttackDirection(mSystemMgr.GetEntityManager(), Trans, Combat, Ctx);
        State.VM.RefreshDirectionalFrame();
    }
    else
    {
        Ctx.IsAttacking = false;
    }
    Ctx.IsWalking = DesiredState == AnimState::Walking;

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

    if (!IsBuilding && Ctx.HasDirectionalFrames)
        State.VM.RefreshDirectionalFrame();

    ConsumeIScriptEvents(EntityID, State, Trans, Player, Combat);

    const int ScriptFrame = std::max(0, State.VM.GetCurrentFrame());
    Render.TotalFrames = std::max(Render.TotalFrames, ScriptFrame + 1);
    Render.FrameIndex = std::clamp(ScriptFrame, 0, std::max(0, Render.TotalFrames - 1));
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
        State.HasPlayedBuilt = true;
        State.AnimID = -1;
    }
    else if (!State.VM.IsRunning() &&
             State.AnimID == Game::IScriptAnims::Anim_WalkingToIdle)
    {
        State.WasWalking = false;
        State.AnimID = -1;
    }
    else
    {
        State.WasWorking = IsWorking;
        State.WasWalking = DesiredState == AnimState::Walking;
        if (UnderConstruction ||
            (State.AnimID == Game::IScriptAnims::Anim_Built && State.VM.IsRunning()))
        {
            State.WasUnderConstruction = true;
        }
        else if (!Bldg || !Bldg->UnderConstruction)
        {
            State.WasUnderConstruction = false;
        }
    }

    const bool DebugEntityImportant =
        Player && (Player->PlayerID == 0 || Player->PlayerID == 1);
    if (DebugIScriptRuntimeEnabled() && DebugIScriptRuntimeLogs > 0 && DebugEntityImportant)
    {
        --DebugIScriptRuntimeLogs;
        Core::Utils::Logger::Info("IScriptRuntime",
            "entity=" + std::to_string(EntityID) +
            " image=" + std::to_string(Render.ImageID) +
            " script=" + std::to_string(ImageInfo->IScriptID) +
            " targetAnim=" + std::to_string(TargetAnim) +
            " frame=" + std::to_string(Render.FrameIndex) +
            " total=" + std::to_string(Render.TotalFrames) +
            " init=" + std::to_string(HasAnim(Game::IScriptAnims::Anim_Init) ? 1 : 0) +
            " starEdit=" + std::to_string(HasAnim(Game::IScriptAnims::Anim_StarEditInit) ? 1 : 0) +
            " built=" + std::to_string(HasAnim(Game::IScriptAnims::Anim_Built) ? 1 : 0) +
            " almostBuilt=" + std::to_string(HasAnim(Game::IScriptAnims::Anim_AlmostBuilt) ? 1 : 0));
    }

    return true;
}

void AnimationSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;
    auto& EM = mSystemMgr.GetEntityManager();
    UpdateScriptEffects(DeltaTime);

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
        auto* Status = EM.GetComponent<StatusEffectComponent>(Eid);
        auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);

        AnimState State = GetAnimStateFromComponents(Cmd, Combat, Path, Unit, Bldg, Morph, Status);

        if (UpdateIScriptAnimation(Eid, DeltaTime, State, *Render, *Trans, Path, Player, Combat, Bldg, Status, Prod))
            continue;

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
            bool HasDirectionalFrames = false;
            if (!Bldg && Render->TotalFrames >= 17)
            {
                EnsureIScriptLoaded();
                if (sIScriptDB.IsLoaded())
                    if (const auto* ImageInfo = sIScriptDB.FindImageInfo((int)Render->ImageID))
                        HasDirectionalFrames = ImageInfo->HasDirectionalFrames;
            }

            if (HasDirectionalFrames)
            {
                const Core::Math::Vec2 Direction =
                    State == AnimState::Attacking
                        ? ResolveAttackDirection(EM, *Trans, Combat)
                        : ResolveMovementDirection(*Trans, Path);
                const int IScriptDirection = DirectionToIScriptDirection(Direction);
                bool Flipped = false;
                Render->FrameIndex = DirectionalFrameIndex(0, IScriptDirection,
                                                           Render->TotalFrames,
                                                           HasDirectionalFrames,
                                                           &Flipped);
                Render->Flipped = Flipped;
                continue;
            }

            // No clip data yet: hold the frame selected by movement/building state.
            // Buildings need their real iscript idle/working sequences; full-GRP
            // cycling runs into construction/working/death frames and looks wrong.
            Render->FrameIndex = std::clamp(Render->FrameIndex, 0, Render->TotalFrames - 1);
        }
    }

    // Root scripts establish the current sprite heading and may create image
    // overlays. Advance those overlays afterwards so a newly-created shadow
    // receives the same heading in this frame.
    UpdateImageTrees(DeltaTime);
}

} // namespace Game::Systems
