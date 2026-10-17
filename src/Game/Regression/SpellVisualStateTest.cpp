#include "Game/Systems/SpellSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/IScriptVM.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

void CombatSystem::ApplyDamage(Core::ECS::EntityManager&,
                               int,
                               const Game::Components::CombatComponent&,
                               int,
                               int,
                               int)
{
}

} // namespace Game::Systems

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::printf("FAIL: %s\n", Message);
            std::fflush(stdout);
            std::exit(1);
        }
    }

    int CreateCaster(Core::ECS::EntityManager& EM,
                     int PlayerID,
                     Core::Math::Vec2 Position,
                     int Energy)
    {
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Game::Races::GetUnitDefByID(
            Game::Races::GetUnitDefID("Queen")).ChkUnitID;
        Unit.UnitRace = Game::Components::Race::Zerg;
        Unit.HitPoints = 80;
        Unit.MaxHitPoints = 80;
        Unit.EnergyPoints = Energy;
        Unit.MaxEnergyPoints = 200;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        return EntityID;
    }

    int CreateUnit(Core::ECS::EntityManager& EM,
                   const char* DefName,
                   int PlayerID,
                   Core::Math::Vec2 Position)
    {
        const int DefID = Game::Races::GetUnitDefID(DefName);
        const auto Def = Game::Races::GetUnitDefByID(DefID);
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = Def.ChkUnitID;
        Unit.UnitRace = Def.RaceType;
        Unit.HitPoints = Def.HitPoints;
        Unit.MaxHitPoints = Def.HitPoints;
        Unit.SupplyCost = Def.SupplyCost;
        Unit.SizeType = Def.SizeType;
        Unit.Armor = Def.Armor;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        return EntityID;
    }
}

int main()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();

    const int Caster = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Caster).Position = {128.0f, 96.0f};

    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(Caster);
    Unit.ChkUnitID = 64;
    Unit.HitPoints = 80;
    Unit.MaxHitPoints = 80;
    Unit.EnergyPoints = 125;
    Unit.MaxEnergyPoints = 200;

    auto& Player = EM.AddComponent<Game::Components::PlayerComponent>(Caster);
    Player.PlayerID = 2;
    Player.PlayerColor = Core::Color(40, 80, 255, 255);

    auto& Render = EM.AddComponent<Game::Components::RenderComponent>(Caster);
    Render.ImageID = 123;
    Render.SubunitImageID = 456;
    Render.FrameIndex = 7;
    Render.TotalFrames = 12;
    Render.Flipped = true;
    Render.Tint = Core::Color(200, 220, 255, 210);

    const int HallucinationTarget = EM.CreateEntity();
    auto& TargetTransform =
        EM.AddComponent<Game::Components::TransformComponent>(HallucinationTarget);
    TargetTransform.Position = {256.0f, 128.0f};
    auto& TargetUnit =
        EM.AddComponent<Game::Components::UnitComponent>(HallucinationTarget);
    const auto DragoonDef = Game::Races::GetUnitDefByID(
        Game::Races::GetUnitDefID("Dragoon"));
    TargetUnit.ChkUnitID = DragoonDef.ChkUnitID;
    TargetUnit.UnitRace = DragoonDef.RaceType;
    TargetUnit.HitPoints = 31;
    TargetUnit.MaxHitPoints = DragoonDef.HitPoints;
    TargetUnit.ShieldPoints = 17;
    TargetUnit.MaxShieldPoints = DragoonDef.ShieldPoints;
    TargetUnit.Armor = DragoonDef.Armor;
    TargetUnit.EnergyPoints = 40;
    TargetUnit.MaxEnergyPoints = 200;
    TargetUnit.SupplyCost = DragoonDef.SupplyCost;
    auto& TargetPlayer =
        EM.AddComponent<Game::Components::PlayerComponent>(HallucinationTarget);
    TargetPlayer = Player;
    auto& TargetRender =
        EM.AddComponent<Game::Components::RenderComponent>(HallucinationTarget);
    TargetRender.ImageID = 789;
    TargetRender.SubunitImageID = 987;
    TargetRender.FrameIndex = 3;
    TargetRender.TotalFrames = 9;
    auto& TargetPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(HallucinationTarget);
    TargetPath.TargetSpeed = 75.0f;
    TargetPath.CurrentSpeed = 25.0f;
    TargetPath.CollisionRadius = 12;
    auto& TargetCombat =
        EM.AddComponent<Game::Components::CombatComponent>(HallucinationTarget);
    TargetCombat.GroundDamage = 20;
    TargetCombat.AttackRange = 128.0f;
    TargetCombat.TargetEntityID = Caster;

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, Caster, Game::Systems::SpellType::Hallucination,
                HallucinationTarget),
            "hallucination can be cast by an energized caster");

    auto* CasterUnit = EM.GetComponent<Game::Components::UnitComponent>(Caster);
    Require(CasterUnit && CasterUnit->EnergyPoints == 25,
            "hallucination consumes caster energy");

    auto* Cooldown = EM.GetComponent<Game::Components::SpellCooldown>(Caster);
    Require(Cooldown && Cooldown->SpellType == (uint8_t)Game::Systems::SpellType::Hallucination &&
                Cooldown->CooldownRemaining > 0.0f,
            "hallucination applies spell cooldown");

    int IllusionCount = 0;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::StatusEffectComponent,
                                           Game::Components::RenderComponent,
                                           Game::Components::PlayerComponent,
                                           Game::Components::UnitComponent,
                                           Game::Components::TransformComponent>())
    {
        if (EntityID == Caster || EntityID == HallucinationTarget)
            continue;

        auto* Status = EM.GetComponent<Game::Components::StatusEffectComponent>(EntityID);
        auto* IllusionRender = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
        auto* IllusionPlayer = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        auto* IllusionUnit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        auto* IllusionTrans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);

        Require(Status && Status->HasEffect(Game::Components::StatusEffect::Hallucination),
                "illusion carries hallucination visual status");
        Require(IllusionRender && IllusionRender->ImageID == TargetRender.ImageID &&
                    IllusionRender->SubunitImageID == TargetRender.SubunitImageID &&
                    IllusionRender->FrameIndex == TargetRender.FrameIndex,
                "illusion copies the selected target render state");
        Require(IllusionPlayer && IllusionPlayer->PlayerID == Player.PlayerID,
                "illusion keeps the caster player identity");
        Require(IllusionUnit && IllusionUnit->ChkUnitID == TargetUnit.ChkUnitID &&
                    IllusionUnit->HitPoints == TargetUnit.MaxHitPoints &&
                    IllusionUnit->ShieldPoints == TargetUnit.MaxShieldPoints &&
                    IllusionUnit->Armor == TargetUnit.Armor &&
                    IllusionUnit->EnergyPoints == 0 &&
                    IllusionUnit->SupplyCost == 0,
                "illusion copies target identity and starts at full vitality without spell energy");
        auto* IllusionPath =
            EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
        auto* IllusionCombat =
            EM.GetComponent<Game::Components::CombatComponent>(EntityID);
        Require(IllusionPath && IllusionPath->CollisionRadius == TargetPath.CollisionRadius &&
                    IllusionPath->CurrentSpeed == 0.0f && IllusionPath->Path.empty(),
                "illusion copies collision data but starts stationary");
        Require(IllusionCombat &&
                    IllusionCombat->GroundDamage == TargetCombat.GroundDamage &&
                    IllusionCombat->TargetEntityID == -1,
                "illusion copies weapon data without transient targets");
        Require(IllusionTrans &&
                    IllusionTrans->Position.Y == TargetTransform.Position.Y &&
                    (IllusionTrans->Position.X == TargetTransform.Position.X - 32.0f ||
                     IllusionTrans->Position.X == TargetTransform.Position.X + 32.0f),
                "illusion is spawned around the selected target");

        IllusionCount++;
    }

    Require(IllusionCount == 2, "hallucination creates two renderable visual copies");

    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, Caster, Game::Systems::SpellType::Hallucination),
            "hallucination requires an explicit friendly unit target");

    const int PlagueCaster =
        CreateCaster(EM, 2, {2048.0f, 96.0f}, 200);
    const int PlagueCornerTarget =
        CreateUnit(EM, "Marine", 5, {2112.0f, 160.0f});
    const int PlagueOutsideTarget =
        CreateUnit(EM, "Marine", 5, {2113.0f, 96.0f});
    const int PlagueInvincibleTarget =
        CreateUnit(EM, "Marine", 5, {2080.0f, 96.0f});
    EM.GetComponent<Game::Components::UnitComponent>(PlagueInvincibleTarget)->IsInvincible = true;
    const int PlagueBurrowedTarget =
        CreateUnit(EM, "Hydralisk", 5, {2016.0f, 96.0f});
    auto& PlagueBurrow =
        EM.AddComponent<Game::Components::BurrowComponent>(PlagueBurrowedTarget);
    PlagueBurrow.IsBurrowed = true;
    PlagueBurrow.Phase = Game::Components::BurrowPhase::Burrowed;

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, PlagueCaster, Game::Systems::SpellType::Plague,
                -1, {2048.0f, 96.0f}),
            "Plague can be cast at a ground position");
    auto* CornerPlague =
        EM.GetComponent<Game::Components::StatusEffectComponent>(PlagueCornerTarget);
    Require(CornerPlague &&
                CornerPlague->HasEffect(Game::Components::StatusEffect::Plague),
            "Plague uses the classic 64-pixel square and includes its corner");
    Require(!EM.GetComponent<Game::Components::StatusEffectComponent>(PlagueCaster) &&
                !EM.GetComponent<Game::Components::StatusEffectComponent>(PlagueOutsideTarget) &&
                !EM.GetComponent<Game::Components::StatusEffectComponent>(PlagueInvincibleTarget) &&
                !EM.GetComponent<Game::Components::StatusEffectComponent>(PlagueBurrowedTarget),
            "Plague excludes its caster, outside-square, invincible, and burrowed units");

    constexpr int BroodlingCasterPlayerID = 2;
    constexpr int BroodlingVictimPlayerID = 5;
    auto& VictimResources =
        Game::Systems::HarvestSystem::GetResources(BroodlingVictimPlayerID);
    VictimResources.SupplyUsed = 1;
    VictimResources.SupplyMax = 10;

    const int BroodlingCaster =
        CreateCaster(EM, BroodlingCasterPlayerID, {256.0f, 96.0f}, 200);
    const int BroodlingVictim =
        CreateUnit(EM, "Marine", BroodlingVictimPlayerID, {288.0f, 96.0f});
    const int MarineChkID = Game::Races::GetUnitDefByID(
        Game::Races::GetUnitDefID("Marine")).ChkUnitID;
    auto& BroodlingVictimRender =
        EM.AddComponent<Game::Components::RenderComponent>(BroodlingVictim);
    BroodlingVictimRender.ImageID = (uint32_t)MarineChkID;
    BroodlingVictimRender.Layer = Game::Components::RenderLayer::Units;

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, BroodlingCaster, Game::Systems::SpellType::SpawnBroodling,
                BroodlingVictim),
            "spawn broodling can be cast on a live enemy unit");
    Require(!EM.IsAlive(BroodlingVictim),
            "spawn broodling destroys the target unit");
    Require(VictimResources.SupplyUsed == 0,
            "spell-driven unit death releases consumed supply");
    auto* DeathRecord =
        EM.GetComponent<Game::Components::DeathRecordComponent>(BroodlingVictim);
    Require(DeathRecord &&
                DeathRecord->VictimPlayerID == BroodlingVictimPlayerID &&
                DeathRecord->VictimUnitID == Game::Races::GetUnitDefByID(
                    Game::Races::GetUnitDefID("Marine")).ChkUnitID &&
                DeathRecord->KillerPlayerID == BroodlingCasterPlayerID &&
                DeathRecord->AwardKill,
            "spell-driven unit death records trigger-visible death metadata");
    int DeathVisualCount = 0;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::EffectComponent,
                                           Game::Components::RenderComponent,
                                           Game::Components::TransformComponent,
                                           Game::Components::UnitComponent>())
    {
        auto* DeathEffect = EM.GetComponent<Game::Components::EffectComponent>(EntityID);
        auto* DeathRender = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
        auto* DeathTrans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        auto* DeathUnit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        if (!DeathEffect || !DeathRender || !DeathTrans || !DeathUnit ||
            DeathUnit->ChkUnitID != MarineChkID)
        {
            continue;
        }

        Require(DeathUnit->HitPoints == 0,
            "death visual keeps the victim unit identity but is not alive");
        Require(DeathRender->ImageID == (uint32_t)MarineChkID &&
                    DeathRender->Layer == Game::Components::RenderLayer::Effects &&
                    DeathRender->Visible,
            "death visual has render data on the effects layer");
        Require(DeathTrans->Position.X == 288.0f &&
                    DeathTrans->Position.Y == 96.0f,
            "death visual remains at the victim death position");
        Require(DeathEffect->LifetimeRemaining > 0.0f &&
                    DeathEffect->InitialAnimation ==
                        Game::IScriptAnims::Anim_Death &&
                    DeathEffect->DestroyWhenAnimationCompletes &&
                    !EM.GetComponent<
                        Game::Components::PathfindingComponent>(EntityID),
            "death visual is a finite collision-free, IScript-driven effect");
        const auto* DeathPlayer =
            EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        Require(DeathPlayer &&
                    DeathPlayer->PlayerID == BroodlingVictimPlayerID,
            "death visual preserves owner tint without becoming a live entity");
        ++DeathVisualCount;
    }
    Require(DeathVisualCount == 1,
            "spell-driven unit death leaves one scripted death visual");

    const int BroodlingDefID = Game::Races::GetUnitDefID("Broodling");
    const auto BroodlingDef = Game::Races::GetUnitDefByID(BroodlingDefID);
    int BroodlingCount = 0;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                           Game::Components::PlayerComponent>())
    {
        auto* BroodlingUnit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        auto* BroodlingPlayer = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        if (!BroodlingUnit || !BroodlingPlayer ||
            BroodlingPlayer->PlayerID != BroodlingCasterPlayerID ||
            BroodlingUnit->ChkUnitID != BroodlingDef.ChkUnitID)
        {
            continue;
        }

        auto* BroodlingRender = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
        auto* BroodlingPath = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
        auto* BroodlingCommand = EM.GetComponent<Game::Components::CommandComponent>(EntityID);
        auto* BroodlingCombat = EM.GetComponent<Game::Components::CombatComponent>(EntityID);
        Require(BroodlingUnit->HitPoints == BroodlingDef.HitPoints &&
                    BroodlingUnit->SupplyCost == BroodlingDef.SupplyCost &&
                    BroodlingUnit->Speed == BroodlingDef.Speed,
                "spawn broodling initializes unit stats from RaceDef");
        Require(BroodlingRender &&
                    BroodlingRender->ImageID == (uint32_t)BroodlingDef.ChkUnitID &&
                    BroodlingRender->Visible,
                "spawn broodling creates renderable broodlings");
        Require(BroodlingPath && !BroodlingPath->IsAir &&
                    BroodlingPath->NodeTolerance == 8.0f,
                "spawn broodling creates ground pathing state");
        Require(BroodlingCommand &&
                    BroodlingCommand->CurrentCommand == Game::Components::CommandType::Stop,
                "spawn broodling creates command state");
        Require(BroodlingCombat &&
                    BroodlingCombat->CanTargetGround &&
                    BroodlingCombat->GroundDamage == BroodlingDef.GroundDamage,
                "spawn broodling creates combat state from RaceDef");
        ++BroodlingCount;
    }
    Require(BroodlingCount == 2,
            "spawn broodling creates two complete broodling units");

    // Replenish energy & cooldown for validation checks
    EM.GetComponent<Game::Components::UnitComponent>(BroodlingCaster)->EnergyPoints = 200;
    EM.RemoveComponent<Game::Components::SpellCooldown>(BroodlingCaster);

    const int BroodlingBuildingTarget =
        CreateUnit(EM, "Marine", BroodlingVictimPlayerID, {288.0f, 96.0f});
    EM.AddComponent<Game::Components::BuildingComponent>(BroodlingBuildingTarget);
    std::string RejectReason;
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, BroodlingCaster, Game::Systems::SpellType::SpawnBroodling,
                BroodlingBuildingTarget, {}, &RejectReason) &&
            RejectReason == "spawn broodling requires a non-building unit",
            "spawn broodling rejects building targets");

    const int BroodlingAirTarget =
        CreateUnit(EM, "Mutalisk", BroodlingVictimPlayerID, {288.0f, 96.0f});
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, BroodlingCaster, Game::Systems::SpellType::SpawnBroodling,
                BroodlingAirTarget, {}, &RejectReason) &&
            RejectReason == "spawn broodling requires a live organic ground unit",
            "spawn broodling rejects air targets");

    const int BroodlingMechTarget =
        CreateUnit(EM, "Vulture", BroodlingVictimPlayerID, {288.0f, 96.0f});
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, BroodlingCaster, Game::Systems::SpellType::SpawnBroodling,
                BroodlingMechTarget, {}, &RejectReason) &&
            RejectReason == "spawn broodling requires a live organic ground unit",
            "spawn broodling rejects mechanical targets");

    constexpr int ConsumePlayerID = 6;
    constexpr int ConsumeEnemyPlayerID = 7;
    const int HydraliskDefID = Game::Races::GetUnitDefID("Hydralisk");
    const auto HydraliskDef = Game::Races::GetUnitDefByID(HydraliskDefID);
    auto& ConsumeResources =
        Game::Systems::HarvestSystem::GetResources(ConsumePlayerID);
    ConsumeResources.SupplyUsed = HydraliskDef.SupplyCost;
    ConsumeResources.SupplyMax = 10;

    const int ConsumeCaster =
        CreateCaster(EM, ConsumePlayerID, {352.0f, 96.0f}, 120);
    const int EnemyConsumeTarget =
        CreateUnit(EM, "Hydralisk", ConsumeEnemyPlayerID, {384.0f, 96.0f});
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, ConsumeCaster, Game::Systems::SpellType::Consume,
                EnemyConsumeTarget),
            "consume rejects enemy targets before spending cooldown");

    const int FriendlyConsumeTarget =
        CreateUnit(EM, "Hydralisk", ConsumePlayerID, {320.0f, 96.0f});
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, ConsumeCaster, Game::Systems::SpellType::Consume,
                FriendlyConsumeTarget),
            "consume can be cast on a friendly zerg unit");
    auto* ConsumeCasterUnit =
        EM.GetComponent<Game::Components::UnitComponent>(ConsumeCaster);
    Require(ConsumeCasterUnit && ConsumeCasterUnit->EnergyPoints == 170,
            "consume restores caster energy up to the spell amount");
    Require(!EM.IsAlive(FriendlyConsumeTarget),
            "consume destroys the consumed friendly unit");
    Require(ConsumeResources.SupplyUsed == 0,
            "consume releases the consumed unit supply");
    auto* ConsumeDeathRecord =
        EM.GetComponent<Game::Components::DeathRecordComponent>(FriendlyConsumeTarget);
    Require(ConsumeDeathRecord &&
                ConsumeDeathRecord->VictimPlayerID == ConsumePlayerID &&
                ConsumeDeathRecord->KillerPlayerID == ConsumePlayerID &&
                !ConsumeDeathRecord->AwardKill,
            "consume records a trigger-visible friendly death without kill credit");
    auto* ConsumeCooldown =
        EM.GetComponent<Game::Components::SpellCooldown>(ConsumeCaster);
    Require(ConsumeCooldown &&
                ConsumeCooldown->SpellType == (uint8_t)Game::Systems::SpellType::Consume &&
                ConsumeCooldown->CooldownRemaining > 0.0f,
            "consume applies a spell cooldown after a successful cast");

    Core::ECS::SystemManager EnergyUpgradeSysMgr;
    auto& EnergyUpgradeEM = EnergyUpgradeSysMgr.GetEntityManager();
    constexpr int EnergyUpgradePlayerID = 11;
    auto& EnergyUpgrades =
        Game::Systems::UpgradeSystem::GetUpgrades(EnergyUpgradePlayerID);
    EnergyUpgrades.Levels[static_cast<size_t>(
        Game::Systems::ResearchId::Zerg_QueenEnergy)] = 1;
    const int UpgradedConsumeCaster =
        CreateCaster(EnergyUpgradeEM, EnergyUpgradePlayerID, {448.0f, 96.0f}, 210);
    auto* UpgradedConsumeCasterUnit =
        EnergyUpgradeEM.GetComponent<Game::Components::UnitComponent>(UpgradedConsumeCaster);
    Require(Game::Systems::UpgradeSystem::GetEffectiveMaxEnergyPoints(
                EnergyUpgradeEM, UpgradedConsumeCaster) == 250,
            "Gamete Meiosis raises Queen effective max energy to 250");
    const int UpgradedConsumeTarget =
        CreateUnit(EnergyUpgradeEM, "Hydralisk", EnergyUpgradePlayerID, {480.0f, 96.0f});
    Require(Game::Systems::SpellSystem::CastSpell(
                EnergyUpgradeEM, UpgradedConsumeCaster, Game::Systems::SpellType::Consume,
                UpgradedConsumeTarget),
            "consume can use upgraded caster energy cap");
    Require(UpgradedConsumeCasterUnit &&
                UpgradedConsumeCasterUnit->MaxEnergyPoints == 250 &&
                UpgradedConsumeCasterUnit->EnergyPoints == 250,
            "consume restores energy up to upgraded max energy cap");

    constexpr int EnsnarePlayerID = 9;
    constexpr int EnsnareEnemyPlayerID = 10;
    const int EnsnareCaster =
        CreateCaster(EM, EnsnarePlayerID, {608.0f, 96.0f}, 200);
    const int EnsnareTarget =
        CreateUnit(EM, "Marine", EnsnareEnemyPlayerID, {640.0f, 96.0f});
    const int EnsnareNearbyEnemy =
        CreateUnit(EM, "Marine", EnsnareEnemyPlayerID, {704.0f, 96.0f});
    const int EnsnareFriendly =
        CreateUnit(EM, "Marine", EnsnarePlayerID, {672.0f, 96.0f});
    const int EnsnareFarEnemy =
        CreateUnit(EM, "Marine", EnsnareEnemyPlayerID, {800.0f, 96.0f});
    auto* EnsnareTargetUnit =
        EM.GetComponent<Game::Components::UnitComponent>(EnsnareTarget);
    Require(EnsnareTargetUnit, "ensnare target has unit data");
    EnsnareTargetUnit->Speed = 2.8f;
    auto& EnsnareCloak =
        EM.AddComponent<Game::Components::CloakComponent>(EnsnareTarget);
    EnsnareCloak.IsCloaked = true;
    EnsnareCloak.IsBurrowed = true;

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, EnsnareCaster, Game::Systems::SpellType::Ensnare,
                -1, {640.0f, 96.0f}),
            "ensnare can be cast at a target position");
    Require(EnsnareTargetUnit->Speed == 2.8f,
            "ensnare does not permanently change base unit speed");
    auto* EnsnareTargetStatus =
        EM.GetComponent<Game::Components::StatusEffectComponent>(EnsnareTarget);
    auto* EnsnareNearbyStatus =
        EM.GetComponent<Game::Components::StatusEffectComponent>(EnsnareNearbyEnemy);
    Require(EnsnareTargetStatus &&
                EnsnareTargetStatus->HasEffect(Game::Components::StatusEffect::Ensnare),
            "ensnare applies slow status to the target area unit");
    Require(EnsnareNearbyStatus &&
                EnsnareNearbyStatus->HasEffect(Game::Components::StatusEffect::Ensnare),
            "ensnare applies slow status to nearby enemy units");
    Require(!EnsnareCloak.IsCloaked && !EnsnareCloak.IsBurrowed,
            "ensnare reveals cloaked or burrowed targets");
    Require(!EM.GetComponent<Game::Components::StatusEffectComponent>(EnsnareFriendly),
            "ensnare does not slow friendly units in the target area");
    Require(!EM.GetComponent<Game::Components::StatusEffectComponent>(EnsnareFarEnemy),
            "ensnare does not slow enemies outside the target radius");
    auto* EnsnareCasterUnit =
        EM.GetComponent<Game::Components::UnitComponent>(EnsnareCaster);
    Require(EnsnareCasterUnit && EnsnareCasterUnit->EnergyPoints == 125,
            "ensnare consumes caster energy");

    constexpr int ScannerPlayerID = 1;
    constexpr int ScannerEnemyPlayerID = 3;
    const int ScannerCaster =
        CreateCaster(EM, ScannerPlayerID, {896.0f, 96.0f}, 100);
    const int ScannerCloakedTarget =
        CreateUnit(EM, "Marine", ScannerEnemyPlayerID, {928.0f, 96.0f});
    auto& ScannerCloak =
        EM.AddComponent<Game::Components::CloakComponent>(ScannerCloakedTarget);
    ScannerCloak.IsCloaked = true;

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, ScannerCaster, Game::Systems::SpellType::ScannerSweep,
                -1, {928.0f, 96.0f}),
            "scanner sweep can be cast at a target position");
    Require(ScannerCloak.IsCloaked,
            "scanner sweep does not permanently remove target cloak state");
    auto* ScannerCasterUnit =
        EM.GetComponent<Game::Components::UnitComponent>(ScannerCaster);
    Require(ScannerCasterUnit && ScannerCasterUnit->EnergyPoints == 50,
            "scanner sweep consumes caster energy");

    int ScannerRevealCount = 0;
    for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                           Game::Components::RevealAreaComponent>())
    {
        auto* Reveal =
            EM.GetComponent<Game::Components::RevealAreaComponent>(EntityID);
        auto* RevealTrans =
            EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        if (!Reveal || Reveal->PlayerID != ScannerPlayerID)
            continue;
        Require(RevealTrans &&
                    RevealTrans->Position.X == 928.0f &&
                    RevealTrans->Position.Y == 96.0f,
                "scanner sweep creates a reveal area at the target position");
        Require(Reveal->SightRange == 5 &&
                    Reveal->DurationRemaining > 0.0f &&
                    Reveal->ProvidesDetection,
                "scanner sweep reveal area provides temporary detector coverage");
        ++ScannerRevealCount;
    }
    Require(ScannerRevealCount == 1,
            "scanner sweep creates exactly one reveal area for the caster player");

    constexpr int EmpPlayerID = 6;
    constexpr int EmpEnemyPlayerID = 7;
    const int EmpCaster =
        CreateCaster(EM, EmpPlayerID, {1088.0f, 96.0f}, 150);
    const int EmpActiveCloakTarget =
        CreateUnit(EM, "Marine", EmpEnemyPlayerID, {1120.0f, 96.0f});
    auto* EmpActiveUnit =
        EM.GetComponent<Game::Components::UnitComponent>(EmpActiveCloakTarget);
    Require(EmpActiveUnit, "emp active cloak target has unit data");
    EmpActiveUnit->EnergyPoints = 80;
    EmpActiveUnit->MaxEnergyPoints = 100;
    EmpActiveUnit->ShieldPoints = 30;
    EmpActiveUnit->MaxShieldPoints = 30;
    auto& EmpActiveCloak =
        EM.AddComponent<Game::Components::CloakComponent>(EmpActiveCloakTarget);
    EmpActiveCloak.IsCloaked = true;
    EmpActiveCloak.IsPassiveCloak = false;

    const int EmpPassiveCloakTarget =
        CreateUnit(EM, "Marine", EmpEnemyPlayerID, {1152.0f, 96.0f});
    auto& EmpPassiveCloak =
        EM.AddComponent<Game::Components::CloakComponent>(EmpPassiveCloakTarget);
    EmpPassiveCloak.IsCloaked = true;
    EmpPassiveCloak.IsPassiveCloak = true;

    const int EmpBurrowedTarget =
        CreateUnit(EM, "Marine", EmpEnemyPlayerID, {1184.0f, 96.0f});
    auto& EmpBurrowedCloak =
        EM.AddComponent<Game::Components::CloakComponent>(EmpBurrowedTarget);
    EmpBurrowedCloak.IsCloaked = true;
    EmpBurrowedCloak.IsBurrowed = true;

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, EmpCaster, Game::Systems::SpellType::EMPShockwave,
                -1, {1120.0f, 96.0f}),
            "emp shockwave can be cast at a target position");
    Require(EmpActiveUnit->EnergyPoints == 0 &&
                EmpActiveUnit->ShieldPoints == 0,
            "emp drains target energy and shields");
    Require(!EmpActiveCloak.IsCloaked,
            "emp decloaks active energy-based cloak targets");
    Require(EmpPassiveCloak.IsCloaked,
            "emp does not remove passive cloak state");
    Require(EmpBurrowedCloak.IsCloaked && EmpBurrowedCloak.IsBurrowed,
            "emp does not unburrow targets");

    constexpr int LockdownPlayerID = 0;
    constexpr int LockdownEnemyPlayerID = 5;
    const int LockdownCaster =
        CreateCaster(EM, LockdownPlayerID, {768.0f, 192.0f}, 150);
    const int LockdownBiologicalTarget =
        CreateUnit(EM, "Marine", LockdownEnemyPlayerID, {800.0f, 192.0f});
    const int LockdownMechanicalTarget =
        CreateUnit(EM, "Vulture", LockdownEnemyPlayerID, {832.0f, 192.0f});
    const int LockdownScvTarget =
        CreateUnit(EM, "SCV", LockdownEnemyPlayerID, {848.0f, 192.0f});
    const int LockdownProbeTarget =
        CreateUnit(EM, "Probe", LockdownEnemyPlayerID, {864.0f, 192.0f});
    const int LockdownZergTarget =
        CreateUnit(EM, "Zergling", LockdownEnemyPlayerID, {880.0f, 192.0f});

    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, LockdownCaster, Game::Systems::SpellType::Lockdown,
                LockdownBiologicalTarget),
            "lockdown rejects biological targets before spending energy");
    Require(!Game::Systems::SpellSystem::CastSpell(
                EM, LockdownCaster, Game::Systems::SpellType::Lockdown,
                LockdownBiologicalTarget),
            "lockdown cast fails on biological targets");

    // SCV and Probe are mechanical units and valid lockdown targets; Zerg units are not
    Require(Game::Systems::SpellSystem::CanCastSpell(
                EM, LockdownCaster, Game::Systems::SpellType::Lockdown,
                LockdownScvTarget),
            "SCV is mechanical and can be targeted by lockdown");
    Require(Game::Systems::SpellSystem::CanCastSpell(
                EM, LockdownCaster, Game::Systems::SpellType::Lockdown,
                LockdownProbeTarget),
            "Probe is mechanical and can be targeted by lockdown");
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, LockdownCaster, Game::Systems::SpellType::Lockdown,
                LockdownZergTarget),
            "Zerg units are organic and cannot be targeted by lockdown");

    auto* LockdownCasterUnit =
        EM.GetComponent<Game::Components::UnitComponent>(LockdownCaster);
    Require(LockdownCasterUnit && LockdownCasterUnit->EnergyPoints == 150,
            "failed lockdown does not consume caster energy");
    Require(!EM.GetComponent<Game::Components::SpellCooldown>(LockdownCaster),
            "failed lockdown does not apply cooldown");

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, LockdownCaster, Game::Systems::SpellType::Lockdown,
                LockdownMechanicalTarget),
            "lockdown can be cast on mechanical targets");
    auto* LockdownStatus =
        EM.GetComponent<Game::Components::StatusEffectComponent>(LockdownMechanicalTarget);
    Require(LockdownStatus &&
                LockdownStatus->HasEffect(Game::Components::StatusEffect::Lockdown),
            "lockdown applies frozen status to mechanical targets");
    Require(LockdownCasterUnit && LockdownCasterUnit->EnergyPoints == 50,
            "successful lockdown consumes caster energy");

    constexpr int RecallPlayerID = 4;
    constexpr int RecallEnemyPlayerID = 8;
    const int RecallCaster =
        CreateCaster(EM, RecallPlayerID, {512.0f, 512.0f}, 200);
    const int RecallFriendly =
        CreateUnit(EM, "Marine", RecallPlayerID, {800.0f, 800.0f});
    const int RecallEnemy =
        CreateUnit(EM, "Marine", RecallEnemyPlayerID, {832.0f, 800.0f});
    const int RecallBuilding =
        CreateUnit(EM, "Marine", RecallPlayerID, {800.0f, 832.0f});
    EM.AddComponent<Game::Components::BuildingComponent>(RecallBuilding);
    const int RecallResource =
        CreateUnit(EM, "Marine", RecallPlayerID, {832.0f, 832.0f});
    EM.AddComponent<Game::Components::ResourceComponent>(RecallResource);

    auto& RecallCommand =
        EM.AddComponent<Game::Components::CommandComponent>(RecallFriendly);
    RecallCommand.CurrentCommand = Game::Components::CommandType::Move;
    RecallCommand.TargetEntityID = RecallEnemy;
    RecallCommand.TargetPosition = {900.0f, 900.0f};
    RecallCommand.CommandQueue.push_back(Game::Components::CommandType::Move);

    auto& RecallPath =
        EM.AddComponent<Game::Components::PathfindingComponent>(RecallFriendly);
    RecallPath.Path.push_back({900.0f, 900.0f});
    RecallPath.PathIndex = 0;
    RecallPath.CurrentSpeed = 32.0f;
    RecallPath.TargetSpeed = 32.0f;
    RecallPath.CurrentVelocity = {32.0f, 0.0f};

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, RecallCaster, Game::Systems::SpellType::Recall,
                -1, {800.0f, 800.0f}),
            "recall can be cast at a target position");

    auto* RecallCasterUnit =
        EM.GetComponent<Game::Components::UnitComponent>(RecallCaster);
    Require(RecallCasterUnit && RecallCasterUnit->EnergyPoints == 50,
            "recall consumes caster energy");

    auto* RecalledTrans =
        EM.GetComponent<Game::Components::TransformComponent>(RecallFriendly);
    Require(RecalledTrans &&
                RecalledTrans->Position.X == 480.0f &&
                RecalledTrans->Position.Y == 512.0f,
            "recall teleports friendly units next to the caster");
    Require(RecallCommand.CurrentCommand == Game::Components::CommandType::Stop &&
                RecallCommand.TargetEntityID == -1 &&
                RecallCommand.CommandQueue.empty(),
            "recall clears stale command state after teleport");
    Require(RecallPath.Path.empty() &&
                RecallPath.CurrentSpeed == 0.0f &&
                RecallPath.TargetSpeed == 0.0f &&
                RecallPath.CurrentVelocity.Length() == 0.0f,
            "recall clears stale pathing state after teleport");

    auto* EnemyRecallTrans =
        EM.GetComponent<Game::Components::TransformComponent>(RecallEnemy);
    auto* BuildingRecallTrans =
        EM.GetComponent<Game::Components::TransformComponent>(RecallBuilding);
    auto* ResourceRecallTrans =
        EM.GetComponent<Game::Components::TransformComponent>(RecallResource);
    Require(EnemyRecallTrans &&
                EnemyRecallTrans->Position.X == 832.0f &&
                EnemyRecallTrans->Position.Y == 800.0f,
            "recall does not teleport enemy units");
    Require(BuildingRecallTrans &&
                BuildingRecallTrans->Position.X == 800.0f &&
                BuildingRecallTrans->Position.Y == 832.0f,
            "recall does not teleport buildings");
    Require(ResourceRecallTrans &&
                ResourceRecallTrans->Position.X == 832.0f &&
                ResourceRecallTrans->Position.Y == 832.0f,
            "recall does not teleport resource entities");

    auto* RecallCooldown =
        EM.GetComponent<Game::Components::SpellCooldown>(RecallCaster);
    Require(RecallCooldown &&
                RecallCooldown->SpellType == (uint8_t)Game::Systems::SpellType::Recall &&
                RecallCooldown->CooldownRemaining > 0.0f,
            "recall applies a spell cooldown after a successful cast");

    // ==========================================
    // Extended StarCraft Spells Regression Tests
    // ==========================================
    const int SpellCasterP1 = CreateCaster(EM, 1, {1000.0f, 1000.0f}, 250);
    auto* SpellCasterUnit = EM.GetComponent<Game::Components::UnitComponent>(SpellCasterP1);
    SpellCasterUnit->ShieldPoints = 100;
    SpellCasterUnit->MaxShieldPoints = 100;

    const int MarineP1 = CreateUnit(EM, "Marine", 1, {1020.0f, 1000.0f});
    const int DragoonP2 = CreateUnit(EM, "Dragoon", 2, {1050.0f, 1000.0f});
    const int ZealotP2 = CreateUnit(EM, "Zealot", 2, {1060.0f, 1000.0f});

    auto ResetCaster = [&](int Energy = 200) {
        SpellCasterUnit->EnergyPoints = Energy;
        if (auto* CD = EM.GetComponent<Game::Components::SpellCooldown>(SpellCasterP1))
        {
            CD->CooldownRemaining = 0.0f;
        }
    };

    // 1. Defensive Matrix
    ResetCaster(250);
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::DefensiveMatrix, MarineP1),
            "cast defensive matrix on friendly unit succeeds");
    Require(SpellCasterUnit->EnergyPoints == 150, "defensive matrix costs 100 energy");
    auto* MarineUnit = EM.GetComponent<Game::Components::UnitComponent>(MarineP1);
    auto* MarineStatus = EM.GetComponent<Game::Components::StatusEffectComponent>(MarineP1);
    Require(MarineUnit && MarineUnit->DefensiveMatrixPoints == 250,
            "defensive matrix provides 250 barrier points");
    Require(MarineStatus && MarineStatus->HasEffect(Game::Components::StatusEffect::DefensiveMatrix),
            "defensive matrix status effect active");

    // 2. Irradiate
    ResetCaster(150);
    std::string FailReason;
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::Irradiate, DragoonP2, {}, &FailReason),
            "irradiate cannot target mechanical units");
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::Irradiate, ZealotP2),
            "irradiate can target organic units");
    Require(SpellCasterUnit->EnergyPoints == 75, "irradiate costs 75 energy");
    auto* ZealotStatus = EM.GetComponent<Game::Components::StatusEffectComponent>(ZealotP2);
    Require(ZealotStatus && ZealotStatus->HasEffect(Game::Components::StatusEffect::Irradiate),
            "target gains irradiate status effect");

    // 3. Restoration
    ResetCaster(200);
    MarineStatus->AddEffect(Game::Components::StatusEffect::Blind, 100.0f);
    MarineStatus->AddEffect(Game::Components::StatusEffect::Lockdown, 100.0f);
    MarineStatus->AddEffect(Game::Components::StatusEffect::Parasite, 100.0f);
    MarineStatus->AddEffect(Game::Components::StatusEffect::Plague, 100.0f);
    MarineStatus->AddEffect(Game::Components::StatusEffect::Ensnare, 100.0f);
    MarineUnit->AcidSporeCount = 5;
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::Restoration, MarineP1),
            "cast restoration on debuffed unit succeeds");
    Require(SpellCasterUnit->EnergyPoints == 150, "restoration costs 50 energy");
    Require(!MarineStatus->HasEffect(Game::Components::StatusEffect::Blind) &&
            !MarineStatus->HasEffect(Game::Components::StatusEffect::Lockdown) &&
            !MarineStatus->HasEffect(Game::Components::StatusEffect::Parasite) &&
            !MarineStatus->HasEffect(Game::Components::StatusEffect::Plague) &&
            !MarineStatus->HasEffect(Game::Components::StatusEffect::Ensnare) &&
            MarineUnit->AcidSporeCount == 0,
            "restoration dispels all debuffs and acid spores");

    // 4. Optical Flare
    ResetCaster(150);
    const int DetectorP2 = CreateUnit(EM, "Observer", 2, {1100.0f, 1000.0f});
    auto* DetectorUnit = EM.GetComponent<Game::Components::UnitComponent>(DetectorP2);
    DetectorUnit->IsDetector = true;
    DetectorUnit->SightRange = 9;
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::OpticalFlare, DetectorP2),
            "cast optical flare succeeds");
    Require(SpellCasterUnit->EnergyPoints == 75, "optical flare costs 75 energy");
    auto* DetectorStatus = EM.GetComponent<Game::Components::StatusEffectComponent>(DetectorP2);
    Require(DetectorStatus && DetectorStatus->HasEffect(Game::Components::StatusEffect::Blind),
            "target gains blind status effect");
    Require(DetectorUnit->SightRange == 1, "blind reduces sight range to 1");
    Require(!DetectorUnit->IsDetector, "blind disables detector ability");

    // Restoration cleanses Blind and restores sight range and detector capability
    ResetCaster(100);
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::Restoration, DetectorP2),
            "restoration on blinded detector succeeds");
    Require(!DetectorStatus->HasEffect(Game::Components::StatusEffect::Blind),
            "restoration cleanses blind effect");
    Require(DetectorUnit->SightRange == 9, "restoration restores normal sight range");
    Require(DetectorUnit->IsDetector, "restoration restores detector capability");

    // 5. Feedback
    ResetCaster(100);
    const int HighTemplarP2 = CreateUnit(EM, "High Templar", 2, {1150.0f, 1000.0f});
    auto* HtUnit = EM.GetComponent<Game::Components::UnitComponent>(HighTemplarP2);
    HtUnit->EnergyPoints = 180;
    HtUnit->MaxEnergyPoints = 200;
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::Feedback, HighTemplarP2),
            "feedback cast on unit with energy succeeds");
    Require(SpellCasterUnit->EnergyPoints == 50, "feedback costs 50 energy");
    Require(HtUnit->EnergyPoints == 0, "feedback completely drains target energy");

    // 6. Maelstrom
    ResetCaster(150);
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::Maelstrom, -1, {1055.0f, 1000.0f}),
            "cast maelstrom at target position succeeds");
    Require(SpellCasterUnit->EnergyPoints == 50, "maelstrom costs 100 energy");
    auto* DragoonStatus = EM.GetComponent<Game::Components::StatusEffectComponent>(DragoonP2);
    Require(ZealotStatus && ZealotStatus->HasEffect(Game::Components::StatusEffect::Maelstrom),
            "organic zealot in maelstrom area is affected");
    Require(!DragoonStatus || !DragoonStatus->HasEffect(Game::Components::StatusEffect::Maelstrom),
            "mechanical dragoon in maelstrom area is immune");

    // 7. Mind Control
    ResetCaster(200);
    auto& P1Res = Game::Systems::HarvestSystem::GetResources(1);
    auto& P2Res = Game::Systems::HarvestSystem::GetResources(2);
    P1Res.SupplyUsed = 10;
    P2Res.SupplyUsed = 10;
    P1Res.SupplyMax = 50;
    P2Res.SupplyMax = 50;

    const int EnemyMarine = CreateUnit(EM, "Marine", 2, {1200.0f, 1000.0f});
    SpellCasterUnit->ShieldPoints = 100;
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::MindControl, EnemyMarine),
            "cast mind control on enemy unit succeeds");
    Require(SpellCasterUnit->EnergyPoints == 50, "mind control costs 150 energy");
    Require(SpellCasterUnit->ShieldPoints == 0, "mind control completely depletes caster shields");
    auto* ControlledPlayer = EM.GetComponent<Game::Components::PlayerComponent>(EnemyMarine);
    Require(ControlledPlayer && ControlledPlayer->PlayerID == 1,
            "target player ownership transferred to caster");
    Require(P2Res.SupplyUsed == 9, "mind control deducts supply from original owner");
    Require(P1Res.SupplyUsed == 11, "mind control adds supply to new owner");

    // 8. Yamato Gun
    ResetCaster(200);
    const int YamatoTargetBC = CreateUnit(EM, "Battlecruiser", 2, {1250.0f, 1000.0f});
    auto* YamatoTargetBCUnit = EM.GetComponent<Game::Components::UnitComponent>(YamatoTargetBC);
    Require(YamatoTargetBCUnit && YamatoTargetBCUnit->HitPoints == 500, "Battlecruiser spawned with 500 HP");
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, SpellCasterP1, Game::Systems::SpellType::YamatoGun, YamatoTargetBC),
            "cast yamato gun on battlecruiser succeeds");
    Require(SpellCasterUnit->EnergyPoints == 50, "yamato gun costs 150 energy");

    // 9. Infestation
    const int CC = CreateUnit(EM, "CommandCenter", 2, {1300.0f, 1000.0f});
    auto* CCUnit = EM.GetComponent<Game::Components::UnitComponent>(CC);
    auto* CCPlayer = EM.GetComponent<Game::Components::PlayerComponent>(CC);
    auto& CCBldg = EM.AddComponent<Game::Components::BuildingComponent>(CC);
    Require(CCUnit && CCUnit->ChkUnitID == 106 && CCUnit->HitPoints == 1500,
            "Terran Command Center spawned with 1500 HP");

    const int InfestingQueen = CreateCaster(EM, 1, {1290.0f, 1000.0f}, 0);
    FailReason.clear();
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, InfestingQueen, Game::Systems::SpellType::Infestation, CC, {}, &FailReason),
            "cannot infest healthy Command Center");
    Require(!Game::Systems::SpellSystem::CastSpell(
                EM, InfestingQueen, Game::Systems::SpellType::Infestation, CC),
            "cast infestation fails on healthy Command Center");

    // Damaged but friendly should fail
    CCUnit->HitPoints = 700;
    CCPlayer->PlayerID = 1;
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, InfestingQueen, Game::Systems::SpellType::Infestation, CC, {}, &FailReason),
            "cannot infest friendly Command Center");

    // Damaged enemy but lifted should fail
    CCPlayer->PlayerID = 2;
    CCBldg.IsLifted = true;
    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, InfestingQueen, Game::Systems::SpellType::Infestation, CC, {}, &FailReason),
            "cannot infest lifted Command Center");

    // Valid: Damaged enemy grounded Command Center (< 50% HP)
    CCBldg.IsLifted = false;
    CCBldg.BurnDamageAccumulator = 1.5f;
    P2Res.SupplyMax = 50;

    Require(Game::Systems::SpellSystem::CanCastSpell(
                EM, InfestingQueen, Game::Systems::SpellType::Infestation, CC),
            "can cast infestation on damaged enemy Command Center");
    Require(Game::Systems::SpellSystem::CastSpell(
                EM, InfestingQueen, Game::Systems::SpellType::Infestation, CC),
            "cast infestation succeeds");

    Require(CCUnit->ChkUnitID == 130,
            "Command Center converted to Infested Command Center (ChkUnitID 130)");
    Require(CCUnit->UnitRace == Game::Components::Race::Zerg,
            "Infested Command Center is Zerg race");
    Require(CCUnit->HitPoints == CCUnit->MaxHitPoints,
            "Infested Command Center HP restored to full (1500 HP)");
    Require(CCBldg.BurnDamageAccumulator == 0.0f, "Infested Command Center burn damage accumulator reset");
    Require(CCPlayer->PlayerID == 1,
            "Infested Command Center transferred to Queen owner (Player 1)");
    Require(P2Res.SupplyMax == 40,
            "Terran player loses 10 SupplyMax from infested Command Center");
    Require(!EM.IsAlive(InfestingQueen),
            "Queen is consumed and destroyed upon infesting Command Center");

    std::printf("SpellVisualStateTest passed\n");
    return 0;
}
