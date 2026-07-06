#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Core/Utils/Logger.h"

#include <algorithm>

namespace Game::Systems
{

using namespace Game::Components;
using Core::Utils::Logger;

namespace
{
    bool IsMechanicalLockdownTarget(Core::ECS::EntityManager& EM, int EntityID)
    {
        const auto* Unit = EM.GetComponent<UnitComponent>(EntityID);
        if (!Unit || Unit->HitPoints <= 0)
            return false;
        if (EM.GetComponent<ResourceComponent>(EntityID))
            return false;
        if (EM.GetComponent<BuildingComponent>(EntityID))
            return false;

        const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        if (DefID >= 0)
        {
            const auto Def = Game::Races::GetUnitDefByID(DefID);
            if (Def.IsBuilding)
                return false;

            switch (Def.RaceType)
            {
                case Race::Terran:
                    return Def.Name != "SCV" &&
                           Def.Name != "Marine" &&
                           Def.Name != "Firebat" &&
                           Def.Name != "Medic" &&
                           Def.Name != "Ghost";
                case Race::Protoss:
                    return Def.Name == "Dragoon" ||
                           Def.Name == "Reaver" ||
                           Def.Name == "Shuttle" ||
                           Def.Name == "Scout" ||
                           Def.Name == "Corsair" ||
                           Def.Name == "Carrier" ||
                           Def.Name == "Arbiter" ||
                           Def.Name == "Observer";
                case Race::Zerg:
                    return Def.Name == "Infested Terran";
            }
        }

        if (Unit->UnitRace == Race::Terran)
            return Unit->SizeType != UnitSize::Small;
        if (Unit->UnitRace == Race::Protoss)
            return Unit->SizeType != UnitSize::Small;
        return false;
    }

    void ClearMotionAfterTeleport(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (auto* Path = EM.GetComponent<PathfindingComponent>(EntityID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->TargetSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }

        if (auto* Cmd = EM.GetComponent<CommandComponent>(EntityID))
        {
            Cmd->CancelAll();
            Cmd->CommandQueue.clear();
            Cmd->TargetEntityID = -1;
            Cmd->TargetPosition = {};
        }
    }
}

int SpellSystem::GetSpellEnergyCost(SpellType Spell)
{
    switch (Spell)
    {
        case SpellType::PsionicStorm:  return 75;
        case SpellType::YamatoGun:     return 150;
        case SpellType::Lockdown:      return 100;
        case SpellType::EMPShockwave:  return 100;
        case SpellType::ScannerSweep:  return 50;
        case SpellType::Hallucination: return 100;
        case SpellType::StasisField:   return 100;
        case SpellType::Recall:        return 150;
        case SpellType::DarkSwarm:     return 100;
        case SpellType::Plague:        return 150;
        case SpellType::Ensnare:       return 75;
        case SpellType::SpawnBroodling:return 150;
        case SpellType::Parasite:      return 75;
        case SpellType::Consume:       return 0;
        default: return 0;
    }
}

float SpellSystem::GetSpellCooldown(SpellType Spell)
{
    switch (Spell)
    {
        case SpellType::PsionicStorm:  return 3.0f;
        case SpellType::YamatoGun:     return 10.0f;
        case SpellType::Lockdown:      return 4.0f;
        case SpellType::EMPShockwave:  return 6.0f;
        case SpellType::ScannerSweep:  return 5.0f;
        case SpellType::StasisField:   return 4.0f;
        case SpellType::Recall:        return 6.0f;
        case SpellType::DarkSwarm:     return 4.0f;
        case SpellType::Plague:        return 4.0f;
        default: return 2.0f;
    }
}

bool SpellSystem::CanCastSpell(Core::ECS::EntityManager& EM, int CasterID, SpellType Spell,
                                int TargetEntityID, const Core::Math::Vec2& TargetPos,
                                std::string* OutReason)
{
    (void)TargetPos;
    auto* CasterUnit = EM.GetComponent<UnitComponent>(CasterID);
    auto* CasterTrans = EM.GetComponent<TransformComponent>(CasterID);
    auto* CasterPlayer = EM.GetComponent<PlayerComponent>(CasterID);
    if (!CasterUnit || !CasterTrans || !CasterPlayer)
    {
        if (OutReason) *OutReason = "invalid caster";
        return false;
    }

    int EnergyCost = GetSpellEnergyCost(Spell);
    if (CasterUnit->EnergyPoints < EnergyCost)
    {
        if (OutReason) *OutReason = "not enough energy";
        return false;
    }

    // Check cooldown
    auto* SpellComp = EM.GetComponent<Components::SpellCooldown>(CasterID);
    if (SpellComp && SpellComp->CooldownRemaining > 0.0f)
    {
        if (OutReason) *OutReason = "spell on cooldown";
        return false;
    }

    if (TargetEntityID >= 0)
    {
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
        if (!TargetUnit || TargetUnit->HitPoints <= 0)
        {
            if (OutReason) *OutReason = "invalid target";
            return false;
        }
    }

    if (Spell == SpellType::Consume)
    {
        if (TargetEntityID < 0 || TargetEntityID == CasterID)
        {
            if (OutReason) *OutReason = "invalid consume target";
            return false;
        }
        auto* TargetPlayer = EM.GetComponent<PlayerComponent>(TargetEntityID);
        auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
        if (!TargetPlayer || !TargetUnit || TargetPlayer->PlayerID != CasterPlayer->PlayerID)
        {
            if (OutReason) *OutReason = "consume requires a friendly target";
            return false;
        }
        if (TargetUnit->UnitRace != Race::Zerg || EM.GetComponent<BuildingComponent>(TargetEntityID))
        {
            if (OutReason) *OutReason = "consume requires a friendly zerg unit";
            return false;
        }
    }

    if (Spell == SpellType::Lockdown)
    {
        if (TargetEntityID < 0 || !IsMechanicalLockdownTarget(EM, TargetEntityID))
        {
            if (OutReason) *OutReason = "lockdown requires a mechanical target";
            return false;
        }
    }

    return true;
}

bool SpellSystem::CastSpell(Core::ECS::EntityManager& EM, int CasterID, SpellType Spell,
                             int TargetEntityID, const Core::Math::Vec2& TargetPos)
{
    std::string Reason;
    if (!CanCastSpell(EM, CasterID, Spell, TargetEntityID, TargetPos, &Reason))
    {
        Logger::Debug("SpellSystem", "Cannot cast: " + Reason);
        return false;
    }

    auto* CasterUnit = EM.GetComponent<UnitComponent>(CasterID);
    auto* CasterTrans = EM.GetComponent<TransformComponent>(CasterID);
    auto* CasterPlayer = EM.GetComponent<PlayerComponent>(CasterID);
    if (!CasterUnit || !CasterTrans || !CasterPlayer) return false;

    int EnergyCost = GetSpellEnergyCost(Spell);
    CasterUnit->EnergyPoints -= EnergyCost;

    // Apply cooldown
    auto* SpellComp = EM.GetComponent<Components::SpellCooldown>(CasterID);
    if (!SpellComp)
        SpellComp = &EM.AddComponent<Components::SpellCooldown>(CasterID);
    SpellComp->SpellType = (uint8_t)Spell;
    SpellComp->CooldownRemaining = GetSpellCooldown(Spell);

    Core::Math::Vec2 Pos = TargetPos;
    if (TargetEntityID >= 0)
    {
        if (auto* TTrans = EM.GetComponent<TransformComponent>(TargetEntityID))
            Pos = TTrans->Position;
    }

    switch (Spell)
    {
        case SpellType::PsionicStorm:
        {
            // AoE 112 damage over 7 seconds in 3-tile radius
            float Radius = 96.0f; // 3 tiles
            float Duration = 7.0f;
            float TickDamage = 112.0f / (Duration / 0.25f); // 4 ticks/sec

            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* UPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!UUnit || !UTrans || !UPlayer) continue;
                if (UUnit->HitPoints <= 0) continue;
                if (UPlayer->PlayerID == CasterPlayer->PlayerID) continue;

                float D = UTrans->Position.DistanceTo(Pos);
                if (D <= Radius)
                {
                    int Dmg = std::max(1, (int)TickDamage);
                    if (UUnit->ShieldPoints > 0)
                    {
                        if (Dmg <= UUnit->ShieldPoints)
                        { UUnit->ShieldPoints -= Dmg; Dmg = 0; }
                        else
                        { Dmg -= UUnit->ShieldPoints; UUnit->ShieldPoints = 0; }
                    }
                    UUnit->HitPoints = std::max(0, UUnit->HitPoints - Dmg);
                    if (UUnit->HitPoints <= 0)
                    {
                        RecordUnitDeath(EM, Uid, CasterID, true);
                        EM.DestroyEntity(Uid);
                    }
                }
            }
            Logger::Info("SpellSystem", "Psionic Storm cast at (" +
                std::to_string((int)Pos.X) + "," + std::to_string((int)Pos.Y) + ")");
            break;
        }

        case SpellType::YamatoGun:
        {
            if (TargetEntityID >= 0)
            {
                CombatComponent FakeCombat;
                FakeCombat.GroundDmgType = DamageType::Normal;
                FakeCombat.GroundDamage = 260;
                FakeCombat.AirDamage = 260;
                FakeCombat.WeaponBeh = WeaponBehavior::AppearOnTarget;
                FakeCombat.Splash = SplashType::None;
                FakeCombat.CanTargetGround = true;
                FakeCombat.CanTargetAir = true;
                FakeCombat.AttackRange = 10.0f;
                FakeCombat.AttackCooldown = 0.0f;
                CombatSystem::ApplyDamage(EM, TargetEntityID, FakeCombat, CasterID, 260);
            }
            Logger::Info("SpellSystem", "Yamato Gun fired");
            break;
        }

        case SpellType::Lockdown:
        {
            if (TargetEntityID >= 0)
            {
                auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
                if (!Status)
                    Status = &EM.AddComponent<StatusEffectComponent>(TargetEntityID);
                Status->AddEffect(StatusEffect::Lockdown, 15.0f, CasterID);
            }
            Logger::Info("SpellSystem", "Lockdown applied");
            break;
        }

        case SpellType::EMPShockwave:
        {
            float Radius = 96.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* UPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!UUnit || !UTrans || !UPlayer) continue;

                float D = UTrans->Position.DistanceTo(Pos);
                if (D <= Radius)
                {
                    UUnit->ShieldPoints = 0;
                    UUnit->EnergyPoints = 0;
                    if (auto* Cloak = EM.GetComponent<CloakComponent>(Uid);
                        Cloak && Cloak->IsCloaked && !Cloak->IsPassiveCloak && !Cloak->IsBurrowed)
                    {
                        Cloak->IsCloaked = false;
                    }
                    auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                    if (!Status)
                        Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                    Status->AddEffect(StatusEffect::EMP, 1.0f, CasterID);
                }
            }
            Logger::Info("SpellSystem", "EMP Shockwave detonated");
            break;
        }

        case SpellType::ScannerSweep:
        {
            constexpr float Radius = 160.0f;
            int RevealID = EM.CreateEntity();
            EM.AddComponent<TransformComponent>(RevealID).Position = Pos;
            auto& Reveal = EM.AddComponent<RevealAreaComponent>(RevealID);
            Reveal.PlayerID = CasterPlayer->PlayerID;
            Reveal.SightRange = std::max(1, (int)(Radius / 32.0f));
            Reveal.DurationRemaining = 8.0f;
            Reveal.ProvidesDetection = true;
            Logger::Info("SpellSystem", "Scanner Sweep at (" +
                std::to_string((int)Pos.X) + "," + std::to_string((int)Pos.Y) + ")");
            break;
        }

        case SpellType::StasisField:
        {
            float Radius = 64.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                if (!UUnit || !UTrans) continue;
                if (UTrans->Position.DistanceTo(Pos) <= Radius)
                {
                    auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                    if (!Status)
                        Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                    Status->AddEffect(StatusEffect::Stasis, 5.0f, CasterID);
                }
            }
            Logger::Info("SpellSystem", "Stasis Field cast");
            break;
        }

        case SpellType::Recall:
        {
            static const Core::Math::Vec2 kRecallOffsets[] =
            {
                {-32.0f, 0.0f},
                {32.0f, 0.0f},
                {0.0f, -32.0f},
                {0.0f, 32.0f},
                {-32.0f, -32.0f},
                {32.0f, -32.0f},
                {-32.0f, 32.0f},
                {32.0f, 32.0f},
            };

            constexpr float Radius = 96.0f;
            int RecalledCount = 0;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent, PlayerComponent>();
            for (int Uid : Units)
            {
                if (Uid == CasterID) continue;
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* UPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!UUnit || !UTrans || !UPlayer) continue;
                if (UUnit->HitPoints <= 0) continue;
                if (UPlayer->PlayerID != CasterPlayer->PlayerID) continue;
                if (EM.GetComponent<BuildingComponent>(Uid) ||
                    EM.GetComponent<ResourceComponent>(Uid))
                {
                    continue;
                }
                if (UTrans->Position.DistanceTo(Pos) > Radius) continue;

                const auto& Offset =
                    kRecallOffsets[RecalledCount %
                                   (int)(sizeof(kRecallOffsets) / sizeof(kRecallOffsets[0]))];
                UTrans->Position = CasterTrans->Position + Offset;
                ClearMotionAfterTeleport(EM, Uid);
                ++RecalledCount;
            }

            Logger::Info("SpellSystem", "Recall cast, teleported " +
                std::to_string(RecalledCount) + " units");
            break;
        }

        case SpellType::DarkSwarm:
        {
            // DarkSwarm: protect ground units from ranged attacks
            float Radius = 96.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent>();
            for (int Uid : Units)
            {
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                if (UTrans && UTrans->Position.DistanceTo(Pos) <= Radius)
                {
                    auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                    if (!Status)
                        Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                    Status->AddEffect(StatusEffect::DarkSwarm, 8.0f, CasterID);
                }
            }
            Logger::Info("SpellSystem", "Dark Swarm cast");
            break;
        }

        case SpellType::Plague:
        {
            float Radius = 96.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* UUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* UPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!UUnit || !UTrans || !UPlayer) continue;
                if (UTrans->Position.DistanceTo(Pos) <= Radius)
                {
                    auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                    if (!Status)
                        Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                    Status->AddEffect(StatusEffect::Plague, 20.0f, CasterID);
                }
            }
            Logger::Info("SpellSystem", "Plague cast");
            break;
        }

        case SpellType::Ensnare:
        {
            constexpr float Radius = 96.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, UnitComponent>();
            for (int Uid : Units)
            {
                auto* TargetUnit = EM.GetComponent<UnitComponent>(Uid);
                auto* TargetTrans = EM.GetComponent<TransformComponent>(Uid);
                auto* TargetPlayer = EM.GetComponent<PlayerComponent>(Uid);
                if (!TargetUnit || !TargetTrans || !TargetPlayer) continue;
                if (TargetUnit->HitPoints <= 0) continue;
                if (TargetPlayer->PlayerID == CasterPlayer->PlayerID) continue;
                if (EM.GetComponent<ResourceComponent>(Uid)) continue;
                if (TargetTrans->Position.DistanceTo(Pos) > Radius) continue;

                auto* Status = EM.GetComponent<StatusEffectComponent>(Uid);
                if (!Status)
                    Status = &EM.AddComponent<StatusEffectComponent>(Uid);
                Status->AddEffect(StatusEffect::Ensnare, 8.0f, CasterID);

                if (auto* Cloak = EM.GetComponent<CloakComponent>(Uid))
                {
                    Cloak->IsCloaked = false;
                    Cloak->IsBurrowed = false;
                }
            }
            Logger::Info("SpellSystem", "Ensnare cast");
            break;
        }

        case SpellType::Parasite:
        {
            if (TargetEntityID >= 0)
            {
                auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
                if (!Status)
                    Status = &EM.AddComponent<StatusEffectComponent>(TargetEntityID);
                Status->AddEffect(StatusEffect::Parasite, 999.0f, CasterID);
            }
            Logger::Info("SpellSystem", "Parasite cast");
            break;
        }

        case SpellType::Hallucination:
        {
            // Create 2 illusory copies near the caster
            auto* CasterRace = EM.GetComponent<UnitComponent>(CasterID);
            if (CasterRace)
            {
                auto* CasterRender = EM.GetComponent<RenderComponent>(CasterID);
                for (int I = 0; I < 2; I++)
                {
                    int IllusionID = EM.CreateEntity();
                    auto& ITrans = EM.AddComponent<TransformComponent>(IllusionID);
                    auto& IUnit = EM.AddComponent<UnitComponent>(IllusionID);
                    auto& IPlayer = EM.AddComponent<PlayerComponent>(IllusionID);
                    auto& IStatus = EM.AddComponent<StatusEffectComponent>(IllusionID);

                    ITrans.Position = CasterTrans->Position;
                    ITrans.Position.X += (float)((I == 0 ? -32.0f : 32.0f));
                    IUnit.UnitRace = CasterRace->UnitRace;
                    IUnit.ChkUnitID = CasterRace->ChkUnitID;
                    IUnit.HitPoints = 1;
                    IUnit.MaxHitPoints = 1;
                    if (CasterPlayer)
                        IPlayer = *CasterPlayer;
                    if (CasterRender)
                        EM.AddComponent<RenderComponent>(IllusionID) = *CasterRender;
                    IStatus.AddEffect(StatusEffect::Hallucination, 180.0f, CasterID);
                }
            }
            Logger::Info("SpellSystem", "Hallucination cast");
            break;
        }

        case SpellType::SpawnBroodling:
        {
            if (TargetEntityID >= 0)
            {
                auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
                if (TargetUnit && TargetUnit->HitPoints > 0)
                {
                    // Kill target, spawn 2 broodlings
                    TargetUnit->HitPoints = 0;
                    RecordUnitDeath(EM, TargetEntityID, CasterID, true);
                    EM.DestroyEntity(TargetEntityID);

                    const int BroodlingDefID = Game::Races::GetUnitDefID("Broodling");
                    const auto BroodlingDef = Game::Races::GetUnitDefByID(BroodlingDefID);
                    for (int I = 0; I < 2 && BroodlingDef.Name != "Unknown"; I++)
                    {
                        int BroodID = EM.CreateEntity();
                        auto& BTrans = EM.AddComponent<TransformComponent>(BroodID);
                        auto& BUnit = EM.AddComponent<UnitComponent>(BroodID);
                        auto& BPlayer = EM.AddComponent<PlayerComponent>(BroodID);
                        auto& BCombat = EM.AddComponent<CombatComponent>(BroodID);
                        auto& BCommand = EM.AddComponent<CommandComponent>(BroodID);
                        auto& BPath = EM.AddComponent<PathfindingComponent>(BroodID);
                        auto& BRender = EM.AddComponent<RenderComponent>(BroodID);

                        BTrans.Position = Pos;
                        BTrans.Position.X += (float)(I * 24.0f);
                        BUnit.UnitRace = BroodlingDef.RaceType;
                        BUnit.ChkUnitID = BroodlingDef.ChkUnitID;
                        BUnit.HitPoints = BroodlingDef.HitPoints;
                        BUnit.MaxHitPoints = BroodlingDef.HitPoints;
                        BUnit.ShieldPoints = BroodlingDef.ShieldPoints;
                        BUnit.MaxShieldPoints = BroodlingDef.ShieldPoints;
                        BUnit.EnergyPoints = BroodlingDef.EnergyPoints;
                        BUnit.MaxEnergyPoints = BroodlingDef.EnergyPoints;
                        BUnit.Armor = BroodlingDef.Armor;
                        BUnit.Speed = BroodlingDef.Speed;
                        BUnit.SightRange = BroodlingDef.SightRange;
                        BUnit.SupplyCost = BroodlingDef.SupplyCost;
                        BUnit.SizeType = BroodlingDef.SizeType;
                        BPlayer = *CasterPlayer;

                        Races::ApplyCombatDefinition(BCombat, BroodlingDef, false);

                        BCommand.CurrentCommand = CommandType::Stop;
                        BPath.NodeTolerance = 8.0f;
                        BPath.IsAir = false;

                        BRender.ImageID = (uint32_t)BroodlingDef.ChkUnitID;
                        BRender.FrameIndex = 0;
                        BRender.TotalFrames = 1;
                        BRender.Tint = Core::Colors::White();
                        BRender.Layer = RenderLayer::Units;
                    }
                }
            }
            Logger::Info("SpellSystem", "Spawn Broodling cast");
            break;
        }

        case SpellType::Consume:
        {
            if (TargetEntityID >= 0)
            {
                if (auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
                    TargetUnit && TargetUnit->HitPoints > 0)
                {
                    TargetUnit->HitPoints = 0;
                    RecordUnitDeath(EM, TargetEntityID, CasterID, true);
                    EM.DestroyEntity(TargetEntityID);
                    UpgradeSystem::RefreshUnitEnergyCap(EM, CasterID);
                    const int MaxEnergy = std::max(
                        CasterUnit->MaxEnergyPoints,
                        UpgradeSystem::GetEffectiveMaxEnergyPoints(EM, CasterID));
                    CasterUnit->MaxEnergyPoints = MaxEnergy;
                    CasterUnit->EnergyPoints =
                        std::min(MaxEnergy, CasterUnit->EnergyPoints + 50);
                }
            }
            Logger::Info("SpellSystem", "Consume cast");
            break;
        }

        default: break;
    }

    return true;
}

void SpellSystem::Update(float DeltaTime)
{
    if (DeltaTime <= 0.0f) return;

    auto& EM = mSystemMgr.GetEntityManager();
    auto Entities = EM.GetEntitiesWith<Components::SpellCooldown>();
    for (int Id : Entities)
    {
        auto* SC = EM.GetComponent<Components::SpellCooldown>(Id);
        if (SC && SC->CooldownRemaining > 0.0f)
            SC->CooldownRemaining -= DeltaTime;
    }
}

} // namespace Game::Systems
