#include "Game/Systems/CombatSystem.h"

#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Core/ECS/SystemManager.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                              const std::vector<Core::ECS::EntityID>& Entities,
                              const Core::Math::Vec2& Target)
{
    for (Core::ECS::EntityID EntityID : Entities)
    {
        auto* Cmd = EM.GetComponent<Components::CommandComponent>(EntityID);
        if (!Cmd)
            Cmd = &EM.AddComponent<Components::CommandComponent>(EntityID);
        Cmd->CurrentCommand = Components::CommandType::Move;
        Cmd->TargetPosition = Target;

        auto* Path = EM.GetComponent<Components::PathfindingComponent>(EntityID);
        if (!Path)
            Path = &EM.AddComponent<Components::PathfindingComponent>(EntityID);
        Path->Path.clear();
        Path->Path.push_back(Target);
        Path->PathIndex = 0;
    }
}

void CommandSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                    const std::vector<int>& Units,
                                    Core::Math::Vec2 Target)
{
    for (int EntityID : Units)
    {
        auto* Cmd = EM.GetComponent<Components::CommandComponent>(EntityID);
        if (!Cmd)
            Cmd = &EM.AddComponent<Components::CommandComponent>(EntityID);
        Cmd->CurrentCommand = Components::CommandType::AttackMove;
        Cmd->TargetPosition = Target;
        Cmd->TargetEntityID = -1;
    }
}

bool AnimationSystem::HasIScriptAnimationForImage(int, int)
{
    return false;
}

int UpgradeSystem::GetWeaponBonus(const Core::ECS::EntityManager&, int)
{
    return 0;
}

int UpgradeSystem::GetArmorBonus(const Core::ECS::EntityManager&, int)
{
    return 0;
}

void BulletSystem::Update(float)
{
}

int BulletSystem::SpawnBullet(Core::ECS::EntityManager&,
                              const Components::CombatComponent&,
                              const Components::TransformComponent&,
                              const Components::PlayerComponent&,
                              int,
                              int,
                              int,
                              float,
                              int,
                              const Core::Math::Vec2&)
{
    return -1;
}

} // namespace Game::Systems

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

    int SpawnUnit(Core::ECS::EntityManager& EM,
                  Core::Math::Vec2 Pos,
                  int PlayerID,
                  bool Air)
    {
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Pos;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.HitPoints = 40;
        Unit.MaxHitPoints = 40;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
        Path.IsAir = Air;
        if (Air)
        {
            auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
            Combat.MyElevation = Game::Components::Elevation::Air;
        }
        return EntityID;
    }

    void AddGroundOnlyWeapon(Core::ECS::EntityManager& EM, int EntityID)
    {
        auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
        Combat.GroundDamage = 6;
        Combat.AirDamage = 0;
        Combat.AttackRange = 4.0f;
        Combat.AttackCooldown = 0.1f;
        Combat.CanTargetGround = true;
        Combat.CanTargetAir = false;
        Combat.WeaponBeh = Game::Components::WeaponBehavior::Melee;
    }
}

int main()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();
    Game::Systems::BulletSystem Bullet(SysMgr);
    Game::Systems::CombatSystem Combat(SysMgr, Bullet);

    int Marine = SpawnUnit(EM, {0.0f, 0.0f}, 0, false);
    AddGroundOnlyWeapon(EM, Marine);
    auto& MarineCmd = EM.AddComponent<Game::Components::CommandComponent>(Marine);

    int AirTarget = SpawnUnit(EM, {32.0f, 0.0f}, 1, true);
    Game::Systems::CombatSystem::IssueAttack(EM, {Marine}, AirTarget);
    Combat.Update(0.1f);
    Require(EM.GetComponent<Game::Components::UnitComponent>(AirTarget)->HitPoints == 40,
        "ground-only attacker does not damage air target on direct attack");
    Require(EM.GetComponent<Game::Components::CombatComponent>(Marine)->TargetEntityID == -1,
        "ground-only attacker clears invalid air target");
    Require(MarineCmd.CurrentCommand == Game::Components::CommandType::Stop,
        "direct invalid air attack stops instead of chasing forever");

    Game::Systems::CombatSystem::IssueAttackMove(EM, {Marine}, {96.0f, 0.0f});
    Combat.Update(0.1f);
    Require(MarineCmd.TargetEntityID == -1,
        "attack-move does not acquire air target for ground-only attacker");
    Require(EM.GetComponent<Game::Components::UnitComponent>(AirTarget)->HitPoints == 40,
        "attack-move leaves air target undamaged");

    int GroundTarget = SpawnUnit(EM, {32.0f, 0.0f}, 1, false);
    Combat.Update(0.2f);
    Require(MarineCmd.TargetEntityID == GroundTarget,
        "attack-move acquires reachable ground target");
    Require(EM.GetComponent<Game::Components::UnitComponent>(GroundTarget)->HitPoints < 40,
        "ground-only attacker damages ground target");

    int Geyser = SpawnUnit(EM, {16.0f, 64.0f}, 1, false);
    auto& GeyserRes = EM.AddComponent<Game::Components::ResourceComponent>(Geyser);
    GeyserRes.Type = Game::Components::ResourceType::VespeneGas;
    GeyserRes.Amount = 5000;
    GeyserRes.MaxAmount = 5000;
    auto* GeyserUnit = EM.GetComponent<Game::Components::UnitComponent>(Geyser);
    GeyserUnit->HitPoints = 5;
    GeyserUnit->MaxHitPoints = 5;

    Game::Systems::CombatSystem::IssueAttack(EM, {Marine}, Geyser);
    Combat.Update(0.2f);
    Require(EM.GetComponent<Game::Components::ResourceComponent>(Geyser) != nullptr,
        "vespene geyser resource remains in the world after attack order");
    Require(EM.GetComponent<Game::Components::UnitComponent>(Geyser)->HitPoints == 5,
        "ordinary combat does not damage resource nodes");
    Require(EM.GetComponent<Game::Components::CombatComponent>(Marine)->TargetEntityID == -1,
        "direct attack order does not keep resource nodes as combat targets");
    Require(MarineCmd.CurrentCommand == Game::Components::CommandType::Stop,
        "direct resource attack stops instead of chasing a harvest-only target");

    Core::ECS::SystemManager ResourceSysMgr;
    auto& ResourceEM = ResourceSysMgr.GetEntityManager();
    Game::Systems::BulletSystem ResourceBullet(ResourceSysMgr);
    Game::Systems::CombatSystem ResourceCombat(ResourceSysMgr, ResourceBullet);
    int ResourceMarine = SpawnUnit(ResourceEM, {0.0f, 0.0f}, 0, false);
    AddGroundOnlyWeapon(ResourceEM, ResourceMarine);
    auto& ResourceMarineCmd =
        ResourceEM.AddComponent<Game::Components::CommandComponent>(ResourceMarine);
    int NearbyGeyser = SpawnUnit(ResourceEM, {32.0f, 0.0f}, 1, false);
    auto& NearbyGeyserRes =
        ResourceEM.AddComponent<Game::Components::ResourceComponent>(NearbyGeyser);
    NearbyGeyserRes.Type = Game::Components::ResourceType::VespeneGas;
    NearbyGeyserRes.Amount = 5000;
    NearbyGeyserRes.MaxAmount = 5000;
    Game::Systems::CombatSystem::IssueAttackMove(ResourceEM, {ResourceMarine}, {96.0f, 0.0f});
    ResourceCombat.Update(0.2f);
    Require(ResourceMarineCmd.TargetEntityID == -1,
        "attack-move does not acquire resource nodes as enemies");

    Core::ECS::SystemManager DamageTypeSysMgr;
    auto& DamageTypeEM = DamageTypeSysMgr.GetEntityManager();
    int AntiAirUnit = SpawnUnit(DamageTypeEM, {0.0f, 0.0f}, 0, false);
    auto& AntiAirCombat =
        DamageTypeEM.AddComponent<Game::Components::CombatComponent>(AntiAirUnit);
    AntiAirCombat.AirDmgType = Game::Components::DamageType::Concussive;
    AntiAirCombat.GroundDmgType = Game::Components::DamageType::Explosive;
    int LargeAirTarget = SpawnUnit(DamageTypeEM, {32.0f, 0.0f}, 1, true);
    auto* LargeAirUnit =
        DamageTypeEM.GetComponent<Game::Components::UnitComponent>(LargeAirTarget);
    LargeAirUnit->SizeType = Game::Components::UnitSize::Large;
    LargeAirUnit->HitPoints = 100;
    LargeAirUnit->MaxHitPoints = 100;
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, LargeAirTarget, AntiAirCombat, AntiAirUnit, 40);
    Require(LargeAirUnit->HitPoints == 90,
        "damage against air targets uses AirDmgType size modifiers");

    Core::ECS::SystemManager ShieldSysMgr;
    auto& ShieldEM = ShieldSysMgr.GetEntityManager();
    Game::Systems::BulletSystem ShieldBullet(ShieldSysMgr);
    Game::Systems::CombatSystem ShieldCombat(ShieldSysMgr, ShieldBullet);
    int ShieldedUnit = SpawnUnit(ShieldEM, {0.0f, 0.0f}, 0, false);
    auto* ShieldedUnitData =
        ShieldEM.GetComponent<Game::Components::UnitComponent>(ShieldedUnit);
    ShieldedUnitData->ShieldPoints = 0;
    ShieldedUnitData->MaxShieldPoints = 5;
    for (int I = 0; I < 60; ++I)
        ShieldCombat.Update(1.0f / 60.0f);
    Require(ShieldedUnitData->ShieldPoints >= 1,
        "shields regenerate from zero using accumulated frame deltas");

    std::printf("CombatRulesTest passed\n");
    return 0;
}
