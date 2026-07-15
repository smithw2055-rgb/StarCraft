#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Core/Color.h"
#include "Core/Math/Vec2.h"
#include "Core/Math/FixedPoint.h"
#include "Game/DataEnums.h"
#include "Game/IScriptVM.h"

namespace Game::Components
{

using Core::Color;

enum class Race : uint8_t
{
    Terran,
    Zerg,
    Protoss
};

enum class UnitSize : uint8_t
{
    Small,
    Medium,
    Large
};

enum class ResourceType : uint8_t
{
    None,
    Minerals,
    VespeneGas
};

enum class HarvestState : uint8_t
{
    Idle,
    QueuedAtResource,
    MovingToResource,
    Mining,
    MovingToDepot,
    Depositing
};

enum class DamageType : uint8_t
{
    Normal,      // 100% vs all sizes
    Concussive,  // 100% vs Small, 50% vs Medium, 25% vs Large
    Explosive    // 50% vs Small, 75% vs Medium, 100% vs Large
};

enum class SplashType : uint8_t
{
    None,
    Radial,   // circular area around target
    Line,     // line area from attacker through target
    Air       // radial but only hits air units
};

enum class WeaponBehavior : uint8_t
{
    FlyDirect,      // 0: normal projectile, fly to target, damage on impact
    AppearOnTarget, // 2: appear on target unit/site (instant)
    Lob,            // 5: lobbed projectile (no tracking)
    Melee,          // 7: melee instant hit
    Persist,        // 8: stay near target and deal damage over time
    Missile         // 11: fly and track target
};

enum class StatusEffect : uint8_t
{
    None            = 0,
    Lockdown        = 1,   // mechanical unit frozen
    Ensnare         = 2,   // slowed
    Plague          = 3,   // HP drain to 1
    Stasis          = 4,   // frozen, invulnerable
    Maelstrom       = 5,   // organic unit frozen
    Irradiate       = 6,   // AoE damage over time (organic only)
    Blind           = 7,   // vision reduced
    Parasite        = 8,   // shared vision for caster
    Consume         = 9,   // being consumed (dying)
    SpawnBroodling  = 10,  // unit dying, broods spawn
    EMP             = 11,  // shields drained, energy drained
    DisruptionWeb   = 12,  // ground units inside cannot fire
    DarkSwarm       = 13,  // ground units inside immune to ranged
    Storm           = 14,  // psionic storm DoT (special, not on unit but area)
    Hallucination   = 15,  // blue-white visual duplicate
    Stim            = 16,  // terran infantry combat stimulant
};

enum class Elevation : uint8_t
{
    Ground,
    Air,
    Both
};

enum class CommandType : uint8_t
{
    Move,
    AttackMove,
    Attack,
    Patrol,
    HoldPosition,
    Stop,
    Harvest,
    Build,
    Repair,
    Train,
    Upgrade,
    UseAbility,
    Follow
};

enum class OrderType : uint8_t
{
    None = 0,
    Move,
    AttackMove,
    Attack,
    AttackTile,
    Patrol,
    HoldPosition,
    Guard,
    Stop,
    Repair,
    Build,
    PlaceBuilding,
    Train,
    Cancel,
    Research,
    Upgrade,
    UseTech,
    LiftOff,
    Land,
    Load,
    Unload,
    Burrow,
    Unburrow,
    Cloak,
    Decloak,
    Morph,
    WarpIn,
    ReturnCargo,
    Gather,
    Follow,
    BuildAddon
};

struct Order
{
    OrderType Type = OrderType::None;
    Core::Math::Vec2 TargetPosition;
    int TargetEntityID = -1;
    int PathingIgnoreEntityID = -1;
    int AbilityID = -1;

    bool IsValid() const { return Type != OrderType::None; }
};

enum class RenderLayer : uint8_t
{
    Terrain = 0,
    GroundDecor,
    Creep,
    Buildings,
    Units,
    Effects,
    AirUnits,
    UIOverlay,
    MAX_LAYERS
};

enum class TerrainType : uint8_t
{
    Floor,
    Grass,
    Dirt,
    Rock,
    Water,
    Lava,
    HighGrass,
    LowRock,
    HighRock,
    Ruins,
    Floor2,
    Floor3,
    Mud,
    Ash,
    Sand
};

struct TransformComponent
{
    Core::Math::Vec2 Position;
    Core::Math::Vec2 Scale = {1.0f, 1.0f};
    float Rotation = 0.0f;
};

struct RenderComponent
{
    uint32_t ImageID = 0;
    uint32_t SubunitImageID = UINT32_MAX;
    // Modifier 12 is only active during the short Protoss materialization
    // phase. The building GRP supplies the silhouette while texture.grp
    // supplies the indexed color surface selected by the warp IScript.
    bool UseProtossWarpTexture = false;
    int ProtossWarpTextureFrame = 0;
    // A completed Terran add-on owns a separate bridge sprite.  It is not a
    // units.dat subunit (turrets use SubunitImageID), so keep its lifecycle
    // independent and render it underneath the add-on body.
    uint32_t AddonConnectorImageID = UINT32_MAX;
    // Root-image offset authored by IScript sethorpos/setvertpos. This is a
    // presentation-only displacement: airborne building shadows remain at
    // the logical ground position while the body and landing gear move.
    Core::Math::Vec2 VisualOffset;
    int FrameIndex = 0;
    Core::Color Tint = Core::Colors::White();
    int TotalFrames = 1;
    int ZOrder = 0;
    bool Visible = true;
    bool Flipped = false;
    RenderLayer Layer = RenderLayer::Units;
};

struct EffectComponent
{
    float LifetimeRemaining = 0.75f;
    int ParentEntityID = -1;
    Core::Math::Vec2 Offset;
    bool FollowParent = false;
};

struct ImageSubnode
{
    uint32_t ImageID = 0;
    int ParentIndex = -1;
    int FrameIndex = 0;
    int TotalFrames = 1;
    Core::Math::Vec2 Offset;
    Core::Math::Vec2 CreationOffset;
    bool DrawAbove = true;
    bool Visible = true;
    bool Flipped = false;
    // Directional images belong to the same sprite as the root image. StarCraft
    // updates every directional image (including air-unit shadows) when the
    // sprite heading changes.
    bool InheritSpriteDirection = false;
    // Set after this image executes followmaingraphic. Its VM periodically
    // copies the main image frame, whose flip state is already absolute.
    bool FollowsMainGraphic = false;
    bool PersistentSpriteAttachment = false;
    bool UseSpecialOffset = false;
    bool IsDamageOverlay = false;
    int OverlayOffsetType = -1;
    int OverlayOffsetIndex = 0;
    bool UseSpriteID = false;
    // Runtime-only modulation for authored child images.  The Command Center's
    // canonical production control image is a single static frame, so a
    // subtle alpha pulse supplies the continuous activity cue without cycling
    // into unrelated GRP frames.
    float Opacity = 1.0f;
    float RuntimeVisualPhase = 0.0f;
    // OpenBW attaches carried mineral/gas graphics to the worker sprite as a
    // persistent image with a special LO offset. Mark that runtime-owned node
    // so it can be updated or removed without disturbing IScript overlays.
    bool IsCarriedResourceOverlay = false;
    // units.dat Subunit1 (Goliath head, Tank turret, Siege Tank turret) is a
    // real independently-scripted image, not a copy of the body frame.
    bool IsUnitSubunit = false;
    // Terran add-on bridges are real sprite images with their own Init,
    // Working, LiftOff, and Landing scripts. Keep the runtime-owned bridge in
    // the image tree so animation state changes reach it like every other
    // image attached to the sprite.
    bool IsAddonConnectorOverlay = false;
    float LifetimeRemaining = -1.0f; // negative means persistent until animation changes.
    int ScriptID = -1;
    int AnimID = -1;
    float StepAccumulator = 0.0f;
    Game::IScriptVM VM;
};

struct ImageTreeComponent
{
    std::vector<ImageSubnode> Nodes;
};

struct LarvaComponent
{
    int ParentHatcheryEntityID = -1;
};

struct UnitComponent
{
    int ChkUnitID = 0;
    Race UnitRace = Race::Terran;
    UnitSize SizeType = UnitSize::Medium;
    int HitPoints = 100;
    int MaxHitPoints = 100;
    int ShieldPoints = 0;
    int MaxShieldPoints = 0;
    float ShieldRegenAccumulator = 0.0f;
    float PlagueDamageAccumulator = 0.0f;
    float IrradiateDamageAccumulator = 0.0f;
    int EnergyPoints = 0;
    int MaxEnergyPoints = 0;
    int Armor = 0;
    float Speed = 2.0f;
    int SightRange = 8;
    int SupplyCost = 1;
    int HangarCount = 0;
    bool IsInvincible = false;
    bool IsDetector = false;
};

struct PlayerComponent
{
    int PlayerID = 0;
    Color PlayerColor = Core::Colors::Blue();
    // Rescue Passive is a scenario ownership mode, not a hostile faction.
    // Keep it on the entity so gameplay systems can preserve campaign allies
    // without needing to reach back into the CHK parser at runtime.
    bool IsRescuePassive = false;
};

struct DeathRecordComponent
{
    int VictimPlayerID = -1;
    int VictimUnitID = -1;
    int KillerPlayerID = -1;
    int KillerEntityID = -1;
    bool AwardKill = false;
    bool CountedByTriggers = false;
};

struct CommandComponent
{
    std::vector<CommandType> CommandQueue; // legacy, deprecated
    CommandType CurrentCommand = CommandType::Stop;
    Core::Math::Vec2 TargetPosition;
    int TargetEntityID = -1;

    // Rich order queue (replaces the above over time)
    std::vector<Order> Orders;
    int CurrentOrderIndex = -1;
    Core::Math::Vec2 RallyPoint;
    bool HasRallyPoint = false;

    void QueueOrder(const Order& InOrder)
    {
        // If no current order, set it immediately
        if (CurrentOrderIndex < 0 && Orders.empty())
        {
            Orders.push_back(InOrder);
            CurrentOrderIndex = 0;
            ApplyOrder(InOrder);
        }
        else
        {
            Orders.push_back(InOrder);
        }
    }

    void AdvanceQueue()
    {
        size_t Next = (size_t)(CurrentOrderIndex + 1);
        if (Next < Orders.size())
        {
            CurrentOrderIndex = (int)Next;
            ApplyOrder(Orders[Next]);
        }
        else
        {
            CurrentOrderIndex = (int)Orders.size();
        }
    }

    void CancelOrder(int Index)
    {
        if (Index < 0 || Index >= (int)Orders.size()) return;
        Orders.erase(Orders.begin() + Index);
        if (Index < CurrentOrderIndex)
            CurrentOrderIndex--;
        else if (Index == CurrentOrderIndex && Index < (int)Orders.size())
            ApplyOrder(Orders[Index]);
        else if (CurrentOrderIndex >= (int)Orders.size())
            CurrentOrderIndex = (int)Orders.size() - 1;
    }

    void CancelAll()
    {
        Orders.clear();
        CurrentOrderIndex = -1;
        CurrentCommand = CommandType::Stop;
    }

    bool HasPendingOrders() const
    {
        return CurrentOrderIndex >= 0 && CurrentOrderIndex < (int)Orders.size();
    }

    const Order* GetCurrentOrder() const
    {
        if (CurrentOrderIndex >= 0 && CurrentOrderIndex < (int)Orders.size())
            return &Orders[CurrentOrderIndex];
        return nullptr;
    }

private:
    void ApplyOrder(const Order& InOrder)
    {
        TargetPosition = InOrder.TargetPosition;
        TargetEntityID = InOrder.TargetEntityID;
        switch (InOrder.Type)
        {
            case OrderType::Move:        CurrentCommand = CommandType::Move;        break;
            case OrderType::AttackMove:  CurrentCommand = CommandType::AttackMove;  break;
            case OrderType::Attack:      CurrentCommand = CommandType::Attack;      break;
            case OrderType::Patrol:      CurrentCommand = CommandType::Patrol;      break;
            case OrderType::HoldPosition:CurrentCommand = CommandType::HoldPosition;break;
            case OrderType::Stop:        CurrentCommand = CommandType::Stop;        break;
            case OrderType::Gather: case OrderType::ReturnCargo:
                                         CurrentCommand = CommandType::Harvest;     break;
            case OrderType::Build: case OrderType::PlaceBuilding: case OrderType::BuildAddon:
                                         CurrentCommand = CommandType::Build;       break;
            case OrderType::Repair:      CurrentCommand = CommandType::Repair;      break;
            case OrderType::Train:       CurrentCommand = CommandType::Train;       break;
            case OrderType::Research: case OrderType::Upgrade:
                                         CurrentCommand = CommandType::Upgrade;     break;
            case OrderType::UseTech:     CurrentCommand = CommandType::UseAbility;  break;
            case OrderType::Follow:      CurrentCommand = CommandType::Follow;      break;
            default:                     CurrentCommand = CommandType::Stop;        break;
        }
    }
};

struct CombatComponent
{
    int GroundDamage = 0;
    int AirDamage = 0;
    DamageType GroundDmgType = DamageType::Normal;
    DamageType AirDmgType = DamageType::Normal;
    float AttackRange = 1.0f;
    float AttackCooldown = 1.0f;
    float CooldownRemaining = 0.0f;
    int TargetEntityID = -1;
    WeaponBehavior WeaponBeh = WeaponBehavior::Melee;
    SplashType Splash = SplashType::None;
    float SplashRadius = 0.0f;
    Elevation MyElevation = Elevation::Ground;
    bool CanTargetAir = false;
    bool CanTargetGround = true;
    bool IsRanged = false;  // false = melee / true = ranged (affects DarkSwarm immunity)
    bool PendingAttackFrame = false;
    int PendingAttackTargetEntityID = -1;
    Core::Math::Vec2 PendingAttackTargetPos;
    int PendingAttackDamage = 0;
    int PendingAttackUpgradeBonus = 0;
    float PendingAttackRangePixels = 0.0f;
    float PendingAttackElapsed = 0.0f;
    int AutoTargetEntityID = -1;
    int LastDamagerEntityID = -1;
    float AutoTargetLockSeconds = 0.0f;
};

struct SiegeTransitionComponent
{
    bool Active = false;
    bool TargetSieged = false;
    // Stage 0 aligns/starts the authored transition. Stage 1 is the sieged
    // hull Init animation, which lowers the tank and emits sigorder 1.
    int Stage = 0;
    float Elapsed = 0.0f;
    bool AnimationComplete = false;
    int TargetChkUnitID = -1;
    float TargetSpeed = 0.0f;
    uint32_t TargetImageID = UINT32_MAX;
    uint32_t TargetSubunitImageID = UINT32_MAX;
    CombatComponent TargetCombat;
};

enum class BuildingFlightPhase : uint8_t
{
    Grounded,
    LiftingOff,
    Airborne,
    Landing
};

enum class ProtossConstructionPhase : uint8_t
{
    None,
    Rift,
    RiftClosing,
    Materializing,
    WarpIn
};

struct BuildingComponent
{
    bool IsBuilding = true;
    float BuildProgress = 0.0f;
    float TotalBuildTime = 60.0f;
    bool IsAddon = false;
    bool CanLiftOff = false;
    bool IsLifted = false;
    BuildingFlightPhase FlightPhase = BuildingFlightPhase::Grounded;
    bool FlightAnimationComplete = false;
    // Lift/land has two authored visual stages. The IScript retracts/deploys
    // the landing gear; this runtime stage moves only the building body while
    // its ground shadow remains anchored at the logical position.
    bool FlightHeightTransitionActive = false;
    float FlightHeightTransitionElapsed = 0.0f;
    float FlightVisualHeight = 0.0f;
    bool RequiresCreep = false;
    bool RequiresPylon = false;
    bool HasPylonPower = false;
    bool UnderConstruction = true;
    ProtossConstructionPhase ProtossPhase = ProtossConstructionPhase::None;
    bool ProtossPhaseSignal = false;
    float ProtossPhaseElapsed = 0.0f;
    bool IsOrganic = false;      // Zerg organic building (spreads creep)
    int BuilderUnitID = -1;
    std::vector<int> AssistantBuilderUnitIDs;
    int AddonID = -1;
    int BuildingDefID = -1;
    int LarvaCount = 0;          // Zerg Hatchery larvae count
    float LarvaTimer = 0.0f;     // Larva spawn timer
    float BuildTimeRemaining = 0.0f; // time remaining for build/research
    int ResearchingID = -1;          // ResearchId being researched (-1 = none)
    bool IsResearching = false;      // true if currently researching
    int ResearchPlayer = -1;         // player conducting the research

    bool HasBuilderUnit(int EntityID) const
    {
        return BuilderUnitID == EntityID ||
               std::find(AssistantBuilderUnitIDs.begin(),
                         AssistantBuilderUnitIDs.end(), EntityID) !=
                   AssistantBuilderUnitIDs.end();
    }
};

struct ResourceComponent
{
    ResourceType Type = ResourceType::None;
    int Amount = 0;
    int MaxAmount = 0;
    bool IsDepleted = false;
    int ActiveGathererEntityID = -1;
    std::vector<int> QueuedGatherers;
};

struct HarvestComponent
{
    HarvestState State = HarvestState::Idle;
    ResourceType CarryingType = ResourceType::None;
    int CarryingAmount = 0;
    int CarryCapacity = 8;
    float MineTimer = 0.0f;
    float MineInterval = 1.0f;
    int TargetResourceID = -1;
    int DesiredResourceID = -1;
    int ReturnBuildingID = -1;
    bool QueuedAtResource = false;
    // OpenBW hides a worker while HarvestGas is active, then restores it at a
    // refinery edge facing the nearest depot. Keep the exit point so an
    // interrupted order can also release the worker safely.
    bool InsideGasBuilding = false;
    Core::Math::Vec2 GasExitPosition;
};

struct PathfindingComponent
{
    std::vector<Core::Math::Vec2> Path;
    int PathIndex = 0;
    float NodeTolerance = 4.0f;
    Core::Math::Vec2 RequestedDestination;
    int RequestedTargetEntityID = -1;
    int RequestedIgnoreEntityID = -1;
    int RepathFailCount = 0;
    float LastRepathTime = 0.0f;
    int BlockedByEntityID = -1;

    // Movement state (OpenBW alignment)
    Core::Math::Vec2 CurrentVelocity;
    float CurrentSpeed = 0.0f;      // actual current speed (pixels/sec)
    float TargetSpeed = 0.0f;       // desired top speed (from unit def)
    float Acceleration = 200.0f;    // pixels/sec^2
    float TurnRate = 8.0f;          // radians/sec
    float FacingAngle = 0.0f;       // current facing in radians
    Core::Math::Vec2 RepulseVector; // accumulated repulse from neighbors
    Core::Math::Vec2 FriendlyYieldVector; // soft nudge requested by an allied mover
    Core::Math::Vec2 FriendlyYieldAnchor;
    bool HasFriendlyYieldAnchor = false;

    // Air / ground flag
    bool IsAir = false;
    int CollisionRadius = 16;       // tile pixels for collision
    bool HasCollisionBounds = false;
    int CollisionLeft = 16;
    int CollisionUp = 16;
    int CollisionRight = 16;
    int CollisionDown = 16;

    // OpenBW-style retry hook: movement marks a final target as failed when it
    // stops making progress, and higher-level orders can try another edge.
    bool HasRecentMoveFailure = false;
    Core::Math::Vec2 RecentFailedMoveTarget;
    float RecentMoveFailureCooldown = 0.0f;
    float StallTimer = 0.0f;
    float LastWaypointDistance = 0.0f;
    bool HasLastWaypointDistance = false;
    float DestinationStallTimer = 0.0f;
    float LastDestinationDistance = 0.0f;
    bool HasLastDestinationDistance = false;
};

// Gas buildings reuse the neutral geyser entity. Preserve its original ECS
// presentation so destroying a Refinery/Extractor/Assimilator can reveal the
// same geyser again instead of deleting the resource or leaving the building
// permanently invulnerable.
struct ClaimedGeyserComponent
{
    bool IsClaimed = false;
    UnitComponent OriginalUnit;
    PlayerComponent OriginalPlayer;
    RenderComponent OriginalRender;
    BuildingComponent OriginalBuilding;
    PathfindingComponent OriginalPathfinding;
    bool HadPathfinding = false;
};

struct UnitCollisionBounds
{
    bool IsValid = false;
    int Left = 16;
    int Up = 16;
    int Right = 16;
    int Down = 16;
};

inline void ApplyCollisionBounds(PathfindingComponent& Path, const UnitCollisionBounds& Bounds)
{
    if (!Bounds.IsValid)
        return;

    Path.HasCollisionBounds = true;
    Path.CollisionLeft = std::max(0, Bounds.Left);
    Path.CollisionUp = std::max(0, Bounds.Up);
    Path.CollisionRight = std::max(0, Bounds.Right);
    Path.CollisionDown = std::max(0, Bounds.Down);
    const int Width = Path.CollisionLeft + Path.CollisionRight + 1;
    const int Height = Path.CollisionUp + Path.CollisionDown + 1;
    Path.CollisionRadius = std::max(4, std::max(Width, Height) / 2);
}

struct ProductionComponent
{
    std::vector<int> Queue;
    float TrainingProgress = 0.0f;
    float TrainingTimePerUnit = 0.0f;
    int CurrentTrainingUnitDefID = -1;
};

struct VisionComponent
{
    bool VisibleToPlayer[8] = {};
    bool HasBeenSeen[8] = {};
};

struct RevealAreaComponent
{
    int PlayerID = 0;
    int SightRange = 1;
    float DurationRemaining = 0.0f;
    bool ProvidesDetection = false;
};

struct BulletComponent
{
    int SourceEntityID = -1;
    WeaponBehavior Behavior = WeaponBehavior::FlyDirect;
    SplashType Splash = SplashType::None;
    float SplashRadius = 0.0f;
    int BaseDamage = 0;
    DamageType DamageKind = DamageType::Normal;
    float Speed = 10.0f;
    float MaxLifetime = 5.0f;
    float LifetimeRemaining = 5.0f;
    Core::Math::Vec2 Velocity;
    Core::Math::Vec2 TargetPos;
    int TargetEntityID = -1;
    bool HasHit = false;
    int DamageUpgradeBonus = 0;
    bool IsRangedAttack = false;
    bool PlayImpactAnimation = false;
};

struct StatusEffectComponent
{
    static constexpr int kMaxEffects = 4;
    StatusEffect ActiveEffects[kMaxEffects] = {};
    float EffectTimers[kMaxEffects] = {};
    float TotalDurations[kMaxEffects] = {};
    int EffectSources[kMaxEffects] = {};

    void AddEffect(StatusEffect Effect, float Duration, int SourceID = -1)
    {
        for (int i = 0; i < kMaxEffects; ++i)
        {
            if (ActiveEffects[i] == Effect)
            {
                EffectTimers[i] = std::max(EffectTimers[i], Duration);
                TotalDurations[i] = std::max(TotalDurations[i], Duration);
                EffectSources[i] = SourceID;
                return;
            }
        }

        for (int i = 0; i < kMaxEffects; ++i)
            if (ActiveEffects[i] == StatusEffect::None)
            {
                ActiveEffects[i] = Effect;
                EffectTimers[i] = Duration;
                TotalDurations[i] = Duration;
                EffectSources[i] = SourceID;
                return;
            }
    }

    bool HasEffect(StatusEffect Effect) const
    {
        for (int i = 0; i < kMaxEffects; ++i)
            if (ActiveEffects[i] == Effect && EffectTimers[i] > 0.0f)
                return true;
        return false;
    }

    void RemoveEffect(StatusEffect Effect)
    {
        for (int i = 0; i < kMaxEffects; ++i)
            if (ActiveEffects[i] == Effect)
            {
                ActiveEffects[i] = StatusEffect::None;
                EffectTimers[i] = 0.0f;
                EffectSources[i] = -1;
            }
    }

    void Tick(float DeltaTime)
    {
        for (int i = 0; i < kMaxEffects; ++i)
            if (ActiveEffects[i] != StatusEffect::None)
            {
                EffectTimers[i] -= DeltaTime;
                if (EffectTimers[i] <= 0.0f)
                {
                    ActiveEffects[i] = StatusEffect::None;
                    EffectTimers[i] = 0.0f;
                    EffectSources[i] = -1;
                }
            }
    }
};

struct CloakComponent
{
    bool IsCloaked = false;
    bool IsPassiveCloak = false;
    bool IsBurrowed = false;
    float CloakEnergyCost = 0.0f;
};

enum class BurrowPhase : uint8_t
{
    Surface,
    Burrowing,
    Burrowed,
    Unburrowing
};

struct BurrowComponent
{
    bool CanBurrow = false;
    bool IsBurrowed = false;
    BurrowPhase Phase = BurrowPhase::Surface;
    float BurrowTransitionTime = 0.0f;

    bool IsTransitioning() const
    {
        return Phase == BurrowPhase::Burrowing ||
               Phase == BurrowPhase::Unburrowing;
    }
};

struct NukeComponent
{
    int GhostEntityID = -1;
    int SiloEntityID = -1;
    Core::Math::Vec2 TargetPosition;
    float ArmTimer = 0.0f;
    float FlightTimer = 0.0f;
    float NukeDotTimer = 0.0f;
    bool Armed = false;
    bool Launched = false;
    bool Detonated = false;
    bool HasNukeDot = false;
    int NukeDotEntityID = -1;
};

struct SpiderMineComponent
{
    int VultureEntityID = -1;
    int MaxMines = 3;
    int CurrentMineCount = 0;
    bool IsPlaced = false;
    bool IsBurrowed = false;
    bool IsTriggered = false;
    float BurrowTimer = 0.0f;
    float TriggerRange = 96.0f;
    float ChaseSpeed = 400.0f;
    int ChasingEntityID = -1;
};

struct MorphComponent
{
    int MorphTargetDefID = -1;    // target unit def ID being morphed into
    float MorphTimer = 0.0f;      // remaining morph time
    float MorphDuration = 0.0f;   // total morph duration
    bool IsMorphing = false;
    int SourceUnitID = -1;        // original entity (for egg->unit tracking)
};

struct SpellCooldown
{
    uint8_t SpellType = 0;
    float CooldownRemaining = 0.0f;
};

struct LoadedUnitsComponent
{
    std::vector<int> PassengerIDs;
};

struct PendingTransportLoadComponent
{
    int PassengerID = -1;
    Core::Math::Vec2 LastIssuedTarget;
    float RepathTimer = 0.0f;
};

struct PendingTransportBoardingComponent
{
    std::vector<int> PassengerIDs;
    int CurrentIndex = 0;
};

struct PendingBoardTransportComponent
{
    int TransportID = -1;
    Core::Math::Vec2 LastIssuedTarget;
    float RepathTimer = 0.0f;
};

struct PendingTransportUnloadComponent
{
    float TimeUntilNextPassenger = 0.0f;
    int SequenceIndex = 0;
    bool Active = false;
};

} // namespace Game::Components
