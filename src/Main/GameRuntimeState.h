#pragma once

#include "Client/ClientRuntime.h"
#include "Main/GameGuiPresentation.h"
#include "Main/AppRuntime.h"
// === Consolidated from src/Main/CampaignRuntime.h ===


#include <string>
#include <utility>

#include "Core/Utils/Logger.h"
#include "Game/Campaign/CampaignController.h"
#include "Game/Trigger/TriggerVM.h"
#include "Main/CampaignPresentation.h"

namespace Game::Client::GameLoop
{

struct CampaignRuntime
{
    template <typename EntityManagerT, typename TriggerDataT>
    bool InitializeCampaign(EntityManagerT& Entities, const TriggerDataT* TriggerData)
    {
        Active = Campaign.Init(Entities, TriggerData);
        return Active;
    }

    bool IsActive() const
    {
        return Active;
    }

    void Deactivate()
    {
        Active = false;
        ScenarioPaused = false;
        ClearProgressTarget();
    }

    Game::Campaign::CampaignController& Controller()
    {
        return Campaign;
    }

    const Game::Campaign::CampaignController& Controller() const
    {
        return Campaign;
    }

    bool HasTriggerRuntime() const
    {
        return Campaign.GetTriggerVM() != nullptr;
    }

    template <typename SoundCallbackT>
    void SetSoundCallback(SoundCallbackT&& SoundCallback)
    {
        Campaign.SetSoundCallback(std::forward<SoundCallbackT>(SoundCallback));
    }

    template <typename CameraPositionProviderT>
    void SetMapAudioCallback(Core::Audio::AudioSystem& Audio,
                             const std::string& MapPath,
                             CameraPositionProviderT&& CameraPositionProvider)
    {
        Campaign.SetSoundCallback(
            [&Audio, &MapPath, CameraPositionProvider = std::forward<CameraPositionProviderT>(CameraPositionProvider)]
            (const Game::Campaign::CampaignMessage& Message) {
                const auto Camera = CameraPositionProvider();
                PlayCampaignMessageAudio(Audio, MapPath, Message, Camera.X, Camera.Y);
            });
    }

    template <typename SetAllianceT,
              typename ResolveImageIdT,
              typename CanBurrowT,
              typename ResolveCollisionBoundsT,
              typename CenterViewT,
              typename MinimapPingT,
              typename SetGamePausedT,
              typename ResolveSubunitImageIdT = std::function<uint32_t(int)>>
    void ConfigureTriggerRuntime(SetAllianceT&& SetAlliance,
                                 ResolveImageIdT&& ResolveImageID,
                                 CanBurrowT&& CanBurrow,
                                 ResolveCollisionBoundsT&& ResolveCollisionBounds,
                                 CenterViewT&& CenterView,
                                 MinimapPingT&& MinimapPing,
                                 SetGamePausedT&& SetGamePaused,
                                 ResolveSubunitImageIdT&& ResolveSubunitImageID = {})
    {
        auto* TriggerVM = Campaign.GetTriggerVM();
        if (!TriggerVM)
            return;

        Game::Trigger::TriggerRuntimeConfig TriggerConfig;
        TriggerConfig.SetAlliance = std::forward<SetAllianceT>(SetAlliance);
        TriggerConfig.ResolveImageID = std::forward<ResolveImageIdT>(ResolveImageID);
        TriggerConfig.CanBurrow = std::forward<CanBurrowT>(CanBurrow);
        TriggerConfig.ResolveCollisionBounds = std::forward<ResolveCollisionBoundsT>(ResolveCollisionBounds);
        TriggerConfig.CenterView = std::forward<CenterViewT>(CenterView);
        TriggerConfig.MinimapPing = std::forward<MinimapPingT>(MinimapPing);
        TriggerConfig.SetGamePaused = std::forward<SetGamePausedT>(SetGamePaused);
        TriggerConfig.ResolveSubunitImageID = std::forward<ResolveSubunitImageIdT>(ResolveSubunitImageID);
        TriggerVM->SetRuntimeConfig(std::move(TriggerConfig));
    }

    void SetProgressTarget(bool Expansion, int Race, int Mission)
    {
        ProgressSet = Expansion ? 1 : 0;
        ProgressRace = Race;
        ProgressMission = Mission;
        ProgressRecorded = false;
    }

    void ClearProgressTarget()
    {
        ProgressSet = -1;
        ProgressRace = -1;
        ProgressMission = -1;
        ProgressRecorded = false;
    }

    bool HasPendingProgressRecord() const
    {
        return !ProgressRecorded && ProgressSet >= 0 && ProgressRace >= 0 && ProgressMission >= 0;
    }

    void RecordVictoryProgressIfNeeded()
    {
        if (!HasPendingProgressRecord())
            return;

        if (!IsActive() || Campaign.GetState() != Game::Campaign::CampaignState::Victory)
            return;

        const int UnlockedCount = GetUnlockedMissionCount();
        Game::Campaign::SaveCampaignProgressCount(GetProgressSet(), GetProgressRace(), UnlockedCount);
        MarkProgressRecorded();
        Core::Utils::Logger::Info("CampaignProgress",
            "Unlocked campaign set=" + std::to_string(GetProgressSet()) +
            " race=" + std::to_string(GetProgressRace()) +
            " count=" + std::to_string(UnlockedCount));
    }

    int GetProgressSet() const
    {
        return ProgressSet;
    }

    int GetProgressRace() const
    {
        return ProgressRace;
    }

    int GetUnlockedMissionCount() const
    {
        const int UnlockedCount = ProgressMission + 2;
        if (UnlockedCount < 1)
            return 1;
        if (UnlockedCount > 10)
            return 10;
        return UnlockedCount;
    }

    void MarkProgressRecorded()
    {
        ProgressRecorded = true;
    }

    void SetScenarioPaused(bool Paused)
    {
        ScenarioPaused = Paused;
    }

    bool IsScenarioPaused() const
    {
        return ScenarioPaused;
    }

    Game::Campaign::CampaignController Campaign;
    bool Active = false;
    bool ScenarioPaused = false;
    int ProgressSet = -1;
    int ProgressRace = -1;
    int ProgressMission = -1;
    bool ProgressRecorded = false;
};

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameAudioRuntime.h ===
#include "Client/ClientRuntime.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/Utils/Logger.h"

#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"
#include "Resource/TblDecoder.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Game::Client::GameLoop
{

class GameAudioRuntime
{
public:
    void Initialize()
    {
        Audio.Init();
    }

    Core::Audio::AudioSystem& GetAudioSystem() { return Audio; }
    const Core::Audio::AudioSystem& GetAudioSystem() const { return Audio; }

    int PlayFile(const std::string& Path, float X = 0.0f, float Y = 0.0f, int Priority = 20,
                 Core::Audio::SoundFlags Flags = Core::Audio::SoundFlags::PerSound)
    {
        return Audio.PlayFile(Path, X, Y, Priority, Flags);
    }

    void ConfigureGameSounds(Game::Client::ClientRuntime& Client)
    {
        auto Source = Client.GetArchiveSource();
        Audio.SetFileLoader([Source](const std::string& Path) {
            auto Data = Source.ReadWithFallback(Path);
            return Data ? std::move(*Data) : std::vector<uint8_t>{};
        });

        const auto Dat = Source.ReadWithFallback("arr/sfxdata.dat");
        const auto Tbl = Source.ReadWithFallback("arr/sfxdata.tbl");
        if (!Dat || !Tbl)
        {
            Core::Utils::Logger::Warning("GameAudio", "sfxdata.dat/tbl not found in game archives");
            return;
        }

        constexpr size_t Count = 1144;
        constexpr size_t FilenameOffset = 0;
        constexpr size_t PriorityOffset = Count * 4;
        constexpr size_t FlagsOffset = PriorityOffset + Count;
        constexpr size_t RaceOffset = FlagsOffset + Count;
        constexpr size_t MinVolumeOffset = RaceOffset + Count * 2;
        if (Dat->size() < MinVolumeOffset + Count)
        {
            Core::Utils::Logger::Warning("GameAudio", "sfxdata.dat is truncated");
            return;
        }

        const auto Paths = Resource::TblDecoder::DecodeStrings(*Tbl);
        auto ReadU32 = [&](size_t Offset) -> uint32_t {
            return (uint32_t)(*Dat)[Offset] |
                   ((uint32_t)(*Dat)[Offset + 1] << 8) |
                   ((uint32_t)(*Dat)[Offset + 2] << 16) |
                   ((uint32_t)(*Dat)[Offset + 3] << 24);
        };
        size_t ConfiguredPaths = 0;
        for (size_t I = 0; I < Count; ++I)
        {
            const uint32_t PathIndex = ReadU32(FilenameOffset + I * 4);
            const std::string SoundPath =
                Core::Audio::ResolveArchiveSoundPath(Paths, PathIndex);
            if (!SoundPath.empty())
            {
                Audio.SetSoundPath((int)I, SoundPath);
                ++ConfiguredPaths;
            }

            Core::Audio::SoundType Type;
            Type.Id = (uint32_t)I;
            Type.Priority = (*Dat)[PriorityOffset + I];
            Type.Flags = (*Dat)[FlagsOffset + I];
            Type.Race = (int)(*Dat)[RaceOffset + I * 2] |
                        ((int)(*Dat)[RaceOffset + I * 2 + 1] << 8);
            Type.MinVolume = (*Dat)[MinVolumeOffset + I];
            Audio.SetSoundType((int)I, Type);
        }
        Core::Utils::Logger::Info(
            "GameAudio",
            "Configured " + std::to_string(ConfiguredPaths) +
            " archive sound paths from 1144 sfxdata entries");
    }

    void UpdateForClientView(Game::Client::ClientRuntime& Client, float DeltaSeconds)
    {
        const Core::Math::Vec2 CameraPosition = Client.GetCameraPosition();
        Audio.SetListenerPosition(CameraPosition.X, CameraPosition.Y);

        float ScreenLeft = 0.0f;
        float ScreenTop = 0.0f;
        float WorldW = 0.0f;
        float WorldH = 0.0f;
        Client.GetVisibleWorldRect(ScreenLeft, ScreenTop, WorldW, WorldH);
        Audio.SetScreenRect(ScreenLeft, ScreenTop, WorldW, WorldH);

        Audio.Update(DeltaSeconds);
    }

    void UpdateClientWorldEffects(Game::Client::ClientRuntime& Client,
                                  Core::ECS::EntityManager& Entities,
                                  float DeltaSeconds)
    {
        Client.UpdateWorldEffects(Entities, Audio, DeltaSeconds);
    }

    void RegisterGameplaySystems(GameSession& Session)
    {
        GameSessionBootstrap::RegisterGameplaySystems(Session, Audio);
    }

    template <typename CameraPositionProviderT>
    void ConfigureCampaignMapAudio(CampaignRuntime& Campaign,
                                   const std::string& MapPath,
                                   CameraPositionProviderT&& CameraPositionProvider)
    {
        Campaign.SetMapAudioCallback(
            Audio,
            MapPath,
            std::forward<CameraPositionProviderT>(CameraPositionProvider));
    }

private:
    Core::Audio::AudioSystem Audio;
};

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameInteractionSmokeRuntime.h ===
#include "Client/ClientRuntime.h"
#include "Client/Selection.h"
#include "Resource/GameDataLoader.h"
#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Main/GameCommandDispatchRuntime.h"
#include "Main/GameSession.h"
#include "Main/GameGuiPresentation.h"

#if ENABLE_GUI_UI
#include "third_party/sokol/sokol_app.h"
#endif

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Game::Client::GameLoop
{

struct GameInteractionSmokeState
{
    bool Attempted = false;
};

struct GameInteractionDemoState
{
    enum class Stage
    {
        Init,
        HarvestLoop,
        ProductionQueue,
        Construction,
        GasConstruction,
        GasHarvest,
        BarracksConstruction,
        MarineProduction,
        Research,
        FullRoster,
        Observe,
        Done,
        Failed
    };

    bool Enabled = false;
    bool FrameProbeLogged = false;
    bool Started = false;
    Stage CurrentStage = Stage::Init;
    float StageSeconds = 0.0f;
    int WorkerID = -1;
    int ResourceID = -1;
    int DepotID = -1;
    int TrainedUnitStartCount = 0;
    int BuildSiteID = -1;
    int RefineryID = -1;
    int GasWorkerID = -1;
    int BarracksID = -1;
    int MarineStartCount = 0;
    int EngineeringBayID = -1;
    int ResearchStartLevel = 0;
    int FocusEntityID = -1;
    std::vector<int> FullRosterEntityIDs;
    int FullRosterBuildingCount = 0;
    int FullRosterUnitCount = 0;
};


#if !defined(STARCRAFT_DISABLE_SMOKE_HARNESS)
#include "../../tests/Smoke/GameInteractionSmokeHarness.inl"
#else
inline void RunInteractionSmokeIfRequested(GameInteractionSmokeState& State,
                                           GameSession& Session,
                                           CampaignRuntime& Campaign,
                                           ClientRuntime& Client,
                                           GuiRuntime& Gui,
                                           UI::Hud& Hud)
{
    (void)State; (void)Session; (void)Campaign; (void)Client; (void)Gui; (void)Hud;
}

inline float RunInteractionDemoIfRequested(GameInteractionDemoState& State,
                                           GameSession& Session,
                                           ClientRuntime& Client,
                                           UI::Hud& Hud,
                                           float Dt)
{
    (void)State; (void)Session; (void)Client; (void)Hud; (void)Dt;
    return 1.0f;
}
#endif

} // namespace Game::Client::GameLoop

#include "Main/GameMenuPresentation.h"
#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{

struct GameAppRuntimeState
{
    AppRuntime Runtime;
    GameAudioRuntime Audio;
    GameMenuRuntime Menu;
    GuiRuntime Gui;
};

struct GameClientRuntimeState
{
    ClientRuntime Runtime;
    UI::Hud Hud;
};

struct GameSessionRuntimeState
{
    GameSession Game;
    CampaignRuntime Campaign;
};

struct GameRuntimeState
{
    GameAppRuntimeState App;
    GameClientRuntimeState Client;
    GameSessionRuntimeState Session;
    GameInteractionSmokeState InteractionSmoke;
    GameInteractionDemoState InteractionDemo;
};

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameEntitySpawnRuntime.h ===


#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "Core/Math/Vec2.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Main/GameLoop/CursorTypes.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop::GameEntitySpawnRuntime
{

inline constexpr int NormalizeChkPlayerID(uint8_t ChkOwner)
{
    // CHK UNIT owner is already a zero-based StarCraft player id (0..11).
    // Keep it as data here; callers that index arrays must bounds-check there.
    return (int)ChkOwner;
}

static_assert(NormalizeChkPlayerID(1) == 1,
              "CHK owner values are data ids, not one-based display labels.");

template <typename ResolveImageIdT, typename ResolveSubunitImageIdT, typename ResolveCollisionBoundsT>
inline void SpawnChkEntities(GameSession& Session,
                             ResolveImageIdT&& ResolveImageID,
                             ResolveSubunitImageIdT&& ResolveSubunitImageID,
                             ResolveCollisionBoundsT&& ResolveCollisionBounds)
{
    auto& EM = Session.Entities();
    for (const auto& UnitPlacement : Session.GetChkUnits())
    {
        if (!IsRenderableChkUnit(UnitPlacement.UnitID))
            continue;

        int EntityID = EM.CreateEntity();
        auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
        Trans.Position = {(float)UnitPlacement.X, (float)UnitPlacement.Y};

        auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
        Unit.ChkUnitID = UnitPlacement.UnitID;
        Unit.HitPoints = 60;
        Unit.MaxHitPoints = 60;
        Unit.Speed = IsMineralUnit(UnitPlacement.UnitID) || IsGeyserUnit(UnitPlacement.UnitID) ? 0.0f : 2.8f;
        Unit.SightRange = 8;

        const int UnitDefID = Game::Races::GetUnitDefIDByChkUnitID(UnitPlacement.UnitID);
        if (UnitDefID >= 0)
        {
            const auto Def = Game::Races::GetUnitDefByID(UnitDefID);
            Unit.UnitRace = Def.RaceType;
            Unit.SizeType = Def.SizeType;
            Unit.Armor = Def.Armor;
            Unit.Speed = Def.Speed;
            Unit.SightRange = Def.SightRange;
            Unit.SupplyCost = Def.SupplyCost;
            Unit.MaxHitPoints = Def.HitPoints;
            Unit.HitPoints = Def.HitPoints;
            if (UnitPlacement.HpPercent > 0 && UnitPlacement.HpPercent < 100)
                Unit.HitPoints = std::max(1, Unit.MaxHitPoints * (int)UnitPlacement.HpPercent / 100);
            Unit.ShieldPoints = Def.ShieldPoints;
            Unit.MaxShieldPoints = Def.ShieldPoints;
            Unit.EnergyPoints = Def.EnergyPoints;
            Unit.MaxEnergyPoints = Def.EnergyPoints;
        }
        else if (IsBuildingChkId(UnitPlacement.UnitID) ||
                 IsMineralUnit(UnitPlacement.UnitID) ||
                 IsGeyserUnit(UnitPlacement.UnitID))
        {
            Unit.SizeType = Game::Components::UnitSize::Large;
            Unit.MaxHitPoints = IsGeyserUnit(UnitPlacement.UnitID) ? 500 :
                IsMineralUnit(UnitPlacement.UnitID) ? 1 : 1000;
            Unit.HitPoints = Unit.MaxHitPoints;
        }

        auto& Player = EM.AddComponent<Game::Components::PlayerComponent>(EntityID);
        Player.PlayerID = NormalizeChkPlayerID(UnitPlacement.Owner);
        constexpr uint8_t kChkOwnerRescuePassive = 3;
        const auto& ScriptPlayers = Session.GetScriptData().Players;
        if (Player.PlayerID >= 0 && Player.PlayerID < (int)ScriptPlayers.size())
            Player.IsRescuePassive =
                ScriptPlayers[(size_t)Player.PlayerID].Owner == kChkOwnerRescuePassive;
        Game::Systems::UpgradeSystem::RefreshUnitEnergyCap(EM, EntityID);
        if (UnitPlacement.EnergyPercent > 0 && Unit.MaxEnergyPoints > 0)
        {
            Unit.EnergyPoints =
                Unit.MaxEnergyPoints * std::min(100, (int)UnitPlacement.EnergyPercent) / 100;
        }
        Unit.HangarCount = std::max(0, (int)UnitPlacement.HangarCount);

        auto& RenderComp = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
        RenderComp.ImageID = (uint32_t)ResolveImageID(UnitPlacement.UnitID);
        RenderComp.SubunitImageID = (uint32_t)ResolveSubunitImageID(UnitPlacement.UnitID);
        RenderComp.FrameIndex = 0;
        RenderComp.TotalFrames = 1;
        RenderComp.Layer = IsBuildingChkId(UnitPlacement.UnitID) ?
            Game::Components::RenderLayer::Buildings :
            Game::Components::RenderLayer::Units;

        const bool CanBurrow = Game::Systems::ZergSystem::CanUnitBurrow(
            UnitPlacement.UnitID, RenderComp.ImageID);
        if (CanBurrow)
        {
            auto& Burrow = EM.AddComponent<Game::Components::BurrowComponent>(EntityID);
            Burrow.CanBurrow = true;
        }

        if ((UnitPlacement.ValidFlags & 0x10) != 0)
            Unit.IsInvincible = (UnitPlacement.StateFlags & 0x10) != 0;
        if ((UnitPlacement.ValidFlags & 0x08) != 0 &&
            (UnitPlacement.StateFlags & 0x08) != 0)
        {
            auto& Status = EM.AddComponent<Game::Components::StatusEffectComponent>(EntityID);
            Status.AddEffect(Game::Components::StatusEffect::Hallucination, 1000000.0f, -1);
        }
        if ((UnitPlacement.ValidFlags & 0x03) != 0)
        {
            auto& Cloak = EM.AddComponent<Game::Components::CloakComponent>(EntityID);
            Cloak.IsCloaked = (UnitPlacement.ValidFlags & 0x01) != 0 &&
                              (UnitPlacement.StateFlags & 0x01) != 0;
            Cloak.IsBurrowed = CanBurrow &&
                               (UnitPlacement.ValidFlags & 0x02) != 0 &&
                               (UnitPlacement.StateFlags & 0x02) != 0;
            if (Cloak.IsBurrowed)
            {
                auto* Burrow = EM.GetComponent<Game::Components::BurrowComponent>(EntityID);
                Burrow->IsBurrowed = true;
                Burrow->Phase = Game::Components::BurrowPhase::Burrowed;
                Cloak.IsCloaked = true;
            }
        }

        if (UnitDefID >= 0)
        {
            const auto Def = Game::Races::GetUnitDefByID(UnitDefID);
            if (Def.GroundDamage > 0 || Def.AirDamage > 0)
            {
                auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
                Game::Races::ApplyCombatDefinition(
                    Combat, Def, Game::Data::IsAirUnitType(Unit.ChkUnitID));
            }
        }

        if (IsMineralUnit(UnitPlacement.UnitID))
        {
            auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
            Res.Type = Game::Components::ResourceType::Minerals;
            Res.Amount = UnitPlacement.ResourceAmount > 0 ? (int)UnitPlacement.ResourceAmount : 1500;
            Res.MaxAmount = Res.Amount;
            auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
            Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(UnitPlacement.UnitID));
        }
        else if (IsGeyserUnit(UnitPlacement.UnitID))
        {
            auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
            Res.Type = Game::Components::ResourceType::VespeneGas;
            Res.Amount = UnitPlacement.ResourceAmount > 0 ? (int)UnitPlacement.ResourceAmount : 5000;
            Res.MaxAmount = Res.Amount;
            auto& Build = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
            Build.IsBuilding = true;
            Build.UnderConstruction = false;
            auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
            Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(UnitPlacement.UnitID));
        }
        else if (IsBuildingChkId(UnitPlacement.UnitID))
        {
            auto& Build = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
            Build.IsBuilding = true;
            Build.UnderConstruction = false;
            Build.IsOrganic = IsOrganicBuilding(UnitPlacement.UnitID);
            Build.RequiresPylon = UnitDefID >= 0
                ? Game::Races::GetUnitDefByID(UnitDefID).RequiresPylon
                : RequiresPylonChkId(UnitPlacement.UnitID);
            Build.TotalBuildTime = 120.0f;
            Build.BuildProgress = Build.TotalBuildTime;
            Build.BuildingDefID = UnitDefID;
            if (UnitDefID >= 0)
            {
                const auto Def = Game::Races::GetUnitDefByID(UnitDefID);
                Build.CanLiftOff = Def.CanLiftOff;
                Build.IsAddon = Def.IsAddon;
            }
            if (UnitDefID == Game::Races::GetUnitDefID("Refinery"))
            {
                auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
                Res.Type = Game::Components::ResourceType::VespeneGas;
                Res.Amount = UnitPlacement.ResourceAmount > 0 ? (int)UnitPlacement.ResourceAmount : 5000;
                Res.MaxAmount = Res.Amount;
            }
            auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
            Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(UnitPlacement.UnitID));
        }
        else
        {
            auto& Cmd = EM.AddComponent<Game::Components::CommandComponent>(EntityID);
            Cmd.CurrentCommand = Game::Components::CommandType::Stop;
            auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
            Path.NodeTolerance = 8.0f;
            Path.IsAir = Game::Data::IsAirUnitType(Unit.ChkUnitID);
            Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(UnitPlacement.UnitID));
        }
    }
    Game::Systems::BuildingSystem::RestoreSpawnedAddonLinks(EM);
}

template <typename ResolveImageIdT, typename ResolveCollisionBoundsT>
inline int SpawnResourceEntity(GameSession& Session,
                               int ChkUnitId,
                               int WorldX,
                               int WorldY,
                               int Amount,
                               ResolveImageIdT&& ResolveImageID,
                               ResolveCollisionBoundsT&& ResolveCollisionBounds)
{
    auto& EM = Session.Entities();
    int EntityID = EM.CreateEntity();
    auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
    Trans.Position = {(float)WorldX, (float)WorldY};
    auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
    Unit.ChkUnitID = ChkUnitId;
    Unit.SizeType = Game::Components::UnitSize::Large;
    Unit.HitPoints = IsGeyserUnit(ChkUnitId) ? 500 : 1;
    Unit.MaxHitPoints = Unit.HitPoints;
    Unit.Speed = 0.0f;
    auto& Player = EM.AddComponent<Game::Components::PlayerComponent>(EntityID);
    Player.PlayerID = 11;
    auto& RenderComp = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
    RenderComp.ImageID = (uint32_t)ResolveImageID(ChkUnitId);
    RenderComp.TotalFrames = 1;
    RenderComp.Layer = Game::Components::RenderLayer::Units;
    auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
    Res.Type = IsGeyserUnit(ChkUnitId) ?
        Game::Components::ResourceType::VespeneGas :
        Game::Components::ResourceType::Minerals;
    Res.Amount = Amount;
    Res.MaxAmount = Amount;
    auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
    Game::Components::ApplyCollisionBounds(Path, ResolveCollisionBounds(ChkUnitId));
    if (IsGeyserUnit(ChkUnitId))
    {
        auto& Build = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
        Build.IsBuilding = true;
        Build.UnderConstruction = false;
    }
    return EntityID;
}

inline void SpawnTerranAuditFormation(GameSession& Session, Core::Math::Vec2 CameraPosition)
{
    auto& EM = Session.Entities();
    const auto& TerranDefs = Game::Races::GetTerranDefs();
    float BaseX = CameraPosition.X - 240.0f;
    float BaseY = CameraPosition.Y - 160.0f;
    int UnitCol = 0;
    int BuildingCol = 0;
    for (size_t I = 0; I < TerranDefs.size(); ++I)
    {
        const auto& Def = TerranDefs[I];
        if (Def.IsBuilding)
        {
            Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(EM, (int)I,
                {BaseX + (float)(BuildingCol % 6) * 96.0f,
                 BaseY + 180.0f + (float)(BuildingCol / 6) * 96.0f}, 0);
            ++BuildingCol;
        }
        else
        {
            Game::Systems::ProductionSystem::DebugSpawnUnit(EM, (int)I,
                {BaseX + (float)(UnitCol % 6) * 72.0f,
                 BaseY + (float)(UnitCol / 6) * 72.0f}, 0);
            ++UnitCol;
        }
    }
}

inline void SpawnTerranAuditFormationOnce(GameSession& Session, Core::Math::Vec2 CameraPosition)
{
    if (!Session.IsEcsInitialized() || Session.HasTerranAuditSpawned())
        return;

    SpawnTerranAuditFormation(Session, CameraPosition);
    Session.MarkTerranAuditSpawned();
}

} // namespace Game::Client::GameLoop::GameEntitySpawnRuntime

// === Consolidated from src/Main/GameController.h ===


#include <cstdint>
#include <string>



namespace Game::Client::GameLoop
{

class GameController
{
public:
    GameController() = default;

    bool Initialize(int Width, int Height);
    void Update(float Dt, int Width, int Height);
    void Render(int Width, int Height);
    void OnEvent(const void* Event);
    void Shutdown();
    void StartGame();
    void RestartGame();
    void ReturnToMainMenu();
    void SetMapPathOverride(std::string MapPath);
    void SetCampaignRuntimeEnabled(bool Enabled);
    void SetDirectStartEnabled(bool Enabled);
    void SetInteractionDemoEnabled(bool Enabled);
    void SetInitialCameraOverride(float WorldX, float WorldY);
    void SetInitialGuiHudEnabled(bool Enabled);

    // Public accessors for platform callbacks to reach game state
    bool IsInitialized() const { return mRuntime.App.Runtime.Initialized; }
    int GetViewportWidth() const { return mRuntime.Client.Runtime.GetViewportWidth(); }
    int GetViewportHeight() const { return mRuntime.Client.Runtime.GetViewportHeight(); }

    // Networking
    bool HostGame(const std::string& BindIP, int Port, int MaxClients = 7);
    bool JoinGame(const std::string& HostIP, int Port);
    void StartNetworkGame();

    // Replay
    bool SaveReplay(const std::string& FilePath);
    bool LoadReplay(const std::string& FilePath);
    void RecordGameAction(uint8_t PlayerID, uint8_t ActionType,
                          uint16_t P1 = 0, uint16_t P2 = 0,
                          uint16_t P3 = 0, uint8_t P4 = 0);

    // Save/Load game state
    bool SaveGame(const std::string& FilePath);
    bool LoadGame(const std::string& FilePath);

private:
    void InitGameSystems();

    GameRuntimeState mRuntime;
    bool mHasInitialCameraOverride = false;
    float mInitialCameraX = 0.0f;
    float mInitialCameraY = 0.0f;
    bool mInitialGuiHudEnabled = false;
    bool mDirectStartEnabled = false;

    // Glue menu
};

} // namespace Game::Client::GameLoop
