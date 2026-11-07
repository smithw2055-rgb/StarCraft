#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"

namespace Core::ECS {

// World-scoped Upgrade & Tech Resource stored inside EntityManager::Context()
struct WorldUpgradeResource {
    static constexpr size_t kMaxPlayers = 12;
    std::array<Game::Systems::PlayerUpgrades, kMaxPlayers> PlayerUpgrades{};
};

// Complete deterministic entity state record covering all simulation-affecting ECS components
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

    bool HasCommand = false;
    Game::Components::CommandComponent Command{};

    bool HasPathfinding = false;
    Game::Components::PathfindingComponent Pathfinding{};

    bool HasBuilding = false;
    Game::Components::BuildingComponent Building{};

    bool HasResource = false;
    Game::Components::ResourceComponent Resource{};

    bool HasHarvest = false;
    Game::Components::HarvestComponent Harvest{};

    bool HasProduction = false;
    Game::Components::ProductionComponent Production{};

    bool HasBullet = false;
    Game::Components::BulletComponent Bullet{};

    bool HasStatusEffect = false;
    Game::Components::StatusEffectComponent StatusEffect{};

    bool HasCloak = false;
    Game::Components::CloakComponent Cloak{};

    bool HasBurrow = false;
    Game::Components::BurrowComponent Burrow{};

    bool HasLoadedUnits = false;
    Game::Components::LoadedUnitsComponent LoadedUnits{};

    bool HasMorph = false;
    Game::Components::MorphComponent Morph{};

    bool HasSiegeTransition = false;
    Game::Components::SiegeTransitionComponent SiegeTransition{};

    bool HasSpiderMine = false;
    Game::Components::SpiderMineComponent SpiderMine{};

    bool HasNuke = false;
    Game::Components::NukeComponent Nuke{};

    bool HasArchonWarp = false;
    Game::Components::ArchonWarpComponent ArchonWarp{};
};

struct WorldDeterministicSnapshot {
    uint32_t FrameNumber = 0;
    uint32_t StateHashFNV1a = 0;
    bool HasUpgradesResource = false;
    WorldUpgradeResource Upgrades{};
    bool HasEconomyResource = false;
    Game::Systems::PlayerResourceStore Economy{};
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
        auto MixFloat = [&](float Value) {
            const float Canonical = (Value == 0.0f) ? 0.0f : Value;
            MixBytes(&Canonical, sizeof(float));
        };

        MixBytes(&FrameNumber, sizeof(FrameNumber));

        if (const auto* Upg = EM.Context().GetResource<WorldUpgradeResource>()) {
            MixBytes(Upg, sizeof(WorldUpgradeResource));
        }

        if (const auto* Econ = EM.Context().GetResource<Game::Systems::PlayerResourceStore>()) {
            MixBytes(Econ, sizeof(Game::Systems::PlayerResourceStore));
        }

        // Deterministically sort active entities by ID before hashing
        std::vector<EntityID> SortedIds = EM.GetActiveEntities();
        std::sort(SortedIds.begin(), SortedIds.end());

        for (EntityID Id : SortedIds) {
            const uint32_t Gen = EM.GetGeneration(Id);
            MixBytes(&Id, sizeof(Id));
            MixBytes(&Gen, sizeof(Gen));

            if (const auto* T = EM.GetComponent<Game::Components::TransformComponent>(Id)) {
                MixFloat(T->Position.X);
                MixFloat(T->Position.Y);
                MixFloat(T->Rotation);
            }
            if (const auto* U = EM.GetComponent<Game::Components::UnitComponent>(Id)) {
                MixBytes(&U->ChkUnitID, sizeof(int));
                MixBytes(&U->HitPoints, sizeof(int));
                MixBytes(&U->ShieldPoints, sizeof(int));
                MixBytes(&U->EnergyPoints, sizeof(int));
                MixBytes(&U->HangarCount, sizeof(int));
            }
            if (const auto* P = EM.GetComponent<Game::Components::PlayerComponent>(Id)) {
                MixBytes(&P->PlayerID, sizeof(int));
            }
            if (const auto* C = EM.GetComponent<Game::Components::CombatComponent>(Id)) {
                MixBytes(&C->TargetEntityID, sizeof(int));
                MixFloat(C->CooldownRemaining);
            }
            if (const auto* Cmd = EM.GetComponent<Game::Components::CommandComponent>(Id)) {
                MixBytes(&Cmd->CurrentCommand, sizeof(Cmd->CurrentCommand));
                MixFloat(Cmd->TargetPosition.X);
                MixFloat(Cmd->TargetPosition.Y);
                MixBytes(&Cmd->TargetEntityID, sizeof(int));
                MixBytes(&Cmd->CurrentOrderIndex, sizeof(int));
                const uint32_t OrderCount = static_cast<uint32_t>(Cmd->Orders.size());
                MixBytes(&OrderCount, sizeof(OrderCount));
                for (const auto& Ord : Cmd->Orders) {
                    MixBytes(&Ord.Type, sizeof(Ord.Type));
                    MixFloat(Ord.TargetPosition.X);
                    MixFloat(Ord.TargetPosition.Y);
                    MixBytes(&Ord.TargetEntityID, sizeof(int));
                    MixBytes(&Ord.AbilityID, sizeof(int));
                }
            }
            if (const auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(Id)) {
                MixBytes(&Path->HasFixedPosition, sizeof(bool));
                if (Path->HasFixedPosition) {
                    MixBytes(&Path->FixedPosition.X.raw_value, sizeof(int32_t));
                    MixBytes(&Path->FixedPosition.Y.raw_value, sizeof(int32_t));
                    MixBytes(&Path->FixedVelocity.X.raw_value, sizeof(int32_t));
                    MixBytes(&Path->FixedVelocity.Y.raw_value, sizeof(int32_t));
                }
                MixBytes(&Path->PathIndex, sizeof(int));
                const uint32_t WaypointCount = static_cast<uint32_t>(Path->Path.size());
                MixBytes(&WaypointCount, sizeof(WaypointCount));
                for (const auto& Wp : Path->Path) {
                    MixFloat(Wp.X);
                    MixFloat(Wp.Y);
                }
            }
            if (const auto* B = EM.GetComponent<Game::Components::BuildingComponent>(Id)) {
                MixBytes(&B->BuildingDefID, sizeof(int));
                MixFloat(B->BuildProgress);
                MixBytes(&B->UnderConstruction, sizeof(bool));
                MixBytes(&B->IsLifted, sizeof(bool));
                MixBytes(&B->LarvaCount, sizeof(int));
                MixBytes(&B->IsResearching, sizeof(bool));
                MixBytes(&B->ResearchingID, sizeof(int));
            }
            if (const auto* Res = EM.GetComponent<Game::Components::ResourceComponent>(Id)) {
                MixBytes(&Res->Type, sizeof(Res->Type));
                MixBytes(&Res->Amount, sizeof(int));
                MixBytes(&Res->IsDepleted, sizeof(bool));
            }
            if (const auto* H = EM.GetComponent<Game::Components::HarvestComponent>(Id)) {
                MixBytes(&H->State, sizeof(H->State));
                MixBytes(&H->CarryingType, sizeof(H->CarryingType));
                MixBytes(&H->CarryingAmount, sizeof(int));
                MixBytes(&H->TargetResourceID, sizeof(int));
                MixBytes(&H->ReturnBuildingID, sizeof(int));
            }
            if (const auto* Prod = EM.GetComponent<Game::Components::ProductionComponent>(Id)) {
                MixFloat(Prod->TrainingProgress);
                MixBytes(&Prod->CurrentTrainingUnitDefID, sizeof(int));
                const uint16_t QSize = static_cast<uint16_t>(Prod->Queue.size());
                MixBytes(&QSize, sizeof(QSize));
                for (uint16_t i = 0; i < QSize; ++i) {
                    const int Item = Prod->Queue[i];
                    MixBytes(&Item, sizeof(int));
                }
            }
            if (const auto* Bul = EM.GetComponent<Game::Components::BulletComponent>(Id)) {
                MixBytes(&Bul->SourceEntityID, sizeof(int));
                MixBytes(&Bul->TargetEntityID, sizeof(int));
                MixBytes(&Bul->BaseDamage, sizeof(int));
                MixFloat(Bul->LifetimeRemaining);
                MixBytes(&Bul->HasHit, sizeof(bool));
            }
            if (const auto* Status = EM.GetComponent<Game::Components::StatusEffectComponent>(Id)) {
                MixBytes(Status->ActiveEffects, sizeof(Status->ActiveEffects));
                for (int i = 0; i < Game::Components::StatusEffectComponent::kMaxEffects; ++i) {
                    MixFloat(Status->EffectTimers[i]);
                }
            }
            if (const auto* Cloak = EM.GetComponent<Game::Components::CloakComponent>(Id)) {
                MixBytes(&Cloak->IsCloaked, sizeof(bool));
                MixBytes(&Cloak->IsBurrowed, sizeof(bool));
            }
            if (const auto* Bur = EM.GetComponent<Game::Components::BurrowComponent>(Id)) {
                MixBytes(&Bur->IsBurrowed, sizeof(bool));
                MixBytes(&Bur->Phase, sizeof(Bur->Phase));
            }
            if (const auto* Loaded = EM.GetComponent<Game::Components::LoadedUnitsComponent>(Id)) {
                const uint16_t Count = static_cast<uint16_t>(Loaded->PassengerIDs.size());
                MixBytes(&Count, sizeof(Count));
                for (uint16_t i = 0; i < Count; ++i) {
                    const int Pid = Loaded->PassengerIDs[i];
                    MixBytes(&Pid, sizeof(int));
                }
            }
            if (const auto* Morph = EM.GetComponent<Game::Components::MorphComponent>(Id)) {
                MixBytes(&Morph->MorphTargetDefID, sizeof(int));
                MixFloat(Morph->MorphTimer);
                MixBytes(&Morph->IsMorphing, sizeof(bool));
            }
            if (const auto* Siege = EM.GetComponent<Game::Components::SiegeTransitionComponent>(Id)) {
                MixBytes(&Siege->Active, sizeof(bool));
                MixBytes(&Siege->TargetSieged, sizeof(bool));
                MixBytes(&Siege->Stage, sizeof(int));
                MixFloat(Siege->Elapsed);
            }
            if (const auto* Mine = EM.GetComponent<Game::Components::SpiderMineComponent>(Id)) {
                MixBytes(&Mine->IsPlaced, sizeof(bool));
                MixBytes(&Mine->IsBurrowed, sizeof(bool));
                MixBytes(&Mine->IsTriggered, sizeof(bool));
                MixBytes(&Mine->ChasingEntityID, sizeof(int));
            }
            if (const auto* Nuke = EM.GetComponent<Game::Components::NukeComponent>(Id)) {
                MixBytes(&Nuke->Armed, sizeof(bool));
                MixBytes(&Nuke->Launched, sizeof(bool));
                MixFloat(Nuke->FlightTimer);
            }
            if (const auto* Warp = EM.GetComponent<Game::Components::ArchonWarpComponent>(Id)) {
                MixBytes(&Warp->PartnerEntityID, sizeof(int));
                MixBytes(&Warp->IsDarkArchon, sizeof(bool));
                MixFloat(Warp->WarpTimer);
                MixBytes(&Warp->IsWarping, sizeof(bool));
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
        if (const auto* Econ = EM.Context().GetResource<Game::Systems::PlayerResourceStore>()) {
            Snap.HasEconomyResource = true;
            Snap.Economy = *Econ;
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
            if (const auto* Cmd = EM.GetComponent<Game::Components::CommandComponent>(Id)) {
                Rec.HasCommand = true;
                Rec.Command = *Cmd;
            }
            if (const auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(Id)) {
                Rec.HasPathfinding = true;
                Rec.Pathfinding = *Path;
            }
            if (const auto* B = EM.GetComponent<Game::Components::BuildingComponent>(Id)) {
                Rec.HasBuilding = true;
                Rec.Building = *B;
            }
            if (const auto* Res = EM.GetComponent<Game::Components::ResourceComponent>(Id)) {
                Rec.HasResource = true;
                Rec.Resource = *Res;
            }
            if (const auto* H = EM.GetComponent<Game::Components::HarvestComponent>(Id)) {
                Rec.HasHarvest = true;
                Rec.Harvest = *H;
            }
            if (const auto* Prod = EM.GetComponent<Game::Components::ProductionComponent>(Id)) {
                Rec.HasProduction = true;
                Rec.Production = *Prod;
            }
            if (const auto* Bul = EM.GetComponent<Game::Components::BulletComponent>(Id)) {
                Rec.HasBullet = true;
                Rec.Bullet = *Bul;
            }
            if (const auto* Status = EM.GetComponent<Game::Components::StatusEffectComponent>(Id)) {
                Rec.HasStatusEffect = true;
                Rec.StatusEffect = *Status;
            }
            if (const auto* Cloak = EM.GetComponent<Game::Components::CloakComponent>(Id)) {
                Rec.HasCloak = true;
                Rec.Cloak = *Cloak;
            }
            if (const auto* Bur = EM.GetComponent<Game::Components::BurrowComponent>(Id)) {
                Rec.HasBurrow = true;
                Rec.Burrow = *Bur;
            }
            if (const auto* Loaded = EM.GetComponent<Game::Components::LoadedUnitsComponent>(Id)) {
                Rec.HasLoadedUnits = true;
                Rec.LoadedUnits = *Loaded;
            }
            if (const auto* Morph = EM.GetComponent<Game::Components::MorphComponent>(Id)) {
                Rec.HasMorph = true;
                Rec.Morph = *Morph;
            }
            if (const auto* Siege = EM.GetComponent<Game::Components::SiegeTransitionComponent>(Id)) {
                Rec.HasSiegeTransition = true;
                Rec.SiegeTransition = *Siege;
            }
            if (const auto* Mine = EM.GetComponent<Game::Components::SpiderMineComponent>(Id)) {
                Rec.HasSpiderMine = true;
                Rec.SpiderMine = *Mine;
            }
            if (const auto* Nuke = EM.GetComponent<Game::Components::NukeComponent>(Id)) {
                Rec.HasNuke = true;
                Rec.Nuke = *Nuke;
            }
            if (const auto* Warp = EM.GetComponent<Game::Components::ArchonWarpComponent>(Id)) {
                Rec.HasArchonWarp = true;
                Rec.ArchonWarp = *Warp;
            }
            Snap.Entities.push_back(std::move(Rec));
        }

        Snap.StateHashFNV1a = ComputeWorldHashFNV1a(EM, FrameNumber);
        return Snap;
    }

    static void RestoreSnapshot(EntityManager& EM, const WorldDeterministicSnapshot& Snap) {
        std::vector<EntityID> CurrentIds = EM.GetActiveEntities();
        for (EntityID Id : CurrentIds) {
            EM.DestroyEntity(Id);
        }

        if (Snap.HasUpgradesResource) {
            EM.Context().SetResource<WorldUpgradeResource>(Snap.Upgrades);
        } else {
            EM.Context().RemoveResource<WorldUpgradeResource>();
        }

        if (Snap.HasEconomyResource) {
            EM.Context().SetResource<Game::Systems::PlayerResourceStore>(Snap.Economy);
        } else {
            EM.Context().RemoveResource<Game::Systems::PlayerResourceStore>();
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
            if (Rec.HasCommand) {
                EM.AddComponent<Game::Components::CommandComponent>(NewId) = Rec.Command;
            }
            if (Rec.HasPathfinding) {
                EM.AddComponent<Game::Components::PathfindingComponent>(NewId) = Rec.Pathfinding;
            }
            if (Rec.HasBuilding) {
                EM.AddComponent<Game::Components::BuildingComponent>(NewId) = Rec.Building;
            }
            if (Rec.HasResource) {
                EM.AddComponent<Game::Components::ResourceComponent>(NewId) = Rec.Resource;
            }
            if (Rec.HasHarvest) {
                EM.AddComponent<Game::Components::HarvestComponent>(NewId) = Rec.Harvest;
            }
            if (Rec.HasProduction) {
                EM.AddComponent<Game::Components::ProductionComponent>(NewId) = Rec.Production;
            }
            if (Rec.HasBullet) {
                EM.AddComponent<Game::Components::BulletComponent>(NewId) = Rec.Bullet;
            }
            if (Rec.HasStatusEffect) {
                EM.AddComponent<Game::Components::StatusEffectComponent>(NewId) = Rec.StatusEffect;
            }
            if (Rec.HasCloak) {
                EM.AddComponent<Game::Components::CloakComponent>(NewId) = Rec.Cloak;
            }
            if (Rec.HasBurrow) {
                EM.AddComponent<Game::Components::BurrowComponent>(NewId) = Rec.Burrow;
            }
            if (Rec.HasLoadedUnits) {
                EM.AddComponent<Game::Components::LoadedUnitsComponent>(NewId) = Rec.LoadedUnits;
            }
            if (Rec.HasMorph) {
                EM.AddComponent<Game::Components::MorphComponent>(NewId) = Rec.Morph;
            }
            if (Rec.HasSiegeTransition) {
                EM.AddComponent<Game::Components::SiegeTransitionComponent>(NewId) = Rec.SiegeTransition;
            }
            if (Rec.HasSpiderMine) {
                EM.AddComponent<Game::Components::SpiderMineComponent>(NewId) = Rec.SpiderMine;
            }
            if (Rec.HasNuke) {
                EM.AddComponent<Game::Components::NukeComponent>(NewId) = Rec.Nuke;
            }
            if (Rec.HasArchonWarp) {
                EM.AddComponent<Game::Components::ArchonWarpComponent>(NewId) = Rec.ArchonWarp;
            }
        }
    }
};

} // namespace Core::ECS
