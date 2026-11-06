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
    uint64_t RebuildCount = 0;
    size_t IndexedEntityCount = 0;

    void RebuildFromECS(const Core::ECS::EntityManager& EM) {
        SpatialHash.Clear();
        IndexedEntityCount = 0;
        ++RebuildCount;

        for (Core::ECS::EntityID Id : EM.GetActiveEntities()) {
            const auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(Id);
            const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Id);
            if (!Trans || (Unit && Unit->HitPoints <= 0)) continue;
            SpatialHash.Insert(Id, Trans->Position);
            ++IndexedEntityCount;
        }
    }

    void QueryRadius(
        const Core::ECS::EntityManager& EM,
        const Core::Math::Vec2& Center,
        float Radius,
        std::vector<Core::ECS::EntityID>& OutEntities,
        int FilterExcludePlayerID = -1) const
    {
        OutEntities.clear();
        std::vector<int> Candidates;
        SpatialHash.QueryInto(
            Center.X - Radius,
            Center.Y - Radius,
            Center.X + Radius,
            Center.Y + Radius,
            Candidates);

        const float RadiusSq = Radius * Radius;
        for (int CandID : Candidates) {
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
        std::vector<Core::ECS::EntityID> InRange;
        QueryRadius(EM, Center, MaxSearchRadius, InRange, MyPlayerID);

        Core::ECS::EntityID BestID = Core::ECS::InvalidEntity;
        float BestDistSq = std::numeric_limits<float>::max();

        for (Core::ECS::EntityID Id : InRange) {
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

// Phase 3 Spatial Index System: Automatically rebuilds WorldContext SpatialIndexResource
// during FixedSimulation so CombatSystem, Splash damage, and AI target acquisition run in O(1) cell lookups.
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
