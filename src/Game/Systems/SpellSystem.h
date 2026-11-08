#pragma once

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"
#include "Engine/Gameplay/AbilityAndStatusServer2D.h"
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
    DefensiveMatrix,    // Terran: 250 HP barrier
    Irradiate,          // Terran: AoE radiation to organic target
    Restoration,        // Terran: dispel debuffs
    OpticalFlare,       // Terran: blind target (sight to 1, disable detector)
    Feedback,           // Protoss: drain target energy, deal equal damage
    Maelstrom,          // Protoss: freeze organic units in area
    MindControl,        // Protoss: take permanent control of target unit
    DisruptionWeb,      // Protoss: ground units inside cannot attack
    Infestation,        // Zerg: infest damaged Terran Command Center
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
    using ImageIDResolver = std::function<uint32_t(int)>;

    SpellSystem(Core::ECS::SystemManager& SysMgr) : mSystemMgr(SysMgr) {}
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "SpellSystem"; }

    static void SetImageResolver(ImageIDResolver Resolver);

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

    static void BindEngineAbilityServer(Engine::Gameplay::AbilityAndStatusServer2D* Server) noexcept { sEngineAbilityServer = Server; }
    static Engine::Gameplay::AbilityAndStatusServer2D* GetBoundEngineAbilityServer() noexcept { return sEngineAbilityServer; }

private:
    Core::ECS::SystemManager& mSystemMgr;
    float mStormTickTimer = 0.0f;
    inline static Engine::Gameplay::AbilityAndStatusServer2D* sEngineAbilityServer = nullptr;
};

} // namespace Game::Systems
