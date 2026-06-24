#pragma once

#include <cstdint>
#include <vector>

#include "Render/sokol/Color.h"
#include "Core/Math/Vec2.h"

namespace Game::Components
{

using Render::Color;

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
    Train,
    Upgrade,
    UseAbility,
    Follow
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
    int FrameIndex = 0;
    Render::Color Tint = Render::Colors::White();
    int TotalFrames = 1;
    int ZOrder = 0;
    bool Visible = true;
    bool Flipped = false;
    RenderLayer Layer = RenderLayer::Units;
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
    int EnergyPoints = 0;
    int MaxEnergyPoints = 0;
    int Armor = 0;
    float Speed = 2.0f;
    int SightRange = 8;
    int SupplyCost = 1;
};

struct PlayerComponent
{
    int PlayerID = 0;
    Color PlayerColor = Render::Colors::Blue();
};

struct CommandComponent
{
    std::vector<CommandType> CommandQueue;
    CommandType CurrentCommand = CommandType::Stop;
    Core::Math::Vec2 TargetPosition;
    int TargetEntityID = -1;
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
};

struct BuildingComponent
{
    bool IsBuilding = true;
    float BuildProgress = 0.0f;
    float TotalBuildTime = 60.0f;
    bool IsAddon = false;
    bool CanLiftOff = false;
    bool IsLifted = false;
    bool RequiresCreep = false;
    bool RequiresPylon = false;
    bool HasPylonPower = false;
    bool UnderConstruction = true;
    bool IsOrganic = false;      // Zerg organic building (spreads creep)
    int BuilderUnitID = -1;
    int AddonID = -1;
    int BuildingDefID = -1;
    int LarvaCount = 0;          // Zerg Hatchery larvae count
    float LarvaTimer = 0.0f;     // Larva spawn timer
};

struct ResourceComponent
{
    ResourceType Type = ResourceType::None;
    int Amount = 0;
    int MaxAmount = 0;
    bool IsDepleted = false;
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
    int ReturnBuildingID = -1;
};

struct PathfindingComponent
{
    std::vector<Core::Math::Vec2> Path;
    int PathIndex = 0;
    float NodeTolerance = 4.0f;
};

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

} // namespace Game::Components
