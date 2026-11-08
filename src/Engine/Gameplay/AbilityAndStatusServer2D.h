#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Engine::Gameplay {

enum class StatusEffectKind : uint8_t {
    None = 0,
    StimPack,
    Lockdown,
    DefensiveMatrix,
    Irradiate,
    Plague,
    Ensnare,
    Parasite,
    OpticalFlare,
    StasisField,
    Maelstrom,
    AcidSpores
};

struct AbilityDefinition2D {
    uint32_t AbilityID = 0;
    std::string Name;
    int EnergyCost = 0;
    uint32_t CooldownTicks = 0;
    float CastRangePx = 0.0f;
    float AoERadiusPx = 0.0f;
    StatusEffectKind AppliedStatus = StatusEffectKind::None;
    uint32_t StatusDurationTicks = 0;
    int StatusMagnitude = 0;
};

struct ActiveStatusEffect2D {
    StatusEffectKind Kind = StatusEffectKind::None;
    uint32_t RemainingTicks = 0;
    uint32_t TotalDurationTicks = 0;
    int Magnitude = 0;
};

struct CasterEnergyPool2D {
    int CurrentSubUnits = 50 * 256;
    int MaxSubUnits = 200 * 256;
    std::unordered_map<uint32_t, uint32_t> AbilityCooldowns;
};

struct AbilityCastEvent2D {
    int CasterEntityID = -1;
    int TargetEntityID = -1;
    uint32_t AbilityID = 0;
    Core::Math::Vec2 CasterPos{0.0f, 0.0f};
    Core::Math::Vec2 TargetPos{0.0f, 0.0f};
    float AoERadiusPx = 0.0f;
    StatusEffectKind AppliedStatus = StatusEffectKind::None;
    uint32_t StatusDurationTicks = 0;
    int StatusMagnitude = 0;
};

class AbilityAndStatusServer2D {
public:
    static constexpr int kEnergySubUnitsPerWhole = 256;
    static constexpr int kDefaultRegenSubUnitsPerTick = 8;
    static constexpr int kDefaultInitialEnergyWhole = 50;
    static constexpr int kDefaultMaxEnergyWhole = 200;
    static constexpr int kUpgradedMaxEnergyWhole = 250;

    AbilityAndStatusServer2D() = default;
    ~AbilityAndStatusServer2D() = default;

    void Clear() {
        m_Casters.clear();
        m_Abilities.clear();
        m_EntityStatuses.clear();
        m_RecentCastEvents.clear();
        m_TotalAbilitiesCast = 0;
        m_TotalExpiredStatuses = 0;
    }

    void RegisterCaster(int entityID,
                        int initialEnergyWhole = kDefaultInitialEnergyWhole,
                        int maxEnergyWhole = kDefaultMaxEnergyWhole) {
        if (entityID < 0) {
            return;
        }
        const int clampedMax = std::max(0, maxEnergyWhole);
        const int clampedInit = std::clamp(initialEnergyWhole, 0, clampedMax);

        CasterEnergyPool2D pool{};
        pool.MaxSubUnits = clampedMax * kEnergySubUnitsPerWhole;
        pool.CurrentSubUnits = clampedInit * kEnergySubUnitsPerWhole;
        m_Casters[entityID] = std::move(pool);
    }

    void UnregisterEntity(int entityID) {
        m_Casters.erase(entityID);
        m_EntityStatuses.erase(entityID);
    }

    bool IsCasterRegistered(int entityID) const {
        return m_Casters.find(entityID) != m_Casters.end();
    }

    int GetCasterEnergyWhole(int entityID) const {
        const auto it = m_Casters.find(entityID);
        if (it == m_Casters.end()) {
            return 0;
        }
        return it->second.CurrentSubUnits / kEnergySubUnitsPerWhole;
    }

    int GetCasterEnergySubUnits(int entityID) const {
        const auto it = m_Casters.find(entityID);
        if (it == m_Casters.end()) {
            return 0;
        }
        return it->second.CurrentSubUnits;
    }

    int GetCasterMaxEnergyWhole(int entityID) const {
        const auto it = m_Casters.find(entityID);
        if (it == m_Casters.end()) {
            return 0;
        }
        return it->second.MaxSubUnits / kEnergySubUnitsPerWhole;
    }

    void SetCasterMaxEnergyWhole(int entityID, int maxEnergyWhole) {
        auto it = m_Casters.find(entityID);
        if (it == m_Casters.end()) {
            return;
        }
        const int clampedMaxSubUnits = std::max(0, maxEnergyWhole) * kEnergySubUnitsPerWhole;
        it->second.MaxSubUnits = clampedMaxSubUnits;
        if (it->second.CurrentSubUnits > clampedMaxSubUnits) {
            it->second.CurrentSubUnits = clampedMaxSubUnits;
        }
    }

    void SetCasterEnergyWhole(int entityID, int energyWhole) {
        auto it = m_Casters.find(entityID);
        if (it == m_Casters.end()) {
            return;
        }
        const int targetSubUnits = std::max(0, energyWhole) * kEnergySubUnitsPerWhole;
        it->second.CurrentSubUnits = std::clamp(targetSubUnits, 0, it->second.MaxSubUnits);
    }

    bool ConsumeEnergy(int entityID, int energyCostWhole) {
        if (energyCostWhole <= 0) {
            return true;
        }
        auto it = m_Casters.find(entityID);
        if (it == m_Casters.end()) {
            return false;
        }
        const int requiredSubUnits = energyCostWhole * kEnergySubUnitsPerWhole;
        if (it->second.CurrentSubUnits < requiredSubUnits) {
            return false;
        }
        it->second.CurrentSubUnits -= requiredSubUnits;
        return true;
    }

    void DrainAllEnergy(int entityID) {
        auto it = m_Casters.find(entityID);
        if (it != m_Casters.end()) {
            it->second.CurrentSubUnits = 0;
        }
    }

    void StepEnergyRegeneration(uint32_t deltaTicks = 1,
                                int subUnitsPerTick = kDefaultRegenSubUnitsPerTick) {
        if (deltaTicks == 0) {
            return;
        }
        const int regenDelta = static_cast<int>(deltaTicks) * std::max(0, subUnitsPerTick);
        for (auto& [entityID, pool] : m_Casters) {
            if (!IsEntityFrozenOrStunned(entityID)) {
                pool.CurrentSubUnits = std::min(pool.MaxSubUnits, pool.CurrentSubUnits + regenDelta);
            }
            for (auto cdIt = pool.AbilityCooldowns.begin(); cdIt != pool.AbilityCooldowns.end();) {
                if (cdIt->second <= deltaTicks) {
                    cdIt = pool.AbilityCooldowns.erase(cdIt);
                } else {
                    cdIt->second -= deltaTicks;
                    ++cdIt;
                }
            }
        }
    }

    void RegisterAbility(const AbilityDefinition2D& def) {
        if (def.AbilityID == 0) {
            return;
        }
        m_Abilities[def.AbilityID] = def;
    }

    const AbilityDefinition2D* FindAbility(uint32_t abilityID) const {
        const auto it = m_Abilities.find(abilityID);
        return (it != m_Abilities.end()) ? &it->second : nullptr;
    }

    std::size_t GetRegisteredAbilityCount() const {
        return m_Abilities.size();
    }

    void LoadStandardRTSAbilities() {
        const std::array<AbilityDefinition2D, 16> standardAbilities = {{
            {1u,  "StimPack",        0,   15u,  0.0f,   0.0f,   StatusEffectKind::StimPack,        336u, 10},
            {2u,  "EMPShockwave",    100, 24u,  256.0f, 96.0f,  StatusEffectKind::None,            0u,   100},
            {3u,  "Lockdown",        100, 24u,  256.0f, 0.0f,   StatusEffectKind::Lockdown,        984u, 0},
            {4u,  "Irradiate",       75,  24u,  288.0f, 32.0f,  StatusEffectKind::Irradiate,       600u, 250},
            {5u,  "DefensiveMatrix", 100, 15u,  256.0f, 0.0f,   StatusEffectKind::DefensiveMatrix, 840u, 250},
            {6u,  "YamatoGun",       150, 72u,  320.0f, 0.0f,   StatusEffectKind::None,            0u,   260},
            {7u,  "PsionicStorm",    75,  45u,  288.0f, 96.0f,  StatusEffectKind::None,            112u, 112},
            {8u,  "StasisField",     100, 45u,  288.0f, 96.0f,  StatusEffectKind::StasisField,     984u, 0},
            {9u,  "Plague",          150, 45u,  288.0f, 96.0f,  StatusEffectKind::Plague,          720u, 300},
            {10u, "DarkSwarm",       100, 30u,  288.0f, 128.0f, StatusEffectKind::None,            900u, 0},
            {11u, "Ensnare",         75,  30u,  288.0f, 96.0f,  StatusEffectKind::Ensnare,         540u, 50},
            {12u, "Consume",         0,   15u,  32.0f,  0.0f,   StatusEffectKind::None,            0u,   50},
            {13u, "Parasite",        75,  24u,  384.0f, 0.0f,   StatusEffectKind::Parasite,        3600u, 0},
            {14u, "OpticalFlare",    75,  24u,  288.0f, 0.0f,   StatusEffectKind::OpticalFlare,    3600u, 0},
            {15u, "Maelstrom",       100, 45u,  320.0f, 96.0f,  StatusEffectKind::Maelstrom,       264u, 0},
            {16u, "AcidSpores",      0,   15u,  224.0f, 64.0f,  StatusEffectKind::AcidSpores,      540u, 1}
        }};

        for (const auto& def : standardAbilities) {
            RegisterAbility(def);
        }
    }

    uint32_t GetAbilityCooldownTicks(int casterEntityID, uint32_t abilityID) const {
        const auto casterIt = m_Casters.find(casterEntityID);
        if (casterIt == m_Casters.end()) {
            return 0;
        }
        const auto cdIt = casterIt->second.AbilityCooldowns.find(abilityID);
        return (cdIt != casterIt->second.AbilityCooldowns.end()) ? cdIt->second : 0;
    }

    bool TryCastAbility(int casterEntityID,
                        uint32_t abilityID,
                        int targetEntityID,
                        Core::Math::Vec2 casterPos,
                        Core::Math::Vec2 targetPos) {
        const AbilityDefinition2D* ability = FindAbility(abilityID);
        if (!ability) {
            return false;
        }

        if (IsEntityFrozenOrStunned(casterEntityID)) {
            return false;
        }

        if (ability->CastRangePx > 0.0f) {
            const float dx = targetPos.X - casterPos.X;
            const float dy = targetPos.Y - casterPos.Y;
            const float distSq = dx * dx + dy * dy;
            const float maxRange = ability->CastRangePx;
            if (distSq > maxRange * maxRange) {
                return false;
            }
        }

        if (ability->EnergyCost > 0 || IsCasterRegistered(casterEntityID)) {
            auto casterIt = m_Casters.find(casterEntityID);
            if (casterIt == m_Casters.end()) {
                if (ability->EnergyCost > 0) {
                    return false;
                }
            } else {
                const auto cdIt = casterIt->second.AbilityCooldowns.find(abilityID);
                if (cdIt != casterIt->second.AbilityCooldowns.end() && cdIt->second > 0) {
                    return false;
                }
                if (!ConsumeEnergy(casterEntityID, ability->EnergyCost)) {
                    return false;
                }
                if (ability->CooldownTicks > 0) {
                    casterIt->second.AbilityCooldowns[abilityID] = ability->CooldownTicks;
                }
            }
        }

        if (ability->Name == "Consume" && IsCasterRegistered(casterEntityID)) {
            auto& pool = m_Casters[casterEntityID];
            pool.CurrentSubUnits = std::min(
                pool.MaxSubUnits,
                pool.CurrentSubUnits + 50 * kEnergySubUnitsPerWhole
            );
        }

        if (ability->Name == "EMPShockwave" && targetEntityID >= 0 &&
            !HasStatusEffect(targetEntityID, StatusEffectKind::StasisField)) {
            DrainAllEnergy(targetEntityID);
            RemoveStatusEffect(targetEntityID, StatusEffectKind::DefensiveMatrix);
        }

        const bool isSelfCastAbility = (ability->CastRangePx <= 0.0f && ability->AoERadiusPx <= 0.0f);
        const int effectiveTargetID = (targetEntityID >= 0)
            ? targetEntityID
            : (isSelfCastAbility ? casterEntityID : -1);
        if (ability->AppliedStatus != StatusEffectKind::None &&
            ability->StatusDurationTicks > 0 &&
            effectiveTargetID >= 0) {
            ApplyStatusEffect(effectiveTargetID,
                              ability->AppliedStatus,
                              ability->StatusDurationTicks,
                              ability->StatusMagnitude);
        }

        AbilityCastEvent2D ev{};
        ev.CasterEntityID = casterEntityID;
        ev.TargetEntityID = effectiveTargetID;
        ev.AbilityID = abilityID;
        ev.CasterPos = casterPos;
        ev.TargetPos = targetPos;
        ev.AoERadiusPx = ability->AoERadiusPx;
        ev.AppliedStatus = ability->AppliedStatus;
        ev.StatusDurationTicks = ability->StatusDurationTicks;
        ev.StatusMagnitude = ability->StatusMagnitude;
        if (m_RecentCastEvents.size() >= 256) {
            m_RecentCastEvents.erase(m_RecentCastEvents.begin());
        }
        m_RecentCastEvents.push_back(ev);
        ++m_TotalAbilitiesCast;

        return true;
    }

    void ApplyStatusEffect(int targetEntityID,
                           StatusEffectKind kind,
                           uint32_t durationTicks,
                           int magnitude = 0) {
        if (targetEntityID < 0 || kind == StatusEffectKind::None || durationTicks == 0) {
            return;
        }

        if (kind != StatusEffectKind::StasisField &&
            HasStatusEffect(targetEntityID, StatusEffectKind::StasisField)) {
            return;
        }

        auto& statusList = m_EntityStatuses[targetEntityID];
        for (auto& existing : statusList) {
            if (existing.Kind == kind) {
                existing.RemainingTicks = std::max(existing.RemainingTicks, durationTicks);
                existing.TotalDurationTicks = std::max(existing.TotalDurationTicks, durationTicks);
                if (kind == StatusEffectKind::AcidSpores) {
                    existing.Magnitude = std::clamp(existing.Magnitude + std::max(1, magnitude), 1, 9);
                } else if (kind == StatusEffectKind::DefensiveMatrix) {
                    existing.Magnitude = (magnitude > 0) ? magnitude : 250;
                } else {
                    existing.Magnitude = std::max(existing.Magnitude, magnitude);
                }
                return;
            }
        }

        ActiveStatusEffect2D effect{};
        effect.Kind = kind;
        effect.RemainingTicks = durationTicks;
        effect.TotalDurationTicks = durationTicks;
        if (kind == StatusEffectKind::DefensiveMatrix && magnitude <= 0) {
            effect.Magnitude = 250;
        } else if (kind == StatusEffectKind::AcidSpores && magnitude <= 0) {
            effect.Magnitude = 1;
        } else {
            effect.Magnitude = magnitude;
        }
        statusList.push_back(effect);
    }

    bool HasStatusEffect(int targetEntityID, StatusEffectKind kind) const {
        const auto it = m_EntityStatuses.find(targetEntityID);
        if (it == m_EntityStatuses.end()) {
            return false;
        }
        for (const auto& effect : it->second) {
            if (effect.Kind == kind && effect.RemainingTicks > 0) {
                return true;
            }
        }
        return false;
    }

    uint32_t GetStatusRemainingTicks(int targetEntityID, StatusEffectKind kind) const {
        const auto it = m_EntityStatuses.find(targetEntityID);
        if (it == m_EntityStatuses.end()) {
            return 0;
        }
        for (const auto& effect : it->second) {
            if (effect.Kind == kind && effect.RemainingTicks > 0) {
                return effect.RemainingTicks;
            }
        }
        return 0;
    }

    int GetStatusMagnitude(int targetEntityID, StatusEffectKind kind) const {
        const auto it = m_EntityStatuses.find(targetEntityID);
        if (it == m_EntityStatuses.end()) {
            return 0;
        }
        for (const auto& effect : it->second) {
            if (effect.Kind == kind && effect.RemainingTicks > 0) {
                return effect.Magnitude;
            }
        }
        return 0;
    }

    void RemoveStatusEffect(int targetEntityID, StatusEffectKind kind) {
        auto it = m_EntityStatuses.find(targetEntityID);
        if (it == m_EntityStatuses.end()) {
            return;
        }
        auto& vec = it->second;
        vec.erase(
            std::remove_if(vec.begin(), vec.end(),
                           [kind](const ActiveStatusEffect2D& e) { return e.Kind == kind; }),
            vec.end()
        );
        if (vec.empty()) {
            m_EntityStatuses.erase(it);
        }
    }

    int AbsorbDamageWithDefensiveMatrix(int targetEntityID, int incomingDamage) {
        if (incomingDamage <= 0) {
            return 0;
        }
        if (HasStatusEffect(targetEntityID, StatusEffectKind::StasisField)) {
            return 0;
        }

        auto it = m_EntityStatuses.find(targetEntityID);
        if (it == m_EntityStatuses.end()) {
            return incomingDamage;
        }

        auto& vec = it->second;
        for (auto effIt = vec.begin(); effIt != vec.end(); ++effIt) {
            if (effIt->Kind == StatusEffectKind::DefensiveMatrix &&
                effIt->RemainingTicks > 0 &&
                effIt->Magnitude > 0) {
                const int absorbed = std::min(effIt->Magnitude, incomingDamage);
                effIt->Magnitude -= absorbed;
                const int remainingDamage = incomingDamage - absorbed;

                if (effIt->Magnitude <= 0) {
                    vec.erase(effIt);
                    if (vec.empty()) {
                        m_EntityStatuses.erase(it);
                    }
                }
                return remainingDamage;
            }
        }

        return incomingDamage;
    }

    float GetMoveSpeedMultiplier(int targetEntityID) const {
        const auto it = m_EntityStatuses.find(targetEntityID);
        if (it == m_EntityStatuses.end()) {
            return 1.0f;
        }

        bool hasStim = false;
        bool hasEnsnare = false;

        for (const auto& effect : it->second) {
            if (effect.RemainingTicks == 0) {
                continue;
            }
            if (effect.Kind == StatusEffectKind::Lockdown ||
                effect.Kind == StatusEffectKind::StasisField ||
                effect.Kind == StatusEffectKind::Maelstrom) {
                return 0.0f;
            }
            if (effect.Kind == StatusEffectKind::StimPack) {
                hasStim = true;
            } else if (effect.Kind == StatusEffectKind::Ensnare) {
                hasEnsnare = true;
            }
        }

        if (hasStim && hasEnsnare) {
            return 0.75f;
        }
        if (hasEnsnare) {
            return 0.5f;
        }
        if (hasStim) {
            return 1.5f;
        }
        return 1.0f;
    }

    float GetAttackCooldownMultiplier(int targetEntityID) const {
        if (IsEntityFrozenOrStunned(targetEntityID)) {
            return 0.0f;
        }
        float multiplier = 1.0f;
        if (HasStatusEffect(targetEntityID, StatusEffectKind::StimPack)) {
            multiplier *= 0.6666667f;
        }
        if (HasStatusEffect(targetEntityID, StatusEffectKind::Ensnare)) {
            multiplier *= 1.25f;
        }
        const int acidStacks = GetStatusMagnitude(targetEntityID, StatusEffectKind::AcidSpores);
        if (acidStacks > 0) {
            multiplier *= (1.0f + 0.1f * static_cast<float>(acidStacks));
        }
        return multiplier;
    }

    bool IsEntityFrozenOrStunned(int targetEntityID) const {
        return HasStatusEffect(targetEntityID, StatusEffectKind::Lockdown) ||
               HasStatusEffect(targetEntityID, StatusEffectKind::StasisField) ||
               HasStatusEffect(targetEntityID, StatusEffectKind::Maelstrom);
    }

    std::size_t StepStatusEffects(uint32_t deltaTicks = 1) {
        if (deltaTicks == 0) {
            return 0;
        }

        std::size_t expiredCount = 0;
        for (auto it = m_EntityStatuses.begin(); it != m_EntityStatuses.end();) {
            auto& vec = it->second;
            for (auto effIt = vec.begin(); effIt != vec.end();) {
                if (effIt->RemainingTicks <= deltaTicks) {
                    effIt = vec.erase(effIt);
                    ++expiredCount;
                    ++m_TotalExpiredStatuses;
                } else {
                    effIt->RemainingTicks -= deltaTicks;
                    ++effIt;
                }
            }
            if (vec.empty()) {
                it = m_EntityStatuses.erase(it);
            } else {
                ++it;
            }
        }

        return expiredCount;
    }

    const std::vector<AbilityCastEvent2D>& GetRecentCastEvents() const {
        return m_RecentCastEvents;
    }

    void ClearRecentCastEvents() {
        m_RecentCastEvents.clear();
    }

    uint64_t GetTotalAbilitiesCast() const {
        return m_TotalAbilitiesCast;
    }

    uint64_t GetTotalExpiredStatuses() const {
        return m_TotalExpiredStatuses;
    }

private:
    std::unordered_map<int, CasterEnergyPool2D> m_Casters;
    std::unordered_map<uint32_t, AbilityDefinition2D> m_Abilities;
    std::unordered_map<int, std::vector<ActiveStatusEffect2D>> m_EntityStatuses;
    std::vector<AbilityCastEvent2D> m_RecentCastEvents;
    uint64_t m_TotalAbilitiesCast = 0;
    uint64_t m_TotalExpiredStatuses = 0;
};

} // namespace Engine::Gameplay
