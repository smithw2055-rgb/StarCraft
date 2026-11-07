#pragma once

// ============================================================================
// Engine.h: Single Umbrella Header for the General-Purpose 2D/RTS Engine Foundation
// ============================================================================

#include "Engine/AI/BehaviorTreeServer.h"
#include "Engine/DataAssetRegistry.h"
#include "Engine/Debug/EngineTelemetryServer.h"
#include "Engine/ECS/GenericWorldSnapshot.h"
#include "Engine/ECS/WorldSerializer.h"
#include "Engine/EngineApp.h"
#include "Engine/EngineTime.h"
#include "Engine/Input/CommandRouter.h"
#include "Engine/Navigation/NavigationServer2D.h"
#include "Engine/Net/DeterministicSessionServer.h"
#include "Engine/Node2D.h"
#include "Engine/Render/RenderGraph2D.h"
#include "Engine/Scene/PackedScene2D.h"
#include "Engine/Scene/PrefabRegistry.h"
