#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Systems/UpgradeSystem.h"

namespace Core::ECS {

// World-scoped Upgrade & Tech Resource stored inside EntityManager::Context()
struct WorldUpgradeResource {
    static constexpr size_t kMaxPlayers = 12;
    std::array<Game::Systems::PlayerUpgrades, kMaxPlayers> PlayerUpgrades{};
};

struct EntityStateRecord {
    EntityID Id = InvalidEntity;
    uint32_t Generation = 0;
    bool HasTransform = false;
    Game::Components::TransformComponent Transform{};
    bool HasUnit = false;
    Game::Components::UnitComponent Unit{};
    bool HasPlayer = false;
    Game::Components::PlayerComponent Player{};
    bool HasCombat = false;
    Game::Components::CombatComponent Combat{};
    bool HasRender = false;
    Game::Components::RenderComponent Render{};
};

struct WorldDeterministicSnapshot {
    uint32_t FrameNumber = 0;
    uint32_t StateHashFNV1a = 0;
    bool HasUpgradesResource = false;
    WorldUpgradeResource Upgrades{};
    std::vector<EntityStateRecord> Entities;
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

        // Deterministically sort active entities by ID before hashing
        std::vector<EntityID> SortedIds = EM.GetActiveEntities();
        std::sort(SortedIds.begin(), SortedIds.end());

        for (EntityID Id : SortedIds) {
            MixBytes(&Id, sizeof(Id));
            if (const auto* T = EM.GetComponent<Game::Components::TransformComponent>(Id)) {
                MixBytes(&T->Position.X, sizeof(float));
                MixBytes(&T->Position.Y, sizeof(float));
                MixBytes(&T->Rotation, sizeof(float));
            }
            if (const auto* U = EM.GetComponent<Game::Components::UnitComponent>(Id)) {
                MixBytes(&U->ChkUnitID, sizeof(int));
                MixBytes(&U->HitPoints, sizeof(int));
                MixBytes(&U->ShieldPoints, sizeof(int));
                MixBytes(&U->EnergyPoints, sizeof(int));
            }
            if (const auto* P = EM.GetComponent<Game::Components::PlayerComponent>(Id)) {
                MixBytes(&P->PlayerID, sizeof(int));
            }
        }
        return Hash;
    }

    static WorldDeterministicSnapshot CaptureSnapshot(const EntityManager& EM, uint32_t FrameNumber = 0) {
        WorldDeterministicSnapshot Snap;
        Snap.FrameNumber = FrameNumber;
        if (const auto* Upg = EM.Context().GetResource<WorldUpgradeResource>()) {
            Snap.HasUpgradesResource = true;
            Snap.Upgrades = *Upg;
        }

        std::vector<EntityID> SortedIds = EM.GetActiveEntities();
        std::sort(SortedIds.begin(), SortedIds.end());
        Snap.Entities.reserve(SortedIds.size());

        for (EntityID Id : SortedIds) {
            EntityStateRecord Rec;
            Rec.Id = Id;
            Rec.Generation = EM.GetGeneration(Id);
            if (const auto* T = EM.GetComponent<Game::Components::TransformComponent>(Id)) {
                Rec.HasTransform = true;
                Rec.Transform = *T;
            }
            if (const auto* U = EM.GetComponent<Game::Components::UnitComponent>(Id)) {
                Rec.HasUnit = true;
                Rec.Unit = *U;
            }
            if (const auto* P = EM.GetComponent<Game::Components::PlayerComponent>(Id)) {
                Rec.HasPlayer = true;
                Rec.Player = *P;
            }
            if (const auto* C = EM.GetComponent<Game::Components::CombatComponent>(Id)) {
                Rec.HasCombat = true;
                Rec.Combat = *C;
            }
            if (const auto* R = EM.GetComponent<Game::Components::RenderComponent>(Id)) {
                Rec.HasRender = true;
                Rec.Render = *R;
            }
            Snap.Entities.push_back(Rec);
        }

        Snap.StateHashFNV1a = ComputeWorldHashFNV1a(EM, FrameNumber);
        return Snap;
    }

    static void RestoreSnapshot(EntityManager& EM, const WorldDeterministicSnapshot& Snap) {
        // Destroy any currently active entities not in snapshot or reset their state
        std::vector<EntityID> CurrentIds = EM.GetActiveEntities();
        for (EntityID Id : CurrentIds) {
            EM.DestroyEntity(Id);
        }

        if (Snap.HasUpgradesResource) {
            EM.Context().SetResource<WorldUpgradeResource>(Snap.Upgrades);
        } else {
            EM.Context().RemoveResource<WorldUpgradeResource>();
        }

        for (const auto& Rec : Snap.Entities) {
            EntityID NewId = EM.CreateEntityWithID(Rec.Id, Rec.Generation);
            if (Rec.HasTransform) {
                EM.AddComponent<Game::Components::TransformComponent>(NewId) = Rec.Transform;
            }
            if (Rec.HasUnit) {
                EM.AddComponent<Game::Components::UnitComponent>(NewId) = Rec.Unit;
            }
            if (Rec.HasPlayer) {
                EM.AddComponent<Game::Components::PlayerComponent>(NewId) = Rec.Player;
            }
            if (Rec.HasCombat) {
                EM.AddComponent<Game::Components::CombatComponent>(NewId) = Rec.Combat;
            }
            if (Rec.HasRender) {
                EM.AddComponent<Game::Components::RenderComponent>(NewId) = Rec.Render;
            }
        }
    }
};

} // namespace Core::ECS
