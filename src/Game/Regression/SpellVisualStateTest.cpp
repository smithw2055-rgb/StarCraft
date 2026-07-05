#include "Game/Systems/SpellSystem.h"

#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/HarvestSystem.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Game::Systems
{

void CombatSystem::ApplyDamage(Core::ECS::EntityManager&,
                               int,
                               const Game::Components::CombatComponent&,
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
            std::fprintf(stderr, "FAIL: %s\n", Message);
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

    Require(Game::Systems::SpellSystem::CastSpell(
                EM, Caster, Game::Systems::SpellType::Hallucination),
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
        if (EntityID == Caster)
            continue;

        auto* Status = EM.GetComponent<Game::Components::StatusEffectComponent>(EntityID);
        auto* IllusionRender = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
        auto* IllusionPlayer = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        auto* IllusionUnit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        auto* IllusionTrans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);

        Require(Status && Status->HasEffect(Game::Components::StatusEffect::Hallucination),
                "illusion carries hallucination visual status");
        Require(IllusionRender && IllusionRender->ImageID == Render.ImageID &&
                    IllusionRender->SubunitImageID == Render.SubunitImageID &&
                    IllusionRender->FrameIndex == Render.FrameIndex &&
                    IllusionRender->Flipped == Render.Flipped,
                "illusion copies the caster render state for sprite effect rendering");
        Require(IllusionPlayer && IllusionPlayer->PlayerID == Player.PlayerID,
                "illusion keeps the caster player identity");
        Require(IllusionUnit && IllusionUnit->ChkUnitID == Unit.ChkUnitID &&
                    IllusionUnit->HitPoints == 1 &&
                    IllusionUnit->MaxHitPoints == 1,
                "illusion keeps unit identity but has illusion hit points");
        Require(IllusionTrans &&
                    IllusionTrans->Position.Y == 96.0f &&
                    (IllusionTrans->Position.X == 96.0f ||
                     IllusionTrans->Position.X == 160.0f),
                "illusion is spawned at the expected visual offset");

        IllusionCount++;
    }

    Require(IllusionCount == 2, "hallucination creates two renderable visual copies");

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
    const int MarineChkID = Game::Races::GetUnitDefByID(
        Game::Races::GetUnitDefID("Marine")).ChkUnitID;
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
        Require(DeathEffect->LifetimeRemaining > 0.0f,
            "death visual has a finite effect lifetime");
        Require(!EM.GetComponent<Game::Components::PlayerComponent>(EntityID),
            "death visual is not counted as a live player-owned unit");
        ++DeathVisualCount;
    }
    Require(DeathVisualCount == 1,
            "spell-driven unit death leaves one short-lived death visual");

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

    Require(!Game::Systems::SpellSystem::CanCastSpell(
                EM, LockdownCaster, Game::Systems::SpellType::Lockdown,
                LockdownBiologicalTarget),
            "lockdown rejects biological targets before spending energy");
    Require(!Game::Systems::SpellSystem::CastSpell(
                EM, LockdownCaster, Game::Systems::SpellType::Lockdown,
                LockdownBiologicalTarget),
            "lockdown cast fails on biological targets");
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

    std::printf("SpellVisualStateTest passed\n");
    return 0;
}
