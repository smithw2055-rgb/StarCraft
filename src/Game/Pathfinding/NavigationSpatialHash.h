#pragma once

#include "Core/Spatial/DeterministicSpatialGrid2D.h"

namespace Game::Pathfinding
{

using NavigationSpatialHash = Core::Spatial::DeterministicSpatialGrid2D<64, 32, 256>;

} // namespace Game::Pathfinding
