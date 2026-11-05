#include "Client/Selection.h"
#include "Game/PlayerColor.h"

#include "Core/ECS/EntityManager.h"
#include "Resource/GameDataLoader.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Resource::Graphics
{

Resource::GameDataLoader* DataAssetCacheView::GetDataLoader() const
{
    return nullptr;
}

} // namespace Resource::Graphics

namespace Resource
{

const UnitEntry* GameDataLoader::FindUnitByID(uint32_t) const
{
    return nullptr;
}

const SpriteEntry* GameDataLoader::FindSpriteByID(uint32_t) const
{
    return nullptr;
}

uint32_t GameDataLoader::ResolveUnitSpriteID(uint32_t) const
{
    return UINT32_MAX;
}

} // namespace Resource

namespace Core::Platform
{

bool IsKeyDown(int) { return false; }
bool IsMouseButtonDown(int) { return false; }
bool IsMouseButtonPressed(int) { return false; }
bool IsMouseButtonReleased(int) { return false; }
float GetMouseX() { return -1.0f; }
float GetMouseY() { return -1.0f; }
bool IsWindowFocused() { return true; }
bool IsMouseInClientArea() { return true; }
bool IsMouseInGameContentArea() { return true; }
double GetAbsoluteTime() { return 0.0; }

} // namespace Core::Platform

namespace
{
    void Require(bool Value, const char* Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message);
            std::exit(1);
        }
    }

    int CreateSelectable(Core::ECS::EntityManager& EM,
                         int ChkUnitID,
                         Core::Math::Vec2 Position,
                         int PlayerID,
                         Game::Components::UnitSize Size = Game::Components::UnitSize::Medium)
    {
        const int EntityID = EM.CreateEntity();
        EM.AddComponent<Game::Components::TransformComponent>(EntityID).Position = Position;
        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = ChkUnitID;
        Unit.HitPoints = 100;
        Unit.MaxHitPoints = 100;
        Unit.SizeType = Size;
        EM.AddComponent<Game::Components::PlayerComponent>(EntityID).PlayerID = PlayerID;
        auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        Render.Visible = true;
        return EntityID;
    }

    void TestPointPickingUsesInteractionFootprints()
    {
        Core::ECS::EntityManager EM;
        Game::Client::Selection Selection;

        const int CommandCenter = CreateSelectable(
            EM,
            106,
            {256.0f, 256.0f},
            0,
            Game::Components::UnitSize::Large);
        EM.AddComponent<Game::Components::BuildingComponent>(CommandCenter).IsBuilding = true;

        const int Mineral = CreateSelectable(
            EM,
            176,
            {384.0f, 256.0f},
            11,
            Game::Components::UnitSize::Large);
        EM.AddComponent<Game::Components::ResourceComponent>(Mineral).Type =
            Game::Components::ResourceType::Minerals;

        Require(Selection.PickEntityAtWorld(EM, 316.0f, 256.0f) == CommandCenter,
            "building pick uses its large gameplay selection footprint");
        Require(Selection.PickEntityAtWorld(EM, 426.0f, 256.0f) == Mineral,
            "resource pick uses an expanded mineral click footprint");

        auto* MineralRender = EM.GetComponent<Game::Components::RenderComponent>(Mineral);
        Require(MineralRender != nullptr, "mineral has render component");
        MineralRender->Visible = false;
        Require(Selection.PickEntityAtWorld(EM, 426.0f, 256.0f) == -1,
            "selection ignores invisible resource entities");
    }

    void TestPointPickingIgnoresDeadVisibleUnits()
    {
        Core::ECS::EntityManager EM;
        Game::Client::Selection Selection;

        const int DeadMarine = CreateSelectable(EM, 0, {64.0f, 64.0f}, 0);
        auto* DeadUnit = EM.GetComponent<Game::Components::UnitComponent>(DeadMarine);
        Require(DeadUnit != nullptr, "dead point-pick coverage has unit data");
        DeadUnit->HitPoints = 0;

        Require(Selection.PickEntityAtWorld(EM, 64.0f, 64.0f) == -1,
            "point selection ignores visible entities with no hit points");
    }

    void TestOverlappingPointPickPrioritizesAirGroundBuilding()
    {
        Core::ECS::EntityManager EM;
        Game::Client::Selection Selection;
        const Core::Math::Vec2 SharedPosition{192.0f, 192.0f};
        const int Building = CreateSelectable(
            EM, 106, SharedPosition, 0, Game::Components::UnitSize::Large);
        EM.AddComponent<Game::Components::BuildingComponent>(Building).IsBuilding = true;
        const int Ground = CreateSelectable(EM, 0, SharedPosition, 0);
        const int Air = CreateSelectable(EM, 43, SharedPosition, 0);
        EM.AddComponent<Game::Components::PathfindingComponent>(Air).IsAir = true;

        Require(Selection.PickEntityAtWorld(EM, 192.0f, 192.0f) == Air,
            "overlapping point pick prioritizes an air unit");
        EM.GetComponent<Game::Components::RenderComponent>(Air)->Visible = false;
        Require(Selection.PickEntityAtWorld(EM, 192.0f, 192.0f) == Ground,
            "overlapping point pick falls back to a ground unit");
        EM.GetComponent<Game::Components::RenderComponent>(Ground)->Visible = false;
        Require(Selection.PickEntityAtWorld(EM, 192.0f, 192.0f) == Building,
            "overlapping point pick selects a building last");
    }

    void TestLurkerParticipatesInUnitSelection()
    {
        Core::ECS::EntityManager EM;
        Game::Client::Selection Selection;
        const int Lurker = CreateSelectable(EM, 103, {96.0f, 96.0f}, 0);
        const int Hydralisk = CreateSelectable(EM, 38, {128.0f, 96.0f}, 0);
        const auto Picked =
            Selection.CollectRectSelection(EM, 64.0f, 64.0f, 96.0f, 64.0f);
        Require(Picked.size() == 2 &&
                std::find(Picked.begin(), Picked.end(), Lurker) != Picked.end() &&
                std::find(Picked.begin(), Picked.end(), Hydralisk) != Picked.end(),
            "surface Lurker joins a movable multi-unit selection");
    }

    void TestRectSelectionPrefersLocalUnits()
    {
        Core::ECS::EntityManager EM;
        Game::Client::Selection Selection;

        const int LocalMarine = CreateSelectable(EM, 0, {64.0f, 64.0f}, 0);
        const int LocalScv = CreateSelectable(EM, 7, {96.0f, 64.0f}, 0);
        CreateSelectable(EM, 37, {128.0f, 64.0f}, 1);
        const int EnemyCommandCenter = CreateSelectable(
            EM, 106, {160.0f, 128.0f}, 1, Game::Components::UnitSize::Large);
        EM.AddComponent<Game::Components::BuildingComponent>(EnemyCommandCenter).IsBuilding = true;

        auto Picked = Selection.CollectRectSelection(EM, 32.0f, 32.0f, 128.0f, 64.0f);
        Require(Picked.size() == 2,
            "box selection prefers local multi-selectable units over enemies");
        Require(Picked[0] == LocalMarine && Picked[1] == LocalScv,
            "box selection preserves local unit order");

        EM.GetComponent<Game::Components::RenderComponent>(LocalMarine)->Visible = false;
        EM.GetComponent<Game::Components::RenderComponent>(LocalScv)->Visible = false;
        Picked = Selection.CollectRectSelection(EM, 32.0f, 32.0f, 128.0f, 64.0f);
        Require(Picked.empty(),
            "box selection never falls back to enemy units when no local unit is inside");
        Require(Selection.PickEntityAtWorld(EM, 128.0f, 64.0f) == -1,
            "point selection never selects an enemy unit");
        Require(Selection.PickEntityAtWorld(EM, 160.0f, 128.0f) == -1,
            "point selection never selects an enemy building");
    }

    void TestRectSelectionIgnoresDeadVisibleUnits()
    {
        Core::ECS::EntityManager EM;
        Game::Client::Selection Selection;

        const int DeadMarine = CreateSelectable(EM, 0, {64.0f, 64.0f}, 0);
        const int LiveMarine = CreateSelectable(EM, 0, {96.0f, 64.0f}, 0);
        auto* DeadUnit = EM.GetComponent<Game::Components::UnitComponent>(DeadMarine);
        Require(DeadUnit != nullptr, "dead rect-select coverage has unit data");
        DeadUnit->HitPoints = 0;

        auto Picked = Selection.CollectRectSelection(EM, 32.0f, 32.0f, 128.0f, 64.0f);
        Require(Picked.size() == 1 && Picked[0] == LiveMarine,
            "box selection ignores visible entities with no hit points");
    }

    void TestRectSelectionReturnsSingleSelectBuildings()
    {
        Core::ECS::EntityManager EM;
        Game::Client::Selection Selection;

        const int Depot = CreateSelectable(
            EM,
            106,
            {128.0f, 128.0f},
            0,
            Game::Components::UnitSize::Large);
        EM.AddComponent<Game::Components::BuildingComponent>(Depot).IsBuilding = true;

        auto Picked = Selection.CollectRectSelection(EM, 64.0f, 64.0f, 160.0f, 160.0f);
        Require(Picked.size() == 1 && Picked[0] == Depot,
            "box selection returns a single building when no selectable units are present");
    }

    void TestSpiderMinesCannotBeSelected()
    {
        Core::ECS::EntityManager EM;
        Game::Client::Selection Selection;
        const int Mine = CreateSelectable(EM, 13, {96.0f, 96.0f}, 0,
                                          Game::Components::UnitSize::Small);
        EM.AddComponent<Game::Components::SpiderMineComponent>(Mine).IsPlaced = true;

        Require(Selection.PickEntityAtWorld(EM, 96.0f, 96.0f) == -1,
            "placed Spider Mine cannot be selected by clicking");
        Require(Selection.CollectRectSelection(EM, 64.0f, 64.0f, 64.0f, 64.0f).empty(),
            "placed Spider Mine cannot be selected by dragging");
    }

    void TestPlayerColorsDifferentiateEnemyAndFriendly()
    {
        Require(Game::PlayerColorForID(0) == Core::Colors::Blue(),
            "local player slot uses the friendly blue team colour");
        Require(Game::PlayerColorForID(1) == Core::Colors::Red(),
            "enemy player slot uses a distinct red team colour");
        Require(Game::PlayerColorForID(11) == Core::Colors::Gray(),
            "neutral player slot uses a distinct neutral colour");
    }
}

int main()
{
    TestPointPickingUsesInteractionFootprints();
    TestPointPickingIgnoresDeadVisibleUnits();
    TestOverlappingPointPickPrioritizesAirGroundBuilding();
    TestLurkerParticipatesInUnitSelection();
    TestRectSelectionPrefersLocalUnits();
    TestRectSelectionIgnoresDeadVisibleUnits();
    TestRectSelectionReturnsSingleSelectBuildings();
    TestSpiderMinesCannotBeSelected();
    TestPlayerColorsDifferentiateEnemyAndFriendly();

    std::printf("SelectionInteractionTest passed\n");
    return 0;
}
