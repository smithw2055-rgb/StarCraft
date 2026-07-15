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
    constexpr int kScvMiningWeaponID = 14;
    constexpr uint32_t kCommandCenterImageID = 275u;
    constexpr uint32_t kCommandCenterControlImageID = 276u;

    uint32_t CarriedResourceImageID(const UnitComponent* Unit,
                                    const HarvestComponent* Harvest)
    {
        if (!Unit || !Harvest || Harvest->CarryingAmount <= 0)
            return UINT32_MAX;
        if (Harvest->CarryingType == ResourceType::Minerals)
            return 397u;
        if (Harvest->CarryingType != ResourceType::VespeneGas)
            return UINT32_MAX;
        if (Unit->ChkUnitID == 41)
            return 401u;
        if (Unit->ChkUnitID == 64)
            return 399u;
        return 403u;
    }

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

    const ResourceComponent* ResolveMiningResource(Core::ECS::EntityManager& EM,
                                                   const HarvestComponent* Harvest)
    {
        if (!Harvest || Harvest->State != HarvestState::Mining ||
            Harvest->TargetResourceID < 0)
        {
            return nullptr;
        }
        return EM.GetComponent<ResourceComponent>(Harvest->TargetResourceID);
    }
}

std::unordered_map<int, AnimationSystem::UnitAnimData> AnimationSystem::sAnimDB;
Game::IScriptDatabase AnimationSystem::sIScriptDB;
bool AnimationSystem::sTriedLoadIScript = false;
std::string AnimationSystem::sIScriptPath = "assets/scripts/iscript.bin";
std::string AnimationSystem::sImagesDatPath = "assets/arr/images.dat";
AnimationSystem::OverlayOffsetResolver AnimationSystem::sOverlayOffsetResolver;
AnimationSystem::WeaponImageResolver AnimationSystem::sWeaponImageResolver;
AnimationSystem::ImageAttachmentPersistenceResolver
    AnimationSystem::sImageAttachmentPersistenceResolver;

bool AnimationSystem::InitializeIScriptAssets(const std::string& IScriptPath,
                                              const std::string& ImagesDatPath)
{
    if (IScriptPath.empty() || ImagesDatPath.empty())
        return false;

    if (sIScriptPath != IScriptPath || sImagesDatPath != ImagesDatPath ||
        !sIScriptDB.IsLoaded())
    {
        sIScriptPath = IScriptPath;
        sImagesDatPath = ImagesDatPath;
        sIScriptDB = {};
        sTriedLoadIScript = false;
    }
    EnsureIScriptLoaded();
    return sIScriptDB.IsLoaded();
}

void AnimationSystem::SetOverlayOffsetResolver(OverlayOffsetResolver Resolver)
{
    sOverlayOffsetResolver = std::move(Resolver);
}

void AnimationSystem::SetWeaponImageResolver(WeaponImageResolver Resolver)
{
    sWeaponImageResolver = std::move(Resolver);
}

void AnimationSystem::SetImageAttachmentPersistenceResolver(
    ImageAttachmentPersistenceResolver Resolver)
{
    sImageAttachmentPersistenceResolver = std::move(Resolver);
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
    sIScriptDB.Load(sIScriptPath, sImagesDatPath);
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
    // Carried resources are runtime sprite attachments. Ensure workers that
    // currently have no other overlays still receive an image tree.
    for (int ID : EM.GetEntitiesWith<RenderComponent, UnitComponent, HarvestComponent>())
    {
        const auto* Harvest = EM.GetComponent<HarvestComponent>(ID);
        if (Harvest && Harvest->CarryingAmount > 0 &&
            !EM.GetComponent<ImageTreeComponent>(ID))
        {
            EM.AddComponent<ImageTreeComponent>(ID);
        }
    }
    auto Entities = EM.GetEntitiesWith<ImageTreeComponent>();
    for (int ID : Entities)
    {
        auto* Tree = EM.GetComponent<ImageTreeComponent>(ID);
        auto* Trans = EM.GetComponent<TransformComponent>(ID);
        auto* Player = EM.GetComponent<PlayerComponent>(ID);
        auto* Combat = EM.GetComponent<CombatComponent>(ID);
        auto* Path = EM.GetComponent<PathfindingComponent>(ID);
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Harvest = EM.GetComponent<HarvestComponent>(ID);
        auto* RootRender = EM.GetComponent<RenderComponent>(ID);
        const auto RootState = mScriptStates.find(ID);
        if (!Tree)
            continue;
        UpdateCarriedResourceOverlay(ID, *Tree, Unit, Harvest);
        const size_t InitialCount = Tree->Nodes.size();
        for (size_t I = 0; I < InitialCount && I < Tree->Nodes.size(); ++I)
        {
            auto& Node = Tree->Nodes[I];
            const auto RefreshNodeDirection = [&]()
            {
                if (!Node.InheritSpriteDirection)
                    return;
                auto& NodeContext = Node.VM.GetContext();
                if (Node.IsUnitSubunit && Trans)
                {
                    const bool HasCombatAim = Combat &&
                        (Combat->PendingAttackFrame || Combat->TargetEntityID >= 0);
                    const auto Direction = HasCombatAim
                        ? ResolveAttackDirection(EM, *Trans, Combat)
                        : ResolveMovementDirection(*Trans, Path);
                    NodeContext.CurrentDirection = DirectionToIScriptDirection(
                        QuantizeDirectionToEightWay(Direction));
                    NodeContext.FlDirect = NodeContext.CurrentDirection;
                }
                else if (Node.ParentIndex >= 0 &&
                         Node.ParentIndex < (int)Tree->Nodes.size())
                {
                    const auto& ParentContext =
                        Tree->Nodes[(size_t)Node.ParentIndex].VM.GetContext();
                    NodeContext.CurrentDirection = ParentContext.CurrentDirection;
                    NodeContext.FlDirect = ParentContext.FlDirect;
                }
                else if (RootState != mScriptStates.end())
                {
                    const auto& RootContext = RootState->second.VM.GetContext();
                    NodeContext.CurrentDirection = RootContext.CurrentDirection;
                    NodeContext.FlDirect = RootContext.FlDirect;
                }
                Node.VM.RefreshDirectionalFrame();
            };
            if (Node.Visible && RootRender && sOverlayOffsetResolver &&
                (Node.UseSpecialOffset || Node.OverlayOffsetType >= 0))
            {
                uint32_t OffsetSourceImageID = RootRender->ImageID;
                int OffsetSourceFrame = RootRender->FrameIndex;
                bool OffsetSourceFlipped = RootRender->Flipped;
                if (Node.ParentIndex >= 0 &&
                    Node.ParentIndex < (int)Tree->Nodes.size())
                {
                    const auto& Parent = Tree->Nodes[(size_t)Node.ParentIndex];
                    OffsetSourceImageID = Parent.ImageID;
                    OffsetSourceFrame = Parent.FrameIndex;
                    OffsetSourceFlipped = Parent.Flipped;
                }
                const size_t LoType = Node.OverlayOffsetType >= 0
                    ? (size_t)Node.OverlayOffsetType
                    : 2u;
                const size_t OffsetIndex = Node.OverlayOffsetType >= 0
                    ? (size_t)std::max(0, Node.OverlayOffsetIndex)
                    : 0u;
                const auto Offset = sOverlayOffsetResolver(
                    OffsetSourceImageID, LoType,
                    (size_t)std::max(0, OffsetSourceFrame),
                    OffsetIndex, OffsetSourceFlipped);
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
                RefreshNodeDirection();
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
                RefreshNodeDirection();
                auto& NodeContext = Node.VM.GetContext();
                if (NodeContext.FollowMainGraphic && RootRender)
                {
                    Node.FollowsMainGraphic = true;
                    if (RootState != mScriptStates.end())
                    {
                        NodeContext.BaseFrameIndex =
                            RootState->second.VM.GetContext().BaseFrameIndex;
                    }
                    else
                    {
                        NodeContext.BaseFrameIndex = RootRender->FrameIndex;
                    }
                    NodeContext.FrameIndex = RootRender->FrameIndex;
                    NodeContext.FlipState = RootRender->Flipped;
                }
                NodeContext.FollowMainGraphic = false;
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
                    if (!Node.VM.IsRunning() &&
                        !(Node.PersistentSpriteAttachment &&
                          Node.LifetimeRemaining < 0.0f))
                    {
                        Node.Visible = false;
                        MarkImageSubtreeHidden(*Tree, (int)I);
                    }
                }
            }
            // A persistent attachment may finish its Init script, but it must
            // still rotate with the parent sprite. OpenBW calls
            // set_sprite_images_heading_by_index() for every image, including
            // the carried mineral/gas image.
            if (Node.Visible && Node.InheritSpriteDirection)
            {
                RefreshNodeDirection();
                Node.FrameIndex = std::max(0, Node.VM.GetCurrentFrame());
                Node.Flipped = Node.VM.GetContext().FlipState;
            }
        }
        // ParentIndex stores vector indices, so keep node slots stable even after
        // a temporary overlay expires. AddImageTreeNode can reuse hidden slots.
    }
}

void AnimationSystem::EnsureUnitSubunitNode(
    int EntityID,
    const RenderComponent& Render)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* Tree = EM.GetComponent<ImageTreeComponent>(EntityID);
    ImageSubnode* Subunit = nullptr;
    if (Tree)
    {
        for (auto& Node : Tree->Nodes)
        {
            if (Node.IsUnitSubunit)
            {
                Subunit = &Node;
                break;
            }
        }
    }

    if (Render.SubunitImageID == UINT32_MAX)
    {
        if (Subunit)
            Subunit->Visible = false;
        return;
    }

    if (!Tree)
        Tree = &EM.AddComponent<ImageTreeComponent>(EntityID);
    if (!Subunit)
    {
        Tree->Nodes.emplace_back();
        Subunit = &Tree->Nodes.back();
    }
    if (Subunit->Visible && Subunit->ImageID == Render.SubunitImageID)
        return;

    *Subunit = ImageSubnode{};
    Subunit->ImageID = Render.SubunitImageID;
    Subunit->ParentIndex = -1;
    Subunit->DrawAbove = true;
    Subunit->Visible = true;
    Subunit->PersistentSpriteAttachment = true;
    Subunit->IsUnitSubunit = true;
    Subunit->LifetimeRemaining = -1.0f;
    StartImageNodeScript(*Subunit);
}

void AnimationSystem::UpdateCarriedResourceOverlay(
    int EntityID,
    ImageTreeComponent& Tree,
    const UnitComponent* Unit,
    const HarvestComponent* Harvest)
{
    const uint32_t DesiredImageID = CarriedResourceImageID(Unit, Harvest);
    ImageSubnode* CarryNode = nullptr;
    for (auto& Node : Tree.Nodes)
    {
        if (Node.IsCarriedResourceOverlay)
        {
            CarryNode = &Node;
            break;
        }
    }

    if (DesiredImageID == UINT32_MAX)
    {
        if (CarryNode)
            CarryNode->Visible = false;
        return;
    }

    if (CarryNode && CarryNode->ImageID == DesiredImageID)
    {
        CarryNode->Visible = true;
        CarryNode->PersistentSpriteAttachment = true;
        CarryNode->UseSpecialOffset = true;
        return;
    }

    if (!CarryNode)
    {
        for (auto& Node : Tree.Nodes)
        {
            if (!Node.Visible)
            {
                CarryNode = &Node;
                break;
            }
        }
    }
    if (!CarryNode)
    {
        if (Tree.Nodes.size() >= 64)
            return;
        Tree.Nodes.emplace_back();
        CarryNode = &Tree.Nodes.back();
    }

    *CarryNode = ImageSubnode{};
    CarryNode->ImageID = DesiredImageID;
    CarryNode->ParentIndex = -1;
    CarryNode->DrawAbove = true;
    CarryNode->Visible = true;
    CarryNode->PersistentSpriteAttachment = true;
    CarryNode->UseSpecialOffset = true;
    CarryNode->IsCarriedResourceOverlay = true;
    CarryNode->LifetimeRemaining = -1.0f;
    StartImageNodeScript(*CarryNode);
    if (DebugIScriptRuntimeEnabled())
    {
        Core::Utils::Logger::Info(
            "AnimationSystem",
            "attached carried resource image entity=" + std::to_string(EntityID) +
                " image=" + std::to_string(DesiredImageID) +
                " special_offset=2:0");
    }
}

void AnimationSystem::UpdateAddonConnectorOverlay(
    int EntityID,
    const RenderComponent& Render)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* Tree = EM.GetComponent<ImageTreeComponent>(EntityID);
    if (Render.AddonConnectorImageID == UINT32_MAX)
    {
        if (Tree)
        {
            for (auto& Node : Tree->Nodes)
            {
                if (Node.IsAddonConnectorOverlay)
                    Node.Visible = false;
            }
        }
        return;
    }

    AddImageTreeNode(EntityID, (int)Render.AddonConnectorImageID, 0, 0,
                     false, false, -1.0f, false, true);
    Tree = EM.GetComponent<ImageTreeComponent>(EntityID);
    if (!Tree)
        return;
    for (auto& Node : Tree->Nodes)
    {
        if (Node.ParentIndex == -1 &&
            Node.ImageID == Render.AddonConnectorImageID)
        {
            Node.IsAddonConnectorOverlay = true;
            Node.PersistentSpriteAttachment = true;
            Node.Visible = true;
            break;
        }
    }
}

void AnimationSystem::UpdateDamageOverlays(int EntityID,
                                           const RenderComponent& Render,
                                           const UnitComponent* Unit,
                                           const BuildingComponent* Building)
{
    auto& EM = mSystemMgr.GetEntityManager();
    auto* Tree = EM.GetComponent<ImageTreeComponent>(EntityID);
    auto HideExisting = [&]()
    {
        if (!Tree)
            return;
        for (size_t I = 0; I < Tree->Nodes.size(); ++I)
        {
            if (!Tree->Nodes[I].IsDamageOverlay)
                continue;
            Tree->Nodes[I].Visible = false;
            MarkImageSubtreeHidden(*Tree, (int)I);
        }
    };

    HideExisting();
    if (!Unit || Unit->MaxHitPoints <= 0 || Unit->HitPoints <= 0 ||
        Unit->HitPoints >= Unit->MaxHitPoints ||
        (Building && Building->UnderConstruction) ||
        EM.GetComponent<EffectComponent>(EntityID) || !sOverlayOffsetResolver)
    {
        return;
    }

    struct DamageSlot
    {
        int Index = 0;
        Core::Math::Vec2 Offset;
    };
    std::vector<DamageSlot> Slots;
    constexpr int kDamageOverlaySlots = 22;
    for (int Index = 0; Index < kDamageOverlaySlots; ++Index)
    {
        const auto Offset = sOverlayOffsetResolver(
            Render.ImageID, 1, (size_t)std::max(0, Render.FrameIndex),
            (size_t)Index, Render.Flipped);
        if (Offset)
            Slots.push_back({Index, *Offset});
    }
    if (Slots.empty())
        return;

    const float MissingFraction = std::clamp(
        (float)(Unit->MaxHitPoints - Unit->HitPoints) /
            ((float)Unit->MaxHitPoints * (2.0f / 3.0f)),
        0.0f, 1.0f);
    const int DamageSteps = std::clamp(
        (int)std::ceil(MissingFraction * (float)(Slots.size() * 2)),
        0, (int)Slots.size() * 2);
    const int LargeCount = DamageSteps / 2;
    const bool HasSmallRemainder = (DamageSteps & 1) != 0;

    constexpr int kSmallDamageImageBase = 450;
    constexpr int kLargeDamageImageBase = 472;
    auto AddDamageNode = [&](const DamageSlot& Slot, int ImageID)
    {
        AddImageTreeNode(EntityID, -1, ImageID, Slot.Offset,
                         true, false, -1.0f, false, true, false);
        Tree = EM.GetComponent<ImageTreeComponent>(EntityID);
        if (!Tree)
            return;
        for (auto& Node : Tree->Nodes)
        {
            if (!Node.Visible || (int)Node.ImageID != ImageID ||
                Node.ParentIndex != -1 ||
                Node.CreationOffset.X != Slot.Offset.X ||
                Node.CreationOffset.Y != Slot.Offset.Y)
            {
                continue;
            }
            Node.IsDamageOverlay = true;
            Node.OverlayOffsetType = 1;
            Node.OverlayOffsetIndex = Slot.Index;
            break;
        }
    };

    for (int I = 0; I < LargeCount; ++I)
        AddDamageNode(Slots[(size_t)I], kLargeDamageImageBase + Slots[(size_t)I].Index);
    if (HasSmallRemainder && LargeCount < (int)Slots.size())
    {
        const auto& Slot = Slots[(size_t)LargeCount];
        AddDamageNode(Slot, kSmallDamageImageBase + Slot.Index);
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
    const auto InitializeInheritedDirection = [Tree](ImageSubnode& Node)
    {
        if (!Node.InheritSpriteDirection || Node.ParentIndex < 0 ||
            Node.ParentIndex >= (int)Tree->Nodes.size())
        {
            return;
        }
        const auto& ParentContext =
            Tree->Nodes[(size_t)Node.ParentIndex].VM.GetContext();
        auto& Context = Node.VM.GetContext();
        Context.CurrentDirection = ParentContext.CurrentDirection;
        Context.FlDirect = ParentContext.FlDirect;
        Node.VM.RefreshDirectionalFrame();
        Node.FrameIndex = std::max(0, Node.VM.GetCurrentFrame());
        Node.Flipped = Context.FlipState;
    };

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
            InitializeInheritedDirection(Node);
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
            InitializeInheritedDirection(Node);
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
    InitializeInheritedDirection(Node);
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

void AnimationSystem::RunImageTreeAnimation(int EntityID, int AnimID)
{
    auto* Tree = mSystemMgr.GetEntityManager().GetComponent<ImageTreeComponent>(EntityID);
    if (!Tree || AnimID < 0)
        return;

    EnsureIScriptLoaded();
    if (!sIScriptDB.IsLoaded())
        return;

    for (auto& Node : Tree->Nodes)
    {
        if (!Node.Visible || Node.UseSpriteID)
            continue;
        const auto* ImageInfo = sIScriptDB.FindImageInfo((int)Node.ImageID);
        const auto* Script = ImageInfo && ImageInfo->IScriptID >= 0
            ? sIScriptDB.FindScript(ImageInfo->IScriptID)
            : nullptr;
        if (!Script || AnimID >= (int)Script->AnimationPC.size() ||
            Script->AnimationPC[(size_t)AnimID] <= 0)
        {
            continue;
        }
        if (Node.AnimID == AnimID && Node.VM.IsRunning())
            continue;

        Node.ScriptID = ImageInfo->IScriptID;
        Node.AnimID = AnimID;
        Node.StepAccumulator = 0.0f;
        Node.VM.LoadScript(AnimID, &sIScriptDB.GetProgram().ProgramData,
                           Script->AnimationPC[(size_t)AnimID]);
        auto& Context = Node.VM.GetContext();
        Context.ImageID = (int)Node.ImageID;
        Context.FrameIndex = Node.FrameIndex;
        Context.HasDirectionalFrames = ImageInfo->HasDirectionalFrames;
        Node.InheritSpriteDirection = ImageInfo->HasDirectionalFrames;
    }
}

void AnimationSystem::RunUnitSubunitAnimation(int EntityID, int AnimID)
{
    auto* Tree = mSystemMgr.GetEntityManager().GetComponent<ImageTreeComponent>(EntityID);
    if (!Tree || AnimID < 0)
        return;

    EnsureIScriptLoaded();
    if (!sIScriptDB.IsLoaded())
        return;

    for (auto& Node : Tree->Nodes)
    {
        if (!Node.Visible || !Node.IsUnitSubunit || Node.UseSpriteID)
            continue;
        const auto* ImageInfo = sIScriptDB.FindImageInfo((int)Node.ImageID);
        const auto* Script = ImageInfo && ImageInfo->IScriptID >= 0
            ? sIScriptDB.FindScript(ImageInfo->IScriptID)
            : nullptr;
        if (!Script || AnimID >= (int)Script->AnimationPC.size() ||
            Script->AnimationPC[(size_t)AnimID] <= 0)
        {
            continue;
        }
        if (Node.AnimID == AnimID && Node.VM.IsRunning())
            continue;

        Node.ScriptID = ImageInfo->IScriptID;
        Node.AnimID = AnimID;
        Node.StepAccumulator = 0.0f;
        Node.VM.LoadScript(AnimID, &sIScriptDB.GetProgram().ProgramData,
                           Script->AnimationPC[(size_t)AnimID]);
        auto& Context = Node.VM.GetContext();
        Context.ImageID = (int)Node.ImageID;
        Context.FrameIndex = Node.FrameIndex;
        Context.HasDirectionalFrames = ImageInfo->HasDirectionalFrames;
        Node.InheritSpriteDirection = ImageInfo->HasDirectionalFrames;
    }
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
                                  Event.D != 0, false, NodeLifetime,
                                  false);
                break;
            case Game::IScriptEventType::CreateImageOverlaySpecialOffset:
                AddImageTreeNode(EntityID, NodeIndex, Event.A, {},
                                  Event.D != 0, false, NodeLifetime,
                                  false, false, true);
                break;
            case Game::IScriptEventType::CreateSpriteOverlay:
                AddImageTreeNode(EntityID, NodeIndex, Event.A,
                                  Core::Math::Vec2{(float)Event.B, (float)Event.C},
                                  Event.D != 0, false, NodeLifetime, true,
                                  false);
                break;
            case Game::IScriptEventType::CreateGasOverlays:
                CreateGasOverlay(EntityID, NodeIndex, SourceImageID, SourceFrame,
                                 SourceFlipped, Event.A, NodeLifetime);
                break;
            case Game::IScriptEventType::OrderSignal:
                if ((Event.A & 1) != 0)
                {
                    if (auto* Building = mSystemMgr.GetEntityManager()
                            .GetComponent<BuildingComponent>(EntityID);
                        Building && Building->UnderConstruction &&
                        Building->ProtossPhase != ProtossConstructionPhase::None &&
                        Building->ProtossPhase != ProtossConstructionPhase::Rift)
                    {
                        Building->ProtossPhaseSignal = true;
                    }
                }
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

bool AnimationSystem::SpawnHarvestWeaponVisual(int EntityID,
                                               const TransformComponent& Trans,
                                               const HarvestComponent* Harvest,
                                               int WeaponID,
                                               bool ParentFlipped)
{
    auto& EM = mSystemMgr.GetEntityManager();
    const auto* Resource = ResolveMiningResource(EM, Harvest);
    if (WeaponID != kScvMiningWeaponID || !sWeaponImageResolver)
    {
        return false;
    }

    int VisualTargetID = -1;
    if (Resource && Resource->Type == ResourceType::Minerals && Harvest)
    {
        VisualTargetID = Harvest->TargetResourceID;
    }
    else
    {
        const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        const auto* Command = EM.GetComponent<CommandComponent>(EntityID);
        const auto* TargetBuilding = Command && Command->TargetEntityID >= 0
            ? EM.GetComponent<BuildingComponent>(Command->TargetEntityID)
            : nullptr;
        const auto* TargetUnit = Command && Command->TargetEntityID >= 0
            ? EM.GetComponent<UnitComponent>(Command->TargetEntityID)
            : nullptr;
        const bool IsConstructionWork =
            Command && Command->CurrentCommand == CommandType::Build &&
            TargetBuilding && TargetBuilding->UnderConstruction &&
            TargetBuilding->HasBuilderUnit(EntityID);
        const bool IsRepairWork =
            Command && Command->CurrentCommand == CommandType::Repair &&
            TargetBuilding && !TargetBuilding->UnderConstruction && TargetUnit &&
            TargetUnit->HitPoints > 0 &&
            TargetUnit->HitPoints < TargetUnit->MaxHitPoints;
        if (!Unit || Unit->ChkUnitID != 7 ||
            (!IsConstructionWork && !IsRepairWork))
        {
            return false;
        }
        VisualTargetID = Command->TargetEntityID;
    }

    const auto ImageID = sWeaponImageResolver(WeaponID);
    const auto* TargetTrans = VisualTargetID >= 0
        ? EM.GetComponent<TransformComponent>(VisualTargetID)
        : nullptr;
    if (!ImageID || !TargetTrans)
        return false;

    Core::Math::Vec2 ContactOffset = TargetTrans->Position - Trans.Position;
    if (ContactOffset.Length() > 0.001f)
        ContactOffset = ContactOffset.Normalized() * 20.0f;
    else
        ContactOffset = {20.0f, 0.0f};
    if (ParentFlipped)
        ContactOffset.X = -ContactOffset.X;

    // OpenBW drives the worker's AlmostBuilt animation toward a point twenty
    // pixels in front of it. The project DAT chain maps Fusion Cutter to image
    // 526 (unit/bullet/scvspark.grp), whose Init script owns the spark frames.
    AddImageTreeNode(EntityID, (int)*ImageID,
                     (int)std::lround(ContactOffset.X),
                     (int)std::lround(ContactOffset.Y),
                     true, false, 0.35f);
    static int DebugMiningSparkLogs = 24;
    if (DebugIScriptRuntimeEnabled() && DebugMiningSparkLogs > 0)
    {
        --DebugMiningSparkLogs;
        Core::Utils::Logger::Info(
            "IScriptRuntime",
            "mining-spark entity=" + std::to_string(EntityID) +
                " target=" + std::to_string(VisualTargetID) +
                " weapon=" + std::to_string(WeaponID) +
                " image=" + std::to_string(*ImageID) +
                " offset=" + std::to_string((int)std::lround(ContactOffset.X)) +
                "," + std::to_string((int)std::lround(ContactOffset.Y)));
    }
    return true;
}

void AnimationSystem::UpdateCommandCenterProductionVisual(
    int EntityID,
    const RenderComponent& Render,
    const ProductionComponent* Production,
    float DeltaTime)
{
    if (Render.ImageID != kCommandCenterImageID)
        return;

    auto* Tree = mSystemMgr.GetEntityManager()
        .GetComponent<ImageTreeComponent>(EntityID);
    if (!Tree)
        return;

    const bool IsProducing = Production &&
        (Production->CurrentTrainingUnitDefID >= 0 || !Production->Queue.empty());
    for (auto& Node : Tree->Nodes)
    {
        if (Node.ImageID != kCommandCenterControlImageID)
            continue;
        if (!IsProducing)
        {
            Node.Opacity = 1.0f;
            Node.RuntimeVisualPhase = 0.0f;
            continue;
        }

        Node.RuntimeVisualPhase += DeltaTime;
        constexpr float kPulseHz = 1.8f;
        const float Pulse = 0.5f + 0.5f * std::sin(
            Node.RuntimeVisualPhase * 6.28318530718f * kPulseHz);
        Node.Opacity = 0.45f + Pulse * 0.55f;
    }
}

void AnimationSystem::ConsumeIScriptEvents(int EntityID,
                                           EntityScriptState& State,
                                           const TransformComponent& Trans,
                                           const PlayerComponent* Player,
                                           CombatComponent* Combat,
                                           const HarvestComponent* Harvest)
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
                    State.AnimID == Game::IScriptAnims::Anim_Init &&
                    sImageAttachmentPersistenceResolver &&
                    sImageAttachmentPersistenceResolver((uint32_t)Event.A);
                AddImageTreeNode(EntityID, Event.A, Event.B, Event.C,
                                 Event.D != 0, false,
                                 TransientOverlay ? 0.35f : -1.0f,
                                 false, PersistentAttachment);
                break;
            }
            case Game::IScriptEventType::CreateImageOverlaySpecialOffset:
            {
                const bool PersistentAttachment =
                    State.AnimID == Game::IScriptAnims::Anim_Init &&
                    sImageAttachmentPersistenceResolver &&
                    sImageAttachmentPersistenceResolver((uint32_t)Event.A);
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
                // Script 193 drives modifier 12 by changing modifier_data1.
                // Keep that value on the root render image; texture.grp is a
                // palette surface, not a standalone overlay image.
                if (auto* Building = mSystemMgr.GetEntityManager()
                        .GetComponent<BuildingComponent>(EntityID);
                    Building && Building->UnderConstruction &&
                    Building->ProtossPhase ==
                        ProtossConstructionPhase::Materializing)
                {
                    if (auto* Render = mSystemMgr.GetEntityManager()
                            .GetComponent<RenderComponent>(EntityID))
                    {
                        Render->UseProtossWarpTexture = true;
                        Render->ProtossWarpTextureFrame = Event.A;
                    }
                }
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
                if (Event.Type == Game::IScriptEventType::UseWeapon &&
                    SpawnHarvestWeaponVisual(EntityID, Trans, Harvest, Event.A,
                                             State.VM.GetContext().FlipState))
                {
                    break;
                }
                if (Combat && Combat->PendingAttackFrame && mBulletSystem)
                    CombatSystem::ResolvePendingAttackFrame(mSystemMgr.GetEntityManager(), *mBulletSystem, EntityID);
                break;
            case Game::IScriptEventType::OrderSignal:
                if ((Event.A & 1) != 0)
                {
                    auto& EM = mSystemMgr.GetEntityManager();
                    if (auto* Siege = EM.GetComponent<SiegeTransitionComponent>(EntityID);
                        Siege && Siege->Active)
                    {
                        Siege->AnimationComplete = true;
                    }
                    if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
                        Building && Building->UnderConstruction &&
                        Building->ProtossPhase != ProtossConstructionPhase::None &&
                        Building->ProtossPhase != ProtossConstructionPhase::Rift)
                    {
                        Building->ProtossPhaseSignal = true;
                    }
                }
                if ((Event.A & 4) != 0)
                {
                    auto& EM = mSystemMgr.GetEntityManager();
                    if (auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID))
                    {
                        auto* Cloak = EM.GetComponent<CloakComponent>(EntityID);
                        if (Burrow->Phase == BurrowPhase::Burrowing)
                        {
                            Burrow->Phase = BurrowPhase::Burrowed;
                            Burrow->IsBurrowed = true;
                            Burrow->BurrowTransitionTime = 0.0f;
                            if (!Cloak)
                                Cloak = &EM.AddComponent<CloakComponent>(EntityID);
                            Cloak->IsBurrowed = true;
                            Cloak->IsCloaked = true;
                        }
                        else if (Burrow->Phase == BurrowPhase::Unburrowing)
                        {
                            Burrow->Phase = BurrowPhase::Surface;
                            Burrow->IsBurrowed = false;
                            Burrow->BurrowTransitionTime = 0.0f;
                            if (Cloak)
                            {
                                Cloak->IsBurrowed = false;
                                if (!Cloak->IsPassiveCloak)
                                    Cloak->IsCloaked = false;
                            }
                        }
                    }
                }
                if ((Event.A & 0x10) != 0)
                {
                    auto& EM = mSystemMgr.GetEntityManager();
                    if (auto* Building = EM.GetComponent<BuildingComponent>(EntityID))
                    {
                        if (!Building->IsAddon &&
                            (Building->FlightPhase == BuildingFlightPhase::LiftingOff ||
                             Building->FlightPhase == BuildingFlightPhase::Landing))
                        {
                            Building->FlightAnimationComplete = true;
                        }
                    }
                }
                break;
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
                                             BuildingComponent* Bldg,
                                             const StatusEffectComponent* Status,
                                             const ProductionComponent* Prod,
                                             const HarvestComponent* Harvest,
                                             const BulletComponent* Bullet)
{
    auto& State = mScriptStates[EntityID];

    const bool IsEffect = mSystemMgr.GetEntityManager().GetComponent<EffectComponent>(EntityID) != nullptr;
    if (IsEffect && !mSystemMgr.GetEntityManager().GetComponent<UnitComponent>(EntityID))
        return false;
    const bool IsResource =
        mSystemMgr.GetEntityManager().GetComponent<ResourceComponent>(EntityID) != nullptr;

    const bool IsBuilding = Bldg != nullptr;
    const bool UnderConstruction = Bldg && Bldg->UnderConstruction;
    const float ConstructionProgress =
        (Bldg && Bldg->TotalBuildTime > 0.0f)
            ? std::clamp(Bldg->BuildProgress / Bldg->TotalBuildTime, 0.0f, 1.0f)
            : 0.0f;
    // OpenBW starts IsWorking for both unit production and research/upgrades.
    // Several Terran research structures animate only a child image, so
    // omitting BuildingComponent::IsResearching makes them look permanently
    // idle even though their timers are advancing.
    const bool IsWorking =
        (Prod && (Prod->CurrentTrainingUnitDefID >= 0 || !Prod->Queue.empty())) ||
        (Bldg && Bldg->IsResearching);
    const auto* MiningResource =
        ResolveMiningResource(mSystemMgr.GetEntityManager(), Harvest);
    const bool IsMiningMinerals =
        MiningResource && MiningResource->Type == ResourceType::Minerals;
    auto& EM = mSystemMgr.GetEntityManager();
    const auto* EntityUnit = EM.GetComponent<UnitComponent>(EntityID);
    const auto* EntityCommand = EM.GetComponent<CommandComponent>(EntityID);
    const auto* ConstructionTarget =
        EntityCommand && EntityCommand->TargetEntityID >= 0
            ? EM.GetComponent<BuildingComponent>(EntityCommand->TargetEntityID)
            : nullptr;
    const auto* ConstructionTargetUnit =
        EntityCommand && EntityCommand->TargetEntityID >= 0
            ? EM.GetComponent<UnitComponent>(EntityCommand->TargetEntityID)
            : nullptr;
    const bool IsConstructing =
        EntityUnit && EntityUnit->ChkUnitID == 7 && EntityCommand &&
        ((EntityCommand->CurrentCommand == CommandType::Build &&
          ConstructionTarget && ConstructionTarget->UnderConstruction &&
          ConstructionTarget->HasBuilderUnit(EntityID)) ||
         (EntityCommand->CurrentCommand == CommandType::Repair &&
          ConstructionTarget && !ConstructionTarget->UnderConstruction &&
          ConstructionTargetUnit && ConstructionTargetUnit->HitPoints > 0 &&
          ConstructionTargetUnit->HitPoints < ConstructionTargetUnit->MaxHitPoints));
    const bool IsFrozen = IsFrozenByStatus(Status);
    const auto* SiegeTransition =
        EM.GetComponent<SiegeTransitionComponent>(EntityID);
    const bool IsPendingAttack =
        Combat && Combat->PendingAttackFrame && !IsFrozen;

    bool AttackTargetsAir = false;
    if (IsPendingAttack && Combat->PendingAttackTargetEntityID >= 0)
    {
        const int TargetID = Combat->PendingAttackTargetEntityID;
        const auto* TargetPath = EM.GetComponent<PathfindingComponent>(TargetID);
        const auto* TargetCombat = EM.GetComponent<CombatComponent>(TargetID);
        AttackTargetsAir = (TargetPath && TargetPath->IsAir) ||
            (TargetCombat && TargetCombat->MyElevation == Elevation::Air);
    }
    const int AttackAnim = AttackTargetsAir
        ? Game::IScriptAnims::Anim_AirAttkInit
        : Game::IScriptAnims::Anim_GndAttkInit;
    // Tank/Goliath-style units own a separate, independently scripted turret.
    // Only that subunit may run the attack entry; running the root entry too
    // creates a second muzzle flash at the chassis origin.
    const bool SubunitOwnsPendingAttack =
        IsPendingAttack && Render.SubunitImageID != UINT32_MAX &&
        HasIScriptAnimationForImage((int)Render.SubunitImageID, AttackAnim);
    const bool RootOwnsPendingAttack =
        IsPendingAttack && !SubunitOwnsPendingAttack;

    EnsureIScriptLoaded();
    if (!sIScriptDB.IsLoaded())
        return false;

    const auto* ImageInfo = sIScriptDB.FindImageInfo((int)Render.ImageID);
    if (!ImageInfo || ImageInfo->IScriptID < 0)
        return false;

    const bool UseProtossMaterializationScript =
        Bldg && Bldg->UnderConstruction &&
        Bldg->ProtossPhase == ProtossConstructionPhase::Materializing;
    const int ActiveScriptID = UseProtossMaterializationScript
        ? 193
        : ImageInfo->IScriptID;
    const auto* Script = sIScriptDB.FindScript(ActiveScriptID);
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
        (!IsResource || (Bldg && Bldg->BuildingDefID >= 0)) &&
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
        RootOwnsPendingAttack,
        HasAnim);
    const bool ImageOrScriptChanged =
        State.ImageID != (int)Render.ImageID ||
        State.ScriptID != ActiveScriptID;
    // A newly selected building image must execute Init once before another
    // state. Init creates the shadow, refinery overlay, and other permanent
    // image layers that Built/AlmostBuilt/IsWorking assume already exist.
    if (NeedsInitialBuilt)
    {
        const int BuiltAnim =
            ResolvePlayableIScriptAnimation(Game::IScriptAnims::Anim_Built, HasAnim);
        if (BuiltAnim >= 0)
            TargetAnim = BuiltAnim;
    }
    if (ImageOrScriptChanged &&
        HasAnim(Game::IScriptAnims::Anim_Init))
    {
        TargetAnim = Game::IScriptAnims::Anim_Init;
    }
    // LiftOff leaves the landing gear in its retracted terminal frame. Do not
    // immediately broadcast Init to the building image tree when it becomes
    // airborne; Init is the grounded pose and visibly lowers the gear again.
    if (!ImageOrScriptChanged && IsBuilding &&
        DesiredState == AnimState::Flying &&
        State.AnimID == Game::IScriptAnims::Anim_LiftOff)
    {
        if (auto* Tree = EM.GetComponent<ImageTreeComponent>(EntityID))
        {
            for (auto& Node : Tree->Nodes)
            {
                if (Node.Visible &&
                    Node.AnimID == Game::IScriptAnims::Anim_LiftOff)
                {
                    Node.VM.PauseAtCurrentFrame();
                    Node.StepAccumulator = 0.0f;
                }
            }
        }
        return true;
    }
    if ((IsMiningMinerals || IsConstructing) &&
        HasAnim(Game::IScriptAnims::Anim_AlmostBuilt))
    {
        TargetAnim = Game::IScriptAnims::Anim_AlmostBuilt;
    }
    else if ((State.WasMiningMinerals || State.WasConstructing) &&
             HasAnim(Game::IScriptAnims::Anim_GndAttkToIdle))
    {
        TargetAnim = Game::IScriptAnims::Anim_GndAttkToIdle;
    }
    if (Bullet)
    {
        if (Bullet->Behavior == WeaponBehavior::Persist &&
            HasAnim(Game::IScriptAnims::Anim_SpecialState2))
        {
            TargetAnim = Game::IScriptAnims::Anim_SpecialState2;
        }
        else if (HasAnim(Game::IScriptAnims::Anim_GndAttkInit))
        {
            TargetAnim = Game::IScriptAnims::Anim_GndAttkInit;
        }
    }
    if (Bldg && Bldg->UnderConstruction)
    {
        switch (Bldg->ProtossPhase)
        {
            case ProtossConstructionPhase::Rift:
                if (HasAnim(Game::IScriptAnims::Anim_Init))
                    TargetAnim = Game::IScriptAnims::Anim_Init;
                break;
            case ProtossConstructionPhase::RiftClosing:
                if (HasAnim(Game::IScriptAnims::Anim_SpecialState1))
                    TargetAnim = Game::IScriptAnims::Anim_SpecialState1;
                else if (HasAnim(Game::IScriptAnims::Anim_Init))
                    TargetAnim = Game::IScriptAnims::Anim_Init;
                break;
            case ProtossConstructionPhase::Materializing:
                if (HasAnim(Game::IScriptAnims::Anim_Init))
                    TargetAnim = Game::IScriptAnims::Anim_Init;
                break;
            case ProtossConstructionPhase::WarpIn:
                if (HasAnim(Game::IScriptAnims::Anim_WarpIn))
                    TargetAnim = Game::IScriptAnims::Anim_WarpIn;
                break;
            case ProtossConstructionPhase::None:
                break;
        }
    }
    if (SiegeTransition && SiegeTransition->Active &&
        !SiegeTransition->TargetSieged && SiegeTransition->Stage == 0 &&
        HasAnim(Game::IScriptAnims::Anim_SpecialState2))
    {
        TargetAnim = Game::IScriptAnims::Anim_SpecialState2;
    }
    if (TargetAnim < 0)
        return false;

    const bool IsTransientAnim =
        RootOwnsPendingAttack ||
        (SiegeTransition && SiegeTransition->Active) ||
        UnderConstruction ||
        IsWorking ||
        IsMiningMinerals ||
        IsConstructing ||
        DesiredState == AnimState::Burrowing ||
        DesiredState == AnimState::Unburrowing ||
        DesiredState == AnimState::LiftingOff ||
        DesiredState == AnimState::Landing ||
        State.WasWorking ||
        State.WasMiningMinerals ||
        State.WasConstructing ||
        Bullet != nullptr ||
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
        State.ScriptID = ActiveScriptID;
        State.AnimID = TargetAnim;
        State.StepAccumulator = 0.0f;
        if (TargetAnim == Game::IScriptAnims::Anim_Built)
            State.HasPlayedBuilt = true;
        if (ShouldClearImageTreeForAnimRestart(
                IsBuilding, ImageOrScriptChanged, PreviousAnim, TargetAnim,
                UnderConstruction))
        {
            ClearImageTree(EntityID, !ImageOrScriptChanged);
        }
        else if (IsBuilding)
        {
            // StarCraft runs a requested sprite animation on every image in
            // the sprite, not just on its root. This preserves completed
            // building machinery and lets turret/add-on child images enter
            // Working, WorkingToIdle, LiftOff, Landing, and attack states.
            RunImageTreeAnimation(EntityID, TargetAnim);
        }
    }

    // The construction-rift root does not necessarily expose SpecialState1.
    // StarCraft's sprite_run_anim still sends that animation to every child
    // image, and the authored rift child emits sigorder 1 when it closes.
    if (Bldg && Bldg->UnderConstruction &&
        Bldg->ProtossPhase == ProtossConstructionPhase::RiftClosing)
    {
        RunImageTreeAnimation(EntityID, Game::IScriptAnims::Anim_SpecialState1);
    }

    auto& Ctx = State.VM.GetContext();
    Ctx.LiftOffState = Bldg && Bldg->IsLifted;
    if (!IsBuilding)
    {
        Core::Math::Vec2 AnimDirection;
        if ((IsMiningMinerals && Harvest && Harvest->TargetResourceID >= 0) ||
            (IsConstructing && EntityCommand &&
             EntityCommand->TargetEntityID >= 0))
        {
            const int WorkTargetID = IsMiningMinerals
                ? Harvest->TargetResourceID
                : EntityCommand->TargetEntityID;
            const auto* TargetTrans = mSystemMgr.GetEntityManager()
                .GetComponent<TransformComponent>(WorkTargetID);
            AnimDirection = TargetTrans
                ? QuantizeDirectionToEightWay(TargetTrans->Position - Trans.Position)
                : ResolveAnimationDirection(mSystemMgr.GetEntityManager(), DesiredState,
                                            Trans, Path, IsFrozen ? nullptr : Combat);
        }
        else
        {
            AnimDirection = ResolveAnimationDirection(
                mSystemMgr.GetEntityManager(), DesiredState, Trans, Path,
                IsFrozen ? nullptr : Combat);
        }
        Ctx.CurrentDirection = DirectionToIScriptDirection(AnimDirection);
        Ctx.FlDirect = Ctx.CurrentDirection;
        if (Ctx.HasDirectionalFrames)
            State.VM.RefreshDirectionalFrame();
    }

    if (RootOwnsPendingAttack)
    {
        Ctx.IsAttacking = true;
        ApplyAttackDirection(mSystemMgr.GetEntityManager(), Trans, Combat, Ctx);
        State.VM.RefreshDirectionalFrame();
    }
    else
    {
        Ctx.IsAttacking = false;
    }
    if (IsPendingAttack)
    {
        // Turrets are authored as child images. Drive their attack entry even
        // when the root deliberately stays in its idle/walking animation.
        if (SubunitOwnsPendingAttack)
            RunUnitSubunitAnimation(EntityID, AttackAnim);
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

    ConsumeIScriptEvents(EntityID, State, Trans, Player, Combat, Harvest);

    // Some Protoss WarpIn roots end while a child effect emits sigorder 1;
    // other building scripts have no signaling child. Treat a terminal root
    // animation as the safe equivalent so construction cannot remain stuck
    // after every authored visual has finished.
    if (Bldg && Bldg->UnderConstruction && !State.VM.IsRunning() &&
        Bldg->ProtossPhase != ProtossConstructionPhase::None &&
        Bldg->ProtossPhase != ProtossConstructionPhase::Rift)
    {
        Bldg->ProtossPhaseSignal = true;
    }

    if (Bldg && Bldg->IsAddon && !State.VM.IsRunning() &&
        (Bldg->FlightPhase == BuildingFlightPhase::LiftingOff ||
         Bldg->FlightPhase == BuildingFlightPhase::Landing))
    {
        Bldg->FlightPhase = BuildingFlightPhase::Grounded;
        Bldg->FlightAnimationComplete = false;
    }

    const int ScriptFrame = std::max(0, State.VM.GetCurrentFrame());
    const auto& RootContext = State.VM.GetContext();
    if (RootContext.HorPosSet)
        Render.VisualOffset.X = (float)RootContext.HorPos;
    if (RootContext.VertPosSet)
        Render.VisualOffset.Y = (float)RootContext.VertPos;
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
             State.AnimID == Game::IScriptAnims::Anim_GndAttkToIdle &&
             (State.WasMiningMinerals || State.WasConstructing))
    {
        State.WasMiningMinerals = false;
        State.WasConstructing = false;
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
        State.WasWorking = IsWorking ||
            (State.AnimID == Game::IScriptAnims::Anim_WorkingToIdle &&
             State.VM.IsRunning());
        if (IsMiningMinerals)
            State.WasMiningMinerals = true;
        else if (State.AnimID != Game::IScriptAnims::Anim_GndAttkToIdle)
            State.WasMiningMinerals = false;
        if (IsConstructing)
            State.WasConstructing = true;
        else if (State.AnimID != Game::IScriptAnims::Anim_GndAttkToIdle)
            State.WasConstructing = false;
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
        auto* Burrow = EM.GetComponent<BurrowComponent>(Eid);
        auto* Status = EM.GetComponent<StatusEffectComponent>(Eid);
        auto* Prod = EM.GetComponent<ProductionComponent>(Eid);
        auto* Harvest = EM.GetComponent<HarvestComponent>(Eid);
        auto* Player = EM.GetComponent<PlayerComponent>(Eid);
        auto* Bullet = EM.GetComponent<BulletComponent>(Eid);

        AnimState State = GetAnimStateFromComponents(
            Cmd, Combat, Path, Unit, Bldg, Morph, Burrow, Status);

        const bool UsedIScript = UpdateIScriptAnimation(
            Eid, DeltaTime, State, *Render, *Trans, Path, Player,
            Combat, Bldg, Status, Prod, Harvest, Bullet);
        EnsureUnitSubunitNode(Eid, *Render);
        if (auto* Siege = EM.GetComponent<SiegeTransitionComponent>(Eid);
            Siege && Siege->Active && Siege->Stage == 0)
        {
            RunUnitSubunitAnimation(
                Eid, Siege->TargetSieged
                    ? Game::IScriptAnims::Anim_SpecialState1
                    : Game::IScriptAnims::Anim_SpecialState2);
        }
        if (Combat && Combat->PendingAttackFrame &&
            Render->SubunitImageID != UINT32_MAX)
        {
            bool TargetAir = false;
            if (Combat->PendingAttackTargetEntityID >= 0)
            {
                const int TargetID = Combat->PendingAttackTargetEntityID;
                const auto* TargetPath = EM.GetComponent<PathfindingComponent>(TargetID);
                const auto* TargetCombat = EM.GetComponent<CombatComponent>(TargetID);
                TargetAir = (TargetPath && TargetPath->IsAir) ||
                    (TargetCombat && TargetCombat->MyElevation == Elevation::Air);
            }
            RunUnitSubunitAnimation(
                Eid, TargetAir ? Game::IScriptAnims::Anim_AirAttkInit
                               : Game::IScriptAnims::Anim_GndAttkInit);
        }
        UpdateCommandCenterProductionVisual(Eid, *Render, Prod, DeltaTime);
        UpdateAddonConnectorOverlay(Eid, *Render);
        UpdateDamageOverlays(Eid, *Render, Unit, Bldg);
        if (UsedIScript)
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

    // Siege/unsiege uses the authored tank scripts instead of instantly
    // replacing the unit image. Tank-mode turret SpecialState1 aligns first;
    // the sieged hull Init lowers the body and signals completion. Unsiege
    // keeps the sieged images until hull SpecialState2 raises them and signals.
    for (int Eid : EM.GetEntitiesWith<SiegeTransitionComponent, UnitComponent,
                                     RenderComponent>())
    {
        auto* Siege = EM.GetComponent<SiegeTransitionComponent>(Eid);
        auto* Unit = EM.GetComponent<UnitComponent>(Eid);
        auto* Render = EM.GetComponent<RenderComponent>(Eid);
        if (!Siege || !Siege->Active || !Unit || !Render)
            continue;

        Siege->Elapsed += DeltaTime;
        if (Siege->TargetSieged && Siege->Stage == 0 && Siege->Elapsed >= 0.15f)
        {
            Render->ImageID = Siege->TargetImageID;
            Render->SubunitImageID = Siege->TargetSubunitImageID;
            Render->FrameIndex = 0;
            Siege->Stage = 1;
            Siege->Elapsed = 0.0f;
            Siege->AnimationComplete = false;
            continue;
        }

        const float Timeout = Siege->TargetSieged ? 4.0f : 4.0f;
        const bool CanFinish =
            (!Siege->TargetSieged && Siege->Stage == 0) ||
            (Siege->TargetSieged && Siege->Stage == 1);
        if (!CanFinish || (!Siege->AnimationComplete && Siege->Elapsed < Timeout))
            continue;

        if (!Siege->TargetSieged)
        {
            Render->ImageID = Siege->TargetImageID;
            Render->SubunitImageID = Siege->TargetSubunitImageID;
            Render->FrameIndex = 0;
        }
        Unit->ChkUnitID = Siege->TargetChkUnitID;
        Unit->Speed = Siege->TargetSpeed;
        if (auto* Combat = EM.GetComponent<CombatComponent>(Eid))
            *Combat = Siege->TargetCombat;
        Siege->Active = false;
        Siege->AnimationComplete = false;
        Core::Utils::Logger::Info(
            "AnimationSystem",
            "Tank " + std::to_string(Eid) +
                (Siege->TargetSieged
                    ? " completed authored siege animation"
                    : " completed authored unsiege animation"));
    }

    // The original mineral GRPs contain four authored depletion frames. Keep
    // the displayed crystal size tied to the actual remaining resource after
    // iscript has advanced, and remove the whole sprite at zero resources.
    for (int Eid : EM.GetEntitiesWith<ResourceComponent, RenderComponent>())
    {
        auto* Resource = EM.GetComponent<ResourceComponent>(Eid);
        auto* Render = EM.GetComponent<RenderComponent>(Eid);
        if (!Resource || !Render || Resource->Type != ResourceType::Minerals)
            continue;

        if (Resource->Amount <= 0 || Resource->IsDepleted)
        {
            Render->Visible = false;
            if (auto* Tree = EM.GetComponent<ImageTreeComponent>(Eid))
            {
                for (auto& Node : Tree->Nodes)
                    Node.Visible = false;
            }
            continue;
        }

        Render->Visible = true;
        const int MaxAmount = std::max(1, Resource->MaxAmount);
        const float Remaining = std::clamp(
            (float)Resource->Amount / (float)MaxAmount, 0.0f, 1.0f);
        const int DepletionFrame = Remaining > 0.75f ? 0 :
                                   Remaining > 0.50f ? 1 :
                                   Remaining > 0.25f ? 2 : 3;
        Render->TotalFrames = std::max(Render->TotalFrames, 4);
        Render->FrameIndex = DepletionFrame;

        // Mineral shadows use matching four-frame GRPs. Keep them on the same
        // depletion stage as the crystal body.
        if (auto* Tree = EM.GetComponent<ImageTreeComponent>(Eid))
        {
            for (auto& Node : Tree->Nodes)
            {
                if (Node.TotalFrames >= 4)
                    Node.FrameIndex = DepletionFrame;
            }
        }
    }
}

} // namespace Game::Systems
