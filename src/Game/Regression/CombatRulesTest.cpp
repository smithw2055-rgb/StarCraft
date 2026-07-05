#include "Game/Systems/CombatSystem.h"

#include "Game/DataEnums.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Races/RaceDef.h"

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

    int SpawnDefUnit(Core::ECS::EntityManager& EM,
                     const char* DefName,
                     Core::Math::Vec2 Pos,
                     int PlayerID,
                     bool CompletedBuilding = false)
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Pos;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.ShieldPoints = Def.ShieldPoints;
        Unit.MaxShieldPoints = Def.ShieldPoints;
        Unit.Armor = Def.Armor;
        Unit.SupplyCost = Def.SupplyCost;
        Unit.SizeType = Def.SizeType;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
        Path.IsAir = Game::Data::IsAirUnitType(Def.ChkUnitID);
        if (CompletedBuilding)
        {
            auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
            Building.IsBuilding = true;
            Building.UnderConstruction = false;
            Building.BuildingDefID = DefID;
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
    auto* MarinePath = EM.GetComponent<Game::Components::PathfindingComponent>(Marine);
    Require(MarinePath, "marine has a path component for combat motion tests");

    int AirTarget = SpawnUnit(EM, {32.0f, 0.0f}, 1, true);
    Game::Systems::CombatSystem::IssueAttack(EM, {Marine}, AirTarget);
    Require(EM.GetComponent<Game::Components::CombatComponent>(Marine)->TargetEntityID == -1,
        "direct invalid air attack clears combat target immediately at order time");
    Require(MarineCmd.CurrentCommand == Game::Components::CommandType::Stop &&
            MarineCmd.TargetEntityID == -1,
        "direct invalid air attack clears command target immediately at order time");
    Require(MarinePath->Path.empty() &&
            MarinePath->PathIndex == 0 &&
            MarinePath->CurrentSpeed == 0.0f &&
            MarinePath->CurrentVelocity.Length() == 0.0f,
        "direct invalid air attack clears stale chase motion at order time");
    Combat.Update(0.1f);
    Require(EM.GetComponent<Game::Components::UnitComponent>(AirTarget)->HitPoints == 40,
        "ground-only attacker does not damage air target on direct attack");
    Require(EM.GetComponent<Game::Components::CombatComponent>(Marine)->TargetEntityID == -1,
        "ground-only attacker clears invalid air target");
    Require(MarineCmd.CurrentCommand == Game::Components::CommandType::Stop,
        "direct invalid air attack stops instead of chasing forever");
    Require(MarinePath->Path.empty() &&
            MarinePath->PathIndex == 0 &&
            MarinePath->CurrentSpeed == 0.0f &&
            MarinePath->CurrentVelocity.Length() == 0.0f,
        "direct invalid air attack clears stale chase motion immediately");

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

    int DeadTarget = SpawnUnit(EM, {64.0f, 0.0f}, 1, false);
    auto* DeadTargetUnit = EM.GetComponent<Game::Components::UnitComponent>(DeadTarget);
    Require(DeadTargetUnit != nullptr, "dead direct attack target has unit data");
    DeadTargetUnit->HitPoints = 0;
    MarineCmd.CurrentCommand = Game::Components::CommandType::Attack;
    MarineCmd.TargetEntityID = GroundTarget;
    EM.GetComponent<Game::Components::CombatComponent>(Marine)->TargetEntityID = GroundTarget;
    MarinePath->Path.push_back({64.0f, 0.0f});
    MarinePath->PathIndex = 0;
    MarinePath->CurrentSpeed = 32.0f;
    MarinePath->CurrentVelocity = {32.0f, 0.0f};
    Game::Systems::CombatSystem::IssueAttack(EM, {Marine}, DeadTarget);
    Require(DeadTargetUnit->HitPoints == 0,
        "direct attack order does not revive or damage a dead target");
    Require(EM.GetComponent<Game::Components::CombatComponent>(Marine)->TargetEntityID == -1,
        "direct attack on a dead target clears stale combat target immediately");
    Require(MarineCmd.CurrentCommand == Game::Components::CommandType::Stop &&
            MarineCmd.TargetEntityID == -1,
        "direct attack on a dead target clears stale command target immediately");
    Require(MarinePath->Path.empty() &&
            MarinePath->PathIndex == 0 &&
            MarinePath->CurrentSpeed == 0.0f &&
            MarinePath->CurrentVelocity.Length() == 0.0f,
        "direct attack on a dead target clears stale chase motion immediately");

    int StasisMarine = SpawnUnit(EM, {0.0f, 32.0f}, 0, false);
    AddGroundOnlyWeapon(EM, StasisMarine);
    auto& StasisCmd = EM.AddComponent<Game::Components::CommandComponent>(StasisMarine);
    StasisCmd.CurrentCommand = Game::Components::CommandType::Attack;
    StasisCmd.TargetEntityID = GroundTarget;
    auto* StasisCombat = EM.GetComponent<Game::Components::CombatComponent>(StasisMarine);
    Require(StasisCombat != nullptr, "stasis attack coverage has combat data");
    StasisCombat->TargetEntityID = GroundTarget;
    auto* StasisPath = EM.GetComponent<Game::Components::PathfindingComponent>(StasisMarine);
    Require(StasisPath != nullptr, "stasis attack coverage has path data");
    StasisPath->Path.push_back({32.0f, 32.0f});
    StasisPath->CurrentSpeed = 32.0f;
    StasisPath->CurrentVelocity = {32.0f, 0.0f};
    EM.AddComponent<Game::Components::StatusEffectComponent>(StasisMarine)
        .AddEffect(Game::Components::StatusEffect::Stasis, 5.0f, -1);
    int FrozenOrderTarget = SpawnUnit(EM, {32.0f, 32.0f}, 1, false);
    auto* FrozenOrderTargetUnit =
        EM.GetComponent<Game::Components::UnitComponent>(FrozenOrderTarget);

    Game::Systems::CombatSystem::IssueAttack(EM, {StasisMarine}, FrozenOrderTarget);
    Combat.Update(0.2f);
    Require(FrozenOrderTargetUnit && FrozenOrderTargetUnit->HitPoints == 40,
        "stasis attacker direct attack order does not damage the target");
    Require(StasisCombat->TargetEntityID == -1,
        "stasis attacker direct attack order clears combat target");
    Require(StasisCmd.CurrentCommand == Game::Components::CommandType::Stop &&
            StasisCmd.TargetEntityID == -1,
        "stasis attacker direct attack order clears command target");
    Require(StasisPath->Path.empty() &&
            StasisPath->CurrentSpeed == 0.0f &&
            StasisPath->CurrentVelocity.Length() == 0.0f,
        "stasis attacker direct attack order clears stale chase motion");

    int Geyser = SpawnUnit(EM, {16.0f, 64.0f}, 1, false);
    auto& GeyserRes = EM.AddComponent<Game::Components::ResourceComponent>(Geyser);
    GeyserRes.Type = Game::Components::ResourceType::VespeneGas;
    GeyserRes.Amount = 5000;
    GeyserRes.MaxAmount = 5000;
    auto* GeyserUnit = EM.GetComponent<Game::Components::UnitComponent>(Geyser);
    GeyserUnit->HitPoints = 5;
    GeyserUnit->MaxHitPoints = 5;
    MarinePath->Path.push_back({128.0f, 64.0f});
    MarinePath->PathIndex = 0;
    MarinePath->CurrentSpeed = 32.0f;
    MarinePath->CurrentVelocity = {32.0f, 0.0f};

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
    Require(MarinePath->Path.empty() &&
            MarinePath->PathIndex == 0 &&
            MarinePath->CurrentSpeed == 0.0f &&
            MarinePath->CurrentVelocity.Length() == 0.0f,
        "direct resource attack clears stale movement immediately");

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

    Core::ECS::SystemManager PendingSysMgr;
    auto& PendingEM = PendingSysMgr.GetEntityManager();
    Game::Systems::BulletSystem PendingBullet(PendingSysMgr);
    int PendingMarine = SpawnUnit(PendingEM, {0.0f, 0.0f}, 0, false);
    AddGroundOnlyWeapon(PendingEM, PendingMarine);
    auto& PendingCmd = PendingEM.AddComponent<Game::Components::CommandComponent>(PendingMarine);
    int VanishedTarget = SpawnUnit(PendingEM, {32.0f, 0.0f}, 1, false);
    auto* PendingCombat =
        PendingEM.GetComponent<Game::Components::CombatComponent>(PendingMarine);
    PendingCmd.CurrentCommand = Game::Components::CommandType::Attack;
    PendingCmd.TargetEntityID = VanishedTarget;
    PendingCombat->TargetEntityID = VanishedTarget;
    PendingCombat->PendingAttackFrame = true;
    PendingCombat->PendingAttackTargetEntityID = VanishedTarget;
    PendingCombat->PendingAttackDamage = 6;
    auto& PendingPath = PendingEM.AddComponent<Game::Components::PathfindingComponent>(PendingMarine);
    PendingPath.Path.push_back({64.0f, 0.0f});
    PendingPath.PathIndex = 0;
    PendingPath.CurrentSpeed = 32.0f;
    PendingPath.CurrentVelocity = {32.0f, 0.0f};
    PendingEM.DestroyEntity(VanishedTarget);

    Require(!Game::Systems::CombatSystem::ResolvePendingAttackFrame(
                PendingEM, PendingBullet, PendingMarine),
        "pending attack frame fails cleanly when target disappears before damage frame");
    Require(PendingCombat->TargetEntityID == -1 &&
            !PendingCombat->PendingAttackFrame &&
            PendingCombat->PendingAttackTargetEntityID == -1,
        "vanished pending attack target clears combat target immediately");
    Require(PendingCmd.CurrentCommand == Game::Components::CommandType::Stop &&
            PendingCmd.TargetEntityID == -1,
        "vanished direct attack target stops the attacker immediately");
    Require(PendingPath.Path.empty() &&
            PendingPath.PathIndex == 0 &&
            PendingPath.CurrentSpeed == 0.0f &&
            PendingPath.CurrentVelocity.Length() == 0.0f,
        "vanished direct attack target clears stale chase motion immediately");

    int PendingAttackMove = SpawnUnit(PendingEM, {0.0f, 64.0f}, 0, false);
    AddGroundOnlyWeapon(PendingEM, PendingAttackMove);
    auto& PendingAttackMoveCmd =
        PendingEM.AddComponent<Game::Components::CommandComponent>(PendingAttackMove);
    int VanishedAttackMoveTarget = SpawnUnit(PendingEM, {32.0f, 64.0f}, 1, false);
    auto* PendingAttackMoveCombat =
        PendingEM.GetComponent<Game::Components::CombatComponent>(PendingAttackMove);
    PendingAttackMoveCmd.CurrentCommand = Game::Components::CommandType::AttackMove;
    PendingAttackMoveCmd.TargetPosition = {96.0f, 64.0f};
    PendingAttackMoveCmd.TargetEntityID = VanishedAttackMoveTarget;
    PendingAttackMoveCombat->TargetEntityID = VanishedAttackMoveTarget;
    PendingAttackMoveCombat->PendingAttackFrame = true;
    PendingAttackMoveCombat->PendingAttackTargetEntityID = VanishedAttackMoveTarget;
    PendingAttackMoveCombat->PendingAttackDamage = 6;
    PendingEM.DestroyEntity(VanishedAttackMoveTarget);

    Require(!Game::Systems::CombatSystem::ResolvePendingAttackFrame(
                PendingEM, PendingBullet, PendingAttackMove),
        "pending attack-move frame fails cleanly when target disappears before damage frame");
    Require(PendingAttackMoveCombat->TargetEntityID == -1 &&
            !PendingAttackMoveCombat->PendingAttackFrame &&
            PendingAttackMoveCombat->PendingAttackTargetEntityID == -1,
        "vanished attack-move target clears combat target immediately");
    Require(PendingAttackMoveCmd.CurrentCommand == Game::Components::CommandType::AttackMove &&
            PendingAttackMoveCmd.TargetEntityID == -1 &&
            PendingAttackMoveCmd.TargetPosition.X == 96.0f,
        "vanished attack-move target preserves the attack-move destination");

    Core::ECS::SystemManager DeadAttackerSysMgr;
    auto& DeadAttackerEM = DeadAttackerSysMgr.GetEntityManager();
    Game::Systems::BulletSystem DeadAttackerBullet(DeadAttackerSysMgr);
    Game::Systems::CombatSystem DeadAttackerCombatSystem(
        DeadAttackerSysMgr, DeadAttackerBullet);
    int DeadPendingAttacker =
        SpawnUnit(DeadAttackerEM, {0.0f, 0.0f}, 0, false);
    AddGroundOnlyWeapon(DeadAttackerEM, DeadPendingAttacker);
    auto* DeadPendingUnit =
        DeadAttackerEM.GetComponent<Game::Components::UnitComponent>(DeadPendingAttacker);
    auto* DeadPendingCombat =
        DeadAttackerEM.GetComponent<Game::Components::CombatComponent>(DeadPendingAttacker);
    auto& DeadPendingCmd =
        DeadAttackerEM.AddComponent<Game::Components::CommandComponent>(DeadPendingAttacker);
    auto& DeadPendingPath =
        DeadAttackerEM.AddComponent<Game::Components::PathfindingComponent>(DeadPendingAttacker);
    int DeadPendingTarget =
        SpawnUnit(DeadAttackerEM, {32.0f, 0.0f}, 1, false);
    auto* DeadPendingTargetUnit =
        DeadAttackerEM.GetComponent<Game::Components::UnitComponent>(DeadPendingTarget);
    Require(DeadPendingUnit && DeadPendingCombat && DeadPendingTargetUnit,
        "dead pending attacker setup has attacker combat and target unit data");
    DeadPendingUnit->HitPoints = 0;
    DeadPendingCmd.CurrentCommand = Game::Components::CommandType::Attack;
    DeadPendingCmd.TargetEntityID = DeadPendingTarget;
    DeadPendingCombat->TargetEntityID = DeadPendingTarget;
    DeadPendingCombat->PendingAttackFrame = true;
    DeadPendingCombat->PendingAttackTargetEntityID = DeadPendingTarget;
    DeadPendingCombat->PendingAttackDamage = 6;
    DeadPendingPath.Path.push_back({64.0f, 0.0f});
    DeadPendingPath.CurrentSpeed = 32.0f;
    DeadPendingPath.CurrentVelocity = {32.0f, 0.0f};
    Require(!Game::Systems::CombatSystem::ResolvePendingAttackFrame(
                DeadAttackerEM, DeadAttackerBullet, DeadPendingAttacker),
        "dead attacker cannot resolve a pending attack frame");
    Require(DeadPendingTargetUnit->HitPoints == 40,
        "dead pending attacker does not damage the target");
    Require(DeadPendingCombat->TargetEntityID == -1 &&
            !DeadPendingCombat->PendingAttackFrame &&
            DeadPendingCombat->PendingAttackTargetEntityID == -1,
        "dead pending attacker clears combat state");
    Require(DeadPendingCmd.CurrentCommand == Game::Components::CommandType::Stop &&
            DeadPendingCmd.TargetEntityID == -1,
        "dead pending attacker clears attack command state");
    Require(DeadPendingPath.Path.empty() &&
            DeadPendingPath.CurrentSpeed == 0.0f &&
            DeadPendingPath.CurrentVelocity.Length() == 0.0f,
        "dead pending attacker clears stale chase motion");

    int DeadUpdateAttacker =
        SpawnUnit(DeadAttackerEM, {0.0f, 64.0f}, 0, false);
    AddGroundOnlyWeapon(DeadAttackerEM, DeadUpdateAttacker);
    auto* DeadUpdateUnit =
        DeadAttackerEM.GetComponent<Game::Components::UnitComponent>(DeadUpdateAttacker);
    auto* DeadUpdateCombat =
        DeadAttackerEM.GetComponent<Game::Components::CombatComponent>(DeadUpdateAttacker);
    auto& DeadUpdateCmd =
        DeadAttackerEM.AddComponent<Game::Components::CommandComponent>(DeadUpdateAttacker);
    int DeadUpdateTarget =
        SpawnUnit(DeadAttackerEM, {32.0f, 64.0f}, 1, false);
    auto* DeadUpdateTargetUnit =
        DeadAttackerEM.GetComponent<Game::Components::UnitComponent>(DeadUpdateTarget);
    Require(DeadUpdateUnit && DeadUpdateCombat && DeadUpdateTargetUnit,
        "dead update attacker setup has combat and target unit data");
    DeadUpdateUnit->HitPoints = 0;
    DeadUpdateCmd.CurrentCommand = Game::Components::CommandType::Attack;
    DeadUpdateCmd.TargetEntityID = DeadUpdateTarget;
    DeadUpdateCombat->TargetEntityID = DeadUpdateTarget;
    DeadAttackerCombatSystem.Update(0.2f);
    Require(DeadUpdateTargetUnit->HitPoints == 40,
        "dead attacker does not fire during combat update");
    Require(DeadUpdateCombat->TargetEntityID == -1,
        "dead attacker combat update clears target state");
    Require(DeadUpdateCmd.CurrentCommand == Game::Components::CommandType::Stop &&
            DeadUpdateCmd.TargetEntityID == -1,
        "dead attacker combat update clears attack command state");

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

    Core::ECS::SystemManager StatusDamageSysMgr;
    auto& StatusDamageEM = StatusDamageSysMgr.GetEntityManager();
    Game::Systems::BulletSystem StatusDamageBullet(StatusDamageSysMgr);
    Game::Systems::CombatSystem StatusDamageCombat(StatusDamageSysMgr, StatusDamageBullet);

    int PlagueSmallStepUnit = SpawnUnit(StatusDamageEM, {0.0f, 0.0f}, 0, false);
    auto* PlagueSmallUnit =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(PlagueSmallStepUnit);
    PlagueSmallUnit->HitPoints = 100;
    PlagueSmallUnit->MaxHitPoints = 100;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(PlagueSmallStepUnit)
        .AddEffect(Game::Components::StatusEffect::Plague, 20.0f, -1);
    for (int I = 0; I < 60; ++I)
        StatusDamageCombat.Update(1.0f / 60.0f);
    Require(PlagueSmallUnit->HitPoints == 98,
        "Plague drains by elapsed time instead of once per frame");

    int PlagueLargeStepUnit = SpawnUnit(StatusDamageEM, {32.0f, 0.0f}, 0, false);
    auto* PlagueLargeUnit =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(PlagueLargeStepUnit);
    PlagueLargeUnit->HitPoints = 100;
    PlagueLargeUnit->MaxHitPoints = 100;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(PlagueLargeStepUnit)
        .AddEffect(Game::Components::StatusEffect::Plague, 20.0f, -1);
    StatusDamageCombat.Update(1.0f);
    Require(PlagueLargeUnit->HitPoints == 98,
        "Plague damage is stable across equivalent large and small frame deltas");

    int PlagueLowHpUnit = SpawnUnit(StatusDamageEM, {64.0f, 0.0f}, 0, false);
    auto* PlagueLowUnit =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(PlagueLowHpUnit);
    PlagueLowUnit->HitPoints = 3;
    PlagueLowUnit->MaxHitPoints = 1000;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(PlagueLowHpUnit)
        .AddEffect(Game::Components::StatusEffect::Plague, 20.0f, -1);
    StatusDamageCombat.Update(1.0f);
    Require(PlagueLowUnit->HitPoints == 1,
        "Plague drains hit points down to one without killing the unit");

    int IrradiateUnit = SpawnUnit(StatusDamageEM, {96.0f, 0.0f}, 0, false);
    auto* IrradiatedUnit =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(IrradiateUnit);
    IrradiatedUnit->HitPoints = 100;
    IrradiatedUnit->MaxHitPoints = 100;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(IrradiateUnit)
        .AddEffect(Game::Components::StatusEffect::Irradiate, 20.0f, -1);
    for (int I = 0; I < 60; ++I)
        StatusDamageCombat.Update(1.0f / 60.0f);
    Require(IrradiatedUnit->HitPoints == 96,
        "Irradiate drains by elapsed time instead of once per frame");

    int EmpStatusUnit = SpawnUnit(StatusDamageEM, {128.0f, 0.0f}, 0, false);
    auto* EmpStatusUnitData =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(EmpStatusUnit);
    EmpStatusUnitData->EnergyPoints = 50;
    EmpStatusUnitData->MaxEnergyPoints = 100;
    EmpStatusUnitData->ShieldPoints = 30;
    EmpStatusUnitData->MaxShieldPoints = 30;
    auto& EmpStatusCloak =
        StatusDamageEM.AddComponent<Game::Components::CloakComponent>(EmpStatusUnit);
    EmpStatusCloak.IsCloaked = true;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(EmpStatusUnit)
        .AddEffect(Game::Components::StatusEffect::EMP, 1.0f, -1);
    StatusDamageCombat.Update(0.1f);
    Require(EmpStatusUnitData->EnergyPoints == 0 &&
                EmpStatusUnitData->ShieldPoints == 0 &&
                !EmpStatusCloak.IsCloaked,
        "EMP status drains energy, shields, and active cloak during combat updates");

    Core::ECS::SystemManager StasisSysMgr;
    auto& StasisEM = StasisSysMgr.GetEntityManager();
    Game::Systems::BulletSystem StasisBullet(StasisSysMgr);
    int StasisAttacker = SpawnUnit(StasisEM, {0.0f, 0.0f}, 0, false);
    auto& StasisRangedCombat =
        StasisEM.AddComponent<Game::Components::CombatComponent>(StasisAttacker);
    StasisRangedCombat.GroundDmgType = Game::Components::DamageType::Normal;
    StasisRangedCombat.AirDmgType = Game::Components::DamageType::Normal;
    StasisRangedCombat.WeaponBeh = Game::Components::WeaponBehavior::FlyDirect;
    StasisRangedCombat.IsRanged = true;

    int StasisTarget = SpawnUnit(StasisEM, {32.0f, 0.0f}, 1, false);
    auto& StasisStatus =
        StasisEM.AddComponent<Game::Components::StatusEffectComponent>(StasisTarget);
    StasisStatus.AddEffect(Game::Components::StatusEffect::Stasis, 5.0f, StasisAttacker);
    auto* StasisTargetUnit =
        StasisEM.GetComponent<Game::Components::UnitComponent>(StasisTarget);
    Game::Systems::CombatSystem::ApplyDamage(
        StasisEM, StasisTarget, StasisRangedCombat, StasisAttacker, 20);
    Require(StasisTargetUnit && StasisTargetUnit->HitPoints == 40,
        "Stasis prevents direct combat damage while the target is frozen");

    auto* StasisAttackerTrans =
        StasisEM.GetComponent<Game::Components::TransformComponent>(StasisAttacker);
    auto* StasisAttackerPlayer =
        StasisEM.GetComponent<Game::Components::PlayerComponent>(StasisAttacker);
    auto* StasisTargetTrans =
        StasisEM.GetComponent<Game::Components::TransformComponent>(StasisTarget);
    Require(StasisAttackerTrans && StasisAttackerPlayer && StasisTargetTrans,
        "stasis bullet test has source and target presentation state");
    int StasisBulletID = StasisBullet.SpawnBullet(
        StasisEM, StasisRangedCombat, *StasisAttackerTrans, *StasisAttackerPlayer,
        StasisAttacker, 20, 0, 128.0f, StasisTarget, StasisTargetTrans->Position);
    Require(StasisBulletID >= 0, "stasis bullet test spawns a projectile");
    StasisBullet.Update(0.1f);
    Require(StasisTargetUnit && StasisTargetUnit->HitPoints == 40,
        "Stasis prevents real bullet impact damage while the target is frozen");

    StasisStatus.RemoveEffect(Game::Components::StatusEffect::Stasis);
    Game::Systems::CombatSystem::ApplyDamage(
        StasisEM, StasisTarget, StasisRangedCombat, StasisAttacker, 20);
    Require(StasisTargetUnit && StasisTargetUnit->HitPoints == 20,
        "damage resumes after Stasis is removed");

    int InvincibleTarget = SpawnUnit(StasisEM, {64.0f, 0.0f}, 1, false);
    auto* InvincibleTargetUnit =
        StasisEM.GetComponent<Game::Components::UnitComponent>(InvincibleTarget);
    InvincibleTargetUnit->IsInvincible = true;
    Game::Systems::CombatSystem::ApplyDamage(
        StasisEM, InvincibleTarget, StasisRangedCombat, StasisAttacker, 20);
    Require(InvincibleTargetUnit && InvincibleTargetUnit->HitPoints == 40,
        "invincible unit state prevents direct combat damage");

    auto* InvincibleTargetTrans =
        StasisEM.GetComponent<Game::Components::TransformComponent>(InvincibleTarget);
    int InvincibleBulletID = StasisBullet.SpawnBullet(
        StasisEM, StasisRangedCombat, *StasisAttackerTrans, *StasisAttackerPlayer,
        StasisAttacker, 20, 0, 128.0f, InvincibleTarget, InvincibleTargetTrans->Position);
    Require(InvincibleBulletID >= 0, "invincible bullet test spawns a projectile");
    StasisBullet.Update(0.1f);
    Require(InvincibleTargetUnit && InvincibleTargetUnit->HitPoints == 40,
        "invincible unit state prevents real bullet impact damage");

    InvincibleTargetUnit->IsInvincible = false;
    Game::Systems::CombatSystem::ApplyDamage(
        StasisEM, InvincibleTarget, StasisRangedCombat, StasisAttacker, 20);
    Require(InvincibleTargetUnit && InvincibleTargetUnit->HitPoints == 20,
        "damage resumes after invincibility is removed");

    Core::ECS::SystemManager DarkSwarmSysMgr;
    auto& DarkSwarmEM = DarkSwarmSysMgr.GetEntityManager();
    int DarkSwarmAttacker = SpawnUnit(DarkSwarmEM, {0.0f, 0.0f}, 0, false);
    auto& RangedCombat =
        DarkSwarmEM.AddComponent<Game::Components::CombatComponent>(DarkSwarmAttacker);
    RangedCombat.GroundDmgType = Game::Components::DamageType::Normal;
    RangedCombat.AirDmgType = Game::Components::DamageType::Normal;
    RangedCombat.WeaponBeh = Game::Components::WeaponBehavior::FlyDirect;
    RangedCombat.IsRanged = true;

    int SwarmedGround = SpawnUnit(DarkSwarmEM, {32.0f, 0.0f}, 1, false);
    DarkSwarmEM.AddComponent<Game::Components::StatusEffectComponent>(SwarmedGround)
        .AddEffect(Game::Components::StatusEffect::DarkSwarm, 8.0f, DarkSwarmAttacker);
    auto* SwarmedGroundUnit =
        DarkSwarmEM.GetComponent<Game::Components::UnitComponent>(SwarmedGround);
    Game::Systems::CombatSystem::ApplyDamage(
        DarkSwarmEM, SwarmedGround, RangedCombat, DarkSwarmAttacker, 20);
    Require(SwarmedGroundUnit && SwarmedGroundUnit->HitPoints == 40,
        "Dark Swarm prevents ranged damage against protected ground units");

    auto MeleeCombat = RangedCombat;
    MeleeCombat.WeaponBeh = Game::Components::WeaponBehavior::Melee;
    MeleeCombat.IsRanged = false;
    Game::Systems::CombatSystem::ApplyDamage(
        DarkSwarmEM, SwarmedGround, MeleeCombat, DarkSwarmAttacker, 20);
    Require(SwarmedGroundUnit && SwarmedGroundUnit->HitPoints == 20,
        "Dark Swarm does not prevent melee damage against protected ground units");

    int SwarmedAir = SpawnUnit(DarkSwarmEM, {64.0f, 0.0f}, 1, true);
    DarkSwarmEM.AddComponent<Game::Components::StatusEffectComponent>(SwarmedAir)
        .AddEffect(Game::Components::StatusEffect::DarkSwarm, 8.0f, DarkSwarmAttacker);
    auto* SwarmedAirUnit =
        DarkSwarmEM.GetComponent<Game::Components::UnitComponent>(SwarmedAir);
    Game::Systems::CombatSystem::ApplyDamage(
        DarkSwarmEM, SwarmedAir, RangedCombat, DarkSwarmAttacker, 20);
    Require(SwarmedAirUnit && SwarmedAirUnit->HitPoints == 20,
        "Dark Swarm does not prevent ranged damage against air units");

    Core::ECS::SystemManager SupplyDeathSysMgr;
    auto& SupplyDeathEM = SupplyDeathSysMgr.GetEntityManager();
    int SupplyKiller = SpawnUnit(SupplyDeathEM, {0.0f, 0.0f}, 0, false);
    auto& SupplyKillerCombat =
        SupplyDeathEM.AddComponent<Game::Components::CombatComponent>(SupplyKiller);
    SupplyKillerCombat.GroundDmgType = Game::Components::DamageType::Normal;
    SupplyKillerCombat.AirDmgType = Game::Components::DamageType::Normal;

    constexpr int MarineSupplyPlayerID = 2;
    auto& MarineSupply =
        Game::Systems::HarvestSystem::GetResources(MarineSupplyPlayerID);
    MarineSupply.SupplyUsed = 1;
    MarineSupply.SupplyMax = 10;
    int SupplyMarine =
        SpawnDefUnit(SupplyDeathEM, "Marine", {32.0f, 0.0f}, MarineSupplyPlayerID);
    Game::Systems::CombatSystem::ApplyDamage(
        SupplyDeathEM, SupplyMarine, SupplyKillerCombat, SupplyKiller, 9999);
    Require(!SupplyDeathEM.IsAlive(SupplyMarine),
        "lethal combat damage destroys the supply test Marine");
    Require(MarineSupply.SupplyUsed == 0,
        "unit death releases consumed supply");
    Require(MarineSupply.SupplyMax == 10,
        "ordinary unit death does not change supply cap");

    constexpr int OverlordSupplyPlayerID = 3;
    auto& OverlordSupply =
        Game::Systems::HarvestSystem::GetResources(OverlordSupplyPlayerID);
    OverlordSupply.SupplyUsed = 9;
    OverlordSupply.SupplyMax = 17;
    int SupplyOverlord =
        SpawnDefUnit(SupplyDeathEM, "Overlord", {64.0f, 0.0f}, OverlordSupplyPlayerID);
    Game::Systems::CombatSystem::ApplyDamage(
        SupplyDeathEM, SupplyOverlord, SupplyKillerCombat, SupplyKiller, 9999);
    Require(!SupplyDeathEM.IsAlive(SupplyOverlord),
        "lethal combat damage destroys the supply test Overlord");
    Require(OverlordSupply.SupplyUsed == 9,
        "Overlord death does not release used supply because it consumes none");
    Require(OverlordSupply.SupplyMax ==
            17 - Game::Races::GetSupplyProvidedByDefID(
                     Game::Races::GetUnitDefID("Overlord")),
        "Overlord death removes its supplied control");

    constexpr int DepotSupplyPlayerID = 4;
    auto& DepotSupply =
        Game::Systems::HarvestSystem::GetResources(DepotSupplyPlayerID);
    DepotSupply.SupplyUsed = 8;
    DepotSupply.SupplyMax = 18;
    int SupplyDepot =
        SpawnDefUnit(SupplyDeathEM, "SupplyDepot", {96.0f, 0.0f},
                     DepotSupplyPlayerID, true);
    Game::Systems::CombatSystem::ApplyDamage(
        SupplyDeathEM, SupplyDepot, SupplyKillerCombat, SupplyKiller, 9999);
    Require(!SupplyDeathEM.IsAlive(SupplyDepot),
        "lethal combat damage destroys the supply test depot");
    Require(DepotSupply.SupplyUsed == 8,
        "supply provider building death does not release unit supply");
    Require(DepotSupply.SupplyMax ==
            18 - Game::Races::GetSupplyProvidedByDefID(
                     Game::Races::GetUnitDefID("SupplyDepot")),
        "Supply Depot death removes its supplied capacity");

    std::printf("CombatRulesTest passed\n");
    return 0;
}
