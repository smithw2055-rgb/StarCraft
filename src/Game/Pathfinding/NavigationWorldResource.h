#pragma once

#include <algorithm>
#include <vector>

#include "Core/ECS/EntityManager.h"
#include "Engine/Navigation/NavigationServer2D.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/NavigationService.h"
#include "Game/SimulationContext.h"

namespace Game::Pathfinding
{

// ============================================================================
// NavigationWorldResource (Phase 52 & Phase 62 Unified Navigation Resource):
// - Converges NavigationService & Engine::Navigation::NavigationServer2D
//   (replacing the legacy stack-heavy 417-line RegionPathfinder).
// - Provides unified FindPathViaEngineServer and StepNavigationAgent helpers.
// ============================================================================
struct NavigationWorldResource
{
    NavigationService NavService;
    Engine::Navigation::NavigationServer2D NavServer;
    const Game::Map::GameMap* BoundMap = nullptr;
    uint32_t BoundGridVersion = 0;
    bool RegionsBuilt = false;

    void SyncWithMap(const Game::Map::GameMap* Map)
    {
        if (!Map)
            return;

        const uint32_t CurrentGridVer = Map->GetNavigationGrid().GetVersion();
        const bool MapChanged = (BoundMap != Map);

        if (MapChanged || BoundGridVersion != CurrentGridVer)
        {
            NavService.SetBaseGrid(Map->GetNavigationGrid());
            BoundGridVersion = CurrentGridVer;
        }

        if (MapChanged || !RegionsBuilt)
        {
            const int W = std::max(1, Map->GetWidth());
            const int H = std::max(1, Map->GetHeight());
            NavServer.InitializeGrid(W, H, 32.0f);
            for (int y = 0; y < H; ++y)
            {
                for (int x = 0; x < W; ++x)
                {
                    NavServer.SetCellBlocked(x, y, !Map->IsWalkable(x, y));
                    NavServer.SetCellElevation(
                        x, y, static_cast<uint8_t>(Map->GetGroundHeight(x, y)), false);
                }
            }
            NavServer.RebuildConnectedRegions(1u);
            BoundMap = Map;
            RegionsBuilt = true;
        }
    }

    std::vector<Engine::Navigation::GridCoord2D> FindPathViaEngineServer(
        Engine::Navigation::GridCoord2D StartTile,
        Engine::Navigation::GridCoord2D GoalTile,
        uint8_t FootprintClearance = 1)
    {
        if (RegionsBuilt && FootprintClearance == 1 &&
            !NavServer.AreCellsConnected(StartTile, GoalTile))
        {
            return {};
        }
        return NavServer.FindPath(StartTile, GoalTile, FootprintClearance);
    }

    Core::Math::Vec2 StepNavigationAgent(
        Engine::Navigation::NavigationAgent2D& Agent,
        Core::Math::Vec2 CurrentWorldPos,
        float MaxSpeedPxPerSec,
        float DeltaSeconds,
        const std::vector<Engine::Navigation::AvoidanceAgent2D>& Neighbors = {})
    {
        return NavServer.StepNavigationAgent(
            Agent, CurrentWorldPos, MaxSpeedPxPerSec, DeltaSeconds, Neighbors);
    }

    static const Game::Map::GameMap* ResolveMap(
        const Core::ECS::EntityManager& EM,
        const Game::Map::GameMap* FallbackMap = nullptr)
    {
        if (const auto* SimCtx = Game::SimulationContext::TryGet(EM))
        {
            if (SimCtx->Map)
                return SimCtx->Map;
            if (SimCtx->MutableMap)
                return SimCtx->MutableMap;
        }
        if (const auto* NavRes = EM.GetResource<NavigationWorldResource>())
        {
            if (NavRes->BoundMap)
                return NavRes->BoundMap;
        }
        return FallbackMap;
    }

    static NavigationWorldResource& GetOrSync(
        Core::ECS::EntityManager& EM,
        const Game::Map::GameMap* ExplicitMap = nullptr)
    {
        auto& Res = EM.GetOrCreateResource<NavigationWorldResource>();
        const Game::Map::GameMap* EffectiveMap =
            ExplicitMap ? ExplicitMap : ResolveMap(EM, Res.BoundMap);
        if (EffectiveMap)
            Res.SyncWithMap(EffectiveMap);
        return Res;
    }
};

} // namespace Game::Pathfinding
