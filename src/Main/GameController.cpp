#include "Main/GameRuntimeState.h"
#include "Game/Campaign/CampaignMissionCatalog.h"
#include "Main/GameGuiPresentation.h"
#include "Main/GameRuntimeFrame.h"
#include "Render/Draw2D.h"

#include "Core/Platform/Platform.h"
#include "Core/Utils/InputDebug.h"
#include "Core/Utils/Logger.h"
#include "Game/Components.h"
#include "Game/Races/RaceDef.h"
// === Consolidated from src/Main/GameEcsSessionBootstrap.h ===
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <string>

#include "Client/ClientRuntime.h"
#include "Core/Utils/Logger.h"
#include "Main/GameRuntimeFrame.h"
// === Consolidated from src/Main/GameCampaignBootstrap.h ===
#include <string>

#include "Client/ClientRuntime.h"
#include "Main/GameSession.h"
#include "Game/Systems/ZergSystem.h"
#include "Resource/GameDataLoader.h"

namespace Game::Client::GameLoop
{

inline void InitializeCampaignRuntimeForSession(
    CampaignRuntime& Campaign,
    GameSession& Session,
    GameAudioRuntime& Audio,
    ClientRuntime& Client,
    const std::string& MapPath,
    bool PreserveManualCamera)
{
    auto& Entities = Session.Systems().GetEntityManager();
    Campaign.InitializeCampaign(
        Entities,
        Session.GetScriptData().Triggers.empty() ? nullptr : &Session.GetScriptData());

    if (!Campaign.IsActive() || !Campaign.HasTriggerRuntime())
        return;

    Audio.ConfigureCampaignMapAudio(
        Campaign,
        MapPath,
        [&Client]() { return Client.GetCameraPosition(); });

    Campaign.ConfigureTriggerRuntime(
        [&Session](int PlayerA, int PlayerB, bool Allied) {
            if (auto* FogSystem = Session.GetFogSystem())
                FogSystem->SetAlliance(PlayerA, PlayerB, Allied);
        },
        [&Client](int ChkUnitID, auto, int) {
            return (int)Client.GetSpriteAssets().ResolveImageID(ChkUnitID);
        },
        [](int ChkUnitID, uint32_t ImageID) {
            return Game::Systems::ZergSystem::CanUnitBurrow(
                ChkUnitID, ImageID);
        },
        [&Client](int ChkUnitID) {
            Game::Components::UnitCollisionBounds Bounds;
            auto DataAssets = Client.GetDataAssets();
            if (!DataAssets.HasCache())
                return Bounds;

            const auto* DataLoader = DataAssets.GetDataLoader();
            const auto* UnitData = DataLoader ? DataLoader->FindUnitByID((uint32_t)ChkUnitID) : nullptr;
            if (!UnitData)
                return Bounds;

            Bounds.IsValid = UnitData->DimensionWidth() > 0 && UnitData->DimensionHeight() > 0;
            Bounds.Left = UnitData->DimensionLeft;
            Bounds.Up = UnitData->DimensionUp;
            Bounds.Right = UnitData->DimensionRight;
            Bounds.Down = UnitData->DimensionDown;
            return Bounds;
        },
        [&Session, &Client, PreserveManualCamera](auto Position) {
            if (PreserveManualCamera)
                return;
            if (!Session.IsMapLoaded())
                return;
            Client.CenterCamera(Position.X, Position.Y);
        },
        [&Client](auto Position) {
            Client.AddCampaignPing(Position);
        },
        [&Campaign](bool Paused) {
            Campaign.SetScenarioPaused(Paused);
        },
        [&Client](int ChkUnitID) {
            return Client.GetSpriteAssets().ResolveSubunitImageID(ChkUnitID);
        });
}

} // namespace Game::Client::GameLoop

#include "Main/GameSession.h"
#include "Main/GameSessionBootstrap.h"
#include "Resource/GameDataLoader.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop
{
inline void InitializeEcsMapAndGameplaySystems(
    GameSession& Session,
    ClientRuntime& Client,
    GameAudioRuntime& Audio)
{
    auto* TilesetData = Session.HasTilesetName()
        ? Client.GetTilesetAssets().GetTilesetData(Session.GetTilesetName())
        : nullptr;
    Session.BuildEcsMapFromLoadedMap(TilesetData);

    Audio.RegisterGameplaySystems(Session);
}

inline void InitializeGameplayEntitiesAndRuntimeState(
    GameSession& Session,
    ClientRuntime& Client,
    UI::Hud& Hud,
    bool AddPlayableStarts,
    bool RegisterGenericAi)
{
    auto ResolveChkImageID = [&Client](int ChkUnitID) {
        return Client.GetSpriteAssets().ResolveImageID(ChkUnitID);
    };
    auto ResolveChkSubunitImageID = [&Client](int ChkUnitID) {
        return Client.GetSpriteAssets().ResolveSubunitImageID(ChkUnitID);
    };
    auto ResolveChkCollisionBounds = [&Client](int ChkUnitID) {
        Game::Components::UnitCollisionBounds Bounds;
        auto DataAssets = Client.GetDataAssets();
        if (!DataAssets.HasCache())
            return Bounds;

        const auto* DataLoader = DataAssets.GetDataLoader();
        const auto* UnitData = DataLoader ? DataLoader->FindUnitByID((uint32_t)ChkUnitID) : nullptr;
        if (!UnitData)
            return Bounds;

        Bounds.IsValid = UnitData->DimensionWidth() > 0 && UnitData->DimensionHeight() > 0;
        Bounds.Left = UnitData->DimensionLeft;
        Bounds.Up = UnitData->DimensionUp;
        Bounds.Right = UnitData->DimensionRight;
        Bounds.Down = UnitData->DimensionDown;
        return Bounds;
    };
    GameEntitySpawnRuntime::SpawnChkEntities(
        Session, ResolveChkImageID, ResolveChkSubunitImageID, ResolveChkCollisionBounds);

    auto SpawnResourceForStart = [&Session, &Client, &ResolveChkCollisionBounds](
        int ChkUnitId, int WorldX, int WorldY, int Amount)
    {
        auto ResolveImageID = [&Client](int ChkUnitID) {
            return Client.GetSpriteAssets().ResolveImageID(ChkUnitID);
        };
        GameEntitySpawnRuntime::SpawnResourceEntity(
            Session, ChkUnitId, WorldX, WorldY, Amount,
            ResolveImageID, ResolveChkCollisionBounds);
    };
    auto IsMineralStartUnit = [](int ChkUnitId) { return IsMineralUnit(ChkUnitId); };
    auto IsGeyserStartUnit = [](int ChkUnitId) { return IsGeyserUnit(ChkUnitId); };
    if (AddPlayableStarts)
    {
        for (int PlayerID = 0; PlayerID < 8; ++PlayerID)
            GameSessionBootstrap::EnsurePlayableStart(
                Session, PlayerID, SpawnResourceForStart, IsMineralStartUnit, IsGeyserStartUnit);
    }

    GameSessionBootstrap::RebuildPlayerSupplyAndRegisterAi(
        Session, RegisterGenericAi);

    Session.MarkEcsInitialized();
    SyncHudFromPlayerResources(Hud, Session.GetLocalPlayerID());

    Session.RebuildRegionsFromEcsMap();
    Session.StartDefaultReplayRecording((uint32_t)std::time(nullptr));
}

inline void InitializeLoadedMapGameplayRuntime(
    GameSession& Session,
    ClientRuntime& Client,
    GameAudioRuntime& Audio,
    UI::Hud& Hud,
    CampaignRuntime& Campaign,
    const std::string& MapPath,
    bool EnableCampaignRuntime,
    bool PreserveManualCamera = false)
{
    if (!Session.IsMapLoaded())
        return;

    InitializeEcsMapAndGameplaySystems(Session, Client, Audio);
    const bool IsCampaignMission =
        GameSessionBootstrap::Detail::IsCampaignMissionPath(MapPath);
    const char* ShowcaseValue = std::getenv("STARCRAFT_AI_SHOWCASE");
    const bool RunShowcase = ShowcaseValue && ShowcaseValue[0] != '\0' &&
                             ShowcaseValue[0] != '0';
    const bool IsObserver =
        GameSessionBootstrap::Detail::IsAiObserverMatch();
    const bool AddPlayableStarts = !IsCampaignMission || RunShowcase || IsObserver;
    const bool RegisterGenericAi = (!IsCampaignMission && !RunShowcase) || IsObserver;
    InitializeGameplayEntitiesAndRuntimeState(
        Session, Client, Hud, AddPlayableStarts, RegisterGenericAi);

    const int LocalPlayerID = Session.GetLocalPlayerID();
    GameSessionBootstrap::Detail::StartRace PlayerRace =
        GameSessionBootstrap::Detail::RaceFromScriptPlayer(Session, LocalPlayerID);
    PlayerRace = GameSessionBootstrap::Detail::ApplyShowcaseRaceOverride(PlayerRace);

    if (PlayerRace == GameSessionBootstrap::Detail::StartRace::Terran)
    {
        for (int Eid : Session.Entities().GetEntitiesWith<Game::Components::UnitComponent,
                                                          Game::Components::PlayerComponent>())
        {
            auto* P = Session.Entities().GetComponent<Game::Components::PlayerComponent>(Eid);
            auto* U = Session.Entities().GetComponent<Game::Components::UnitComponent>(Eid);
            if (P && U && P->PlayerID == LocalPlayerID)
            {
                if (U->UnitRace == Game::Components::Race::Protoss)
                {
                    PlayerRace = GameSessionBootstrap::Detail::StartRace::Protoss;
                    break;
                }
                else if (U->UnitRace == Game::Components::Race::Zerg)
                {
                    PlayerRace = GameSessionBootstrap::Detail::StartRace::Zerg;
                    break;
                }
            }
        }
    }

    std::string ConsoleRace = "terran";
    if (PlayerRace == GameSessionBootstrap::Detail::StartRace::Protoss)
        ConsoleRace = "protoss";
    else if (PlayerRace == GameSessionBootstrap::Detail::StartRace::Zerg)
        ConsoleRace = "zerg";

    Client.SetConsoleRace(ConsoleRace);
    Core::Utils::Logger::Info("GameBootstrap", "Selected console race: " + ConsoleRace);

    const bool EnableCampaign = EnableCampaignRuntime;
    if (EnableCampaign)
    {
        InitializeCampaignRuntimeForSession(
            Campaign, Session, Audio, Client, MapPath, PreserveManualCamera);
    }
    else
    {
        Campaign.Deactivate();
        Core::Utils::Logger::Info(
            "GameController",
            "Campaign runtime disabled for custom map: " + MapPath);
    }
}

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameMapBootstrap.h ===
#include <string>
#include <utility>

#include "Client/MapLoader.h"
#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"
#include "Main/AppRuntime.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop
{

template <typename TryLoadMapT>
bool ResolveAndLoadStartupMap(AppRuntime& App,
                              TryLoadMapT&& TryLoadMap)
{
    App.ResolveMapPath();
    if (App.MapPath.empty())
    {
        Core::Utils::Logger::Error("GameMain", "No default map found; startup map load aborted");
        return false;
    }

    if (!TryLoadMap())
    {
        Core::Utils::Logger::Error("GameMain", "Startup map load aborted");
        return false;
    }

    return true;
}

inline void InitializeClientWorldViewFromSession(ClientRuntime& Client, GameSession& Session)
{
    Core::Utils::Logger::Info(
        "GameBootstrap",
        "InitializeClientWorldViewFromSession viewport=" +
            std::to_string(Core::Platform::GetWindowWidth()) + "x" +
            std::to_string(Core::Platform::GetWindowHeight()));
    Client.SetLocalPlayerId(Session.GetLocalPlayerID());
    const auto Camera = Session.GetInitialCameraPosition();
    const std::string TilesetName = Session.GetTilesetNameOrDefault();
    Client.InitializeWorldView(
        Session.GetMapData(),
        TilesetName,
        Camera.first,
        Camera.second,
        Core::Platform::GetWindowWidth(),
        Core::Platform::GetWindowHeight());
    Session.MarkMapLoaded();
}

inline bool LoadRealMapIntoSession(AppRuntime& App, GameSession& Session, ClientRuntime& Client)
{
    Core::Utils::Logger::Info("GameBootstrap", "LoadRealMapIntoSession path=" + App.MapPath);
    Game::Client::MapLoader::Result Result = Game::Client::MapLoader::Load(App.MapPath);
    if (!Result.Loaded)
    {
        Core::Utils::Logger::Warning("GameMain", "Map load failed: " + Result.ErrorMessage);
        return false;
    }

    Session.ApplyLoadedMap(Result);

    if (!Result.RawChkData.empty())
    {
        if (Session.ParseLoadedScriptData(Result.RawChkData))
        {
            Core::Utils::Logger::Info("GameController",
                "Parsed real map CHK script: map=" + App.MapPath +
                " chk=" + Result.ChkPath +
                " strings=" + std::to_string(Session.GetScriptData().Strings.size()) +
                " locations=" + std::to_string(Session.GetScriptData().Locations.size()) +
                " triggers=" + std::to_string(Session.GetScriptData().Triggers.size()));
        }
        else
        {
            Core::Utils::Logger::Warning("GameController", "Failed to parse real map CHK script data");
        }
    }

    InitializeClientWorldViewFromSession(Client, Session);
    return true;
}

inline void ConfigureSyntheticSkirmishMapIntoSession(GameSession& Session, ClientRuntime& Client)
{
    Session.ConfigureSyntheticSkirmishMap();
    InitializeClientWorldViewFromSession(Client, Session);
}

} // namespace Game::Client::GameLoop

// === Consolidated from src/Main/GameResourceBootstrap.h ===
#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

#include "Client/ClientRuntime.h"
#include "Core/Utils/Logger.h"
// === Consolidated from src/Main/GameSystemAssetResolvers.h ===
#include <cstdint>
#include <functional>
#include <utility>

#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/NukeAndSpiderMineSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/SpellSystem.h"
#include "Game/Systems/SystemAssetBridge.h"
#include "Game/Systems/ZergSystem.h"

namespace Game::Client::GameLoop
{

inline void ConfigureModernAssetRegistryResolvers(const Resource::ModernAssetRegistry& Registry)
{
    Game::Systems::SystemAssetBridge::BindModernAssetRegistry(Registry);
}

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetImageResolver(Resolver);
    Game::Systems::CommandSystem::SetImageResolver(Resolver);
    Game::Systems::NukeAndSpiderMineSystem::SetImageResolver(Resolver);
    Game::Systems::SpellSystem::SetImageResolver(Resolver);
    Game::Systems::ZergSystem::SetImageResolver(std::move(Resolver));
}

inline void ConfigureBuildAndProductionImageResolvers(std::function<uint32_t(int)> Resolver,
                                                      std::function<uint32_t(int)> ConstructionResolver)
{
    ConfigureBuildAndProductionImageResolvers( std::move(Resolver) );
    Game::Systems::BuildingSystem::SetConstructionImageResolver(std::move(ConstructionResolver));
}

inline void ConfigureBuildAndProductionSubunitImageResolvers(
    std::function<uint32_t(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetSubunitImageResolver(Resolver);
    Game::Systems::BuildingSystem::SetSubunitImageResolver(Resolver);
    Game::Systems::CommandSystem::SetSubunitImageResolver(std::move(Resolver));
}

inline void ConfigureGameplayCollisionBoundsResolvers(
    std::function<Game::Components::UnitCollisionBounds(int)> Resolver)
{
    Game::Systems::ProductionSystem::SetCollisionBoundsResolver(Resolver);
    Game::Systems::BuildingSystem::SetCollisionBoundsResolver(Resolver);
    Game::Systems::ZergSystem::SetCollisionBoundsResolver(std::move(Resolver));
}

inline void ConfigureBuildingAddonOffsetResolver(
    Game::Systems::BuildingSystem::AddonOffsetResolver Resolver)
{
    Game::Systems::BuildingSystem::SetAddonOffsetResolver(std::move(Resolver));
}

inline void ConfigureAnimationOverlayOffsetResolver(
    Game::Systems::AnimationSystem::OverlayOffsetResolver Resolver)
{
    Game::Systems::AnimationSystem::SetOverlayOffsetResolver(std::move(Resolver));
}

inline void ConfigureAnimationWeaponImageResolver(
    Game::Systems::AnimationSystem::WeaponImageResolver Resolver)
{
    Game::Systems::AnimationSystem::SetWeaponImageResolver(std::move(Resolver));
}

inline void ConfigureAnimationSpriteImageResolver(
    Game::Systems::AnimationSystem::SpriteImageResolver Resolver)
{
    Game::Systems::AnimationSystem::SetSpriteImageResolver(std::move(Resolver));
}

inline void ConfigureAnimationImageAttachmentPersistenceResolver(
    Game::Systems::AnimationSystem::ImageAttachmentPersistenceResolver Resolver)
{
    Game::Systems::AnimationSystem::SetImageAttachmentPersistenceResolver(std::move(Resolver));
}

inline void ConfigureZergBurrowCapabilityResolver(
    Game::Systems::ZergSystem::BurrowCapabilityResolver Resolver)
{
    Game::Systems::ZergSystem::SetBurrowCapabilityResolver(std::move(Resolver));
}

inline void ConfigureBulletVisualResolver(
    Game::Systems::BulletSystem::VisualResolver Resolver)
{
    Game::Systems::BulletSystem::SetVisualResolver(std::move(Resolver));
}

} // namespace Game::Client::GameLoop

#include "Resource/GameDataLoader.h"

namespace Game::Client::GameLoop
{

inline bool InitializeClientResourcesAndSystemAssetResolvers(Game::Client::ClientRuntime& Client,
                                                            const std::string& InstallPath)
{
    if (!Client.InitializeResources(InstallPath))
    {
        Core::Utils::Logger::Error("GameMain", "ResourceCache init failed");
        return false;
    }

    const std::filesystem::path AssetRoot =
        std::filesystem::path(InstallPath) / "assets";
    if (!Game::Systems::AnimationSystem::InitializeIScriptAssets(
            (AssetRoot / "scripts" / "iscript.bin").string(),
            (AssetRoot / "arr" / "images.dat").string()))
    {
        Core::Utils::Logger::Error(
            "GameMain", "Project iscript assets failed to initialize from: " +
                AssetRoot.string());
        return false;
    }

    ConfigureBuildAndProductionImageResolvers(
        [&Client](int ChkUnitID) { return Client.GetSpriteAssets().ResolveImageID(ChkUnitID); },
        [&Client](int ChkUnitID) {
            auto DataAssets = Client.GetDataAssets();
            const auto* DataLoader = DataAssets.GetDataLoader();
            if (!DataLoader || ChkUnitID < 0)
                return std::numeric_limits<uint32_t>::max();
            return DataLoader->ResolveUnitConstructionImageID((uint32_t)ChkUnitID);
        });
    ConfigureBuildAndProductionSubunitImageResolvers(
        [&Client](int ChkUnitID) {
            return Client.GetSpriteAssets().ResolveSubunitImageID(ChkUnitID);
        });
    ConfigureGameplayCollisionBoundsResolvers(
        [&Client](int ChkUnitID) {
            Game::Components::UnitCollisionBounds Bounds;
            auto DataAssets = Client.GetDataAssets();
            if (!DataAssets.HasCache())
                return Bounds;

            const auto* DataLoader = DataAssets.GetDataLoader();
            const auto* UnitData = DataLoader ? DataLoader->FindUnitByID((uint32_t)ChkUnitID) : nullptr;
            if (!UnitData)
                return Bounds;

            Bounds.IsValid = UnitData->DimensionWidth() > 0 && UnitData->DimensionHeight() > 0;
            Bounds.Left = UnitData->DimensionLeft;
            Bounds.Up = UnitData->DimensionUp;
            Bounds.Right = UnitData->DimensionRight;
            Bounds.Down = UnitData->DimensionDown;
            return Bounds;
        });
    ConfigureBuildingAddonOffsetResolver(
        [&Client](int ParentDefID, int AddonDefID) {
            if (ParentDefID < 0 || AddonDefID < 0)
                return Core::Math::Vec2{96.0f, 16.0f};
            const auto ParentDef = Game::Races::GetUnitDefByID(ParentDefID);
            const auto AddonDef = Game::Races::GetUnitDefByID(AddonDefID);
            const auto* DataLoader = Client.GetDataAssets().GetDataLoader();
            const auto* Parent = DataLoader && ParentDef.ChkUnitID >= 0
                ? DataLoader->FindUnitByID((uint32_t)ParentDef.ChkUnitID)
                : nullptr;
            const auto* Addon = DataLoader && AddonDef.ChkUnitID >= 0
                ? DataLoader->FindUnitByID((uint32_t)AddonDef.ChkUnitID)
                : nullptr;
            if (!Parent || !Addon || Parent->PlacementWidth <= 0 ||
                Parent->PlacementHeight <= 0 || Addon->PlacementWidth <= 0 ||
                Addon->PlacementHeight <= 0)
            {
                return Core::Math::Vec2{96.0f, 16.0f};
            }
            return Core::Math::Vec2{
                (float)(-Parent->PlacementWidth / 2 + Addon->AddonPositionX +
                        Addon->PlacementWidth / 2),
                (float)(-Parent->PlacementHeight / 2 + Addon->AddonPositionY +
                        Addon->PlacementHeight / 2)};
        });
    ConfigureZergBurrowCapabilityResolver(
        [](int, uint32_t ImageID) {
            return ImageID != std::numeric_limits<uint32_t>::max() &&
                   Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                       (int)ImageID, Game::IScriptAnims::Anim_Burrow) &&
                   Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                       (int)ImageID, Game::IScriptAnims::Anim_UnBurrow);
        });
    auto OverlayTables = std::make_shared<std::unordered_map<std::string, Resource::ImageOffsetTable>>();
    ConfigureAnimationOverlayOffsetResolver(
        [&Client, OverlayTables](uint32_t ImageID, size_t LoType, size_t Frame,
                                 size_t OffsetIndex, bool Flipped)
            -> std::optional<Core::Math::Vec2> {
            auto DataAssets = Client.GetDataAssets();
            const auto* DataLoader = DataAssets.GetDataLoader();
            if (!DataLoader)
                return std::nullopt;

            const std::string Path = DataLoader->ResolveImageOverlayPath(ImageID, LoType);
            if (Path.empty())
                return std::nullopt;

            auto It = OverlayTables->find(Path);
            if (It == OverlayTables->end())
            {
                auto Data = Client.GetArchiveSource().ReadWithFallback("unit/" + Path);
                if (!Data)
                    return std::nullopt;
                auto Table = Resource::DatDecoder::DecodeImageOffsets(*Data);
                if (!Table)
                    return std::nullopt;
                It = OverlayTables->emplace(Path, std::move(*Table)).first;
            }

            if (Frame >= It->second.Frames.size() ||
                OffsetIndex >= It->second.Frames[Frame].size())
            {
                return std::nullopt;
            }

            const auto& Offset = It->second.Frames[Frame][OffsetIndex];
            if (Offset.X == 127 && Offset.Y == 127)
                return std::nullopt;
            const float X = (float)(Flipped ? -Offset.X : Offset.X);
            return Core::Math::Vec2{X, (float)Offset.Y};
        });
    ConfigureAnimationWeaponImageResolver(
        [&Client](int WeaponID) -> std::optional<uint32_t> {
            const auto* Loader = Client.GetDataAssets().GetDataLoader();
            if (!Loader || WeaponID < 0)
                return std::nullopt;
            const auto* Weapon = Loader->FindWeaponByID((uint32_t)WeaponID);
            if (!Weapon || Weapon->FlingyID >= 209)
                return std::nullopt;
            const auto* Flingy = Loader->FindFlingyByID(Weapon->FlingyID);
            const auto* Sprite = Flingy ? Loader->FindSpriteByID(Flingy->SpriteID) : nullptr;
            if (!Sprite || Loader->ResolveImageGrpPath(Sprite->ImageID).empty())
                return std::nullopt;
            return Sprite->ImageID;
        });
    ConfigureAnimationSpriteImageResolver(
        [&Client](int SpriteID) -> std::optional<uint32_t> {
            const auto* Loader = Client.GetDataAssets().GetDataLoader();
            if (!Loader || SpriteID < 0)
                return std::nullopt;
            const auto* Sprite = Loader->FindSpriteByID((uint32_t)SpriteID);
            if (!Sprite ||
                Loader->ResolveImageGrpPath(Sprite->ImageID).empty())
            {
                return std::nullopt;
            }
            return Sprite->ImageID;
        });
    ConfigureAnimationImageAttachmentPersistenceResolver(
        [&Client](uint32_t ImageID) {
            if (ImageID == 135u || ImageID == 136u ||
                ImageID == 926u || ImageID == 927u || ImageID == 928u)
            {
                return true;
            }
            const auto* DataLoader = Client.GetDataAssets().GetDataLoader();
            const auto* Relation = DataLoader
                ? DataLoader->FindImageRelationByID(ImageID)
                : nullptr;
            return Relation && (Relation->IsShadow() || Relation->IsGlowOverlay());
        });
    ConfigureBulletVisualResolver(
        [&Client](int ChkUnitID, bool TargetIsAir)
            -> std::optional<Game::Systems::BulletSystem::VisualSpec> {
            const auto* Loader = Client.GetDataAssets().GetDataLoader();
            if (!Loader || ChkUnitID < 0)
                return std::nullopt;
            const auto* Unit = Loader->FindUnitByID((uint32_t)ChkUnitID);
            if (!Unit)
                return std::nullopt;

            constexpr int CarrierChkUnitID = 72;
            constexpr int InterceptorChkUnitID = 73;
            constexpr int ReaverChkUnitID = 83;
            constexpr int ScarabChkUnitID = 85;

            // Reavers do not own a weapons.dat projectile. The launched
            // Scarab is a real unit whose Death IScript creates the authored
            // explosion, so use that unit image as the visible lob payload.
            if (ChkUnitID == ReaverChkUnitID)
            {
                const auto* Scarab =
                    Loader->FindUnitByID(ScarabChkUnitID);
                const auto* ScarabFlingy = Scarab
                    ? Loader->FindFlingyByID(Scarab->FlingyID)
                    : nullptr;
                const auto* ScarabSprite = ScarabFlingy
                    ? Loader->FindSpriteByID(ScarabFlingy->SpriteID)
                    : nullptr;
                if (!ScarabSprite ||
                    Loader->ResolveImageGrpPath(
                        ScarabSprite->ImageID).empty())
                {
                    return std::nullopt;
                }

                Game::Systems::BulletSystem::VisualSpec Spec;
                Spec.ImageID = ScarabSprite->ImageID;
                Spec.Speed =
                    (float)ScarabFlingy->TopSpeed * 24.0f / 256.0f;
                Spec.HasInitAnimation =
                    Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                        (int)ScarabSprite->ImageID,
                        Game::IScriptAnims::Anim_Init);
                Spec.HasImpactAnimation =
                    Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                        (int)ScarabSprite->ImageID,
                        Game::IScriptAnims::Anim_Death);
                Spec.BulletType = 1;
                Spec.LifetimeSeconds = 10.0f;
                Spec.ProjectileCount = 1;
                return Spec;
            }

            // Tank/Goliath-style units store their real weapons on the
            // units.dat subunit (turret) while the body advertises None=130.
            // Follow the same indirection used by OpenBW before resolving the
            // weapon flingy and its projectile metadata.
            const Resource::UnitEntry* WeaponOwner = Unit;
            if (ChkUnitID == CarrierChkUnitID)
            {
                WeaponOwner =
                    Loader->FindUnitByID(InterceptorChkUnitID);
                if (!WeaponOwner)
                    return std::nullopt;
            }
            uint32_t WeaponID =
                TargetIsAir
                    ? WeaponOwner->AirWeapon
                    : WeaponOwner->GroundWeapon;
            if (ChkUnitID != CarrierChkUnitID &&
                WeaponID >= 130 && Unit->Subunit1ID < 228)
            {
                if (const auto* Subunit =
                        Loader->FindUnitByID((uint32_t)Unit->Subunit1ID))
                {
                    WeaponOwner = Subunit;
                    WeaponID = TargetIsAir
                        ? Subunit->AirWeapon
                        : Subunit->GroundWeapon;
                }
            }
            const auto* Weapon = Loader->FindWeaponByID(WeaponID);
            // Flingy 0 is classic StarCraft's sentinel for melee / projectile-less attacks
            // (e.g. Zealot Psi Blades, Zergling Claws, Ultralisk Kaiser Blades).
            // OpenBW fire_weapon explicitly guards with `if (!weapon_type->flingy) return`.
            // Resolving Flingy 0 would incorrectly map to Scourge avenger.grp.
            if (!Weapon || Weapon->FlingyID == 0 || Weapon->FlingyID >= 209)
                return std::nullopt;
            const auto* Flingy = Loader->FindFlingyByID(Weapon->FlingyID);
            if (!Flingy)
                return std::nullopt;
            const auto* Sprite = Loader->FindSpriteByID(Flingy->SpriteID);
            if (!Sprite)
                return std::nullopt;
            const std::string GrpPath = Loader->ResolveImageGrpPath(Sprite->ImageID);
            if (GrpPath.empty())
                return std::nullopt;

            std::string LowerGrpPath = GrpPath;
            std::transform(LowerGrpPath.begin(), LowerGrpPath.end(), LowerGrpPath.begin(),
                [](unsigned char C) { return (char)std::tolower(C); });
            // circle14.grp is classic StarCraft's invisible projectile / collision
            // delivery helper (e.g. Firebat Flame Thrower, Psi Storm delivery).
            // It carries real weapon simulation parameters but must never render.
            const bool IsInvisibleBullet =
                LowerGrpPath.find("circle14") != std::string::npos;

            Game::Systems::BulletSystem::VisualSpec Spec;
            Spec.ImageID = IsInvisibleBullet ? UINT32_MAX : Sprite->ImageID;
            Spec.Speed = (float)Flingy->TopSpeed * 24.0f / 256.0f;
            Spec.HasInitAnimation = !IsInvisibleBullet &&
                Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                    (int)Sprite->ImageID, Game::IScriptAnims::Anim_Init);
            Spec.HasImpactAnimation = !IsInvisibleBullet &&
                Game::Systems::AnimationSystem::HasIScriptAnimationForImage(
                    (int)Sprite->ImageID, Game::IScriptAnims::Anim_Death);
            Spec.BulletType = (int)Weapon->WeaponBehavior;
            Spec.LifetimeSeconds = (float)Weapon->Lifetime / 24.0f;
            Spec.HeadingOffsetRadians =
                (float)Weapon->BulletHeadingOffset *
                (6.28318530718f / 256.0f);
            Spec.ForwardOffset = (float)Weapon->ForwardOffset;
            Spec.UpwardOffset = (float)Weapon->UpwardOffset;
            const int AttackEvents = std::max(
                1, (int)(TargetIsAir
                    ? WeaponOwner->MaxAirHits
                    : WeaponOwner->MaxGroundHits));
            Spec.ProjectileCount = AttackEvents *
                std::max(1, (int)Weapon->BulletCount);
            Spec.AppliesAcidSpores = Weapon->SplashType == 18;
            if (ChkUnitID == CarrierChkUnitID)
            {
                // PickWeaponHitCount clamps this expansion to the number of
                // loaded fighters, producing one visible pulse per fighter.
                Spec.ProjectileCount = 8;
                const auto* InterceptorFlingy =
                    Loader->FindFlingyByID(WeaponOwner->FlingyID);
                const auto* InterceptorSprite = InterceptorFlingy
                    ? Loader->FindSpriteByID(
                          InterceptorFlingy->SpriteID)
                    : nullptr;
                if (InterceptorSprite &&
                    !Loader->ResolveImageGrpPath(
                        InterceptorSprite->ImageID).empty())
                {
                    Spec.FlightProxyImageID =
                        InterceptorSprite->ImageID;
                }
            }
            return Spec;
        });
    return true;
}

} // namespace Game::Client::GameLoop

#include "Main/GameRuntimeFrame.h"
// === Consolidated from src/Main/GameSessionOperations.h ===
#include <cstdint>
#include <string>

#include "Main/AppRuntime.h"
#include "Main/GameMenuPresentation.h"


#include "Main/GameSession.h"
#include "UI/Hud.h"

namespace Game::Client::GameLoop::GameSessionOperations
{

inline bool HostGame(GameSession& Session,
                     const std::string& BindIP,
                     int Port,
                     int MaxClients)
{
    return HostSessionNetworkGame(Session, BindIP, Port, MaxClients);
}

inline bool JoinGame(GameSession& Session,
                     const std::string& HostIP,
                     int Port)
{
    return JoinSessionNetworkGame(Session, HostIP, Port);
}

inline void StartNetworkGame(GameSession& Session)
{
    StartSessionNetworkGame(Session);
}

inline void RecordGameAction(GameSession& Session,
                             uint64_t FrameCount,
                             uint8_t PlayerID,
                             uint8_t ActionType,
                             uint16_t P1,
                             uint16_t P2,
                             uint16_t P3,
                             uint8_t P4)
{
    RecordSessionLocalFrameAction(Session, FrameCount, PlayerID, ActionType, P1, P2, P3, P4);
}

inline bool SaveGame(const std::string& FilePath,
                     const AppRuntime& App,
                     const GameMenuRuntime& Menu,
                     const UI::Hud& Hud)
{
    return SaveCurrentGameToFile(FilePath, App, Menu, Hud);
}

inline bool SaveGame(const std::string& FilePath,
                     const AppRuntime& App,
                     const GameMenuRuntime& Menu,
                     const UI::Hud& Hud,
                     const GameSession& Session)
{
    return SaveCurrentGameToFile(FilePath, App, Menu, Hud, Session);
}

inline bool LoadGame(const std::string& FilePath,
                     const AppRuntime& App,
                     GameMenuRuntime& Menu,
                     UI::Hud& Hud)
{
    return LoadCurrentGameFromFile(FilePath, App, Menu, Hud);
}

inline bool LoadGame(const std::string& FilePath,
                     const AppRuntime& App,
                     GameMenuRuntime& Menu,
                     UI::Hud& Hud,
                     GameSession& Session)
{
    return LoadCurrentGameFromFile(FilePath, App, Menu, Hud, Session);
}

inline bool SaveReplay(GameSession& Session, const std::string& FilePath)
{
    return SaveSessionReplayToFile(Session, FilePath);
}

inline bool LoadReplay(GameSession& Session, const std::string& FilePath)
{
    return LoadSessionReplayFromFile(Session, FilePath);
}

} // namespace Game::Client::GameLoop::GameSessionOperations

#include "UI/HudLayout.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace Game::Client::GameLoop
{

namespace
{
    bool InitializeClientShellRuntime()
    {
        Core::Platform::Initialize();
        if (!Render::Initialize())
        {
            std::cerr << "Renderer init failed" << std::endl;
            return false;
        }
        if (!Render::g_Draw2D.Init())
        {
            std::cerr << "Draw2D init failed" << std::endl;
            return false;
        }
        return true;
    }

    void ShutdownClientShellRuntime(ClientRuntime& Client, UI::Hud& Hud)
    {
        Hud.Shutdown();
        Render::g_Draw2D.Shutdown();
        Client.ShutdownResources();
        Render::Shutdown();
        Core::Platform::Shutdown();
    }

    template <typename QuitT, typename SetMapPathT, typename StartGameT>
    void HandleGlueMenuAction(
        const std::string& Action,
        CampaignRuntime& Campaign,
        QuitT&& Quit,
        SetMapPathT&& SetMapPath,
        StartGameT&& StartGame)
    {
        if (Action == "exit")
        {
            Quit();
        }
        else if (Game::Campaign::IsCampaignMissionAction(Action))
        {
            const Game::Campaign::CampaignMissionRef Mission = Game::Campaign::ResolveCampaignMission(Action);
            if (Mission.Valid)
            {
                Campaign.SetProgressTarget(Mission.Expansion, Mission.Race, Mission.Mission);
                SetMapPath(Mission.Path);
                StartGame();
            }
        }
        else if (Action == "start_original" || Action == "start_expansion" ||
                 Action == "start_custom_game")
        {
            std::cout << "StarCraft: menu action '" << Action
                      << "' has no campaign map path; using default map resolver" << std::endl;
            Campaign.ClearProgressTarget();
            StartGame();
        }
    }

    template <typename InitGameSystemsT>
    void StartGameFromMainMenu(AppRuntime& App,
                               GameMenuRuntime& Menu,
                               CampaignRuntime& Campaign,
                               GameSession& Session,
                               InitGameSystemsT&& InitGameSystems)
    {
        if (App.State != GameState::MainMenu)
            return;

        std::cout << "StarCraft: Starting game..." << std::endl;
        Menu.CloseAndResume();
        Campaign.SetScenarioPaused(false);

        InitGameSystems();
        if (!Session.IsEcsInitialized())
        {
            std::cerr << "Game init failed!" << std::endl;
            return;
        }

        App.State = GameState::InGame;
        Render::ResetStateCache();
        std::cout << "StarCraft: Game started" << std::endl;
    }

    template <typename InitializeEcsT>
    void InitializeGameplayRuntime(AppRuntime& App,
                                   UI::Hud& Hud,
                                   Game::Client::ClientRuntime& Client,
                                   int LocalPlayerID,
                                   InitializeEcsT&& InitializeEcs)
    {
        Hud.Init(Client.GetUIAssets(), Client.GetSpriteAssets());
        ResetPlayerResourceState();
        InitializeEcs();
        SyncHudFromPlayerResources(Hud, LocalPlayerID);
        App.Initialized = true;
    }

    bool ShouldDevStartInGame(const AppRuntime& App, bool DirectStartEnabled)
    {
        if (DirectStartEnabled)
            return true;
        if (!App.MapPathOverride.empty())
            return true;
        const char* DevStart = std::getenv("STARCRAFT_DEV_START_INGAME");
        return DevStart && std::string(DevStart) == "1";
    }

    void LogControllerViewportState(const char* Stage)
    {
        if (!Core::Utils::InputDebugEnabled())
            return;

        std::ostringstream Stream;
        Stream << "stage=" << Stage
               << " logical=" << Core::Platform::GetWindowWidth() << "x" << Core::Platform::GetWindowHeight()
               << " client=" << Core::Platform::GetClientWidth() << "x" << Core::Platform::GetClientHeight()
               << " framebuffer=" << Core::Platform::GetFramebufferWidth() << "x" << Core::Platform::GetFramebufferHeight();
        Core::Utils::InputDebugLog("InputControllerState", Stream.str());
    }

    void LogMouseDebugBootstrapSnapshot(GameSession& Session, ClientRuntime& Client)
    {
        if (!Core::Utils::InputDebugEnabled() || !Session.IsEcsInitialized())
            return;

        const auto Camera = Client.GetCameraPosition();
        Core::Utils::Logger::Info(
            "InputBootstrap",
            "viewport=" + std::to_string(Client.GetViewportWidth()) + "x" +
                std::to_string(Client.GetViewportHeight()) +
                " camera=" + std::to_string((int)Camera.X) + "," +
                std::to_string((int)Camera.Y));

        const auto Layout = UI::ResolveHudLayout(Client.GetViewportWidth(), Client.GetViewportHeight());
        Core::Utils::Logger::Info(
            "InputBootstrap",
            "minimap_rect=" + std::to_string(Layout.MinimapRect.X) + "," +
                std::to_string(Layout.MinimapRect.Y) + "," +
                std::to_string(Layout.MinimapRect.W) + "," +
                std::to_string(Layout.MinimapRect.H));

        auto& EM = Session.Entities();
        const int LocalPlayerID = Session.GetLocalPlayerID();
        int Logged = 0;
        for (int EntityID : EM.GetEntitiesWithCached<Game::Components::TransformComponent,
                                                     Game::Components::UnitComponent,
                                                     Game::Components::PlayerComponent>())
        {
            auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            if (!Player || !Unit || !Transform || Player->PlayerID != LocalPlayerID)
                continue;

            const auto Screen = Client.WorldToScreen(Transform->Position.X, Transform->Position.Y);
            const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
            const auto Def = DefID >= 0 ? Game::Races::GetUnitDefByID(DefID) : Game::Races::UnitDef();
            const std::string Name = Def.Name.empty() ? ("CHK_" + std::to_string(Unit->ChkUnitID)) : Def.Name;
            Core::Utils::Logger::Info(
                "InputBootstrap",
                "unit id=" + std::to_string(EntityID) +
                    " name=" + Name +
                    " world=" + std::to_string((int)Transform->Position.X) + "," +
                    std::to_string((int)Transform->Position.Y) +
                    " screen=" + std::to_string((int)Screen.X) + "," +
                    std::to_string((int)Screen.Y));
            if (++Logged >= 12)
                break;
        }
    }
} // namespace

// ============================================================================
// Lifecycle & Frame Dispatch
// ============================================================================

bool GameController::Initialize(int Width, int Height)
{
    (void)Width;
    (void)Height;
    std::cout << "StarCraft: init" << std::endl;
    std::cout << "GameController.Initialize this=" << (uintptr_t)this
              << " runtime=" << (uintptr_t)&mRuntime
              << " client_runtime=" << (uintptr_t)&mRuntime.Client.Runtime
              << std::endl;

    if (!InitializeClientShellRuntime())
        return false;

    LogControllerViewportState("post_shell_init");

    mRuntime.Client.Runtime.InitializeWorldEffects(800);
    mRuntime.App.Audio.Initialize();

    LogControllerViewportState("pre_gui_init");
    InitializeGuiRuntime(
        mRuntime.App.Gui,
        mRuntime.App.Runtime.AssetRoot,
        Core::Platform::GetWindowWidth(),
        Core::Platform::GetWindowHeight(),
        [this](const std::string& Action) {
            HandleGlueMenuAction(
                Action,
                mRuntime.Session.Campaign,
                []() { Core::Platform::RequestQuit(); },
                [this](const std::string& MapPath) { SetMapPathOverride(MapPath); },
                [this]() { StartGame(); });
        },
        [this](const std::string& AudioPath) {
            mRuntime.App.Audio.PlayFile(AudioPath, 0.0f, 0.0f, 35, Core::Audio::SoundFlags::PerSound);
        },
        !mDirectStartEnabled);
#if ENABLE_GUI_UI
    if (mInitialGuiHudEnabled && mRuntime.App.Gui.MainGui.IsActive())
        mRuntime.App.Gui.UseGui = true;
#endif

    LogControllerViewportState("post_gui_init");

    mRuntime.App.Runtime.Initialized = true;
    mRuntime.App.Runtime.State = GameState::MainMenu;
    if (ShouldDevStartInGame(mRuntime.App.Runtime, mDirectStartEnabled))
    {
        LogControllerViewportState("pre_start_game");
        if (mDirectStartEnabled)
            std::cout << "StarCraft: direct test map startup entering game" << std::endl;
        StartGame();
    }
    return true;
}

void GameController::Update(float Dt, int Width, int Height)
{
    (void)Width;
    (void)Height;
    UpdateGameRuntimeFrame(
        mRuntime,
        Dt,
        [this](const std::string& Path) { return SaveGame(Path); },
        [this](const std::string& Path) { return LoadGame(Path); },
        [this]() { UpdateGuiPresentationLayer(mRuntime.App.Gui, mRuntime.Client.Hud, mRuntime.Session.Game, mRuntime.Client.Runtime); },
        [this](uint8_t PlayerID, uint8_t ActionType, uint16_t P1, uint16_t P2) {
            RecordGameAction(PlayerID, ActionType, P1, P2);
        },
        [this]() { RestartGame(); },
        [this]() { ReturnToMainMenu(); });
}

void GameController::Render(int Width, int Height)
{
    (void)Width;
    (void)Height;
    RenderGameRuntimeFrame(mRuntime);
}

void GameController::OnEvent(const void* Event)
{
    HandleGameRuntimeFrameEvent(mRuntime, Event);
}

void GameController::Shutdown()
{
    std::cout << "StarCraft: cleanup" << std::endl;
    ShutdownGuiRuntime(mRuntime.App.Gui);
    ShutdownClientShellRuntime(mRuntime.Client.Runtime, mRuntime.Client.Hud);
}

// ============================================================================
// Startup & Session Transitions
// ============================================================================

void GameController::StartGame()
{
    StartGameFromMainMenu(
        mRuntime.App.Runtime,
        mRuntime.App.Menu,
        mRuntime.Session.Campaign,
        mRuntime.Session.Game,
        [this]() { InitGameSystems(); });
}

void GameController::RestartGame()
{
    mRuntime.App.Runtime.State = GameState::MainMenu;
    mRuntime.Client.Runtime.ResetSessionState();
    mRuntime.Session.Game.ResetMatchOutcome();
    if (mRuntime.Session.Campaign.IsActive())
        mRuntime.Session.Campaign.Controller().Reset();
    StartGame();
}

void GameController::ReturnToMainMenu()
{
    mRuntime.App.Runtime.State = GameState::MainMenu;
    mRuntime.Client.Runtime.ResetSessionState();
    mRuntime.Session.Game.ResetMatchOutcome();
    mRuntime.Session.Campaign.Deactivate();
    mRuntime.App.Menu.CloseAndResume();
}

void GameController::InitGameSystems()
{
    Core::Utils::Logger::Info("GameBootstrap", "InitGameSystems begin");
    if (!mRuntime.App.Runtime.DetectGameInstall())
        return;

    if (!InitializeClientResourcesAndSystemAssetResolvers(mRuntime.Client.Runtime, mRuntime.App.Runtime.InstallPath))
        return;
    mRuntime.App.Audio.ConfigureGameSounds(mRuntime.Client.Runtime);

    if (!ResolveAndLoadStartupMap(
        mRuntime.App.Runtime,
        [this]() { return LoadRealMapIntoSession(mRuntime.App.Runtime, mRuntime.Session.Game, mRuntime.Client.Runtime); }))
        return;

    InitializeGameplayRuntime(
        mRuntime.App.Runtime,
        mRuntime.Client.Hud,
        mRuntime.Client.Runtime,
        mRuntime.Session.Game.GetLocalPlayerID(),
        [this]() {
            InitializeLoadedMapGameplayRuntime(
                mRuntime.Session.Game,
                mRuntime.Client.Runtime,
                mRuntime.App.Audio,
                mRuntime.Client.Hud,
                mRuntime.Session.Campaign,
                mRuntime.App.Runtime.MapPath,
                mRuntime.App.Runtime.CampaignRuntimeEnabled,
                mHasInitialCameraOverride);
        });
    LogMouseDebugBootstrapSnapshot(mRuntime.Session.Game, mRuntime.Client.Runtime);

    if (mHasInitialCameraOverride)
        mRuntime.Client.Runtime.CenterCamera(mInitialCameraX, mInitialCameraY);
}

// ============================================================================
// Configuration & Session Operations API
// ============================================================================

void GameController::SetMapPathOverride(std::string MapPath)
{
    mRuntime.App.Runtime.SetMapPathOverride(std::move(MapPath));
}

void GameController::SetCampaignRuntimeEnabled(bool Enabled)
{
    mRuntime.App.Runtime.SetCampaignRuntimeEnabled(Enabled);
}

void GameController::SetDirectStartEnabled(bool Enabled)
{
    mDirectStartEnabled = Enabled;
}

void GameController::SetInteractionDemoEnabled(bool Enabled)
{
    mRuntime.InteractionDemo.Enabled = Enabled;
}

void GameController::SetInitialCameraOverride(float WorldX, float WorldY)
{
    mHasInitialCameraOverride = true;
    mInitialCameraX = WorldX;
    mInitialCameraY = WorldY;
}

void GameController::SetInitialGuiHudEnabled(bool Enabled)
{
    mInitialGuiHudEnabled = Enabled;
}

bool GameController::HostGame(const std::string& BindIP, int Port, int MaxClients)
{
    return GameSessionOperations::HostGame(mRuntime.Session.Game, BindIP, Port, MaxClients);
}

bool GameController::JoinGame(const std::string& HostIP, int Port)
{
    return GameSessionOperations::JoinGame(mRuntime.Session.Game, HostIP, Port);
}

void GameController::StartNetworkGame()
{
    GameSessionOperations::StartNetworkGame(mRuntime.Session.Game);
}

void GameController::RecordGameAction(uint8_t PlayerID, uint8_t ActionType,
                                       uint16_t P1, uint16_t P2,
                                       uint16_t P3, uint8_t P4)
{
    GameSessionOperations::RecordGameAction(
        mRuntime.Session.Game,
        mRuntime.App.Runtime.FrameCount,
        PlayerID,
        ActionType,
        P1,
        P2,
        P3,
        P4);
}

bool GameController::SaveGame(const std::string& FilePath)
{
    return GameSessionOperations::SaveGame(
        FilePath,
        mRuntime.App.Runtime,
        mRuntime.App.Menu,
        mRuntime.Client.Hud,
        mRuntime.Session.Game);
}

bool GameController::LoadGame(const std::string& FilePath)
{
    return GameSessionOperations::LoadGame(
        FilePath,
        mRuntime.App.Runtime,
        mRuntime.App.Menu,
        mRuntime.Client.Hud,
        mRuntime.Session.Game);
}

bool GameController::SaveReplay(const std::string& FilePath)
{
    return GameSessionOperations::SaveReplay(mRuntime.Session.Game, FilePath);
}

bool GameController::LoadReplay(const std::string& FilePath)
{
    return GameSessionOperations::LoadReplay(mRuntime.Session.Game, FilePath);
}

} // namespace Game::Client::GameLoop


// === Consolidated from src/Main/GameGuiPresentation.cpp ===
#include "Main/GameGuiPresentation.h"

#include "Client/ClientRuntime.h"
#include "Core/Platform/Platform.h"
#include "Main/GameRuntimeFrame.h"
#include "Main/AppRuntime.h"
#include "Main/GameSession.h"
#include "Render/Draw2D.h"
#include "UI/Hud.h"
#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"

namespace Game::Client::GameLoop
{

namespace
{
void EnsureGuiPresentationSize(GuiRuntime& Gui, GameState State)
{
#if ENABLE_GUI_UI
    const int Width = Core::Platform::GetWindowWidth();
    const int Height = Core::Platform::GetWindowHeight();
    if (Width <= 0 || Height <= 0)
        return;

    if (Gui.AppliedWidth == Width && Gui.AppliedHeight == Height)
        return;

    Gui.AppliedWidth = Width;
    Gui.AppliedHeight = Height;
    if (State == GameState::MainMenu)
    {
        if (Gui.GlueGuiInitialized)
            Gui.GlueGui.Resize(Width, Height);
    }
    else if (Gui.MainGui.IsActive())
    {
        Gui.MainGui.Resize(Width, Height);
    }
#else
    (void)Gui;
    (void)State;
#endif
}
}

void UpdateGlueMenuPresentationLayer(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    EnsureGuiPresentationSize(Gui, GameState::MainMenu);
    if (Gui.GlueGuiInitialized)
        Gui.GlueGui.Update(Core::Platform::GetAbsoluteTime());
#else
    (void)Gui;
#endif
}

bool UpdateGuiPresentationForState(GuiRuntime& Gui, GameState State)
{
    if (State == GameState::MainMenu)
    {
        UpdateGlueMenuPresentationLayer(Gui);
        return true;
    }

    return false;
}

void RenderGlueMenuPresentationLayer(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    if (Gui.GlueGuiInitialized)
    {
        Gui.GlueGui.RenderOffscreen();
        sg_reset_state_cache();
    }
#endif

    Render::BeginFrame();

#if ENABLE_GUI_UI
    if (Gui.GlueGuiInitialized)
    {
        Gui.GlueGui.Render();
        sg_reset_state_cache();
        Render::EndFrame();
        return;
    }
#else
    (void)Gui;
#endif

    Render::EndFrame();
}

bool HandleGlueMenuPresentationEvent(GuiRuntime& Gui, const void* Event)
{
#if ENABLE_GUI_UI
    if (Gui.GlueGuiInitialized && Gui.GlueGui.HandleEvent(Event))
    {
        sapp_consume_event();
        return true;
    }
#else
    (void)Gui;
    (void)Event;
#endif
    return false;
}

void ResizeGuiPresentationForState(GuiRuntime& Gui, GameState State)
{
    EnsureGuiPresentationSize(Gui, State);
}

void HandleGuiPresentationEventForState(GuiRuntime& Gui, GameState State, const void* Event)
{
    const sapp_event* E = static_cast<const sapp_event*>(Event);
    if (E && E->type == SAPP_EVENTTYPE_RESIZED)
        ResizeGuiPresentationForState(Gui, State);

    if (State == GameState::MainMenu)
    {
        HandleGlueMenuPresentationEvent(Gui, Event);
        return;
    }

    HandleGuiHudPresentationEvent(Gui, Event);
}

void UpdateGuiPresentationLayer(
    GuiRuntime& Gui,
    UI::Hud& Hud,
    GameSession& Session,
    ClientRuntime& Client)
{
#if ENABLE_GUI_UI
    EnsureGuiPresentationSize(Gui, GameState::InGame);
    if (Gui.UseGui && Gui.MainGui.IsActive())
    {
        SyncCampaignPingsToHud(Hud, Client, Session.GetMapData().Width, Session.GetMapData().Height);
        SyncMinimapDotsToHud(
            Hud, Session.Entities(), Session.GetLocalPlayerID(), Session.GetFogSystem());
        const auto HudResources = CaptureHudResourceState(Hud);
        Gui.MainGui.SetResources(
            HudResources.MineralCount,
            HudResources.GasCount,
            HudResources.SupplyUsed,
            HudResources.SupplyMax);
        Gui.MainGui.Update(
             Session.Entities(),
             Client.GetSelectedIds(),
             Hud,
             Core::Platform::GetAbsoluteTime());
        Gui.MainGui.RenderOffscreen();
        sg_reset_state_cache();
    }
#endif
}

void RenderGuiPresentationLayer(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    if (Gui.UseGui && Gui.MainGui.IsActive())
    {
        Gui.MainGui.Render();
    }
#endif
    sg_reset_state_cache();
    Render::EndPass();
    Render::Commit();
}

void HandleGuiHudPresentationEvent(GuiRuntime& Gui, const void* Event)
{
#if ENABLE_GUI_UI
    const sapp_event* E = static_cast<const sapp_event*>(Event);
    if (Gui.UseGui && Gui.MainGui.IsActive() && E)
    {
        const bool HandledByGui = Gui.MainGui.HandleEvent(E);
        if (E->type == SAPP_EVENTTYPE_MOUSE_DOWN ||
            E->type == SAPP_EVENTTYPE_MOUSE_UP ||
            E->type == SAPP_EVENTTYPE_MOUSE_SCROLL)
        {
            if (HandledByGui || Gui.MainGui.IsPointInHudEvent(E))
                sapp_consume_event();
        }
    }
#else
    (void)Gui;
    (void)Event;
#endif
}

} // namespace Game::Client::GameLoop


// === Consolidated from src/Main/GameGuiBootstrap.cpp ===

#include <memory>
#include <utility>

#include "Core/Utils/Logger.h"

#if ENABLE_GUI_UI
#include <NsApp/ApplicationLauncher.h>
#include <NsApp/Display.h>
#include <NsApp/LocalFontProvider.h>
#include <NsApp/LocalTextureProvider.h>
#include <NsApp/LocalXamlProvider.h>
#include <NsApp/ThemeProviders.h>
#include <NsCore/RegisterComponent.h>
#include <NsGui/IntegrationAPI.h>
#include <NsGui/Uri.h>
#endif

namespace Game::Client::GameLoop
{

#if ENABLE_GUI_UI
namespace
{
class EmbeddedAudioDisplay final : public AeroApp::Display
{
public:
    void EnterMessageLoop(bool) override {}
    void* GetNativeHandle() const override { return nullptr; }
    uint32_t GetClientWidth() const override { return 0; }
    uint32_t GetClientHeight() const override { return 0; }
};

std::unique_ptr<EmbeddedAudioDisplay> gAeroAudioDisplay;

void PlayAeroAudio(void* User, const Aero::Uri& Uri, float Volume)
{
    auto* Display = static_cast<AeroApp::Display*>(User);
    if (Display)
        Display->PlayAudio(Uri, Volume);
}
}
#endif

void InitializeGuiRuntime(
    GuiRuntime& Gui,
    std::string& AssetRoot,
    int Width,
    int Height,
    std::function<void(const std::string& Action)> GlueActionCallback,
    std::function<void(const std::string& AudioPath)> GlueAudioCallback,
    bool EnableGlueMenu)
{
    Gui.UseGui = false;

#if ENABLE_GUI_UI
    Aero::GUI::Init();

    Gui.GuiInitialized = true;
    AeroApp::Launcher::RegisterAppComponents();
    gAeroAudioDisplay = std::make_unique<EmbeddedAudioDisplay>();
    Aero::GUI::SetPlayAudioCallback(gAeroAudioDisplay.get(), &PlayAeroAudio);
    Aero::GUI::SetXamlProvider(Aero::MakePtr<AeroApp::LocalXamlProvider>(AssetRoot.c_str()));
    Aero::GUI::SetTextureProvider(Aero::MakePtr<AeroApp::LocalTextureProvider>(AssetRoot.c_str()));
    Aero::GUI::SetFontProvider(Aero::MakePtr<AeroApp::LocalFontProvider>(AssetRoot.c_str()));
    AeroApp::SetThemeProviders();
    Aero::GUI::LoadApplicationResources(AeroApp::Theme::DarkBlue());

    const bool GuiHudReady = Gui.MainGui.Init(AssetRoot, Width, Height, "ui/Hud.xaml");
    Gui.UseGui = false;
    Gui.AppliedWidth = Width;
    Gui.AppliedHeight = Height;
    if (GuiHudReady)
    {
        Core::Utils::Logger::Info(
            "GameMain",
            "Gui HUD loaded; Native Terran HUD is active in-game by default (F9 toggles Gui)");
    }
    else
    {
        Core::Utils::Logger::Info(
            "GameMain",
            "Gui HUD unavailable; Native Terran HUD is active in-game");
    }

    Gui.GlueGuiInitialized = false;
    if (EnableGlueMenu)
    {
        Gui.GlueGuiInitialized = Gui.GlueGui.Initialize(AssetRoot, Width, Height);
        if (Gui.GlueGuiInitialized)
        {
            Gui.GlueGui.SetActionCallback(std::move(GlueActionCallback));
            if (GlueAudioCallback)
                Gui.GlueGui.SetAudioCallback(std::move(GlueAudioCallback));
        }
    }
    else
    {
        Core::Utils::Logger::Info("GameMain", "Glue menu skipped for direct test map startup");
    }
#else
    (void)AssetRoot;
    (void)Width;
    (void)Height;
    (void)GlueActionCallback;
    (void)GlueAudioCallback;
    (void)EnableGlueMenu;
#endif
}

void ShutdownGuiRuntime(GuiRuntime& Gui)
{
#if ENABLE_GUI_UI
    Gui.MainGui.Shutdown();
    if (Gui.GlueGuiInitialized)
        Gui.GlueGui.Shutdown();
    if (Gui.GuiInitialized)
    {
        Aero::GUI::SetPlayAudioCallback(nullptr, nullptr);
        gAeroAudioDisplay.reset();
        Aero::GUI::Shutdown();
        Gui.GuiInitialized = false;
    }
#else
    (void)Gui;
#endif
}

} // namespace Game::Client::GameLoop
