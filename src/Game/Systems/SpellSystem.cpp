#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Components.h"
#include "Core/Utils/Logger.h"

#include <algorithm>

namespace Game::Systems
{

using namespace Game::Components;
using Core::Utils::Logger;

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
    auto* CasterUnit = EM.GetComponent<UnitComponent>(CasterID);
    auto* CasterTrans = EM.GetComponent<TransformComponent>(CasterID);
    if (!CasterUnit || !CasterTrans)
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
                    if (UUnit->HitPoints <= 0) EM.DestroyEntity(Uid);
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
            // Reveal area - mark cloaked units as detected in area
            float Radius = 160.0f;
            auto Units = EM.GetEntitiesWith<TransformComponent, CloakComponent>();
            for (int Uid : Units)
            {
                auto* UTrans = EM.GetComponent<TransformComponent>(Uid);
                if (UTrans && UTrans->Position.DistanceTo(Pos) <= Radius)
                {
                    auto* Cloak = EM.GetComponent<CloakComponent>(Uid);
                    if (Cloak) Cloak->IsCloaked = false;
                }
            }
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
            if (TargetEntityID >= 0)
            {
                auto* TargetUnit = EM.GetComponent<UnitComponent>(TargetEntityID);
                if (TargetUnit)
                {
                    TargetUnit->Speed *= 0.5f;
                    auto* Status = EM.GetComponent<StatusEffectComponent>(TargetEntityID);
                    if (!Status)
                        Status = &EM.AddComponent<StatusEffectComponent>(TargetEntityID);
                    Status->AddEffect(StatusEffect::Ensnare, 8.0f, CasterID);
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
                for (int I = 0; I < 2; I++)
                {
                    int IllusionID = EM.CreateEntity();
                    auto& ITrans = EM.AddComponent<TransformComponent>(IllusionID);
                    auto& IUnit = EM.AddComponent<UnitComponent>(IllusionID);
                    auto& IPlayer = EM.AddComponent<PlayerComponent>(IllusionID);

                    ITrans.Position = CasterTrans->Position;
                    ITrans.Position.X += (float)((I == 0 ? -32.0f : 32.0f));
                    IUnit.UnitRace = CasterRace->UnitRace;
                    IUnit.ChkUnitID = CasterRace->ChkUnitID;
                    IUnit.HitPoints = 1;
                    IUnit.MaxHitPoints = 1;
                    IPlayer.PlayerID = CasterPlayer->PlayerID;
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
                    EM.DestroyEntity(TargetEntityID);

                    for (int I = 0; I < 2; I++)
                    {
                        int BroodID = EM.CreateEntity();
                        auto& BTrans = EM.AddComponent<TransformComponent>(BroodID);
                        auto& BUnit = EM.AddComponent<UnitComponent>(BroodID);
                        auto& BPlayer = EM.AddComponent<PlayerComponent>(BroodID);
                        auto& BCombat = EM.AddComponent<CombatComponent>(BroodID);

                        BTrans.Position = Pos;
                        BTrans.Position.X += (float)(I * 24.0f);
                        BUnit.UnitRace = Race::Zerg;
                        BUnit.ChkUnitID = 0x28; // Broodling
                        BUnit.HitPoints = 30;
                        BUnit.MaxHitPoints = 30;
                        BUnit.Speed = 2.5f;
                        BUnit.SightRange = 5;
                        BUnit.SizeType = UnitSize::Small;
                        BPlayer.PlayerID = CasterPlayer->PlayerID;

                        BCombat.GroundDamage = 5;
                        BCombat.AttackRange = 1.0f;
                        BCombat.AttackCooldown = 0.5f;
                        BCombat.WeaponBeh = WeaponBehavior::Melee;
                    }
                }
            }
            Logger::Info("SpellSystem", "Spawn Broodling cast");
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
