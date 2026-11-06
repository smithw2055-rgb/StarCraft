#include "UI/Hud.h"
#include "UI/ViewModel/HudViewModel.h"
#include "UI/ViewModel/ObservableProperty.h"
#include "Core/ECS/EntityManager.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

#include "Game/Systems/CombatSystem.h"

namespace Game::Systems
{
    void CombatSystem::IssueAttack(Core::ECS::EntityManager&, const std::vector<int>&, int) {}
    void CombatSystem::IssueAttackMove(Core::ECS::EntityManager&, const std::vector<int>&, Core::Math::Vec2) {}
    void CombatSystem::ApplyDamage(Core::ECS::EntityManager&, int, const Game::Components::CombatComponent&, int, int, int) {}
}

namespace
{

void TestObservablePropertySemantics()
{
    using namespace Game::Client::UI::ViewModel;

    ObservableProperty<int> Prop(100);
    assert(Prop.Get() == 100);
    assert(Prop.GetVersion() == 1u);
    assert(Prop.ConsumeDirty());
    assert(!Prop.IsDirty());

    int CallbackCount = 0;
    int LastOld = 0;
    int LastNew = 0;

    auto SubId = Prop.Subscribe([&](const int& OldV, const int& NewV)
    {
        ++CallbackCount;
        LastOld = OldV;
        LastNew = NewV;
    });

    // Assigning equal value must not fire callback or mark dirty
    assert(!Prop.Set(100));
    assert(CallbackCount == 0);
    assert(!Prop.IsDirty());
    assert(Prop.GetVersion() == 1u);

    // Assigning new value must fire callback and increment version
    assert(Prop.Set(250));
    assert(CallbackCount == 1);
    assert(LastOld == 100 && LastNew == 250);
    assert(Prop.IsDirty());
    assert(Prop.GetVersion() == 2u);
    assert(Prop.ConsumeDirty());

    Prop.Unsubscribe(SubId);
    assert(Prop.Set(500));
    assert(CallbackCount == 1);
}

void TestHudViewModelEventBusReactivity()
{
    using namespace Game::Client::UI::ViewModel;

    Core::ECS::EntityManager World;
    HudViewModel VM;
    VM.BindToWorld(World, 0);
    VM.ClearDirtyFlags();

    int MineralNotifications = 0;
    VM.Minerals.Subscribe([&](const int&, const int&) { ++MineralNotifications; });

    // 1. Emit ResourceChangedEvent for player 0 via World.Events()
    World.Events().Publish(Game::Events::ResourceChangedEvent{0, 375, 175, 425, 175});
    assert(VM.Minerals.Get() == 425);
    assert(VM.Gas.Get() == 175);
    assert(VM.FormattedMineralText.Get() == "425");
    assert(VM.FormattedGasText.Get() == "175");
    assert(MineralNotifications == 1);
    assert(HasDirtyFlag(VM.GetDirtyFlags(), HudDirtyFlags::Resources));
    assert(HasDirtyFlag(VM.GetDirtyFlags(), HudDirtyFlags::CommandCard));
    assert(VM.GetStats().EventBusSignalsHandled == 1u);

    // Event for another player (Player 1) must be ignored by Player 0's ViewModel
    VM.ClearDirtyFlags();
    World.Events().Publish(Game::Events::ResourceChangedEvent{1, 50, 50, 999, 999});
    assert(VM.Minerals.Get() == 425);
    assert(VM.GetDirtyFlags() == HudDirtyFlags::None);

    // 2. Emit ResearchCompletedEvent for player 0
    World.Events().Publish(Game::Events::ResearchCompletedEvent{0, 1, 1, 1});
    assert(HasDirtyFlag(VM.GetDirtyFlags(), HudDirtyFlags::CommandCard));
    assert(HasDirtyFlag(VM.GetDirtyFlags(), HudDirtyFlags::InfoPanel));
    assert(VM.GetStats().EventBusSignalsHandled == 2u);

    // 3. Emit UnitDeathEvent for a selected unit
    VM.SelectedEntityIds.Set({10, 11});
    VM.ClearDirtyFlags();
    World.Events().Publish(Game::Events::UnitDeathEvent{11, 0, 7, -1, -1, false});
    assert(HasDirtyFlag(VM.GetDirtyFlags(), HudDirtyFlags::Selection));
    assert(HasDirtyFlag(VM.GetDirtyFlags(), HudDirtyFlags::CommandCard));
    assert(HasDirtyFlag(VM.GetDirtyFlags(), HudDirtyFlags::InfoPanel));
    assert(VM.GetStats().EventBusSignalsHandled == 3u);
}

void TestHudUpdateContextReactiveSkippingAndSelectiveRebuild()
{
    using namespace Game::Client::UI;

    Core::ECS::EntityManager EM;
    EM.ActivateWorld();

    // Create a Terran SCV (ChkUnitID 7)
    const int Scv = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Scv).Position = {256.0f, 256.0f};
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(Scv);
    Unit.ChkUnitID = 7; // Terran SCV
    Unit.UnitRace = Game::Components::Race::Terran;
    Unit.HitPoints = 60;
    Unit.MaxHitPoints = 60;
    EM.AddComponent<Game::Components::PlayerComponent>(Scv).PlayerID = 0;
    EM.AddComponent<Game::Components::CommandComponent>(Scv);
    EM.AddComponent<Game::Components::HarvestComponent>(Scv);

    Hud TestHud;
    std::vector<int> Selection = {Scv};

    // Frame 1: Initial selection must build both CommandCard and InfoPanel
    TestHud.UpdateContext(EM, Selection);
    const auto& Stats1 = TestHud.GetViewModel().GetStats();
    assert(Stats1.TotalSyncCalls == 1u);
    assert(Stats1.CommandCardRebuilds == 1u);
    assert(Stats1.InfoPanelRebuilds == 1u);
    assert(Stats1.CommandCardSkips == 0u);
    assert(Stats1.InfoPanelSkips == 0u);

    // Verify SCV buttons were populated (e.g. Build Basic Structure at slot 6)
    assert(!TestHud.GetButtonDefs()[0].Label.empty());
    assert(!TestHud.GetButtonDefs()[6].Label.empty());

    // Frames 2..60: Steady state (nothing changed) -> 59 consecutive skips!
    for (int Frame = 2; Frame <= 60; ++Frame)
    {
        TestHud.UpdateContext(EM, Selection);
    }
    const auto& Stats60 = TestHud.GetViewModel().GetStats();
    assert(Stats60.TotalSyncCalls == 60u);
    assert(Stats60.CommandCardRebuilds == 1u);
    assert(Stats60.InfoPanelRebuilds == 1u);
    assert(Stats60.CommandCardSkips == 59u);
    assert(Stats60.InfoPanelSkips == 59u);

    // Frame 61: SCV takes 15 damage (HitPoints 60 -> 45).
    // Only InfoPanel should rebuild; CommandCard must be skipped!
    Unit.HitPoints = 45;
    TestHud.UpdateContext(EM, Selection);
    const auto& Stats61 = TestHud.GetViewModel().GetStats();
    assert(Stats61.InfoPanelRebuilds == 2u);
    assert(Stats61.CommandCardRebuilds == 1u);
    assert(Stats61.CommandCardSkips == 60u);

    // Frame 62: Player clicks "Build Basic Structure" (slot 6) -> opens submenu page
    // CommandCard must rebuild, InfoPanel can be skipped!
    TestHud.OnButtonInvoked(6);
    TestHud.UpdateContext(EM, Selection);
    const auto& Stats62 = TestHud.GetViewModel().GetStats();
    assert(Stats62.CommandCardRebuilds == 2u);
    assert(Stats62.InfoPanelRebuilds == 2u);
    assert(Stats62.InfoPanelSkips == 60u);

    // Frame 63: HarvestSystem adds minerals -> emits ResourceChangedEvent on World.Events()
    // CommandCard must rebuild to refresh button affordability!
    EM.Events().Publish(Game::Events::ResourceChangedEvent{0, 500, 0, 550, 0});
    TestHud.UpdateContext(EM, Selection);
    const auto& Stats63 = TestHud.GetViewModel().GetStats();
    assert(Stats63.EventBusSignalsHandled >= 1u);
    assert(Stats63.CommandCardRebuilds == 3u);
    assert(TestHud.GetViewModel().Minerals.Get() == 550);
    assert(TestHud.GetViewModel().FormattedMineralText.Get() == "550");
}

} // namespace

int main()
{
    TestObservablePropertySemantics();
    TestHudViewModelEventBusReactivity();
    TestHudUpdateContextReactiveSkippingAndSelectiveRebuild();

    std::cout << "[Phase4HudMvvmBindingTest] All ObservableProperty, HudViewModel EventBus & selective HUD rebuild tests passed.\n";
    return 0;
}
