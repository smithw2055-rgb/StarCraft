#pragma once

#include "Core/ECS/EntityManager.h"
#include "Core/ECS/EventBus.h"
#include "Game/Components.h"
#include "Game/SimulationContext.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "UI/HudState.h"
#include "UI/ViewModel/ObservableProperty.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Game::Client::UI::ViewModel
{

struct HudUnitStatusSnapshot
{
    int EntityId = -1;
    int ChkUnitId = -1;
    int HitPoints = 0;
    int MaxHitPoints = 0;
    int ShieldPoints = 0;
    int MaxShieldPoints = 0;
    int EnergyPoints = 0;
    int MaxEnergyPoints = 0;
    int QueueCount = 0;
    int QuantizedProgressPermille = 0;
    int LoadedUnitCount = 0;
    int RemainingMines = 0;
    int AddonEntityId = -1;
    int ResearchingId = -1;
    uint8_t FlightPhase = 0;
    uint8_t BurrowPhaseVal = 0;
    bool UnderConstruction = false;
    bool HasPylonPower = true;
    bool IsLifted = false;
    bool IsResearching = false;
    bool IsSieged = false;
    bool IsSiegeTransitioning = false;
    bool IsBurrowed = false;
    bool IsCloaked = false;
    bool IsMorphing = false;
    bool NukeArmed = false;

    bool operator==(const HudUnitStatusSnapshot& Other) const
    {
        return EntityId == Other.EntityId &&
               ChkUnitId == Other.ChkUnitId &&
               HitPoints == Other.HitPoints &&
               MaxHitPoints == Other.MaxHitPoints &&
               ShieldPoints == Other.ShieldPoints &&
               MaxShieldPoints == Other.MaxShieldPoints &&
               EnergyPoints == Other.EnergyPoints &&
               MaxEnergyPoints == Other.MaxEnergyPoints &&
               QueueCount == Other.QueueCount &&
               QuantizedProgressPermille == Other.QuantizedProgressPermille &&
               LoadedUnitCount == Other.LoadedUnitCount &&
               RemainingMines == Other.RemainingMines &&
               AddonEntityId == Other.AddonEntityId &&
               ResearchingId == Other.ResearchingId &&
               FlightPhase == Other.FlightPhase &&
               BurrowPhaseVal == Other.BurrowPhaseVal &&
               UnderConstruction == Other.UnderConstruction &&
               HasPylonPower == Other.HasPylonPower &&
               IsLifted == Other.IsLifted &&
               IsResearching == Other.IsResearching &&
               IsSieged == Other.IsSieged &&
               IsSiegeTransitioning == Other.IsSiegeTransitioning &&
               IsBurrowed == Other.IsBurrowed &&
               IsCloaked == Other.IsCloaked &&
               IsMorphing == Other.IsMorphing &&
               NukeArmed == Other.NukeArmed;
    }

    bool operator!=(const HudUnitStatusSnapshot& Other) const
    {
        return !(*this == Other);
    }
};

struct HudViewModelStats
{
    uint64_t TotalSyncCalls = 0;
    uint64_t CommandCardRebuilds = 0;
    uint64_t CommandCardSkips = 0;
    uint64_t InfoPanelRebuilds = 0;
    uint64_t InfoPanelSkips = 0;
    uint64_t ResourceTextRebuilds = 0;
    uint64_t EventBusSignalsHandled = 0;
};

// ============================================================================
// HudViewModel: Reactive MVVM ViewModel for Classic HUD & NoesisGUI MainGui
// - Subscribes to Core::ECS::EventBus domain events (ResourceChangedEvent,
//   ResearchCompletedEvent, BuildingCompletedEvent, UnitDeathEvent)
// - Tracks fine-grained HudDirtyFlags (Resources, Selection, CommandCard, InfoPanel)
// - Eliminates redundant per-frame CommandCard & InfoPanel rebuilds when idle
// ============================================================================
class HudViewModel
{
public:
    HudViewModel()
    {
        FormatResourceStrings();
    }

    ~HudViewModel()
    {
        UnbindFromWorld();
    }

    void BindToWorld(Core::ECS::EntityManager& World, int LocalPlayerID = 0)
    {
        if (mBoundWorld == &World && mLocalPlayerID == LocalPlayerID)
            return;

        UnbindFromWorld();
        mBoundWorld = &World;
        mLocalPlayerID = LocalPlayerID;
        mDirtyFlags = HudDirtyFlags::All;

        auto& Bus = World.Events();

        mResourceSub = Bus.Subscribe<Game::Events::ResourceChangedEvent>(
            [this](const Game::Events::ResourceChangedEvent& Ev)
            {
                if (Ev.PlayerID != mLocalPlayerID)
                    return;
                ++mStats.EventBusSignalsHandled;
                bool Changed = false;
                Changed |= Minerals.Set(Ev.NewMinerals);
                Changed |= Gas.Set(Ev.NewGas);
                if (Changed)
                {
                    FormatResourceStrings();
                    mDirtyFlags |= (HudDirtyFlags::Resources | HudDirtyFlags::CommandCard);
                }
            });

        mResearchSub = Bus.Subscribe<Game::Events::ResearchCompletedEvent>(
            [this](const Game::Events::ResearchCompletedEvent& Ev)
            {
                if (Ev.PlayerID != mLocalPlayerID)
                    return;
                ++mStats.EventBusSignalsHandled;
                mDirtyFlags |= (HudDirtyFlags::CommandCard | HudDirtyFlags::InfoPanel);
            });

        mBuildingSub = Bus.Subscribe<Game::Events::BuildingCompletedEvent>(
            [this](const Game::Events::BuildingCompletedEvent& Ev)
            {
                if (Ev.PlayerID == mLocalPlayerID ||
                    Ev.BuildingEntityID == PrimarySelectedEntityId.Get())
                {
                    ++mStats.EventBusSignalsHandled;
                    mDirtyFlags |= (HudDirtyFlags::CommandCard | HudDirtyFlags::InfoPanel);
                }
            });

        mDeathSub = Bus.Subscribe<Game::Events::UnitDeathEvent>(
            [this](const Game::Events::UnitDeathEvent& Ev)
            {
                const auto& Sel = SelectedEntityIds.Get();
                if (std::find(Sel.begin(), Sel.end(), Ev.VictimEntityID) != Sel.end())
                {
                    ++mStats.EventBusSignalsHandled;
                    mDirtyFlags |= (HudDirtyFlags::Selection |
                                    HudDirtyFlags::CommandCard |
                                    HudDirtyFlags::InfoPanel);
                }
            });
    }

    void UnbindFromWorld()
    {
        if (!mBoundWorld)
            return;

        auto& Bus = mBoundWorld->Events();
        if (mResourceSub != 0) Bus.Unsubscribe<Game::Events::ResourceChangedEvent>(mResourceSub);
        if (mResearchSub != 0) Bus.Unsubscribe<Game::Events::ResearchCompletedEvent>(mResearchSub);
        if (mBuildingSub != 0) Bus.Unsubscribe<Game::Events::BuildingCompletedEvent>(mBuildingSub);
        if (mDeathSub != 0)    Bus.Unsubscribe<Game::Events::UnitDeathEvent>(mDeathSub);

        mResourceSub = 0;
        mResearchSub = 0;
        mBuildingSub = 0;
        mDeathSub = 0;
        mBoundWorld = nullptr;
    }

    void SetLocalPlayerID(int PlayerID)
    {
        if (mLocalPlayerID != PlayerID)
        {
            mLocalPlayerID = PlayerID;
            mDirtyFlags |= HudDirtyFlags::All;
        }
    }

    int GetLocalPlayerID() const { return mLocalPlayerID; }

    void SetResourceState(const HudResourceState& Res)
    {
        bool Changed = false;
        Changed |= Minerals.Set(Res.MineralCount);
        Changed |= Gas.Set(Res.GasCount);
        Changed |= SupplyUsed.Set(Res.SupplyUsed);
        Changed |= SupplyMax.Set(Res.SupplyMax);
        if (Changed)
        {
            FormatResourceStrings();
            mDirtyFlags |= (HudDirtyFlags::Resources | HudDirtyFlags::CommandCard);
        }
    }

    // Synchronizes ViewModel with ECS World & current selection, returning active dirty flags
    HudDirtyFlags SyncWithWorld(Core::ECS::EntityManager& World,
                                const std::vector<int>& CurrentSelectedIds,
                                int CurrentPanelPage = 0,
                                int CurrentActiveCommand = -1)
    {
        if (mBoundWorld != &World)
            BindToWorld(World, mLocalPlayerID);

        ++mStats.TotalSyncCalls;

        // Sync from HarvestSystem PlayerResources when the underlying store mutates
        {
            const auto& PlayerRes = Game::Systems::HarvestSystem::GetResources(World, mLocalPlayerID);
            if (PlayerRes.Minerals != mLastStoreMinerals ||
                PlayerRes.VespeneGas != mLastStoreGas ||
                PlayerRes.SupplyUsed != mLastStoreSupplyUsed ||
                PlayerRes.SupplyMax != mLastStoreSupplyMax)
            {
                mLastStoreMinerals = PlayerRes.Minerals;
                mLastStoreGas = PlayerRes.VespeneGas;
                mLastStoreSupplyUsed = PlayerRes.SupplyUsed;
                mLastStoreSupplyMax = PlayerRes.SupplyMax;
                bool ResChanged = false;
                ResChanged |= Minerals.Set(PlayerRes.Minerals);
                ResChanged |= Gas.Set(PlayerRes.VespeneGas);
                ResChanged |= SupplyUsed.Set(PlayerRes.SupplyUsed);
                ResChanged |= SupplyMax.Set(PlayerRes.SupplyMax);
                if (ResChanged)
                {
                    FormatResourceStrings();
                    mDirtyFlags |= (HudDirtyFlags::Resources | HudDirtyFlags::CommandCard);
                }
            }
        }

        if (SelectedEntityIds.Set(CurrentSelectedIds))
        {
            mDirtyFlags |= (HudDirtyFlags::Selection |
                            HudDirtyFlags::CommandCard |
                            HudDirtyFlags::InfoPanel);
        }

        const int PrimaryId = CurrentSelectedIds.empty() ? -1 : CurrentSelectedIds.front();
        if (PrimarySelectedEntityId.Set(PrimaryId))
        {
            mDirtyFlags |= (HudDirtyFlags::Selection |
                            HudDirtyFlags::CommandCard |
                            HudDirtyFlags::InfoPanel);
        }

        if (CommandPanelPage.Set(CurrentPanelPage) ||
            ActiveCommandIndex.Set(CurrentActiveCommand))
        {
            mDirtyFlags |= HudDirtyFlags::CommandCard;
        }

        // Check if tech-tree buildings or upgrades changed in the World
        const uint64_t TechHash = ComputeWorldTechAndBuildingSignature(World, mLocalPlayerID);
        if (TechHash != mLastWorldTechSignature)
        {
            mLastWorldTechSignature = TechHash;
            mDirtyFlags |= (HudDirtyFlags::CommandCard | HudDirtyFlags::InfoPanel);
        }

        // Sample primary entity status
        const HudUnitStatusSnapshot NewStatus = CaptureUnitStatus(World, PrimaryId);
        const HudUnitStatusSnapshot OldStatus = PrimaryUnitStatus.Get();
        if (PrimaryUnitStatus.Set(NewStatus))
        {
            mDirtyFlags |= HudDirtyFlags::InfoPanel;
            // Rebuild CommandCard too if structural/state flags affecting buttons changed
            if (OldStatus.EntityId != NewStatus.EntityId ||
                OldStatus.ChkUnitId != NewStatus.ChkUnitId ||
                OldStatus.UnderConstruction != NewStatus.UnderConstruction ||
                OldStatus.HasPylonPower != NewStatus.HasPylonPower ||
                OldStatus.IsLifted != NewStatus.IsLifted ||
                OldStatus.FlightPhase != NewStatus.FlightPhase ||
                OldStatus.AddonEntityId != NewStatus.AddonEntityId ||
                OldStatus.IsResearching != NewStatus.IsResearching ||
                OldStatus.ResearchingId != NewStatus.ResearchingId ||
                OldStatus.IsSieged != NewStatus.IsSieged ||
                OldStatus.IsSiegeTransitioning != NewStatus.IsSiegeTransitioning ||
                OldStatus.IsBurrowed != NewStatus.IsBurrowed ||
                OldStatus.BurrowPhaseVal != NewStatus.BurrowPhaseVal ||
                OldStatus.IsCloaked != NewStatus.IsCloaked ||
                OldStatus.IsMorphing != NewStatus.IsMorphing ||
                OldStatus.NukeArmed != NewStatus.NukeArmed ||
                OldStatus.QueueCount != NewStatus.QueueCount ||
                OldStatus.LoadedUnitCount != NewStatus.LoadedUnitCount ||
                OldStatus.RemainingMines != NewStatus.RemainingMines ||
                OldStatus.EnergyPoints != NewStatus.EnergyPoints)
            {
                mDirtyFlags |= HudDirtyFlags::CommandCard;
            }
        }

        // For multi-selection (>1 units), check if any secondary selected unit's state changed
        if (CurrentSelectedIds.size() > 1)
        {
            uint64_t MultiHash = 14695981039346656037ULL;
            for (int Id : CurrentSelectedIds)
            {
                if (const auto* U = World.GetComponent<Game::Components::UnitComponent>(Id))
                {
                    MultiHash ^= static_cast<uint64_t>(U->ChkUnitID);
                    MultiHash *= 1099511628211ULL;
                    MultiHash ^= static_cast<uint64_t>(U->HitPoints + (U->ShieldPoints << 16));
                    MultiHash *= 1099511628211ULL;
                }
            }
            if (MultiHash != mLastMultiSelectHpHash)
            {
                mLastMultiSelectHpHash = MultiHash;
                mDirtyFlags |= (HudDirtyFlags::InfoPanel | HudDirtyFlags::CommandCard);
            }
        }

        return mDirtyFlags;
    }

    void MarkCommandCardDirty()
    {
        mDirtyFlags |= HudDirtyFlags::CommandCard;
    }

    void MarkInfoPanelDirty()
    {
        mDirtyFlags |= HudDirtyFlags::InfoPanel;
    }

    void MarkAllDirty()
    {
        mDirtyFlags |= HudDirtyFlags::All;
    }

    HudDirtyFlags GetDirtyFlags() const { return mDirtyFlags; }

    void ClearDirtyFlags(HudDirtyFlags FlagsToClear = HudDirtyFlags::All)
    {
        mDirtyFlags = static_cast<HudDirtyFlags>(
            static_cast<uint32_t>(mDirtyFlags) & ~static_cast<uint32_t>(FlagsToClear));
    }

    void RecordCommandCardRebuild() { ++mStats.CommandCardRebuilds; }
    void RecordCommandCardSkip()    { ++mStats.CommandCardSkips; }
    void RecordInfoPanelRebuild()   { ++mStats.InfoPanelRebuilds; }
    void RecordInfoPanelSkip()      { ++mStats.InfoPanelSkips; }

    const HudViewModelStats& GetStats() const { return mStats; }

    // Observable properties exposed for View / XAML binding
    ObservableProperty<int> Minerals{50};
    ObservableProperty<int> Gas{0};
    ObservableProperty<int> SupplyUsed{0};
    ObservableProperty<int> SupplyMax{200};
    ObservableProperty<std::string> FormattedMineralText{"50"};
    ObservableProperty<std::string> FormattedGasText{"0"};
    ObservableProperty<std::string> FormattedSupplyText{"0/200"};

    ObservableProperty<std::vector<int>> SelectedEntityIds{};
    ObservableProperty<int> PrimarySelectedEntityId{-1};
    ObservableProperty<int> CommandPanelPage{0};
    ObservableProperty<int> ActiveCommandIndex{-1};
    ObservableProperty<HudUnitStatusSnapshot> PrimaryUnitStatus{};

private:
    void FormatResourceStrings()
    {
        char Buf[32];
        std::snprintf(Buf, sizeof(Buf), "%d", Minerals.Get());
        FormattedMineralText.Set(Buf);

        std::snprintf(Buf, sizeof(Buf), "%d", Gas.Get());
        FormattedGasText.Set(Buf);

        std::snprintf(Buf, sizeof(Buf), "%d/%d",
                      SupplyUsed.Get(), std::min(200, SupplyMax.Get()));
        FormattedSupplyText.Set(Buf);
        ++mStats.ResourceTextRebuilds;
    }

    static uint64_t ComputeWorldTechAndBuildingSignature(Core::ECS::EntityManager& World, int PlayerID)
    {
        uint64_t H = 14695981039346656037ULL;
        auto Mix = [&](uint64_t V)
        {
            H ^= V;
            H *= 1099511628211ULL;
        };

        const auto& Upg = Game::Systems::UpgradeSystem::GetUpgrades(World, PlayerID);
        for (int Level : Upg.Levels)
        {
            Mix(static_cast<uint64_t>(Level));
        }

        World.ForEachComponent<Game::Components::BuildingComponent, Game::Components::UnitComponent>(
            [&](Core::ECS::EntityID Id,
                const Game::Components::BuildingComponent& Bldg,
                const Game::Components::UnitComponent& Unit)
            {
                Mix(static_cast<uint64_t>(Id));
                Mix(static_cast<uint64_t>(Unit.ChkUnitID));
                Mix(static_cast<uint64_t>(Bldg.BuildingDefID));
                Mix(static_cast<uint64_t>(Bldg.UnderConstruction ? 1u : 0u) |
                    (static_cast<uint64_t>(Bldg.IsLifted ? 1u : 0u) << 1) |
                    (static_cast<uint64_t>(Bldg.FlightPhase) << 2) |
                    (static_cast<uint64_t>(Bldg.HasPylonPower ? 1u : 0u) << 5) |
                    (static_cast<uint64_t>(Bldg.IsResearching ? 1u : 0u) << 6));
                Mix(static_cast<uint64_t>(Bldg.AddonID + 2));
                Mix(static_cast<uint64_t>(Bldg.LarvaCount));
            });

        World.ForEachComponent<Game::Components::NukeComponent>(
            [&](Core::ECS::EntityID Id, const Game::Components::NukeComponent& Nuke)
            {
                Mix(static_cast<uint64_t>(Id));
                Mix(static_cast<uint64_t>(Nuke.Armed ? 1u : 0u));
            });

        return H;
    }

    static HudUnitStatusSnapshot CaptureUnitStatus(Core::ECS::EntityManager& World, int EntityId)
    {
        HudUnitStatusSnapshot Snap;
        if (EntityId < 0 || !World.IsAlive(EntityId))
            return Snap;

        const auto* Unit = World.GetComponent<Game::Components::UnitComponent>(EntityId);
        if (!Unit)
            return Snap;

        Snap.EntityId = EntityId;
        Snap.ChkUnitId = Unit->ChkUnitID;
        Snap.HitPoints = Unit->HitPoints;
        Snap.MaxHitPoints = Unit->MaxHitPoints;
        Snap.ShieldPoints = Unit->ShieldPoints;
        Snap.MaxShieldPoints = Unit->MaxShieldPoints;
        Snap.EnergyPoints = Unit->EnergyPoints;
        Snap.MaxEnergyPoints = Unit->MaxEnergyPoints;
        Snap.IsSieged = (Unit->ChkUnitID == 30 || Unit->ChkUnitID == 25);

        if (const auto* SiegeTrans = World.GetComponent<Game::Components::SiegeTransitionComponent>(EntityId))
        {
            Snap.IsSiegeTransitioning = SiegeTrans->Active;
        }

        if (const auto* Bldg = World.GetComponent<Game::Components::BuildingComponent>(EntityId))
        {
            Snap.UnderConstruction = Bldg->UnderConstruction;
            Snap.HasPylonPower = Bldg->HasPylonPower;
            Snap.IsLifted = Bldg->IsLifted;
            Snap.FlightPhase = static_cast<uint8_t>(Bldg->FlightPhase);
            Snap.AddonEntityId = Bldg->AddonID;
            Snap.IsResearching = Bldg->IsResearching;
            Snap.ResearchingId = Bldg->ResearchingID;
            if (Bldg->UnderConstruction && Bldg->TotalBuildTime > 0.0f)
            {
                Snap.QuantizedProgressPermille = static_cast<int>(
                    std::lround((Bldg->BuildProgress / Bldg->TotalBuildTime) * 1000.0f));
            }
            else if (Bldg->IsResearching && Bldg->TotalBuildTime > 0.0f)
            {
                Snap.QuantizedProgressPermille = static_cast<int>(
                    std::lround(((Bldg->TotalBuildTime - Bldg->BuildTimeRemaining) / Bldg->TotalBuildTime) * 1000.0f));
            }
        }

        if (const auto* Prod = World.GetComponent<Game::Components::ProductionComponent>(EntityId))
        {
            Snap.QueueCount = static_cast<int>(Prod->Queue.size());
            if (!Prod->Queue.empty() && Prod->TrainingTimePerUnit > 0.0f)
            {
                Snap.QuantizedProgressPermille = static_cast<int>(
                    std::lround((Prod->TrainingProgress / Prod->TrainingTimePerUnit) * 1000.0f));
            }
        }

        if (const auto* Loaded = World.GetComponent<Game::Components::LoadedUnitsComponent>(EntityId))
        {
            Snap.LoadedUnitCount = static_cast<int>(Loaded->PassengerIDs.size());
        }

        if (const auto* Mines = World.GetComponent<Game::Components::VultureMineInventoryComponent>(EntityId))
        {
            Snap.RemainingMines = Mines->RemainingMines;
        }

        if (const auto* Burrow = World.GetComponent<Game::Components::BurrowComponent>(EntityId))
        {
            Snap.IsBurrowed = Burrow->IsBurrowed;
            Snap.BurrowPhaseVal = static_cast<uint8_t>(Burrow->Phase);
        }

        if (const auto* Cloak = World.GetComponent<Game::Components::CloakComponent>(EntityId))
        {
            Snap.IsCloaked = Cloak->IsCloaked;
        }

        if (const auto* Morph = World.GetComponent<Game::Components::MorphComponent>(EntityId))
        {
            Snap.IsMorphing = Morph->IsMorphing;
        }

        if (const auto* Nuke = World.GetComponent<Game::Components::NukeComponent>(EntityId))
        {
            Snap.NukeArmed = Nuke->Armed;
        }

        return Snap;
    }

    Core::ECS::EntityManager* mBoundWorld = nullptr;
    int mLocalPlayerID = 0;
    Core::ECS::SubscriptionID mResourceSub = 0;
    Core::ECS::SubscriptionID mResearchSub = 0;
    Core::ECS::SubscriptionID mBuildingSub = 0;
    Core::ECS::SubscriptionID mDeathSub = 0;
    HudDirtyFlags mDirtyFlags = HudDirtyFlags::All;
    int mLastStoreMinerals = 400;
    int mLastStoreGas = 100;
    int mLastStoreSupplyUsed = 0;
    int mLastStoreSupplyMax = 10;
    uint64_t mLastMultiSelectHpHash = 0;
    uint64_t mLastWorldTechSignature = 0;
    HudViewModelStats mStats = {};
};

} // namespace Game::Client::UI::ViewModel
