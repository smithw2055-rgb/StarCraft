#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/Pathfinding/NavigationSpatialHash.h"

namespace Game::Pathfinding {

struct SpatialIndexResource {
    NavigationSpatialHash SpatialHash;
    NavigationSpatialHash AllUnitsHash;
    NavigationSpatialHash BuildingsHash;
    NavigationSpatialHash ResourcesHash;
    uint64_t RebuildCount = 0;
    uint64_t SharedQueryReuses = 0;
    uint64_t LastBuiltMaskVersion = 0;
    size_t IndexedEntityCount = 0;
    bool PositionsDirty = true;

    void MarkPositionsDirty() noexcept {
        PositionsDirty = true;
    }

    void RebuildFromECS(const Core::ECS::EntityManager& EM) {
        SpatialHash.Clear();
        AllUnitsHash.Clear();
        BuildingsHash.Clear();
        ResourcesHash.Clear();
        IndexedEntityCount = 0;
        ++RebuildCount;
        LastBuiltMaskVersion = EM.GetMaskMutationVersion();
        PositionsDirty = false;

        EM.ForEachComponent<Game::Components::TransformComponent>(
            [&](Core::ECS::EntityID Id, const Game::Components::TransformComponent& Trans) {
                if (EM.HasComponent<Game::Components::ResourceComponent>(Id)) {
                    ResourcesHash.Insert(Id, Trans.Position);
                }
                if (EM.HasComponent<Game::Components::BuildingComponent>(Id)) {
                    BuildingsHash.Insert(Id, Trans.Position);
                }
                const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Id);
                if (Unit) {
                    AllUnitsHash.Insert(Id, Trans.Position);
                    if (Unit->HitPoints <= 0) return;
                }
                SpatialHash.Insert(Id, Trans.Position);
                ++IndexedEntityCount;
            });

        // Finalize CSR contiguous cell slices so all subsequent QueryInto calls copy cache lines directly
        SpatialHash.FinalizeCSR();
        AllUnitsHash.FinalizeCSR();
        BuildingsHash.FinalizeCSR();
        ResourcesHash.FinalizeCSR();
    }

    // Acquires an up-to-date CSR-finalized SpatialIndexResource for EM, rebuilding only if
    // entity masks mutated, positions were marked dirty, or the index was never built.
    static SpatialIndexResource& AcquireUpToDate(Core::ECS::EntityManager& EM) {
        auto& Res = EM.Context().GetOrCreateResource<SpatialIndexResource>();
        if (Res.RebuildCount == 0 ||
            Res.PositionsDirty ||
            Res.LastBuiltMaskVersion != EM.GetMaskMutationVersion())
        {
            Res.RebuildFromECS(EM);
        } else {
            ++Res.SharedQueryReuses;
        }
        return Res;
    }

    // Rebuilds and returns the CSR-finalized spatial hash of all (TransformComponent, UnitComponent) entities
    const NavigationSpatialHash& RebuildUnitsHash(const Core::ECS::EntityManager& EM) {
        AllUnitsHash.Clear();
        EM.ForEachComponent<Game::Components::TransformComponent, Game::Components::UnitComponent>(
            [&](Core::ECS::EntityID Id,
                const Game::Components::TransformComponent& Trans,
                const Game::Components::UnitComponent&) {
                AllUnitsHash.Insert(Id, Trans.Position);
            });
        AllUnitsHash.FinalizeCSR();
        return AllUnitsHash;
    }

    void QueryRadius(
        const Core::ECS::EntityManager& EM,
        const Core::Math::Vec2& Center,
        float Radius,
        std::vector<Core::ECS::EntityID>& OutEntities,
        int FilterExcludePlayerID = -1) const
    {
        OutEntities.clear();
        static thread_local std::vector<int> sCandidates;
        SpatialHash.QueryInto(
            Center.X - Radius,
            Center.Y - Radius,
            Center.X + Radius,
            Center.Y + Radius,
            sCandidates);

        const float RadiusSq = Radius * Radius;
        for (int CandID : sCandidates) {
            if (!EM.IsAlive(CandID)) continue;
            if (FilterExcludePlayerID >= 0) {
                const auto* P = EM.GetComponent<Game::Components::PlayerComponent>(CandID);
                if (P && P->PlayerID == FilterExcludePlayerID) continue;
            }
            const auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(CandID);
            if (!Trans) continue;
            const float Dx = Trans->Position.X - Center.X;
            const float Dy = Trans->Position.Y - Center.Y;
            if (Dx * Dx + Dy * Dy <= RadiusSq) {
                OutEntities.push_back(CandID);
            }
        }
        std::sort(OutEntities.begin(), OutEntities.end());
        OutEntities.erase(std::unique(OutEntities.begin(), OutEntities.end()), OutEntities.end());
    }

    Core::ECS::EntityID FindNearestEnemy(
        const Core::ECS::EntityManager& EM,
        const Core::Math::Vec2& Center,
        float MaxSearchRadius,
        int MyPlayerID) const
    {
        static thread_local std::vector<Core::ECS::EntityID> sInRange;
        QueryRadius(EM, Center, MaxSearchRadius, sInRange, MyPlayerID);

        Core::ECS::EntityID BestID = Core::ECS::InvalidEntity;
        float BestDistSq = std::numeric_limits<float>::max();

        for (Core::ECS::EntityID Id : sInRange) {
            const auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(Id);
            if (!Trans) continue;
            const float DistSq = (Trans->Position - Center).LengthSq();
            if (DistSq < BestDistSq || (DistSq == BestDistSq && Id < BestID)) {
                BestDistSq = DistSq;
                BestID = Id;
            }
        }
        return BestID;
    }
};

// Phase 3 & Phase 15 Spatial Index System: Automatically rebuilds WorldContext SpatialIndexResource
// during FixedSimulation so CombatSystem, BulletSystem, ProtossSystem, NukeAndSpiderMineSystem,
// and AI target acquisition share O(1) CSR cell lookups without redundant per-system rebuilds.
class WorldSpatialIndexSystem final : public Core::ECS::ISystem {
public:
    explicit WorldSpatialIndexSystem(Core::ECS::SystemManager& SysMgr)
        : mSystemMgr(SysMgr) {}

    std::string_view GetName() const override { return "WorldSpatialIndexSystem"; }
    Core::ECS::SystemPhase GetPhase() const override { return Core::ECS::SystemPhase::FixedSimulation; }

    void Update(float DeltaTime) override {
        (void)DeltaTime;
        auto& EM = mSystemMgr.GetEntityManager();
        auto& SpatialRes = EM.Context().GetOrCreateResource<SpatialIndexResource>();
        SpatialRes.RebuildFromECS(EM);
    }

private:
    Core::ECS::SystemManager& mSystemMgr;
};

} // namespace Game::Pathfinding
