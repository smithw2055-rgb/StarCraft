#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/GeyserState.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/NavigationSpatialHash.h"
#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Game::Systems
{

using namespace Game::Components;

static bool CanAttackTarget(const CombatComponent& Combat,
                            Core::ECS::EntityManager& EM,
                            int TargetID);

namespace
{
    CombatSystem::DetectorChecker gDetectorChecker;
    CombatSystem::AllianceChecker gAllianceChecker;
    static const Game::Pathfinding::NavigationSpatialHash* sActiveCombatSpatialHash = nullptr;
    constexpr float kAutoTargetLockSeconds = 2.5f;
    constexpr float kInvalidDirectAttackAutoAcquireDelay = 0.35f;
    constexpr int kSubPointsPerPoint = 256;

    int ExactVitalRaw(int VisiblePoints, int SubDamage)
    {
        return std::max(0,
            VisiblePoints * kSubPointsPerPoint -
            std::clamp(SubDamage, 0, kSubPointsPerPoint - 1));
    }

    void StoreVitalRaw(int Raw, int& VisiblePoints, int& SubDamage)
    {
        if (Raw <= 0)
        {
            VisiblePoints = 0;
            SubDamage = 0;
            return;
        }
        VisiblePoints = (Raw + kSubPointsPerPoint - 1) / kSubPointsPerPoint;
        SubDamage = VisiblePoints * kSubPointsPerPoint - Raw;
    }

    uint8_t ShieldImpactDirectionIndex(Core::ECS::EntityManager& EM,
                                       int TargetID,
                                       int AttackerID)
    {
        Core::Math::Vec2 Direction{0.0f, -1.0f};
        const auto* Target = EM.GetComponent<TransformComponent>(TargetID);
        const auto* Attacker = EM.GetComponent<TransformComponent>(AttackerID);
        if (Target && Attacker)
        {
            const auto Delta = Target->Position - Attacker->Position;
            if (Delta.Length() > 0.001f)
                Direction = Delta.Normalized();
        }
        const float Angle = std::atan2(Direction.X, -Direction.Y);
        float Normalized = Angle;
        if (Normalized < 0.0f)
            Normalized += 6.28318530718f;
        const int Heading =
            (int)std::lround(Normalized * 256.0f / 6.28318530718f) & 0xff;
        return (uint8_t)((((Heading - 124) & 0xff) / 8) & 31);
    }

    void QueueShieldImpact(Core::ECS::EntityManager& EM,
                           int TargetID,
                           int AttackerID)
    {
        auto* Pending = EM.GetComponent<ShieldImpactComponent>(TargetID);
        if (!Pending)
            Pending = &EM.AddComponent<ShieldImpactComponent>(TargetID);
        if (Pending->Impacts.size() < 8)
        {
            ShieldImpactRequest Request;
            Request.DirectionIndex =
                ShieldImpactDirectionIndex(EM, TargetID, AttackerID);
            if (const auto* Render = EM.GetComponent<RenderComponent>(TargetID))
            {
                Request.SourceImageID = Render->ImageID;
                Request.SourceFrame = Render->FrameIndex;
                Request.SourceFlipped = Render->Flipped;
            }
            Pending->Impacts.push_back(Request);
        }
    }

    bool IsProtectedResourceEntity(Core::ECS::EntityManager& EM, int EntityID)
    {
        return IsProtectedResourceNode(EM, EntityID);
    }

    bool RestoreClaimedGeyser(Core::ECS::EntityManager& EM, int EntityID)
    {
        auto* Claimed = EM.GetComponent<ClaimedGeyserComponent>(EntityID);
        auto* Resource = EM.GetComponent<ResourceComponent>(EntityID);
        auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
        auto* Render = EM.GetComponent<RenderComponent>(EntityID);
        auto* Building = EM.GetComponent<BuildingComponent>(EntityID);
        if (!Claimed || !Claimed->IsClaimed || !Resource || !Unit || !Player ||
            !Render || !Building)
        {
            return false;
        }

        *Unit = Claimed->OriginalUnit;
        *Player = Claimed->OriginalPlayer;
        *Render = Claimed->OriginalRender;
        *Building = Claimed->OriginalBuilding;
        if (Claimed->HadPathfinding)
        {
            auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
            if (!Path)
                Path = &EM.AddComponent<PathfindingComponent>(EntityID);
            *Path = Claimed->OriginalPathfinding;
        }
        if (auto* Tree = EM.GetComponent<ImageTreeComponent>(EntityID))
            *Tree = ImageTreeComponent{};
        if (auto* Command = EM.GetComponent<CommandComponent>(EntityID))
        {
            Command->CancelAll();
            Command->TargetEntityID = -1;
        }
        Claimed->IsClaimed = false;

        // Workers inside or queued at the destroyed gas building must be
        // released visibly; otherwise they remain hidden in the restored raw
        // geyser and can never receive another economy order.
        for (int WorkerID : EM.GetEntitiesWith<HarvestComponent>())
        {
            auto* Harvest = EM.GetComponent<HarvestComponent>(WorkerID);
            if (!Harvest ||
                (Harvest->TargetResourceID != EntityID &&
                 Harvest->DesiredResourceID != EntityID))
            {
                continue;
            }
            if (Harvest->InsideGasBuilding)
            {
                if (auto* WorkerTransform = EM.GetComponent<TransformComponent>(WorkerID))
                    WorkerTransform->Position = Harvest->GasExitPosition;
                if (auto* WorkerRender = EM.GetComponent<RenderComponent>(WorkerID))
                    WorkerRender->Visible = true;
            }
            Harvest->State = HarvestState::Idle;
            Harvest->TargetResourceID = -1;
            Harvest->DesiredResourceID = -1;
            Harvest->QueuedAtResource = false;
            Harvest->InsideGasBuilding = false;
            Harvest->MineTimer = 0.0f;
            if (auto* WorkerCommand = EM.GetComponent<CommandComponent>(WorkerID))
            {
                WorkerCommand->CancelAll();
                WorkerCommand->TargetEntityID = -1;
            }
            if (auto* WorkerPath = EM.GetComponent<PathfindingComponent>(WorkerID))
            {
                WorkerPath->Path.clear();
                WorkerPath->PathIndex = 0;
                WorkerPath->CurrentSpeed = 0.0f;
                WorkerPath->TargetSpeed = 0.0f;
                WorkerPath->CurrentVelocity = {};
            }
        }
        Resource->ActiveGathererEntityID = -1;
        Resource->QueuedGatherers.clear();
        return true;
    }

    float CollisionRadiusFromPath(const PathfindingComponent* Path, float Fallback)
    {
        if (!Path)
            return Fallback;
        if (!Path->HasCollisionBounds)
            return Path->CollisionRadius > 0 ? (float)Path->CollisionRadius : Fallback;

        const int Width = Path->CollisionLeft + Path->CollisionRight + 1;
        const int Height = Path->CollisionUp + Path->CollisionDown + 1;
        return (float)std::max(4, std::max(Width, Height) / 2);
    }

    float CollisionRadiusFromUnit(const UnitComponent* Unit, float Fallback)
    {
        if (!Unit)
            return Fallback;
        switch (Unit->SizeType)
        {
            case UnitSize::Small: return 12.0f;
            case UnitSize::Medium: return 16.0f;
            case UnitSize::Large: return 20.0f;
        }
        return Fallback;
    }

    float CombatCollisionRadius(Core::ECS::EntityManager& EM, int EntityID, float Fallback = 16.0f)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
            return CollisionRadiusFromPath(Path, Fallback);
        return CollisionRadiusFromUnit(EM.GetComponent<UnitComponent>(EntityID), Fallback);
    }

    float CombatEdgeDistance(Core::ECS::EntityManager& EM, int A, int B)
    {
        auto* TA = EM.GetComponent<TransformComponent>(A);
        auto* TB = EM.GetComponent<TransformComponent>(B);
        if (!TA || !TB)
            return std::numeric_limits<float>::max();

        struct WorldBounds
        {
            float Left = 0.0f;
            float Top = 0.0f;
            float Right = 0.0f;
            float Bottom = 0.0f;
        };
        const auto BoundsFor = [&](int EntityID,
                                   const TransformComponent& Transform)
        {
            if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
                Path && Path->HasCollisionBounds)
            {
                return WorldBounds{
                    Transform.Position.X - (float)Path->CollisionLeft,
                    Transform.Position.Y - (float)Path->CollisionUp,
                    Transform.Position.X + (float)Path->CollisionRight + 1.0f,
                    Transform.Position.Y + (float)Path->CollisionDown + 1.0f};
            }
            const float Radius = CombatCollisionRadius(EM, EntityID);
            return WorldBounds{
                Transform.Position.X - Radius,
                Transform.Position.Y - Radius,
                Transform.Position.X + Radius,
                Transform.Position.Y + Radius};
        };

        const WorldBounds BA = BoundsFor(A, *TA);
        const WorldBounds BB = BoundsFor(B, *TB);
        float GapX = 0.0f;
        float GapY = 0.0f;
        if (BA.Left > BB.Right)
            GapX = BA.Left - BB.Right;
        else if (BB.Left > BA.Right)
            GapX = BB.Left - BA.Right;
        if (BA.Top > BB.Bottom)
            GapY = BA.Top - BB.Bottom;
        else if (BB.Top > BA.Bottom)
            GapY = BB.Top - BA.Bottom;

        return std::sqrt(GapX * GapX + GapY * GapY);
    }

    bool IsClassicOrganicUnitType(int ChkUnitID)
    {
        return Game::Races::IsOrganicChkUnitID(ChkUnitID);
    }

    bool CanTakeIrradiateDamage(const UnitComponent& Unit)
    {
        // These three unit types carry the Organic units.dat flag but are
        // explicitly exempt from Irradiate's periodic area damage.
        return IsClassicOrganicUnitType(Unit.ChkUnitID) &&
               Unit.ChkUnitID != 35 && // Larva
               Unit.ChkUnitID != 36 && // Egg
               Unit.ChkUnitID != 97;   // Lurker Egg
    }

    bool IsEntityBurrowed(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID))
        {
            if (Burrow->IsBurrowed || Burrow->Phase == BurrowPhase::Burrowed)
                return true;
        }
        if (auto* Cloak = EM.GetComponent<CloakComponent>(EntityID))
            return Cloak->IsBurrowed;
        return false;
    }

    int ContainingTransport(Core::ECS::EntityManager& EM, int PassengerID)
    {
        if (const auto* Unit = EM.GetComponent<UnitComponent>(PassengerID))
        {
            const int TransportID = Unit->LoadedInTransportID;
            if (TransportID >= 0 && EM.IsAlive(TransportID) &&
                EM.GetComponent<LoadedUnitsComponent>(TransportID))
            {
                return TransportID;
            }
        }
        return -1;
    }

    bool IsWorkerUnit(const UnitComponent* Unit)
    {
        if (!Unit)
            return false;
        return Unit->ChkUnitID == 0x07 || // SCV
               Unit->ChkUnitID == 0x29 || // Drone
               Unit->ChkUnitID == 0x40;   // Probe
    }

    bool IsPersistentNonCombatCommand(CommandType Command)
    {
        return Command == CommandType::Harvest ||
               Command == CommandType::Build ||
               Command == CommandType::Train ||
               Command == CommandType::Upgrade ||
               Command == CommandType::UseAbility;
    }

    bool CanChaseTargetTemporarily(const CommandComponent* Cmd,
                                   const BuildingComponent* Building)
    {
        if (Building && !Building->IsLifted)
            return false;
        if (!Cmd)
            return true;
        return !IsPersistentNonCombatCommand(Cmd->CurrentCommand) &&
               Cmd->CurrentCommand != CommandType::HoldPosition;
    }

    bool IsTargetDetectedForPlayer(Core::ECS::EntityManager& EM,
                                  int TargetID,
                                  int ObserverPlayerID)
    {
        const auto* Cloak = EM.GetComponent<CloakComponent>(TargetID);
        if (!Cloak || (!Cloak->IsCloaked && !Cloak->IsBurrowed))
            return true;

        const auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
        if (!TargetTrans)
            return false;

        if (gDetectorChecker && ObserverPlayerID >= 0)
        {
            const int TX = (int)(TargetTrans->Position.X / 32.0f);
            const int TY = (int)(TargetTrans->Position.Y / 32.0f);
            return gDetectorChecker(TX, TY, ObserverPlayerID);
        }

        for (int EntityID : EM.GetEntitiesWith<UnitComponent, TransformComponent, PlayerComponent>())
        {
            const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
            const auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
            const auto* Player = EM.GetComponent<PlayerComponent>(EntityID);
            if (!Unit || !Trans || !Player || Unit->HitPoints <= 0)
                continue;
            if (Player->PlayerID != ObserverPlayerID)
                continue;
            if (!Unit->IsDetector)
                continue;

            if (const auto* Bldg = EM.GetComponent<BuildingComponent>(EntityID))
            {
                if (Bldg->UnderConstruction || (Bldg->RequiresPylon && !Bldg->HasPylonPower))
                    continue;
            }

            const float DetectRangePixels = (float)Unit->SightRange * 32.0f;
            if ((Trans->Position - TargetTrans->Position).LengthSq() <= DetectRangePixels * DetectRangePixels)
                return true;
        }

        return false;
    }

    bool IsActiveTargetValid(Core::ECS::EntityManager& EM,
                             const CombatComponent& Combat,
                             int TargetID,
                             int AttackerID = -1)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
        if (!TargetUnit || TargetUnit->HitPoints <= 0 || !TargetTrans ||
            !CanAttackTarget(Combat, EM, TargetID))
        {
            return false;
        }

        auto* Cloak = EM.GetComponent<CloakComponent>(TargetID);
        if (Cloak && (Cloak->IsCloaked || Cloak->IsBurrowed) && AttackerID >= 0)
        {
            auto* AttackerUnit = EM.GetComponent<UnitComponent>(AttackerID);
            if (!AttackerUnit || !AttackerUnit->IsDetector)
            {
                auto* AttackerPlayer = EM.GetComponent<PlayerComponent>(AttackerID);
                const int PlayerID = AttackerPlayer ? AttackerPlayer->PlayerID : 0;
                if (!IsTargetDetectedForPlayer(EM, TargetID, PlayerID))
                    return false;
            }
        }

        return true;
    }

    void SetAutoTarget(CombatComponent& Combat, int TargetID, float LockSeconds)
    {
        Combat.AutoTargetEntityID = TargetID;
        Combat.AutoTargetLockSeconds = std::max(Combat.AutoTargetLockSeconds, LockSeconds);
    }

    bool AreEnemyPlayers(Core::ECS::EntityManager& EM, int A, int B)
    {
        auto* PA = EM.GetComponent<PlayerComponent>(A);
        auto* PB = EM.GetComponent<PlayerComponent>(B);
        if (!PA || !PB) return false;
        if (PA->PlayerID == PB->PlayerID || PA->IsRescuePassive || PB->IsRescuePassive)
            return false;
        if (gAllianceChecker && gAllianceChecker(PA->PlayerID, PB->PlayerID))
            return false;
        return true;
    }
}

static bool IsAirTarget(Core::ECS::EntityManager& EM, int EntityID);

static bool EntityHasIScriptAttack(Core::ECS::EntityManager& EM,
                                   int EntityID,
                                   int TargetID)
{
    const bool TargetAir = IsAirTarget(EM, TargetID);
    const int AttackAnim = TargetAir
        ? Game::IScriptAnims::Anim_AirAttkInit
        : Game::IScriptAnims::Anim_GndAttkInit;
    const auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
    const bool UsesSpecialSelfDestruct =
        !TargetAir && Combat && Combat->SelfDestructOnAttack;
    const auto HasAttackAnimation = [&](int ImageID)
    {
        return AnimationSystem::HasIScriptAnimationForImage(
                   ImageID, AttackAnim) ||
               (UsesSpecialSelfDestruct &&
                AnimationSystem::HasIScriptAnimationForImage(
                    ImageID, Game::IScriptAnims::Anim_SpecialState1));
    };
    if (const auto* Render = EM.GetComponent<RenderComponent>(EntityID);
        Render && HasAttackAnimation((int)Render->ImageID))
    {
        return true;
    }
    if (const auto* Render = EM.GetComponent<RenderComponent>(EntityID);
        Render && Render->SubunitImageID != UINT32_MAX &&
        HasAttackAnimation((int)Render->SubunitImageID))
    {
        return true;
    }
    if (const auto* Tree = EM.GetComponent<ImageTreeComponent>(EntityID))
    {
        for (const auto& Node : Tree->Nodes)
        {
            if (Node.Visible && !Node.UseSpriteID &&
                HasAttackAnimation((int)Node.ImageID))
            {
                return true;
            }
        }
    }
    return false;
}

static int ApplySizeDamageModifierRaw(int DamageRaw,
                                      DamageType DmgType,
                                      UnitSize TargetSize)
{
    switch (DmgType)
    {
        case DamageType::Concussive:
            switch (TargetSize)
            {
                case UnitSize::Small:  return DamageRaw;
                case UnitSize::Medium: return DamageRaw / 2;
                case UnitSize::Large:  return DamageRaw / 4;
            }
            break;
        case DamageType::Explosive:
            switch (TargetSize)
            {
                case UnitSize::Small:  return DamageRaw / 2;
                case UnitSize::Medium: return DamageRaw * 3 / 4;
                case UnitSize::Large:  return DamageRaw;
            }
            break;
        case DamageType::Normal:
        default:
            return DamageRaw;
    }
    return DamageRaw;
}

static void IssueMovePreservingCommand(Core::ECS::EntityManager& EM,
                                       int EntityID,
                                       const Core::Math::Vec2& MoveTarget,
                                       CommandType Command,
                                       const Core::Math::Vec2& CommandTarget,
                                       int TargetEntityID,
                                       int IgnoreBlockerEntityID = -1)
{
    std::vector<Order> SavedOrders;
    int SavedCurrentOrderIndex = -1;
    Core::Math::Vec2 SavedRallyPoint;
    bool SavedHasRallyPoint = false;
    if (auto* ExistingCmd = EM.GetComponent<CommandComponent>(EntityID))
    {
        SavedOrders = ExistingCmd->Orders;
        SavedCurrentOrderIndex = ExistingCmd->CurrentOrderIndex;
        SavedRallyPoint = ExistingCmd->RallyPoint;
        SavedHasRallyPoint = ExistingCmd->HasRallyPoint;
    }

    CommandSystem::IssueMove(EM, {EntityID}, MoveTarget, IgnoreBlockerEntityID);

    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    if (!Cmd)
        Cmd = &EM.AddComponent<CommandComponent>(EntityID);
    Cmd->Orders = std::move(SavedOrders);
    Cmd->CurrentOrderIndex = SavedCurrentOrderIndex;
    Cmd->RallyPoint = SavedRallyPoint;
    Cmd->HasRallyPoint = SavedHasRallyPoint;
    Cmd->CurrentCommand = Command;
    Cmd->TargetPosition = CommandTarget;
    Cmd->TargetEntityID = TargetEntityID;
}

static bool IsAirTarget(Core::ECS::EntityManager& EM, int EntityID)
{
    if (!EM.IsAlive(EntityID))
        return false;

    if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
    {
        if (Path->IsAir)
            return true;
    }

    if (auto* Combat = EM.GetComponent<CombatComponent>(EntityID))
        return Combat->MyElevation == Elevation::Air;

    return false;
}

static bool IsRangedAttack(const CombatComponent& Combat)
{
    return Combat.IsRanged || Combat.WeaponBeh != WeaponBehavior::Melee;
}

static int EngagementTargetFor(Core::ECS::EntityManager& EM, int EntityID)
{
    if (const auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
        Cmd && Cmd->TargetEntityID >= 0 &&
        (Cmd->CurrentCommand == CommandType::Attack ||
         Cmd->CurrentCommand == CommandType::AttackMove))
    {
        return Cmd->TargetEntityID;
    }

    if (const auto* Combat = EM.GetComponent<CombatComponent>(EntityID))
    {
        if (Combat->TargetEntityID >= 0)
            return Combat->TargetEntityID;
        return Combat->AutoTargetEntityID;
    }

    return -1;
}

static bool MovesInAir(Core::ECS::EntityManager& EM, int EntityID)
{
    if (const auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        return Path->IsAir;
    if (const auto* Combat = EM.GetComponent<CombatComponent>(EntityID))
        return Combat->MyElevation == Elevation::Air;
    return false;
}

static Core::Math::Vec2 ComputeCombatFormationTarget(
    Core::ECS::EntityManager& EM,
    int EntityID,
    int TargetID,
    float RangePixels)
{
    const auto* SelfTrans = EM.GetComponent<TransformComponent>(EntityID);
    const auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
    const auto* SelfCombat = EM.GetComponent<CombatComponent>(EntityID);
    const auto* SelfPlayer = EM.GetComponent<PlayerComponent>(EntityID);
    if (!SelfTrans || !TargetTrans || !SelfCombat || !SelfPlayer)
        return TargetTrans ? TargetTrans->Position : Core::Math::Vec2{};

    const bool SelfRanged = IsRangedAttack(*SelfCombat);
    const bool SelfAir = MovesInAir(EM, EntityID);
    std::vector<int> Engagers;
    std::vector<int> RoleEngagers;
    Core::Math::Vec2 GroupCenter;

    std::vector<int> NearbyUnits;
    if (sActiveCombatSpatialHash && !sActiveCombatSpatialHash->Empty())
    {
        const float SearchRadius = 512.0f;
        sActiveCombatSpatialHash->QueryInto(
            TargetTrans->Position.X - SearchRadius,
            TargetTrans->Position.Y - SearchRadius,
            TargetTrans->Position.X + SearchRadius,
            TargetTrans->Position.Y + SearchRadius,
            NearbyUnits);
    }
    else
    {
        NearbyUnits = EM.GetEntitiesWith<TransformComponent, UnitComponent,
                                          CombatComponent, PlayerComponent>();
    }

    for (int CandidateID : NearbyUnits)
    {
        const auto* CandidateUnit = EM.GetComponent<UnitComponent>(CandidateID);
        const auto* CandidatePlayer = EM.GetComponent<PlayerComponent>(CandidateID);
        const auto* CandidateCombat = EM.GetComponent<CombatComponent>(CandidateID);
        const auto* CandidateTrans = EM.GetComponent<TransformComponent>(CandidateID);
        const auto* CandidateBuilding = EM.GetComponent<BuildingComponent>(CandidateID);
        if (!CandidateUnit || CandidateUnit->HitPoints <= 0 ||
            !CandidatePlayer || !CandidateCombat || !CandidateTrans ||
            CandidatePlayer->PlayerID != SelfPlayer->PlayerID ||
            EngagementTargetFor(EM, CandidateID) != TargetID ||
            MovesInAir(EM, CandidateID) != SelfAir ||
            (CandidateBuilding && !CandidateBuilding->IsLifted))
        {
            continue;
        }

        Engagers.push_back(CandidateID);
        GroupCenter += CandidateTrans->Position;
        if (IsRangedAttack(*CandidateCombat) == SelfRanged)
            RoleEngagers.push_back(CandidateID);
    }

    if (Engagers.size() < 2 || RoleEngagers.empty())
        return TargetTrans->Position;

    std::sort(Engagers.begin(), Engagers.end());
    std::sort(RoleEngagers.begin(), RoleEngagers.end());
    GroupCenter = GroupCenter / (float)Engagers.size();

    Core::Math::Vec2 Forward = TargetTrans->Position - GroupCenter;
    if (Forward.LengthSq() <= 0.001f)
        Forward = TargetTrans->Position - SelfTrans->Position;
    if (Forward.LengthSq() <= 0.001f)
        Forward = {1.0f, 0.0f};
    else
        Forward = Forward.Normalized();
    const Core::Math::Vec2 Lateral{-Forward.Y, Forward.X};

    const auto SelfIt =
        std::find(RoleEngagers.begin(), RoleEngagers.end(), EntityID);
    if (SelfIt == RoleEngagers.end())
        return TargetTrans->Position;
    const int RoleIndex = (int)std::distance(RoleEngagers.begin(), SelfIt);
    const int Columns = std::min(5, (int)RoleEngagers.size());
    const int Row = RoleIndex / Columns;
    const int Column = RoleIndex % Columns;
    const int RowBegin = Row * Columns;
    const int RowCount = std::min(
        Columns, (int)RoleEngagers.size() - RowBegin);

    float SlotSpacing = 28.0f;
    for (int CandidateID : RoleEngagers)
        SlotSpacing = std::max(
            SlotSpacing, CombatCollisionRadius(EM, CandidateID) * 2.0f + 8.0f);
    const float LateralOffset =
        ((float)Column - (float)(RowCount - 1) * 0.5f) * SlotSpacing;

    const float SelfRadius = CombatCollisionRadius(EM, EntityID);
    const float TargetRadius = CombatCollisionRadius(EM, TargetID);
    const float RoleDepth = SelfRanged
        ? std::max(24.0f, RangePixels * 0.72f)
        : 6.0f;
    float StandOff = TargetRadius + SelfRadius + RoleDepth +
                     (float)Row * SlotSpacing;
    if (SelfRanged && RangePixels > TargetRadius + SelfRadius + 8.0f)
    {
        StandOff = std::min(StandOff, RangePixels - 6.0f);
    }
    else if (!SelfRanged)
    {
        StandOff = TargetRadius + SelfRadius + 4.0f;
    }

    return TargetTrans->Position - Forward * StandOff +
           Lateral * LateralOffset;
}

static void IssueCombatApproachPreservingCommand(
    Core::ECS::EntityManager& EM,
    int EntityID,
    int TargetID,
    float RangePixels,
    CommandType Command,
    const Core::Math::Vec2& CommandTarget,
    int PreserveTargetEntity)
{
    const auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
    if (!TargetTrans)
        return;

    const Core::Math::Vec2 ApproachTarget = ComputeCombatFormationTarget(
        EM, EntityID, TargetID, RangePixels);
    if (const auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
        Path && Path->PathIndex < (int)Path->Path.size())
    {
        Core::Math::Vec2 ExistingTarget = Path->RequestedDestination;
        if (ExistingTarget.LengthSq() <= 0.001f && !Path->Path.empty())
            ExistingTarget = Path->Path.back();
        if (ExistingTarget.DistanceTo(ApproachTarget) <= 24.0f)
            return;
    }

    IssueMovePreservingCommand(EM, EntityID, ApproachTarget,
                               Command, CommandTarget,
                               PreserveTargetEntity, TargetID);
}

static bool IsProtectedByDarkSwarm(Core::ECS::EntityManager& EM, int TargetID)
{
    if (IsAirTarget(EM, TargetID))
        return false;
    if (EM.GetComponent<BuildingComponent>(TargetID))
        return false;

    auto* Status = EM.GetComponent<StatusEffectComponent>(TargetID);
    if (Status && Status->HasEffect(StatusEffect::DarkSwarm))
        return true;

    if (!EM.HasAnyComponent<SpellAreaComponent>())
        return false;

    const auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
    if (!TargetTrans)
        return false;
    bool Protected = false;
    EM.ForEachEntityWith<TransformComponent, SpellAreaComponent>(
        [&](int AreaID)
        {
            if (Protected) return;
            const auto* AreaTrans = EM.GetComponent<TransformComponent>(AreaID);
            const auto* Area = EM.GetComponent<SpellAreaComponent>(AreaID);
            if (AreaTrans && Area && Area->Effect == StatusEffect::DarkSwarm &&
                Area->DurationRemaining > 0.0f &&
                TargetTrans->Position.DistanceTo(AreaTrans->Position) <= Area->Radius)
            {
                Protected = true;
            }
        });
    return Protected;
}

static bool IsUnderDisruptionWeb(Core::ECS::EntityManager& EM, int EntityID)
{
    if (IsAirTarget(EM, EntityID))
        return false;

    auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
    if (Status && Status->HasEffect(StatusEffect::DisruptionWeb))
        return true;

    if (!EM.HasAnyComponent<SpellAreaComponent>())
        return false;

    const auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
    if (!Trans)
        return false;

    bool UnderWeb = false;
    EM.ForEachEntityWith<TransformComponent, SpellAreaComponent>(
        [&](int AreaID)
        {
            if (UnderWeb) return;
            const auto* AreaTrans = EM.GetComponent<TransformComponent>(AreaID);
            const auto* Area = EM.GetComponent<SpellAreaComponent>(AreaID);
            if (AreaTrans && Area && Area->Effect == StatusEffect::DisruptionWeb &&
                Area->DurationRemaining > 0.0f &&
                Trans->Position.DistanceTo(AreaTrans->Position) <= Area->Radius)
            {
                UnderWeb = true;
            }
        });
    return UnderWeb;
}

static bool IsInvulnerableByStatus(Core::ECS::EntityManager& EM, int TargetID)
{
    if (auto* Unit = EM.GetComponent<UnitComponent>(TargetID))
    {
        if (Unit->IsInvincible)
            return true;
    }

    auto* Status = EM.GetComponent<StatusEffectComponent>(TargetID);
    return Status && Status->HasEffect(StatusEffect::Stasis);
}

static bool IsFrozenByStatus(Core::ECS::EntityManager& EM, int EntityID)
{
    auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
    return Status && (Status->HasEffect(StatusEffect::Stasis) ||
                      Status->HasEffect(StatusEffect::Lockdown) ||
                      Status->HasEffect(StatusEffect::Maelstrom));
}

static float ActiveStatusSeconds(const StatusEffectComponent& Status,
                                 StatusEffect Effect,
                                 float DeltaTime)
{
    for (int I = 0; I < StatusEffectComponent::kMaxEffects; ++I)
    {
        if (Status.ActiveEffects[I] == Effect && Status.EffectTimers[I] > 0.0f)
            return std::min(DeltaTime, Status.EffectTimers[I]);
    }
    return 0.0f;
}

static int StatusEffectSource(const StatusEffectComponent& Status,
                              StatusEffect Effect)
{
    for (int I = 0; I < StatusEffectComponent::kMaxEffects; ++I)
    {
        if (Status.ActiveEffects[I] == Effect && Status.EffectTimers[I] > 0.0f)
            return Status.EffectSources[I];
    }
    return -1;
}

static float ModifiedAttackCooldownSeconds(Core::ECS::EntityManager& EM,
                                           int EntityID,
                                           float BaseCooldownSeconds)
{
    auto* Status = EM.GetComponent<StatusEffectComponent>(EntityID);
    int CooldownFrames = std::max(1, (int)std::lround(BaseCooldownSeconds * 24.0f));
    if (const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        Unit && Unit->AcidSporeCount > 0)
    {
        CooldownFrames +=
            std::max(CooldownFrames / 8, 3) * Unit->AcidSporeCount;
    }

    // Brood War applies these modifiers to the integer weapons.dat frame
    // count, then clamps the result.  Keeping this in frames preserves such
    // important breakpoints as 15 -> 7 for Stim and 8 -> 5 for Adrenal
    // Glands instead of producing fractional-frame cooldowns.
    const bool HasSpeedBoost =
        (Status && Status->HasEffect(StatusEffect::Stim)) ||
        UpgradeSystem::GetAttackCooldownMultiplier(EM, EntityID) < 1.0f;
    if (HasSpeedBoost)
        CooldownFrames /= 2;
    if (Status && Status->HasEffect(StatusEffect::Ensnare))
        CooldownFrames += CooldownFrames / 4;

    return (float)std::clamp(CooldownFrames, 5, 250) / 24.0f;
}

static bool CanAttackTarget(const CombatComponent& Combat,
                            Core::ECS::EntityManager& EM,
                            int TargetID)
{
    if (!EM.IsAlive(TargetID))
        return false;
    if (IsProtectedResourceEntity(EM, TargetID))
        return false;
    if (ContainingTransport(EM, TargetID) >= 0)
        return false;
    const bool TargetAir = IsAirTarget(EM, TargetID);
    return TargetAir ? (Combat.CanTargetAir && Combat.AirDamage > 0)
                     : (Combat.CanTargetGround && Combat.GroundDamage > 0);
}

static float EffectiveAttackRangePixels(const Core::ECS::EntityManager& EM,
                                        int EntityID,
                                        const CombatComponent& Combat,
                                        int TargetID = -1)
{
    float BunkerBonusPixels = 0.0f;
    const int TransportID = ContainingTransport(const_cast<Core::ECS::EntityManager&>(EM), EntityID);
    if (TransportID >= 0)
    {
        const auto* TU = const_cast<Core::ECS::EntityManager&>(EM).GetComponent<UnitComponent>(TransportID);
        if (TU && TU->ChkUnitID == 125)
            BunkerBonusPixels = 32.0f;
    }

    if (TargetID < 0)
    {
        const float GroundRange = Combat.GroundAttackRange > 0.0f
            ? Combat.GroundAttackRange
            : Combat.AttackRange;
        const float AirRange = Combat.AirAttackRange > 0.0f
            ? Combat.AirAttackRange
            : Combat.AttackRange;
        const float GroundPixels = Combat.CanTargetGround
            ? GroundRange * 32.0f +
                (float)UpgradeSystem::GetRangeBonusPixelsForTarget(
                    EM, EntityID, false) + BunkerBonusPixels
            : 0.0f;
        const float AirPixels = Combat.CanTargetAir
            ? AirRange * 32.0f +
                (float)UpgradeSystem::GetRangeBonusPixelsForTarget(
                    EM, EntityID, true) + BunkerBonusPixels
            : 0.0f;
        return std::max(GroundPixels, AirPixels);
    }

    const bool TargetIsAir =
        IsAirTarget(const_cast<Core::ECS::EntityManager&>(EM), TargetID);
    const float ChannelRange = TargetIsAir
        ? Combat.AirAttackRange
        : Combat.GroundAttackRange;
    const float BaseRange = ChannelRange > 0.0f
        ? ChannelRange
        : Combat.AttackRange;
    return BaseRange * 32.0f +
           (float)UpgradeSystem::GetRangeBonusPixelsForTarget(EM, EntityID, TargetIsAir) +
           BunkerBonusPixels;
}

static void ClearCombatTargetForInvalidTarget(Core::ECS::EntityManager& EM,
                                              int AttackerID,
                                              int TargetID)
{
    auto ClearMotion = [&EM](int EntityID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }
    };

    if (auto* Combat = EM.GetComponent<CombatComponent>(AttackerID))
    {
        if (Combat->TargetEntityID == TargetID)
            Combat->TargetEntityID = -1;
        if (Combat->AutoTargetEntityID == TargetID)
            Combat->AutoTargetEntityID = -1;
        if (Combat->LastDamagerEntityID == TargetID)
            Combat->LastDamagerEntityID = -1;
        if (Combat->PendingAttackTargetEntityID == TargetID)
            Combat->PendingAttackTargetEntityID = -1;
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackDamage = 0;
        Combat->PendingAttackUpgradeBonus = 0;
        Combat->PendingAttackHitCount = 1;
        Combat->PendingAttackElapsed = 0.0f;
        Combat->PendingAttackMissedAtLaunch = false;
        Combat->AutoTargetLockSeconds = 0.0f;
    }

    if (auto* Cmd = EM.GetComponent<CommandComponent>(AttackerID))
    {
        if (Cmd->TargetEntityID == TargetID)
            Cmd->TargetEntityID = -1;
        if (Cmd->CurrentCommand == CommandType::Attack)
        {
            Cmd->CurrentCommand = CommandType::Stop;
            ClearMotion(AttackerID);
        }
    }
}

static void ClearCombatStateForInactiveAttacker(Core::ECS::EntityManager& EM, int AttackerID)
{
    if (auto* Combat = EM.GetComponent<CombatComponent>(AttackerID))
    {
        Combat->TargetEntityID = -1;
        Combat->AutoTargetEntityID = -1;
        Combat->LastDamagerEntityID = -1;
        Combat->AutoTargetLockSeconds = 0.0f;
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackTargetEntityID = -1;
        Combat->PendingAttackDamage = 0;
        Combat->PendingAttackUpgradeBonus = 0;
        Combat->PendingAttackHitCount = 1;
        Combat->PendingAttackElapsed = 0.0f;
        Combat->PendingAttackMissedAtLaunch = false;
    }

    if (auto* Cmd = EM.GetComponent<CommandComponent>(AttackerID))
    {
        Cmd->TargetEntityID = -1;
        if (Cmd->CurrentCommand == CommandType::Attack ||
            Cmd->CurrentCommand == CommandType::AttackMove)
        {
            Cmd->CurrentCommand = CommandType::Stop;
        }
    }

    if (auto* Path = EM.GetComponent<PathfindingComponent>(AttackerID))
    {
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};
    }
}

static void ClearDirectAttackOrderForInvalidTarget(Core::ECS::EntityManager& EM,
                                                   const std::vector<int>& Attackers,
                                                   int TargetEntityID)
{
    (void)TargetEntityID;
    for (int ID : Attackers)
    {
        if (auto* Combat = EM.GetComponent<CombatComponent>(ID))
        {
            Combat->TargetEntityID = -1;
            Combat->AutoTargetEntityID = -1;
            Combat->LastDamagerEntityID = -1;
            Combat->PendingAttackFrame = false;
            Combat->PendingAttackTargetEntityID = -1;
            Combat->PendingAttackDamage = 0;
            Combat->PendingAttackUpgradeBonus = 0;
            Combat->PendingAttackHitCount = 1;
            Combat->PendingAttackElapsed = 0.0f;
            Combat->PendingAttackMissedAtLaunch = false;
            Combat->AutoTargetLockSeconds = kInvalidDirectAttackAutoAcquireDelay;
        }

        if (auto* Cmd = EM.GetComponent<CommandComponent>(ID))
        {
            Cmd->CurrentCommand = CommandType::Stop;
            Cmd->TargetEntityID = -1;
        }

        if (auto* Path = EM.GetComponent<PathfindingComponent>(ID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }
    }
}

void CombatSystem::SetDetectorChecker(DetectorChecker Checker)
{
    gDetectorChecker = std::move(Checker);
}

void CombatSystem::SetAllianceChecker(AllianceChecker Checker)
{
    gAllianceChecker = std::move(Checker);
}

void CombatSystem::IssueAttack(Core::ECS::EntityManager& EM,
                                const std::vector<int>& Attackers,
                                int TargetEntityID)
{
    auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetEntityID);
    auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
    if (!EM.IsAlive(TargetEntityID) ||
        !TargetTrans ||
        !TargetUnit ||
        TargetUnit->HitPoints <= 0 ||
        IsProtectedResourceEntity(EM, TargetEntityID))
    {
        ClearDirectAttackOrderForInvalidTarget(EM, Attackers, TargetEntityID);
        return;
    }

    for (int ID : Attackers)
    {
        auto* Combat = EM.GetComponent<CombatComponent>(ID);
        if (!Combat) continue;
        auto* AttackerUnit = EM.GetComponent<UnitComponent>(ID);
        if (!AttackerUnit || AttackerUnit->HitPoints <= 0 || IsFrozenByStatus(EM, ID))
        {
            ClearCombatStateForInactiveAttacker(EM, ID);
            continue;
        }
        if (!CanAttackTarget(*Combat, EM, TargetEntityID))
        {
            ClearCombatTargetForInvalidTarget(EM, ID, TargetEntityID);
            continue;
        }

        Combat->TargetEntityID = TargetEntityID;

        auto* Cmd = EM.GetComponent<CommandComponent>(ID);
        if (!Cmd)
            Cmd = &EM.AddComponent<CommandComponent>(ID);
        Cmd->CurrentCommand = CommandType::Attack;
        Cmd->TargetEntityID = TargetEntityID;

        IssueMovePreservingCommand(EM, ID, TargetTrans->Position,
                                   CommandType::Attack, TargetTrans->Position, TargetEntityID,
                                   TargetEntityID);
    }
}

void CombatSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                    const std::vector<int>& Units,
                                    Core::Math::Vec2 Target)
{
    CommandSystem::IssueAttackMove(EM, Units, Target);
}

bool CombatSystem::AreEnemies(int A, int B, Core::ECS::EntityManager& EM)
{
    return AreEnemyPlayers(EM, A, B);
}

static int EvaluateTargetAttackPriority(Core::ECS::EntityManager& EM, int AttackerID, int TargetID)
{
    const auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
    if (!TargetUnit)
        return 5;

    // Eggs and Larvae have the lowest priority
    if (TargetUnit->ChkUnitID == 35 || TargetUnit->ChkUnitID == 36 || TargetUnit->ChkUnitID == 97)
        return 5;

    const auto* AttackerCombat = EM.GetComponent<CombatComponent>(AttackerID);
    const auto* TargetCombat = EM.GetComponent<CombatComponent>(TargetID);
    const auto* TargetBuilding = EM.GetComponent<BuildingComponent>(TargetID);

    // Retaliation: if this enemy was the last one that damaged us, give it highest priority
    if (AttackerCombat && AttackerCombat->LastDamagerEntityID == TargetID)
        return 0;

    const bool AttackerIsAir = MovesInAir(EM, AttackerID);

    // Defensive buildings (Photon Cannon, Sunken Colony, Spore Colony, Missile Turret, Bunker)
    if (TargetBuilding)
    {
        const bool IsBunker = (TargetUnit->ChkUnitID == 125);
        const bool ArmedBuilding = TargetCombat && (TargetCombat->CanTargetGround || TargetCombat->CanTargetAir);
        if (IsBunker || ArmedBuilding)
        {
            bool ThreatensAttacker = false;
            if (IsBunker)
            {
                ThreatensAttacker = true;
            }
            else if (TargetCombat)
            {
                ThreatensAttacker = AttackerIsAir ? TargetCombat->CanTargetAir : TargetCombat->CanTargetGround;
            }
            return ThreatensAttacker ? 1 : 2;
        }
        // Non-combat building (Pylon, Supply Depot, Barracks, Hatchery, etc.)
        return 4;
    }

    // Workers (SCV, Probe, Drone)
    if (IsWorkerUnit(TargetUnit))
        return 3;

    // Combat units
    if (TargetCombat && (TargetCombat->CanTargetGround || TargetCombat->CanTargetAir))
    {
        const bool CanThreatenAttacker = AttackerIsAir ? TargetCombat->CanTargetAir : TargetCombat->CanTargetGround;
        if (CanThreatenAttacker)
            return 0; // Armed enemy unit that can attack me
        else
            return 2; // Armed enemy unit that cannot attack me
    }

    // Unarmed mobile military/special units (e.g. High Templar, Defiler, Medic, Observer, Overlord, Dropship)
    return 3;
}

int CombatSystem::FindNearestEnemy(Core::ECS::EntityManager& EM, int EntityID, float MaxRange)
{
    auto* MyTrans = EM.GetComponent<TransformComponent>(EntityID);
    if (!MyTrans) return -1;

    int BestID = -1;
    int BestPriority = 999;
    float BestDist = MaxRange;
    const float BoxLimit = MaxRange + 64.0f;

    static thread_local std::vector<int> Entities;
    if (sActiveCombatSpatialHash && !sActiveCombatSpatialHash->Empty())
    {
        sActiveCombatSpatialHash->QueryInto(
            MyTrans->Position.X - BoxLimit, MyTrans->Position.Y - BoxLimit,
            MyTrans->Position.X + BoxLimit, MyTrans->Position.Y + BoxLimit,
            Entities);
    }
    else
    {
        Entities = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
    }
    for (int ID : Entities)
    {
        if (ID == EntityID) continue;

        auto* TargetTrans = EM.GetComponent<TransformComponent>(ID);
        if (!TargetTrans) continue;

        const float DX = std::abs(TargetTrans->Position.X - MyTrans->Position.X);
        const float DY = std::abs(TargetTrans->Position.Y - MyTrans->Position.Y);
        if (DX > BoxLimit || DY > BoxLimit)
            continue;

        if (!AreEnemies(EntityID, ID, EM)) continue;

        auto* TargetUnit = EM.GetComponent<UnitComponent>(ID);
        if (!TargetUnit) continue;
        if (TargetUnit->HitPoints <= 0) continue;
        if (IsProtectedResourceEntity(EM, ID)) continue;
        auto* MyCombat = EM.GetComponent<CombatComponent>(EntityID);
        if (MyCombat && !CanAttackTarget(*MyCombat, EM, ID)) continue;

        // Skip cloaked units we can't detect
        auto* Cloak = EM.GetComponent<CloakComponent>(ID);
        if (Cloak && (Cloak->IsCloaked || Cloak->IsBurrowed))
        {
            auto* MyUnit = EM.GetComponent<UnitComponent>(EntityID);
            bool IsDetector = MyUnit && MyUnit->IsDetector;
            if (!IsDetector)
            {
                auto* MyPlayer = EM.GetComponent<PlayerComponent>(EntityID);
                const int PlayerID = MyPlayer ? MyPlayer->PlayerID : 0;
                if (!IsTargetDetectedForPlayer(EM, ID, PlayerID))
                    continue;
            }
        }

        float Dist = CombatEdgeDistance(EM, EntityID, ID);
        if (Dist > MaxRange)
            continue;
        if (MyCombat && MyCombat->MinimumAttackRange > 0.0f &&
            Dist < MyCombat->MinimumAttackRange * 32.0f)
        {
            continue;
        }

        const int Priority = EvaluateTargetAttackPriority(EM, EntityID, ID);
        if (Priority < BestPriority || (Priority == BestPriority && Dist < BestDist))
        {
            BestPriority = Priority;
            BestDist = Dist;
            BestID = ID;
        }
    }
    return BestID;
}

int CombatSystem::PickWeaponDamage(const CombatComponent& Combat,
                                    const UnitComponent* TargetUnit,
                                    bool TargetIsAir) const
{
    if (!TargetUnit) return 0;

    if (TargetIsAir)
        return Combat.CanTargetAir ? Combat.AirDamage : 0;
    return Combat.CanTargetGround ? Combat.GroundDamage : 0;
}

int CombatSystem::PickWeaponHitCount(const CombatComponent& Combat,
                                     const UnitComponent* AttackerUnit,
                                     Core::ECS::EntityManager& EM,
                                     int AttackerID,
                                     bool TargetIsAir) const
{
    // Interceptors are not separate combat entities yet, but scenario-authored
    // HangarCount still controls the aggregate volley. A negative count marks
    // runtime-produced units that use the current full-payload abstraction.
    if (AttackerUnit && AttackerUnit->ChkUnitID == 72)
    {
        const int Capacity = UpgradeSystem::GetMaxHangarCount(EM, AttackerID);
        if (Capacity > 0)
        {
            return AttackerUnit->HangarCount >= 0
                ? std::clamp(AttackerUnit->HangarCount, 0, Capacity)
                : Capacity;
        }
    }
    return std::max(1,
        TargetIsAir ? Combat.AirHitCount : Combat.GroundHitCount);
}

float CombatSystem::PickWeaponCooldown(const CombatComponent& Combat,
                                       bool TargetIsAir) const
{
    const float ChannelCooldown = TargetIsAir
        ? Combat.AirAttackCooldown
        : Combat.GroundAttackCooldown;
    return ChannelCooldown > 0.0f
        ? ChannelCooldown
        : Combat.AttackCooldown;
}

bool CombatSystem::RollMissChance(const CombatComponent& Combat,
                                  const TransformComponent& AttackerTrans,
                                  const TransformComponent& TargetTrans,
                                  float RangePixels,
                                  Core::ECS::EntityManager& EM,
                                  int AttackerID,
                                  int TargetID)
{
    (void)RangePixels;
    if (!mMap || !IsRangedAttack(Combat) ||
        IsAirTarget(EM, AttackerID) || IsAirTarget(EM, TargetID) ||
        Combat.WeaponBeh == WeaponBehavior::Melee ||
        Combat.WeaponBeh == WeaponBehavior::Lob ||
        Combat.WeaponBeh == WeaponBehavior::Persist ||
        Combat.Splash == SplashType::Line)
    {
        return false;
    }

    const int AttackerHeight =
        mMap->GetGroundHeightAtWorld(AttackerTrans.Position);
    const int TargetHeight =
        mMap->GetGroundHeightAtWorld(TargetTrans.Position);
    if (AttackerHeight < 0 || TargetHeight <= AttackerHeight)
        return false;

    // Brood War compares an 8-bit RNG value against 119, yielding a
    // 120/256 miss rate for low-ground ranged fire into high ground. Keep the
    // stream local and deterministic so lockstep simulations remain stable.
    mMissRngState = mMissRngState * 22695477u + 1u;
    const uint8_t Roll = (uint8_t)((mMissRngState >> 16) & 0xffu);
    return Roll <= 119u;
}

void CombatSystem::ApplyDamage(Core::ECS::EntityManager& EM, int TargetID,
                                const CombatComponent& Attacker, int AttackerID,
                                int BaseDamage, int HitCount)
{
    if (BaseDamage <= 0)
        return;
    ApplyDamageRaw(EM, TargetID, Attacker, AttackerID,
                   BaseDamage * 256, HitCount);
}

void CombatSystem::ApplyDamageRaw(Core::ECS::EntityManager& EM, int TargetID,
                                   const CombatComponent& Attacker, int AttackerID,
                                   int BaseDamageRaw, int HitCount,
                                   bool RespectDarkSwarm)
{
    const auto* AttackerStatus =
        EM.GetComponent<StatusEffectComponent>(AttackerID);
    const bool IsHallucinatedAttacker =
        AttackerStatus &&
        AttackerStatus->HasEffect(StatusEffect::Hallucination);

    auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
    if (!EM.IsAlive(TargetID) || !TargetUnit || TargetUnit->HitPoints <= 0) return;
    if (BaseDamageRaw <= 0) return;
    if (IsProtectedResourceEntity(EM, TargetID)) return;
    if (IsInvulnerableByStatus(EM, TargetID)) return;
    if (RespectDarkSwarm && IsRangedAttack(Attacker) &&
        IsProtectedByDarkSwarm(EM, TargetID)) return;

    BaseDamageRaw += TargetUnit->AcidSporeCount * 256;

    if (IsHallucinatedAttacker)
    {
        if (ExactVitalRaw(TargetUnit->ShieldPoints, TargetUnit->ShieldSubDamage) > 0)
        {
            QueueShieldImpact(EM, TargetID, AttackerID);
        }
        return;
    }

    const bool TargetIsAir = IsAirTarget(EM, TargetID);
    const DamageType DamageKind = TargetIsAir ? Attacker.AirDmgType : Attacker.GroundDmgType;
    const bool IgnoresArmor = DamageKind == DamageType::IgnoreArmor;
    constexpr int MinimumDamageRaw = 128;

    if (auto* TargetStatus = EM.GetComponent<StatusEffectComponent>(TargetID);
        TargetStatus && TargetStatus->HasEffect(StatusEffect::Hallucination))
    {
        BaseDamageRaw = BaseDamageRaw > std::numeric_limits<int>::max() / 2
            ? std::numeric_limits<int>::max()
            : BaseDamageRaw * 2;
    }

    int ShieldArmor = 0;
    if (auto* TargetPlayer = EM.GetComponent<PlayerComponent>(TargetID))
    {
        ShieldArmor = UpgradeSystem::GetResearchLevel(
            TargetPlayer->PlayerID, ResearchId::Protoss_PlasmaShields);
    }
    const int Armor =
        TargetUnit->Armor + UpgradeSystem::GetArmorBonus(EM, TargetID);

    bool HadShieldDamage = false;
    for (int Hit = 0; Hit < std::max(1, HitCount); ++Hit)
    {
        int RemainingDamageRaw = BaseDamageRaw;
        bool AbsorbedShieldDamage = false;

        if (auto* TargetStatus = EM.GetComponent<StatusEffectComponent>(TargetID);
            TargetStatus && TargetStatus->HasEffect(StatusEffect::DefensiveMatrix) &&
            TargetUnit->DefensiveMatrixPoints > 0)
        {
            const int MatrixRaw = TargetUnit->DefensiveMatrixPoints * kSubPointsPerPoint;
            if (MatrixRaw >= RemainingDamageRaw)
            {
                const int NewMatrixRaw = MatrixRaw - RemainingDamageRaw;
                TargetUnit->DefensiveMatrixPoints =
                    (NewMatrixRaw + kSubPointsPerPoint - 1) / kSubPointsPerPoint;

                // Fixed scratch damage (0.5 visible points / MinimumDamageRaw = 128) dealt directly to HP
                int HitPointsRaw = ExactVitalRaw(TargetUnit->HitPoints, TargetUnit->HitPointSubDamage);
                HitPointsRaw = std::max(0, HitPointsRaw - MinimumDamageRaw);
                StoreVitalRaw(HitPointsRaw, TargetUnit->HitPoints, TargetUnit->HitPointSubDamage);
                RemainingDamageRaw = 0;
            }
            else
            {
                RemainingDamageRaw -= MatrixRaw;
                TargetUnit->DefensiveMatrixPoints = 0;
                TargetStatus->RemoveEffect(StatusEffect::DefensiveMatrix);
            }
        }

        int ShieldRaw = ExactVitalRaw(
            TargetUnit->ShieldPoints, TargetUnit->ShieldSubDamage);
        // OpenBW only routes a weapon hit through shields while at least one
        // full shield point remains. Fractional shield energy is retained and
        // becomes active again after regeneration reaches that threshold.
        if (TargetUnit->MaxShieldPoints > 0 &&
            ShieldRaw >= kSubPointsPerPoint)
        {
            const int ShieldArmorRaw = IgnoresArmor ? 0 : ShieldArmor * kSubPointsPerPoint;
            int ShieldDamageRaw = RemainingDamageRaw > ShieldArmorRaw
                ? RemainingDamageRaw - ShieldArmorRaw
                : MinimumDamageRaw;
            const int Absorbed = std::min(ShieldRaw, ShieldDamageRaw);
            ShieldRaw -= Absorbed;
            RemainingDamageRaw = ShieldDamageRaw - Absorbed;
            AbsorbedShieldDamage = Absorbed > 0;
            HadShieldDamage = HadShieldDamage || AbsorbedShieldDamage;
            StoreVitalRaw(ShieldRaw,
                          TargetUnit->ShieldPoints,
                          TargetUnit->ShieldSubDamage);
        }

        if (RemainingDamageRaw > 0)
        {
            const int ArmorRaw = IgnoresArmor ? 0 : Armor * kSubPointsPerPoint;
            int HitPointDamageRaw = RemainingDamageRaw > ArmorRaw
                ? RemainingDamageRaw - ArmorRaw
                : 0;
            if (!IgnoresArmor)
            {
                HitPointDamageRaw = ApplySizeDamageModifierRaw(
                    HitPointDamageRaw, DamageKind, TargetUnit->SizeType);
            }
            if (!AbsorbedShieldDamage &&
                HitPointDamageRaw < MinimumDamageRaw)
            {
                HitPointDamageRaw = MinimumDamageRaw;
            }

            int HitPointsRaw = ExactVitalRaw(
                TargetUnit->HitPoints, TargetUnit->HitPointSubDamage);
            HitPointsRaw = std::max(0, HitPointsRaw - HitPointDamageRaw);
            StoreVitalRaw(HitPointsRaw,
                          TargetUnit->HitPoints,
                          TargetUnit->HitPointSubDamage);
            if (HitPointsRaw <= 0)
                break;
        }
    }

    if (HadShieldDamage &&
        ExactVitalRaw(TargetUnit->ShieldPoints,
                      TargetUnit->ShieldSubDamage) > 0)
    {
        QueueShieldImpact(EM, TargetID, AttackerID);
    }

    if (TargetUnit->HitPoints > 0)
    {
        if (auto* TargetCombat = EM.GetComponent<CombatComponent>(TargetID))
        {
            TargetCombat->LastDamagerEntityID = AttackerID;
            auto* TargetBuilding = EM.GetComponent<BuildingComponent>(TargetID);
            if (AreEnemyPlayers(EM, TargetID, AttackerID) &&
                !IsWorkerUnit(TargetUnit) &&
                CanAttackTarget(*TargetCombat, EM, AttackerID))
            {
                SetAutoTarget(*TargetCombat, AttackerID, kAutoTargetLockSeconds);
            }
            else if (TargetBuilding &&
                     AreEnemyPlayers(EM, TargetID, AttackerID) &&
                     CanAttackTarget(*TargetCombat, EM, AttackerID))
            {
                SetAutoTarget(*TargetCombat, AttackerID, kAutoTargetLockSeconds);
            }
        }
    }
    if (TargetUnit->HitPoints <= 0)
    {
        TargetUnit->HitPoints = 0;
        RecordUnitDeath(EM, TargetID, AttackerID, true);
        const bool RestoredGeyser = RestoreClaimedGeyser(EM, TargetID);
        if (!RestoredGeyser)
            EM.DestroyEntity(TargetID);
        ClearCombatTargetForInvalidTarget(EM, AttackerID, TargetID);
        Core::Utils::Logger::Info("CombatSystem",
            "Entity " + std::to_string(TargetID) + " destroyed by " +
            std::to_string(AttackerID) +
            (RestoredGeyser ? "; restored vespene geyser" : ""));
    }
}

bool CombatSystem::ResolvePendingAttackFrame(Core::ECS::EntityManager& EM,
                                             BulletSystem& BulletSys,
                                             int AttackerID,
                                             float ForwardOffsetPixels)
{
    auto* Combat = EM.GetComponent<CombatComponent>(AttackerID);
    auto* Trans = EM.GetComponent<TransformComponent>(AttackerID);
    auto* PlayerComp = EM.GetComponent<PlayerComponent>(AttackerID);
    auto* AttackerUnit = EM.GetComponent<UnitComponent>(AttackerID);
    if (!Combat || !Trans || !Combat->PendingAttackFrame)
        return false;
    if (!AttackerUnit || AttackerUnit->HitPoints <= 0)
    {
        ClearCombatStateForInactiveAttacker(EM, AttackerID);
        return false;
    }
    if (IsUnderDisruptionWeb(EM, AttackerID))
    {
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackTargetEntityID = -1;
        Combat->PendingAttackDamage = 0;
        Combat->PendingAttackUpgradeBonus = 0;
        Combat->PendingAttackHitCount = 1;
        Combat->PendingAttackElapsed = 0.0f;
        Combat->PendingAttackMissedAtLaunch = false;
        return false;
    }

    const int TargetID = Combat->PendingAttackTargetEntityID;
    Core::Math::Vec2 TargetPos = Combat->PendingAttackTargetPos;
    if (TargetID >= 0)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);
        auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
        if (!EM.IsAlive(TargetID) || !TargetUnit || TargetUnit->HitPoints <= 0 || !TargetTrans)
        {
            ClearCombatTargetForInvalidTarget(EM, AttackerID, TargetID);
            return false;
        }
        if (IsProtectedResourceEntity(EM, TargetID))
        {
            ClearCombatTargetForInvalidTarget(EM, AttackerID, TargetID);
            return false;
        }
        TargetPos = TargetTrans->Position;
    }

    if (Combat->PendingAttackDamage <= 0)
    {
        Combat->PendingAttackFrame = false;
        Combat->PendingAttackTargetEntityID = -1;
        Combat->PendingAttackHitCount = 1;
        Combat->PendingAttackMissedAtLaunch = false;
        return false;
    }

    const bool DirectMelee =
        (Combat->WeaponBeh == WeaponBehavior::Melee ||
         (Combat->AttackRange <= 1.0f && Combat->WeaponBeh != WeaponBehavior::Bounce &&
          Combat->WeaponBeh != WeaponBehavior::AppearOnTarget &&
          Combat->WeaponBeh != WeaponBehavior::Lob)) &&
        Combat->Splash == SplashType::None;
    const bool DestroySourceDirectly =
        DirectMelee && Combat->SelfDestructOnAttack;
    if (DirectMelee)
    {
        if (TargetID >= 0)
        {
            ApplyDamage(
                EM, TargetID, *Combat, AttackerID,
                Combat->PendingAttackDamage +
                    Combat->PendingAttackUpgradeBonus,
                1);
        }

        // Multi-hit melee scripts author one AttackMelee opcode per impact.
        // Keep the request armed until every authored hit has consumed one
        // damage application instead of collapsing all hits into the first
        // blade/contact frame.
        if (Combat->PendingAttackHitCount > 1)
        {
            --Combat->PendingAttackHitCount;
            Combat->PendingAttackElapsed = 0.0f;
            return true;
        }
    }
    else if (PlayerComp)
    {
        TransformComponent LaunchTrans = *Trans;
        if (std::abs(ForwardOffsetPixels) > 0.001f)
        {
            const Core::Math::Vec2 ToTarget =
                TargetPos - Trans->Position;
            const Core::Math::Vec2 LaunchDirection =
                ToTarget.Length() > 0.001f
                    ? ToTarget.Normalized()
                    : Core::Math::Vec2{1.0f, 0.0f};
            LaunchTrans.Position =
                LaunchTrans.Position + LaunchDirection * ForwardOffsetPixels;
        }
        BulletSys.SpawnBullet(EM, *Combat, LaunchTrans, *PlayerComp,
            AttackerID, Combat->PendingAttackDamage, Combat->PendingAttackUpgradeBonus,
            Combat->PendingAttackRangePixels, TargetID, TargetPos,
            Combat->PendingAttackHitCount,
            Combat->PendingAttackMissedAtLaunch);
    }

    Combat->PendingAttackFrame = false;
    Combat->PendingAttackTargetEntityID = -1;
    Combat->PendingAttackDamage = 0;
    Combat->PendingAttackUpgradeBonus = 0;
    Combat->PendingAttackHitCount = 1;
    Combat->PendingAttackElapsed = 0.0f;
    Combat->PendingAttackMissedAtLaunch = false;

    if (DestroySourceDirectly && EM.IsAlive(AttackerID))
    {
        if (auto* SourceUnit = EM.GetComponent<UnitComponent>(AttackerID))
            SourceUnit->HitPoints = 0;
        RecordUnitDeath(EM, AttackerID, -1, false);
        EM.DestroyEntity(AttackerID);
    }
    return true;
}

bool CombatSystem::BeginOrResolveAttack(Core::ECS::EntityManager& EM,
                                        int EntityID,
                                        int TargetID,
                                        const TransformComponent& TargetTrans,
                                        int BaseDamage,
                                        int UpgradeBonus,
                                        int HitCount,
                                        float RangePixels,
                                        bool MissedAtLaunch)
{
    auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
    if (!Combat)
        return false;

    const bool HasIScriptAttack = EntityHasIScriptAttack(EM, EntityID, TargetID);

    if (HasIScriptAttack)
    {
        Combat->PendingAttackFrame = true;
        Combat->PendingAttackTargetEntityID = TargetID;
        Combat->PendingAttackTargetPos = TargetTrans.Position;
        Combat->PendingAttackDamage = BaseDamage;
        Combat->PendingAttackUpgradeBonus = UpgradeBonus;
        Combat->PendingAttackHitCount = std::max(1, HitCount);
        Combat->PendingAttackRangePixels = RangePixels;
        Combat->PendingAttackElapsed = 0.0f;
        Combat->PendingAttackMissedAtLaunch = MissedAtLaunch;
        return true;
    }

    Combat->PendingAttackFrame = true;
    Combat->PendingAttackTargetEntityID = TargetID;
    Combat->PendingAttackTargetPos = TargetTrans.Position;
    Combat->PendingAttackDamage = BaseDamage;
    Combat->PendingAttackUpgradeBonus = UpgradeBonus;
    Combat->PendingAttackHitCount = std::max(1, HitCount);
    Combat->PendingAttackRangePixels = RangePixels;
    Combat->PendingAttackElapsed = 0.0f;
    Combat->PendingAttackMissedAtLaunch = MissedAtLaunch;
    ResolvePendingAttackFrame(EM, mBulletSystem, EntityID);
    return true;
}

void CombatSystem::UpdateStatusEffects(Core::ECS::EntityManager& EM, float DeltaTime)
{
    auto Entities = EM.GetEntitiesWith<UnitComponent>();
    for (int ID : Entities)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (!Unit)
            continue;

        int ActiveAcidSpores = 0;
        for (float& Timer : Unit->AcidSporeTimers)
        {
            if (Timer > 0.0f)
            {
                Timer = std::max(0.0f, Timer - DeltaTime);
                if (Timer > 0.0f)
                    ++ActiveAcidSpores;
            }
        }
        Unit->AcidSporeCount = ActiveAcidSpores;

        auto* Status = EM.GetComponent<StatusEffectComponent>(ID);
        if (!Status) continue;

        if (Unit->HitPoints <= 0)
        {
            Status->Tick(DeltaTime);
            continue;
        }

        // Plague is updated every eight Brood War frames. Each tick removes
        // 1010/256 HP, bypasses shields and armor, and only applies while the
        // target has strictly more than one tick of raw HP remaining.
        float PlagueSeconds = ActiveStatusSeconds(*Status, StatusEffect::Plague, DeltaTime);
        if (PlagueSeconds > 0.0f)
        {
            constexpr float TickSeconds = 8.0f / 24.0f;
            constexpr int TickDamageRaw = 1010;
            Unit->PlagueDamageAccumulator += PlagueSeconds;
            const int TickCount = (int)std::floor(
                (Unit->PlagueDamageAccumulator + 0.000001f) / TickSeconds);
            if (TickCount > 0)
            {
                Unit->PlagueDamageAccumulator -= (float)TickCount * TickSeconds;
                if (!IsInvulnerableByStatus(EM, ID))
                {
                    int HitPointsRaw = ExactVitalRaw(
                        Unit->HitPoints, Unit->HitPointSubDamage);
                    for (int Tick = 0; Tick < TickCount; ++Tick)
                    {
                        if (HitPointsRaw > TickDamageRaw)
                            HitPointsRaw -= TickDamageRaw;
                    }
                    StoreVitalRaw(HitPointsRaw,
                                  Unit->HitPoints,
                                  Unit->HitPointSubDamage);
                }
            }
        }
        else
        {
            Unit->PlagueDamageAccumulator = 0.0f;
        }

        // Irradiate also ticks every eight frames. The irradiated entity is a
        // moving carrier; only nearby organic non-buildings take the exact
        // 853/256 raw spell damage, with armor and size modifiers ignored.
        float IrradiateSeconds = ActiveStatusSeconds(*Status, StatusEffect::Irradiate, DeltaTime);
        if (IrradiateSeconds > 0.0f)
        {
            constexpr float TickSeconds = 8.0f / 24.0f;
            constexpr int TickDamageRaw = 853;
            Unit->IrradiateDamageAccumulator += IrradiateSeconds;
            const int TickCount = (int)std::floor(
                (Unit->IrradiateDamageAccumulator + 0.000001f) / TickSeconds);
            if (TickCount > 0)
            {
                Unit->IrradiateDamageAccumulator -= (float)TickCount * TickSeconds;
                const int EffectSource = StatusEffectSource(*Status, StatusEffect::Irradiate);
                const int DamageSource = EffectSource >= 0 ? EffectSource : ID;
                CombatComponent IrradiateDamage;
                IrradiateDamage.GroundDmgType = DamageType::IgnoreArmor;
                IrradiateDamage.AirDmgType = DamageType::IgnoreArmor;
                IrradiateDamage.CanTargetGround = true;
                IrradiateDamage.CanTargetAir = true;
                IrradiateDamage.WeaponBeh = WeaponBehavior::Persist;

                for (int Tick = 0; Tick < TickCount && EM.IsAlive(ID); ++Tick)
                {
                    std::vector<int> Victims;
                    const bool SourceBurrowed = IsEntityBurrowed(EM, ID);
                    const int TransportID = ContainingTransport(EM, ID);
                    if (SourceBurrowed)
                    {
                        Victims.push_back(ID);
                    }
                    else if (TransportID >= 0)
                    {
                        Victims.push_back(TransportID);
                    }
                    else
                    {
                        for (int VictimID : EM.GetEntitiesWith<UnitComponent>())
                        {
                            if (!EM.IsAlive(VictimID))
                                continue;
                            auto* Victim = EM.GetComponent<UnitComponent>(VictimID);
                            if (!Victim || !CanTakeIrradiateDamage(*Victim))
                                continue;
                            if (VictimID != ID && IsEntityBurrowed(EM, VictimID))
                                continue;
                            if (CombatEdgeDistance(EM, ID, VictimID) <= 32.0f)
                                Victims.push_back(VictimID);
                        }
                    }

                    for (int VictimID : Victims)
                    {
                        auto* Victim = EM.GetComponent<UnitComponent>(VictimID);
                        if (!EM.IsAlive(VictimID) || !Victim ||
                            !CanTakeIrradiateDamage(*Victim))
                        {
                            continue;
                        }
                        ApplyDamageRaw(EM, VictimID, IrradiateDamage,
                                       DamageSource, TickDamageRaw, 1, false);
                    }
                }
            }
        }
        else
        {
            Unit->IrradiateDamageAccumulator = 0.0f;
        }

        if (!EM.IsAlive(ID))
            continue;
        Unit = EM.GetComponent<UnitComponent>(ID);
        Status = EM.GetComponent<StatusEffectComponent>(ID);
        if (!Unit || !Status)
            continue;

        // EMP: zero shields (handled on application), energy drain
        if (Status->HasEffect(StatusEffect::EMP))
        {
            Unit->ShieldPoints = 0;
            Unit->ShieldSubDamage = 0;
            Unit->EnergyPoints = 0;
            Unit->DefensiveMatrixPoints = 0;
            Status->RemoveEffect(StatusEffect::DefensiveMatrix);
            if (auto* Cloak = EM.GetComponent<CloakComponent>(ID);
                Cloak && Cloak->IsCloaked && !Cloak->IsPassiveCloak && !Cloak->IsBurrowed)
            {
                Cloak->IsCloaked = false;
            }
        }

        // Stasis: freeze unit (handled by disabling combat/movement in respective systems)
        const bool WasHallucination =
            Status->HasEffect(StatusEffect::Hallucination);
        Status->Tick(DeltaTime);
        if (!Status->HasEffect(StatusEffect::DefensiveMatrix))
        {
            Unit->DefensiveMatrixPoints = 0;
        }
        if (WasHallucination &&
            !Status->HasEffect(StatusEffect::Hallucination))
        {
            // Hallucination expiration is not a real unit death: no supply,
            // provider capacity, or kill score changes. Preserve only its
            // short authored disappearance visual.
            const auto* Render = EM.GetComponent<RenderComponent>(ID);
            const bool WasHidden = Render && !Render->Visible;
            SpawnDeathVisual(EM, ID, *Unit, WasHidden);
            EM.DestroyEntity(ID);
        }
    }
}

void CombatSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();

    // Brood War restores 7/256 shield points per logic frame (24 Hz).
    auto ShieldUnits = EM.GetEntitiesWith<UnitComponent>();
    for (int ID : ShieldUnits)
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        if (Unit && Unit->MaxShieldPoints > 0 &&
            (Unit->ShieldPoints < Unit->MaxShieldPoints ||
             Unit->ShieldSubDamage > 0))
        {
            constexpr int ShieldSubPointsPerSecond = 7 * 24;
            Unit->ShieldRegenAccumulator +=
                DeltaTime * (float)ShieldSubPointsPerSecond;
            const int RegenRaw =
                (int)(Unit->ShieldRegenAccumulator + 0.0001f);
            if (RegenRaw > 0)
            {
                Unit->ShieldRegenAccumulator -= (float)RegenRaw;
                if (Unit->ShieldRegenAccumulator < 0.0f)
                    Unit->ShieldRegenAccumulator = 0.0f;
                int ShieldRaw = std::max(
                    0, Unit->ShieldPoints * 256 -
                       std::clamp(Unit->ShieldSubDamage, 0, 255));
                ShieldRaw = std::min(
                    Unit->MaxShieldPoints * 256,
                    ShieldRaw + RegenRaw);
                Unit->ShieldPoints = (ShieldRaw + 255) / 256;
                Unit->ShieldSubDamage =
                    Unit->ShieldPoints * 256 - ShieldRaw;
            }
        }
        else if (Unit && Unit->MaxShieldPoints > 0 &&
                 Unit->ShieldPoints >= Unit->MaxShieldPoints)
        {
            Unit->ShieldPoints = Unit->MaxShieldPoints;
            Unit->ShieldSubDamage = 0;
            Unit->ShieldRegenAccumulator = 0.0f;
        }
    }

    // Status effect tick
    UpdateStatusEffects(EM, DeltaTime);

    // Active cloaked units drain energy over time and decloak upon depletion
    for (int ID : EM.GetEntitiesWith<UnitComponent, CloakComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Cloak = EM.GetComponent<CloakComponent>(ID);
        if (!Unit || !Cloak || Unit->HitPoints <= 0)
            continue;

        if (Cloak->IsCloaked && !Cloak->IsPassiveCloak && !Cloak->IsBurrowed)
        {
            const float DrainRate = Cloak->CloakEnergyCost > 0.0f ? Cloak->CloakEnergyCost : 1.25f;
            Unit->CloakEnergyAccumulator += DrainRate * DeltaTime;
            const int DrainPoints = static_cast<int>(Unit->CloakEnergyAccumulator);
            if (DrainPoints > 0)
            {
                Unit->CloakEnergyAccumulator -= static_cast<float>(DrainPoints);
                Unit->EnergyPoints -= DrainPoints;
                if (Unit->EnergyPoints <= 0)
                {
                    Unit->EnergyPoints = 0;
                    Cloak->IsCloaked = false;
                    Cloak->CloakEnergyCost = 0.0f;
                }
            }
        }
    }

    // Terran buildings burn down when below 33% HP
    for (int ID : EM.GetEntitiesWith<UnitComponent, BuildingComponent>())
    {
        auto* Unit = EM.GetComponent<UnitComponent>(ID);
        auto* Building = EM.GetComponent<BuildingComponent>(ID);
        if (!Unit || !Building || Building->UnderConstruction || Unit->HitPoints <= 0)
            continue;

        if (Unit->UnitRace == Race::Terran && Unit->HitPoints < (Unit->MaxHitPoints * 33) / 100)
        {
            constexpr float BurnRate = 2.0f;
            Building->BurnDamageAccumulator += BurnRate * DeltaTime;
            const int BurnPoints = static_cast<int>(Building->BurnDamageAccumulator);
            if (BurnPoints > 0)
            {
                Building->BurnDamageAccumulator -= static_cast<float>(BurnPoints);
                Unit->HitPoints -= BurnPoints;
                if (Unit->HitPoints <= 0)
                {
                    Unit->HitPoints = 0;
                    RecordUnitDeath(EM, ID, -1, false);
                    EM.DestroyEntity(ID);
                }
            }
        }
        else
        {
            Building->BurnDamageAccumulator = 0.0f;
        }
    }

    // Combat processing with Spatial Hash acceleration
    static thread_local Game::Pathfinding::NavigationSpatialHash CombatSpatialHash;
    CombatSpatialHash.Clear();
    auto UnitEntities = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
    for (int UID : UnitEntities)
    {
        if (auto* T = EM.GetComponent<TransformComponent>(UID))
            CombatSpatialHash.Insert(UID, T->Position);
    }
    sActiveCombatSpatialHash = &CombatSpatialHash;
    struct CombatSpatialScope
    {
        ~CombatSpatialScope() { sActiveCombatSpatialHash = nullptr; }
    } Scope;

    auto Entities = EM.GetEntitiesWith<TransformComponent, CombatComponent>();
    for (auto ID : Entities)
        ProcessCombat(EM, ID, DeltaTime);
}

void CombatSystem::ProcessCombat(Core::ECS::EntityManager& EM, int EntityID, float DeltaTime)
{
    auto* Combat = EM.GetComponent<CombatComponent>(EntityID);
    auto* Trans = EM.GetComponent<TransformComponent>(EntityID);
    auto* Cmd = EM.GetComponent<CommandComponent>(EntityID);
    auto* Unit = EM.GetComponent<UnitComponent>(EntityID);

    if (!Combat || !Trans) return;
    if (!Unit || Unit->HitPoints <= 0)
    {
        ClearCombatStateForInactiveAttacker(EM, EntityID);
        return;
    }

    const int TransportID = ContainingTransport(EM, EntityID);
    const bool InTransport = (TransportID >= 0);
    const auto* TransportUnit = InTransport ? EM.GetComponent<UnitComponent>(TransportID) : nullptr;
    const bool InBunker = InTransport && TransportUnit && (TransportUnit->ChkUnitID == 125);

    if (InTransport && !InBunker)
    {
        ClearCombatStateForInactiveAttacker(EM, EntityID);
        return;
    }

    // Check if unit is frozen by stasis/lockdown/maelstrom
    if (IsFrozenByStatus(EM, EntityID))
        return;

    if (const auto* Bldg = EM.GetComponent<BuildingComponent>(EntityID))
    {
        if (Bldg->UnderConstruction || (Bldg->RequiresPylon && !Bldg->HasPylonPower))
        {
            ClearCombatStateForInactiveAttacker(EM, EntityID);
            return;
        }
    }

    // Disruption Web prevents ground units and ground defense buildings from attacking
    if (IsUnderDisruptionWeb(EM, EntityID) || (InTransport && IsUnderDisruptionWeb(EM, TransportID)))
        return;

    if (auto* Burrow = EM.GetComponent<BurrowComponent>(EntityID))
    {
        const bool IsLurker = Unit->ChkUnitID == 103;
        const bool FullyBurrowed =
            Burrow->IsBurrowed || Burrow->Phase == BurrowPhase::Burrowed;
        if (Burrow->IsTransitioning() ||
            (FullyBurrowed && !IsLurker) ||
            (!FullyBurrowed && IsLurker))
        {
            return;
        }
    }

    // Reduce cooldown
    if (Combat->CooldownRemaining > 0.0f)
        Combat->CooldownRemaining -= DeltaTime;
    Combat->AutoTargetLockSeconds = std::max(0.0f, Combat->AutoTargetLockSeconds - DeltaTime);

    if (Combat->PendingAttackFrame)
    {
        Combat->PendingAttackElapsed += DeltaTime;
        if (Combat->PendingAttackElapsed >= 0.8f)
            ResolvePendingAttackFrame(EM, mBulletSystem, EntityID);
        return;
    }

    const float BaseRangePixels = EffectiveAttackRangePixels(EM, EntityID, *Combat);
    const bool HoldPosition = Cmd && Cmd->CurrentCommand == CommandType::HoldPosition;
    const bool ExplicitAttackOrder =
        Cmd && (Cmd->CurrentCommand == CommandType::Attack ||
                Cmd->CurrentCommand == CommandType::AttackMove);

    int TargetID = ExplicitAttackOrder ? Cmd->TargetEntityID : -1;

    // Validate target
    if (TargetID >= 0)
    {
        if (!IsActiveTargetValid(EM, *Combat, TargetID, EntityID))
        {
            ClearCombatTargetForInvalidTarget(EM, EntityID, TargetID);
            TargetID = -1;
        }
    }

    if (Combat->AutoTargetEntityID >= 0 &&
        !IsActiveTargetValid(EM, *Combat, Combat->AutoTargetEntityID, EntityID))
    {
        Combat->AutoTargetEntityID = -1;
        Combat->AutoTargetLockSeconds = 0.0f;
    }

    if (TargetID < 0 && Combat->AutoTargetEntityID >= 0)
    {
        TargetID = Combat->AutoTargetEntityID;
    }

    // Find new target for attack-move
    if (TargetID < 0 && Cmd && Cmd->CurrentCommand == CommandType::AttackMove)
    {
        // Ground units in StarCraft acquire targets up to their sight radius (typically ~7-8 tiles = 224-256 px).
        const float SearchRangePixels = std::max(BaseRangePixels * 2.0f, 224.0f);
        TargetID = FindNearestEnemy(EM, EntityID, SearchRangePixels);
        if (TargetID >= 0)
        {
            const Core::Math::Vec2 AttackMoveDestination = Cmd->TargetPosition;
            Combat->TargetEntityID = TargetID;
            Cmd->TargetEntityID = TargetID;
            if (EM.GetComponent<TransformComponent>(TargetID))
            {
                IssueCombatApproachPreservingCommand(
                    EM, EntityID, TargetID, BaseRangePixels,
                    CommandType::AttackMove, AttackMoveDestination, TargetID);
            }
        }
    }

    if (TargetID < 0)
    {
        const bool PassiveAutoAcquire =
            (HoldPosition && !IsWorkerUnit(Unit)) ||
            (!Cmd && !IsWorkerUnit(Unit)) ||
            (EM.GetComponent<BuildingComponent>(EntityID) != nullptr) ||
            ((Cmd->CurrentCommand == CommandType::Stop ||
              Cmd->CurrentCommand == CommandType::Follow ||
              Cmd->CurrentCommand == CommandType::Patrol) &&
             !IsWorkerUnit(Unit));
        if (PassiveAutoAcquire && Combat->AutoTargetLockSeconds <= 0.0f)
        {
            TargetID = FindNearestEnemy(EM, EntityID, BaseRangePixels);
            if (TargetID >= 0)
                SetAutoTarget(*Combat, TargetID, 0.35f);
        }
    }

    if (TargetID < 0)
    {
        Combat->TargetEntityID = -1;
        if (Cmd && Cmd->CurrentCommand == CommandType::AttackMove)
        {
            auto* Path = EM.GetComponent<PathfindingComponent>(EntityID);
            const bool HasActivePath = Path && Path->PathIndex < (int)Path->Path.size();
            const bool HasRecentFailure = Path && Path->HasRecentMoveFailure && Path->RecentMoveFailureTicks > 0;
            const bool ReachedPartialLimit = Path &&
                Path->LastPathStatus == Game::Pathfinding::PathStatus::Partial &&
                Trans->Position.DistanceTo(Path->ResolvedPathDestination) <= 24.0f;
            const bool IsStalled = Path && (Path->StallTicks > 0 || Path->DestinationStallTicks > 0);

            if (!HasActivePath && !HasRecentFailure && !ReachedPartialLimit && !IsStalled &&
                Trans->Position.DistanceTo(Cmd->TargetPosition) > 8.0f)
            {
                IssueMovePreservingCommand(EM, EntityID, Cmd->TargetPosition,
                                           CommandType::AttackMove, Cmd->TargetPosition, -1);
                if (Path)
                {
                    Path->RecentMoveFailureTicks = 12;
                    Path->RecentMoveFailureCooldown = 0.5f;
                }
            }
        }
        return;
    }
    Combat->TargetEntityID = TargetID;

    auto* TargetTrans = EM.GetComponent<TransformComponent>(TargetID);
    if (!TargetTrans) return;

    // Check range
    const float RangePixels = EffectiveAttackRangePixels(EM, EntityID, *Combat, TargetID);
    float Dist = CombatEdgeDistance(EM, EntityID, TargetID);

    if (Dist > RangePixels)
    {
        if (HoldPosition || InBunker || !CanChaseTargetTemporarily(Cmd, EM.GetComponent<BuildingComponent>(EntityID)))
            return;

        const CommandType PreserveCommand = Cmd ? Cmd->CurrentCommand : CommandType::Attack;
        const Core::Math::Vec2 PreserveTarget = Cmd ? Cmd->TargetPosition : TargetTrans->Position;
        const int PreserveTargetEntity = Cmd ? Cmd->TargetEntityID : TargetID;
        IssueCombatApproachPreservingCommand(
            EM, EntityID, TargetID, RangePixels,
            PreserveCommand, PreserveTarget, PreserveTargetEntity);
        return;
    }

    if (Combat->MinimumAttackRange > 0.0f &&
        Dist < Combat->MinimumAttackRange * 32.0f)
    {
        // Siege weapons do not automatically back away from targets inside
        // their dead zone. An explicit focus-fire order remains selected,
        // while automatic acquisition releases the unusable target so an
        // enemy in the valid range ring can be selected on the next update.
        const bool DirectFocusFire =
            Cmd && Cmd->CurrentCommand == CommandType::Attack;
        if (!DirectFocusFire)
        {
            Combat->TargetEntityID = -1;
            Combat->AutoTargetEntityID = -1;
            Combat->AutoTargetLockSeconds = 0.0f;
            if (Cmd && Cmd->CurrentCommand == CommandType::AttackMove)
                Cmd->TargetEntityID = -1;
        }
        return;
    }

    // In range - stop and fire
    auto* PathComp = EM.GetComponent<PathfindingComponent>(EntityID);
    if (PathComp) { PathComp->Path.clear(); PathComp->PathIndex = 0; }

    if (Combat->CooldownRemaining <= 0.0f)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetID);

        const bool TargetIsAir = IsAirTarget(EM, TargetID);
        int Damage = PickWeaponDamage(*Combat, TargetUnit, TargetIsAir);
        if (Damage <= 0) return;

        const int UpgradeBonus = UpgradeSystem::GetWeaponBonusForTarget(
            EM, EntityID, TargetIsAir);
        const int HitCount = PickWeaponHitCount(
            *Combat, Unit, EM, EntityID, TargetIsAir);
        if (HitCount <= 0 ||
            (Unit->ChkUnitID == 83 && Unit->HangarCount == 0))
        {
            return;
        }

        Combat->CooldownRemaining = ModifiedAttackCooldownSeconds(
            EM, EntityID, PickWeaponCooldown(*Combat, TargetIsAir));

        const bool MissedAtLaunch = RollMissChance(
            *Combat, *Trans, *TargetTrans, RangePixels, EM,
            EntityID, TargetID);
        const bool AttackStarted = BeginOrResolveAttack(
            EM, EntityID, TargetID, *TargetTrans,
            Damage, UpgradeBonus, HitCount, RangePixels, MissedAtLaunch);
        if (AttackStarted && Unit->ChkUnitID == 83 && Unit->HangarCount > 0)
            --Unit->HangarCount;
    }
}

} // namespace Game::Systems
