#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/HarvestSystem.h"

#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Races/RaceDef.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

static int gScriptAttackImageID = -1;
static int gScriptAttackAnimID = -1;

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

void CommandSystem::IssueMove(Core::ECS::EntityManager& EM,
                              const std::vector<Core::ECS::EntityID>& Entities,
                              const Core::Math::Vec2& Target,
                              int PathingIgnoreEntityID)
{
    (void)PathingIgnoreEntityID;
    IssueMove(EM, Entities, Target);
}

void CommandSystem::IssueAttackMove(Core::ECS::EntityManager& EM,
                                    const std::vector<int>& Units,
                                    Core::Math::Vec2 Target,
                                    bool Queued)
{
    (void)Queued;
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

void CommandSystem::QueueOrder(Core::ECS::EntityManager& EM,
                               Core::ECS::EntityID EntityID,
                               const Components::Order& InOrder)
{
    if (auto* Cmd = EM.GetComponent<Components::CommandComponent>(EntityID))
        Cmd->QueueOrder(InOrder);
}

void CommandSystem::AdvanceQueue(Core::ECS::EntityManager& EM,
                                 Core::ECS::EntityID EntityID)
{
    if (auto* Cmd = EM.GetComponent<Components::CommandComponent>(EntityID))
        Cmd->AdvanceQueue();
}

bool AnimationSystem::HasIScriptAnimationForImage(int ImageID, int AnimID)
{
    return ImageID == gScriptAttackImageID && AnimID == gScriptAttackAnimID;
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

    int ResolveMarineHitPointsAfterOneAttack(int WeaponLevel, int ArmorLevel)
    {
        constexpr int WeaponPlayerID = 5;
        constexpr int ArmorPlayerID = 6;

        auto& WeaponUpgrades = Game::Systems::UpgradeSystem::GetUpgrades(WeaponPlayerID);
        auto& ArmorUpgrades = Game::Systems::UpgradeSystem::GetUpgrades(ArmorPlayerID);
        WeaponUpgrades.Levels[static_cast<size_t>(
            Game::Systems::ResearchId::Terran_InfantryWeapons)] =
            static_cast<uint8_t>(WeaponLevel);
        ArmorUpgrades.Levels[static_cast<size_t>(
            Game::Systems::ResearchId::Terran_InfantryArmor)] =
            static_cast<uint8_t>(ArmorLevel);

        Core::ECS::SystemManager SysMgr;
        auto& EM = SysMgr.GetEntityManager();
        Game::Systems::BulletSystem Bullet(SysMgr);
        Game::Systems::CombatSystem Combat(SysMgr, Bullet);

        int Attacker = SpawnDefUnit(EM, "Marine", {0.0f, 0.0f}, WeaponPlayerID);
        AddGroundOnlyWeapon(EM, Attacker);
        int Target = SpawnDefUnit(EM, "Marine", {32.0f, 0.0f}, ArmorPlayerID);

        Game::Systems::CombatSystem::IssueAttack(EM, {Attacker}, Target);
        Combat.Update(0.2f);

        auto* TargetUnit = EM.GetComponent<Game::Components::UnitComponent>(Target);
        Require(TargetUnit != nullptr, "upgrade damage test target remains available");
        return TargetUnit->HitPoints;
    }

    int ResolveMarineHitPointsAtExtendedRange(bool HasU238Shells)
    {
        constexpr int MarinePlayerID = 5;
        auto& Upgrades = Game::Systems::UpgradeSystem::GetUpgrades(MarinePlayerID);
        Upgrades.Levels[static_cast<size_t>(
            Game::Systems::ResearchId::Terran_InfantryWeapons)] = 0;
        Upgrades.Levels[static_cast<size_t>(
            Game::Systems::ResearchId::Terran_U238Shells)] =
            HasU238Shells ? 1 : 0;

        Core::ECS::SystemManager SysMgr;
        auto& EM = SysMgr.GetEntityManager();
        Game::Systems::BulletSystem Bullet(SysMgr);
        Game::Systems::CombatSystem Combat(SysMgr, Bullet);

        int Attacker = SpawnDefUnit(EM, "Marine", {0.0f, 0.0f}, MarinePlayerID);
        AddGroundOnlyWeapon(EM, Attacker);
        int Target = SpawnDefUnit(EM, "Marine", {176.0f, 0.0f}, 1);

        Game::Systems::CombatSystem::IssueAttack(EM, {Attacker}, Target);
        Combat.Update(0.2f);

        auto* TargetUnit = EM.GetComponent<Game::Components::UnitComponent>(Target);
        Require(TargetUnit != nullptr, "U-238 range test target remains available");
        return TargetUnit->HitPoints;
    }
}

int main()
{
    {
        Core::ECS::SystemManager TurretSysMgr;
        auto& TurretEM = TurretSysMgr.GetEntityManager();
        Game::Systems::BulletSystem TurretBullet(TurretSysMgr);
        Game::Systems::CombatSystem TurretCombat(TurretSysMgr, TurretBullet);

        const int Turret = SpawnDefUnit(
            TurretEM, "MissileTurret", {0.0f, 0.0f}, 0, true);
        auto& TurretWeapon =
            TurretEM.AddComponent<Game::Components::CombatComponent>(Turret);
        TurretWeapon.AirDamage = 20;
        TurretWeapon.AttackRange = 224.0f;
        TurretWeapon.AttackCooldown = 0.5f;
        TurretWeapon.CanTargetGround = false;
        TurretWeapon.CanTargetAir = true;
        TurretWeapon.IsRanged = true;
        TurretWeapon.WeaponBeh = Game::Components::WeaponBehavior::Missile;
        TurretEM.AddComponent<Game::Components::RenderComponent>(Turret).ImageID = 100;
        auto& TurretTree =
            TurretEM.AddComponent<Game::Components::ImageTreeComponent>(Turret);
        TurretTree.Nodes.emplace_back();
        TurretTree.Nodes.back().ImageID = 777;
        TurretTree.Nodes.back().Visible = true;

        const int AirTarget = SpawnUnit(TurretEM, {96.0f, 0.0f}, 1, true);
        const int InitialHitPoints =
            TurretEM.GetComponent<Game::Components::UnitComponent>(AirTarget)->HitPoints;
        Game::Systems::gScriptAttackImageID = 777;
        Game::Systems::gScriptAttackAnimID = Game::IScriptAnims::Anim_AirAttkInit;
        Game::Systems::CombatSystem::IssueAttack(TurretEM, {Turret}, AirTarget);
        TurretCombat.Update(0.2f);

        const auto* TurretCombatState =
            TurretEM.GetComponent<Game::Components::CombatComponent>(Turret);
        Require(TurretCombatState && TurretCombatState->PendingAttackFrame,
            "Missile Turret waits for its child turret AirAttkInit fire frame");
        Require(TurretEM.GetEntitiesWith<Game::Components::BulletComponent>().empty(),
            "Missile Turret does not create a projectile before the child fire frame");
        Require(TurretEM.GetComponent<Game::Components::UnitComponent>(AirTarget)->HitPoints ==
                    InitialHitPoints,
            "Missile Turret does not apply damage before its visible missile launches");
        Game::Systems::gScriptAttackImageID = -1;
        Game::Systems::gScriptAttackAnimID = -1;
    }

    {
        Core::ECS::SystemManager VisualSysMgr;
        auto& VisualEM = VisualSysMgr.GetEntityManager();
        Game::Systems::BulletSystem VisualBullet(VisualSysMgr);

        const int Source = VisualEM.CreateEntity();
        auto& SourceTrans = VisualEM.AddComponent<Game::Components::TransformComponent>(Source);
        SourceTrans.Position = {16.0f, 20.0f};
        auto& SourceUnit = VisualEM.AddComponent<Game::Components::UnitComponent>(Source);
        SourceUnit.ChkUnitID = 7;
        auto& SourcePlayer = VisualEM.AddComponent<Game::Components::PlayerComponent>(Source);
        SourcePlayer.PlayerID = 2;

        const int Target = VisualEM.CreateEntity();
        VisualEM.AddComponent<Game::Components::TransformComponent>(Target).Position = {176.0f, 20.0f};
        VisualEM.AddComponent<Game::Components::UnitComponent>(Target);

        bool ResolverCalled = false;
        Game::Systems::BulletSystem::SetVisualResolver(
            [&](int ChkUnitID, bool TargetIsAir)
                -> std::optional<Game::Systems::BulletSystem::VisualSpec> {
                ResolverCalled = ChkUnitID == 7 && !TargetIsAir;
                Game::Systems::BulletSystem::VisualSpec Spec;
                Spec.ImageID = 321u;
                Spec.Speed = 240.0f;
                Spec.HasInitAnimation = true;
                Spec.HasImpactAnimation = true;
                Spec.BulletType = 1; // follow target
                Spec.LifetimeSeconds = 1.25f;
                Spec.HeadingOffsetRadians = 3.14159265359f * 0.5f;
                Spec.ForwardOffset = 12.0f;
                Spec.UpwardOffset = 4.0f;
                Spec.ProjectileCount = 2;
                return Spec;
            });

        Game::Components::CombatComponent Weapon;
        Weapon.WeaponBeh = Game::Components::WeaponBehavior::FlyDirect;
        Weapon.IsRanged = true;
        const int BulletID = VisualBullet.SpawnBullet(
            VisualEM, Weapon, SourceTrans, SourcePlayer, Source,
            6, 0, 160.0f, Target, {176.0f, 20.0f}, 2);

        const auto* Render = VisualEM.GetComponent<Game::Components::RenderComponent>(BulletID);
        const auto* Direction = VisualEM.GetComponent<Game::Components::PathfindingComponent>(BulletID);
        const auto* BulletState =
            VisualEM.GetComponent<Game::Components::BulletComponent>(BulletID);
        const auto* BulletTransform =
            VisualEM.GetComponent<Game::Components::TransformComponent>(BulletID);
        Require(ResolverCalled, "bullet visual resolver receives source unit and target elevation");
        Require(Render && Render->ImageID == 321u,
            "ranged bullet carries the resolved images.dat visual instead of a placeholder");
        Require(BulletState &&
                BulletState->Behavior == Game::Components::WeaponBehavior::Missile &&
                std::abs(BulletState->MaxLifetime - 1.25f) < 0.001f,
            "weapons.dat follow-target behavior and 30-frame lifetime reach the bullet");
        Require(BulletTransform &&
                std::abs(BulletTransform->Position.X - 28.0f) < 0.001f &&
                std::abs(BulletTransform->Position.Y - 16.0f) < 0.001f,
            "projectile starts at the DAT forward/upward muzzle offset");
        Require(Direction && Direction->CurrentVelocity.Y < -200.0f,
            "DAT heading offset controls the projectile's initial rendered heading");
        const auto TwinProjectiles =
            VisualEM.GetEntitiesWith<Game::Components::BulletComponent>();
        Require(TwinProjectiles.size() == 2,
            "weapons.dat bullet_count creates two independently rendered projectiles");
        const int TwinBulletID = TwinProjectiles[0] == BulletID
            ? TwinProjectiles[1]
            : TwinProjectiles[0];
        const auto* TwinDirection =
            VisualEM.GetComponent<Game::Components::PathfindingComponent>(TwinBulletID);
        Require(TwinDirection && TwinDirection->CurrentVelocity.Y > 200.0f &&
                VisualEM.GetComponent<Game::Components::BulletComponent>(BulletID)
                        ->HitCount == 1 &&
                VisualEM.GetComponent<Game::Components::BulletComponent>(TwinBulletID)
                        ->HitCount == 1,
            "twin projectiles alternate heading and split, rather than multiply, hit segments");

        const auto FirstLaunchPosition =
            VisualEM.GetComponent<Game::Components::TransformComponent>(BulletID)
                ->Position;
        const float FirstLaunchLifetime =
            VisualEM.GetComponent<Game::Components::BulletComponent>(BulletID)
                ->LifetimeRemaining;
        VisualBullet.Update(0.1f);
        Require(VisualEM.GetComponent<Game::Components::TransformComponent>(
                    BulletID)->Position.X == FirstLaunchPosition.X &&
                VisualEM.GetComponent<Game::Components::TransformComponent>(
                    BulletID)->Position.Y == FirstLaunchPosition.Y &&
                VisualEM.GetComponent<Game::Components::BulletComponent>(
                    BulletID)->LifetimeRemaining == FirstLaunchLifetime,
            "projectile movement and DAT lifetime wait for the Init sigorder launch signal");
        for (int ProjectileID : TwinProjectiles)
        {
            VisualEM.GetComponent<Game::Components::BulletComponent>(
                ProjectileID)->AwaitingLaunchSignal = false;
        }

        auto* MovingTargetTransform =
            VisualEM.GetComponent<Game::Components::TransformComponent>(Target);
        MovingTargetTransform->Position = {176.0f, 100.0f};
        VisualBullet.Update(0.1f);
        Direction = VisualEM.GetComponent<Game::Components::PathfindingComponent>(BulletID);
        Require(Direction && Direction->CurrentVelocity.X > 0.0f &&
                Direction->CurrentVelocity.Y > 0.0f,
            "tracking projectile updates its rendered heading toward a moving target");

        VisualBullet.Update(1.0f);
        Require(!VisualEM.IsAlive(BulletID),
            "projectile is removed after resolving its impact");
        int ImpactVisualCount = 0;
        for (int EffectID : VisualEM.GetEntitiesWith<
                 Game::Components::EffectComponent,
                 Game::Components::RenderComponent,
                 Game::Components::UnitComponent>())
        {
            const auto* EffectRender =
                VisualEM.GetComponent<Game::Components::RenderComponent>(EffectID);
            const auto* EffectUnit =
                VisualEM.GetComponent<Game::Components::UnitComponent>(EffectID);
            const auto* EffectTrans =
                VisualEM.GetComponent<Game::Components::TransformComponent>(EffectID);
            const auto* EffectState =
                VisualEM.GetComponent<Game::Components::EffectComponent>(EffectID);
            if (!EffectRender || EffectRender->ImageID != 321u)
                continue;
            Require(EffectUnit && EffectUnit->HitPoints == 0,
                "impact visual selects the projectile Death animation state");
            Require(EffectTrans && EffectTrans->Position.X == 176.0f &&
                    EffectTrans->Position.Y == 100.0f,
                "impact visual is anchored at the resolved target position");
            Require(EffectState && EffectState->IScriptDirection >= 0 &&
                    !VisualEM.GetComponent<Game::Components::PathfindingComponent>(EffectID),
                "impact visual preserves projectile heading without entering movement/collision");
            ++ImpactVisualCount;
        }
        Require(ImpactVisualCount == 2,
            "each resolved twin projectile creates its own transient Death animation");

        const int FarTarget = VisualEM.CreateEntity();
        VisualEM.AddComponent<Game::Components::TransformComponent>(FarTarget)
            .Position = {2016.0f, 20.0f};
        auto& FarTargetUnit =
            VisualEM.AddComponent<Game::Components::UnitComponent>(FarTarget);
        FarTargetUnit.HitPoints = 100;
        FarTargetUnit.MaxHitPoints = 100;
        const int ExpiringBullet = VisualBullet.SpawnBullet(
            VisualEM, Weapon, SourceTrans, SourcePlayer, Source,
            6, 0, 2000.0f, FarTarget, {2016.0f, 20.0f});
        VisualEM.GetComponent<Game::Components::BulletComponent>(ExpiringBullet)
            ->AwaitingLaunchSignal = false;
        VisualBullet.Update(5.0f);
        Require(!VisualEM.IsAlive(ExpiringBullet) &&
                VisualEM.GetComponent<Game::Components::UnitComponent>(FarTarget)
                        ->HitPoints == 100,
            "large delta is clipped to DAT lifetime and cannot carry a projectile into a distant hit");
        bool SawLifetimeEndpoint = false;
        for (int EffectID :
             VisualEM.GetEntitiesWith<Game::Components::EffectComponent,
                                      Game::Components::TransformComponent>())
        {
            const auto* EffectTrans =
                VisualEM.GetComponent<Game::Components::TransformComponent>(EffectID);
            if (EffectTrans && EffectTrans->Position.X > SourceTrans.Position.X &&
                EffectTrans->Position.X < 500.0f)
            {
                SawLifetimeEndpoint = true;
            }
        }
        Require(SawLifetimeEndpoint,
            "expired projectile Death visual stays at its lifetime-clipped endpoint");

        Game::Systems::BulletSystem::SetVisualResolver({});
    }

    {
        Core::ECS::SystemManager DatBehaviorSysMgr;
        auto& DatBehaviorEM = DatBehaviorSysMgr.GetEntityManager();
        Game::Systems::BulletSystem DatBehaviorBullet(DatBehaviorSysMgr);
        int RawBulletType = 5;
        Game::Systems::BulletSystem::SetVisualResolver(
            [&](int, bool)
                -> std::optional<Game::Systems::BulletSystem::VisualSpec> {
                Game::Systems::BulletSystem::VisualSpec Spec;
                Spec.BulletType = RawBulletType;
                Spec.Speed = 100.0f;
                Spec.LifetimeSeconds = 10.0f;
                return Spec;
            });

        const int SourceAt = SpawnUnit(
            DatBehaviorEM, {10.0f, 20.0f}, 0, false);
        const int TargetAt = SpawnUnit(
            DatBehaviorEM, {70.0f, 20.0f}, 1, false);
        const auto SourceAtTrans = *DatBehaviorEM.GetComponent<
            Game::Components::TransformComponent>(SourceAt);
        const auto SourceAtPlayer = *DatBehaviorEM.GetComponent<
            Game::Components::PlayerComponent>(SourceAt);
        const auto TargetAtPos = DatBehaviorEM.GetComponent<
            Game::Components::TransformComponent>(TargetAt)->Position;
        Game::Components::CombatComponent InstantCombat;
        InstantCombat.GroundDmgType = Game::Components::DamageType::Normal;
        InstantCombat.WeaponBeh = Game::Components::WeaponBehavior::FlyDirect;
        InstantCombat.IsRanged = true;
        const int SourceAtBullet = DatBehaviorBullet.SpawnBullet(
            DatBehaviorEM, InstantCombat, SourceAtTrans, SourceAtPlayer,
            SourceAt, 10, 0, 64.0f, TargetAt, TargetAtPos);
        const auto* SourceAtBulletTrans = DatBehaviorEM.GetComponent<
            Game::Components::TransformComponent>(SourceAtBullet);
        Require(SourceAtBulletTrans && SourceAtBulletTrans->Position.X == 10.0f &&
                SourceAtBulletTrans->Position.Y == 20.0f,
            "weapons.dat appear-at-source projectile is created at the attacker");
        DatBehaviorBullet.Update(0.1f);
        Require(DatBehaviorEM.IsAlive(SourceAt) &&
                DatBehaviorEM.GetComponent<Game::Components::UnitComponent>(
                    TargetAt)->HitPoints == 30,
            "appear-at-source resolves its hit without destroying a non-suicide source");

        RawBulletType = 2;
        const int TargetUnitBullet = DatBehaviorBullet.SpawnBullet(
            DatBehaviorEM, InstantCombat, SourceAtTrans, SourceAtPlayer,
            SourceAt, 10, 0, 64.0f, TargetAt, TargetAtPos);
        const auto* TargetUnitBulletTrans = DatBehaviorEM.GetComponent<
            Game::Components::TransformComponent>(TargetUnitBullet);
        Require(TargetUnitBulletTrans &&
                std::abs(TargetUnitBulletTrans->Position.X - 62.0f) < 0.001f &&
                std::abs(TargetUnitBulletTrans->Position.Y - 20.0f) < 0.001f,
            "appear-at-target-unit clips the hit visual to the target inner bounds facing the attacker");
        DatBehaviorBullet.Update(0.1f);
        Require(DatBehaviorEM.GetComponent<Game::Components::UnitComponent>(
                    TargetAt)->HitPoints == 20,
            "appear-at-target-unit still resolves damage on its selected target");

        RawBulletType = 6;
        const int SuicideSource = SpawnUnit(
            DatBehaviorEM, {100.0f, 20.0f}, 0, false);
        const int SuicideTarget = SpawnUnit(
            DatBehaviorEM, {104.0f, 20.0f}, 1, false);
        const auto SuicideSourceTrans = *DatBehaviorEM.GetComponent<
            Game::Components::TransformComponent>(SuicideSource);
        const auto SuicideSourcePlayer = *DatBehaviorEM.GetComponent<
            Game::Components::PlayerComponent>(SuicideSource);
        const auto SuicideTargetPos = DatBehaviorEM.GetComponent<
            Game::Components::TransformComponent>(SuicideTarget)->Position;
        InstantCombat.SelfDestructOnAttack = true;
        const int SuicideBullet = DatBehaviorBullet.SpawnBullet(
            DatBehaviorEM, InstantCombat, SuicideSourceTrans,
            SuicideSourcePlayer, SuicideSource, 20, 0, 16.0f,
            SuicideTarget, SuicideTargetPos);
        Require(SuicideBullet >= 0 && !DatBehaviorEM.IsAlive(SuicideSource),
            "weapons.dat self-destruct bullet kills its source during launch");
        DatBehaviorBullet.Update(0.1f);
        Require(DatBehaviorEM.GetComponent<Game::Components::UnitComponent>(
                    SuicideTarget)->HitPoints == 20,
            "self-destruct projectile still resolves damage from its source position");

        RawBulletType = 8;
        InstantCombat.SelfDestructOnAttack = false;
        const int ScatterTarget = SpawnUnit(
            DatBehaviorEM, {160.0f, 20.0f}, 1, true);
        const auto ScatterTargetPos = DatBehaviorEM.GetComponent<
            Game::Components::TransformComponent>(ScatterTarget)->Position;
        const int ScatterBullet = DatBehaviorBullet.SpawnBullet(
            DatBehaviorEM, InstantCombat, SourceAtTrans, SourceAtPlayer,
            SourceAt, 6, 0, 192.0f, ScatterTarget, ScatterTargetPos);
        DatBehaviorEM.GetComponent<Game::Components::TransformComponent>(
            ScatterTarget)->Position = {220.0f, 60.0f};
        DatBehaviorBullet.Update(0.05f);
        const auto* ScatterState = DatBehaviorEM.GetComponent<
            Game::Components::BulletComponent>(ScatterBullet);
        Require(ScatterState && ScatterState->FollowTargetPositionOffset &&
                ScatterState->TargetPos.X == 219.0f &&
                ScatterState->TargetPos.Y == 59.0f,
            "attack-target-position projectile retains its authored offset while following a moving target");

        Game::Systems::BulletSystem::SetVisualResolver({});
    }

    Core::ECS::SystemManager RescuePassiveSysMgr;
    auto& RescuePassiveEM = RescuePassiveSysMgr.GetEntityManager();
    Game::Systems::BulletSystem RescuePassiveBullet(RescuePassiveSysMgr);
    Game::Systems::CombatSystem RescuePassiveCombat(RescuePassiveSysMgr, RescuePassiveBullet);
    int RescueDragoon = SpawnUnit(RescuePassiveEM, {0.0f, 0.0f}, 1, false);
    AddGroundOnlyWeapon(RescuePassiveEM, RescueDragoon);
    RescuePassiveEM.GetComponent<Game::Components::PlayerComponent>(RescueDragoon)->IsRescuePassive = true;
    int RescueFenix = SpawnUnit(RescuePassiveEM, {32.0f, 0.0f}, 2, false);
    RescuePassiveEM.GetComponent<Game::Components::PlayerComponent>(RescueFenix)->IsRescuePassive = true;
    RescuePassiveCombat.Update(0.2f);
    Require(RescuePassiveEM.GetComponent<Game::Components::CombatComponent>(RescueDragoon)->TargetEntityID == -1,
        "Rescue Passive scenario units do not auto-target another rescue player");
    Require(RescuePassiveEM.GetComponent<Game::Components::UnitComponent>(RescueFenix)->HitPoints == 40,
        "Rescue Passive mission target remains unharmed during automatic combat");

    Core::ECS::SystemManager ProbeWorkerSysMgr;
    auto& ProbeWorkerEM = ProbeWorkerSysMgr.GetEntityManager();
    Game::Systems::BulletSystem ProbeWorkerBullet(ProbeWorkerSysMgr);
    Game::Systems::CombatSystem ProbeWorkerCombat(
        ProbeWorkerSysMgr, ProbeWorkerBullet);
    const int PassiveProbe = SpawnDefUnit(
        ProbeWorkerEM, "Probe", {0.0f, 0.0f}, 0);
    AddGroundOnlyWeapon(ProbeWorkerEM, PassiveProbe);
    ProbeWorkerEM.AddComponent<Game::Components::CommandComponent>(PassiveProbe)
        .CurrentCommand = Game::Components::CommandType::Stop;
    const int ProbeEnemy = SpawnUnit(
        ProbeWorkerEM, {32.0f, 0.0f}, 1, false);
    ProbeWorkerCombat.Update(0.2f);
    Require(ProbeWorkerEM.GetComponent<Game::Components::CombatComponent>(
                PassiveProbe)->TargetEntityID == -1 &&
            ProbeWorkerEM.GetComponent<Game::Components::UnitComponent>(
                ProbeEnemy)->HitPoints == 40,
        "Probe CHK 64 is classified as a worker and does not passively acquire enemies");

    Core::ECS::SystemManager QueenCombatSysMgr;
    auto& QueenCombatEM = QueenCombatSysMgr.GetEntityManager();
    Game::Systems::BulletSystem QueenCombatBullet(QueenCombatSysMgr);
    Game::Systems::CombatSystem QueenCombat(
        QueenCombatSysMgr, QueenCombatBullet);
    const int PassiveQueen = SpawnDefUnit(
        QueenCombatEM, "Queen", {0.0f, 0.0f}, 0);
    AddGroundOnlyWeapon(QueenCombatEM, PassiveQueen);
    QueenCombatEM.AddComponent<Game::Components::CommandComponent>(PassiveQueen)
        .CurrentCommand = Game::Components::CommandType::Stop;
    const int QueenEnemy = SpawnUnit(
        QueenCombatEM, {32.0f, 0.0f}, 1, false);
    QueenCombat.Update(0.2f);
    Require(QueenCombatEM.GetComponent<Game::Components::CombatComponent>(
                PassiveQueen)->TargetEntityID == QueenEnemy &&
            QueenCombatEM.GetComponent<Game::Components::UnitComponent>(
                QueenEnemy)->HitPoints < 40,
        "Queen CHK 45 is not misclassified as a worker and can passively acquire enemies");

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

    int Refinery = SpawnUnit(EM, {48.0f, 64.0f}, 1, false);
    auto* RefineryUnit = EM.GetComponent<Game::Components::UnitComponent>(Refinery);
    auto* RefineryPlayer = EM.GetComponent<Game::Components::PlayerComponent>(Refinery);
    auto* RefineryPath = EM.GetComponent<Game::Components::PathfindingComponent>(Refinery);
    RefineryUnit->ChkUnitID = 188;
    RefineryUnit->HitPoints = 500;
    RefineryUnit->MaxHitPoints = 500;
    RefineryPlayer->PlayerID = 11;
    auto& RefineryRender = EM.AddComponent<Game::Components::RenderComponent>(Refinery);
    RefineryRender.ImageID = 188;
    auto& RefineryBuilding = EM.AddComponent<Game::Components::BuildingComponent>(Refinery);
    RefineryBuilding.UnderConstruction = false;
    RefineryBuilding.BuildingDefID = -1;
    auto& RefineryResource = EM.AddComponent<Game::Components::ResourceComponent>(Refinery);
    RefineryResource.Type = Game::Components::ResourceType::VespeneGas;
    RefineryResource.Amount = 5000;
    RefineryResource.MaxAmount = 5000;
    auto& GeyserSnapshot =
        EM.AddComponent<Game::Components::ClaimedGeyserComponent>(Refinery);
    GeyserSnapshot.IsClaimed = true;
    GeyserSnapshot.OriginalUnit = *RefineryUnit;
    GeyserSnapshot.OriginalPlayer = *RefineryPlayer;
    GeyserSnapshot.OriginalRender = RefineryRender;
    GeyserSnapshot.OriginalBuilding = RefineryBuilding;
    GeyserSnapshot.OriginalPathfinding = *RefineryPath;
    GeyserSnapshot.HadPathfinding = true;

    RefineryUnit->ChkUnitID = 110;
    RefineryUnit->HitPoints = 5;
    RefineryUnit->MaxHitPoints = 5;
    RefineryPlayer->PlayerID = 1;
    RefineryRender.ImageID = 307;
    RefineryRender.Layer = Game::Components::RenderLayer::Buildings;
    RefineryBuilding.BuildingDefID = Game::Races::GetUnitDefID("Refinery");
    Require(Game::Systems::IsMeleeStructureEntity(EM, Refinery),
        "claimed Refinery remains a melee structure despite retaining ResourceComponent");

    int GasWorker = SpawnUnit(EM, {48.0f, 64.0f}, 1, false);
    auto& GasWorkerRender = EM.AddComponent<Game::Components::RenderComponent>(GasWorker);
    GasWorkerRender.Visible = false;
    auto& GasWorkerHarvest = EM.AddComponent<Game::Components::HarvestComponent>(GasWorker);
    GasWorkerHarvest.State = Game::Components::HarvestState::Mining;
    GasWorkerHarvest.TargetResourceID = Refinery;
    GasWorkerHarvest.DesiredResourceID = Refinery;
    GasWorkerHarvest.InsideGasBuilding = true;
    GasWorkerHarvest.GasExitPosition = {80.0f, 96.0f};
    EM.AddComponent<Game::Components::CommandComponent>(GasWorker)
        .CurrentCommand = Game::Components::CommandType::Harvest;

    auto* MarineCombat = EM.GetComponent<Game::Components::CombatComponent>(Marine);
    Game::Systems::CombatSystem::IssueAttack(EM, {Marine}, Refinery);
    Require(MarineCombat && MarineCombat->TargetEntityID == Refinery,
        "claimed refinery remains a valid combat target despite owning gas resource data");
    auto RefineryProjectileCombat = *MarineCombat;
    RefineryProjectileCombat.WeaponBeh = Game::Components::WeaponBehavior::AppearOnTarget;
    RefineryProjectileCombat.IsRanged = true;
    const int RefineryProjectile = Bullet.SpawnBullet(
        EM, RefineryProjectileCombat,
        *EM.GetComponent<Game::Components::TransformComponent>(Marine),
        *EM.GetComponent<Game::Components::PlayerComponent>(Marine),
        Marine, 10, 0, 128.0f, Refinery,
        EM.GetComponent<Game::Components::TransformComponent>(Refinery)->Position);
    Require(RefineryProjectile >= 0,
        "ranged attack creates a projectile against a claimed refinery");
    Bullet.Update(0.1f);
    Require(EM.IsAlive(Refinery) && RefineryUnit->ChkUnitID == 188 &&
            RefineryUnit->HitPoints == 500 && RefineryPlayer->PlayerID == 11 &&
            RefineryRender.ImageID == 188 && RefineryRender.Visible &&
            RefineryBuilding.BuildingDefID == -1 && !GeyserSnapshot.IsClaimed,
        "projectile-destroyed refinery restores the original neutral vespene geyser entity");
    Require(RefineryResource.Amount == 5000 &&
            RefineryResource.Type == Game::Components::ResourceType::VespeneGas,
        "restored geyser preserves its remaining gas amount");
    Require(GasWorkerRender.Visible &&
            GasWorkerHarvest.State == Game::Components::HarvestState::Idle &&
            GasWorkerHarvest.TargetResourceID < 0 &&
            GasWorkerHarvest.DesiredResourceID < 0 &&
            !GasWorkerHarvest.InsideGasBuilding,
        "destroyed refinery releases and reveals workers that were harvesting inside");

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

    int ShieldedLargeTarget = SpawnUnit(DamageTypeEM, {64.0f, 0.0f}, 1, false);
    auto* ShieldedLargeUnit =
        DamageTypeEM.GetComponent<Game::Components::UnitComponent>(ShieldedLargeTarget);
    ShieldedLargeUnit->SizeType = Game::Components::UnitSize::Large;
    ShieldedLargeUnit->HitPoints = 100;
    ShieldedLargeUnit->MaxHitPoints = 100;
    ShieldedLargeUnit->ShieldPoints = 20;
    ShieldedLargeUnit->MaxShieldPoints = 20;
    AntiAirCombat.GroundDmgType = Game::Components::DamageType::Concussive;
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, ShieldedLargeTarget, AntiAirCombat, AntiAirUnit, 40);
    Require(ShieldedLargeUnit->ShieldPoints == 0 &&
            ShieldedLargeUnit->HitPoints == 95,
        "shields absorb raw weapon damage before HP size modifiers are applied");

    int FractionalSmallTarget =
        SpawnUnit(DamageTypeEM, {96.0f, 0.0f}, 1, false);
    auto* FractionalSmallUnit =
        DamageTypeEM.GetComponent<Game::Components::UnitComponent>(
            FractionalSmallTarget);
    FractionalSmallUnit->SizeType = Game::Components::UnitSize::Small;
    FractionalSmallUnit->HitPoints = 10;
    FractionalSmallUnit->MaxHitPoints = 10;
    AntiAirCombat.GroundDmgType = Game::Components::DamageType::Explosive;
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, FractionalSmallTarget, AntiAirCombat, AntiAirUnit, 5);
    Require(FractionalSmallUnit->HitPoints == 8 &&
            FractionalSmallUnit->HitPointSubDamage == 128,
        "explosive damage preserves the first half hit point against a small target");
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, FractionalSmallTarget, AntiAirCombat, AntiAirUnit, 5);
    Require(FractionalSmallUnit->HitPoints == 5 &&
            FractionalSmallUnit->HitPointSubDamage == 0,
        "two 2.5-point explosive hits total exactly five hit points");

    int MinimumDamageTarget =
        SpawnUnit(DamageTypeEM, {128.0f, 0.0f}, 1, false);
    auto* MinimumDamageUnit =
        DamageTypeEM.GetComponent<Game::Components::UnitComponent>(
            MinimumDamageTarget);
    MinimumDamageUnit->Armor = 10;
    MinimumDamageUnit->HitPoints = 10;
    MinimumDamageUnit->MaxHitPoints = 10;
    AntiAirCombat.GroundDmgType = Game::Components::DamageType::Normal;
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, MinimumDamageTarget, AntiAirCombat, AntiAirUnit, 1, 2);
    Require(MinimumDamageUnit->HitPoints == 9 &&
            MinimumDamageUnit->HitPointSubDamage == 0,
        "two armor-limited weapon segments apply the classic 0.5 minimum each");

    int ShieldOverflowTarget =
        SpawnUnit(DamageTypeEM, {160.0f, 0.0f}, 1, false);
    auto* ShieldOverflowUnit =
        DamageTypeEM.GetComponent<Game::Components::UnitComponent>(
            ShieldOverflowTarget);
    ShieldOverflowUnit->HitPoints = 10;
    ShieldOverflowUnit->MaxHitPoints = 10;
    ShieldOverflowUnit->ShieldPoints = 1;
    ShieldOverflowUnit->MaxShieldPoints = 1;
    ShieldOverflowUnit->Armor = 1;
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, ShieldOverflowTarget, AntiAirCombat, AntiAirUnit, 2);
    Require(ShieldOverflowUnit->ShieldPoints == 0 &&
            ShieldOverflowUnit->HitPoints == 10,
        "shield overflow is absorbed by body armor without a spurious minimum HP hit");

    int ShieldImpactTarget =
        SpawnUnit(DamageTypeEM, {176.0f, 0.0f}, 1, false);
    auto* ShieldImpactUnit =
        DamageTypeEM.GetComponent<Game::Components::UnitComponent>(
            ShieldImpactTarget);
    ShieldImpactUnit->HitPoints = 20;
    ShieldImpactUnit->MaxHitPoints = 20;
    ShieldImpactUnit->ShieldPoints = 10;
    ShieldImpactUnit->MaxShieldPoints = 10;
    AntiAirCombat.GroundDmgType = Game::Components::DamageType::Normal;
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, ShieldImpactTarget, AntiAirCombat, AntiAirUnit, 3);
    const auto* ShieldImpact =
        DamageTypeEM.GetComponent<Game::Components::ShieldImpactComponent>(
            ShieldImpactTarget);
    Require(ShieldImpactUnit->ShieldPoints == 7 && ShieldImpact &&
            ShieldImpact->Impacts.size() == 1 &&
            ShieldImpact->Impacts.front().DirectionIndex == 24,
        "a surviving shield queues the OpenBW type-5 overlay direction from an eastbound hit");

    int HallucinationTarget =
        SpawnUnit(DamageTypeEM, {192.0f, 0.0f}, 1, false);
    auto* HallucinationUnit =
        DamageTypeEM.GetComponent<Game::Components::UnitComponent>(HallucinationTarget);
    HallucinationUnit->HitPoints = 20;
    HallucinationUnit->MaxHitPoints = 20;
    HallucinationUnit->Armor = 0;
    DamageTypeEM.AddComponent<Game::Components::StatusEffectComponent>(HallucinationTarget)
        .AddEffect(Game::Components::StatusEffect::Hallucination, 180.0f, AntiAirUnit);
    Game::Components::CombatComponent HallucinationDamage;
    HallucinationDamage.GroundDmgType = Game::Components::DamageType::Normal;
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, HallucinationTarget, HallucinationDamage, AntiAirUnit, 5);
    Require(HallucinationUnit->HitPoints == 10,
        "hallucinations take double raw damage before armor and shield resolution");

    int HallucinationVictim =
        SpawnUnit(DamageTypeEM, {224.0f, 0.0f}, 1, false);
    auto* HallucinationVictimUnit =
        DamageTypeEM.GetComponent<Game::Components::UnitComponent>(HallucinationVictim);
    HallucinationVictimUnit->HitPoints = 20;
    HallucinationVictimUnit->MaxHitPoints = 20;
    HallucinationVictimUnit->ShieldPoints = 10;
    HallucinationVictimUnit->MaxShieldPoints = 10;
    DamageTypeEM.AddComponent<Game::Components::StatusEffectComponent>(AntiAirUnit)
        .AddEffect(Game::Components::StatusEffect::Hallucination, 180.0f, AntiAirUnit);
    Game::Systems::CombatSystem::ApplyDamage(
        DamageTypeEM, HallucinationVictim, HallucinationDamage, AntiAirUnit, 5);
    const auto* HallucinatedShieldImpact =
        DamageTypeEM.GetComponent<Game::Components::ShieldImpactComponent>(
            HallucinationVictim);
    Require(HallucinationVictimUnit->HitPoints == 20 &&
            HallucinationVictimUnit->ShieldPoints == 10 &&
            HallucinatedShieldImpact &&
            HallucinatedShieldImpact->Impacts.size() == 1,
        "hallucination weapon impacts deal no damage but still create the shield-hit visual");

    Game::Map::GameMap HeightMap;
    HeightMap.Generate(4, 4);
    Game::Map::Tile HighTile = HeightMap.GetTile(1, 0);
    HighTile.GroundHeight = 2;
    HighTile.SubtileGroundHeights.fill(2);
    HeightMap.SetTile(1, 0, HighTile);
    Core::ECS::SystemManager HeightSysMgr;
    auto& HeightEM = HeightSysMgr.GetEntityManager();
    Game::Systems::BulletSystem HeightBullet(HeightSysMgr);
    Game::Systems::CombatSystem HeightCombat(
        HeightSysMgr, HeightBullet, &HeightMap);
    int LowGroundAttacker = SpawnUnit(HeightEM, {16.0f, 16.0f}, 10, false);
    int HighGroundTarget = SpawnUnit(HeightEM, {48.0f, 16.0f}, 11, false);
    int FlatGroundAttacker = SpawnUnit(HeightEM, {16.0f, 48.0f}, 10, false);
    int FlatGroundTarget = SpawnUnit(HeightEM, {48.0f, 48.0f}, 11, false);
    for (int AttackerID : {LowGroundAttacker, FlatGroundAttacker})
    {
        AddGroundOnlyWeapon(HeightEM, AttackerID);
        auto* HeightWeapon =
            HeightEM.GetComponent<Game::Components::CombatComponent>(AttackerID);
        HeightWeapon->WeaponBeh = Game::Components::WeaponBehavior::AppearOnTarget;
        HeightWeapon->IsRanged = true;
    }
    Game::Systems::CombatSystem::IssueAttack(
        HeightEM, {LowGroundAttacker}, HighGroundTarget);
    Game::Systems::CombatSystem::IssueAttack(
        HeightEM, {FlatGroundAttacker}, FlatGroundTarget);
    HeightCombat.Update(0.2f);
    HeightBullet.Update(0.1f);
    Require(HeightEM.GetComponent<Game::Components::UnitComponent>(
                HighGroundTarget)->HitPoints == 40 &&
            HeightEM.GetComponent<Game::Components::UnitComponent>(
                FlatGroundTarget)->HitPoints == 34,
        "low-ground ranged fire uses the classic 120/256 miss roll while same-height fire remains accurate");

    Core::ECS::SystemManager RectRangeSysMgr;
    auto& RectRangeEM = RectRangeSysMgr.GetEntityManager();
    Game::Systems::BulletSystem RectRangeBullet(RectRangeSysMgr);
    Game::Systems::CombatSystem RectRangeCombat(RectRangeSysMgr, RectRangeBullet);
    int VerticalAttacker = SpawnUnit(RectRangeEM, {0.0f, 160.0f}, 10, false);
    int HorizontalAttacker = SpawnUnit(RectRangeEM, {160.0f, 0.0f}, 10, false);
    int RectangularTarget = SpawnUnit(RectRangeEM, {0.0f, 0.0f}, 11, false);
    AddGroundOnlyWeapon(RectRangeEM, VerticalAttacker);
    AddGroundOnlyWeapon(RectRangeEM, HorizontalAttacker);
    for (int AttackerID : {VerticalAttacker, HorizontalAttacker})
    {
        auto* AttackerCombat =
            RectRangeEM.GetComponent<Game::Components::CombatComponent>(AttackerID);
        AttackerCombat->AttackRange = 100.0f / 32.0f;
        auto* AttackerPath =
            RectRangeEM.GetComponent<Game::Components::PathfindingComponent>(AttackerID);
        AttackerPath->HasCollisionBounds = true;
        AttackerPath->CollisionLeft = 8;
        AttackerPath->CollisionRight = 7;
        AttackerPath->CollisionUp = 8;
        AttackerPath->CollisionDown = 7;
    }
    auto* RectTargetPath =
        RectRangeEM.GetComponent<Game::Components::PathfindingComponent>(RectangularTarget);
    RectTargetPath->HasCollisionBounds = true;
    RectTargetPath->CollisionLeft = 64;
    RectTargetPath->CollisionRight = 63;
    RectTargetPath->CollisionUp = 32;
    RectTargetPath->CollisionDown = 31;
    auto* RectTargetUnit =
        RectRangeEM.GetComponent<Game::Components::UnitComponent>(RectangularTarget);
    RectTargetUnit->HitPoints = 200;
    RectTargetUnit->MaxHitPoints = 200;
    Game::Systems::CombatSystem::IssueAttack(
        RectRangeEM, {VerticalAttacker, HorizontalAttacker}, RectangularTarget);
    RectRangeCombat.Update(0.2f);
    Require(RectTargetUnit->HitPoints == 194,
        "combat range uses the target's rectangular DAT bounds: side attacker fires while top attacker remains out of range");

    Core::ECS::SystemManager HangarSysMgr;
    auto& HangarEM = HangarSysMgr.GetEntityManager();
    Game::Systems::BulletSystem HangarBullet(HangarSysMgr);
    Game::Systems::CombatSystem HangarCombat(HangarSysMgr, HangarBullet);
    int Carrier = SpawnDefUnit(HangarEM, "Carrier", {0.0f, 0.0f}, 10);
    auto* CarrierUnit =
        HangarEM.GetComponent<Game::Components::UnitComponent>(Carrier);
    CarrierUnit->HangarCount = 2;
    auto& CarrierCombat =
        HangarEM.AddComponent<Game::Components::CombatComponent>(Carrier);
    CarrierCombat.GroundDamage = 6;
    CarrierCombat.AirDamage = 6;
    CarrierCombat.GroundDmgType = Game::Components::DamageType::Normal;
    CarrierCombat.AirDmgType = Game::Components::DamageType::Normal;
    CarrierCombat.AttackRange = 8.0f;
    CarrierCombat.AttackCooldown = 1.0f;
    CarrierCombat.CanTargetGround = true;
    CarrierCombat.CanTargetAir = true;
    CarrierCombat.WeaponBeh = Game::Components::WeaponBehavior::AppearOnTarget;
    CarrierCombat.IsRanged = true;
    int CarrierTarget = SpawnUnit(HangarEM, {64.0f, 0.0f}, 11, false);
    auto* CarrierTargetUnit =
        HangarEM.GetComponent<Game::Components::UnitComponent>(CarrierTarget);
    CarrierTargetUnit->HitPoints = 100;
    CarrierTargetUnit->MaxHitPoints = 100;
    Game::Systems::CombatSystem::IssueAttack(HangarEM, {Carrier}, CarrierTarget);
    HangarCombat.Update(0.2f);
    HangarBullet.Update(0.1f);
    Require(CarrierTargetUnit->HitPoints == 88,
        "Carrier aggregate damage uses the actual two-interceptor CHK hangar count");
    CarrierUnit->HangarCount = 0;
    CarrierCombat.CooldownRemaining = 0.0f;
    int EmptyCarrierTarget = SpawnUnit(HangarEM, {64.0f, 64.0f}, 11, false);
    Game::Systems::CombatSystem::IssueAttack(
        HangarEM, {Carrier}, EmptyCarrierTarget);
    HangarCombat.Update(0.2f);
    HangarBullet.Update(0.1f);
    Require(HangarEM.GetComponent<Game::Components::UnitComponent>(
                EmptyCarrierTarget)->HitPoints == 40 &&
            CarrierCombat.CooldownRemaining <= 0.0f,
        "an explicitly empty Carrier neither attacks nor consumes weapon cooldown");

    int Reaver = SpawnDefUnit(HangarEM, "Reaver", {0.0f, 128.0f}, 10);
    auto* ReaverUnit =
        HangarEM.GetComponent<Game::Components::UnitComponent>(Reaver);
    ReaverUnit->HangarCount = 1;
    auto& ReaverCombat =
        HangarEM.AddComponent<Game::Components::CombatComponent>(Reaver);
    ReaverCombat.GroundDamage = 20;
    ReaverCombat.GroundDmgType = Game::Components::DamageType::Normal;
    ReaverCombat.AttackRange = 8.0f;
    ReaverCombat.AttackCooldown = 1.0f;
    ReaverCombat.CanTargetGround = true;
    ReaverCombat.WeaponBeh = Game::Components::WeaponBehavior::Lob;
    ReaverCombat.IsRanged = true;
    int ReaverTarget = SpawnUnit(HangarEM, {64.0f, 128.0f}, 11, false);
    Game::Systems::CombatSystem::IssueAttack(HangarEM, {Reaver}, ReaverTarget);
    HangarCombat.Update(0.2f);
    HangarBullet.Update(0.3f);
    Require(HangarEM.GetComponent<Game::Components::UnitComponent>(
                ReaverTarget)->HitPoints == 20 &&
            ReaverUnit->HangarCount == 0,
        "Reaver consumes one scenario-authored Scarab payload per launched attack");
    ReaverCombat.CooldownRemaining = 0.0f;
    int EmptyReaverTarget = SpawnUnit(HangarEM, {64.0f, 192.0f}, 11, false);
    Game::Systems::CombatSystem::IssueAttack(
        HangarEM, {Reaver}, EmptyReaverTarget);
    HangarCombat.Update(0.2f);
    HangarBullet.Update(0.3f);
    Require(HangarEM.GetComponent<Game::Components::UnitComponent>(
                EmptyReaverTarget)->HitPoints == 40,
        "an empty Reaver cannot fire another Scarab");

    Core::ECS::SystemManager ShieldSysMgr;
    auto& ShieldEM = ShieldSysMgr.GetEntityManager();
    Game::Systems::BulletSystem ShieldBullet(ShieldSysMgr);
    Game::Systems::CombatSystem ShieldCombat(ShieldSysMgr, ShieldBullet);
    int ShieldedUnit = SpawnUnit(ShieldEM, {0.0f, 0.0f}, 0, false);
    auto* ShieldedUnitData =
        ShieldEM.GetComponent<Game::Components::UnitComponent>(ShieldedUnit);
    ShieldedUnitData->ShieldPoints = 0;
    ShieldedUnitData->MaxShieldPoints = 10;
    for (int I = 0; I < 256; ++I)
        ShieldCombat.Update(1.0f / 24.0f);
    Require(ShieldedUnitData->ShieldPoints == 7 &&
            ShieldedUnitData->ShieldSubDamage == 0,
        "256 Brood War logic frames regenerate exactly seven shield points");

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
    Require(PlagueSmallUnit->HitPoints == 89 &&
            PlagueSmallUnit->HitPointSubDamage == 214,
        "one second of Plague applies three classic 1010/256 HP ticks");

    int PlagueLargeStepUnit = SpawnUnit(StatusDamageEM, {32.0f, 0.0f}, 0, false);
    auto* PlagueLargeUnit =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(PlagueLargeStepUnit);
    PlagueLargeUnit->HitPoints = 100;
    PlagueLargeUnit->MaxHitPoints = 100;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(PlagueLargeStepUnit)
        .AddEffect(Game::Components::StatusEffect::Plague, 20.0f, -1);
    StatusDamageCombat.Update(1.0f);
    Require(PlagueLargeUnit->HitPoints == 89 &&
            PlagueLargeUnit->HitPointSubDamage == 214,
        "Plague fixed-point damage is stable across equivalent large and small frame deltas");

    int PlagueLowHpUnit = SpawnUnit(StatusDamageEM, {64.0f, 0.0f}, 0, false);
    auto* PlagueLowUnit =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(PlagueLowHpUnit);
    PlagueLowUnit->HitPoints = 5;
    PlagueLowUnit->MaxHitPoints = 1000;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(PlagueLowHpUnit)
        .AddEffect(Game::Components::StatusEffect::Plague, 20.0f, -1);
    StatusDamageCombat.Update(1.0f);
    Require(PlagueLowUnit->HitPoints == 2 &&
            PlagueLowUnit->HitPointSubDamage == 242,
        "Plague stops when the remaining fixed-point HP cannot survive another tick");

    int PlagueFullDurationUnit = SpawnUnit(StatusDamageEM, {320.0f, 0.0f}, 0, false);
    auto* PlagueFullUnit =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(PlagueFullDurationUnit);
    PlagueFullUnit->HitPoints = 100;
    PlagueFullUnit->MaxHitPoints = 100;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(PlagueFullDurationUnit)
        .AddEffect(Game::Components::StatusEffect::Plague, 25.0f, -1);
    StatusDamageCombat.Update(25.0f);
    Require(PlagueFullUnit->HitPoints == 2 &&
            PlagueFullUnit->HitPointSubDamage == 162,
        "the full 75-tick Plague duration leaves the classic 350/256 raw HP remainder");

    Core::ECS::SystemManager IrradiateSysMgr;
    auto& IrradiateEM = IrradiateSysMgr.GetEntityManager();
    Game::Systems::BulletSystem IrradiateBullet(IrradiateSysMgr);
    Game::Systems::CombatSystem IrradiateCombat(IrradiateSysMgr, IrradiateBullet);
    int IrradiateUnit = SpawnUnit(IrradiateEM, {0.0f, 0.0f}, 0, false);
    auto* IrradiatedUnit =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(IrradiateUnit);
    IrradiatedUnit->ChkUnitID = 0; // Marine: units.dat Organic flag.
    IrradiatedUnit->HitPoints = 100;
    IrradiatedUnit->MaxHitPoints = 100;
    IrradiateEM.AddComponent<Game::Components::StatusEffectComponent>(IrradiateUnit)
        .AddEffect(Game::Components::StatusEffect::Irradiate, 25.0f, -1);
    for (int I = 0; I < 60; ++I)
        IrradiateCombat.Update(1.0f / 60.0f);
    Require(IrradiatedUnit->HitPoints == 91 &&
            IrradiatedUnit->HitPointSubDamage == 255,
        "one second of Irradiate applies three classic 853/256 spell-damage ticks");

    int ShieldedIrradiateUnit = SpawnUnit(IrradiateEM, {256.0f, 0.0f}, 0, false);
    auto* ShieldedIrradiate =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(ShieldedIrradiateUnit);
    ShieldedIrradiate->ChkUnitID = 0;
    ShieldedIrradiate->HitPoints = 100;
    ShieldedIrradiate->MaxHitPoints = 100;
    ShieldedIrradiate->ShieldPoints = 10;
    ShieldedIrradiate->MaxShieldPoints = 10;
    ShieldedIrradiate->Armor = 99;
    IrradiateEM.AddComponent<Game::Components::StatusEffectComponent>(ShieldedIrradiateUnit)
        .AddEffect(Game::Components::StatusEffect::Irradiate, 25.0f, -1);
    IrradiateCombat.Update(8.0f / 24.0f);
    Require(ShieldedIrradiate->HitPoints == 100 &&
            ShieldedIrradiate->ShieldPoints == 7 &&
            ShieldedIrradiate->ShieldSubDamage == 85,
        "Irradiate consumes shields first while ignoring shield and body armor");

    int MechanicalCarrier = SpawnUnit(IrradiateEM, {512.0f, 0.0f}, 0, false);
    auto* MechanicalCarrierUnit =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(MechanicalCarrier);
    MechanicalCarrierUnit->ChkUnitID = 2; // Vulture: not organic.
    MechanicalCarrierUnit->HitPoints = 80;
    MechanicalCarrierUnit->MaxHitPoints = 80;
    int OrganicBystander = SpawnUnit(IrradiateEM, {544.0f, 0.0f}, 1, false);
    auto* OrganicBystanderUnit =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(OrganicBystander);
    OrganicBystanderUnit->ChkUnitID = 0;
    int MechanicalBystander = SpawnUnit(IrradiateEM, {576.0f, 0.0f}, 1, false);
    auto* MechanicalBystanderUnit =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(MechanicalBystander);
    MechanicalBystanderUnit->ChkUnitID = 3; // Goliath: not organic.
    int IrradiateEgg = SpawnUnit(IrradiateEM, {544.0f, 24.0f}, 1, false);
    auto* IrradiateEggUnit =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(IrradiateEgg);
    IrradiateEggUnit->ChkUnitID = 36;
    int IrradiateCocoon = SpawnUnit(IrradiateEM, {544.0f, -24.0f}, 1, false);
    auto* IrradiateCocoonUnit =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(IrradiateCocoon);
    IrradiateCocoonUnit->ChkUnitID = 59;
    int BurrowedBystander = SpawnUnit(IrradiateEM, {512.0f, 32.0f}, 1, false);
    auto* BurrowedBystanderUnit =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(BurrowedBystander);
    BurrowedBystanderUnit->ChkUnitID = 38;
    auto& IrradiateBurrow =
        IrradiateEM.AddComponent<Game::Components::BurrowComponent>(BurrowedBystander);
    IrradiateBurrow.IsBurrowed = true;
    IrradiateBurrow.Phase = Game::Components::BurrowPhase::Burrowed;
    IrradiateEM.AddComponent<Game::Components::StatusEffectComponent>(MechanicalCarrier)
        .AddEffect(Game::Components::StatusEffect::Irradiate, 25.0f, MechanicalCarrier);
    IrradiateCombat.Update(8.0f / 24.0f);
    Require(MechanicalCarrierUnit->HitPoints == 80 &&
            OrganicBystanderUnit->HitPoints == 37 &&
            OrganicBystanderUnit->HitPointSubDamage == 85 &&
            MechanicalBystanderUnit->HitPoints == 40 &&
            IrradiateEggUnit->HitPoints == 40 &&
            IrradiateCocoonUnit->HitPoints == 37 &&
            IrradiateCocoonUnit->HitPointSubDamage == 85 &&
            BurrowedBystanderUnit->HitPoints == 40,
        "Irradiate applies the classic organic, egg, cocoon, mechanical, and burrow filters");

    int LethalIrradiateUnit = SpawnUnit(IrradiateEM, {1024.0f, 0.0f}, 0, false);
    auto* LethalIrradiate =
        IrradiateEM.GetComponent<Game::Components::UnitComponent>(LethalIrradiateUnit);
    LethalIrradiate->ChkUnitID = 0;
    LethalIrradiate->HitPoints = 40;
    LethalIrradiate->MaxHitPoints = 40;
    IrradiateEM.AddComponent<Game::Components::StatusEffectComponent>(LethalIrradiateUnit)
        .AddEffect(Game::Components::StatusEffect::Irradiate, 25.0f, LethalIrradiateUnit);
    IrradiateCombat.Update(25.0f);
    Require(!IrradiateEM.IsAlive(LethalIrradiateUnit),
        "Irradiate remains lethal after applying its full fixed-point duration");

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

    int MatrixUnit = SpawnUnit(StatusDamageEM, {160.0f, 0.0f}, 0, false);
    auto* MatrixUnitData =
        StatusDamageEM.GetComponent<Game::Components::UnitComponent>(MatrixUnit);
    MatrixUnitData->HitPoints = 100;
    MatrixUnitData->MaxHitPoints = 100;
    MatrixUnitData->Armor = 0;
    MatrixUnitData->DefensiveMatrixPoints = 250;
    StatusDamageEM.AddComponent<Game::Components::StatusEffectComponent>(MatrixUnit)
        .AddEffect(Game::Components::StatusEffect::DefensiveMatrix, 35.0f, -1);

    Game::Components::CombatComponent AttackerCombat;
    AttackerCombat.GroundDmgType = Game::Components::DamageType::Normal;
    AttackerCombat.GroundDamage = 50;

    Game::Systems::CombatSystem::ApplyDamage(
        StatusDamageEM, MatrixUnit, AttackerCombat, -1, 50);
    Require(MatrixUnitData->DefensiveMatrixPoints == 200,
        "Defensive Matrix absorbs incoming damage and updates matrix points");
    Require(MatrixUnitData->HitPoints == 100 && MatrixUnitData->HitPointSubDamage == 128,
        "Defensive Matrix limits penetrated damage to 0.5 points (128 subdamage) per hit");

    Game::Systems::CombatSystem::ApplyDamage(
        StatusDamageEM, MatrixUnit, AttackerCombat, -1, 50);
    Require(MatrixUnitData->DefensiveMatrixPoints == 150,
        "Second hit further reduces Defensive Matrix points");
    Require(MatrixUnitData->HitPoints == 99 && MatrixUnitData->HitPointSubDamage == 0,
        "Two scratch hits accumulate to 1 full point of vitality damage");

    StatusDamageEM.GetComponent<Game::Components::StatusEffectComponent>(MatrixUnit)
        ->AddEffect(Game::Components::StatusEffect::EMP, 1.0f, -1);
    StatusDamageCombat.Update(0.1f);
    Require(MatrixUnitData->DefensiveMatrixPoints == 0,
        "EMP clears Defensive Matrix points");
    Require(!StatusDamageEM.GetComponent<Game::Components::StatusEffectComponent>(MatrixUnit)
                ->HasEffect(Game::Components::StatusEffect::DefensiveMatrix),
        "EMP removes Defensive Matrix status effect");

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

    Core::ECS::SystemManager SplashSysMgr;
    auto& SplashEM = SplashSysMgr.GetEntityManager();
    Game::Systems::BulletSystem SplashBullet(SplashSysMgr);
    int SplashSource = SpawnUnit(SplashEM, {0.0f, 0.0f}, 0, false);
    int SplashTarget = SpawnUnit(SplashEM, {32.0f, 0.0f}, 1, false);
    int SplashMediumTarget = SpawnUnit(SplashEM, {80.0f, 0.0f}, 1, false);
    int SplashOuterTarget = SpawnUnit(SplashEM, {108.0f, 0.0f}, 1, false);
    int SplashAirTarget = SpawnUnit(SplashEM, {32.0f, 0.0f}, 1, true);
    auto* SplashSourceTrans =
        SplashEM.GetComponent<Game::Components::TransformComponent>(SplashSource);
    auto* SplashSourcePlayer =
        SplashEM.GetComponent<Game::Components::PlayerComponent>(SplashSource);
    auto* SplashTargetTrans =
        SplashEM.GetComponent<Game::Components::TransformComponent>(SplashTarget);
    auto* SplashTargetUnit =
        SplashEM.GetComponent<Game::Components::UnitComponent>(SplashTarget);
    auto* SplashMediumUnit =
        SplashEM.GetComponent<Game::Components::UnitComponent>(SplashMediumTarget);
    auto* SplashOuterUnit =
        SplashEM.GetComponent<Game::Components::UnitComponent>(SplashOuterTarget);
    auto* SplashAirUnit =
        SplashEM.GetComponent<Game::Components::UnitComponent>(SplashAirTarget);
    auto& SplashCombat =
        SplashEM.AddComponent<Game::Components::CombatComponent>(SplashSource);
    SplashCombat.GroundDamage = 20;
    SplashCombat.GroundDmgType = Game::Components::DamageType::Normal;
    SplashCombat.WeaponBeh = Game::Components::WeaponBehavior::AppearOnTarget;
    SplashCombat.Splash = Game::Components::SplashType::Radial;
    SplashCombat.SplashRadius = 2.0f;
    SplashCombat.IsRanged = true;
    int SplashBulletID = SplashBullet.SpawnBullet(
        SplashEM, SplashCombat, *SplashSourceTrans, *SplashSourcePlayer,
        SplashSource, 20, 0, 96.0f, SplashTarget, SplashTargetTrans->Position);
    Require(SplashBulletID >= 0, "splash bullet test spawns a projectile");
    const auto* SplashBulletTransform =
        SplashEM.GetComponent<Game::Components::TransformComponent>(
            SplashBulletID);
    Require(SplashBulletTransform &&
            SplashBulletTransform->Position.X == SplashTargetTrans->Position.X &&
            SplashBulletTransform->Position.Y == SplashTargetTrans->Position.Y,
        "appear-at-target projectile starts at the target instead of flashing at the attacker");
    SplashBullet.Update(0.1f);
    Require(SplashTargetUnit && SplashTargetUnit->HitPoints == 20,
        "splash direct target takes full damage without an extra impact hit");
    Require(SplashMediumUnit && SplashMediumUnit->HitPoints == 30,
        "splash medium radius takes half damage");
    Require(SplashOuterUnit && SplashOuterUnit->HitPoints == 35,
        "splash outer radius takes quarter damage");
    Require(SplashAirUnit && SplashAirUnit->HitPoints == 40,
        "ground splash does not cross elevation and damage air units");

    Core::ECS::SystemManager BounceSysMgr;
    auto& BounceEM = BounceSysMgr.GetEntityManager();
    Game::Systems::BulletSystem BounceBullet(BounceSysMgr);
    int BounceSource = SpawnUnit(BounceEM, {0.0f, 0.0f}, 0, true);
    int BounceFirst = SpawnUnit(BounceEM, {32.0f, 0.0f}, 1, false);
    int BounceSecond = SpawnUnit(BounceEM, {64.0f, 0.0f}, 1, false);
    int BounceThird = SpawnUnit(BounceEM, {96.0f, 0.0f}, 1, true);
    int BounceOutside = SpawnUnit(BounceEM, {256.0f, 0.0f}, 1, false);
    auto* BounceSourceTrans =
        BounceEM.GetComponent<Game::Components::TransformComponent>(BounceSource);
    auto* BounceSourcePlayer =
        BounceEM.GetComponent<Game::Components::PlayerComponent>(BounceSource);
    auto* BounceFirstTrans =
        BounceEM.GetComponent<Game::Components::TransformComponent>(BounceFirst);
    auto& BounceCombat =
        BounceEM.AddComponent<Game::Components::CombatComponent>(BounceSource);
    BounceCombat.GroundDamage = 9;
    BounceCombat.AirDamage = 9;
    BounceCombat.GroundDmgType = Game::Components::DamageType::Normal;
    BounceCombat.AirDmgType = Game::Components::DamageType::Normal;
    BounceCombat.WeaponBeh = Game::Components::WeaponBehavior::Bounce;
    BounceCombat.CanTargetGround = true;
    BounceCombat.CanTargetAir = true;
    BounceCombat.IsRanged = true;
    Game::Systems::BulletSystem::SetVisualResolver(
        [](int, bool)
            -> std::optional<Game::Systems::BulletSystem::VisualSpec> {
            Game::Systems::BulletSystem::VisualSpec Spec;
            Spec.ImageID = 555u;
            Spec.Speed = 300.0f;
            Spec.HasImpactAnimation = true;
            Spec.BulletType = 7;
            Spec.LifetimeSeconds = 2.5f;
            return Spec;
        });
    BounceEM.AddComponent<Game::Components::StatusEffectComponent>(BounceSecond)
        .AddEffect(Game::Components::StatusEffect::DarkSwarm, 8.0f, BounceSource);
    const int BounceBulletID = BounceBullet.SpawnBullet(
        BounceEM, BounceCombat, *BounceSourceTrans, *BounceSourcePlayer,
        BounceSource, 9, 1, 96.0f, BounceFirst, BounceFirstTrans->Position);
    Require(BounceBulletID >= 0, "Mutalisk bounce test spawns a projectile");
    BounceBullet.Update(0.11f);
    Require(BounceEM.IsAlive(BounceBulletID) &&
            BounceEM.GetComponent<Game::Components::BulletComponent>(
                BounceBulletID)->VisualAnimationOverride ==
                Game::IScriptAnims::Anim_SpecialState1,
        "Mutalisk retarget requests the authored SpecialState1 bounce animation");
    Require(BounceEM.GetEntitiesWith<Game::Components::EffectComponent>().empty(),
        "intermediate Mutalisk bounce does not incorrectly spawn a Death impact");
    for (int Tick = 0; Tick < 20 && BounceEM.IsAlive(BounceBulletID); ++Tick)
        BounceBullet.Update(0.1f);
    Require(!BounceEM.IsAlive(BounceBulletID),
        "Mutalisk projectile terminates after exactly three impacts");
    Require(BounceEM.GetEntitiesWith<Game::Components::EffectComponent>().size() == 1,
        "Mutalisk projectile spawns its Death effect only after the final bounce");
    const auto* BounceFirstUnit =
        BounceEM.GetComponent<Game::Components::UnitComponent>(BounceFirst);
    const auto* BounceSecondUnit =
        BounceEM.GetComponent<Game::Components::UnitComponent>(BounceSecond);
    const auto* BounceThirdUnit =
        BounceEM.GetComponent<Game::Components::UnitComponent>(BounceThird);
    const auto* BounceOutsideUnit =
        BounceEM.GetComponent<Game::Components::UnitComponent>(BounceOutside);
    Require(BounceFirstUnit && BounceFirstUnit->HitPoints == 30 &&
            BounceFirstUnit->HitPointSubDamage == 0,
        "Mutalisk first impact applies upgraded 10-point damage");
    Require(BounceSecondUnit && BounceSecondUnit->HitPoints == 37 &&
            BounceSecondUnit->HitPointSubDamage == 85,
        "Mutalisk second impact preserves exact damage when the initial shot did not miss");
    Require(BounceThirdUnit && BounceThirdUnit->HitPoints == 39 &&
            BounceThirdUnit->HitPointSubDamage == 28,
        "Mutalisk third impact preserves exact 10/9 fixed-point damage");
    Require(BounceOutsideUnit && BounceOutsideUnit->HitPoints == 40,
        "Mutalisk bounce excludes targets outside the 96-pixel search box");
    Game::Systems::BulletSystem::SetVisualResolver({});

    Core::ECS::SystemManager BounceMissSysMgr;
    auto& BounceMissEM = BounceMissSysMgr.GetEntityManager();
    Game::Systems::BulletSystem BounceMissBullet(BounceMissSysMgr);
    const int BounceMissSource =
        SpawnUnit(BounceMissEM, {0.0f, 0.0f}, 0, true);
    const int BounceMissFirst =
        SpawnUnit(BounceMissEM, {32.0f, 0.0f}, 1, false);
    const int BounceMissSecond =
        SpawnUnit(BounceMissEM, {64.0f, 0.0f}, 1, false);
    const int BounceMissThird =
        SpawnUnit(BounceMissEM, {96.0f, 0.0f}, 1, false);
    BounceMissEM.AddComponent<Game::Components::StatusEffectComponent>(
        BounceMissFirst).AddEffect(
            Game::Components::StatusEffect::DarkSwarm, 8.0f, BounceMissSource);
    auto& BounceMissCombat =
        BounceMissEM.AddComponent<Game::Components::CombatComponent>(BounceMissSource);
    BounceMissCombat = BounceCombat;
    const auto* BounceMissSourceTrans =
        BounceMissEM.GetComponent<Game::Components::TransformComponent>(BounceMissSource);
    const auto* BounceMissSourcePlayer =
        BounceMissEM.GetComponent<Game::Components::PlayerComponent>(BounceMissSource);
    const auto* BounceMissFirstTrans =
        BounceMissEM.GetComponent<Game::Components::TransformComponent>(BounceMissFirst);
    const int BounceMissBulletID = BounceMissBullet.SpawnBullet(
        BounceMissEM, BounceMissCombat, *BounceMissSourceTrans,
        *BounceMissSourcePlayer, BounceMissSource, 9, 1, 96.0f,
        BounceMissFirst, BounceMissFirstTrans->Position);
    Require(BounceMissBulletID >= 0,
        "Dark Swarm Mutalisk miss test spawns a projectile");
    for (int Tick = 0; Tick < 20 && BounceMissEM.IsAlive(BounceMissBulletID); ++Tick)
        BounceMissBullet.Update(0.1f);
    Require(BounceMissEM.GetComponent<Game::Components::UnitComponent>(
                BounceMissFirst)->HitPoints == 40 &&
            BounceMissEM.GetComponent<Game::Components::UnitComponent>(
                BounceMissSecond)->HitPoints == 40 &&
            BounceMissEM.GetComponent<Game::Components::UnitComponent>(
                BounceMissThird)->HitPoints == 40,
        "Mutalisk Dark Swarm miss is fixed at launch for the whole bounce chain");

    Core::ECS::SystemManager AirSplashSysMgr;
    auto& AirSplashEM = AirSplashSysMgr.GetEntityManager();
    Game::Systems::BulletSystem AirSplashBullet(AirSplashSysMgr);
    int AirSplashSource = SpawnUnit(AirSplashEM, {0.0f, 0.0f}, 0, true);
    int AirSplashPrimary = SpawnUnit(AirSplashEM, {32.0f, 0.0f}, 1, true);
    int AirSplashInner = SpawnUnit(AirSplashEM, {40.0f, 0.0f}, 1, true);
    int AirSplashOuter = SpawnUnit(AirSplashEM, {110.0f, 0.0f}, 1, true);
    int AirSplashGround = SpawnUnit(AirSplashEM, {40.0f, 0.0f}, 1, false);
    int AirSplashRescue = SpawnUnit(AirSplashEM, {40.0f, 0.0f}, 2, true);
    AirSplashEM.GetComponent<Game::Components::PlayerComponent>(AirSplashRescue)
        ->IsRescuePassive = true;
    for (int TargetID : {AirSplashPrimary, AirSplashInner, AirSplashOuter,
                         AirSplashGround, AirSplashRescue})
    {
        auto* TargetUnit =
            AirSplashEM.GetComponent<Game::Components::UnitComponent>(TargetID);
        TargetUnit->HitPoints = 200;
        TargetUnit->MaxHitPoints = 200;
        TargetUnit->SizeType = Game::Components::UnitSize::Large;
    }
    auto* AirSplashSourceTrans =
        AirSplashEM.GetComponent<Game::Components::TransformComponent>(AirSplashSource);
    auto* AirSplashSourcePlayer =
        AirSplashEM.GetComponent<Game::Components::PlayerComponent>(AirSplashSource);
    auto* AirSplashPrimaryTrans =
        AirSplashEM.GetComponent<Game::Components::TransformComponent>(AirSplashPrimary);
    auto& AirSplashCombat =
        AirSplashEM.AddComponent<Game::Components::CombatComponent>(AirSplashSource);
    AirSplashCombat.AirDamage = 6;
    AirSplashCombat.AirDmgType = Game::Components::DamageType::Explosive;
    AirSplashCombat.AirHitCount = 8;
    AirSplashCombat.WeaponBeh = Game::Components::WeaponBehavior::AppearOnTarget;
    AirSplashCombat.Splash = Game::Components::SplashType::Air;
    AirSplashCombat.SplashInnerRadius = 5.0f / 32.0f;
    AirSplashCombat.SplashMediumRadius = 50.0f / 32.0f;
    AirSplashCombat.SplashRadius = 100.0f / 32.0f;
    AirSplashCombat.CanTargetGround = false;
    AirSplashCombat.CanTargetAir = true;
    AirSplashCombat.IsRanged = true;
    const int AirSplashBulletID = AirSplashBullet.SpawnBullet(
        AirSplashEM, AirSplashCombat, *AirSplashSourceTrans,
        *AirSplashSourcePlayer, AirSplashSource, 6, 0, 192.0f,
        AirSplashPrimary, AirSplashPrimaryTrans->Position, 8);
    Require(AirSplashBulletID >= 0,
        "Valkyrie aggregate air-splash test spawns a projectile");
    AirSplashBullet.Update(0.1f);
    Require(AirSplashEM.GetComponent<Game::Components::UnitComponent>(
                AirSplashPrimary)->HitPoints == 152,
        "air-splash primary target takes eight full six-point rocket hits");
    Require(AirSplashEM.GetComponent<Game::Components::UnitComponent>(
                AirSplashInner)->HitPoints == 176,
        "air-splash secondary target takes half rather than full inner damage");
    Require(AirSplashEM.GetComponent<Game::Components::UnitComponent>(
                AirSplashOuter)->HitPoints == 188,
        "air-splash outer target takes quarter damage with fixed-point precision");
    Require(AirSplashEM.GetComponent<Game::Components::UnitComponent>(
                AirSplashGround)->HitPoints == 200 &&
            AirSplashEM.GetComponent<Game::Components::UnitComponent>(
                AirSplashRescue)->HitPoints == 200,
        "air splash excludes ground units and rescue-passive players");

    Core::ECS::SystemManager CrossElevationSysMgr;
    auto& CrossElevationEM = CrossElevationSysMgr.GetEntityManager();
    Game::Systems::BulletSystem CrossElevationBullet(CrossElevationSysMgr);
    int CrossSource = SpawnUnit(CrossElevationEM, {0.0f, 0.0f}, 0, false);
    int CrossGround = SpawnUnit(CrossElevationEM, {32.0f, 0.0f}, 1, false);
    int CrossAir = SpawnUnit(CrossElevationEM, {32.0f, 0.0f}, 1, true);
    auto* CrossSourceTrans =
        CrossElevationEM.GetComponent<Game::Components::TransformComponent>(CrossSource);
    auto* CrossSourcePlayer =
        CrossElevationEM.GetComponent<Game::Components::PlayerComponent>(CrossSource);
    auto* CrossGroundTrans =
        CrossElevationEM.GetComponent<Game::Components::TransformComponent>(CrossGround);
    auto& CrossCombat =
        CrossElevationEM.AddComponent<Game::Components::CombatComponent>(CrossSource);
    CrossCombat.GroundDamage = 10;
    CrossCombat.AirDamage = 10;
    CrossCombat.GroundDmgType = Game::Components::DamageType::Normal;
    CrossCombat.AirDmgType = Game::Components::DamageType::Normal;
    CrossCombat.WeaponBeh = Game::Components::WeaponBehavior::AppearOnTarget;
    CrossCombat.Splash = Game::Components::SplashType::EnemyRadial;
    CrossCombat.SplashInnerRadius = 3.0f / 32.0f;
    CrossCombat.SplashMediumRadius = 15.0f / 32.0f;
    CrossCombat.SplashRadius = 30.0f / 32.0f;
    CrossCombat.CanTargetGround = true;
    CrossCombat.CanTargetAir = true;
    const int CrossBulletID = CrossElevationBullet.SpawnBullet(
        CrossElevationEM, CrossCombat, *CrossSourceTrans, *CrossSourcePlayer,
        CrossSource, 10, 0, 64.0f, CrossGround, CrossGroundTrans->Position);
    Require(CrossBulletID >= 0, "cross-elevation enemy splash spawns a projectile");
    CrossElevationBullet.Update(0.1f);
    Require(CrossElevationEM.GetComponent<Game::Components::UnitComponent>(
                CrossGround)->HitPoints == 30 &&
            CrossElevationEM.GetComponent<Game::Components::UnitComponent>(
                CrossAir)->HitPoints == 30,
        "dual-target enemy splash follows weapon flags across ground and air");

    Core::ECS::SystemManager LineSplashSysMgr;
    auto& LineSplashEM = LineSplashSysMgr.GetEntityManager();
    Game::Systems::BulletSystem LineSplashBullet(LineSplashSysMgr);
    int LineSource = SpawnUnit(LineSplashEM, {0.0f, 0.0f}, 0, false);
    int LineFront = SpawnUnit(LineSplashEM, {32.0f, 0.0f}, 1, false);
    int LinePrimary = SpawnUnit(LineSplashEM, {96.0f, 0.0f}, 1, false);
    int LineBehind = SpawnUnit(LineSplashEM, {160.0f, 0.0f}, 1, false);
    int LineOffAxis = SpawnUnit(LineSplashEM, {96.0f, 40.0f}, 1, false);
    auto* LineSourceTrans =
        LineSplashEM.GetComponent<Game::Components::TransformComponent>(LineSource);
    auto* LineSourcePlayer =
        LineSplashEM.GetComponent<Game::Components::PlayerComponent>(LineSource);
    auto* LinePrimaryTrans =
        LineSplashEM.GetComponent<Game::Components::TransformComponent>(LinePrimary);
    auto& LineCombat =
        LineSplashEM.AddComponent<Game::Components::CombatComponent>(LineSource);
    LineCombat.GroundDamage = 20;
    LineCombat.GroundDmgType = Game::Components::DamageType::Normal;
    LineCombat.WeaponBeh = Game::Components::WeaponBehavior::AppearOnTarget;
    LineCombat.Splash = Game::Components::SplashType::Line;
    LineCombat.SplashInnerRadius = 20.0f / 32.0f;
    LineCombat.SplashMediumRadius = 20.0f / 32.0f;
    LineCombat.SplashRadius = 20.0f / 32.0f;
    LineCombat.CanTargetGround = true;
    LineCombat.CanTargetAir = false;
    LineCombat.IsRanged = true;
    const int LineBulletID = LineSplashBullet.SpawnBullet(
        LineSplashEM, LineCombat, *LineSourceTrans, *LineSourcePlayer,
        LineSource, 20, 0, 192.0f, LinePrimary,
        LinePrimaryTrans->Position);
    Require(LineBulletID >= 0, "Lurker line test spawns a projectile");
    LineSplashEM.DestroyEntity(LineSource);
    LineSplashBullet.Update(0.1f);
    Require(LineSplashEM.GetComponent<Game::Components::UnitComponent>(
                LineFront)->HitPoints == 20 &&
            LineSplashEM.GetComponent<Game::Components::UnitComponent>(
                LinePrimary)->HitPoints == 20 &&
            LineSplashEM.GetComponent<Game::Components::UnitComponent>(
                LineBehind)->HitPoints == 20,
        "Lurker line extends to maximum range and survives source removal");
    Require(LineSplashEM.GetComponent<Game::Components::UnitComponent>(
                LineOffAxis)->HitPoints == 40,
        "Lurker line excludes units outside the spine width");

    Core::ECS::SystemManager AuthoredSpineSysMgr;
    auto& AuthoredSpineEM = AuthoredSpineSysMgr.GetEntityManager();
    Game::Systems::BulletSystem AuthoredSpineBullet(AuthoredSpineSysMgr);
    const int AuthoredSpineSource =
        SpawnUnit(AuthoredSpineEM, {0.0f, 0.0f}, 0, false);
    const int AuthoredSpineFront =
        SpawnUnit(AuthoredSpineEM, {32.0f, 0.0f}, 1, false);
    const int AuthoredSpineMiddle =
        SpawnUnit(AuthoredSpineEM, {96.0f, 0.0f}, 1, false);
    const int AuthoredSpineBack =
        SpawnUnit(AuthoredSpineEM, {160.0f, 0.0f}, 1, false);
    const int AuthoredSpineOffAxis =
        SpawnUnit(AuthoredSpineEM, {96.0f, 40.0f}, 1, false);
    auto& AuthoredSpineCombat =
        AuthoredSpineEM.AddComponent<Game::Components::CombatComponent>(
            AuthoredSpineSource);
    AuthoredSpineCombat = LineCombat;
    Game::Systems::BulletSystem::SetVisualResolver(
        [](int, bool)
            -> std::optional<Game::Systems::BulletSystem::VisualSpec> {
            Game::Systems::BulletSystem::VisualSpec Spec;
            Spec.ImageID = 985;
            Spec.Speed = 192.0f;
            Spec.HasInitAnimation = true;
            Spec.BulletType = 9;
            Spec.LifetimeSeconds = 2.0f;
            return Spec;
        });
    const auto* AuthoredSpineSourceTrans =
        AuthoredSpineEM.GetComponent<Game::Components::TransformComponent>(
            AuthoredSpineSource);
    const auto* AuthoredSpineSourcePlayer =
        AuthoredSpineEM.GetComponent<Game::Components::PlayerComponent>(
            AuthoredSpineSource);
    const auto* AuthoredSpineBackTrans =
        AuthoredSpineEM.GetComponent<Game::Components::TransformComponent>(
            AuthoredSpineBack);
    const int AuthoredSpineController = AuthoredSpineBullet.SpawnBullet(
        AuthoredSpineEM, AuthoredSpineCombat, *AuthoredSpineSourceTrans,
        *AuthoredSpineSourcePlayer, AuthoredSpineSource, 20, 0, 192.0f,
        AuthoredSpineBack, AuthoredSpineBackTrans->Position);
    Game::Systems::BulletSystem::SetVisualResolver({});
    auto* AuthoredSpineState =
        AuthoredSpineEM.GetComponent<Game::Components::BulletComponent>(
            AuthoredSpineController);
    auto* AuthoredSpineTrans =
        AuthoredSpineEM.GetComponent<Game::Components::TransformComponent>(
            AuthoredSpineController);
    Require(AuthoredSpineState && AuthoredSpineTrans &&
            AuthoredSpineState->DamageDrivenByIScript,
        "real Lurker controller delegates damage timing to its IScript");

    AuthoredSpineTrans->Position = {32.0f, 0.0f};
    Require(AuthoredSpineBullet.ResolveIScriptDamageFrame(
                AuthoredSpineController) &&
            AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineFront)->HitPoints == 20 &&
            AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineMiddle)->HitPoints == 40,
        "first Lurker dogrddamage hits only beside the first visible spine");
    AuthoredSpineBullet.ResolveIScriptDamageFrame(AuthoredSpineController);
    Require(AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineFront)->HitPoints == 20,
        "later spine opcodes cannot damage an already-hit unit twice");

    AuthoredSpineTrans->Position = {96.0f, 0.0f};
    AuthoredSpineBullet.ResolveIScriptDamageFrame(AuthoredSpineController);
    Require(AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineMiddle)->HitPoints == 20 &&
            AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineBack)->HitPoints == 40 &&
            AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineOffAxis)->HitPoints == 40,
        "middle Lurker spine synchronizes damage to its local 20-pixel radius");

    AuthoredSpineTrans->Position = {160.0f, 0.0f};
    AuthoredSpineBullet.ResolveIScriptDamageFrame(AuthoredSpineController);
    Require(AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineBack)->HitPoints == 20,
        "advancing Lurker spine damages the rear unit only when it arrives");
    AuthoredSpineState->AwaitingLaunchSignal = false;
    AuthoredSpineTrans->Position = AuthoredSpineState->TargetPos;
    for (int Frame = 0;
         Frame < 6 && AuthoredSpineEM.IsAlive(AuthoredSpineController);
         ++Frame)
    {
        AuthoredSpineBullet.Update(1.0f / 24.0f);
    }
    Require(!AuthoredSpineEM.IsAlive(AuthoredSpineController) &&
            AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineFront)->HitPoints == 20 &&
            AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineMiddle)->HitPoints == 20 &&
            AuthoredSpineEM.GetComponent<Game::Components::UnitComponent>(
                AuthoredSpineBack)->HitPoints == 20,
        "Lurker endpoint destroys the controller without replaying whole-line damage");

    Core::ECS::SystemManager LineCapSysMgr;
    auto& LineCapEM = LineCapSysMgr.GetEntityManager();
    Game::Systems::BulletSystem LineCapBullet(LineCapSysMgr);
    const int LineCapSource = SpawnUnit(LineCapEM, {0.0f, 0.0f}, 0, false);
    const int LargeEdgeTarget = SpawnUnit(LineCapEM, {260.0f, 0.0f}, 1, false);
    const int BeyondLineTarget = SpawnUnit(LineCapEM, {249.0f, 0.0f}, 1, false);
    auto& LargeTargetPath =
        *LineCapEM.GetComponent<Game::Components::PathfindingComponent>(LargeEdgeTarget);
    LargeTargetPath.HasCollisionBounds = true;
    LargeTargetPath.CollisionLeft = 64;
    LargeTargetPath.CollisionRight = 64;
    LargeTargetPath.CollisionUp = 32;
    LargeTargetPath.CollisionDown = 32;
    auto& LineCapCombat =
        LineCapEM.AddComponent<Game::Components::CombatComponent>(LineCapSource);
    LineCapCombat = LineCombat;
    const auto* LineCapSourceTrans =
        LineCapEM.GetComponent<Game::Components::TransformComponent>(LineCapSource);
    const auto* LineCapSourcePlayer =
        LineCapEM.GetComponent<Game::Components::PlayerComponent>(LineCapSource);
    const auto* LargeEdgeTargetTrans =
        LineCapEM.GetComponent<Game::Components::TransformComponent>(LargeEdgeTarget);
    const int LineCapBulletID = LineCapBullet.SpawnBullet(
        LineCapEM, LineCapCombat, *LineCapSourceTrans, *LineCapSourcePlayer,
        LineCapSource, 20, 0, 192.0f, LargeEdgeTarget,
        LargeEdgeTargetTrans->Position);
    Require(LineCapBulletID >= 0, "fixed-range Lurker line test spawns a projectile");
    LineCapBullet.Update(0.1f);
    Require(LineCapEM.GetComponent<Game::Components::UnitComponent>(
                LargeEdgeTarget)->HitPoints == 20,
        "Lurker line can hit the collision edge of a large target in weapon range");
    Require(LineCapEM.GetComponent<Game::Components::UnitComponent>(
                BeyondLineTarget)->HitPoints == 40,
        "large target center does not extend Lurker damage beyond maximum range");

    Core::ECS::SystemManager PersistSplashSysMgr;
    auto& PersistSplashEM = PersistSplashSysMgr.GetEntityManager();
    Game::Systems::BulletSystem PersistSplashBullet(PersistSplashSysMgr);
    int PersistSource = SpawnUnit(PersistSplashEM, {0.0f, 0.0f}, 0, false);
    int PersistTargetA = SpawnUnit(PersistSplashEM, {128.0f, 0.0f}, 1, false);
    int PersistTargetB = SpawnUnit(PersistSplashEM, {144.0f, 0.0f}, 1, false);
    auto* PersistSourceTrans =
        PersistSplashEM.GetComponent<Game::Components::TransformComponent>(PersistSource);
    auto* PersistSourcePlayer =
        PersistSplashEM.GetComponent<Game::Components::PlayerComponent>(PersistSource);
    auto* PersistTargetAUnit =
        PersistSplashEM.GetComponent<Game::Components::UnitComponent>(PersistTargetA);
    auto* PersistTargetBUnit =
        PersistSplashEM.GetComponent<Game::Components::UnitComponent>(PersistTargetB);
    PersistTargetAUnit->HitPoints = PersistTargetAUnit->MaxHitPoints = 1000;
    PersistTargetBUnit->HitPoints = PersistTargetBUnit->MaxHitPoints = 1000;
    auto& PersistCombat =
        PersistSplashEM.AddComponent<Game::Components::CombatComponent>(PersistSource);
    PersistCombat.GroundDamage = 8;
    PersistCombat.GroundDmgType = Game::Components::DamageType::Normal;
    PersistCombat.WeaponBeh = Game::Components::WeaponBehavior::Persist;
    PersistCombat.Splash = Game::Components::SplashType::Radial;
    PersistCombat.SplashRadius = 2.0f;
    PersistCombat.IsRanged = true;
    Game::Systems::BulletSystem::SetVisualResolver(
        [](int, bool)
            -> std::optional<Game::Systems::BulletSystem::VisualSpec> {
            Game::Systems::BulletSystem::VisualSpec Spec;
            Spec.BulletType = 3;
            Spec.LifetimeSeconds = 60.0f / 24.0f;
            return Spec;
        });
    int PersistBulletID = PersistSplashBullet.SpawnBullet(
        PersistSplashEM, PersistCombat, *PersistSourceTrans, *PersistSourcePlayer,
        PersistSource, 8, 0, 64.0f, -1, {128.0f, 0.0f});
    Require(PersistBulletID >= 0, "persist splash test spawns a projectile");
    const auto* PersistBulletTransform =
        PersistSplashEM.GetComponent<Game::Components::TransformComponent>(
            PersistBulletID);
    Require(PersistBulletTransform &&
            PersistBulletTransform->Position.X == 128.0f &&
            PersistBulletTransform->Position.Y == 0.0f,
        "persist-at-target projectile and visual are anchored at the target point");
    PersistSplashBullet.Update(0.1f);
    Require(PersistTargetAUnit && PersistTargetAUnit->HitPoints == 1000 &&
            PersistTargetBUnit && PersistTargetBUnit->HitPoints == 1000,
        "persist damage does not scale with sub-seven-frame update frequency");
    PersistSplashBullet.Update(0.2f);
    Require(PersistTargetAUnit && PersistTargetAUnit->HitPoints == 992 &&
            PersistTargetBUnit && PersistTargetBUnit->HitPoints == 992,
        "persist splash applies once per authored seven-frame damage tick at its target");
    PersistSplashBullet.Update(2.2f);
    Require(!PersistSplashEM.IsAlive(PersistBulletID) &&
            PersistTargetAUnit->HitPoints == 928 &&
            PersistTargetBUnit->HitPoints == 928,
        "60-frame persist lifetime preserves the classic 56-through-zero nine-tick phase");
    Game::Systems::BulletSystem::SetVisualResolver({});

    InvincibleTargetUnit->IsInvincible = false;
    Game::Systems::CombatSystem::ApplyDamage(
        StasisEM, InvincibleTarget, StasisRangedCombat, StasisAttacker, 20);
    Require(InvincibleTargetUnit && InvincibleTargetUnit->HitPoints == 20,
        "damage resumes after invincibility is removed");

    Core::ECS::SystemManager DarkSwarmSysMgr;
    auto& DarkSwarmEM = DarkSwarmSysMgr.GetEntityManager();
    Game::Systems::BulletSystem DarkSwarmBullet(DarkSwarmSysMgr);
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

    SwarmedGroundUnit->HitPoints = 40;
    SwarmedGroundUnit->HitPointSubDamage = 0;
    auto DarkSwarmSplashCombat = RangedCombat;
    DarkSwarmSplashCombat.WeaponBeh = Game::Components::WeaponBehavior::Melee;
    DarkSwarmSplashCombat.Splash = Game::Components::SplashType::Radial;
    DarkSwarmSplashCombat.SplashInnerRadius = 10.0f / 32.0f;
    DarkSwarmSplashCombat.SplashMediumRadius = 25.0f / 32.0f;
    DarkSwarmSplashCombat.SplashRadius = 40.0f / 32.0f;
    DarkSwarmSplashCombat.CanTargetGround = true;
    DarkSwarmSplashCombat.CanTargetAir = false;
    DarkSwarmSplashCombat.IsRanged = true;
    const auto* DarkSwarmSourceTrans =
        DarkSwarmEM.GetComponent<Game::Components::TransformComponent>(DarkSwarmAttacker);
    const auto* DarkSwarmSourcePlayer =
        DarkSwarmEM.GetComponent<Game::Components::PlayerComponent>(DarkSwarmAttacker);
    const auto* SwarmedGroundTrans =
        DarkSwarmEM.GetComponent<Game::Components::TransformComponent>(SwarmedGround);
    const int DarkSwarmSplashBulletID = DarkSwarmBullet.SpawnBullet(
        DarkSwarmEM, DarkSwarmSplashCombat, *DarkSwarmSourceTrans,
        *DarkSwarmSourcePlayer,
        DarkSwarmAttacker, 20, 0, 384.0f, SwarmedGround,
        SwarmedGroundTrans->Position);
    Require(DarkSwarmSplashBulletID >= 0,
        "Dark Swarm splash test spawns a projectile");
    DarkSwarmBullet.Update(0.1f);
    Require(SwarmedGroundUnit->HitPoints < 40,
        "Dark Swarm misses the direct ranged hit but does not erase splash damage");

    int SwarmedAir = SpawnUnit(DarkSwarmEM, {64.0f, 0.0f}, 1, true);
    DarkSwarmEM.AddComponent<Game::Components::StatusEffectComponent>(SwarmedAir)
        .AddEffect(Game::Components::StatusEffect::DarkSwarm, 8.0f, DarkSwarmAttacker);
    auto* SwarmedAirUnit =
        DarkSwarmEM.GetComponent<Game::Components::UnitComponent>(SwarmedAir);
    Game::Systems::CombatSystem::ApplyDamage(
        DarkSwarmEM, SwarmedAir, RangedCombat, DarkSwarmAttacker, 20);
    Require(SwarmedAirUnit && SwarmedAirUnit->HitPoints == 20,
        "Dark Swarm does not prevent ranged damage against air units");

    int SwarmedBuilding = SpawnUnit(DarkSwarmEM, {96.0f, 0.0f}, 1, false);
    auto& SwarmedBuildingState =
        DarkSwarmEM.AddComponent<Game::Components::BuildingComponent>(SwarmedBuilding);
    SwarmedBuildingState.IsBuilding = true;
    SwarmedBuildingState.UnderConstruction = false;
    DarkSwarmEM.AddComponent<Game::Components::StatusEffectComponent>(SwarmedBuilding)
        .AddEffect(Game::Components::StatusEffect::DarkSwarm, 8.0f, DarkSwarmAttacker);
    auto* SwarmedBuildingUnit =
        DarkSwarmEM.GetComponent<Game::Components::UnitComponent>(SwarmedBuilding);
    Game::Systems::CombatSystem::ApplyDamage(
        DarkSwarmEM, SwarmedBuilding, RangedCombat, DarkSwarmAttacker, 20);
    Require(SwarmedBuildingUnit && SwarmedBuildingUnit->HitPoints == 20,
        "Dark Swarm does not protect buildings from ranged damage");

    const int DynamicSwarmArea = DarkSwarmEM.CreateEntity();
    DarkSwarmEM.AddComponent<Game::Components::TransformComponent>(DynamicSwarmArea)
        .Position = {224.0f, 0.0f};
    auto& DynamicSwarm =
        DarkSwarmEM.AddComponent<Game::Components::SpellAreaComponent>(
            DynamicSwarmArea);
    DynamicSwarm.Effect = Game::Components::StatusEffect::DarkSwarm;
    DynamicSwarm.Radius = 64.0f;
    DynamicSwarm.DurationRemaining = 10.0f;
    const int DynamicSwarmTarget =
        SpawnUnit(DarkSwarmEM, {224.0f, 0.0f}, 1, false);
    auto* DynamicSwarmTargetUnit =
        DarkSwarmEM.GetComponent<Game::Components::UnitComponent>(DynamicSwarmTarget);
    auto* DynamicSwarmTargetTrans =
        DarkSwarmEM.GetComponent<Game::Components::TransformComponent>(DynamicSwarmTarget);
    Game::Systems::CombatSystem::ApplyDamage(
        DarkSwarmEM, DynamicSwarmTarget, RangedCombat, DarkSwarmAttacker, 20);
    Require(DynamicSwarmTargetUnit->HitPoints == 40,
        "a unit entering a live Dark Swarm area receives ranged protection");
    DynamicSwarmTargetTrans->Position = {400.0f, 0.0f};
    Game::Systems::CombatSystem::ApplyDamage(
        DarkSwarmEM, DynamicSwarmTarget, RangedCombat, DarkSwarmAttacker, 20);
    Require(DynamicSwarmTargetUnit->HitPoints == 20,
        "a unit leaving a Dark Swarm area immediately loses ranged protection");

    Core::ECS::SystemManager SiegeAcquireSysMgr;
    auto& SiegeAcquireEM = SiegeAcquireSysMgr.GetEntityManager();
    Game::Systems::BulletSystem SiegeAcquireBullet(SiegeAcquireSysMgr);
    Game::Systems::CombatSystem SiegeAcquireCombatSystem(
        SiegeAcquireSysMgr, SiegeAcquireBullet);
    const int SiegeAcquireTank =
        SpawnUnit(SiegeAcquireEM, {0.0f, 0.0f}, 0, false);
    auto& SiegeAcquireCombat =
        SiegeAcquireEM.AddComponent<Game::Components::CombatComponent>(SiegeAcquireTank);
    SiegeAcquireCombat.GroundDamage = 20;
    SiegeAcquireCombat.GroundDmgType = Game::Components::DamageType::Explosive;
    SiegeAcquireCombat.AttackRange = 12.0f;
    SiegeAcquireCombat.GroundAttackRange = 12.0f;
    SiegeAcquireCombat.MinimumAttackRange = 2.0f;
    SiegeAcquireCombat.AttackCooldown = 1.0f;
    SiegeAcquireCombat.GroundAttackCooldown = 1.0f;
    SiegeAcquireCombat.CanTargetGround = true;
    SiegeAcquireCombat.CanTargetAir = false;
    SiegeAcquireCombat.WeaponBeh = Game::Components::WeaponBehavior::Melee;
    const int SiegeDeadZoneEnemy =
        SpawnUnit(SiegeAcquireEM, {32.0f, 0.0f}, 1, false);
    const int SiegeRangeEnemy =
        SpawnUnit(SiegeAcquireEM, {128.0f, 0.0f}, 1, false);
    Game::Systems::CombatSystem::IssueAttackMove(
        SiegeAcquireEM, {SiegeAcquireTank}, {320.0f, 0.0f});
    SiegeAcquireCombatSystem.Update(0.2f);
    Require(SiegeAcquireEM.GetComponent<Game::Components::UnitComponent>(
                SiegeDeadZoneEnemy)->HitPoints == 40,
        "siege auto-acquisition ignores enemies inside the minimum range dead zone");
    Require(SiegeAcquireEM.GetComponent<Game::Components::UnitComponent>(
                SiegeRangeEnemy)->HitPoints < 40 &&
            SiegeAcquireCombat.TargetEntityID == SiegeRangeEnemy,
        "siege auto-acquisition selects an attackable enemy in the outer range ring");

    auto* SiegeRangeEnemyTransform =
        SiegeAcquireEM.GetComponent<Game::Components::TransformComponent>(
            SiegeRangeEnemy);
    const int SiegeReplacementEnemy =
        SpawnUnit(SiegeAcquireEM, {160.0f, 0.0f}, 1, false);
    SiegeRangeEnemyTransform->Position = {48.0f, 0.0f};
    const int MovedInsideHitPoints =
        SiegeAcquireEM.GetComponent<Game::Components::UnitComponent>(
            SiegeRangeEnemy)->HitPoints;
    SiegeAcquireCombatSystem.Update(1.0f);
    SiegeAcquireCombatSystem.Update(0.2f);
    Require(SiegeAcquireEM.GetComponent<Game::Components::UnitComponent>(
                SiegeRangeEnemy)->HitPoints == MovedInsideHitPoints,
        "attack-move releases a temporary siege target that enters the dead zone");
    Require(SiegeAcquireEM.GetComponent<Game::Components::UnitComponent>(
                SiegeReplacementEnemy)->HitPoints < 40 &&
            SiegeAcquireCombat.TargetEntityID == SiegeReplacementEnemy,
        "attack-move reacquires an outer-ring enemy after its target enters the dead zone");

    Require(ResolveMarineHitPointsAfterOneAttack(0, 0) == 34,
        "unupgraded Marine attack deals base damage through CombatSystem");
    Require(ResolveMarineHitPointsAfterOneAttack(1, 0) == 33,
        "Terran infantry weapon upgrade increases Marine combat damage");
    Require(ResolveMarineHitPointsAfterOneAttack(1, 1) == 34,
        "Terran infantry armor upgrade reduces incoming Marine damage");
    Require(ResolveMarineHitPointsAtExtendedRange(false) == 40,
        "unupgraded Marine cannot attack beyond its base range");
    Require(ResolveMarineHitPointsAtExtendedRange(true) == 34,
        "U-238 Shells extends Marine attack range by one tile");

    Core::ECS::SystemManager StimCooldownSysMgr;
    auto& StimCooldownEM = StimCooldownSysMgr.GetEntityManager();
    Game::Systems::BulletSystem StimCooldownBullet(StimCooldownSysMgr);
    Game::Systems::CombatSystem StimCooldownCombat(StimCooldownSysMgr, StimCooldownBullet);
    int StimAttacker =
        SpawnDefUnit(StimCooldownEM, "Marine", {0.0f, 0.0f}, 8);
    AddGroundOnlyWeapon(StimCooldownEM, StimAttacker);
    auto* StimAttackerCombat =
        StimCooldownEM.GetComponent<Game::Components::CombatComponent>(StimAttacker);
    Require(StimAttackerCombat != nullptr,
        "stim cooldown attacker has combat component");
    StimAttackerCombat->AttackCooldown = 1.0f;
    StimCooldownEM.AddComponent<Game::Components::StatusEffectComponent>(StimAttacker)
        .AddEffect(Game::Components::StatusEffect::Stim, 15.0f, StimAttacker);
    int StimTarget =
        SpawnDefUnit(StimCooldownEM, "Marine", {32.0f, 0.0f}, 9);
    Game::Systems::CombatSystem::IssueAttack(StimCooldownEM, {StimAttacker}, StimTarget);
    StimCooldownCombat.Update(0.2f);
    Require(StimAttackerCombat->CooldownRemaining == 0.5f,
        "Stim halves the next attack cooldown while active");

    Core::ECS::SystemManager UpgradeEffectSysMgr;
    auto& UpgradeEffectEM = UpgradeEffectSysMgr.GetEntityManager();
    Game::Systems::BulletSystem UpgradeEffectBullet(UpgradeEffectSysMgr);
    Game::Systems::CombatSystem UpgradeEffectCombat(UpgradeEffectSysMgr, UpgradeEffectBullet);

    constexpr int ZerglingUpgradePlayerID = 0;
    auto& ZerglingUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(ZerglingUpgradePlayerID);
    ZerglingUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_AdrenalGlands)] = 1;
    ZerglingUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_MetabolicBoost)] = 1;
    int UpgradedZergling =
        SpawnDefUnit(UpgradeEffectEM, "Zergling", {0.0f, 0.0f}, ZerglingUpgradePlayerID);
    AddGroundOnlyWeapon(UpgradeEffectEM, UpgradedZergling);
    auto* ZerglingCombat =
        UpgradeEffectEM.GetComponent<Game::Components::CombatComponent>(UpgradedZergling);
    Require(ZerglingCombat != nullptr, "upgraded zergling has combat component");
    ZerglingCombat->AttackCooldown = 8.0f / 24.0f;
    int ZerglingTarget =
        SpawnDefUnit(UpgradeEffectEM, "Marine", {32.0f, 0.0f}, 1);
    Game::Systems::CombatSystem::IssueAttack(
        UpgradeEffectEM, {UpgradedZergling}, ZerglingTarget);
    UpgradeEffectCombat.Update(0.2f);
    Require(std::abs(ZerglingCombat->CooldownRemaining - 5.0f / 24.0f) < 0.0001f,
        "Adrenal Glands clamps the Zergling's 8-frame cooldown to the classic 5-frame minimum");
    Require(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                UpgradeEffectEM, UpgradedZergling) == 1.5f,
        "Metabolic Boost increases Zergling movement speed multiplier");

    constexpr int HydraliskUpgradePlayerID = 5;
    auto& HydraliskUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(HydraliskUpgradePlayerID);
    HydraliskUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_GroovedSpines)] = 1;
    HydraliskUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_MuscularAugments)] = 1;
    int UpgradedHydralisk =
        SpawnDefUnit(UpgradeEffectEM, "Hydralisk", {48.0f, 0.0f}, HydraliskUpgradePlayerID);
    Require(Game::Systems::UpgradeSystem::GetRangeBonusPixels(
                UpgradeEffectEM, UpgradedHydralisk) == 32,
        "Grooved Spines increases Hydralisk range by one tile");
    Require(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                UpgradeEffectEM, UpgradedHydralisk) == 1.5f,
        "Muscular Augments increases Hydralisk movement speed multiplier");

    constexpr int UltraliskUpgradePlayerID = 10;
    auto& UltraliskUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(UltraliskUpgradePlayerID);
    UltraliskUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_AnabolicSynthesis)] = 1;
    UltraliskUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_ChitinousPlating)] = 1;
    int UpgradedUltralisk =
        SpawnDefUnit(UpgradeEffectEM, "Ultralisk", {56.0f, 0.0f}, UltraliskUpgradePlayerID);
    Require(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                UpgradeEffectEM, UpgradedUltralisk) == 1.5f,
        "Anabolic Synthesis increases Ultralisk movement speed multiplier");
    Require(Game::Systems::UpgradeSystem::GetArmorBonus(
                UpgradeEffectEM, UpgradedUltralisk) == 2,
        "Chitinous Plating gives Ultralisk two extra armor like openbw");

    constexpr int VultureUpgradePlayerID = 2;
    Game::Systems::UpgradeSystem::GetUpgrades(VultureUpgradePlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Terran_IonThrusters)] = 1;
    int UpgradedVulture =
        SpawnDefUnit(UpgradeEffectEM, "Vulture", {64.0f, 0.0f}, VultureUpgradePlayerID);
    Require(std::abs(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                         UpgradeEffectEM, UpgradedVulture) -
                     2560.0f / 1707.0f) < 0.000001f,
        "Ion Thrusters uses the exact Brood War fixed-point Vulture speed ratio");

    constexpr int GoliathUpgradePlayerID = 11;
    Game::Systems::UpgradeSystem::GetUpgrades(GoliathUpgradePlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Terran_CharonBoosters)] = 1;
    int UpgradedGoliath =
        SpawnDefUnit(UpgradeEffectEM, "Goliath", {80.0f, 0.0f}, GoliathUpgradePlayerID);
    Require(Game::Systems::UpgradeSystem::GetRangeBonusPixels(
                UpgradeEffectEM, UpgradedGoliath) == 0,
        "Charon Boosters does not increase Goliath ground range");
    Require(Game::Systems::UpgradeSystem::GetRangeBonusPixelsForTarget(
                UpgradeEffectEM, UpgradedGoliath, false) == 0,
        "Charon Boosters has no bonus against ground targets");
    Require(Game::Systems::UpgradeSystem::GetRangeBonusPixelsForTarget(
                UpgradeEffectEM, UpgradedGoliath, true) == 96,
        "Charon Boosters increases Goliath air range by three tiles");

    constexpr int SiegeUpgradePlayerID = 9;
    auto& SiegeUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(SiegeUpgradePlayerID);
    SiegeUpgrades.Levels.fill(0);
    SiegeUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Terran_VehicleWeapons)] = 1;
    SiegeUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Terran_VehicleArmor)] = 1;
    int SiegeTank = SpawnDefUnit(
        UpgradeEffectEM, "SiegeTank", {88.0f, 0.0f}, SiegeUpgradePlayerID);
    auto* SiegeTankUnit =
        UpgradeEffectEM.GetComponent<Game::Components::UnitComponent>(SiegeTank);
    SiegeTankUnit->ChkUnitID = 30;
    Require(Game::Systems::UpgradeSystem::GetWeaponBonusForTarget(
                UpgradeEffectEM, SiegeTank, false) == 5 &&
            Game::Systems::UpgradeSystem::GetArmorBonus(
                UpgradeEffectEM, SiegeTank) == 1,
        "sieged tank retains vehicle weapon and armor upgrades");

    constexpr int ZealotUpgradePlayerID = 3;
    Game::Systems::UpgradeSystem::GetUpgrades(ZealotUpgradePlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Protoss_ZealotSpeed)] = 1;
    int UpgradedZealot =
        SpawnDefUnit(UpgradeEffectEM, "Zealot", {96.0f, 0.0f}, ZealotUpgradePlayerID);
    Require(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                UpgradeEffectEM, UpgradedZealot) == 1.5f,
        "Leg Enhancements increases Zealot movement speed multiplier");

    constexpr int OverlordUpgradePlayerID = 4;
    Game::Systems::UpgradeSystem::GetUpgrades(OverlordUpgradePlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Zerg_Antennae)] = 1;
    Game::Systems::UpgradeSystem::GetUpgrades(OverlordUpgradePlayerID)
        .Levels[static_cast<size_t>(
            Game::Systems::ResearchId::Zerg_PneumatizedCarapace)] = 1;
    int UpgradedOverlord =
        SpawnDefUnit(UpgradeEffectEM, "Overlord", {128.0f, 0.0f}, OverlordUpgradePlayerID);
    Require(Game::Systems::UpgradeSystem::GetSightRangeBonusTiles(
                UpgradeEffectEM, UpgradedOverlord) == 2,
        "Antennae increases Overlord sight to eleven tiles");
    Require(std::abs(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                         UpgradeEffectEM, UpgradedOverlord) -
                     853.0f / 213.0f) < 0.000001f,
        "Pneumatized Carapace applies the classic Overlord minimum-speed clamp");

    constexpr int GhostUpgradePlayerID = 6;
    Game::Systems::UpgradeSystem::GetUpgrades(GhostUpgradePlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Terran_OcularImplants)] = 1;
    int UpgradedGhost =
        SpawnDefUnit(UpgradeEffectEM, "Ghost", {160.0f, 0.0f}, GhostUpgradePlayerID);
    Require(Game::Systems::UpgradeSystem::GetSightRangeBonusTiles(
                UpgradeEffectEM, UpgradedGhost) == 2,
        "Ocular Implants increases Ghost sight to eleven tiles");

    constexpr int ObserverUpgradePlayerID = 7;
    Game::Systems::UpgradeSystem::GetUpgrades(ObserverUpgradePlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Protoss_SensorArray)] = 1;
    Game::Systems::UpgradeSystem::GetUpgrades(ObserverUpgradePlayerID)
        .Levels[static_cast<size_t>(
            Game::Systems::ResearchId::Protoss_GraviticBoosters)] = 1;
    int UpgradedObserver =
        SpawnDefUnit(UpgradeEffectEM, "Observer", {192.0f, 0.0f}, ObserverUpgradePlayerID);
    Require(Game::Systems::UpgradeSystem::GetSightRangeBonusTiles(
                UpgradeEffectEM, UpgradedObserver) == 2,
        "Sensor Array increases Observer sight to eleven tiles");
    Require(std::abs(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                         UpgradeEffectEM, UpgradedObserver) -
                     1279.0f / 853.0f) < 0.000001f,
        "Gravitic Boosters uses the exact Observer fixed-point speed ratio");

    constexpr int ScoutUpgradePlayerID = 8;
    Game::Systems::UpgradeSystem::GetUpgrades(ScoutUpgradePlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Protoss_ApialSensors)] = 1;
    Game::Systems::UpgradeSystem::GetUpgrades(ScoutUpgradePlayerID)
        .Levels[static_cast<size_t>(
            Game::Systems::ResearchId::Protoss_GraviticThrusters)] = 1;
    int UpgradedScout =
        SpawnDefUnit(UpgradeEffectEM, "Scout", {224.0f, 0.0f}, ScoutUpgradePlayerID);
    Require(Game::Systems::UpgradeSystem::GetSightRangeBonusTiles(
                UpgradeEffectEM, UpgradedScout) == 3,
        "Apial Sensors increases Scout sight to eleven tiles");
    Require(std::abs(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                         UpgradeEffectEM, UpgradedScout) -
                     1707.0f / 1280.0f) < 0.000001f,
        "Gravitic Thrusters uses the Scout-specific fixed-point top speed");

    constexpr int ShuttleUpgradePlayerID = 1;
    Game::Systems::UpgradeSystem::GetUpgrades(ShuttleUpgradePlayerID)
        .Levels[static_cast<size_t>(Game::Systems::ResearchId::Protoss_GraviticDrive)] = 1;
    const int UpgradedShuttle =
        SpawnDefUnit(UpgradeEffectEM, "Shuttle", {256.0f, 0.0f}, ShuttleUpgradePlayerID);
    Require(std::abs(Game::Systems::UpgradeSystem::GetMovementSpeedMultiplier(
                         UpgradeEffectEM, UpgradedShuttle) -
                     1699.0f / 1133.0f) < 0.000001f,
        "Gravitic Drive uses the exact Shuttle fixed-point speed ratio");

    Core::ECS::SystemManager UpgradeRuleSysMgr;
    auto& UpgradeRuleEM = UpgradeRuleSysMgr.GetEntityManager();
    constexpr int TerranRulePlayerID = 2;
    constexpr int ZergRulePlayerID = 4;
    constexpr int ProtossRulePlayerID = 8;
    const auto SavedTerranRuleUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(TerranRulePlayerID);
    const auto SavedZergRuleUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(ZergRulePlayerID);
    const auto SavedProtossRuleUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(ProtossRulePlayerID);
    const auto SavedTerranRuleResources =
        Game::Systems::HarvestSystem::GetResources(TerranRulePlayerID);
    const auto SavedZergRuleResources =
        Game::Systems::HarvestSystem::GetResources(ZergRulePlayerID);
    const auto SavedProtossRuleResources =
        Game::Systems::HarvestSystem::GetResources(ProtossRulePlayerID);
    Game::Systems::UpgradeSystem::GetUpgrades(TerranRulePlayerID).Levels.fill(0);
    Game::Systems::UpgradeSystem::GetUpgrades(ZergRulePlayerID).Levels.fill(0);
    Game::Systems::UpgradeSystem::GetUpgrades(ProtossRulePlayerID).Levels.fill(0);
    Game::Systems::HarvestSystem::GetResources(TerranRulePlayerID).Minerals = 5000;
    Game::Systems::HarvestSystem::GetResources(TerranRulePlayerID).VespeneGas = 5000;
    Game::Systems::HarvestSystem::GetResources(ZergRulePlayerID).Minerals = 5000;
    Game::Systems::HarvestSystem::GetResources(ZergRulePlayerID).VespeneGas = 5000;
    Game::Systems::HarvestSystem::GetResources(ProtossRulePlayerID).Minerals = 5000;
    Game::Systems::HarvestSystem::GetResources(ProtossRulePlayerID).VespeneGas = 5000;

    const int RuleEngineeringBay = SpawnDefUnit(
        UpgradeRuleEM, "EngineeringBay", {0.0f, 0.0f}, TerranRulePlayerID, true);
    auto& TerranRuleUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(TerranRulePlayerID);
    TerranRuleUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Terran_InfantryWeapons)] = 1;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "Terran level-two upgrades require a completed Science Facility");
    const int RuleScienceFacility = SpawnDefUnit(
        UpgradeRuleEM, "ScienceFacility", {32.0f, 0.0f}, TerranRulePlayerID, true);
    auto* RuleScienceBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(
            RuleScienceFacility);
    RuleScienceBuilding->UnderConstruction = true;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "an unfinished Science Facility does not satisfy upgrade prerequisites");
    RuleScienceBuilding->UnderConstruction = false;
    Require(Game::Systems::UpgradeSystem::StartResearch(
                UpgradeRuleEM, RuleEngineeringBay,
                Game::Systems::ResearchId::Terran_InfantryWeapons),
        "completed Science Facility unlocks Terran level-two upgrades");
    auto* RuleEngineeringBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(
            RuleEngineeringBay);
    Require(RuleEngineeringBuilding &&
            std::abs(RuleEngineeringBuilding->BuildTimeRemaining -
                     4480.0f / 24.0f) < 0.0001f,
        "core level-two research uses its exact 4480-frame duration");
    Require(Game::Systems::UpgradeSystem::GetResearchTimeCost(
                Game::Systems::ResearchId::Terran_InfantryWeapons,
                TerranRulePlayerID) == 187,
        "whole-second metadata rounds the exact level-two duration up");
    Require(Game::Systems::UpgradeSystem::CancelResearch(
                UpgradeRuleEM, RuleEngineeringBay),
        "level-two research timer probe cancels cleanly");
    const int RuleMachineShop = SpawnDefUnit(
        UpgradeRuleEM, "MachineShop", {64.0f, 0.0f}, TerranRulePlayerID, true);
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleMachineShop,
                Game::Systems::ResearchId::Terran_CharonBoosters),
        "Charon Boosters remains locked without an Armory");
    SpawnDefUnit(UpgradeRuleEM, "Armory", {96.0f, 0.0f}, TerranRulePlayerID, true);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleMachineShop,
                Game::Systems::ResearchId::Terran_CharonBoosters),
        "completed Armory unlocks Charon Boosters");

    const int RuleEvolutionChamber = SpawnDefUnit(
        UpgradeRuleEM, "Evolution Chamber", {0.0f, 64.0f}, ZergRulePlayerID, true);
    const int RuleSpawningPool = SpawnDefUnit(
        UpgradeRuleEM, "Spawning Pool", {32.0f, 64.0f}, ZergRulePlayerID, true);
    const int RuleHydraliskDen = SpawnDefUnit(
        UpgradeRuleEM, "Hydralisk Den", {64.0f, 64.0f}, ZergRulePlayerID, true);
    auto& ZergRuleUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(ZergRulePlayerID);
    ZergRuleUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_MeleeAttacks)] = 1;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEvolutionChamber,
                Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "Zerg level-two attack waits for Lair technology");
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleSpawningPool,
                Game::Systems::ResearchId::Zerg_AdrenalGlands),
        "Adrenal Glands waits for a completed Hive");
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleHydraliskDen,
                Game::Systems::ResearchId::Zerg_LurkerMorph),
        "Lurker Aspect waits for a completed Lair or Hive");
    const int RuleHive = SpawnDefUnit(
        UpgradeRuleEM, "Hive", {96.0f, 64.0f}, ZergRulePlayerID, true);
    auto* RuleHiveBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(RuleHive);
    RuleHiveBuilding->UnderConstruction = true;
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEvolutionChamber,
                Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "a morphing Hive preserves OpenBW's Zerg level-two prerequisite");
    ZergRuleUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_MeleeAttacks)] = 2;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEvolutionChamber,
                Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "Zerg level-three attack waits for Hive completion");
    SpawnDefUnit(UpgradeRuleEM, "Lair", {128.0f, 64.0f}, ZergRulePlayerID, true);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleHydraliskDen,
                Game::Systems::ResearchId::Zerg_LurkerMorph),
        "completed Lair unlocks Lurker Aspect");
    RuleHiveBuilding->UnderConstruction = false;
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleEvolutionChamber,
                Game::Systems::ResearchId::Zerg_MeleeAttacks),
        "completed Hive unlocks Zerg level-three attack");
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleSpawningPool,
                Game::Systems::ResearchId::Zerg_AdrenalGlands),
        "completed Hive unlocks Adrenal Glands");

    const int RuleForge = SpawnDefUnit(
        UpgradeRuleEM, "Forge", {0.0f, 128.0f}, ProtossRulePlayerID, true);
    auto& ProtossRuleUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(ProtossRulePlayerID);
    ProtossRuleUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_GroundWeapons)] = 1;
    ProtossRuleUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_PlasmaShields)] = 1;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleForge,
                Game::Systems::ResearchId::Protoss_GroundWeapons),
        "Protoss level-two ground upgrades require Templar Archives");
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleForge,
                Game::Systems::ResearchId::Protoss_PlasmaShields),
        "Protoss level-two shields require Cybernetics Core");
    const int RuleCyberCore = SpawnDefUnit(
        UpgradeRuleEM, "Cybernetics Core", {32.0f, 128.0f}, ProtossRulePlayerID, true);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleForge,
                Game::Systems::ResearchId::Protoss_PlasmaShields),
        "completed Cybernetics Core unlocks Protoss level-two shields");
    ProtossRuleUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Protoss_AirWeapons)] = 1;
    Require(!Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleCyberCore,
                Game::Systems::ResearchId::Protoss_AirWeapons),
        "Protoss level-two air upgrades require Fleet Beacon");
    SpawnDefUnit(UpgradeRuleEM, "Templar Archives", {64.0f, 128.0f},
                 ProtossRulePlayerID, true);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleForge,
                Game::Systems::ResearchId::Protoss_GroundWeapons),
        "completed Templar Archives unlocks Protoss level-two ground upgrades");
    const int RuleFleetBeacon = SpawnDefUnit(
        UpgradeRuleEM, "Fleet Beacon", {96.0f, 128.0f}, ProtossRulePlayerID, true);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleCyberCore,
                Game::Systems::ResearchId::Protoss_AirWeapons),
        "completed Fleet Beacon unlocks Protoss level-two air upgrades");
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleFleetBeacon,
                Game::Systems::ResearchId::Protoss_GraviticThrusters),
        "Fleet Beacon exposes Gravitic Thrusters research");
    Require(Game::Systems::UpgradeSystem::StartResearch(
                UpgradeRuleEM, RuleFleetBeacon,
                Game::Systems::ResearchId::Protoss_DisruptionWeb),
        "Disruption Web has complete research metadata");
    auto* RuleFleetBuilding =
        UpgradeRuleEM.GetComponent<Game::Components::BuildingComponent>(RuleFleetBeacon);
    Require(RuleFleetBuilding &&
            std::abs(RuleFleetBuilding->BuildTimeRemaining - 1200.0f / 24.0f) <
                0.0001f,
        "Disruption Web uses the exact 1200-frame duration");
    Require(Game::Systems::UpgradeSystem::CancelResearch(
                UpgradeRuleEM, RuleFleetBeacon),
        "Disruption Web timer probe cancels cleanly");
    const int RuleObservatory = SpawnDefUnit(
        UpgradeRuleEM, "Observatory", {128.0f, 128.0f}, ProtossRulePlayerID, true);
    Require(Game::Systems::UpgradeSystem::CanStartResearch(
                UpgradeRuleEM, RuleObservatory,
                Game::Systems::ResearchId::Protoss_GraviticBoosters),
        "Observatory exposes Gravitic Boosters research");

    Game::Systems::UpgradeSystem::GetUpgrades(TerranRulePlayerID) =
        SavedTerranRuleUpgrades;
    Game::Systems::UpgradeSystem::GetUpgrades(ZergRulePlayerID) =
        SavedZergRuleUpgrades;
    Game::Systems::UpgradeSystem::GetUpgrades(ProtossRulePlayerID) =
        SavedProtossRuleUpgrades;
    Game::Systems::HarvestSystem::GetResources(TerranRulePlayerID) =
        SavedTerranRuleResources;
    Game::Systems::HarvestSystem::GetResources(ZergRulePlayerID) =
        SavedZergRuleResources;
    Game::Systems::HarvestSystem::GetResources(ProtossRulePlayerID) =
        SavedProtossRuleResources;

    Core::ECS::SystemManager BurrowCombatSysMgr;
    auto& BurrowCombatEM = BurrowCombatSysMgr.GetEntityManager();
    Game::Systems::BulletSystem BurrowCombatBullet(BurrowCombatSysMgr);
    Game::Systems::CombatSystem BurrowCombat(BurrowCombatSysMgr, BurrowCombatBullet);

    const int BurrowedHydralisk =
        SpawnDefUnit(BurrowCombatEM, "Hydralisk", {0.0f, 0.0f}, 0);
    AddGroundOnlyWeapon(BurrowCombatEM, BurrowedHydralisk);
    auto& HydraliskBurrow =
        BurrowCombatEM.AddComponent<Game::Components::BurrowComponent>(BurrowedHydralisk);
    HydraliskBurrow.CanBurrow = true;
    HydraliskBurrow.IsBurrowed = true;
    HydraliskBurrow.Phase = Game::Components::BurrowPhase::Burrowed;
    const int HydraliskTarget =
        SpawnDefUnit(BurrowCombatEM, "Marine", {32.0f, 0.0f}, 1);
    const int HydraliskTargetHP =
        BurrowCombatEM.GetComponent<Game::Components::UnitComponent>(HydraliskTarget)->HitPoints;
    Game::Systems::CombatSystem::IssueAttack(
        BurrowCombatEM, {BurrowedHydralisk}, HydraliskTarget);
    BurrowCombat.Update(0.2f);
    Require(BurrowCombatEM.GetComponent<Game::Components::UnitComponent>(HydraliskTarget)->HitPoints ==
                HydraliskTargetHP,
        "ordinary burrowed Zerg units cannot attack while underground");

    const int Lurker = SpawnDefUnit(BurrowCombatEM, "Lurker", {0.0f, 64.0f}, 0);
    AddGroundOnlyWeapon(BurrowCombatEM, Lurker);
    auto& LurkerBurrow =
        BurrowCombatEM.AddComponent<Game::Components::BurrowComponent>(Lurker);
    LurkerBurrow.CanBurrow = true;
    const int LurkerTarget =
        SpawnDefUnit(BurrowCombatEM, "Marine", {32.0f, 64.0f}, 1);
    auto* LurkerTargetUnit =
        BurrowCombatEM.GetComponent<Game::Components::UnitComponent>(LurkerTarget);
    const int LurkerTargetHP = LurkerTargetUnit->HitPoints;
    Game::Systems::CombatSystem::IssueAttack(BurrowCombatEM, {Lurker}, LurkerTarget);
    BurrowCombat.Update(0.2f);
    Require(LurkerTargetUnit->HitPoints == LurkerTargetHP,
        "Lurker cannot attack before completing its Burrow transition");
    LurkerBurrow.IsBurrowed = true;
    LurkerBurrow.Phase = Game::Components::BurrowPhase::Burrowed;
    BurrowCombat.Update(0.2f);
    Require(LurkerTargetUnit->HitPoints < LurkerTargetHP,
        "Lurker attacks after its project Burrow state is complete");

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

    OverlordSupply.SupplyUsed = 9;
    OverlordSupply.SupplyMax = 17;
    const int HallucinatedOverlord = SpawnDefUnit(
        SupplyDeathEM, "Overlord", {80.0f, 0.0f}, OverlordSupplyPlayerID);
    SupplyDeathEM.AddComponent<Game::Components::RenderComponent>(
        HallucinatedOverlord).ImageID = 100u;
    SupplyDeathEM.GetComponent<Game::Components::UnitComponent>(
        HallucinatedOverlord)->SupplyCost = 0;
    SupplyDeathEM.AddComponent<Game::Components::StatusEffectComponent>(
        HallucinatedOverlord).AddEffect(
            Game::Components::StatusEffect::Hallucination, 180.0f);
    Game::Systems::CombatSystem::ApplyDamage(
        SupplyDeathEM, HallucinatedOverlord, SupplyKillerCombat,
        SupplyKiller, 9999);
    Require(!SupplyDeathEM.IsAlive(HallucinatedOverlord) &&
            OverlordSupply.SupplyUsed == 9 &&
            OverlordSupply.SupplyMax == 17,
        "hallucination death never releases supply or removes provider capacity");
    int HallucinationDeathEffect = -1;
    for (int EffectID : SupplyDeathEM.GetEntitiesWith<
             Game::Components::EffectComponent,
             Game::Components::RenderComponent>())
    {
        const auto* Effect =
            SupplyDeathEM.GetComponent<Game::Components::EffectComponent>(EffectID);
        const auto* Render =
            SupplyDeathEM.GetComponent<Game::Components::RenderComponent>(EffectID);
        if (Effect && Render && Effect->SpawnSoundID == 619 &&
            Render->ImageID == 557u)
        {
            HallucinationDeathEffect = EffectID;
            break;
        }
    }
    Require(HallucinationDeathEffect >= 0 &&
            !SupplyDeathEM.GetComponent<Game::Components::PathfindingComponent>(
                HallucinationDeathEffect),
        "killed hallucination creates the dedicated visual and one-shot sound without a collision path");

    Core::ECS::SystemManager HallucinationExpirySysMgr;
    auto& HallucinationExpiryEM =
        HallucinationExpirySysMgr.GetEntityManager();
    Game::Systems::BulletSystem HallucinationExpiryBullet(
        HallucinationExpirySysMgr);
    Game::Systems::CombatSystem HallucinationExpiryCombat(
        HallucinationExpirySysMgr, HallucinationExpiryBullet);
    constexpr int HallucinationExpiryPlayerID = 6;
    auto& HallucinationExpirySupply =
        Game::Systems::HarvestSystem::GetResources(
            HallucinationExpiryPlayerID);
    HallucinationExpirySupply.SupplyUsed = 7;
    HallucinationExpirySupply.SupplyMax = 20;
    const int ExpiringHallucination = SpawnDefUnit(
        HallucinationExpiryEM, "Dragoon", {0.0f, 0.0f},
        HallucinationExpiryPlayerID);
    HallucinationExpiryEM.AddComponent<Game::Components::RenderComponent>(
        ExpiringHallucination).ImageID = 100u;
    HallucinationExpiryEM.GetComponent<Game::Components::UnitComponent>(
        ExpiringHallucination)->SupplyCost = 0;
    HallucinationExpiryEM.AddComponent<
        Game::Components::StatusEffectComponent>(ExpiringHallucination)
        .AddEffect(Game::Components::StatusEffect::Hallucination, 180.0f);
    HallucinationExpiryCombat.Update(179.9f);
    Require(HallucinationExpiryEM.IsAlive(ExpiringHallucination),
        "hallucination remains active immediately before its 180-second expiry");
    HallucinationExpiryCombat.Update(0.2f);
    Require(!HallucinationExpiryEM.IsAlive(ExpiringHallucination) &&
            HallucinationExpirySupply.SupplyUsed == 7 &&
            HallucinationExpirySupply.SupplyMax == 20,
        "hallucination expires as a non-economic visual copy at 180 seconds");
    bool ExpiryUsedNormalDeath = false;
    for (int EffectID : HallucinationExpiryEM.GetEntitiesWith<
             Game::Components::EffectComponent,
             Game::Components::RenderComponent>())
    {
        const auto* Effect = HallucinationExpiryEM.GetComponent<
            Game::Components::EffectComponent>(EffectID);
        ExpiryUsedNormalDeath = ExpiryUsedNormalDeath ||
            (Effect &&
             Effect->InitialAnimation == Game::IScriptAnims::Anim_Death &&
             Effect->SpawnSoundID < 0);
    }
    Require(ExpiryUsedNormalDeath,
        "natural hallucination expiry uses the unit Death IScript rather than the killed-copy burst");

    const int HiddenExpiringHallucination = SpawnDefUnit(
        HallucinationExpiryEM, "Dragoon", {32.0f, 0.0f},
        HallucinationExpiryPlayerID);
    HallucinationExpiryEM.AddComponent<Game::Components::RenderComponent>(
        HiddenExpiringHallucination).ImageID = 100u;
    HallucinationExpiryEM.GetComponent<Game::Components::UnitComponent>(
        HiddenExpiringHallucination)->SupplyCost = 0;
    HallucinationExpiryEM.GetComponent<Game::Components::RenderComponent>(
        HiddenExpiringHallucination)->Visible = false;
    HallucinationExpiryEM.AddComponent<
        Game::Components::StatusEffectComponent>(HiddenExpiringHallucination)
        .AddEffect(Game::Components::StatusEffect::Hallucination, 180.0f);
    HallucinationExpiryCombat.Update(180.1f);
    bool HiddenExpiryUsedBurst = false;
    for (int EffectID : HallucinationExpiryEM.GetEntitiesWith<
             Game::Components::EffectComponent,
             Game::Components::RenderComponent>())
    {
        const auto* Effect = HallucinationExpiryEM.GetComponent<
            Game::Components::EffectComponent>(EffectID);
        const auto* Render = HallucinationExpiryEM.GetComponent<
            Game::Components::RenderComponent>(EffectID);
        HiddenExpiryUsedBurst = HiddenExpiryUsedBurst ||
            (Effect && Render && Effect->SpawnSoundID == 619 &&
             Render->ImageID == 557u && Render->Visible);
    }
    Require(!HallucinationExpiryEM.IsAlive(HiddenExpiringHallucination) &&
            HiddenExpiryUsedBurst,
        "hidden hallucination expiry uses the visible death burst and sound 619");

    constexpr int DepotSupplyPlayerID = 4;
    auto& DepotSupply =
        Game::Systems::HarvestSystem::GetResources(DepotSupplyPlayerID);
    DepotSupply.SupplyUsed = 8;
    DepotSupply.SupplyMax = 18;
    int SupplyDepot =
        SpawnDefUnit(SupplyDeathEM, "SupplyDepot", {96.0f, 0.0f},
                     DepotSupplyPlayerID, true);
    SupplyDeathEM.AddComponent<Game::Components::RenderComponent>(SupplyDepot)
        .ImageID = 100u;
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
    bool HasScriptedDepotDeath = false;
    const int SupplyDepotChkID = Game::Races::GetUnitDefByID(
        Game::Races::GetUnitDefID("SupplyDepot")).ChkUnitID;
    for (int EntityID : SupplyDeathEM.GetEntitiesWith<
             Game::Components::EffectComponent,
             Game::Components::UnitComponent>())
    {
        const auto* EffectUnit =
            SupplyDeathEM.GetComponent<Game::Components::UnitComponent>(EntityID);
        const auto* Effect =
            SupplyDeathEM.GetComponent<Game::Components::EffectComponent>(EntityID);
        HasScriptedDepotDeath = HasScriptedDepotDeath ||
            (EffectUnit && EffectUnit->ChkUnitID == SupplyDepotChkID && Effect &&
             Effect->InitialAnimation == Game::IScriptAnims::Anim_Death &&
             Effect->DestroyWhenAnimationCompletes &&
             !SupplyDeathEM.GetComponent<
                 Game::Components::PathfindingComponent>(EntityID));
    }
    Require(HasScriptedDepotDeath,
        "destroyed building body is a collision-free, IScript-driven Death effect");

    Core::ECS::SystemManager FormationSysMgr;
    auto& FormationEM = FormationSysMgr.GetEntityManager();
    Game::Systems::BulletSystem FormationBullet(FormationSysMgr);
    Game::Systems::CombatSystem FormationCombat(FormationSysMgr, FormationBullet);
    const int FrontA = SpawnDefUnit(
        FormationEM, "Marine", {0.0f, -24.0f}, 0);
    const int FrontB = SpawnDefUnit(
        FormationEM, "Marine", {0.0f, 24.0f}, 0);
    const int BackLine = SpawnDefUnit(
        FormationEM, "Marine", {-16.0f, 0.0f}, 0);
    AddGroundOnlyWeapon(FormationEM, FrontA);
    AddGroundOnlyWeapon(FormationEM, FrontB);
    AddGroundOnlyWeapon(FormationEM, BackLine);
    auto* BackLineCombat =
        FormationEM.GetComponent<Game::Components::CombatComponent>(BackLine);
    Require(BackLineCombat, "combat formation ranged unit has weapon state");
    BackLineCombat->IsRanged = true;
    BackLineCombat->WeaponBeh = Game::Components::WeaponBehavior::Missile;
    const int FormationEnemy = SpawnDefUnit(
        FormationEM, "Marine", {240.0f, 0.0f}, 1);
    Game::Systems::CombatSystem::IssueAttack(
        FormationEM, {FrontA, FrontB, BackLine}, FormationEnemy);
    FormationCombat.Update(0.1f);

    const auto* FrontAPath =
        FormationEM.GetComponent<Game::Components::PathfindingComponent>(FrontA);
    const auto* FrontBPath =
        FormationEM.GetComponent<Game::Components::PathfindingComponent>(FrontB);
    const auto* BackLinePath =
        FormationEM.GetComponent<Game::Components::PathfindingComponent>(BackLine);
    Require(FrontAPath && FrontBPath && BackLinePath &&
            !FrontAPath->Path.empty() && !FrontBPath->Path.empty() &&
            !BackLinePath->Path.empty(),
        "all allied attackers receive combat approach slots");
    const auto FrontATarget = FrontAPath->Path.back();
    const auto FrontBTarget = FrontBPath->Path.back();
    const auto BackLineTarget = BackLinePath->Path.back();
    Require(std::fabs(FrontATarget.Y - FrontBTarget.Y) > 20.0f,
        "melee combatants spread laterally instead of stacking on the enemy center");
    Require(BackLineTarget.X < std::min(FrontATarget.X, FrontBTarget.X),
        "ranged combatants automatically form behind the front line");

    // Test active cloaking energy drain and automatic decloak upon exhaustion
    Core::ECS::SystemManager CloakSysMgr;
    auto& CloakEM = CloakSysMgr.GetEntityManager();
    Game::Systems::BulletSystem CloakBullet(CloakSysMgr);
    Game::Systems::CombatSystem CloakCombat(CloakSysMgr, CloakBullet);
    const int CloakedWraith = SpawnDefUnit(CloakEM, "Wraith", {0.0f, 0.0f}, 0);
    auto* WraithUnit = CloakEM.GetComponent<Game::Components::UnitComponent>(CloakedWraith);
    WraithUnit->EnergyPoints = 2;
    WraithUnit->MaxEnergyPoints = 200;
    auto& WraithCloak = CloakEM.AddComponent<Game::Components::CloakComponent>(CloakedWraith);
    WraithCloak.IsCloaked = true;
    WraithCloak.CloakEnergyCost = 2.0f;
    CloakCombat.Update(0.5f);
    Require(WraithUnit->EnergyPoints == 1 && WraithCloak.IsCloaked,
        "active cloaking drains energy over time while remaining cloaked");
    CloakCombat.Update(0.6f);
    Require(WraithUnit->EnergyPoints == 0 && !WraithCloak.IsCloaked,
        "cloaked unit automatically decloaks when energy is depleted");

    // Test Terran building burn-down when HP < 33%
    Core::ECS::SystemManager BurnSysMgr;
    auto& BurnEM = BurnSysMgr.GetEntityManager();
    Game::Systems::BulletSystem BurnBullet(BurnSysMgr);
    Game::Systems::CombatSystem BurnCombat(BurnSysMgr, BurnBullet);
    const int BurningBarracks = SpawnDefUnit(BurnEM, "Barracks", {100.0f, 100.0f}, 0, true);
    auto* BarracksUnit = BurnEM.GetComponent<Game::Components::UnitComponent>(BurningBarracks);
    auto* BarracksBuilding = BurnEM.GetComponent<Game::Components::BuildingComponent>(BurningBarracks);
    BarracksBuilding->UnderConstruction = false;
    BarracksUnit->MaxHitPoints = 1000;
    BarracksUnit->HitPoints = 200;
    BurnCombat.Update(1.0f);
    Require(BarracksUnit->HitPoints < 200,
        "Terran building below 33% HP burns down over time");

    // Test Disruption Web: ground units cannot attack beneath it, air units unaffected
    {
        Core::ECS::SystemManager DWebSysMgr;
        auto& DWebEM = DWebSysMgr.GetEntityManager();
        Game::Systems::BulletSystem DWebBullet(DWebSysMgr);
        Game::Systems::CombatSystem DWebCombat(DWebSysMgr, DWebBullet);

        const int WebbedMarine = SpawnDefUnit(DWebEM, "Marine", {100.0f, 100.0f}, 0);
        AddGroundOnlyWeapon(DWebEM, WebbedMarine);

        const int TargetGround = SpawnDefUnit(DWebEM, "Marine", {120.0f, 100.0f}, 1);

        // Spawn Disruption Web covering (100, 100)
        const int WebID = DWebEM.CreateEntity();
        DWebEM.AddComponent<Game::Components::TransformComponent>(WebID).Position = {100.0f, 100.0f};
        auto& WebArea = DWebEM.AddComponent<Game::Components::SpellAreaComponent>(WebID);
        WebArea.Effect = Game::Components::StatusEffect::DisruptionWeb;
        WebArea.Radius = 96.0f;
        WebArea.DurationRemaining = 24.0f;

        Game::Systems::CombatSystem::IssueAttack(DWebEM, {WebbedMarine}, TargetGround);
        auto* TargetUnit = DWebEM.GetComponent<Game::Components::UnitComponent>(TargetGround);
        const int TargetHpBefore = TargetUnit->HitPoints;

        DWebCombat.Update(0.2f);
        Require(TargetUnit->HitPoints == TargetHpBefore,
            "ground unit under Disruption Web cannot attack");

        // Air attacker under Disruption Web is unaffected and can attack
        const int AirAttacker = SpawnDefUnit(DWebEM, "Scout", {100.0f, 100.0f}, 0);
        AddGroundOnlyWeapon(DWebEM, AirAttacker);
        auto* AirCombat = DWebEM.GetComponent<Game::Components::CombatComponent>(AirAttacker);
        AirCombat->MyElevation = Game::Components::Elevation::Air;

        Game::Systems::CombatSystem::IssueAttack(DWebEM, {AirAttacker}, TargetGround);
        DWebCombat.Update(0.2f);
        Require(TargetUnit->HitPoints < TargetHpBefore,
            "air unit is unaffected by Disruption Web and can attack");
    }

    {
        // Yamato Gun explosive damage verification
        Core::ECS::SystemManager YamatoSysMgr;
        auto& YamatoEM = YamatoSysMgr.GetEntityManager();
        const int BCAttacker = SpawnDefUnit(YamatoEM, "Battlecruiser", {100.0f, 100.0f}, 0);
        const int BCTarget = SpawnDefUnit(YamatoEM, "Battlecruiser", {150.0f, 100.0f}, 1);
        auto* BCTargetUnit = YamatoEM.GetComponent<Game::Components::UnitComponent>(BCTarget);
        Require(BCTargetUnit && BCTargetUnit->HitPoints == 500 && BCTargetUnit->Armor == 3,
                "Battlecruiser spawns with 500 HP and 3 armor");

        Game::Components::CombatComponent YamatoCombat;
        YamatoCombat.AirDamage = 260;
        YamatoCombat.GroundDamage = 260;
        YamatoCombat.AirDmgType = Game::Components::DamageType::Explosive;
        YamatoCombat.GroundDmgType = Game::Components::DamageType::Explosive;

        Game::Systems::CombatSystem::ApplyDamage(YamatoEM, BCTarget, YamatoCombat, BCAttacker, 260);
        Require(BCTargetUnit->HitPoints == 243,
                "Yamato Gun deals 257 explosive damage to armored large Battlecruiser (260 - 3 armor)");
    }

    {
        // Target priority verification: armed combat unit prioritized over closer building
        Core::ECS::SystemManager PrioSysMgr;
        auto& PrioEM = PrioSysMgr.GetEntityManager();
        Game::Systems::BulletSystem PrioBulletSys(PrioSysMgr);
        Game::Systems::CombatSystem PrioCombatSys(PrioSysMgr, PrioBulletSys);

        const int MarineAttacker = SpawnDefUnit(PrioEM, "Marine", {100.0f, 100.0f}, 0);
        // Pylon is only 30px away from Marine
        const int CloserPylon = SpawnDefUnit(PrioEM, "Pylon", {130.0f, 100.0f}, 1, true);
        (void)CloserPylon;
        // Zealot is 70px away from Marine (further than Pylon, but within range 128px)
        const int FurtherZealot = SpawnDefUnit(PrioEM, "Zealot", {170.0f, 100.0f}, 1);

        const int SelectedTarget = Game::Systems::CombatSystem::FindNearestEnemy(PrioEM, MarineAttacker, 128.0f);
        Require(SelectedTarget == FurtherZealot,
                "Combat target acquisition prioritizes armed enemy unit over closer unarmed building");
    }

    {
        // Friendly force-attack with EnemyOnly splash weapon damages direct target, but not other nearby friendlies
        Core::ECS::SystemManager SplashForceSysMgr;
        auto& SplashForceEM = SplashForceSysMgr.GetEntityManager();
        Game::Systems::BulletSystem SplashForceBulletSys(SplashForceSysMgr);

        const int AttackerID = SpawnUnit(SplashForceEM, {0.0f, 0.0f}, 0, false);
        const int FriendlyDirectTarget = SpawnUnit(SplashForceEM, {32.0f, 0.0f}, 0, false);
        const int FriendlyBystander = SpawnUnit(SplashForceEM, {35.0f, 0.0f}, 0, false);

        auto* DirectTargetUnit = SplashForceEM.GetComponent<Game::Components::UnitComponent>(FriendlyDirectTarget);
        auto* BystanderUnit = SplashForceEM.GetComponent<Game::Components::UnitComponent>(FriendlyBystander);
        DirectTargetUnit->HitPoints = 100;
        DirectTargetUnit->MaxHitPoints = 100;
        BystanderUnit->HitPoints = 100;
        BystanderUnit->MaxHitPoints = 100;

        // Apply EnemyRadial splash damage (like Archon) targeting FriendlyDirectTarget
        SplashForceBulletSys.ApplySplashDamage(
            SplashForceEM, {32.0f, 0.0f},
            10.0f, 20.0f, 30.0f,
            40, Game::Components::DamageType::Normal,
            0, 0, AttackerID,
            Game::Components::SplashType::EnemyRadial,
            true, false, false, 1,
            FriendlyDirectTarget,
            {}, {}, false);

        Require(DirectTargetUnit->HitPoints < 100,
                "Splash weapon with EnemyOnly damages directly targeted friendly unit");
        Require(BystanderUnit->HitPoints == 100,
                "Splash weapon with EnemyOnly does not damage nearby friendly bystanders");
    }

    {
        // Alliance integration in CombatSystem::AreEnemies and target acquisition
        Core::ECS::SystemManager AllySysMgr;
        auto& AllyEM = AllySysMgr.GetEntityManager();
        Game::Systems::BulletSystem AllyBulletSys(AllySysMgr);
        Game::Systems::CombatSystem AllyCombatSys(AllySysMgr, AllyBulletSys);

        const int UnitP0 = SpawnUnit(AllyEM, {100.0f, 100.0f}, 0, false);
        const int UnitP1 = SpawnUnit(AllyEM, {130.0f, 100.0f}, 1, false);

        // Before alliance: units are enemies
        Require(Game::Systems::CombatSystem::AreEnemies(UnitP0, UnitP1, AllyEM),
                "Units from different players are enemies by default");
        Require(Game::Systems::CombatSystem::FindNearestEnemy(AllyEM, UnitP0, 64.0f) == UnitP1,
                "Enemy unit is targeted by FindNearestEnemy");

        // Set alliance checker
        Game::Systems::CombatSystem::SetAllianceChecker([](int A, int B) {
            return (A == 0 && B == 1) || (A == 1 && B == 0);
        });

        Require(!Game::Systems::CombatSystem::AreEnemies(UnitP0, UnitP1, AllyEM),
                "Allied units are not enemies when AllianceChecker reports allied");
        Require(Game::Systems::CombatSystem::FindNearestEnemy(AllyEM, UnitP0, 64.0f) == -1,
                "Allied unit is not targeted by FindNearestEnemy");

        Game::Systems::CombatSystem::SetAllianceChecker(nullptr);
    }

    std::printf("CombatRulesTest passed\n");
    return 0;
}
