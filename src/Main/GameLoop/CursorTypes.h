#pragma once

#include <cstdint>
#include <vector>
#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
namespace Game::Components
{
struct TransformComponent;
struct UnitComponent;
struct CombatComponent;
}
namespace Game::Client { enum class EntityKind : uint8_t; }

namespace Game::Client::GameLoop
{

struct ClickTargetContext
{
    Core::Math::Vec2 WorldPosition;
    int ResourceEntityID = -1;
    int AnyEntityID = -1;
    int EnemyEntityID = -1;
};

enum class GameCursorKind
{
    Arrow,
    Drag,
    Illegal,
    TargetNeutral,
    TargetGreen,
    TargetRed,
    TargetYellow,
    MagnifierGreen,
    MagnifierRed,
    MagnifierYellow,
    ScrollUp,
    ScrollDown,
    ScrollLeft,
    ScrollRight,
    ScrollUpLeft,
    ScrollUpRight,
    ScrollDownLeft,
    ScrollDownRight,
    Time
};

struct CursorRenderState
{
    GameCursorKind Kind = GameCursorKind::Arrow;
    bool UseGameCursor = false;
    float HotspotX = 0.0f;
    float HotspotY = 0.0f;
};

const char* CursorName(GameCursorKind Kind);
float CursorAnimationFps(GameCursorKind Kind);
void CursorHotspot(GameCursorKind Kind, float& OutX, float& OutY);

bool IsMineralUnit(int Id);
bool IsGeyserUnit(int Id);
bool IsBuildingChkId(int Id);
bool IsOrganicBuilding(int Id);
bool RequiresPylonChkId(int Id);
bool IsStartLocationUnit(int Id);
bool IsRenderableChkUnit(int Id);

bool HasCombatSelection(Core::ECS::EntityManager& EM,
                        const std::vector<Core::ECS::EntityID>& EcsIds);
std::vector<Core::ECS::EntityID> FilterHarvesters(
    Core::ECS::EntityManager& EM,
    const std::vector<Core::ECS::EntityID>& EcsIds);

} // namespace Game::Client::GameLoop
