#include "third_party/sokol/sokol_app.h"
#include "third_party/sokol/sokol_gfx.h"
#include "third_party/sokol/sokol_glue.h"
#include "third_party/sokol/sokol_log.h"

#include "Core/Platform/Platform.h"
#include "Core/Utils/Logger.h"
#include "Core/Audio/AudioSystem.h"
#include "Render/sokol/Renderer.h"
#include "Render/sokol/Draw2D.h"
#include "Render/sokol/Camera2D.h"
#include "Render/sokol/Color.h"
#include "Render/sokol/ParticleSystem.h"

#include "Resource/GameDetector.h"
#include "Resource/sokol/ResourceCacheSokol.h"

#include "Game/sokol/Entity.h"
#include "Game/sokol/MapLoader.h"
#include "Game/sokol/TilesetRenderer.h"
#include "Game/sokol/EntityRenderer.h"
#include "Game/sokol/CameraController.h"
#include "Game/sokol/Selection.h"
#include "Game/sokol/UI/Hud.h"

// ECS + game logic systems
#include "Core/ECS/SystemManager.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Systems/HarvestSystem.h"
#include "Game/Systems/BuildingSystem.h"
#include "Game/Systems/ProductionSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/FogOfWarSystem.h"
#include "Game/AI/AIBrain.h"
#include "Game/Systems/UpgradeSystem.h"
#include "Game/Systems/ZergSystem.h"
#include "Game/Systems/ProtossSystem.h"
#include "Game/Campaign/CampaignController.h"
#include "Game/Components.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
    // StarCraft "1.16.1" / Brood War unit IDs:
    //   0x07  = Terran SCV
    //   0x6E  = Terran Command Center
    //   0x0B  = Mineral field (Type 1)
    //   0x0C  = Mineral field (Type 2)
    constexpr int ChkScv = 0x07;
    constexpr int ChkCommandCenter = 0x6E;

    struct State
    {
        bool Initialized = false;
        int FrameCount = 0;
        float FpsTimer = 0.0f;
        int FpsFrames = 0;
        int FpsValue = 0;

        std::string InstallPath;
        std::string MapPath;

        // Game resources
        Resource::SokolBridge::ResourceCacheSokol Cache;
        Game::SokolDemo::GameMap GameMapData;
        std::vector<Game::Map::PlacedUnit> ChkUnits;
        Game::SokolDemo::EntityManager Entities;
        Game::SokolDemo::TilesetRenderer Tilemap;
        Game::SokolDemo::EntityRenderer EntityRen;
        Game::SokolDemo::CameraController CameraCtrl;
        Game::SokolDemo::Selection Selector;
        Game::SokolDemo::UI::Hud Hud;
        Render::Camera2D Camera;
        bool MapLoaded = false;

        // Visual effects
        struct Projectile
        {
            float SX, SY, TX, TY;   // start and target positions
            float Timer = 0.0f;
            float Duration = 0.3f;
            bool Active = false;
        };
        std::vector<Projectile> Projectiles;
        Render::ParticleSystem Particles;

        // Audio
        Core::Audio::AudioSystem Audio;

        // Campaign
        Game::Campaign::CampaignController Campaign;
        bool CampaignActive = false;

        // ECS state
        Core::ECS::SystemManager EcsSysMgr;
        Game::Map::GameMap EcsMap;
        bool EcsInitialized = false;
        bool TerranAuditSpawned = false;
    };
    State g;

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

    static std::vector<Core::ECS::EntityID> FilterHarvesters(
        Core::ECS::EntityManager& EM,
        const std::vector<Core::ECS::EntityID>& EcsIds);

    static bool HasCombatSelection(Core::ECS::EntityManager& EM,
                                   const std::vector<Core::ECS::EntityID>& EcsIds)
    {
        for (int EntityId : EcsIds)
        {
            if (EM.GetComponent<Game::Components::CombatComponent>(EntityId))
                return true;
        }
        return false;
    }

    static const char* CursorName(GameCursorKind Kind)
    {
        switch (Kind)
        {
            case GameCursorKind::Arrow: return "arrow";
            case GameCursorKind::Drag: return "drag";
            case GameCursorKind::Illegal: return "illegal";
            case GameCursorKind::TargetNeutral: return "targn";
            case GameCursorKind::TargetGreen: return "targg";
            case GameCursorKind::TargetRed: return "targr";
            case GameCursorKind::TargetYellow: return "targy";
            case GameCursorKind::MagnifierGreen: return "magg";
            case GameCursorKind::MagnifierRed: return "magr";
            case GameCursorKind::MagnifierYellow: return "magy";
            case GameCursorKind::ScrollUp: return "scrollu";
            case GameCursorKind::ScrollDown: return "scrolld";
            case GameCursorKind::ScrollLeft: return "scrolll";
            case GameCursorKind::ScrollRight: return "scrollr";
            case GameCursorKind::ScrollUpLeft: return "scrollul";
            case GameCursorKind::ScrollUpRight: return "scrollur";
            case GameCursorKind::ScrollDownLeft: return "scrolldl";
            case GameCursorKind::ScrollDownRight: return "scrolldr";
            case GameCursorKind::Time: return "time";
        }
        return "arrow";
    }

    static float CursorAnimationFps(GameCursorKind Kind)
    {
        switch (Kind)
        {
            case GameCursorKind::Arrow:
            case GameCursorKind::Drag:
            case GameCursorKind::Illegal:
                return 6.0f;
            case GameCursorKind::TargetNeutral:
            case GameCursorKind::TargetGreen:
            case GameCursorKind::TargetRed:
            case GameCursorKind::TargetYellow:
            case GameCursorKind::ScrollUp:
            case GameCursorKind::ScrollDown:
            case GameCursorKind::ScrollLeft:
            case GameCursorKind::ScrollRight:
            case GameCursorKind::ScrollUpLeft:
            case GameCursorKind::ScrollUpRight:
            case GameCursorKind::ScrollDownLeft:
            case GameCursorKind::ScrollDownRight:
                return 5.0f;
            case GameCursorKind::MagnifierGreen:
            case GameCursorKind::MagnifierRed:
            case GameCursorKind::MagnifierYellow:
                return 4.0f;
            case GameCursorKind::Time:
                return 0.0f;
        }
        return 0.0f;
    }

    static void CursorHotspot(GameCursorKind Kind, float& OutX, float& OutY)
    {
        switch (Kind)
        {
            case GameCursorKind::Arrow:
                OutX = 3.0f; OutY = 3.0f; return;
            case GameCursorKind::Drag:
                OutX = 8.0f; OutY = 8.0f; return;
            case GameCursorKind::Illegal:
                OutX = 8.0f; OutY = 8.0f; return;
            case GameCursorKind::TargetNeutral:
            case GameCursorKind::TargetGreen:
            case GameCursorKind::TargetRed:
            case GameCursorKind::TargetYellow:
                OutX = 9.0f; OutY = 8.0f; return;
            case GameCursorKind::MagnifierGreen:
            case GameCursorKind::MagnifierRed:
            case GameCursorKind::MagnifierYellow:
                OutX = 9.0f; OutY = 8.0f; return;
            case GameCursorKind::ScrollUp:
            case GameCursorKind::ScrollDown:
            case GameCursorKind::ScrollLeft:
            case GameCursorKind::ScrollRight:
            case GameCursorKind::ScrollUpLeft:
            case GameCursorKind::ScrollUpRight:
            case GameCursorKind::ScrollDownLeft:
            case GameCursorKind::ScrollDownRight:
                OutX = 7.0f; OutY = 8.0f; return;
            case GameCursorKind::Time:
                OutX = 11.0f; OutY = 5.0f; return;
            default:
                OutX = 8.0f; OutY = 8.0f; return;
        }
    }

    // Returns true if the CHK unit ID is a mineral patch
    static bool IsMineralUnit(int Id)
    {
        return (Id >= 100 && Id <= 102) || (Id >= 176 && Id <= 178) ||
               Id == 11 || Id == 12 || Id == 95 || Id == 99;
    }

    // Returns true if the CHK unit ID is a vespene geyser
    static bool IsGeyserUnit(int Id)
    {
        return Id == 103 || Id == 188;
    }

    // Returns true if the CHK unit ID is a building (Terran or Zerg)
    static bool IsBuildingChkId(int Id)
    {
        // Terran buildings
        if ((Id >= 0x6E && Id <= 0x7D) || Id == 0x82 || Id == 0x83) return true;
        // Zerg buildings (Hatchery through Defiler Mound)
        if (Id >= 86 && Id <= 102) return true;
        return false;
    }

    // Returns true if the CHK ID is a Zerg organic building (spreads creep)
    static bool IsOrganicBuilding(int Id)
    {
        return Id == 86 || Id == 87 || Id == 88;  // Hatchery, Lair, Hive
    }

    // Returns true if the CHK ID is a Protoss building that requires Pylon power
    static bool RequiresPylonChkId(int Id)
    {
        return Id >= 127 && Id <= 141 && Id != 128; // Pylon through Observatory, not Assimilator
    }

    // Returns true if the CHK unit ID is a start location marker
    static bool IsStartLocationUnit(int Id)
    {
        return Id == 214 || Id == 115 || Id == 92;
    }

    // Spawn sokol entities from parsed CHK unit data
    static void SpawnChkEntities()
    {
        for (auto& U : g.ChkUnits)
        {
            if (IsStartLocationUnit(U.UnitID))
                continue;

            auto Kind = Game::SokolDemo::EntityKind::Unit;
            if (IsBuildingChkId(U.UnitID))
                Kind = Game::SokolDemo::EntityKind::Building;
            else if (IsMineralUnit(U.UnitID) || IsGeyserUnit(U.UnitID))
                Kind = Game::SokolDemo::EntityKind::Resource;

            int EId = g.Entities.AddEntity(Kind);
            auto& E = g.Entities.GetEntity(EId);
            E.X = (float)U.X;
            E.Y = (float)U.Y;
            E.ChkUnitId = U.UnitID;
            E.ImageId = (int)g.Cache.ResolveImageID(U.UnitID);
            E.PlayerId = U.Owner;
            E.HitPoints = 60;
            E.MaxHitPoints = 60;
            if (Kind == Game::SokolDemo::EntityKind::Building)
            {
                E.Size = Game::SokolDemo::UnitSize::Large;
                E.HitPoints = 1000;
                E.MaxHitPoints = 1000;
            }
            else if (IsMineralUnit(U.UnitID) || IsGeyserUnit(U.UnitID))
            {
                E.ResourceAmount = U.ResourceAmount > 0 ? (int)U.ResourceAmount : 1500;
                E.Size = Game::SokolDemo::UnitSize::Large;
                if (IsGeyserUnit(U.UnitID))
                    E.MaxHitPoints = 500;
            }
        }
        g.EntityRen.ResolveFrames(g.Entities);
        Core::Utils::Logger::Info("SokolMain",
            "Spawned " + std::to_string(g.Entities.GetCount()) + " entities from CHK");
    }

    static Game::SokolDemo::EntityKind ResolveSokolKindFromEcs(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (EM.GetComponent<Game::Components::ResourceComponent>(EntityID))
            return Game::SokolDemo::EntityKind::Resource;
        if (EM.GetComponent<Game::Components::BuildingComponent>(EntityID))
            return Game::SokolDemo::EntityKind::Building;
        return Game::SokolDemo::EntityKind::Unit;
    }

    static bool SyncSokolEntityFromEcs(Core::ECS::EntityManager& EM, int EntityID)
    {
        while (g.Entities.GetCount() < EntityID)
            g.Entities.AddEntity(Game::SokolDemo::EntityKind::Unit);

        auto& SokolEntity = g.Entities.GetEntity(EntityID);
        auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        auto* Render = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
        auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityID);

        bool NeedsResolve = false;
        SokolEntity.Kind = ResolveSokolKindFromEcs(EM, EntityID);
        SokolEntity.Visible = true;
        if (Transform)
        {
            SokolEntity.X = Transform->Position.X;
            SokolEntity.Y = Transform->Position.Y;
        }
        if (Unit)
        {
            SokolEntity.ChkUnitId = Unit->ChkUnitID;
            SokolEntity.HitPoints = Unit->HitPoints;
            SokolEntity.MaxHitPoints = Unit->MaxHitPoints;
            SokolEntity.Size =
                Unit->SizeType == Game::Components::UnitSize::Small ? Game::SokolDemo::UnitSize::Small :
                Unit->SizeType == Game::Components::UnitSize::Large ? Game::SokolDemo::UnitSize::Large :
                                                                      Game::SokolDemo::UnitSize::Medium;
        }
        if (Player)
            SokolEntity.PlayerId = Player->PlayerID;
        if (Render)
        {
            if (SokolEntity.ImageId != (int)Render->ImageID)
                NeedsResolve = true;
            SokolEntity.ImageId = (int)Render->ImageID;
            SokolEntity.FrameIndex = Render->FrameIndex;
            SokolEntity.FrameCount = Render->TotalFrames;
            SokolEntity.Flipped = Render->Flipped;
            if (Render->TotalFrames <= 1 && SokolEntity.Frames && !SokolEntity.Frames->empty())
            {
                const int ResolvedFrameCount = (int)SokolEntity.Frames->size();
                Render->TotalFrames = ResolvedFrameCount;
                SokolEntity.FrameCount = ResolvedFrameCount;
            }
        }
        if (Resource)
            SokolEntity.ResourceAmount = Resource->Amount;
        return NeedsResolve;
    }

    static void SyncSokolEntitiesFromEcs()
    {
        auto& EM = g.EcsSysMgr.GetEntityManager();
        auto AliveEntities = EM.GetEntitiesWith<
            Game::Components::TransformComponent,
            Game::Components::UnitComponent>();

        if (!AliveEntities.empty())
        {
            while (g.Entities.GetCount() < AliveEntities.back())
                g.Entities.AddEntity(Game::SokolDemo::EntityKind::Unit);
        }

        bool NeedsResolve = false;
        auto& SokolEnts = g.Entities.GetAll();
        for (size_t I = 0; I < SokolEnts.size(); ++I)
        {
            int EntityID = (int)(I + 1);
            if (!EM.IsAlive(EntityID))
            {
                if (SokolEnts[I].Visible)
                {
                    g.Particles.EmitDeathBurst(SokolEnts[I].X, SokolEnts[I].Y);
                    g.Audio.Play(Core::Audio::SoundEffect::Explosion);
                }
                SokolEnts[I].Visible = false;
                continue;
            }
            NeedsResolve = SyncSokolEntityFromEcs(EM, EntityID) || NeedsResolve;
        }

        if (NeedsResolve)
            g.EntityRen.ResolveFrames(g.Entities);
    }

    static std::vector<Core::ECS::EntityID> CollectSelectedAliveEcsIds(Core::ECS::EntityManager& EM)
    {
        std::vector<Core::ECS::EntityID> EcsIds;
        const auto& Selected = g.Selector.GetSelected();
        EcsIds.reserve(Selected.size());
        for (int SelectedId : Selected)
        {
            if (EM.IsAlive(SelectedId))
                EcsIds.push_back(SelectedId);
        }
        return EcsIds;
    }

    static bool IsMapInteractionPoint(int ScreenX, int ScreenY, int ViewportWidth, int ViewportHeight)
    {
        if (ScreenX < 0 || ScreenX >= ViewportWidth || ScreenY < 0 || ScreenY >= ViewportHeight)
            return false;
        if (g.Hud.IsPointInHudBand(ScreenX, ScreenY, ViewportWidth, ViewportHeight))
            return false;

        float DummyWX = 0.0f;
        float DummyWY = 0.0f;
        if (g.Hud.CheckMinimapClick(ScreenX, ScreenY, ViewportWidth, ViewportHeight,
                                    g.GameMapData.Width, g.GameMapData.Height,
                                    DummyWX, DummyWY))
        {
            return false;
        }

        return g.Hud.CheckCommandPanelClick(ScreenX, ScreenY, ViewportWidth, ViewportHeight) < 0;
    }

    static GameCursorKind ResolveScrollCursorKind(int MouseX, int MouseY, int ViewportWidth, int ViewportHeight)
    {
        constexpr int EdgeMargin = 8;
        const int TopBlocked = g.Hud.GetResourceBarHeight();
        const int BottomBlocked = g.Hud.GetHudHeight(ViewportWidth, ViewportHeight);
        const int ActiveBottom = ViewportHeight - BottomBlocked;

        const bool AnyMouseButtonDown =
            Core::Platform::IsMouseButtonDown(0) ||
            Core::Platform::IsMouseButtonDown(1) ||
            Core::Platform::IsMouseButtonDown(2);

        if (!Core::Platform::IsWindowFocused() ||
            !Core::Platform::IsMouseInClientArea() ||
            AnyMouseButtonDown ||
            MouseX < 0 || MouseX >= ViewportWidth ||
            MouseY < TopBlocked || MouseY >= ActiveBottom)
        {
            return GameCursorKind::Arrow;
        }

        const bool Left = MouseX < EdgeMargin;
        const bool Right = MouseX >= ViewportWidth - EdgeMargin;
        const bool Up = MouseY < TopBlocked + EdgeMargin;
        const bool Down = MouseY >= ActiveBottom - EdgeMargin;

        if (Up && Left) return GameCursorKind::ScrollUpLeft;
        if (Up && Right) return GameCursorKind::ScrollUpRight;
        if (Down && Left) return GameCursorKind::ScrollDownLeft;
        if (Down && Right) return GameCursorKind::ScrollDownRight;
        if (Up) return GameCursorKind::ScrollUp;
        if (Down) return GameCursorKind::ScrollDown;
        if (Left) return GameCursorKind::ScrollLeft;
        if (Right) return GameCursorKind::ScrollRight;
        return GameCursorKind::Arrow;
    }

    static ClickTargetContext ResolveClickTargetContext(Core::ECS::EntityManager& EM,
                                                        const std::vector<Core::ECS::EntityID>& SelectedIds,
                                                        const Core::Math::Vec2& WorldPosition)
    {
        ClickTargetContext Context;
        Context.WorldPosition = WorldPosition;

        float BestResourceDist = 32.0f;
        for (int EntityId : EM.GetEntitiesWith<
            Game::Components::TransformComponent,
            Game::Components::ResourceComponent>())
        {
            auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityId);
            auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityId);
            if (!Transform || !Resource || Resource->IsDepleted)
                continue;

            float Distance = Transform->Position.DistanceTo(WorldPosition);
            if (Distance < BestResourceDist)
            {
                BestResourceDist = Distance;
                Context.ResourceEntityID = EntityId;
            }
        }

        int MyPlayerId = 0;
        if (!SelectedIds.empty())
        {
            if (auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(SelectedIds.front()))
                MyPlayerId = Player->PlayerID;
        }

        float BestEntityDist = 32.0f;
        for (int EntityId : EM.GetEntitiesWith<
            Game::Components::TransformComponent,
            Game::Components::UnitComponent,
            Game::Components::PlayerComponent>())
        {
            auto* Transform = EM.GetComponent<Game::Components::TransformComponent>(EntityId);
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId);
            auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityId);
            if (!Transform || !Unit || !Player || Unit->HitPoints <= 0)
                continue;

            float Distance = Transform->Position.DistanceTo(WorldPosition);
            if (Distance < BestEntityDist)
            {
                BestEntityDist = Distance;
                Context.AnyEntityID = EntityId;
                Context.EnemyEntityID = (Player->PlayerID != MyPlayerId) ? EntityId : -1;
            }
        }

        return Context;
    }

    static CursorRenderState ResolveCursorState(int MouseX, int MouseY)
    {
        CursorRenderState State;
        const int ViewportWidth = g.Camera.GetViewportWidth();
        const int ViewportHeight = g.Camera.GetViewportHeight();
        State.UseGameCursor =
            Core::Platform::IsWindowFocused() &&
            Core::Platform::IsMouseInClientArea() &&
            MouseX >= 0 && MouseX < ViewportWidth &&
            MouseY >= 0 && MouseY < ViewportHeight;

        if (!State.UseGameCursor)
        {
            CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
            return State;
        }

        if (g.Selector.IsDragging())
        {
            State.Kind = GameCursorKind::Drag;
            CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
            return State;
        }

        const bool MapPoint = IsMapInteractionPoint(MouseX, MouseY, ViewportWidth, ViewportHeight);
        const bool AttackKeyDown = Core::Platform::IsKeyDown(65);

        if (g.Hud.HasPendingTarget())
        {
            if (!MapPoint)
            {
                State.Kind = GameCursorKind::Illegal;
                CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
                return State;
            }

            float WorldX = 0.0f;
            float WorldY = 0.0f;
            g.Camera.ScreenToWorld((float)MouseX, (float)MouseY, WorldX, WorldY);

            bool Legal = true;
            ClickTargetContext TargetContext;
            if (g.EcsInitialized)
            {
                auto& EM = g.EcsSysMgr.GetEntityManager();
                auto EcsIds = CollectSelectedAliveEcsIds(EM);
                TargetContext = ResolveClickTargetContext(EM, EcsIds, {WorldX, WorldY});
            }

            switch (g.Hud.GetPendingTargetMode())
            {
                case Game::SokolDemo::UI::PendingTargetMode::BuildPlacement:
                    State.Kind = GameCursorKind::TargetYellow;
                    break;
                case Game::SokolDemo::UI::PendingTargetMode::BuildRefinery:
                    Legal = TargetContext.ResourceEntityID >= 0;
                    State.Kind = Legal ? GameCursorKind::TargetYellow : GameCursorKind::Illegal;
                    break;
                case Game::SokolDemo::UI::PendingTargetMode::Harvest:
                    Legal = TargetContext.ResourceEntityID >= 0;
                    State.Kind = Legal ? GameCursorKind::MagnifierGreen : GameCursorKind::Illegal;
                    break;
                case Game::SokolDemo::UI::PendingTargetMode::Move:
                    State.Kind = GameCursorKind::TargetGreen;
                    break;
                case Game::SokolDemo::UI::PendingTargetMode::AttackMove:
                    State.Kind = GameCursorKind::TargetRed;
                    break;
                case Game::SokolDemo::UI::PendingTargetMode::Patrol:
                    State.Kind = GameCursorKind::TargetNeutral;
                    break;
                case Game::SokolDemo::UI::PendingTargetMode::Follow:
                    Legal = TargetContext.AnyEntityID >= 0;
                    State.Kind = Legal ? GameCursorKind::TargetNeutral : GameCursorKind::Illegal;
                    break;
                default:
                    break;
            }

            CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
            return State;
        }

        if (g.EcsInitialized && MapPoint)
        {
            auto& EM = g.EcsSysMgr.GetEntityManager();
            auto EcsIds = CollectSelectedAliveEcsIds(EM);
            if (!EcsIds.empty())
            {
                float WorldX = 0.0f;
                float WorldY = 0.0f;
                g.Camera.ScreenToWorld((float)MouseX, (float)MouseY, WorldX, WorldY);
                auto TargetContext = ResolveClickTargetContext(EM, EcsIds, {WorldX, WorldY});

                if (AttackKeyDown || (TargetContext.EnemyEntityID >= 0 && HasCombatSelection(EM, EcsIds)))
                {
                    State.Kind = GameCursorKind::TargetRed;
                    CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
                    return State;
                }

                if (TargetContext.ResourceEntityID >= 0)
                {
                    auto Harvesters = FilterHarvesters(EM, EcsIds);
                    if (!Harvesters.empty())
                    {
                        State.Kind = GameCursorKind::MagnifierGreen;
                        CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
                        return State;
                    }
                }
            }
        }

        const GameCursorKind ScrollKind = ResolveScrollCursorKind(MouseX, MouseY, ViewportWidth, ViewportHeight);
        State.Kind = (ScrollKind != GameCursorKind::Arrow) ? ScrollKind : GameCursorKind::Arrow;
        CursorHotspot(State.Kind, State.HotspotX, State.HotspotY);
        return State;
    }

    static void IssuePatrolCommand(Core::ECS::EntityManager& EM,
                                   const std::vector<Core::ECS::EntityID>& EcsIds,
                                   const Core::Math::Vec2& Target)
    {
        for (int EntityId : EcsIds)
        {
            auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityId);
            if (!Command)
                Command = &EM.AddComponent<Game::Components::CommandComponent>(EntityId);
            Command->TargetPosition = Target;
            auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityId);
            if (Path)
            {
                Path->Path.clear();
                Path->PathIndex = 0;
            }
        }
        Game::Systems::CommandSystem::IssueMove(EM, EcsIds, Target);
        for (int EntityId : EcsIds)
        {
            auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityId);
            if (Command)
                Command->CurrentCommand = Game::Components::CommandType::Patrol;
        }
    }

    static void IssueFollowCommand(Core::ECS::EntityManager& EM,
                                   const std::vector<Core::ECS::EntityID>& EcsIds,
                                   int TargetEntityId)
    {
        if (TargetEntityId < 0)
            return;

        auto* FollowTarget = EM.GetComponent<Game::Components::TransformComponent>(TargetEntityId);
        if (FollowTarget)
            Game::Systems::CommandSystem::IssueMove(EM, EcsIds, FollowTarget->Position);

        for (int EntityId : EcsIds)
        {
            auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityId);
            if (!Command)
                Command = &EM.AddComponent<Game::Components::CommandComponent>(EntityId);
            Command->CurrentCommand = Game::Components::CommandType::Follow;
            Command->TargetEntityID = TargetEntityId;
        }
    }

    static std::vector<Core::ECS::EntityID> FilterHarvesters(Core::ECS::EntityManager& EM,
                                                             const std::vector<Core::ECS::EntityID>& EcsIds)
    {
        std::vector<Core::ECS::EntityID> Harvesters;
        Harvesters.reserve(EcsIds.size());
        const int ScvDefId = Game::Races::GetUnitDefID("SCV");
        for (int EntityId : EcsIds)
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityId);
            if (!Unit)
                continue;
            if (Game::Races::GetTerranDefIDByChkUnitID(Unit->ChkUnitID) == ScvDefId)
                Harvesters.push_back(EntityId);
        }
        return Harvesters;
    }

    static void AuditTerranResources()
    {
        const auto& TerranDefs = Game::Races::GetTerranDefs();
        int OkCount = 0;
        int MissingCount = 0;
        std::string MissingNames;
        for (size_t I = 0; I < TerranDefs.size(); ++I)
        {
            const auto& Def = TerranDefs[I];
            uint32_t ImageID = g.Cache.ResolveImageID(Def.ChkUnitID);
            std::string GrpPath = g.Cache.GetDataLoader().ResolveGrpPath(ImageID);
            bool Found = !GrpPath.empty() && g.Cache.ReadFile(GrpPath).has_value();
            std::string Candidates;
            auto Paths = g.Cache.DebugBuildExtractedAssetCandidates(GrpPath);
            for (size_t P = 0; P < Paths.size(); ++P)
            {
                if (P > 0) Candidates += ", ";
                Candidates += Paths[P];
            }
            Core::Utils::Logger::Info("TerranAudit",
                Def.Name + " -> chk=" + std::to_string(Def.ChkUnitID) +
                " image=" + std::to_string(ImageID) +
                " grp=" + GrpPath +
                " status=" + (Found ? "ok" : "missing") +
                (Candidates.empty() ? "" : " tried=[" + Candidates + "]"));
            if (Found)
            {
                ++OkCount;
            }
            else
            {
                ++MissingCount;
                if (!MissingNames.empty()) MissingNames += ", ";
                MissingNames += Def.Name;
            }
        }
        Core::Utils::Logger::Info("TerranAudit",
            "summary ok=" + std::to_string(OkCount) +
            " missing=" + std::to_string(MissingCount) +
            (MissingNames.empty() ? "" : " names=" + MissingNames));
    }

    static void SpawnTerranAuditFormation()
    {
        if (!g.EcsInitialized || g.TerranAuditSpawned)
            return;

        AuditTerranResources();

        auto& EM = g.EcsSysMgr.GetEntityManager();
        const auto& TerranDefs = Game::Races::GetTerranDefs();
        float BaseX = g.Camera.GetX() - 240.0f;
        float BaseY = g.Camera.GetY() - 160.0f;
        int UnitCol = 0;
        int BuildingCol = 0;
        for (size_t I = 0; I < TerranDefs.size(); ++I)
        {
            const auto& Def = TerranDefs[I];
            if (Def.IsBuilding)
            {
                Core::Math::Vec2 Pos(BaseX + (float)(BuildingCol % 6) * 96.0f,
                                     BaseY + 180.0f + (float)(BuildingCol / 6) * 96.0f);
                Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(EM, (int)I, Pos, 0);
                ++BuildingCol;
            }
            else
            {
                Core::Math::Vec2 Pos(BaseX + (float)(UnitCol % 6) * 72.0f,
                                     BaseY + (float)(UnitCol / 6) * 72.0f);
                Game::Systems::ProductionSystem::DebugSpawnUnit(EM, (int)I, Pos, 0);
                ++UnitCol;
            }
        }

        SyncSokolEntitiesFromEcs();
        g.EntityRen.ResolveFrames(g.Entities);
        g.TerranAuditSpawned = true;
        Core::Utils::Logger::Info("TerranAudit", "spawned Terran audit formation");
    }

    bool TryLoadMap()
    {
        Game::SokolDemo::MapLoader::Result R =
            Game::SokolDemo::MapLoader::Load(g.MapPath);
        if (!R.Loaded)
        {
            Core::Utils::Logger::Warning("SokolMain", "Map load failed: " + R.ErrorMessage);
            return false;
        }
        g.GameMapData = R.Map;
        g.ChkUnits = R.ChkUnits;

        // Initialize tileset
        std::string TsName = g.GameMapData.TilesetName;
        if (TsName.empty()) TsName = "badlands";
        g.Tilemap.Init(TsName, g.Cache);
        g.EntityRen.Init(g.Cache);

        // Spawn all entities from CHK data
        SpawnChkEntities();

        // Initialize camera: center on the first player-0 start location
        int PixelW = g.GameMapData.Width * 32;
        int PixelH = g.GameMapData.Height * 32;
        g.Camera.SetViewport(Core::Platform::GetWindowWidth(),
                             Core::Platform::GetWindowHeight());
        float CamX = (float)PixelW * 0.5f;
        float CamY = (float)PixelH * 0.5f;
        for (auto& SL : g.GameMapData.StartLocations)
        {
            if (SL.PlayerId == 0)
            {
                CamX = (float)(SL.TileX * 32 + 16);
                CamY = (float)(SL.TileY * 32 + 16);
                break;
            }
        }
        g.Camera.SetPosition(CamX, CamY);
        g.CameraCtrl.Attach(g.Camera);
        g.CameraCtrl.SetBounds(g.GameMapData.Width, g.GameMapData.Height);
        g.Tilemap.SetCamera(g.Camera);

        g.MapLoaded = true;
        return true;
    }

    bool InitGameSystems()
    {
        // 1. Find the install path
        Resource::GameInstallInfo Info = Resource::GameDetector::Detect();
        if (!Info.IsValid())
        {
            Core::Utils::Logger::Error("SokolMain",
                "No StarCraft install found. Place the project in a folder that has MPQ/CASC.");
            return false;
        }
        g.InstallPath = Info.InstallPath;
        Core::Utils::Logger::Info("SokolMain", "Install: " + g.InstallPath +
            (Info.Type == Resource::ArchiveType::Casc ? " (CASC)" : " (MPQ)"));

        // 2. Initialize resource cache
        if (!g.Cache.Initialize(g.InstallPath))
        {
            Core::Utils::Logger::Error("SokolMain", "ResourceCache init failed");
            return false;
        }
        Game::Systems::ProductionSystem::SetImageResolver(
            [](int ChkUnitID) { return g.Cache.ResolveImageID(ChkUnitID); });
        Game::Systems::BuildingSystem::SetImageResolver(
            [](int ChkUnitID) { return g.Cache.ResolveImageID(ChkUnitID); });

        // 3. Find a map
        g.MapPath = Game::SokolDemo::MapLoader::FindDefaultMap(g.InstallPath);
        if (g.MapPath.empty())
        {
            Core::Utils::Logger::Warning("SokolMain",
                "No default map found; running with empty map");
            g.GameMapData.Width = 64;
            g.GameMapData.Height = 64;
            g.GameMapData.MegaTileIds.assign(64 * 64, 0);
            g.GameMapData.TilesetName = "badlands";
            g.Tilemap.Init("badlands", g.Cache);
            g.Tilemap.SetCamera(g.Camera);

            int PixelW = g.GameMapData.Width * 32;
            int PixelH = g.GameMapData.Height * 32;
            g.Camera.SetViewport(Core::Platform::GetWindowWidth(),
                                 Core::Platform::GetWindowHeight());
            g.Camera.SetPosition((float)PixelW * 0.5f, (float)PixelH * 0.5f);
            g.CameraCtrl.Attach(g.Camera);
            g.CameraCtrl.SetBounds(g.GameMapData.Width, g.GameMapData.Height);
            g.MapLoaded = true;
        }
        else
        {
            if (!TryLoadMap())
            {
                Core::Utils::Logger::Warning("SokolMain",
                    "Falling back to synthetic map because default map could not be loaded");
                g.GameMapData.Width = 64;
                g.GameMapData.Height = 64;
                g.GameMapData.MegaTileIds.assign(64 * 64, 0);
                g.GameMapData.TilesetName = "badlands";
                g.Tilemap.Init("badlands", g.Cache);
                g.Tilemap.SetCamera(g.Camera);

                int PixelW = g.GameMapData.Width * 32;
                int PixelH = g.GameMapData.Height * 32;
                g.Camera.SetViewport(Core::Platform::GetWindowWidth(),
                                     Core::Platform::GetWindowHeight());
                g.Camera.SetPosition((float)PixelW * 0.5f, (float)PixelH * 0.5f);
                g.CameraCtrl.Attach(g.Camera);
                g.CameraCtrl.SetBounds(g.GameMapData.Width, g.GameMapData.Height);
                g.MapLoaded = true;
            }
        }

        // 4. Initialize HUD
        g.Hud.Init(&g.Cache);

        // 4.5 Spawn demo starting units when the melee map only has start
        // locations/resources and no pre-placed bases.
        auto SpawnStartBaseIfMissing = [&](int PlayerId)
        {
            bool HasPlayerUnits = false;
            for (auto& SE : g.Entities.GetAll())
            {
                if (SE.PlayerId == PlayerId &&
                    (SE.Kind == Game::SokolDemo::EntityKind::Building ||
                     SE.Kind == Game::SokolDemo::EntityKind::Unit))
                {
                    HasPlayerUnits = true;
                    break;
                }
            }
            if (HasPlayerUnits)
                return;

            const decltype(g.GameMapData.StartLocations)::value_type* ChosenStart = nullptr;
            for (auto& SL : g.GameMapData.StartLocations)
            {
                if (SL.PlayerId == PlayerId)
                {
                    ChosenStart = &SL;
                    break;
                }
            }
            if (!g.GameMapData.StartLocations.empty() && ChosenStart == nullptr)
            {
                size_t FallbackIndex = std::min<size_t>((size_t)PlayerId, g.GameMapData.StartLocations.size() - 1);
                ChosenStart = &g.GameMapData.StartLocations[FallbackIndex];
            }
            if (!ChosenStart)
                return;

            float WX = (float)(ChosenStart->TileX * 32 + 16);
            float WY = (float)(ChosenStart->TileY * 32 + 16);
            int CcImageId = (int)g.Cache.ResolveImageID(ChkCommandCenter);
            int ScvImageId = (int)g.Cache.ResolveImageID(ChkScv);

            int CcId = g.Entities.SpawnCommandCenter(PlayerId, (int)WX, (int)WY, ChkCommandCenter, 0);
            g.Entities.GetEntity(CcId).ImageId = CcImageId;

            for (int I = 0; I < 4; I++)
            {
                int ScvId = g.Entities.SpawnScv(PlayerId, (int)WX - 30 + I * 20, (int)WY + 50, ChkScv, 0);
                g.Entities.GetEntity(ScvId).ImageId = ScvImageId;
            }

            g.EntityRen.ResolveFrames(g.Entities);
            Core::Utils::Logger::Info("SokolMain",
                "Spawned demo base for player " + std::to_string(PlayerId) + " at (" +
                std::to_string((int)WX) + "," + std::to_string((int)WY) + ")");
        };
        SpawnStartBaseIfMissing(0);
        SpawnStartBaseIfMissing(1);

        // 5. Initialize ECS systems
        if (g.MapLoaded)
        {
            // Build ECS GameMap from sokol map data
            g.EcsMap.Generate(g.GameMapData.Width, g.GameMapData.Height);
            auto& Cache = g.Cache;
            if (!g.GameMapData.TilesetName.empty())
            {
                auto* Td = Cache.GetTilesetData(g.GameMapData.TilesetName);
                if (Td && Td->Loaded)
                {
                    g.EcsMap.ApplyTilesetData(&Td->Cv5, &Td->Vf4);
                }
            }

            // Register systems
            auto& SysMgr = g.EcsSysMgr;
            auto CmdSys = std::make_unique<Game::Systems::CommandSystem>();
            CmdSys->Init(&SysMgr);
            SysMgr.RegisterSystem(std::move(CmdSys));
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::MovementSystem>(SysMgr));
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::HarvestSystem>(SysMgr));
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::BuildingSystem>(SysMgr));
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::ProductionSystem>(SysMgr));
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::CombatSystem>(SysMgr));
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::UpgradeSystem>(SysMgr));
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::FogOfWarSystem>(SysMgr, &g.EcsMap));

            // Zerg system (creep + larvae)
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::ZergSystem>(SysMgr, &g.EcsMap));
            SysMgr.RegisterSystem(std::make_unique<Game::Systems::ProtossSystem>(SysMgr, &g.EcsMap));

            // AI system (player 1 attacks player 0)
            bool HasPlayer1 = false;
            for (auto& SE : g.Entities.GetAll())
                if (SE.PlayerId == 1) { HasPlayer1 = true; break; }
            if (HasPlayer1)
            {
                SysMgr.RegisterSystem(std::make_unique<Game::Systems::AIBrain>(SysMgr, 1, 0));
                Core::Utils::Logger::Info("SokolMain", "AI Brain registered for player 1");
            }

            // Set static map pointers
            Game::Systems::CommandSystem::SetMap(&g.EcsMap);
            Game::Systems::BuildingSystem::SetMap(&g.EcsMap);

            // Create ECS entities matching all sokol entities (1:1 ID mapping).
            // Sokol entity at index I maps to ECS entity ID (I+1).
            auto& EM = SysMgr.GetEntityManager();
            auto& SokolEnts = g.Entities.GetAll();
            for (size_t I = 0; I < SokolEnts.size(); I++)
            {
                auto& SE = SokolEnts[I];
                int Eid = EM.CreateEntity();
                auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(Eid);
                Trans.Position = Core::Math::Vec2(SE.X, SE.Y);
                auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(Eid);
                Unit.HitPoints = SE.HitPoints > 0 ? SE.HitPoints : 60;
                Unit.MaxHitPoints = SE.MaxHitPoints > 0 ? SE.MaxHitPoints : 60;
                Unit.Speed = IsMineralUnit(SE.ChkUnitId) || IsGeyserUnit(SE.ChkUnitId) ? 0.0f : 2.8f;
                Unit.SightRange = 8;
                Unit.ChkUnitID = SE.ChkUnitId;
                if (int TerranDefID = Game::Races::GetTerranDefIDByChkUnitID(SE.ChkUnitId);
                    TerranDefID >= 0)
                {
                    auto Def = Game::Races::GetUnitDefByID(TerranDefID);
                    Unit.UnitRace = Def.RaceType;
                    Unit.SizeType = Def.SizeType;
                    Unit.Armor = Def.Armor;
                    Unit.Speed = Def.Speed;
                    Unit.SightRange = Def.SightRange;
                    Unit.SupplyCost = Def.SupplyCost;
                }
                auto& Player = EM.AddComponent<Game::Components::PlayerComponent>(Eid);
                Player.PlayerID = SE.PlayerId;
                auto& RenderComp = EM.AddComponent<Game::Components::RenderComponent>(Eid);
                RenderComp.ImageID = (uint32_t)SE.ImageId;
                RenderComp.FrameIndex = 0;
                RenderComp.TotalFrames = SE.FrameCount > 0 ? SE.FrameCount : 1;

                if (IsMineralUnit(SE.ChkUnitId))
                {
                    auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(Eid);
                    Res.Type = Game::Components::ResourceType::Minerals;
                    Res.Amount = SE.ResourceAmount > 0 ? SE.ResourceAmount : 1500;
                    Res.MaxAmount = Res.Amount;
                }
                else if (IsGeyserUnit(SE.ChkUnitId))
                {
                    auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(Eid);
                    Res.Type = Game::Components::ResourceType::VespeneGas;
                    Res.Amount = SE.ResourceAmount > 0 ? SE.ResourceAmount : 5000;
                    Res.MaxAmount = Res.Amount;
                    auto& Build = EM.AddComponent<Game::Components::BuildingComponent>(Eid);
                    Build.IsBuilding = true;
                    Build.UnderConstruction = false;
                    Unit.MaxHitPoints = SE.MaxHitPoints > 0 ? SE.MaxHitPoints : 500;
                    Unit.HitPoints = SE.HitPoints > 0 ? SE.HitPoints : 500;
                }
                else if (IsBuildingChkId(SE.ChkUnitId))
                {
                    // Building (Terran or Zerg)
                    auto& Build = EM.AddComponent<Game::Components::BuildingComponent>(Eid);
                    Build.IsBuilding = true;
                    Build.UnderConstruction = false;
                    Build.IsOrganic = IsOrganicBuilding(SE.ChkUnitId);
                    Build.RequiresPylon = RequiresPylonChkId(SE.ChkUnitId);
                    Build.TotalBuildTime = 120;
                    Build.BuildingDefID = Game::Races::GetTerranDefIDByChkUnitID(SE.ChkUnitId);
                    Unit.MaxHitPoints = SE.MaxHitPoints > 0 ? SE.MaxHitPoints : 1000;
                    Unit.HitPoints = SE.HitPoints > 0 ? SE.HitPoints : 1000;
                }
                else
                {
                    // All units get move/command components (AI needs them too)
                    auto& Cmd = EM.AddComponent<Game::Components::CommandComponent>(Eid);
                    Cmd.CurrentCommand = Game::Components::CommandType::Stop;
                    auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(Eid);
                    Path.NodeTolerance = 8.0f;
                }
            }

            g.EcsInitialized = true;
            Core::Utils::Logger::Info("SokolMain",
                "ECS initialized: " + std::to_string(EM.GetEntityCount()) + " entities");

            // Start campaign
            g.Campaign.Init();
            g.CampaignActive = true;
        }

        return true;
    }
}

static void InitCb()
{
    std::cout << "StarcraftSokol: init" << std::endl;
    Core::Platform::Initialize();
    if (!Render::Initialize())
    {
        std::cerr << "Renderer init failed" << std::endl;
        return;
    }
    if (!Render::g_Draw2D.Init())
    {
        std::cerr << "Draw2D init failed" << std::endl;
        return;
    }

    g.Particles.Init(800);
    g.Audio.Init();

    if (InitGameSystems())
    {
        g.Initialized = true;
    }
    else
    {
        std::cerr << "Game init failed; will render blank scene" << std::endl;
        g.Initialized = true; // still render so we get a window
    }

    g.FrameCount = 0;
    g.FpsTimer = 0.0f;
    g.FpsFrames = 0;
}

static void FrameCb()
{
    Core::Platform::SetDeltaTime(static_cast<float>(sapp_frame_duration()));
    Core::Platform::ProcessInput();

    float Dt = Core::Platform::GetDeltaTime();

    if (g.Initialized)
    {
        // 1. Update camera viewport to match current window size
        g.Camera.SetViewport(Core::Platform::GetWindowWidth(),
                             Core::Platform::GetWindowHeight());
        g.CameraCtrl.SetTopBlockedPx((float)g.Hud.GetResourceBarHeight());
        g.CameraCtrl.SetBottomBlockedPx((float)g.Hud.GetHudHeight(
            g.Camera.GetViewportWidth(), g.Camera.GetViewportHeight()));
        g.CameraCtrl.Update(Dt);

        const int ViewportWidth = g.Camera.GetViewportWidth();
        const int ViewportHeight = g.Camera.GetViewportHeight();
        const int MX = (int)Core::Platform::GetMouseX();
        const int MY = (int)Core::Platform::GetMouseY();

        if (g.Hud.HasPendingTarget() && Core::Platform::IsKeyPressed(SAPP_KEYCODE_ESCAPE))
            g.Hud.CancelPendingTarget();

        // 2. Update selection
        bool BlockSelectionUpdate = false;
        if (Core::Platform::IsMouseButtonPressed(0))
        {
            float DummyWX = 0.0f;
            float DummyWY = 0.0f;
            if (g.Hud.CheckMinimapClick(MX, MY, ViewportWidth, ViewportHeight,
                                        g.GameMapData.Width, g.GameMapData.Height,
                                        DummyWX, DummyWY))
            {
                BlockSelectionUpdate = true;
            }
            else if (g.Hud.CheckCommandPanelClick(MX, MY, ViewportWidth, ViewportHeight) >= 0)
            {
                BlockSelectionUpdate = true;
            }
            else if (g.Hud.IsPointInHudBand(MX, MY, ViewportWidth, ViewportHeight))
            {
                BlockSelectionUpdate = true;
            }
            else if (g.Hud.HasPendingTarget() && IsMapInteractionPoint(MX, MY, ViewportWidth, ViewportHeight))
            {
                BlockSelectionUpdate = true;
            }
            else if (g.EcsInitialized && Core::Platform::IsKeyDown(65) &&
                     IsMapInteractionPoint(MX, MY, ViewportWidth, ViewportHeight))
            {
                BlockSelectionUpdate = true;
            }
        }

        if (BlockSelectionUpdate)
            g.Selector.CancelPointerGesture();
        else
            g.Selector.Update(g.Entities, g.Camera);

        // 3. Click handling: minimap jump, command panel, right-click commands
        if (Core::Platform::IsMouseButtonPressed(0))
        {
            bool ConsumedLeftClick = false;
            float WX = 0.0f;
            float WY = 0.0f;
            if (g.Hud.CheckMinimapClick(MX, MY, ViewportWidth, ViewportHeight,
                                        g.GameMapData.Width, g.GameMapData.Height,
                                        WX, WY))
            {
                g.Camera.SetPosition(WX, WY);
                g.Selector.CancelPointerGesture();
                ConsumedLeftClick = true;
            }

            int CmdIdx = g.Hud.CheckCommandPanelClick(MX, MY, ViewportWidth, ViewportHeight);
            if (CmdIdx >= 0 && g.EcsInitialized)
            {
                auto& Defs = g.Hud.GetButtonDefs();
                if (CmdIdx < 9 && !Defs[CmdIdx].Label.empty())
                {
                    auto& EM = g.EcsSysMgr.GetEntityManager();
                    const auto& Selected = g.Selector.GetSelected();
                    std::vector<int> EcsIds;
                    for (int SEid : Selected)
                    {
                        int Eid = SEid;
                        if (EM.IsAlive(Eid))
                            EcsIds.push_back(Eid);
                    }
                    if (!EcsIds.empty())
                    {
                        const auto& CmdDef = Defs[CmdIdx];
                        if (CmdDef.Enabled)
                        {
                            switch (CmdDef.Kind)
                            {
                                case Game::SokolDemo::UI::ActionKind::Build:
                                {
                                    auto BuildDef = Game::Races::GetUnitDefByID(CmdDef.TargetUnitDefID);
                                    if (BuildDef.IsAddon)
                                    {
                                        Game::Systems::BuildingSystem::StartBuild(
                                            EM, EcsIds[0], CmdDef.TargetUnitDefID,
                                            Game::Systems::BuildTarget{});
                                    }
                                    break;
                                }
                                case Game::SokolDemo::UI::ActionKind::Train:
                                    Game::Systems::ProductionSystem::StartTrain(
                                        EM, EcsIds[0], CmdDef.TargetUnitDefID);
                                    break;
                                case Game::SokolDemo::UI::ActionKind::Research:
                                {
                                    Game::Systems::UpgradeSystem::StartResearch(
                                        EM, EcsIds[0], CmdDef.ResearchId);
                                    break;
                                }
                                case Game::SokolDemo::UI::ActionKind::Command:
                                {
                                    auto ApplyImmediateCommand =
                                        [&](Game::Components::CommandType CommandType)
                                    {
                                        for (int Eid : EcsIds)
                                        {
                                            auto* Command = EM.GetComponent<Game::Components::CommandComponent>(Eid);
                                            if (!Command)
                                                Command = &EM.AddComponent<Game::Components::CommandComponent>(Eid);
                                            Command->CurrentCommand = CommandType;
                                            Command->TargetEntityID = -1;
                                            auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(Eid);
                                            if (Path)
                                            {
                                                Path->Path.clear();
                                                Path->PathIndex = 0;
                                            }
                                        }
                                    };

                                    if (CmdDef.CommandId == Game::SokolDemo::UI::HudCommandId::Stop)
                                        ApplyImmediateCommand(Game::Components::CommandType::Stop);
                                    else if (CmdDef.CommandId == Game::SokolDemo::UI::HudCommandId::Hold)
                                        ApplyImmediateCommand(Game::Components::CommandType::HoldPosition);
                                    break;
                                }
                                default:
                                    break;
                            }
                        }
                    }
                }
                g.Hud.OnButtonInvoked(CmdIdx);
                g.Selector.CancelPointerGesture();
                ConsumedLeftClick = true;
            }

            if (!ConsumedLeftClick && g.EcsInitialized && g.Hud.HasPendingTarget() &&
                IsMapInteractionPoint(MX, MY, ViewportWidth, ViewportHeight))
            {
                g.Camera.ScreenToWorld((float)MX, (float)MY, WX, WY);
                Core::Math::Vec2 Target(WX, WY);
                auto& EM = g.EcsSysMgr.GetEntityManager();
                auto EcsIds = CollectSelectedAliveEcsIds(EM);
                if (!EcsIds.empty())
                {
                    auto ClickTarget = ResolveClickTargetContext(EM, EcsIds, Target);
                    switch (g.Hud.GetPendingTargetMode())
                    {
                        case Game::SokolDemo::UI::PendingTargetMode::BuildPlacement:
                            Game::Systems::BuildingSystem::StartBuild(
                                EM, EcsIds[0], g.Hud.GetPendingBuildDefID(),
                                Game::Systems::BuildTarget::FromPosition(Target));
                            break;
                        case Game::SokolDemo::UI::PendingTargetMode::BuildRefinery:
                            if (ClickTarget.ResourceEntityID >= 0)
                            {
                                Game::Systems::BuildingSystem::StartBuild(
                                    EM, EcsIds[0], g.Hud.GetPendingBuildDefID(),
                                    Game::Systems::BuildTarget::FromEntity(ClickTarget.ResourceEntityID));
                            }
                            break;
                        case Game::SokolDemo::UI::PendingTargetMode::Harvest:
                            if (ClickTarget.ResourceEntityID >= 0)
                                Game::Systems::HarvestSystem::IssueHarvest(EM, EcsIds, ClickTarget.ResourceEntityID);
                            break;
                        case Game::SokolDemo::UI::PendingTargetMode::Move:
                            Game::Systems::CommandSystem::IssueMove(EM, EcsIds, Target);
                            break;
                        case Game::SokolDemo::UI::PendingTargetMode::AttackMove:
                            Game::Systems::CombatSystem::IssueAttackMove(EM, EcsIds, Target);
                            break;
                        case Game::SokolDemo::UI::PendingTargetMode::Patrol:
                            IssuePatrolCommand(EM, EcsIds, Target);
                            break;
                        case Game::SokolDemo::UI::PendingTargetMode::Follow:
                            IssueFollowCommand(EM, EcsIds, ClickTarget.AnyEntityID);
                            break;
                        default:
                            break;
                    }
                }
                g.Hud.CancelPendingTarget();
                g.Selector.CancelPointerGesture();
            }
            // A-move: A key + left-click on map
            else if (!ConsumedLeftClick && g.EcsInitialized && Core::Platform::IsKeyDown(65))
            {
                if (IsMapInteractionPoint(MX, MY, ViewportWidth, ViewportHeight))
                {
                    g.Camera.ScreenToWorld((float)MX, (float)MY, WX, WY);
                    auto& EM = g.EcsSysMgr.GetEntityManager();
                    auto EcsIds = CollectSelectedAliveEcsIds(EM);
                    if (!EcsIds.empty())
                    {
                        Game::Systems::CombatSystem::IssueAttackMove(EM, EcsIds, {WX, WY});
                    }
                }
            }
        }

        if (g.EcsInitialized && Core::Platform::IsMouseButtonReleased(1))
        {
            if (g.Hud.HasPendingTarget())
            {
                g.Hud.CancelPendingTarget();
                g.Selector.CancelPointerGesture();
            }
            else if (IsMapInteractionPoint(MX, MY, ViewportWidth, ViewportHeight))
            {
                auto& EM = g.EcsSysMgr.GetEntityManager();
                auto EcsIds = CollectSelectedAliveEcsIds(EM);
                if (!EcsIds.empty())
                {
                    float WX = 0.0f;
                    float WY = 0.0f;
                    g.Camera.ScreenToWorld((float)MX, (float)MY, WX, WY);
                    Core::Math::Vec2 Target(WX, WY);
                    auto ClickTarget = ResolveClickTargetContext(EM, EcsIds, Target);

                    if (ClickTarget.EnemyEntityID >= 0)
                    {
                        Game::Systems::CombatSystem::IssueAttack(EM, EcsIds, ClickTarget.EnemyEntityID);
                    }
                    else if (ClickTarget.ResourceEntityID >= 0)
                    {
                        auto Harvesters = FilterHarvesters(EM, EcsIds);
                        if (!Harvesters.empty())
                            Game::Systems::HarvestSystem::IssueHarvest(EM, Harvesters, ClickTarget.ResourceEntityID);
                    }
                    else
                    {
                        Game::Systems::CommandSystem::IssueMove(EM, EcsIds, Target);
                    }
                }
            }
        }

        // Command keyboard shortcuts (S=Stop, H=Hold, P=Patrol, F=Follow)
        if (g.EcsInitialized)
        {
            auto& EM = g.EcsSysMgr.GetEntityManager();
            const auto& Selected = g.Selector.GetSelected();
            if (!Selected.empty())
            {
                std::vector<int> EcsIds;
                for (int SEid : Selected)
                {
                    int Eid = SEid;
                    if (EM.IsAlive(Eid)) EcsIds.push_back(Eid);
                }
                if (!EcsIds.empty())
                {
                    auto SetAllCmd = [&](Game::Components::CommandType Cmd) {
                        for (int Eid : EcsIds)
                        {
                            auto* C = EM.GetComponent<Game::Components::CommandComponent>(Eid);
                            if (C) { C->CurrentCommand = Cmd;
                                     C->TargetEntityID = -1; }
                            auto* P = EM.GetComponent<Game::Components::PathfindingComponent>(Eid);
                            if (P) { P->Path.clear(); P->PathIndex = 0; }
                        }
                    };
                    if (Core::Platform::IsKeyPressed(83))       // S = Stop
                        SetAllCmd(Game::Components::CommandType::Stop);
                    else if (Core::Platform::IsKeyPressed(72))  // H = Hold Position
                        SetAllCmd(Game::Components::CommandType::HoldPosition);
                    else if (Core::Platform::IsKeyPressed(80))  // P = Patrol (click sets destination)
                    {
                        // Set command, then right-click will set the patrol endpoint
                        for (int Eid : EcsIds)
                        {
                            auto* C = EM.GetComponent<Game::Components::CommandComponent>(Eid);
                            if (C) C->CurrentCommand = Game::Components::CommandType::Patrol;
                        }
                    }
                    else if (Core::Platform::IsKeyPressed(70))  // F = Follow (click on target)
                    {
                        for (int Eid : EcsIds)
                        {
                            auto* C = EM.GetComponent<Game::Components::CommandComponent>(Eid);
                            if (C) C->CurrentCommand = Game::Components::CommandType::Follow;
                        }
                    }
                }
            }
        }

        if (g.EcsInitialized && Core::Platform::IsKeyPressed(SAPP_KEYCODE_F6))
            SpawnTerranAuditFormation();

        // 3b. Campaign update
        if (g.CampaignActive)
            g.Campaign.Update(g.EcsSysMgr.GetEntityManager(), Dt);

        // 4. Run ECS game logic systems
        if (g.EcsInitialized)
        {
            g.EcsSysMgr.UpdateAll(Dt);

            // Sync player resources to HUD
            {
                auto& Res = Game::Systems::HarvestSystem::GetResources(0);
                g.Hud.MineralCount = Res.Minerals;
                g.Hud.GasCount = Res.VespeneGas;
                g.Hud.SupplyUsed = Res.SupplyUsed;
                g.Hud.SupplyMax = Res.SupplyMax;
            }

            // Sync ECS to sokol entities for rendering
            SyncSokolEntitiesFromEcs();
            g.Selector.PruneAgainst(g.Entities);
        }

        // Spawn projectiles from combat events
        if (g.EcsInitialized)
        {
            auto& EM = g.EcsSysMgr.GetEntityManager();
            auto CombatEnts = EM.GetEntitiesWith<
                Game::Components::TransformComponent,
                Game::Components::CombatComponent>();
            for (int Eid : CombatEnts)
            {
                auto* Cbt = EM.GetComponent<Game::Components::CombatComponent>(Eid);
                auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(Eid);
                if (!Cbt || !Trans) continue;
                // If cooldown was just reset to full, the unit just fired
                if (Cbt->CooldownRemaining > 0.0f &&
                    std::abs(Cbt->CooldownRemaining - Cbt->AttackCooldown) < 0.001f)
                {
                    if (Cbt->TargetEntityID >= 0)
                    {
                        auto* TTrans = EM.GetComponent<Game::Components::TransformComponent>(Cbt->TargetEntityID);
                        if (TTrans)
                        {
                            State::Projectile P;
                            P.SX = Trans->Position.X;
                            P.SY = Trans->Position.Y;
                            P.TX = TTrans->Position.X;
                            P.TY = TTrans->Position.Y;
                            P.Timer = 0.0f;
                            P.Duration = 0.3f;
                            P.Active = true;
                            g.Projectiles.push_back(P);
                            // Spawn damage spark at target
                            g.Particles.EmitDamageSpark(TTrans->Position.X, TTrans->Position.Y);
                            g.Audio.Play(Core::Audio::SoundEffect::WeaponFire);
                        }
                    }
                }
            }
        }
        // Update particles
        g.Particles.Update(Dt);

        // Update + cull expired projectiles
        for (auto It = g.Projectiles.begin(); It != g.Projectiles.end(); )
        {
            It->Timer += Dt;
            if (It->Timer >= It->Duration)
                It = g.Projectiles.erase(It);
            else
                ++It;
        }

        // 5. Animate entities
        g.EntityRen.UpdateAnimation(g.Entities, Dt);
        Render::BeginFrame();
        Render::g_Draw2D.BeginFrame(g.Camera);
        if (g.MapLoaded)
        {
            g.Tilemap.Render(g.GameMapData);
            g.EntityRen.SetCamera(g.Camera.GetX(), g.Camera.GetY(),
                                   (float)g.Camera.GetViewportWidth(),
                                   (float)g.Camera.GetViewportHeight(),
                                   g.Camera.GetZoom());
            g.EntityRen.Render(g.Entities, g.Selector.GetSelected());

            // Render particles
            g.Particles.Render();

            // Render projectiles
            for (auto& P : g.Projectiles)
            {
                float T = P.Timer / P.Duration;
                float CX = P.SX + (P.TX - P.SX) * T;
                float CY = P.SY + (P.TY - P.SY) * T;
                float Radius = 3.0f;
                Render::g_Draw2D.DrawRect(CX - Radius, CY - Radius, Radius * 2, Radius * 2,
                    Render::Color(255, 200, 50, 220));
            }
        }
        Render::g_Draw2D.EndFrame();

        // 6. HUD on top of everything (screen-space, no camera)
        g.Hud.UpdateContext(
            g.Entities,
            g.Selector.GetSelected(),
            g.EcsInitialized ? &g.EcsSysMgr.GetEntityManager() : nullptr);
        Render::g_Draw2D.BeginFrameScreen();
        g.Hud.Draw(g.Entities, g.Selector.GetSelected(), g.GameMapData.Width, g.GameMapData.Height,
                   g.Camera.GetViewportWidth(), g.Camera.GetViewportHeight());
        // Selection rectangle (dragging)
        if (g.Selector.IsDragging())
        {
            float SX, SY, SW, SH;
            g.Selector.GetDragRectScreen(SX, SY, SW, SH);
            Render::g_Draw2D.DrawRect(SX, SY, SW, SH, Render::Color(80, 200, 80, 50));
            Render::g_Draw2D.DrawRectLines(SX, SY, SW, SH, Render::Color(80, 220, 80, 220), 1.0f);
        }
        // Campaign overlay
        if (g.CampaignActive)
        {
            auto& C = g.Campaign;
            int VW = g.Camera.GetViewportWidth();
            int VH = g.Camera.GetViewportHeight();
            // Objectives
            if (C.GetState() == Game::Campaign::CampaignState::Playing)
            {
                int OY = 24;
                for (auto& O : C.GetObjectives())
                {
                    std::string Text = O.Complete ? "[X] " : "[ ] ";
                    Text += O.Description;
                    Render::g_Draw2D.DrawText(Text, 8.0f, (float)OY, 1.5f, 
                        O.Complete ? Render::Color(100, 220, 100, 255) : Render::Color(220, 220, 200, 255));
                    OY += 14;
                }
            }
            // Victory overlay
            if (C.GetState() == Game::Campaign::CampaignState::Victory)
            {
                Render::g_Draw2D.DrawRect(0, 0, (float)VW, (float)VH, Render::Color(0, 80, 0, 160));
                Render::g_Draw2D.DrawText("VICTORY", (float)(VW / 2 - 60), (float)(VH / 2 - 20), 4.0f,
                    Render::Color(100, 255, 100, 255));
                Render::g_Draw2D.DrawText("Press R to restart", (float)(VW / 2 - 60), (float)(VH / 2 + 20), 2.0f,
                    Render::Color(200, 220, 200, 255));
                if (Core::Platform::IsKeyPressed(82)) C.Reset(); // R = restart
            }
            // Defeat overlay
            if (C.GetState() == Game::Campaign::CampaignState::Defeat)
            {
                Render::g_Draw2D.DrawRect(0, 0, (float)VW, (float)VH, Render::Color(80, 0, 0, 160));
                Render::g_Draw2D.DrawText("DEFEAT", (float)(VW / 2 - 60), (float)(VH / 2 - 20), 4.0f,
                    Render::Color(255, 80, 80, 255));
                Render::g_Draw2D.DrawText("Press R to restart", (float)(VW / 2 - 60), (float)(VH / 2 + 20), 2.0f,
                    Render::Color(220, 200, 200, 255));
                if (Core::Platform::IsKeyPressed(82)) C.Reset(); // R = restart
            }
        }

        const int MouseX = (int)Core::Platform::GetMouseX();
        const int MouseY = (int)Core::Platform::GetMouseY();
        const CursorRenderState CursorState = ResolveCursorState(MouseX, MouseY);
        Core::Platform::SetMouseCursorVisible(!CursorState.UseGameCursor);
        if (CursorState.UseGameCursor)
        {
            if (const auto* Frames = g.Cache.GetCursorFrames(CursorName(CursorState.Kind));
                Frames && !Frames->empty())
            {
                size_t FrameIndex = 0;
                const float CursorFps = CursorAnimationFps(CursorState.Kind);
                if (Frames->size() > 1 && CursorFps > 0.0f)
                {
                    FrameIndex =
                        (size_t)(Core::Platform::GetAbsoluteTime() * CursorFps) % Frames->size();
                }
                const auto& Frame = (*Frames)[FrameIndex];
                if (Frame.Valid())
                {
                    Render::g_Draw2D.DrawSpriteFromImage(
                        Frame,
                        (float)MouseX - CursorState.HotspotX,
                        (float)MouseY - CursorState.HotspotY,
                        (float)Frame.Width,
                        (float)Frame.Height,
                        Render::Colors::White());
                }
            }
        }
        Render::g_Draw2D.EndFrame();

        Render::EndFrame();
    }
    else
    {
        Render::BeginFrame();
        Render::EndFrame();
    }

    g.FrameCount++;
    g.FpsTimer += Dt;
    g.FpsFrames++;
    if (g.FpsTimer >= 1.0f)
    {
        g.FpsValue = g.FpsFrames;
        std::cout << "fps=" << g.FpsValue
                  << " win=" << Core::Platform::GetWindowWidth() << "x" << Core::Platform::GetWindowHeight()
                  << " frame=" << g.FrameCount
                  << " mouse=" << Core::Platform::GetMouseX() << "," << Core::Platform::GetMouseY()
                  << std::endl;
        g.FpsTimer = 0.0f;
        g.FpsFrames = 0;
    }
}

static void EventCb(const sapp_event* Event)
{
    Core::Platform::HandleEvent(Event);
}

static void CleanupCb()
{
    std::cout << "StarcraftSokol: cleanup" << std::endl;
    g.Hud.Shutdown();
    Render::g_Draw2D.Shutdown();
    Render::Shutdown();
    Core::Platform::Shutdown();
}

sapp_desc sokol_main(int argc, char* argv[])
{
    (void)argc; (void)argv;
    sapp_desc Desc = {};
    Desc.init_cb = InitCb;
    Desc.frame_cb = FrameCb;
    Desc.event_cb = EventCb;
    Desc.cleanup_cb = CleanupCb;
    Desc.width = 1280;
    Desc.height = 720;
    Desc.high_dpi = true;
    Desc.sample_count = 4;
    Desc.window_title = "StarCraft Sokol Demo";
    Desc.icon.sokol_default = true;
    Desc.logger.func = slog_func;
    return Desc;
}
