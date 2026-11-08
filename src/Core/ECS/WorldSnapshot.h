#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Engine/ECS/GenericWorldSnapshot.h"

namespace Core::ECS {

// ============================================================================
// World-Scoped Economy & Upgrade State Resources (Phase 49 Engine Decoupling):
// - Pure POD structures with zero dependency on Game/ headers.
// - Re-exported into Game::Systems in HarvestSystem.h and UpgradeSystem.h.
// ============================================================================
enum class ResearchId : uint8_t
{
    None = 0,

    // Terran
    Terran_InfantryWeapons,
    Terran_InfantryArmor,
    Terran_VehicleWeapons,
    Terran_VehicleArmor,
    Terran_ShipWeapons,
    Terran_ShipArmor,
    Terran_StimPacks,
    Terran_U238Shells,
    Terran_SiegeMode,
    Terran_SpiderMines,
    Terran_CloakingField,
    Terran_IonThrusters,
    Terran_Lockdown,
    Terran_MoebiusReactor,
    Terran_OcularImplants,
    Terran_ApolloReactor,
    Terran_TitanReactor,
    Terran_CaduceusReactor,
    Terran_YamatoGun,
    Terran_ColossusReactor,
    Terran_CharonBoosters,
    Terran_PersonalCloaking,
    Terran_EMPShockwave,
    Terran_Irradiate,
    Terran_Restoration,
    Terran_OpticalFlare,

    // Zerg
    Zerg_MeleeAttacks,
    Zerg_MissileAttacks,
    Zerg_Carapace,
    Zerg_FlyerAttacks,
    Zerg_FlyerCarapace,
    Zerg_AdrenalGlands,
    Zerg_MetabolicBoost,
    Zerg_AnabolicSynthesis,
    Zerg_ChitinousPlating,
    Zerg_MuscularAugments,
    Zerg_GroovedSpines,
    Zerg_VentralSacs,
    Zerg_Antennae,
    Zerg_PneumatizedCarapace,
    Zerg_Burrow,
    Zerg_Consume,
    Zerg_Ensnare,
    Zerg_Plague,
    Zerg_SpawnBroodling,
    Zerg_QueenEnergy,
    Zerg_DefilerEnergy,
    Zerg_LurkerMorph,

    // Protoss
    Protoss_GroundWeapons,
    Protoss_GroundArmor,
    Protoss_PlasmaShields,
    Protoss_AirWeapons,
    Protoss_AirArmor,
    Protoss_SingularityCharge,
    Protoss_GraviticDrive,
    Protoss_ScarabDamage,
    Protoss_ZealotSpeed,
    Protoss_SensorArray,
    Protoss_ApialSensors,
    Protoss_KhaydarinAmulet,
    Protoss_ArgusJewel,
    Protoss_ArgusTalisman,
    Protoss_KhaydarinCore,
    Protoss_PsionicStorm,
    Protoss_Hallucination,
    Protoss_Recall,
    Protoss_StasisField,
    Protoss_MindControl,
    Protoss_Maelstrom,
    Protoss_DisruptionWeb,
    Protoss_ReaverCapacity,
    Protoss_CarrierCapacity,

    Protoss_GraviticBoosters,
    Protoss_GraviticThrusters,

    Count
};

struct PlayerUpgradesData
{
    std::array<uint8_t, static_cast<size_t>(ResearchId::Count)> Levels = {};

    int GetLevel(ResearchId Id) const
    {
        const size_t Idx = static_cast<size_t>(Id);
        if (Idx < Levels.size())
            return Levels[Idx];
        return 0;
    }
};

using PlayerUpgrades = PlayerUpgradesData;

struct PlayerUpgradeStore
{
    static constexpr size_t kMaxPlayers = 12;
    std::array<PlayerUpgrades, kMaxPlayers> Upgrades{};
    std::array<std::array<bool, static_cast<size_t>(ResearchId::Count)>, kMaxPlayers> Researching{};
};

struct WorldUpgradeResource {
    static constexpr size_t kMaxPlayers = 12;
    std::array<PlayerUpgradesData, kMaxPlayers> PlayerUpgrades{};
};

inline constexpr int kPlayerResourceSlots = 12;

struct PlayerResources
{
    int Minerals = 400;
    int VespeneGas = 100;
    int SupplyUsed = 0;
    int SupplyMax = 10;
};

struct PlayerResourceStore
{
    std::array<PlayerResources, kPlayerResourceSlots> Players{};
};

// Lightweight entity identity header retained for backwards compatibility
struct EntityStateRecord {
    EntityID Id = InvalidEntity;
    uint32_t Generation = 0;
};

// ============================================================================
// WorldDeterministicSnapshot (Phase 37 & Phase 49 Unification):
// - Zero dependency on Game/ headers.
// - Delegates all component pool capture & restore to the game-agnostic
//   Engine::ECS::GenericWorldSnapshot.
// ============================================================================
struct WorldDeterministicSnapshot {
    uint32_t FrameNumber = 0;
    uint32_t StateHashFNV1a = 0;
    bool HasUpgradesResource = false;
    WorldUpgradeResource Upgrades{};
    bool HasEconomyResource = false;
    PlayerResourceStore Economy{};
    std::vector<EntityStateRecord> Entities;
    Engine::ECS::GenericWorldSnapshot GenericSnapshot;
};

class WorldSnapshotSerializer {
public:
    static uint32_t ComputeWorldHashFNV1a(const EntityManager& EM, uint32_t FrameNumber = 0) {
        uint32_t Hash = 2166136261u;
        auto MixBytes = [&](const void* Data, size_t Len) {
            const auto* Bytes = static_cast<const uint8_t*>(Data);
            for (size_t i = 0; i < Len; ++i) {
                Hash ^= Bytes[i];
                Hash *= 16777619u;
            }
        };

        MixBytes(&FrameNumber, sizeof(FrameNumber));

        if (const auto* Upg = EM.Context().GetResource<WorldUpgradeResource>()) {
            MixBytes(Upg, sizeof(WorldUpgradeResource));
        }

        if (const auto* Econ = EM.Context().GetResource<PlayerResourceStore>()) {
            MixBytes(Econ, sizeof(PlayerResourceStore));
        }

        const auto& Hooks = EngineAdapterHooks::Instance();
        if (Hooks.MixWorldComponentsHash) {
            Hooks.MixWorldComponentsHash(EM, Hash);
            return Hash;
        }

        // Pure engine fallback when no game-specific hash hook is registered
        Engine::ECS::GenericWorldSnapshot Generic;
        Generic.Capture(EM, FrameNumber);
        const uint32_t GenericHash = Generic.ComputeBinaryHashFNV1a();
        MixBytes(&GenericHash, sizeof(GenericHash));
        return Hash;
    }

    static WorldDeterministicSnapshot CaptureSnapshot(const EntityManager& EM, uint32_t FrameNumber = 0) {
        WorldDeterministicSnapshot Snap;
        Snap.FrameNumber = FrameNumber;
        if (const auto* Upg = EM.Context().GetResource<WorldUpgradeResource>()) {
            Snap.HasUpgradesResource = true;
            Snap.Upgrades = *Upg;
        }
        if (const auto* Econ = EM.Context().GetResource<PlayerResourceStore>()) {
            Snap.HasEconomyResource = true;
            Snap.Economy = *Econ;
        }

        Snap.GenericSnapshot.Capture(EM, FrameNumber);
        Snap.Entities.reserve(Snap.GenericSnapshot.GetEntities().size());
        for (const auto& Ent : Snap.GenericSnapshot.GetEntities()) {
            Snap.Entities.push_back({Ent.Id, Ent.Generation});
        }

        Snap.StateHashFNV1a = ComputeWorldHashFNV1a(EM, FrameNumber);
        return Snap;
    }

    static void RestoreSnapshot(EntityManager& EM, const WorldDeterministicSnapshot& Snap) {
        Snap.GenericSnapshot.Restore(EM);

        if (Snap.HasUpgradesResource) {
            EM.Context().SetResource<WorldUpgradeResource>(Snap.Upgrades);
        } else {
            EM.Context().RemoveResource<WorldUpgradeResource>();
        }

        if (Snap.HasEconomyResource) {
            EM.Context().SetResource<PlayerResourceStore>(Snap.Economy);
        } else {
            EM.Context().RemoveResource<PlayerResourceStore>();
        }
    }
};

} // namespace Core::ECS

namespace Game::Systems {
using ResearchId = Core::ECS::ResearchId;
using PlayerUpgrades = Core::ECS::PlayerUpgrades;
using PlayerUpgradeStore = Core::ECS::PlayerUpgradeStore;
using Core::ECS::kPlayerResourceSlots;
using PlayerResources = Core::ECS::PlayerResources;
using PlayerResourceStore = Core::ECS::PlayerResourceStore;
} // namespace Game::Systems
