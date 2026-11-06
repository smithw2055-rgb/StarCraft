#include "Core/ECS/EntityManager.h"
#include "Core/ECS/SystemManager.h"
#include "Game/Components.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{

void Require(bool Condition, const char* Message)
{
    if (Condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", Message);
    std::exit(1);
}

struct PositionTag
{
    int X = 0;
    int Y = 0;
};

struct RareTag
{
    int Marker = 0;
};

void TestPagedSparseSetAndPointerStability()
{
    Core::ECS::EntityManager EM;

    const int E1 = EM.CreateEntity();
    const int E2 = EM.CreateEntity();
    auto& P1 = EM.AddComponent<PositionTag>(E1);
    auto& P2 = EM.AddComponent<PositionTag>(E2);
    P1 = {10, 20};
    P2 = {30, 40};

    // Within the same 256-slot page, E1 (id=1) and E2 (id=2) must be contiguous in memory.
    Require(&P1 + 1 == &P2, "components in the same page are contiguous in memory");

    const PositionTag* P1PtrBefore = &P1;
    // Allocate 1,500 more entities across multiple pages; P1PtrBefore must remain valid!
    for (int i = 0; i < 1500; ++i)
    {
        const int Id = EM.CreateEntity();
        EM.AddComponent<PositionTag>(Id) = {i, i * 2};
        if (i % 500 == 0)
        {
            EM.AddComponent<RareTag>(Id) = {i + 1};
        }
    }

    Require(EM.GetComponent<PositionTag>(E1) == P1PtrBefore,
            "paged sparse set preserves component pointer stability across page allocations");
    Require(P1PtrBefore->X == 10 && P1PtrBefore->Y == 20,
            "existing component data remains intact after page growth");

    // Const-correct access without const_cast
    const Core::ECS::EntityManager& ConstEM = EM;
    Require(ConstEM.HasComponent<PositionTag>(E1), "const HasComponent returns true");
    Require(ConstEM.GetComponent<PositionTag>(E2)->X == 30, "const GetComponent reads value");
    Require(ConstEM.GetComponentCount<RareTag>() == 3, "RareTag dense count is 3");

    // Pivot view iteration over [PositionTag, RareTag] scans only the 3 RareTag entities
    int VisitedRare = 0;
    EM.ForEachComponent<PositionTag, RareTag>(
        [&](Core::ECS::EntityID Id, PositionTag& Pos, RareTag& Rare)
        {
            Require(EM.IsAlive(Id), "visited entity is alive");
            Require(Rare.Marker == Pos.X + 1, "RareTag matches PositionTag on same entity");
            ++VisitedRare;
        });
    Require(VisitedRare == 3, "ForEachComponent visits exact intersection using smallest pool pivot");
}

void TestGenerationalEntityHandles()
{
    Core::ECS::EntityManager EM;
    const Core::ECS::EntityHandle FirstHandle = EM.CreateEntityHandle();
    Require(FirstHandle.IsValid(), "created handle is valid");
    Require(EM.IsAlive(FirstHandle), "created handle is alive");
    Require(FirstHandle.Generation == 1, "initial generation is 1");

    // Allocate 1,025 entities first, then destroy FirstHandle and the 1,025 entities
    // so mFreeIds exceeds FreeIdReuseThreshold (1024) with FirstHandle.Id at the front.
    std::vector<int> BatchIds;
    BatchIds.reserve(1025);
    for (int i = 0; i < 1025; ++i)
        BatchIds.push_back(EM.CreateEntity());

    EM.DestroyEntity(FirstHandle.Id);
    Require(!EM.IsAlive(FirstHandle.Id), "destroyed EntityID is not alive");
    Require(!EM.IsAlive(FirstHandle), "destroyed EntityHandle is not alive");

    for (int Id : BatchIds)
        EM.DestroyEntity(Id);

    const Core::ECS::EntityHandle RecycledHandle = EM.CreateEntityHandle();
    Require(RecycledHandle.Id == FirstHandle.Id, "entity ID slot was recycled after threshold");
    Require(RecycledHandle.Generation == FirstHandle.Generation + 1,
            "recycled slot incremented generation counter");
    Require(!EM.IsAlive(FirstHandle), "stale handle from previous generation is rejected");
    Require(EM.IsAlive(RecycledHandle), "new handle on recycled slot is alive");
}

void TestMultiWorldResourceIsolation()
{
    Core::ECS::SystemManager WorldA;
    Core::ECS::SystemManager WorldB;

    auto& EMA = WorldA.GetEntityManager();
    auto& EMB = WorldB.GetEntityManager();

    Game::Systems::HarvestSystem::ResetResources(EMA);
    Game::Systems::HarvestSystem::ResetResources(EMB);
    Game::Systems::UpgradeSystem::ResetUpgrades(EMA);
    Game::Systems::UpgradeSystem::ResetUpgrades(EMB);

    Game::Systems::HarvestSystem::GetResources(EMA, 0).Minerals = 1500;
    Game::Systems::HarvestSystem::GetResources(EMB, 0).Minerals = 250;

    Game::Systems::UpgradeSystem::GetUpgrades(EMA, 0)
        .Levels[(size_t)Game::Systems::ResearchId::Terran_InfantryWeapons] = 3;
    Game::Systems::UpgradeSystem::GetUpgrades(EMB, 0)
        .Levels[(size_t)Game::Systems::ResearchId::Terran_InfantryWeapons] = 1;

    Require(Game::Systems::HarvestSystem::GetResources(EMA, 0).Minerals == 1500,
            "WorldA retains its own minerals");
    Require(Game::Systems::HarvestSystem::GetResources(EMB, 0).Minerals == 250,
            "WorldB retains its own minerals without static cross-talk");
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                EMA, 0, Game::Systems::ResearchId::Terran_InfantryWeapons) == 3,
            "WorldA upgrade level is 3");
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                EMB, 0, Game::Systems::ResearchId::Terran_InfantryWeapons) == 1,
            "WorldB upgrade level is 1");

    // Verify ActiveWorld context switching for legacy static callers
    EMA.ActivateWorld();
    Require(Game::Systems::HarvestSystem::GetResources(0).Minerals == 1500,
            "legacy GetResources resolves to active WorldA");
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                0, Game::Systems::ResearchId::Terran_InfantryWeapons) == 3,
            "legacy GetResearchLevel resolves to active WorldA");

    EMB.ActivateWorld();
    Require(Game::Systems::HarvestSystem::GetResources(0).Minerals == 250,
            "legacy GetResources resolves to active WorldB");
    Require(Game::Systems::UpgradeSystem::GetResearchLevel(
                0, Game::Systems::ResearchId::Terran_InfantryWeapons) == 1,
            "legacy GetResearchLevel resolves to active WorldB");
}

void TestEventBusAndDeferredCommandBuffer()
{
    Core::ECS::SystemManager SysMgr;
    auto& EM = SysMgr.GetEntityManager();
    Game::Systems::HarvestSystem::ResetResources(EM);
    Game::Systems::UpgradeSystem::ResetUpgrades(EM);

    int ImmediateDeathNotifications = 0;
    int LastKilledUnitID = -1;
    auto SubId = EM.Events().Subscribe<Game::Events::UnitDeathEvent>(
        [&](const Game::Events::UnitDeathEvent& Ev)
        {
            ++ImmediateDeathNotifications;
            LastKilledUnitID = Ev.VictimEntityID;
        });

    const int Marine = EM.CreateEntity();
    EM.AddComponent<Game::Components::TransformComponent>(Marine).Position = {100.0f, 100.0f};
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(Marine);
    Unit.ChkUnitID = 0;
    Unit.HitPoints = 40;
    Unit.SupplyCost = 1;
    EM.AddComponent<Game::Components::PlayerComponent>(Marine).PlayerID = 0;

    auto& Res = Game::Systems::HarvestSystem::GetResources(EM, 0);
    Res.SupplyUsed = 5;

    // Defer component addition via EntityCommandBuffer
    EM.Commands().AddComponent<PositionTag>(Marine, PositionTag{7, 9});
    Require(!EM.HasComponent<PositionTag>(Marine),
            "deferred AddComponent does not mutate entity before FlushCommands");

    EM.FlushCommands();
    Require(EM.HasComponent<PositionTag>(Marine) &&
                EM.GetComponent<PositionTag>(Marine)->X == 7,
            "FlushCommands applies deferred AddComponent");

    Game::Systems::RecordUnitDeath(EM, Marine, -1, false);
    Require(ImmediateDeathNotifications == 1 && LastKilledUnitID == Marine,
            "RecordUnitDeath publishes immediate UnitDeathEvent to subscribers");
    Require(EM.Events().Read<Game::Events::UnitDeathEvent>().size() == 1,
            "RecordUnitDeath enqueues UnitDeathEvent into frame event buffer");
    Require(Res.SupplyUsed == 4,
            "RecordUnitDeath settles supply in World-scoped PlayerResourceStore");

    EM.Events().Unsubscribe<Game::Events::UnitDeathEvent>(SubId);
    EM.Events().ClearAllQueues();
    Require(EM.Events().Read<Game::Events::UnitDeathEvent>().empty(),
            "ClearAllQueues empties frame event buffer");
}

} // namespace

int main()
{
    TestPagedSparseSetAndPointerStability();
    TestGenerationalEntityHandles();
    TestMultiWorldResourceIsolation();
    TestEventBusAndDeferredCommandBuffer();

    std::printf("EcsFoundationModernizationTest passed\n");
    return 0;
}
