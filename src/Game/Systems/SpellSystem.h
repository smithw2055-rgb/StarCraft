#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include <vector>
#include <string>
#include <functional>

namespace Game::Systems
{

enum class SpellType : uint8_t
{
    None = 0,
    PsionicStorm,       // Protoss: AoE 112 damage over time
    YamatoGun,          // Terran: single-target 260 damage
    Lockdown,           // Terran: freeze mechanical target
    EMPShockwave,       // Terran: drain shields + energy in area
    ScannerSweep,       // Terran: reveal area for duration
    Hallucination,      // Protoss: create illusory copies
    StasisField,        // Protoss: freeze units in area
    Recall,             // Protoss: teleport units
    DarkSwarm,          // Zerg: protect ground units from ranged
    Plague,             // Zerg: drain HP of units in area
    Ensnare,            // Zerg: slow + reveal units in area
    SpawnBroodling,     // Zerg: destroy target, spawn broodlings
    Parasite,           // Zerg: grant vision of target
    Consume,            // Zerg: consume friendly for energy
    Count
};

struct SpellCooldown
{
    SpellType Type = SpellType::None;
    float CooldownRemaining = 0.0f;
};

class SpellSystem : public Core::ECS::ISystem
{
public:
    SpellSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "SpellSystem"; }

    // Check if a unit can cast a spell
    static bool CanCastSpell(Core::ECS::EntityManager& EM, int CasterID, SpellType Spell,
                             int TargetEntityID = -1, const Core::Math::Vec2& TargetPos = {},
                             std::string* OutReason = nullptr);

    // Issue a spell cast
    static bool CastSpell(Core::ECS::EntityManager& EM, int CasterID, SpellType Spell,
                          int TargetEntityID = -1,
                          const Core::Math::Vec2& TargetPos = {});

    // Energy costs
    static int GetSpellEnergyCost(SpellType Spell);
    static float GetSpellCooldown(SpellType Spell);

private:
    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Systems
