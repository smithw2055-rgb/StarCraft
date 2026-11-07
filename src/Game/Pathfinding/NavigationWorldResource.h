#pragma once

#include <algorithm>

#include "Core/ECS/EntityManager.h"
#include "Game/Map/Map.h"
#include "Game/Pathfinding/NavigationService.h"
#include "Game/Pathfinding/RegionPathfinder.h"
#include "Game/SimulationContext.h"

namespace Game::Pathfinding
{

// ============================================================================
// NavigationWorldResource: World-scoped navigation & region pathfinding state
// Eliminates global static sNavigationService / sRegionPathfinder / gCmdMap
// cross-talk when multiple ECS Worlds execute concurrently or in lockstep tests.
// ============================================================================
struct NavigationWorldResource
{
    NavigationService NavService;
    RegionPathfinder RegionPath;
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
            RegionPathfinder::MapContext Ctx;
            Ctx.MapWidth = std::min(Map->GetWidth(), RegionPathfinder::kMaxMapWidth);
            Ctx.MapHeight = std::min(Map->GetHeight(), RegionPathfinder::kMaxMapHeight);
            for (int y = 0; y < Ctx.MapHeight; ++y)
            {
                for (int x = 0; x < Ctx.MapWidth; ++x)
                {
                    Ctx.Walkable[x][y] = Map->IsWalkable(x, y);
                    Ctx.TileRegion[x][y] = -1;
                }
            }
            RegionPath.BuildRegions(Ctx);
            BoundMap = Map;
            RegionsBuilt = true;
        }
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
