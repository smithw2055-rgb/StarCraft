#pragma once

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
#include "Main/CampaignRuntime.h"
#include "Main/GameCommandDispatchRuntime.h"
#include "Main/GameSession.h"
#include "Main/GuiRuntime.h"

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

namespace InteractionSmoke
{
    struct FullRosterSpawnResult
    {
        std::vector<int> EntityIDs;
        int BuildingCount = 0;
        int UnitCount = 0;
    };

    inline bool IsEnabled()
    {
        const char* Value = std::getenv("STARCRAFT_INTERACTION_SMOKE");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    inline bool IsFullRosterOnly()
    {
        const char* Value = std::getenv("STARCRAFT_FULL_ROSTER_SMOKE");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    inline void Fail(std::string& Error, const std::string& Message)
    {
        if (Error.empty())
            Error = Message;
    }

    inline bool ContainsEntity(const std::vector<int>& Entities, int EntityID)
    {
        return std::find(Entities.begin(), Entities.end(), EntityID) != Entities.end();
    }

    inline bool HasValidSpriteFrames(ClientRuntime& Client,
                                     uint32_t ImageID,
                                     const std::string& Label,
                                     std::string& Error)
    {
        auto Frames = Client.GetSpriteAssets().GetDatImageFrames(ImageID);
        if (!Frames || Frames->empty())
        {
            Fail(Error, Label + " has no DAT image sprite frames; image=" +
                            std::to_string(ImageID));
            return false;
        }

        const auto& FirstFrame = (*Frames)[0];
        if (!FirstFrame.Valid() || FirstFrame.Width <= 0 || FirstFrame.Height <= 0)
        {
            Fail(Error, Label + " has invalid GPU sprite frame; image=" +
                            std::to_string(ImageID) +
                            " size=" + std::to_string(FirstFrame.Width) +
                            "x" + std::to_string(FirstFrame.Height));
            return false;
        }
        return true;
    }

    inline bool VerifyRenderableEntity(Core::ECS::EntityManager& EM,
                                       ClientRuntime& Client,
                                       int EntityID,
                                       const std::string& Label,
                                       std::string& Error)
    {
        auto* Render = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
        if (!Render || !Render->Visible)
        {
            Fail(Error, Label + " missing visible render component");
            return false;
        }

        if (!HasValidSpriteFrames(Client, Render->ImageID, Label, Error))
            return false;
        if (Render->SubunitImageID != UINT32_MAX &&
            !HasValidSpriteFrames(Client, Render->SubunitImageID, Label + " subunit", Error))
        {
            return false;
        }
        return true;
    }

    inline bool VerifyIScriptAnim(uint32_t ImageID,
                                  int AnimID,
                                  const std::string& Label,
                                  std::string& Error)
    {
        if (!Game::Systems::AnimationSystem::HasIScriptAnimationForImage((int)ImageID, AnimID))
        {
            Fail(Error, Label + " missing IScript animation " +
                            std::to_string(AnimID) +
                            " for image=" + std::to_string(ImageID));
            return false;
        }
        return true;
    }

    inline void TickSystems(GameSession& Session, int Frames, float Dt)
    {
        for (int I = 0; I < Frames; ++I)
            Session.UpdateSystems(Dt);
    }

    inline bool IsAliveUnit(Core::ECS::EntityManager& EM, int EntityID)
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        return Unit && Unit->HitPoints > 0;
    }

    inline bool IsOwnedBy(Core::ECS::EntityManager& EM, int EntityID, int PlayerID)
    {
        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        return Player && Player->PlayerID == PlayerID;
    }

    inline void EnsurePlayerResources(int PlayerID)
    {
        auto& Resources = Game::Systems::HarvestSystem::GetResources(PlayerID);
        Resources.Minerals = std::max(Resources.Minerals, 2500);
        Resources.VespeneGas = std::max(Resources.VespeneGas, 1000);
        Resources.SupplyMax = std::max(Resources.SupplyMax, 80);
    }

    inline int CountOwnedUnitsByChk(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID)
    {
        int Count = 0;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (Unit && Unit->ChkUnitID == ChkUnitID && IsOwnedBy(EM, EntityID, PlayerID))
                ++Count;
        }
        return Count;
    }

    inline int FindNewestOwnedUnitByChk(Core::ECS::EntityManager& EM, int PlayerID, int ChkUnitID)
    {
        int BestID = -1;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (Unit && Unit->ChkUnitID == ChkUnitID && IsOwnedBy(EM, EntityID, PlayerID))
                BestID = std::max(BestID, EntityID);
        }
        return BestID;
    }

    inline int FindNewestUnitByChk(Core::ECS::EntityManager& EM, int ChkUnitID)
    {
        int BestID = -1;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::UnitComponent>())
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (Unit && Unit->ChkUnitID == ChkUnitID)
                BestID = std::max(BestID, EntityID);
        }
        return BestID;
    }

    inline int FindBuildingByDefAndBuilder(Core::ECS::EntityManager& EM,
                                           int BuildingDefID,
                                           int BuilderID)
    {
        int BestID = -1;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
            if (Building &&
                Building->BuildingDefID == BuildingDefID &&
                Building->HasBuilderUnit(BuilderID))
            {
                BestID = std::max(BestID, EntityID);
            }
        }
        return BestID;
    }

    inline const UI::CmdButtonDef* FindHudButtonByTarget(const UI::Hud& Hud,
                                                         UI::ActionKind Kind,
                                                         int TargetDefID,
                                                         int* OutIndex = nullptr)
    {
        const auto& Buttons = Hud.GetButtonDefs();
        for (int Index = 0; Index < (int)Buttons.size(); ++Index)
        {
            const auto& Button = Buttons[(size_t)Index];
            if (Button.Kind == Kind && Button.TargetUnitDefID == TargetDefID)
            {
                if (OutIndex)
                    *OutIndex = Index;
                return &Button;
            }
        }
        return nullptr;
    }

    inline const UI::CmdButtonDef* FindHudButtonByCommand(const UI::Hud& Hud,
                                                          UI::HudCommandId CommandID,
                                                          int* OutIndex = nullptr)
    {
        const auto& Buttons = Hud.GetButtonDefs();
        for (int Index = 0; Index < (int)Buttons.size(); ++Index)
        {
            const auto& Button = Buttons[(size_t)Index];
            if (Button.Kind == UI::ActionKind::Command && Button.CommandId == CommandID)
            {
                if (OutIndex)
                    *OutIndex = Index;
                return &Button;
            }
        }
        return nullptr;
    }

    inline const UI::CmdButtonDef* FindHudButtonByResearch(
        const UI::Hud& Hud,
        Game::Systems::TerranResearchId ResearchID,
        int* OutIndex = nullptr)
    {
        const auto& Buttons = Hud.GetButtonDefs();
        for (int Index = 0; Index < (int)Buttons.size(); ++Index)
        {
            const auto& Button = Buttons[(size_t)Index];
            if ((Button.Kind == UI::ActionKind::Research ||
                 Button.Kind == UI::ActionKind::Upgrade) &&
                Button.ResearchId == ResearchID)
            {
                if (OutIndex)
                    *OutIndex = Index;
                return &Button;
            }
        }
        return nullptr;
    }

    inline bool VerifyHudButtonIcon(UI::Hud& Hud,
                                    const UI::CmdButtonDef& Button,
                                    const std::string& Label,
                                    std::string& Error)
    {
        const std::string IconUri = Hud.GetButtonIconUri(Button);
        if (IconUri.empty())
        {
            Fail(Error,
                 Label + " HUD button did not resolve an icon URI; iconIndex=" +
                     std::to_string(Button.IconIndex) +
                     " command=" + std::to_string((int)Button.CommandId));
            return false;
        }
        return true;
    }

    inline bool VerifyGuiCommandClick(GuiRuntime& Gui,
                                      Core::ECS::EntityManager& EM,
                                      const std::vector<int>& SelectedIds,
                                      UI::Hud& Hud,
                                      int CommandIndex,
                                      const std::string& Label,
                                      std::string& Error)
    {
#if ENABLE_GUI_UI
        if (!Gui.MainGui.IsActive())
        {
            Core::Utils::Logger::Info(
                "InteractionSmoke",
                "Interaction smoke GUI HUD inactive; native HUD command path verified for " + Label);
            return true;
        }

        Gui.MainGui.Update(EM, SelectedIds, Hud, 0.0);
        const UI::ScreenRect Rect = Gui.MainGui.GetHost().GetCommandRect(CommandIndex);
        if (!Rect.IsValid())
        {
            Fail(Error, "GUI HUD command rect is invalid for " + Label);
            return false;
        }

        const float X = Rect.X + Rect.W * 0.5f;
        const float Y = Rect.Y + Rect.H * 0.5f;
        if (!Gui.MainGui.IsPointInHud(X, Y) ||
            Gui.MainGui.HitCommandButton(X, Y) != CommandIndex)
        {
            Fail(Error, "GUI HUD hit testing did not resolve " + Label);
            return false;
        }

        sapp_event Down = {};
        Down.type = SAPP_EVENTTYPE_MOUSE_DOWN;
        Down.mouse_button = SAPP_MOUSEBUTTON_LEFT;
        Down.mouse_x = X;
        Down.mouse_y = Y;
        Gui.MainGui.HandleEvent(&Down);

        sapp_event Up = {};
        Up.type = SAPP_EVENTTYPE_MOUSE_UP;
        Up.mouse_button = SAPP_MOUSEBUTTON_LEFT;
        Up.mouse_x = X;
        Up.mouse_y = Y;
        Gui.MainGui.HandleEvent(&Up);

        const int Clicked = Gui.MainGui.ConsumeClickedCommand();
        if (Clicked != CommandIndex)
        {
            Fail(Error,
                 "GUI HUD click did not produce expected command index for " + Label +
                     "; clicked=" + std::to_string(Clicked) +
                     " expected=" + std::to_string(CommandIndex));
            return false;
        }

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke GUI clicked command=" + std::to_string(CommandIndex) +
                " label=\"" + Label + "\"");
        return true;
#else
        (void)Gui;
        (void)EM;
        (void)SelectedIds;
        (void)Hud;
        (void)CommandIndex;
        (void)Label;
        (void)Error;
        return true;
#endif
    }

    inline int FindWorker(Core::ECS::EntityManager& EM, int PlayerID, int ResourceID)
    {
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (!Unit || !IsOwnedBy(EM, EntityID, PlayerID) || !IsAliveUnit(EM, EntityID))
                continue;
            if (Game::Systems::HarvestSystem::CanHarvest(EM, EntityID, ResourceID))
                return EntityID;
        }
        return -1;
    }

    inline int FindResource(Core::ECS::EntityManager& EM)
    {
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::ResourceComponent>())
        {
            auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityID);
            if (Resource && !Resource->IsDepleted && Resource->Amount > 0)
                return EntityID;
        }
        return -1;
    }

    inline int FindAttackTarget(Core::ECS::EntityManager& EM, int PlayerID)
    {
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            if (IsOwnedBy(EM, EntityID, PlayerID) || !IsAliveUnit(EM, EntityID))
                continue;
            if (EM.GetComponent<Game::Components::ResourceComponent>(EntityID))
                continue;
            return EntityID;
        }
        return -1;
    }

    inline int FindAttacker(Core::ECS::EntityManager& EM, int PlayerID, int TargetID)
    {
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent,
                                               Game::Components::CombatComponent>())
        {
            if (!IsOwnedBy(EM, EntityID, PlayerID) || !IsAliveUnit(EM, EntityID))
                continue;
            if (CanEntityAttackTarget(EM, EntityID, TargetID))
                return EntityID;
        }
        return -1;
    }

    inline bool FindWalkableNear(const GameSession& Session,
                                 Core::Math::Vec2 Center,
                                 int MinRadiusTiles,
                                 int MaxRadiusTiles,
                                 Core::Math::Vec2& Out)
    {
        const auto& Map = Session.GetEcsMap();
        int CenterX = 0;
        int CenterY = 0;
        Map.WorldToTile(Center, CenterX, CenterY);

        for (int Radius = MinRadiusTiles; Radius <= MaxRadiusTiles; ++Radius)
        {
            for (int DY = -Radius; DY <= Radius; ++DY)
            {
                for (int DX = -Radius; DX <= Radius; ++DX)
                {
                    if (std::max(std::abs(DX), std::abs(DY)) != Radius)
                        continue;

                    const int TileX = CenterX + DX;
                    const int TileY = CenterY + DY;
                    if (!Map.IsInBounds(TileX, TileY) || !Map.IsWalkable(TileX, TileY))
                        continue;

                    Out = Map.TileToWorld(TileX, TileY);
                    return true;
                }
            }
        }
        return false;
    }

    inline bool FindBuildPlacementNear(GameSession& Session,
                                       Core::ECS::EntityManager& EM,
                                       int BuildingDefID,
                                       Core::Math::Vec2 Center,
                                       int PlayerID,
                                       Core::Math::Vec2& Out)
    {
        const auto& Map = Session.GetEcsMap();

        // Automated construction must stay out of the depot-to-mineral lane.
        // Anchor the search on the nearest owned town hall and prefer the
        // opposite, outer side of the base before falling back to the caller's
        // local search center.
        Core::Math::Vec2 DepotPosition{};
        bool HasDepot = false;
        float BestDepotDistance = std::numeric_limits<float>::max();
        const std::array<int, 5> DepotDefIDs = {{
            Game::Races::GetUnitDefID("CommandCenter"),
            Game::Races::GetUnitDefID("Nexus"),
            Game::Races::GetUnitDefID("Hatchery"),
            Game::Races::GetUnitDefID("Lair"),
            Game::Races::GetUnitDefID("Hive")
        }};
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                                Game::Components::BuildingComponent,
                                                Game::Components::PlayerComponent>())
        {
            const auto* Player =
                EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            const auto* Building =
                EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
            const auto* Transform =
                EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            if (!Player || Player->PlayerID != PlayerID || !Building ||
                Building->UnderConstruction || !Transform ||
                std::find(DepotDefIDs.begin(), DepotDefIDs.end(),
                          Building->BuildingDefID) == DepotDefIDs.end())
            {
                continue;
            }
            const float Distance = Center.DistanceTo(Transform->Position);
            if (Distance < BestDepotDistance)
            {
                BestDepotDistance = Distance;
                DepotPosition = Transform->Position;
                HasDepot = true;
            }
        }

        Core::Math::Vec2 MineralCenter{};
        int MineralCount = 0;
        if (HasDepot)
        {
            constexpr float MineralClusterRadiusSq = 640.0f * 640.0f;
            for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                                    Game::Components::ResourceComponent>())
            {
                const auto* Resource =
                    EM.GetComponent<Game::Components::ResourceComponent>(EntityID);
                const auto* Transform =
                    EM.GetComponent<Game::Components::TransformComponent>(EntityID);
                if (!Resource || Resource->Type !=
                        Game::Components::ResourceType::Minerals ||
                    !Transform ||
                    (Transform->Position - DepotPosition).LengthSq() >
                        MineralClusterRadiusSq)
                {
                    continue;
                }
                MineralCenter += Transform->Position;
                ++MineralCount;
            }
        }

        if (HasDepot && MineralCount > 0)
        {
            MineralCenter = MineralCenter * (1.0f / (float)MineralCount);
            Core::Math::Vec2 TowardMinerals = MineralCenter - DepotPosition;
            if (TowardMinerals.LengthSq() > 1.0f)
            {
                TowardMinerals = TowardMinerals.Normalized();
                const Core::Math::Vec2 AwayFromMinerals = TowardMinerals * -1.0f;
                const Core::Math::Vec2 PreferredTarget =
                    DepotPosition + AwayFromMinerals * 224.0f;
                int DepotTileX = 0;
                int DepotTileY = 0;
                Map.WorldToTile(DepotPosition, DepotTileX, DepotTileY);
                bool FoundOutward = false;
                float BestScore = std::numeric_limits<float>::max();
                Core::Math::Vec2 BestOutward{};
                for (int Radius = 3; Radius <= 18; ++Radius)
                {
                    for (int DY = -Radius; DY <= Radius; ++DY)
                    {
                        for (int DX = -Radius; DX <= Radius; ++DX)
                        {
                            if (std::max(std::abs(DX), std::abs(DY)) != Radius)
                                continue;
                            const int TileX = DepotTileX + DX;
                            const int TileY = DepotTileY + DY;
                            if (!Map.IsInBounds(TileX, TileY))
                                continue;
                            const Core::Math::Vec2 Candidate =
                                Map.TileToWorld(TileX, TileY);
                            const Core::Math::Vec2 FromDepot =
                                Candidate - DepotPosition;
                            const float MineralProjection =
                                FromDepot.X * TowardMinerals.X +
                                FromDepot.Y * TowardMinerals.Y;
                            if (MineralProjection > -32.0f ||
                                !Game::Systems::BuildingSystem::IsPlacementValid(
                                    EM, BuildingDefID, Candidate, PlayerID))
                            {
                                continue;
                            }
                            const float Score =
                                (Candidate - PreferredTarget).LengthSq();
                            if (Score < BestScore)
                            {
                                BestScore = Score;
                                BestOutward = Candidate;
                                FoundOutward = true;
                            }
                        }
                    }
                }
                if (FoundOutward)
                {
                    Out = BestOutward;
                    Core::Utils::Logger::Info(
                        "InteractionSmoke",
                        "outward build placement def=" +
                            std::to_string(BuildingDefID) + " pos=" +
                            std::to_string((int)Out.X) + "," +
                            std::to_string((int)Out.Y));
                    return true;
                }
            }
        }

        int CenterX = 0;
        int CenterY = 0;
        Map.WorldToTile(Center, CenterX, CenterY);

        for (int Radius = 3; Radius <= 18; ++Radius)
        {
            for (int DY = -Radius; DY <= Radius; ++DY)
            {
                for (int DX = -Radius; DX <= Radius; ++DX)
                {
                    if (std::max(std::abs(DX), std::abs(DY)) != Radius)
                        continue;

                    const int TileX = CenterX + DX;
                    const int TileY = CenterY + DY;
                    if (!Map.IsInBounds(TileX, TileY))
                        continue;

                    const Core::Math::Vec2 Candidate = Map.TileToWorld(TileX, TileY);
                    if (Game::Systems::BuildingSystem::IsPlacementValid(
                            EM, BuildingDefID, Candidate, PlayerID))
                    {
                        Out = Candidate;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    inline int SpawnSmokeWorker(GameSession& Session, int PlayerID, int ResourceID)
    {
        auto& EM = Session.Entities();
        auto* ResourceTrans = EM.GetComponent<Game::Components::TransformComponent>(ResourceID);
        if (!ResourceTrans)
            return -1;

        const int ScvDefID = Game::Races::GetUnitDefIDByChkUnitID(0x07);
        if (ScvDefID < 0)
            return -1;

        Core::Math::Vec2 Position = ResourceTrans->Position + Core::Math::Vec2{-96.0f, 32.0f};
        FindWalkableNear(Session, ResourceTrans->Position, 2, 10, Position);
        const int EntityID = Game::Systems::ProductionSystem::DebugSpawnUnit(
            EM, ScvDefID, Position, PlayerID);
        if (EntityID >= 0)
        {
            Core::Utils::Logger::Info(
                "InteractionSmoke",
                "Interaction smoke spawned local worker=" + std::to_string(EntityID) +
                    " for economy verification");
        }
        return EntityID;
    }

    inline int EnsureSmokeDepot(GameSession& Session, int PlayerID, int ResourceID)
    {
        auto& EM = Session.Entities();
        auto* ResourceTrans = EM.GetComponent<Game::Components::TransformComponent>(ResourceID);
        if (!ResourceTrans)
            return -1;

        const int CommandCenterDefID = Game::Races::GetUnitDefIDByChkUnitID(0x6A);
        if (CommandCenterDefID < 0)
            return -1;

        Core::Math::Vec2 Position = ResourceTrans->Position + Core::Math::Vec2{128.0f, 0.0f};
        FindWalkableNear(Session, Position, 0, 10, Position);
        const int DepotID = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
            EM, CommandCenterDefID, Position, PlayerID);
        if (DepotID >= 0)
        {
            Core::Utils::Logger::Info(
                "InteractionSmoke",
                "Interaction smoke spawned local depot=" + std::to_string(DepotID) +
                    " for return-resource verification");
        }
        return DepotID;
    }

    inline bool SpawnSmokeCombatPair(GameSession& Session,
                                     Core::Math::Vec2 Center,
                                     int& OutAttackerID,
                                     int& OutTargetID)
    {
        auto& EM = Session.Entities();
        const int MarineDefID = Game::Races::GetUnitDefIDByChkUnitID(0x00);
        const int ZerglingDefID = Game::Races::GetUnitDefIDByChkUnitID(0x25);
        if (MarineDefID < 0 || ZerglingDefID < 0)
            return false;

        Core::Math::Vec2 AttackerPos = Center;
        FindWalkableNear(Session, Center, 4, 12, AttackerPos);
        const Core::Math::Vec2 TargetPos = AttackerPos + Core::Math::Vec2{56.0f, 0.0f};

        OutAttackerID = Game::Systems::ProductionSystem::DebugSpawnUnit(
            EM, MarineDefID, AttackerPos, 0);
        OutTargetID = Game::Systems::ProductionSystem::DebugSpawnUnit(
            EM, ZerglingDefID, TargetPos, 1);

        if (OutAttackerID < 0 || OutTargetID < 0)
            return false;

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke spawned combat pair attacker=" +
                std::to_string(OutAttackerID) +
                " target=" + std::to_string(OutTargetID));
        return true;
    }

    inline bool SpawnFullRoster(GameSession& Session,
                                FullRosterSpawnResult& Out,
                                std::string& Error)
    {
        auto& EM = Session.Entities();
        const auto& Map = Session.GetEcsMap();
        const auto& Terran = Game::Races::GetTerranDefs();
        const auto& Zerg = Game::Races::GetZergDefs();
        const auto& Protoss = Game::Races::GetProtossDefs();
        const std::array<const std::vector<Game::Races::UnitDef>*, 3> Tables = {{
            &Terran, &Zerg, &Protoss
        }};
        const std::array<int, 3> Offsets = {{
            0,
            (int)Terran.size(),
            (int)Terran.size() + (int)Zerg.size()
        }};

        std::vector<std::pair<int, int>> Buildings;
        std::vector<std::pair<int, int>> Units;
        for (int RaceIndex = 0; RaceIndex < (int)Tables.size(); ++RaceIndex)
        {
            const auto& Defs = *Tables[(size_t)RaceIndex];
            for (int LocalDefID = 0; LocalDefID < (int)Defs.size(); ++LocalDefID)
            {
                const int GlobalDefID = Offsets[(size_t)RaceIndex] + LocalDefID;
                (Defs[(size_t)LocalDefID].IsBuilding ? Buildings : Units)
                    .push_back({GlobalDefID, RaceIndex});
            }
        }

        const int BuildingColumns = std::max(1, (Map.GetWidth() - 8) / 4);
        const int BuildingRows = std::max(
            1, ((int)Buildings.size() + BuildingColumns - 1) / BuildingColumns);
        const int UnitColumns = std::max(1, (Map.GetWidth() - 8) / 2);
        const int BuildingStartY = 4;
        const int UnitStartY = BuildingStartY + BuildingRows * 4 + 4;
        const int UnitRows = std::max(
            1, ((int)Units.size() + UnitColumns - 1) / UnitColumns);
        if (UnitStartY + UnitRows * 2 >= Map.GetHeight() - 2)
        {
            Error = "loaded map is too small for the full unit/building roster";
            return false;
        }

        auto ValidateSpawn = [&](int EntityID,
                                 int DefID,
                                 bool ExpectBuilding,
                                 const std::string& Name)
        {
            if (EntityID < 0 || !EM.IsAlive(EntityID))
            {
                Error = "full roster failed to spawn " + Name;
                return false;
            }
            const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            const auto* Render = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
            const auto* Building =
                EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
            if (!Unit || !Render || (ExpectBuilding != (Building != nullptr)) ||
                (ExpectBuilding && Building->BuildingDefID != DefID))
            {
                Error = "full roster components are incomplete for " + Name;
                return false;
            }
            return true;
        };

        Out = {};
        Out.EntityIDs.reserve(Buildings.size() + Units.size());
        for (int I = 0; I < (int)Buildings.size(); ++I)
        {
            const int TileX = 4 + (I % BuildingColumns) * 4;
            const int TileY = BuildingStartY + (I / BuildingColumns) * 4;
            const auto [DefID, PlayerID] = Buildings[(size_t)I];
            const auto Def = Game::Races::GetUnitDefByID(DefID);
            const int EntityID = Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
                EM, DefID, Map.TileToWorld(TileX, TileY), PlayerID);
            if (!ValidateSpawn(EntityID, DefID, true, Def.Name))
                return false;
            Out.EntityIDs.push_back(EntityID);
            ++Out.BuildingCount;
        }

        for (int I = 0; I < (int)Units.size(); ++I)
        {
            const int TileX = 4 + (I % UnitColumns) * 2;
            const int TileY = UnitStartY + (I / UnitColumns) * 2;
            const auto [DefID, PlayerID] = Units[(size_t)I];
            const auto Def = Game::Races::GetUnitDefByID(DefID);
            const int EntityID = Game::Systems::ProductionSystem::DebugSpawnUnit(
                EM, DefID, Map.TileToWorld(TileX, TileY), PlayerID);
            if (!ValidateSpawn(EntityID, DefID, false, Def.Name))
                return false;
            Out.EntityIDs.push_back(EntityID);
            ++Out.UnitCount;
        }

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "full roster spawned buildings=" + std::to_string(Out.BuildingCount) +
                " units=" + std::to_string(Out.UnitCount) +
                " total=" + std::to_string(Out.EntityIDs.size()));
        return true;
    }

    inline Core::Math::Vec2 FindMoveTarget(GameSession& Session,
                                           Core::ECS::EntityManager& EM,
                                           int EntityID,
                                           bool& FoundPath)
    {
        FoundPath = false;
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        if (!Trans)
            return {0.0f, 0.0f};

        Core::Math::Vec2 WalkableOrigin;
        if (FindWalkableNear(Session, Trans->Position, 0, 8, WalkableOrigin))
            Trans->Position = WalkableOrigin;

        for (int Radius = 3; Radius <= 14; ++Radius)
        {
            const auto& Map = Session.GetEcsMap();
            int TileX = 0;
            int TileY = 0;
            Map.WorldToTile(Trans->Position, TileX, TileY);
            for (int DY = -Radius; DY <= Radius; ++DY)
            {
                for (int DX = -Radius; DX <= Radius; ++DX)
                {
                    if (std::max(std::abs(DX), std::abs(DY)) != Radius)
                        continue;

                    const int CandidateTileX = TileX + DX;
                    const int CandidateTileY = TileY + DY;
                    if (!Map.IsInBounds(CandidateTileX, CandidateTileY) ||
                        !Map.IsWalkable(CandidateTileX, CandidateTileY))
                    {
                        continue;
                    }

                    const Core::Math::Vec2 Candidate =
                        Map.TileToWorld(CandidateTileX, CandidateTileY);
                    Game::Systems::CommandSystem::IssueMove(EM, {EntityID}, Candidate);
                    if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID))
                    {
                        if (!Path->Path.empty())
                        {
                            FoundPath = true;
                            return Candidate;
                        }
                    }
                }
            }
        }
        return Trans->Position;
    }

    inline bool MoveEntityNear(GameSession& Session,
                               Core::ECS::EntityManager& EM,
                               int EntityID,
                               Core::Math::Vec2 Center,
                               int MinRadiusTiles,
                               int MaxRadiusTiles)
    {
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        if (!Trans)
            return false;

        Core::Math::Vec2 Position = Center;
        if (!FindWalkableNear(Session, Center, MinRadiusTiles, MaxRadiusTiles, Position))
            return false;

        Trans->Position = Position;
        if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }
        return true;
    }

    inline bool VerifySelection(Core::ECS::EntityManager& EM,
                                ClientRuntime& Client,
                                int WorkerID,
                                std::string& Error)
    {
        auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
        if (!WorkerTrans)
        {
            Fail(Error, "worker missing transform");
            return false;
        }

        Game::Client::Selection Selector;
        Selector.Init(Client.GetDataAssets());
        Selector.SetLocalPlayerId(0);

        const int Picked = Selector.PickEntityAtWorld(
            EM, WorkerTrans->Position.X, WorkerTrans->Position.Y);
        if (Picked != WorkerID)
        {
            Fail(Error, "point selection did not pick the expected worker");
            return false;
        }

        if (const auto* Loader = Client.GetDataAssets().GetDataLoader())
        {
            const auto* WorkerUnit = EM.GetComponent<Game::Components::UnitComponent>(WorkerID);
            const auto* Sprite = WorkerUnit
                ? Loader->FindSpriteByID(
                    Loader->ResolveUnitSpriteID((uint32_t)WorkerUnit->ChkUnitID))
                : nullptr;
            if (Sprite && Sprite->SelectionCircleVerticalOffset > 0)
            {
                const int RingPicked = Selector.PickEntityAtWorld(
                    EM,
                    WorkerTrans->Position.X,
                    WorkerTrans->Position.Y +
                        (float)Sprite->SelectionCircleVerticalOffset);
                if (RingPicked != WorkerID)
                {
                    Fail(Error, "point selection is not aligned with the sprites.dat selection ring");
                    return false;
                }
            }
        }

        const auto Rect = Selector.CollectRectSelection(
            EM, WorkerTrans->Position.X - 32.0f, WorkerTrans->Position.Y - 32.0f, 64.0f, 64.0f);
        if (!ContainsEntity(Rect, WorkerID))
        {
            Fail(Error, "drag selection did not include the expected worker");
            return false;
        }
        return true;
    }

    inline bool VerifyMove(GameSession& Session,
                           Core::ECS::EntityManager& EM,
                           GuiRuntime& Gui,
                           UI::Hud& Hud,
                           int MoverID,
                           std::string& Error)
    {
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(MoverID);
        if (!Trans)
        {
            Fail(Error, "move unit missing transform");
            return false;
        }

        bool FoundPath = false;
        const Core::Math::Vec2 Start = Trans->Position;
        const Core::Math::Vec2 MoveTarget = FindMoveTarget(Session, EM, MoverID, FoundPath);
        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(MoverID);
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(MoverID);
        if (!FoundPath || !Path || Path->Path.empty())
        {
            Fail(Error, "move target search did not produce a path");
            return false;
        }
        if (Command)
        {
            Command->CurrentCommand = Game::Components::CommandType::Stop;
            Command->TargetEntityID = -1;
            Command->TargetPosition = {};
        }
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {0.0f, 0.0f};

        Hud.UpdateContext(EM, {MoverID});
        int MoveButtonIndex = -1;
        const auto* MoveButton = FindHudButtonByCommand(
            Hud, UI::HudCommandId::Move, &MoveButtonIndex);
        if (!MoveButton || !MoveButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled Move command button");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *MoveButton, "Move command", Error))
            return false;
        if (!VerifyGuiCommandClick(Gui, EM, {MoverID}, Hud, MoveButtonIndex, "Move command", Error))
            return false;
        if (!ExecuteHudCommandIndex(EM, {MoverID}, Hud, MoveButtonIndex))
        {
            Fail(Error, "HUD Move command button did not enter targeting mode");
            return false;
        }
        if (!Hud.HasPendingTarget() ||
            Hud.GetPendingTargetMode() != UI::PendingTargetMode::Move)
        {
            Fail(Error, "HUD Move command did not enter move targeting mode");
            return false;
        }

        ClickTargetContext EmptyClickTarget;
        if (!ExecutePendingTargetCommand(
                EM,
                {MoverID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                EmptyClickTarget,
                MoveTarget))
        {
            Fail(Error, "HUD Move pending target did not issue movement");
            return false;
        }
        Hud.CancelPendingTarget();
        if (Hud.HasPendingTarget())
        {
            Fail(Error, "HUD Move targeting mode did not clear after accepted target");
            return false;
        }

        Command = EM.GetComponent<Game::Components::CommandComponent>(MoverID);
        Path = EM.GetComponent<Game::Components::PathfindingComponent>(MoverID);
        if (!Command || Command->CurrentCommand != Game::Components::CommandType::Move)
        {
            Fail(Error, "HUD Move command did not set a move command");
            return false;
        }
        if (!Path || Path->Path.empty())
        {
            Fail(Error, "HUD Move command did not produce a path");
            return false;
        }

        TickSystems(Session, 8, 0.1f);
        if (Trans->Position.DistanceTo(Start) <= 0.5f)
        {
            Fail(Error, "movement system did not advance the selected unit");
            return false;
        }
        if (auto* Render = EM.GetComponent<Game::Components::RenderComponent>(MoverID))
        {
            if (Render->TotalFrames <= 1)
            {
                Fail(Error, "movement animation did not expose multiple render frames");
                return false;
            }
            if (!VerifyIScriptAnim(Render->ImageID, Game::IScriptAnims::Anim_Walking,
                                   "moved worker", Error))
            {
                return false;
            }
        }
        (void)MoveTarget;
        return true;
    }

    inline bool FindEightWayAnimationProbeCenter(
        const GameSession& Session,
        Core::ECS::EntityManager& EM,
        int MoverID,
        Core::Math::Vec2& Out)
    {
        const auto& Map = Session.GetEcsMap();
        constexpr int MarginTiles = 4;
        constexpr float EntityClearance = 192.0f;
        const auto Entities = EM.GetEntitiesWith<Game::Components::TransformComponent>();

        for (int Y = MarginTiles; Y < Map.GetHeight() - MarginTiles; ++Y)
        {
            for (int X = MarginTiles; X < Map.GetWidth() - MarginTiles; ++X)
            {
                bool Open = true;
                for (int DY = -3; DY <= 3 && Open; ++DY)
                    for (int DX = -3; DX <= 3; ++DX)
                        if (!Map.IsWalkable(X + DX, Y + DY))
                        {
                            Open = false;
                            break;
                        }
                if (!Open)
                    continue;

                const Core::Math::Vec2 Candidate = Map.TileToWorld(X, Y);
                for (int EntityID : Entities)
                {
                    if (EntityID == MoverID)
                        continue;
                    const auto* Trans =
                        EM.GetComponent<Game::Components::TransformComponent>(EntityID);
                    if (Trans && Trans->Position.DistanceTo(Candidate) < EntityClearance)
                    {
                        Open = false;
                        break;
                    }
                }
                if (Open)
                {
                    Out = Candidate;
                    return true;
                }
            }
        }
        return false;
    }

    inline bool VerifyEightWayMovementAnimation(
        GameSession& Session,
        Core::ECS::EntityManager& EM,
        ClientRuntime& Client,
        int MoverID,
        std::string& Error)
    {
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(MoverID);
        auto* Render = EM.GetComponent<Game::Components::RenderComponent>(MoverID);
        auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(MoverID);
        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(MoverID);
        if (!Trans || !Render || !Path || !Command)
        {
            Fail(Error, "eight-way animation probe is missing movement/render components");
            return false;
        }

        Core::Math::Vec2 ProbeCenter;
        if (!FindEightWayAnimationProbeCenter(Session, EM, MoverID, ProbeCenter))
        {
            Fail(Error, "could not find an open map area for eight-way animation probe");
            return false;
        }

        const auto* SpriteFrames =
            Client.GetSpriteAssets().GetDatImageFrames(Render->ImageID);
        if (!SpriteFrames || SpriteFrames->size() < 17)
        {
            Fail(Error, "current HD worker asset does not expose directional frames; image=" +
                            std::to_string(Render->ImageID));
            return false;
        }

        static const std::array<Core::Math::Vec2, 8> Directions = {{
            {1.0f, 0.0f}, {0.70710678f, 0.70710678f},
            {0.0f, 1.0f}, {-0.70710678f, 0.70710678f},
            {-1.0f, 0.0f}, {-0.70710678f, -0.70710678f},
            {0.0f, -1.0f}, {0.70710678f, -0.70710678f}
        }};

        const Core::Math::Vec2 OriginalPosition = Trans->Position;
        std::set<std::pair<int, bool>> DirectionalSignatures;
        bool SawMovementAttachment = false;
        for (const auto& Direction : Directions)
        {
            const Core::Math::Vec2 Target = ProbeCenter + Direction * 96.0f;
            Trans->Position = ProbeCenter;
            Command->CancelAll();
            Command->CurrentCommand = Game::Components::CommandType::Move;
            Command->TargetEntityID = -1;
            Command->TargetPosition = Target;

            Path->Path = {Target};
            Path->PathIndex = 0;
            Path->NodeTolerance = 4.0f;
            Path->RequestedDestination = Target;
            Path->RequestedTargetEntityID = -1;
            Path->RequestedIgnoreEntityID = -1;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {};
            Path->TargetSpeed = 0.0f;
            Path->HasRecentMoveFailure = false;
            Path->HasLastWaypointDistance = false;
            Path->HasLastDestinationDistance = false;
            Path->StallTimer = 0.0f;
            Path->DestinationStallTimer = 0.0f;

            TickSystems(Session, 4, 0.1f);
            const Core::Math::Vec2 Applied = Trans->Position - ProbeCenter;
            if (Applied.Dot(Direction) <= 0.5f)
            {
                Fail(Error, "eight-way animation probe did not move in requested direction");
                return false;
            }

            const int FrameIndex = Render->FrameIndex;
            if (FrameIndex < 0 || FrameIndex >= (int)SpriteFrames->size() ||
                !(*SpriteFrames)[(size_t)FrameIndex].Valid())
            {
                Fail(Error, "eight-way animation selected an invalid HD frame; frame=" +
                                std::to_string(FrameIndex));
                return false;
            }
            DirectionalSignatures.emplace(FrameIndex, Render->Flipped);

            if (const auto* Tree =
                    EM.GetComponent<Game::Components::ImageTreeComponent>(MoverID))
            {
                for (const auto& Node : Tree->Nodes)
                    if (Node.Visible)
                        SawMovementAttachment = true;
            }
        }

        Trans->Position = OriginalPosition;
        Command->CancelAll();
        Command->TargetEntityID = -1;
        Command->TargetPosition = {};
        Path->Path.clear();
        Path->PathIndex = 0;
        Path->CurrentSpeed = 0.0f;
        Path->CurrentVelocity = {};
        Path->TargetSpeed = 0.0f;
        TickSystems(Session, 2, 1.0f / 24.0f);

        if (DirectionalSignatures.size() != Directions.size())
        {
            Fail(Error, "real movement/render pipeline collapsed eight directions to " +
                            std::to_string(DirectionalSignatures.size()));
            return false;
        }
        if (!SawMovementAttachment)
        {
            Fail(Error, "real worker walking animation lost its IScript image attachment");
            return false;
        }

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke eight-way HD movement passed: image=" +
                std::to_string(Render->ImageID) +
                " asset_frames=" + std::to_string(SpriteFrames->size()) +
                " directions=" + std::to_string(DirectionalSignatures.size()));
        return true;
    }

    inline bool VerifyHarvest(GameSession& Session,
                              Core::ECS::EntityManager& EM,
                              ClientRuntime& Client,
                              GuiRuntime& Gui,
                              UI::Hud& Hud,
                              int WorkerID,
                              int ResourceID,
                              int& OutDepotID,
                              std::string& Error)
    {
        auto* ResourceTrans = EM.GetComponent<Game::Components::TransformComponent>(ResourceID);
        auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(ResourceID);
        if (!ResourceTrans)
        {
            Fail(Error, "resource missing transform");
            return false;
        }
        if (!Resource || Resource->Amount <= 0)
        {
            Fail(Error, "resource missing active amount");
            return false;
        }

        const int DepotID = EnsureSmokeDepot(Session, 0, ResourceID);
        if (DepotID < 0)
        {
            Fail(Error, "could not create smoke depot for return-resource verification");
            return false;
        }
        OutDepotID = DepotID;

        MoveEntityNear(Session, EM, WorkerID, ResourceTrans->Position, 1, 8);
        if (auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID))
        {
            if (WorkerTrans->Position.DistanceTo(ResourceTrans->Position) > 36.0f)
                WorkerTrans->Position = ResourceTrans->Position + Core::Math::Vec2{24.0f, 0.0f};
        }

        const std::vector<int> Selected = {WorkerID};
        Hud.UpdateContext(EM, Selected);
        int HarvestButtonIndex = -1;
        const auto* HarvestButton = FindHudButtonByCommand(
            Hud, UI::HudCommandId::Harvest, &HarvestButtonIndex);
        if (!HarvestButton || !HarvestButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled Harvest command button");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *HarvestButton, "Harvest command", Error))
            return false;
        if (!VerifyGuiCommandClick(Gui, EM, Selected, Hud, HarvestButtonIndex, "Harvest command", Error))
            return false;
        if (!ExecuteHudCommandIndex(EM, Selected, Hud, HarvestButtonIndex))
        {
            Fail(Error, "HUD Harvest command button did not enter targeting mode");
            return false;
        }
        if (!Hud.HasPendingTarget() ||
            Hud.GetPendingTargetMode() != UI::PendingTargetMode::Harvest)
        {
            Fail(Error, "HUD Harvest command did not enter harvest targeting mode");
            return false;
        }

        auto Target = Client.ResolveClickTargetContext(EM, Selected, ResourceTrans->Position);
        if (Target.ResourceEntityID != ResourceID)
        {
            Fail(Error, "resource click did not resolve the expected resource");
            return false;
        }
        if (!ExecutePendingTargetCommand(
                EM,
                Selected,
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                Target,
                ResourceTrans->Position))
        {
            Fail(Error, "HUD Harvest pending target did not issue harvesting");
            return false;
        }
        Hud.CancelPendingTarget();
        if (Hud.HasPendingTarget())
        {
            Fail(Error, "HUD Harvest targeting mode did not clear after accepted target");
            return false;
        }

        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(WorkerID);
        auto* Harvest = EM.GetComponent<Game::Components::HarvestComponent>(WorkerID);
        if (!Command || Command->CurrentCommand != Game::Components::CommandType::Harvest ||
            Command->TargetEntityID != ResourceID)
        {
            std::string Reason;
            const bool CanHarvest =
                Game::Systems::HarvestSystem::CanHarvest(EM, WorkerID, ResourceID, &Reason);
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(WorkerID);
            const int ChkUnitID = Unit ? Unit->ChkUnitID : -1;
            const int UnitDefID = Unit ? Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID) : -1;
            const int CurrentCommand = Command ? (int)Command->CurrentCommand : -1;
            const int CurrentTarget = Command ? Command->TargetEntityID : -1;
            Fail(Error,
                 "smart resource click did not set harvest command; worker=" +
                     std::to_string(WorkerID) +
                     " chk=" + std::to_string(ChkUnitID) +
                     " def=" + std::to_string(UnitDefID) +
                     " targetResource=" + std::to_string(Target.ResourceEntityID) +
                     " cmd=" + std::to_string(CurrentCommand) +
                     " cmdTarget=" + std::to_string(CurrentTarget) +
                     " canHarvest=" + (CanHarvest ? "1" : "0") +
                     " reason=" + Reason);
            return false;
        }
        if (!Harvest || Harvest->TargetResourceID != ResourceID)
        {
            Fail(Error, "smart resource click did not create harvest state");
            return false;
        }

        auto& PlayerResources = Game::Systems::HarvestSystem::GetResources(0);
        const int StartMinerals = PlayerResources.Minerals;
        const int StartAmount = Resource->Amount;
        // A campaign mineral patch can already have a worker in its gather
        // queue.  Wait through a complete gather hand-off instead of assuming
        // that the newly selected worker owns the patch within 1.6 seconds.
        // This keeps the runtime smoke aligned with the ordered multi-worker
        // queue used by HarvestSystem.
        for (int I = 0;
             I < 80 && Resource->Amount >= StartAmount && Harvest->CarryingAmount <= 0;
             ++I)
        {
            TickSystems(Session, 1, 0.1f);
        }
        if (Resource->Amount >= StartAmount && Harvest->CarryingAmount <= 0)
        {
            const auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
            const auto* WorkerPath = EM.GetComponent<Game::Components::PathfindingComponent>(WorkerID);
            Fail(Error,
                 "harvest system did not mine from the resource; state=" +
                     std::to_string((int)Harvest->State) +
                     " queued=" + std::to_string(Resource->QueuedGatherers.size()) +
                     " active=" + std::to_string(Resource->ActiveGathererEntityID) +
                     " worker=" +
                     (WorkerTrans
                          ? std::to_string((int)WorkerTrans->Position.X) + "," +
                                std::to_string((int)WorkerTrans->Position.Y)
                          : std::string("missing")) +
                     " resource=" + std::to_string((int)ResourceTrans->Position.X) + "," +
                     std::to_string((int)ResourceTrans->Position.Y) +
                     " path=" +
                     (WorkerPath ? std::to_string(WorkerPath->Path.size()) : std::string("missing")));
            return false;
        }

        if (Harvest->ReturnBuildingID >= 0)
        {
            auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
            auto* DepotTrans = EM.GetComponent<Game::Components::TransformComponent>(Harvest->ReturnBuildingID);
            if (WorkerTrans && DepotTrans)
            {
                WorkerTrans->Position = DepotTrans->Position + Core::Math::Vec2{24.0f, 0.0f};
                if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(WorkerID))
                {
                    Path->Path.clear();
                    Path->PathIndex = 0;
                    Path->CurrentSpeed = 0.0f;
                    Path->CurrentVelocity = {0.0f, 0.0f};
                }
            }
        }

        TickSystems(Session, 8, 0.1f);
        if (PlayerResources.Minerals <= StartMinerals)
        {
            Fail(Error,
                 "harvest system did not return minerals to the depot; state=" +
                     std::to_string((int)Harvest->State) +
                     " carry=" + std::to_string(Harvest->CarryingAmount) +
                     " depot=" + std::to_string(Harvest->ReturnBuildingID) +
                     " minerals=" + std::to_string(PlayerResources.Minerals) +
                     " startMinerals=" + std::to_string(StartMinerals));
            return false;
        }
        return true;
    }

    inline bool VerifyProduction(GameSession& Session,
                                 Core::ECS::EntityManager& EM,
                                 ClientRuntime& Client,
                                 GuiRuntime& Gui,
                                 UI::Hud& Hud,
                                 int DepotID,
                                 int& OutTrainedUnitID,
                                 std::string& Error)
    {
        const int ScvDefID = Game::Races::GetUnitDefIDByChkUnitID(0x07);
        if (DepotID < 0 || ScvDefID < 0)
        {
            Fail(Error, "missing producer or SCV definition for production verification");
            return false;
        }

        EnsurePlayerResources(0);
        Hud.UpdateContext(EM, {DepotID});
        int TrainButtonIndex = -1;
        const auto* TrainButton = FindHudButtonByTarget(
            Hud, UI::ActionKind::Train, ScvDefID, &TrainButtonIndex);
        if (!TrainButton || !TrainButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled SCV train button");
            return false;
        }
        const auto ScvDef = Game::Races::GetUnitDefByID(ScvDefID);
        if (TrainButton->MineralCost != ScvDef.MineralCost ||
            TrainButton->GasCost != ScvDef.GasCost ||
            TrainButton->SupplyCost != ScvDef.SupplyCost)
        {
            Fail(Error, "HUD SCV train button costs do not match race data");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *TrainButton, "SCV train", Error))
            return false;
        if (!VerifyGuiCommandClick(Gui, EM, {DepotID}, Hud, TrainButtonIndex, "SCV train", Error))
            return false;

        auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
        const int StartMinerals = Resources.Minerals;
        const int StartSupplyUsed = Resources.SupplyUsed;
        const int StartScvs = CountOwnedUnitsByChk(EM, 0, 0x07);
        if (!ExecuteHudCommandIndex(EM, {DepotID}, Hud, TrainButtonIndex))
        {
            Fail(Error, "HUD SCV train button did not queue training");
            return false;
        }
        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke HUD queued train button=" +
                std::to_string(TrainButtonIndex) +
                " def=" + std::to_string(ScvDefID));
        if (Resources.Minerals >= StartMinerals)
        {
            Fail(Error, "SCV training did not deduct minerals");
            return false;
        }
        if (Resources.SupplyUsed != StartSupplyUsed + ScvDef.SupplyCost)
        {
            Fail(Error,
                 "SCV training did not reserve supply; supply=" +
                     std::to_string(Resources.SupplyUsed) +
                     " start=" + std::to_string(StartSupplyUsed) +
                     " cost=" + std::to_string(ScvDef.SupplyCost));
            return false;
        }
        Hud.UpdateContext(EM, {DepotID});
        const auto& QueueInfo = Hud.GetState().InfoPanel;
        if (QueueInfo.QueueEntries.empty() || QueueInfo.QueueEntries[0] != "SCV")
        {
            Fail(Error, "HUD info panel did not expose queued SCV after train click");
            return false;
        }

        TickSystems(Session, std::max(1, ScvDef.BuildTime + 2), 1.0f);
        if (CountOwnedUnitsByChk(EM, 0, 0x07) <= StartScvs)
        {
            Fail(Error, "production system did not spawn a trained SCV");
            return false;
        }
        if (Resources.SupplyUsed != StartSupplyUsed + ScvDef.SupplyCost)
        {
            Fail(Error,
                 "completed SCV training did not keep supply occupied; supply=" +
                     std::to_string(Resources.SupplyUsed) +
                     " start=" + std::to_string(StartSupplyUsed) +
                     " cost=" + std::to_string(ScvDef.SupplyCost));
            return false;
        }
        Hud.UpdateContext(EM, {DepotID});
        if (!Hud.GetState().InfoPanel.QueueEntries.empty())
        {
            Fail(Error, "HUD info panel still showed queued SCV after production completed");
            return false;
        }

        const int NewScvID = FindNewestOwnedUnitByChk(EM, 0, 0x07);
        if (!EM.GetComponent<Game::Components::RenderComponent>(NewScvID) ||
            !EM.GetComponent<Game::Components::PathfindingComponent>(NewScvID))
        {
            Fail(Error, "trained SCV missing render or pathfinding components");
            return false;
        }
        if (!VerifyRenderableEntity(EM, Client, NewScvID, "trained SCV", Error))
            return false;
        if (auto* Render = EM.GetComponent<Game::Components::RenderComponent>(NewScvID))
        {
            if (!VerifyIScriptAnim(Render->ImageID, Game::IScriptAnims::Anim_Init,
                                   "trained SCV", Error))
            {
                return false;
            }
        }
        OutTrainedUnitID = NewScvID;

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke trained unit=" + std::to_string(NewScvID) +
                " from depot=" + std::to_string(DepotID));
        return true;
    }

    inline bool VerifyBuildingConstruction(GameSession& Session,
                                           Core::ECS::EntityManager& EM,
                                           ClientRuntime& Client,
                                           GuiRuntime& Gui,
                                           UI::Hud& Hud,
                                           int WorkerID,
                                           int& OutBuildingID,
                                           std::string& Error)
    {
        const int SupplyDepotDefID = Game::Races::GetUnitDefIDByChkUnitID(0x6D);
        auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
        if (SupplyDepotDefID < 0 || !WorkerTrans)
        {
            Fail(Error, "missing Supply Depot definition or worker transform");
            return false;
        }

        EnsurePlayerResources(0);
        Core::Math::Vec2 BuildPos;
        if (!FindBuildPlacementNear(Session, EM, SupplyDepotDefID, WorkerTrans->Position, 0, BuildPos))
        {
            Fail(Error, "could not find a valid Supply Depot placement");
            return false;
        }

        Hud.UpdateContext(EM, {WorkerID});
        if (!VerifyGuiCommandClick(Gui, EM, {WorkerID}, Hud, 6, "SCV basic build", Error))
            return false;
        if (!ExecuteHudCommandIndex(EM, {WorkerID}, Hud, 6))
        {
            Fail(Error, "HUD SCV basic build submenu button did not open");
            return false;
        }
        Hud.UpdateContext(EM, {WorkerID});
        int BuildButtonIndex = -1;
        const auto* BuildButton = FindHudButtonByTarget(
            Hud, UI::ActionKind::Build, SupplyDepotDefID, &BuildButtonIndex);
        if (!BuildButton || !BuildButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled Supply Depot build button");
            return false;
        }
        const auto SupplyDepotDef = Game::Races::GetUnitDefByID(SupplyDepotDefID);
        if (BuildButton->MineralCost != SupplyDepotDef.MineralCost ||
            BuildButton->GasCost != SupplyDepotDef.GasCost)
        {
            Fail(Error, "HUD Supply Depot build button costs do not match race data");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *BuildButton, "Supply Depot build", Error))
            return false;
        if (!VerifyGuiCommandClick(
                Gui, EM, {WorkerID}, Hud, BuildButtonIndex, "Supply Depot build", Error))
        {
            return false;
        }

        auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
        const int StartMinerals = Resources.Minerals;
        const int StartSupplyMax = Resources.SupplyMax;
        if (!ExecuteHudCommandIndex(EM, {WorkerID}, Hud, BuildButtonIndex))
        {
            Fail(Error, "HUD Supply Depot build button did not enter placement mode");
            return false;
        }
        if (!Hud.HasPendingTarget() ||
            Hud.GetPendingTargetMode() != UI::PendingTargetMode::BuildPlacement ||
            Hud.GetPendingBuildDefID() != SupplyDepotDefID)
        {
            Fail(Error, "HUD Supply Depot build click did not enter build placement mode");
            return false;
        }

        Game::Client::GameLoop::ClickTargetContext EmptyClickTarget;
        Core::Math::Vec2 OccupiedBuildPos = {};
        bool HasOccupiedBuildPos = false;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::BuildingComponent,
                                               Game::Components::TransformComponent>())
        {
            if (EntityID == WorkerID)
                continue;
            if (auto* OccupiedTrans =
                    EM.GetComponent<Game::Components::TransformComponent>(EntityID))
            {
                OccupiedBuildPos = OccupiedTrans->Position;
                HasOccupiedBuildPos = true;
                break;
            }
        }
        if (!HasOccupiedBuildPos)
        {
            Fail(Error, "could not find an occupied building position for invalid placement verification");
            return false;
        }
        if (ExecutePendingTargetCommand(
                EM,
                {WorkerID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                EmptyClickTarget,
                OccupiedBuildPos))
        {
            Fail(Error, "HUD Supply Depot placement accepted an occupied location");
            return false;
        }
        if (!Hud.HasPendingTarget() ||
            Hud.GetPendingTargetMode() != UI::PendingTargetMode::BuildPlacement ||
            Hud.GetPendingBuildDefID() != SupplyDepotDefID)
        {
            Fail(Error, "HUD Supply Depot placement mode cleared after rejected target");
            return false;
        }
        if (Resources.Minerals != StartMinerals)
        {
            Fail(Error, "rejected Supply Depot placement changed minerals");
            return false;
        }
        if (FindBuildingByDefAndBuilder(EM, SupplyDepotDefID, WorkerID) >= 0)
        {
            Fail(Error, "rejected Supply Depot placement created a building");
            return false;
        }

        if (!ExecutePendingTargetCommand(
                EM,
                {WorkerID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                EmptyClickTarget,
                BuildPos))
        {
            Fail(Error, "HUD Supply Depot placement command did not start construction");
            return false;
        }
        Hud.CancelPendingTarget();
        if (Hud.HasPendingTarget())
        {
            Fail(Error, "HUD did not clear Supply Depot placement mode after accepted target");
            return false;
        }
        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke HUD placed build button=" +
                std::to_string(BuildButtonIndex) +
                " def=" + std::to_string(SupplyDepotDefID));
        if (Resources.Minerals >= StartMinerals)
        {
            Fail(Error, "Supply Depot construction did not deduct minerals");
            return false;
        }

        const int BuildingID = FindBuildingByDefAndBuilder(EM, SupplyDepotDefID, WorkerID);
        auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(BuildingID);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(BuildingID);
        auto* Render = EM.GetComponent<Game::Components::RenderComponent>(BuildingID);
        auto* BuildingTrans = EM.GetComponent<Game::Components::TransformComponent>(BuildingID);
        if (!Building || !Unit || !Render || !BuildingTrans || !Building->UnderConstruction)
        {
            Fail(Error, "started Supply Depot missing construction components");
            return false;
        }
        if (!VerifyRenderableEntity(EM, Client, BuildingID, "Supply Depot construction", Error))
            return false;
        if (!VerifyIScriptAnim(Render->ImageID, Game::IScriptAnims::Anim_AlmostBuilt,
                               "Supply Depot construction", Error))
        {
            return false;
        }

        if (auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID))
            Trans->Position = BuildingTrans->Position;
        if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(WorkerID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }
        if (auto* Command = EM.GetComponent<Game::Components::CommandComponent>(WorkerID))
        {
            Command->CurrentCommand = Game::Components::CommandType::Stop;
            Command->TargetEntityID = -1;
            Command->TargetPosition = BuildingTrans->Position;
        }

        Building->BuildProgress = std::max(0.0f, Building->TotalBuildTime - 0.05f);
        TickSystems(Session, 12, 0.1f);
        if (Building->UnderConstruction ||
            Building->BuildProgress < Building->TotalBuildTime ||
            Unit->HitPoints != Unit->MaxHitPoints)
        {
            Fail(Error,
                 "building system did not complete Supply Depot construction; progress=" +
                     std::to_string(Building->BuildProgress) +
                     " total=" + std::to_string(Building->TotalBuildTime) +
                     " hp=" + std::to_string(Unit->HitPoints) +
                     "/" + std::to_string(Unit->MaxHitPoints));
            return false;
        }
        if (Resources.SupplyMax <= StartSupplyMax)
        {
            Fail(Error, "completed Supply Depot did not increase supply max");
            return false;
        }
        if (!VerifyIScriptAnim(Render->ImageID, Game::IScriptAnims::Anim_Built,
                               "completed Supply Depot", Error))
        {
            return false;
        }
        OutBuildingID = BuildingID;

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke completed building=" + std::to_string(BuildingID) +
                " def=" + std::to_string(SupplyDepotDefID));
        return true;
    }

    inline bool VerifyResearch(GameSession& Session,
                               Core::ECS::EntityManager& EM,
                               ClientRuntime& Client,
                               GuiRuntime& Gui,
                               UI::Hud& Hud,
                               int WorkerID,
                               std::string& Error)
    {
        const int EngineeringBayDefID = Game::Races::GetUnitDefID("EngineeringBay");
        if (EngineeringBayDefID < 0)
        {
            Fail(Error, "missing Engineering Bay definition for research verification");
            return false;
        }

        auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
        const Core::Math::Vec2 ResearchPos = WorkerTrans
            ? WorkerTrans->Position + Core::Math::Vec2{96.0f, 64.0f}
            : Core::Math::Vec2{96.0f, 64.0f};

        const int EngineeringBayID =
            Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
                EM, EngineeringBayDefID, ResearchPos, 0);
        if (EngineeringBayID < 0)
        {
            Fail(Error, "could not spawn completed Engineering Bay for research verification");
            return false;
        }
        if (!VerifyRenderableEntity(EM, Client, EngineeringBayID,
                                    "research Engineering Bay", Error))
        {
            return false;
        }

        EnsurePlayerResources(0);
        Hud.UpdateContext(EM, {EngineeringBayID});

        constexpr Game::Systems::TerranResearchId HudResearchID =
            Game::Systems::TerranResearchId::InfantryWeapons;
        constexpr Game::Systems::ResearchId SystemResearchID =
            Game::Systems::ResearchId::Terran_InfantryWeapons;

        int ResearchButtonIndex = -1;
        const auto* ResearchButton =
            FindHudButtonByResearch(Hud, HudResearchID, &ResearchButtonIndex);
        if (!ResearchButton || !ResearchButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled Infantry Weapons upgrade button");
            return false;
        }
        if (ResearchButton->Kind != UI::ActionKind::Upgrade)
        {
            Fail(Error, "HUD Infantry Weapons button is not marked as an upgrade action");
            return false;
        }

        int ExpectedMinerals = 0;
        int ExpectedGas = 0;
        if (!Game::Systems::UpgradeSystem::GetResearchCost(
                SystemResearchID, 0, ExpectedMinerals, ExpectedGas))
        {
            Fail(Error, "could not resolve Infantry Weapons upgrade cost");
            return false;
        }
        if (ResearchButton->MineralCost != ExpectedMinerals ||
            ResearchButton->GasCost != ExpectedGas)
        {
            Fail(Error, "HUD Infantry Weapons costs do not match upgrade data");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *ResearchButton, "Infantry Weapons upgrade", Error))
            return false;
        if (!VerifyGuiCommandClick(
                Gui, EM, {EngineeringBayID}, Hud, ResearchButtonIndex,
                "Infantry Weapons upgrade", Error))
        {
            return false;
        }

        auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
        const int StartMinerals = Resources.Minerals;
        const int StartGas = Resources.VespeneGas;
        const int StartLevel =
            Game::Systems::UpgradeSystem::GetResearchLevel(0, SystemResearchID);

        if (!ExecuteHudCommandIndex(EM, {EngineeringBayID}, Hud, ResearchButtonIndex))
        {
            Fail(Error, "HUD Infantry Weapons button did not start upgrade");
            return false;
        }

        auto* Building =
            EM.GetComponent<Game::Components::BuildingComponent>(EngineeringBayID);
        if (!Building || !Building->IsResearching ||
            Building->ResearchingID != static_cast<int>(SystemResearchID))
        {
            Fail(Error, "Engineering Bay did not enter active upgrade state");
            return false;
        }
        if (Resources.Minerals != StartMinerals - ExpectedMinerals ||
            Resources.VespeneGas != StartGas - ExpectedGas)
        {
            Fail(Error, "Infantry Weapons upgrade did not deduct expected resources");
            return false;
        }

        TickSystems(Session, 45, 1.0f);
        const int EndLevel =
            Game::Systems::UpgradeSystem::GetResearchLevel(0, SystemResearchID);
        if (EndLevel != StartLevel + 1)
        {
            Fail(Error,
                 "UpgradeSystem did not complete Infantry Weapons upgrade; level=" +
                     std::to_string(EndLevel) +
                     " start=" + std::to_string(StartLevel));
            return false;
        }
        if (Building->IsResearching || Building->ResearchingID != -1)
        {
            Fail(Error, "Engineering Bay still marked as upgrading after completion");
            return false;
        }

        Hud.UpdateContext(EM, {EngineeringBayID});
        int NextMinerals = 0;
        int NextGas = 0;
        Game::Systems::UpgradeSystem::GetResearchCost(
            SystemResearchID, 0, NextMinerals, NextGas);
        int NextResearchButtonIndex = -1;
        const auto* NextResearchButton =
            FindHudButtonByResearch(Hud, HudResearchID, &NextResearchButtonIndex);
        if (!NextResearchButton ||
            NextResearchButton->Kind != UI::ActionKind::Upgrade ||
            NextResearchButton->MineralCost != NextMinerals ||
            NextResearchButton->GasCost != NextGas)
        {
            Fail(Error, "HUD did not refresh Infantry Weapons next-level upgrade costs");
            return false;
        }

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke completed upgrade=Infantry Weapons level=" +
                std::to_string(EndLevel) +
                " building=" + std::to_string(EngineeringBayID));
        return true;
    }

    inline bool CompleteTerranConstruction(GameSession& Session,
                                           Core::ECS::EntityManager& EM,
                                           int BuildingID,
                                           int WorkerID,
                                           const std::string& Label,
                                           std::string& Error)
    {
        auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(BuildingID);
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(BuildingID);
        auto* BuildingTrans = EM.GetComponent<Game::Components::TransformComponent>(BuildingID);
        if (!Building || !Unit || !BuildingTrans || !Building->UnderConstruction)
        {
            Fail(Error, Label + " missing active construction components");
            return false;
        }

        if (auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID))
            Trans->Position = BuildingTrans->Position;
        if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(WorkerID))
        {
            Path->Path.clear();
            Path->PathIndex = 0;
            Path->CurrentSpeed = 0.0f;
            Path->CurrentVelocity = {0.0f, 0.0f};
        }
        if (auto* Command = EM.GetComponent<Game::Components::CommandComponent>(WorkerID))
        {
            Command->CurrentCommand = Game::Components::CommandType::Stop;
            Command->TargetEntityID = -1;
            Command->TargetPosition = BuildingTrans->Position;
        }

        Building->BuildProgress = std::max(0.0f, Building->TotalBuildTime - 0.05f);
        TickSystems(Session, 12, 0.1f);
        if (Building->UnderConstruction ||
            Building->BuildProgress < Building->TotalBuildTime ||
            Unit->HitPoints != Unit->MaxHitPoints)
        {
            Fail(Error,
                 Label + " did not complete construction; progress=" +
                     std::to_string(Building->BuildProgress) +
                     " total=" + std::to_string(Building->TotalBuildTime) +
                     " hp=" + std::to_string(Unit->HitPoints) +
                     "/" + std::to_string(Unit->MaxHitPoints));
            return false;
        }
        return true;
    }

    inline bool VerifyBarracksMarineProduction(GameSession& Session,
                                               Core::ECS::EntityManager& EM,
                                               ClientRuntime& Client,
                                               GuiRuntime& Gui,
                                               UI::Hud& Hud,
                                               int WorkerID,
                                               int& OutBarracksID,
                                               int& OutMarineID,
                                               std::string& Error)
    {
        const int BarracksDefID = Game::Races::GetUnitDefIDByChkUnitID(0x6F);
        const int MarineDefID = Game::Races::GetUnitDefIDByChkUnitID(0x00);
        auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
        if (BarracksDefID < 0 || MarineDefID < 0 || !WorkerTrans)
        {
            Fail(Error, "missing Barracks/Marine definitions or worker transform");
            return false;
        }

        EnsurePlayerResources(0);
        Core::Math::Vec2 BuildPos;
        if (!FindBuildPlacementNear(Session, EM, BarracksDefID, WorkerTrans->Position, 0, BuildPos))
        {
            Fail(Error, "could not find a valid Barracks placement");
            return false;
        }

        Hud.UpdateContext(EM, {WorkerID});
        int BarracksButtonIndex = -1;
        const auto* BarracksButton = FindHudButtonByTarget(
            Hud, UI::ActionKind::Build, BarracksDefID, &BarracksButtonIndex);
        if (!BarracksButton)
        {
            if (!VerifyGuiCommandClick(Gui, EM, {WorkerID}, Hud, 6,
                                       "SCV basic build for Barracks", Error))
            {
                return false;
            }
            if (!ExecuteHudCommandIndex(EM, {WorkerID}, Hud, 6))
            {
                Fail(Error, "HUD SCV basic build submenu did not open for Barracks");
                return false;
            }
            Hud.UpdateContext(EM, {WorkerID});
            BarracksButton = FindHudButtonByTarget(
                Hud, UI::ActionKind::Build, BarracksDefID, &BarracksButtonIndex);
        }
        if (!BarracksButton || !BarracksButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled Barracks build button");
            return false;
        }

        const auto BarracksDef = Game::Races::GetUnitDefByID(BarracksDefID);
        if (BarracksButton->MineralCost != BarracksDef.MineralCost ||
            BarracksButton->GasCost != BarracksDef.GasCost)
        {
            Fail(Error, "HUD Barracks build button costs do not match race data");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *BarracksButton, "Barracks build", Error))
            return false;
        if (!VerifyGuiCommandClick(
                Gui, EM, {WorkerID}, Hud, BarracksButtonIndex, "Barracks build", Error))
        {
            return false;
        }

        auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
        const int MineralsBeforeBarracks = Resources.Minerals;
        if (!ExecuteHudCommandIndex(EM, {WorkerID}, Hud, BarracksButtonIndex))
        {
            Fail(Error, "HUD Barracks build button did not enter placement mode");
            return false;
        }
        if (!Hud.HasPendingTarget() ||
            Hud.GetPendingTargetMode() != UI::PendingTargetMode::BuildPlacement ||
            Hud.GetPendingBuildDefID() != BarracksDefID)
        {
            Fail(Error, "HUD Barracks build click did not enter build placement mode");
            return false;
        }

        Game::Client::GameLoop::ClickTargetContext EmptyClickTarget;
        if (!ExecutePendingTargetCommand(
                EM,
                {WorkerID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                EmptyClickTarget,
                BuildPos))
        {
            Fail(Error, "HUD Barracks placement command did not start construction");
            return false;
        }
        Hud.CancelPendingTarget();
        if (Resources.Minerals >= MineralsBeforeBarracks)
        {
            Fail(Error, "Barracks construction did not deduct minerals");
            return false;
        }

        const int BarracksID = FindBuildingByDefAndBuilder(EM, BarracksDefID, WorkerID);
        auto* BarracksRender = EM.GetComponent<Game::Components::RenderComponent>(BarracksID);
        if (!EM.GetComponent<Game::Components::BuildingComponent>(BarracksID) ||
            !EM.GetComponent<Game::Components::UnitComponent>(BarracksID) ||
            !BarracksRender)
        {
            Fail(Error, "started Barracks missing construction components");
            return false;
        }
        if (!VerifyRenderableEntity(EM, Client, BarracksID, "Barracks construction", Error))
            return false;
        if (!VerifyIScriptAnim(BarracksRender->ImageID, Game::IScriptAnims::Anim_AlmostBuilt,
                               "Barracks construction", Error))
        {
            return false;
        }
        if (!CompleteTerranConstruction(Session, EM, BarracksID, WorkerID, "Barracks", Error))
            return false;
        if (!VerifyIScriptAnim(BarracksRender->ImageID, Game::IScriptAnims::Anim_Built,
                               "completed Barracks", Error))
        {
            return false;
        }

        Hud.UpdateContext(EM, {BarracksID});
        int MarineButtonIndex = -1;
        const auto* MarineButton = FindHudButtonByTarget(
            Hud, UI::ActionKind::Train, MarineDefID, &MarineButtonIndex);
        if (!MarineButton || !MarineButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled Marine train button");
            return false;
        }

        const auto MarineDef = Game::Races::GetUnitDefByID(MarineDefID);
        if (MarineButton->MineralCost != MarineDef.MineralCost ||
            MarineButton->GasCost != MarineDef.GasCost ||
            MarineButton->SupplyCost != MarineDef.SupplyCost)
        {
            Fail(Error, "HUD Marine train button costs do not match race data");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *MarineButton, "Marine train", Error))
            return false;
        if (!VerifyGuiCommandClick(
                Gui, EM, {BarracksID}, Hud, MarineButtonIndex, "Marine train", Error))
        {
            return false;
        }

        const int StartMinerals = Resources.Minerals;
        const int StartSupplyUsed = Resources.SupplyUsed;
        const int StartMarines = CountOwnedUnitsByChk(EM, 0, 0x00);
        if (!ExecuteHudCommandIndex(EM, {BarracksID}, Hud, MarineButtonIndex))
        {
            Fail(Error, "HUD Marine train button did not queue training");
            return false;
        }
        if (Resources.Minerals >= StartMinerals)
        {
            Fail(Error, "Marine training did not deduct minerals");
            return false;
        }
        if (Resources.SupplyUsed != StartSupplyUsed + MarineDef.SupplyCost)
        {
            Fail(Error,
                 "Marine training did not reserve supply; supply=" +
                     std::to_string(Resources.SupplyUsed) +
                     " start=" + std::to_string(StartSupplyUsed) +
                     " cost=" + std::to_string(MarineDef.SupplyCost));
            return false;
        }
        Hud.UpdateContext(EM, {BarracksID});
        const auto& QueueInfo = Hud.GetState().InfoPanel;
        if (QueueInfo.QueueEntries.empty() || QueueInfo.QueueEntries[0] != "Marine")
        {
            Fail(Error, "HUD info panel did not expose queued Marine after train click");
            return false;
        }

        TickSystems(Session, std::max(1, MarineDef.BuildTime + 2), 1.0f);
        if (CountOwnedUnitsByChk(EM, 0, 0x00) <= StartMarines)
        {
            Fail(Error, "production system did not spawn a trained Marine");
            return false;
        }
        if (Resources.SupplyUsed != StartSupplyUsed + MarineDef.SupplyCost)
        {
            Fail(Error,
                 "completed Marine training did not keep supply occupied; supply=" +
                     std::to_string(Resources.SupplyUsed) +
                     " start=" + std::to_string(StartSupplyUsed) +
                     " cost=" + std::to_string(MarineDef.SupplyCost));
            return false;
        }
        Hud.UpdateContext(EM, {BarracksID});
        if (!Hud.GetState().InfoPanel.QueueEntries.empty())
        {
            Fail(Error, "HUD info panel still showed queued Marine after production completed");
            return false;
        }

        const int MarineID = FindNewestOwnedUnitByChk(EM, 0, 0x00);
        if (!EM.GetComponent<Game::Components::RenderComponent>(MarineID) ||
            !EM.GetComponent<Game::Components::PathfindingComponent>(MarineID))
        {
            Fail(Error, "trained Marine missing render or pathfinding components");
            return false;
        }
        if (!VerifyRenderableEntity(EM, Client, MarineID, "trained Marine", Error))
            return false;
        if (auto* Render = EM.GetComponent<Game::Components::RenderComponent>(MarineID))
        {
            if (!VerifyIScriptAnim(Render->ImageID, Game::IScriptAnims::Anim_Init,
                                   "trained Marine", Error))
            {
                return false;
            }
        }

        OutBarracksID = BarracksID;
        OutMarineID = MarineID;
        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke trained Marine=" + std::to_string(MarineID) +
                " from barracks=" + std::to_string(BarracksID));
        return true;
    }

    inline bool VerifyCampaignVictoryClosure(GameSession& Session,
                                             CampaignRuntime& Campaign,
                                             Core::ECS::EntityManager& EM,
                                             ClientRuntime& Client,
                                             GuiRuntime& Gui,
                                             UI::Hud& Hud,
                                             int WorkerID,
                                             std::string& Error)
    {
        auto& Controller = Campaign.Controller();
        if (Controller.GetState() == Game::Campaign::CampaignState::Victory)
        {
            const auto& Objectives = Controller.GetObjectives();
            if (Objectives.empty())
            {
                Fail(Error, "campaign victory left objectives empty");
                return false;
            }
            for (const auto& Objective : Objectives)
            {
                if (!Objective.Complete)
                {
                    Fail(Error, "campaign victory did not complete every objective");
                    return false;
                }
            }
            Core::Utils::Logger::Info(
                "InteractionSmoke",
                "Interaction smoke campaign already in victory: objectives=" +
                    std::to_string(Objectives.size()));
            return true;
        }
        if (Controller.GetState() == Game::Campaign::CampaignState::Defeat ||
            Controller.GetState() == Game::Campaign::CampaignState::Draw)
        {
            Fail(Error, "campaign entered a non-victory outcome before closure");
            return false;
        }

        const int RaynorID = FindNewestUnitByChk(EM, 19);
        if (RaynorID < 0)
        {
            // The default interaction smoke runs on a melee map. It has no
            // campaign-specific Raynor/Marine objective, so its closure is
            // remaining playable after the complete interaction sequence.
            Core::Utils::Logger::Info(
                "InteractionSmoke",
                "Interaction smoke non-campaign scene remained playable through closure");
            return Controller.GetState() == Game::Campaign::CampaignState::Playing;
        }
        auto* RaynorPlayer = EM.GetComponent<Game::Components::PlayerComponent>(RaynorID);
        auto* RaynorTrans = EM.GetComponent<Game::Components::TransformComponent>(RaynorID);
        if (!RaynorPlayer || !RaynorTrans)
        {
            Fail(Error, "Raynor mission unit is missing owner or transform components");
            return false;
        }
        if (!VerifyRenderableEntity(EM, Client, RaynorID, "mission Raynor", Error))
            return false;
        if (RaynorPlayer->PlayerID != 0)
        {
            auto* WorkerTransForRescue =
                EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
            if (!WorkerTransForRescue)
            {
                Fail(Error, "worker missing transform for Raynor rescue");
                return false;
            }
            Game::Systems::CommandSystem::IssueMove(EM, {WorkerID}, RaynorTrans->Position);
            auto* WorkerCommand = EM.GetComponent<Game::Components::CommandComponent>(WorkerID);
            if (!WorkerCommand ||
                WorkerCommand->CurrentCommand != Game::Components::CommandType::Move)
            {
                Fail(Error, "Raynor rescue move did not issue a movement command");
                return false;
            }
            WorkerTransForRescue->Position =
                RaynorTrans->Position + Core::Math::Vec2{32.0f, 0.0f};
            if (auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(WorkerID))
            {
                Path->Path.clear();
                Path->PathIndex = 0;
                Path->CurrentSpeed = 0.0f;
                Path->CurrentVelocity = {0.0f, 0.0f};
            }
            for (int I = 0; I < 4 && RaynorPlayer->PlayerID != 0; ++I)
                Campaign.Controller().Update(EM, 0.25f);
            if (RaynorPlayer->PlayerID != 0)
            {
                Fail(Error, "rescue passive Raynor did not transfer to the local player");
                return false;
            }
            Core::Utils::Logger::Info(
                "InteractionSmoke",
                "Interaction smoke rescued Raynor entity=" + std::to_string(RaynorID));
        }

        const int BarracksDefID = Game::Races::GetUnitDefIDByChkUnitID(0x6F);
        const int MarineDefID = Game::Races::GetUnitDefIDByChkUnitID(0x00);
        auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
        if (BarracksDefID < 0 || MarineDefID < 0 || !WorkerTrans)
        {
            Fail(Error, "missing Barracks/Marine definitions or worker transform for victory closure");
            return false;
        }

        EnsurePlayerResources(0);
        Core::Math::Vec2 BuildPos;
        if (!FindBuildPlacementNear(Session, EM, BarracksDefID, WorkerTrans->Position, 0, BuildPos))
        {
            Fail(Error, "could not find a valid Barracks placement");
            return false;
        }

        Hud.UpdateContext(EM, {WorkerID});
        int BarracksButtonIndex = -1;
        const auto* BarracksButton = FindHudButtonByTarget(
            Hud, UI::ActionKind::Build, BarracksDefID, &BarracksButtonIndex);
        if (!BarracksButton)
        {
            if (!VerifyGuiCommandClick(Gui, EM, {WorkerID}, Hud, 6, "SCV basic build for Barracks", Error))
                return false;
            if (!ExecuteHudCommandIndex(EM, {WorkerID}, Hud, 6))
            {
                Fail(Error, "HUD SCV basic build submenu did not open for Barracks");
                return false;
            }
            Hud.UpdateContext(EM, {WorkerID});
            BarracksButton = FindHudButtonByTarget(
                Hud, UI::ActionKind::Build, BarracksDefID, &BarracksButtonIndex);
        }
        if (!BarracksButton || !BarracksButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled Barracks build button");
            return false;
        }

        const auto BarracksDef = Game::Races::GetUnitDefByID(BarracksDefID);
        if (BarracksButton->MineralCost != BarracksDef.MineralCost ||
            BarracksButton->GasCost != BarracksDef.GasCost)
        {
            Fail(Error, "HUD Barracks build button costs do not match race data");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *BarracksButton, "Barracks build", Error))
            return false;
        if (!VerifyGuiCommandClick(
                Gui, EM, {WorkerID}, Hud, BarracksButtonIndex, "Barracks build", Error))
        {
            return false;
        }

        auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
        const int MineralsBeforeBarracks = Resources.Minerals;
        if (!ExecuteHudCommandIndex(EM, {WorkerID}, Hud, BarracksButtonIndex))
        {
            Fail(Error, "HUD Barracks build button did not enter placement mode");
            return false;
        }
        if (!Hud.HasPendingTarget() ||
            Hud.GetPendingTargetMode() != UI::PendingTargetMode::BuildPlacement ||
            Hud.GetPendingBuildDefID() != BarracksDefID)
        {
            Fail(Error, "HUD Barracks build click did not enter build placement mode");
            return false;
        }

        Game::Client::GameLoop::ClickTargetContext EmptyClickTarget;
        if (!ExecutePendingTargetCommand(
                EM,
                {WorkerID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                EmptyClickTarget,
                BuildPos))
        {
            Fail(Error, "HUD Barracks placement command did not start construction");
            return false;
        }
        Hud.CancelPendingTarget();
        if (Resources.Minerals >= MineralsBeforeBarracks)
        {
            Fail(Error, "Barracks construction did not deduct minerals");
            return false;
        }

        const int BarracksID = FindBuildingByDefAndBuilder(EM, BarracksDefID, WorkerID);
        auto* BarracksRender = EM.GetComponent<Game::Components::RenderComponent>(BarracksID);
        if (!EM.GetComponent<Game::Components::BuildingComponent>(BarracksID) ||
            !EM.GetComponent<Game::Components::UnitComponent>(BarracksID) ||
            !BarracksRender)
        {
            Fail(Error, "started Barracks missing construction components");
            return false;
        }
        if (!VerifyRenderableEntity(EM, Client, BarracksID, "Barracks construction", Error))
            return false;
        if (!VerifyIScriptAnim(BarracksRender->ImageID, Game::IScriptAnims::Anim_AlmostBuilt,
                               "Barracks construction", Error))
        {
            return false;
        }
        if (!CompleteTerranConstruction(Session, EM, BarracksID, WorkerID, "Barracks", Error))
            return false;
        if (!VerifyIScriptAnim(BarracksRender->ImageID, Game::IScriptAnims::Anim_Built,
                               "completed Barracks", Error))
        {
            return false;
        }

        for (int I = 0;
             I < 10 && Controller.GetState() == Game::Campaign::CampaignState::Playing &&
             !Controller.GetLeaderboard().Active;
             ++I)
        {
            Controller.Update(EM, 1.0f);
        }
        if (!Controller.GetLeaderboard().Active)
        {
            Fail(Error, "campaign did not enable Marine leaderboard after Barracks completion");
            return false;
        }

        Hud.UpdateContext(EM, {BarracksID});
        int MarinesNeeded = std::max(0, 10 - CountOwnedUnitsByChk(EM, 0, 0x00));
        const auto MarineDef = Game::Races::GetUnitDefByID(MarineDefID);
        const int MarineStartSupplyUsed = Resources.SupplyUsed;
        for (int I = 0; I < MarinesNeeded; ++I)
        {
            int MarineButtonIndex = -1;
            const auto* MarineButton = FindHudButtonByTarget(
                Hud, UI::ActionKind::Train, MarineDefID, &MarineButtonIndex);
            if (!MarineButton || !MarineButton->Enabled)
            {
                Fail(Error, "HUD did not expose an enabled Marine train button");
                return false;
            }
            if (MarineButton->MineralCost != MarineDef.MineralCost ||
                MarineButton->GasCost != MarineDef.GasCost ||
                MarineButton->SupplyCost != MarineDef.SupplyCost)
            {
                Fail(Error, "HUD Marine train button costs do not match race data");
                return false;
            }
            if (I == 0)
            {
                if (!VerifyHudButtonIcon(Hud, *MarineButton, "Marine train", Error))
                    return false;
                if (!VerifyGuiCommandClick(
                        Gui, EM, {BarracksID}, Hud, MarineButtonIndex, "Marine train", Error))
                {
                    return false;
                }
            }
            if (!ExecuteHudCommandIndex(EM, {BarracksID}, Hud, MarineButtonIndex))
            {
                Fail(Error, "HUD Marine train button did not queue training");
                return false;
            }
            Hud.UpdateContext(EM, {BarracksID});
            const int ExpectedSupplyUsed =
                MarineStartSupplyUsed + (I + 1) * MarineDef.SupplyCost;
            if (Resources.SupplyUsed != ExpectedSupplyUsed)
            {
                Fail(Error,
                     "Marine training did not reserve supply; queued=" +
                         std::to_string(I + 1) +
                         " supply=" + std::to_string(Resources.SupplyUsed) +
                         " expected=" + std::to_string(ExpectedSupplyUsed));
                return false;
            }
            const auto& QueueInfo = Hud.GetState().InfoPanel;
            if ((int)QueueInfo.QueueEntries.size() < I + 1 ||
                QueueInfo.QueueEntries[(size_t)I] != "Marine")
            {
                Fail(Error,
                     "HUD info panel did not expose queued Marine; queued=" +
                         std::to_string(I + 1));
                return false;
            }
        }

        if (MarinesNeeded > 0)
            TickSystems(Session, MarinesNeeded * std::max(1, MarineDef.BuildTime) + 4, 1.0f);
        if (MarinesNeeded > 0)
        {
            const int ExpectedMarineSupplyUsed =
                MarineStartSupplyUsed + MarinesNeeded * MarineDef.SupplyCost;
            if (Resources.SupplyUsed != ExpectedMarineSupplyUsed)
            {
                Fail(Error,
                     "completed Marine training did not keep supply occupied; supply=" +
                         std::to_string(Resources.SupplyUsed) +
                         " expected=" + std::to_string(ExpectedMarineSupplyUsed));
                return false;
            }
            Hud.UpdateContext(EM, {BarracksID});
            if (!Hud.GetState().InfoPanel.QueueEntries.empty())
            {
                Fail(Error, "HUD info panel still showed queued Marine after production completed");
                return false;
            }
        }
        const int MarineCount = CountOwnedUnitsByChk(EM, 0, 0x00);
        const int BarracksCount = CountOwnedUnitsByChk(EM, 0, 0x6F);
        const int RaynorCount = CountOwnedUnitsByChk(EM, 0, 19);
        if (MarineCount < 10 || BarracksCount < 1 || RaynorCount < 1)
        {
            Fail(Error,
                 "victory unit counts are incomplete; marines=" +
                     std::to_string(MarineCount) +
                     " barracks=" + std::to_string(BarracksCount) +
                     " raynor=" + std::to_string(RaynorCount));
            return false;
        }

        const int NewestMarineID = FindNewestOwnedUnitByChk(EM, 0, 0x00);
        if (!VerifyRenderableEntity(EM, Client, NewestMarineID, "victory Marine", Error))
            return false;

        for (int I = 0;
             I < 16 && Controller.GetState() == Game::Campaign::CampaignState::Playing;
             ++I)
        {
            Controller.Update(EM, 1.0f);
        }
        if (Controller.GetState() != Game::Campaign::CampaignState::Victory)
        {
            Fail(Error,
                 "campaign did not enter victory after objective completion; state=" +
                     std::to_string((int)Controller.GetState()) +
                     " marines=" + std::to_string(MarineCount) +
                     " barracks=" + std::to_string(BarracksCount) +
                     " raynor=" + std::to_string(RaynorCount));
            return false;
        }

        const auto& Objectives = Controller.GetObjectives();
        if (Objectives.empty())
        {
            Fail(Error, "campaign victory left objectives empty");
            return false;
        }
        for (const auto& Objective : Objectives)
        {
            if (!Objective.Complete)
            {
                Fail(Error, "campaign victory did not complete every objective");
                return false;
            }
        }
        const auto& Leaderboard = Controller.GetLeaderboard();
        if (!Leaderboard.Active || Leaderboard.UnitID != 0 || Leaderboard.CurrentValue < 10)
        {
            Fail(Error,
                 "campaign leaderboard did not track Marine victory progress; active=" +
                     std::to_string(Leaderboard.Active ? 1 : 0) +
                     " unit=" + std::to_string(Leaderboard.UnitID) +
                     " value=" + std::to_string(Leaderboard.CurrentValue));
            return false;
        }

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke campaign victory: marines=" +
                std::to_string(MarineCount) +
                " barracks=" + std::to_string(BarracksCount) +
                " raynor=" + std::to_string(RaynorCount) +
                " objectives=" + std::to_string(Objectives.size()) +
                " leaderboard=" + std::to_string(Leaderboard.CurrentValue));
        return true;
    }

    inline bool HasUnsupportedTriggerWork(const std::array<int, 256>& Counts)
    {
        for (int Count : Counts)
            if (Count > 0)
                return true;
        return false;
    }

    inline bool VerifyCampaignScript(GameSession& Session,
                                     CampaignRuntime& Campaign,
                                     std::string& Error)
    {
        const auto& ScriptData = Session.GetScriptData();
        if (ScriptData.Triggers.empty())
        {
            Fail(Error, "loaded map has no parsed CHK triggers");
            return false;
        }
        if (ScriptData.Locations.empty())
        {
            Fail(Error, "loaded map has no parsed CHK locations");
            return false;
        }
        if (!Campaign.IsActive())
        {
            Fail(Error, "campaign scripting runtime is not active for parsed CHK data");
            return false;
        }

        auto& Controller = Campaign.Controller();
        auto* TriggerVM = Controller.GetTriggerVM();
        if (!TriggerVM || !TriggerVM->HasScript())
        {
            Fail(Error, "campaign controller has no runnable TriggerVM script");
            return false;
        }
        if (TriggerVM->GetTriggerCount() != ScriptData.Triggers.size())
        {
            Fail(Error,
                 "TriggerVM trigger count does not match parsed CHK data; vm=" +
                     std::to_string(TriggerVM->GetTriggerCount()) +
                     " parsed=" + std::to_string(ScriptData.Triggers.size()));
            return false;
        }
        if (TriggerVM->GetLocationCount() != ScriptData.Locations.size())
        {
            Fail(Error,
                 "TriggerVM location count does not match parsed CHK data; vm=" +
                     std::to_string(TriggerVM->GetLocationCount()) +
                     " parsed=" + std::to_string(ScriptData.Locations.size()));
            return false;
        }

        Controller.Update(Session.Entities(), 0.25f);
        const auto& Objectives = Controller.GetObjectives();
        const auto& Messages = Controller.GetMessages();
        if (Objectives.empty())
        {
            Fail(Error, "campaign script update did not produce mission objectives");
            return false;
        }
        if (Messages.empty())
        {
            Fail(Error, "campaign script update did not produce mission messages");
            return false;
        }
        const std::string ParsedMissionName =
            ScriptData.GetString(ScriptData.ScenarioNameStringID);
        if (!ParsedMissionName.empty() &&
            Controller.GetMissionName() != ParsedMissionName)
        {
            Fail(Error, "campaign script mission name does not match CHK scenario name");
            return false;
        }
        if (Controller.GetState() == Game::Campaign::CampaignState::Defeat ||
            Controller.GetState() == Game::Campaign::CampaignState::Draw)
        {
            Fail(Error, "campaign script entered an outcome state during interaction smoke");
            return false;
        }
        if (HasUnsupportedTriggerWork(TriggerVM->GetUnsupportedConditionCounts()))
        {
            Fail(Error, "campaign script hit unsupported trigger conditions");
            return false;
        }
        if (HasUnsupportedTriggerWork(TriggerVM->GetUnsupportedActionCounts()))
        {
            Fail(Error, "campaign script hit unsupported trigger actions");
            return false;
        }

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke campaign script: triggers=" +
                std::to_string(TriggerVM->GetTriggerCount()) +
                " locations=" + std::to_string(TriggerVM->GetLocationCount()) +
                " objectives=" + std::to_string(Objectives.size()) +
                " messages=" + std::to_string(Messages.size()) +
                " mission=\"" + Controller.GetMissionName() + "\"" +
                " state=" + std::to_string((int)Controller.GetState()));
        return true;
    }

    inline bool VerifyAttack(GameSession& Session,
                             Core::ECS::EntityManager& EM,
                             ClientRuntime& Client,
                             GuiRuntime& Gui,
                             UI::Hud& Hud,
                             int AttackerID,
                             int TargetID,
                             std::string& Error)
    {
        auto* AttackerTrans = EM.GetComponent<Game::Components::TransformComponent>(AttackerID);
        auto* TargetTrans = EM.GetComponent<Game::Components::TransformComponent>(TargetID);
        auto* TargetUnit = EM.GetComponent<Game::Components::UnitComponent>(TargetID);
        auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(AttackerID);
        auto* AttackerRender = EM.GetComponent<Game::Components::RenderComponent>(AttackerID);
        if (!AttackerTrans || !TargetTrans || !TargetUnit || !Combat || !AttackerRender)
        {
            Fail(Error, "attack smoke entities missing required components");
            return false;
        }
        if (!VerifyIScriptAnim(AttackerRender->ImageID, Game::IScriptAnims::Anim_GndAttkInit,
                               "attacking unit", Error))
        {
            return false;
        }

        const float RangePixels = std::max(24.0f, Combat->AttackRange * 32.0f);
        const float TargetOffset = RangePixels >= 72.0f
            ? std::min(56.0f, RangePixels - 8.0f)
            : 56.0f;
        Core::Math::Vec2 TargetPosition =
            AttackerTrans->Position + Core::Math::Vec2{TargetOffset, 0.0f};
        TargetTrans->Position = TargetPosition;

        const std::vector<int> Selected = {AttackerID};
        Hud.UpdateContext(EM, Selected);
        int AttackButtonIndex = -1;
        const auto* AttackButton = FindHudButtonByCommand(
            Hud, UI::HudCommandId::AttackMove, &AttackButtonIndex);
        if (!AttackButton || !AttackButton->Enabled)
        {
            Fail(Error, "HUD did not expose an enabled Attack command button");
            return false;
        }
        if (!VerifyHudButtonIcon(Hud, *AttackButton, "Attack command", Error))
            return false;
        if (!VerifyGuiCommandClick(Gui, EM, Selected, Hud, AttackButtonIndex, "Attack command", Error))
            return false;
        if (!ExecuteHudCommandIndex(EM, Selected, Hud, AttackButtonIndex))
        {
            Fail(Error, "HUD Attack command button did not enter targeting mode");
            return false;
        }
        if (!Hud.HasPendingTarget() ||
            Hud.GetPendingTargetMode() != UI::PendingTargetMode::AttackMove)
        {
            Fail(Error, "HUD Attack command did not enter attack targeting mode");
            return false;
        }

        auto Target = Client.ResolveClickTargetContext(EM, Selected, TargetTrans->Position);
        if (!ExecutePendingTargetCommand(
                EM,
                Selected,
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                Target,
                TargetTrans->Position))
        {
            Fail(Error, "HUD Attack pending target did not issue an attack");
            return false;
        }
        Hud.CancelPendingTarget();
        if (Hud.HasPendingTarget())
        {
            Fail(Error, "HUD Attack targeting mode did not clear after accepted target");
            return false;
        }

        auto* Command = EM.GetComponent<Game::Components::CommandComponent>(AttackerID);
        const int ActualTargetID = Target.EnemyEntityID;
        if (ActualTargetID < 0)
        {
            Fail(Error, "enemy click did not resolve an attackable target");
            return false;
        }
        auto* ActualTargetUnit = EM.GetComponent<Game::Components::UnitComponent>(ActualTargetID);
        if (!ActualTargetUnit)
        {
            Fail(Error, "resolved enemy target missing unit component");
            return false;
        }
        if (!Command || Command->CurrentCommand != Game::Components::CommandType::Attack ||
            Command->TargetEntityID != ActualTargetID)
        {
            Fail(Error, "HUD Attack target did not set attack command");
            return false;
        }
        if (!Combat || Combat->TargetEntityID != ActualTargetID)
        {
            Fail(Error, "HUD Attack target did not update combat target");
            return false;
        }

        const int StartHp = ActualTargetUnit->HitPoints;
        const int StartShield = ActualTargetUnit->ShieldPoints;
        TickSystems(Session, 1, 0.1f);
        const bool TargetDestroyedAfterFirstTick = !EM.IsAlive(ActualTargetID);
        const auto* FirstTickTargetUnit = TargetDestroyedAfterFirstTick
            ? nullptr
            : EM.GetComponent<Game::Components::UnitComponent>(ActualTargetID);
        const bool TargetDamagedAfterFirstTick =
            TargetDestroyedAfterFirstTick ||
            (FirstTickTargetUnit &&
             (FirstTickTargetUnit->HitPoints < StartHp ||
              FirstTickTargetUnit->ShieldPoints < StartShield));
        if (!Combat->PendingAttackFrame && !TargetDamagedAfterFirstTick)
        {
            Fail(Error, "combat system did not enter an attack animation frame");
            return false;
        }

        bool SawRenderableBullet = false;
        for (int Tick = 0; Tick < 79; ++Tick)
        {
            TickSystems(Session, 1, 0.1f);
            for (int BulletID : EM.GetEntitiesWith<Game::Components::BulletComponent>())
            {
                const auto* Bullet = EM.GetComponent<Game::Components::BulletComponent>(BulletID);
                const auto* BulletRender = EM.GetComponent<Game::Components::RenderComponent>(BulletID);
                if (!Bullet || Bullet->SourceEntityID != AttackerID || !BulletRender)
                    continue;
                const auto* Frames = Client.GetSpriteAssets().GetDatImageFrames(BulletRender->ImageID);
                if (Frames && !Frames->empty() && (*Frames)[0].Valid())
                    SawRenderableBullet = true;
            }
        }
        if (Combat->IsRanged && !SawRenderableBullet)
        {
            Fail(Error, "ranged attack did not produce a renderable weapons.dat projectile");
            return false;
        }
        const bool TargetDestroyed = !EM.IsAlive(ActualTargetID);
        const auto* UpdatedTargetUnit = TargetDestroyed
            ? nullptr
            : EM.GetComponent<Game::Components::UnitComponent>(ActualTargetID);
        const bool TargetDamaged =
            TargetDestroyed ||
            (UpdatedTargetUnit &&
             (UpdatedTargetUnit->HitPoints < StartHp ||
              UpdatedTargetUnit->ShieldPoints < StartShield));
        if (!TargetDamaged)
        {
            Fail(Error, "combat system did not damage the clicked enemy");
            return false;
        }
        return true;
    }

    inline bool Run(GameSession& Session,
                    CampaignRuntime& Campaign,
                    ClientRuntime& Client,
                    GuiRuntime& Gui,
                    UI::Hud& Hud,
                    std::string& Error)
    {
        auto& EM = Session.Entities();
        const int LocalPlayerID = Session.GetLocalPlayerID();
        if (Client.GetLocalPlayerId() != LocalPlayerID)
        {
            Fail(Error, "client selection local player does not match the loaded CHK human slot");
            return false;
        }
        if (Campaign.IsActive() &&
            Campaign.Controller().GetLocalPlayerID() != LocalPlayerID)
        {
            Fail(Error, "campaign outcome local player does not match the loaded CHK human slot");
            return false;
        }
        if (Campaign.IsActive() &&
            Campaign.Controller().GetState() != Game::Campaign::CampaignState::Playing)
        {
            Fail(Error, "loaded scene entered an outcome before gameplay interaction");
            return false;
        }
        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke initial scene remained playable after load: local_player=" +
                std::to_string(LocalPlayerID));

        if (IsFullRosterOnly())
        {
            FullRosterSpawnResult FullRoster;
            if (!SpawnFullRoster(Session, FullRoster, Error))
                return false;
            if (!FullRoster.EntityIDs.empty())
            {
                const int FocusID = FullRoster.EntityIDs.front();
                Client.SetSelectedIdsForDemo({FocusID});
                Hud.UpdateContext(EM, {FocusID});
                if (const auto* Transform =
                        EM.GetComponent<Game::Components::TransformComponent>(FocusID))
                {
                    Client.CenterCamera(Transform->Position.X, Transform->Position.Y);
                }
            }
            Core::Utils::Logger::Info(
                "InteractionSmoke",
                "Interaction smoke passed: roster-only buildings=" +
                    std::to_string(FullRoster.BuildingCount) +
                    " units=" + std::to_string(FullRoster.UnitCount) +
                    " total=" + std::to_string(FullRoster.EntityIDs.size()));
            return true;
        }

        const int ResourceID = FindResource(EM);
        if (ResourceID < 0)
        {
            Fail(Error, "no active resource found in the loaded map");
            return false;
        }

        int WorkerID = FindWorker(EM, 0, ResourceID);
        if (WorkerID < 0)
            WorkerID = SpawnSmokeWorker(Session, 0, ResourceID);
        if (WorkerID < 0)
        {
            Fail(Error, "no local worker available for economy verification");
            return false;
        }

        int AttackerID = -1;
        int TargetID = -1;
        Core::Math::Vec2 CombatCenter = {0.0f, 0.0f};
        if (auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID))
            CombatCenter = WorkerTrans->Position;
        if (!SpawnSmokeCombatPair(Session, CombatCenter, AttackerID, TargetID))
        {
            TargetID = FindAttackTarget(EM, 0);
            if (TargetID < 0)
            {
                Fail(Error, "no enemy target found in the loaded map");
                return false;
            }

            AttackerID = FindAttacker(EM, 0, TargetID);
            if (AttackerID < 0)
            {
                Fail(Error, "no local attacker can target the enemy");
                return false;
            }
        }

        if (!VerifySelection(EM, Client, WorkerID, Error))
            return false;
        if (!VerifyRenderableEntity(EM, Client, WorkerID, "selected worker", Error))
            return false;
        if (!VerifyMove(Session, EM, Gui, Hud, WorkerID, Error))
            return false;
        if (!VerifyEightWayMovementAnimation(Session, EM, Client, WorkerID, Error))
            return false;
        int DepotID = -1;
        if (!VerifyHarvest(Session, EM, Client, Gui, Hud, WorkerID, ResourceID, DepotID, Error))
            return false;
        if (!VerifyRenderableEntity(EM, Client, DepotID, "smoke Command Center", Error))
            return false;
        int TrainedUnitID = -1;
        if (!VerifyProduction(Session, EM, Client, Gui, Hud, DepotID, TrainedUnitID, Error))
            return false;
        int BuiltBuildingID = -1;
        if (!VerifyBuildingConstruction(Session, EM, Client, Gui, Hud, WorkerID, BuiltBuildingID, Error))
            return false;
        if (!VerifyResearch(Session, EM, Client, Gui, Hud, WorkerID, Error))
            return false;
        int BarracksID = -1;
        int MarineID = -1;
        if (!VerifyBarracksMarineProduction(
                Session, EM, Client, Gui, Hud, WorkerID, BarracksID, MarineID, Error))
        {
            return false;
        }
        if (!IsAliveUnit(EM, AttackerID) || !IsAliveUnit(EM, TargetID))
        {
            Core::Math::Vec2 RefreshedCombatCenter = {0.0f, 0.0f};
            if (auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID))
                RefreshedCombatCenter = WorkerTrans->Position;
            if (!SpawnSmokeCombatPair(Session, RefreshedCombatCenter, AttackerID, TargetID))
            {
                Fail(Error, "could not refresh combat pair before attack verification");
                return false;
            }
        }
        if (!VerifyRenderableEntity(EM, Client, AttackerID, "smoke attacker", Error) ||
            !VerifyRenderableEntity(EM, Client, TargetID, "smoke target", Error))
        {
            return false;
        }
        if (!VerifyAttack(Session, EM, Client, Gui, Hud, AttackerID, TargetID, Error))
            return false;
        if (!VerifyCampaignScript(Session, Campaign, Error))
            return false;
        if (!VerifyCampaignVictoryClosure(Session, Campaign, EM, Client, Gui, Hud, WorkerID, Error))
            return false;

        FullRosterSpawnResult FullRoster;
        if (!SpawnFullRoster(Session, FullRoster, Error))
            return false;

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke passed: worker=" + std::to_string(WorkerID) +
                " resource=" + std::to_string(ResourceID) +
                " depot=" + std::to_string(DepotID) +
                " trained=" + std::to_string(TrainedUnitID) +
                " building=" + std::to_string(BuiltBuildingID) +
                " barracks=" + std::to_string(BarracksID) +
                " marine=" + std::to_string(MarineID) +
                " attacker=" + std::to_string(AttackerID) +
                " target=" + std::to_string(TargetID) +
                " roster_buildings=" + std::to_string(FullRoster.BuildingCount) +
                " roster_units=" + std::to_string(FullRoster.UnitCount));
        return true;
    }
}

inline void RunInteractionSmokeIfRequested(GameInteractionSmokeState& State,
                                           GameSession& Session,
                                           CampaignRuntime& Campaign,
                                           ClientRuntime& Client,
                                           GuiRuntime& Gui,
                                           UI::Hud& Hud)
{
    if (State.Attempted || !InteractionSmoke::IsEnabled())
        return;
    if (!Session.IsEcsInitialized() || !Session.IsMapLoaded())
        return;

    State.Attempted = true;
    std::string Error;
    if (!InteractionSmoke::Run(Session, Campaign, Client, Gui, Hud, Error))
        Core::Utils::Logger::Error("InteractionSmoke", "Interaction smoke failed: " + Error);
}

namespace InteractionDemo
{
    inline bool IsEnabled(const GameInteractionDemoState& State)
    {
        if (State.Enabled)
            return true;
        const char* Value = std::getenv("STARCRAFT_INTERACTION_DEMO");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    inline bool IsAiShowcase()
    {
        const char* Value = std::getenv("STARCRAFT_AI_SHOWCASE");
        return Value && Value[0] != '\0' && Value[0] != '0';
    }

    inline void SelectAndFocus(GameInteractionDemoState& State,
                               ClientRuntime& Client,
                               Core::ECS::EntityManager& EM,
                               int EntityID);
    inline void KeepFocus(GameInteractionDemoState& State,
                          ClientRuntime& Client,
                          Core::ECS::EntityManager& EM);

    inline float RunAiShowcase(GameInteractionDemoState& State,
                               GameSession& Session,
                               ClientRuntime& Client,
                               UI::Hud& Hud,
                               float Dt)
    {
        constexpr float kShowcaseSimulationScale = 8.0f;
        auto& EM = Session.Entities();
        if (!State.Started)
        {
            State.Started = true;
            State.StageSeconds = 0.0f;
            Core::Utils::Logger::Info(
                "InteractionDemo",
                "visible AI showcase started at 8x simulation speed; no roster debug spawn");
        }

        State.StageSeconds += Dt;
        KeepFocus(State, Client, EM);
        if (State.StageSeconds < 0.75f)
            return kShowcaseSimulationScale;
        State.StageSeconds = 0.0f;

        const int PlayerID = Session.GetLocalPlayerID();
        int FocusID = -1;
        for (int Eid : EM.GetEntitiesWith<Game::Components::BuildingComponent,
                                          Game::Components::PlayerComponent>())
        {
            auto* B = EM.GetComponent<Game::Components::BuildingComponent>(Eid);
            auto* P = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
            if (!B || !P || P->PlayerID != PlayerID)
                continue;
            if (B->UnderConstruction)
                FocusID = std::max(FocusID, Eid);
            else if (auto* Prod = EM.GetComponent<Game::Components::ProductionComponent>(Eid);
                     Prod && !Prod->Queue.empty() && FocusID < 0)
                FocusID = Eid;
        }
        if (FocusID < 0)
        {
            for (int Eid : EM.GetEntitiesWith<Game::Components::UnitComponent,
                                              Game::Components::PlayerComponent>())
            {
                auto* P = EM.GetComponent<Game::Components::PlayerComponent>(Eid);
                if (P && P->PlayerID == PlayerID)
                    FocusID = std::max(FocusID, Eid);
            }
        }
        if (FocusID >= 0)
        {
            SelectAndFocus(State, Client, EM, FocusID);
            Hud.UpdateContext(EM, {FocusID});
        }
        return kShowcaseSimulationScale;
    }

    inline const char* StageName(GameInteractionDemoState::Stage Stage)
    {
        using StageT = GameInteractionDemoState::Stage;
        switch (Stage)
        {
            case StageT::Init: return "init";
            case StageT::HarvestLoop: return "harvest";
            case StageT::ProductionQueue: return "production";
            case StageT::Construction: return "construction";
            case StageT::GasConstruction: return "gas construction";
            case StageT::GasHarvest: return "gas harvest";
            case StageT::BarracksConstruction: return "barracks";
            case StageT::MarineProduction: return "marine production";
            case StageT::Research: return "research";
            case StageT::FullRoster: return "full roster";
            case StageT::Observe: return "observe";
            case StageT::Done: return "done";
            case StageT::Failed: return "failed";
        }
        return "unknown";
    }

    inline void SetStage(GameInteractionDemoState& State,
                         GameInteractionDemoState::Stage Stage)
    {
        State.CurrentStage = Stage;
        State.StageSeconds = 0.0f;
        Core::Utils::Logger::Info(
            "InteractionDemo",
            std::string("demo stage: ") + StageName(Stage));
    }

    inline void Fail(GameInteractionDemoState& State, const std::string& Message)
    {
        Core::Utils::Logger::Error("InteractionDemo", "demo failed: " + Message);
        SetStage(State, GameInteractionDemoState::Stage::Failed);
    }

    inline std::string SelectionEntityName(Core::ECS::EntityManager& EM, int EntityID)
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        if (!Unit)
            return "Unknown";

        const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
        if (DefID < 0)
            return "CHK_" + std::to_string(Unit->ChkUnitID);

        const auto Def = Game::Races::GetUnitDefByID(DefID);
        return Def.Name.empty() ? ("CHK_" + std::to_string(Unit->ChkUnitID)) : Def.Name;
    }

    inline const char* SelectionEntityKind(Core::ECS::EntityManager& EM, int EntityID)
    {
        if (EM.GetComponent<Game::Components::ResourceComponent>(EntityID))
            return "resource";
        if (EM.GetComponent<Game::Components::BuildingComponent>(EntityID))
            return "building";
        if (EM.GetComponent<Game::Components::UnitComponent>(EntityID))
            return "unit";
        return "entity";
    }

    inline void LogDemoSelection(Core::ECS::EntityManager& EM, int EntityID)
    {
        auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
        if (!Unit)
            return;

        auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
        auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
        Core::Utils::Logger::Info(
            "SelectionTrace",
            "demo selected id=" + std::to_string(EntityID) +
                " kind=" + SelectionEntityKind(EM, EntityID) +
                " name=" + SelectionEntityName(EM, EntityID) +
                " chk=" + std::to_string(Unit->ChkUnitID) +
                " player=" + std::to_string(Player ? Player->PlayerID : -1) +
                " pos=" + std::to_string(Trans ? (int)Trans->Position.X : 0) +
                "," + std::to_string(Trans ? (int)Trans->Position.Y : 0));
    }

    inline void SelectAndFocus(GameInteractionDemoState& State,
                               ClientRuntime& Client,
                               Core::ECS::EntityManager& EM,
                               int EntityID)
    {
        if (EntityID < 0 || !EM.IsAlive(EntityID))
            return;
        const bool SelectionChanged = State.FocusEntityID != EntityID;
        State.FocusEntityID = EntityID;
        if (SelectionChanged)
        {
            Client.SetSelectedIdsForDemo({EntityID});
            LogDemoSelection(EM, EntityID);
        }
        if (auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID))
            Client.CenterCamera(Trans->Position.X, Trans->Position.Y);
    }

    inline void KeepFocus(GameInteractionDemoState& State,
                          ClientRuntime& Client,
                          Core::ECS::EntityManager& EM)
    {
        if (State.FocusEntityID < 0 || !EM.IsAlive(State.FocusEntityID))
            return;
        if (auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(State.FocusEntityID))
            Client.CenterCamera(Trans->Position.X, Trans->Position.Y);
    }

    inline bool StartHarvest(GameInteractionDemoState& State,
                             GameSession& Session,
                             ClientRuntime& Client,
                             UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        SelectAndFocus(State, Client, EM, State.WorkerID);
        Hud.UpdateContext(EM, {State.WorkerID});
        int HarvestButtonIndex = -1;
        const auto* HarvestButton = InteractionSmoke::FindHudButtonByCommand(
            Hud, UI::HudCommandId::Harvest, &HarvestButtonIndex);
        if (!HarvestButton || !HarvestButton->Enabled)
            return false;
        if (!ExecuteHudCommandIndex(EM, {State.WorkerID}, Hud, HarvestButtonIndex))
            return false;
        auto* ResourceTrans =
            EM.GetComponent<Game::Components::TransformComponent>(State.ResourceID);
        if (!ResourceTrans)
            return false;
        auto Target = Client.ResolveClickTargetContext(
            EM, {State.WorkerID}, ResourceTrans->Position);
        if (!ExecutePendingTargetCommand(
                EM,
                {State.WorkerID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                Target,
                ResourceTrans->Position))
        {
            return false;
        }
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo issued harvest worker=" + std::to_string(State.WorkerID) +
                " resource=" + std::to_string(State.ResourceID));
        return true;
    }

    inline bool StartProduction(GameInteractionDemoState& State,
                                GameSession& Session,
                                ClientRuntime& Client,
                                UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        SelectAndFocus(State, Client, EM, State.DepotID);
        Hud.UpdateContext(EM, {State.DepotID});

        const int ScvDefID = Game::Races::GetUnitDefID("SCV");
        int TrainButtonIndex = -1;
        const auto* TrainButton = InteractionSmoke::FindHudButtonByTarget(
            Hud, UI::ActionKind::Train, ScvDefID, &TrainButtonIndex);
        if (!TrainButton || !TrainButton->Enabled)
            return false;

        State.TrainedUnitStartCount =
            InteractionSmoke::CountOwnedUnitsByChk(EM, 0, 0x07);
        if (!ExecuteHudCommandIndex(EM, {State.DepotID}, Hud, TrainButtonIndex))
            return false;
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo queued SCV production depot=" + std::to_string(State.DepotID));
        return true;
    }

    inline bool StartConstruction(GameInteractionDemoState& State,
                                  GameSession& Session,
                                  ClientRuntime& Client,
                                  UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        const int SupplyDepotDefID = Game::Races::GetUnitDefID("SupplyDepot");
        auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(State.WorkerID);
        if (SupplyDepotDefID < 0 || !WorkerTrans)
        {
            Core::Utils::Logger::Warning(
                "InteractionDemo",
                "demo construction failed: missing Supply Depot def or worker transform");
            return false;
        }

        Core::Math::Vec2 PlacementCenter = WorkerTrans->Position;
        Core::Math::Vec2 BuildPos;
        if (!InteractionSmoke::FindBuildPlacementNear(
                Session, EM, SupplyDepotDefID, PlacementCenter, 0, BuildPos))
        {
            BuildPos = PlacementCenter + Core::Math::Vec2{192.0f, 96.0f};
            State.BuildSiteID =
                Game::Systems::BuildingSystem::DebugSpawnBuildingUnderConstruction(
                    EM, SupplyDepotDefID, BuildPos, 0, State.WorkerID);
            if (State.BuildSiteID < 0)
            {
                Core::Utils::Logger::Warning(
                    "InteractionDemo",
                    "demo construction failed: no valid Supply Depot placement near base");
                return false;
            }
            if (auto* NewWorkerTrans =
                    EM.GetComponent<Game::Components::TransformComponent>(State.WorkerID))
            {
                NewWorkerTrans->Position = BuildPos + Core::Math::Vec2{24.0f, 0.0f};
            }
            SelectAndFocus(State, Client, EM, State.BuildSiteID);
            Core::Utils::Logger::Warning(
                "InteractionDemo",
                "demo fallback spawned under-construction Supply Depot entity=" +
                    std::to_string(State.BuildSiteID));
            return true;
        }

        SelectAndFocus(State, Client, EM, State.WorkerID);
        Hud.UpdateContext(EM, {State.WorkerID});
        if (!ExecuteHudCommandIndex(EM, {State.WorkerID}, Hud, 6))
        {
            Core::Utils::Logger::Warning(
                "InteractionDemo",
                "demo construction failed: SCV basic build submenu did not open");
            return false;
        }
        Hud.UpdateContext(EM, {State.WorkerID});

        int BuildButtonIndex = -1;
        const auto* BuildButton = InteractionSmoke::FindHudButtonByTarget(
            Hud, UI::ActionKind::Build, SupplyDepotDefID, &BuildButtonIndex);
        if (!BuildButton || !BuildButton->Enabled)
        {
            Core::Utils::Logger::Warning(
                "InteractionDemo",
                "demo construction failed: Supply Depot build button unavailable");
            return false;
        }
        if (!ExecuteHudCommandIndex(EM, {State.WorkerID}, Hud, BuildButtonIndex))
        {
            Core::Utils::Logger::Warning(
                "InteractionDemo",
                "demo construction failed: Supply Depot build button did not enter placement");
            return false;
        }

        Game::Client::GameLoop::ClickTargetContext EmptyTarget;
        if (!ExecutePendingTargetCommand(
                EM,
                {State.WorkerID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                EmptyTarget,
                BuildPos))
        {
            Core::Utils::Logger::Warning(
                "InteractionDemo",
                "demo construction failed: placement command rejected at " +
                    std::to_string((int)BuildPos.X) + "," +
                    std::to_string((int)BuildPos.Y));
            return false;
        }
        Hud.CancelPendingTarget();
        State.BuildSiteID = InteractionSmoke::FindBuildingByDefAndBuilder(
            EM, SupplyDepotDefID, State.WorkerID);
        if (State.BuildSiteID < 0)
        {
            Core::Utils::Logger::Warning(
                "InteractionDemo",
                "demo construction failed: build site not created for worker");
            return false;
        }
        SelectAndFocus(State, Client, EM, State.BuildSiteID);
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo started Supply Depot construction entity=" +
                std::to_string(State.BuildSiteID));
        return true;
    }

    inline int FindNearestGeyser(Core::ECS::EntityManager& EM, Core::Math::Vec2 Center);
    inline int FindAvailableScvNear(Core::ECS::EntityManager& EM,
                                    int PlayerID,
                                    Core::Math::Vec2 Center,
                                    int ExcludedID = -1);

    inline bool StartRefineryConstruction(GameInteractionDemoState& State,
                                          GameSession& Session,
                                          ClientRuntime& Client,
                                          UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        const int RefineryDefID = Game::Races::GetUnitDefID("Refinery");
        Core::Math::Vec2 Center{0.0f, 0.0f};
        if (auto* DepotTrans = EM.GetComponent<Game::Components::TransformComponent>(State.DepotID))
            Center = DepotTrans->Position;
        const int GeyserID = FindNearestGeyser(EM, Center);
        if (RefineryDefID < 0 || GeyserID < 0)
            return false;

        State.WorkerID = FindAvailableScvNear(EM, 0, Center);
        if (State.WorkerID < 0)
            return false;

        SelectAndFocus(State, Client, EM, State.WorkerID);
        Hud.UpdateContext(EM, {State.WorkerID});
        if (!ExecuteHudCommandIndex(EM, {State.WorkerID}, Hud, 6))
            return false;
        Hud.UpdateContext(EM, {State.WorkerID});

        int BuildButtonIndex = -1;
        const auto* BuildButton = InteractionSmoke::FindHudButtonByTarget(
            Hud, UI::ActionKind::Build, RefineryDefID, &BuildButtonIndex);
        if (!BuildButton || !BuildButton->Enabled)
            return false;
        if (!ExecuteHudCommandIndex(EM, {State.WorkerID}, Hud, BuildButtonIndex))
            return false;

        Game::Client::GameLoop::ClickTargetContext Target;
        Target.AnyEntityID = GeyserID;
        Target.ResourceEntityID = GeyserID;
        Core::Math::Vec2 TargetPos{0.0f, 0.0f};
        if (auto* GeyserTrans = EM.GetComponent<Game::Components::TransformComponent>(GeyserID))
            TargetPos = GeyserTrans->Position;
        if (!ExecutePendingTargetCommand(
                EM,
                {State.WorkerID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                Target,
                TargetPos))
        {
            return false;
        }
        Hud.CancelPendingTarget();
        State.RefineryID = GeyserID;
        SelectAndFocus(State, Client, EM, State.RefineryID);
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo started Refinery construction entity=" +
                std::to_string(State.RefineryID) +
                " worker=" + std::to_string(State.WorkerID));
        return true;
    }

    inline bool StartGasHarvest(GameInteractionDemoState& State,
                                GameSession& Session,
                                ClientRuntime& Client,
                                UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        if (State.RefineryID < 0 || !EM.IsAlive(State.RefineryID))
            return false;
        auto* RefineryBuilding =
            EM.GetComponent<Game::Components::BuildingComponent>(State.RefineryID);
        if (!RefineryBuilding || RefineryBuilding->UnderConstruction)
            return false;

        Core::Math::Vec2 Center{0.0f, 0.0f};
        if (auto* RefineryTrans =
                EM.GetComponent<Game::Components::TransformComponent>(State.RefineryID))
            Center = RefineryTrans->Position;
        State.GasWorkerID = FindAvailableScvNear(EM, 0, Center);
        if (State.GasWorkerID < 0)
            State.GasWorkerID = State.WorkerID;
        if (State.GasWorkerID < 0 ||
            !Game::Systems::HarvestSystem::IssueHarvest(EM, {State.GasWorkerID}, State.RefineryID))
        {
            return false;
        }
        SelectAndFocus(State, Client, EM, State.GasWorkerID);
        Hud.UpdateContext(EM, {State.GasWorkerID});
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo issued gas harvest worker=" + std::to_string(State.GasWorkerID) +
                " refinery=" + std::to_string(State.RefineryID));
        return true;
    }

    inline bool StartBarracksConstruction(GameInteractionDemoState& State,
                                          GameSession& Session,
                                          ClientRuntime& Client,
                                          UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        const int BarracksDefID = Game::Races::GetUnitDefID("Barracks");
        Core::Math::Vec2 Center{0.0f, 0.0f};
        if (auto* DepotTrans = EM.GetComponent<Game::Components::TransformComponent>(State.DepotID))
            Center = DepotTrans->Position;
        const int WorkerID = FindAvailableScvNear(EM, 0, Center, State.GasWorkerID);
        if (BarracksDefID < 0 || WorkerID < 0)
            return false;

        auto* WorkerTrans = EM.GetComponent<Game::Components::TransformComponent>(WorkerID);
        Core::Math::Vec2 BuildPos;
        if (!WorkerTrans ||
            !InteractionSmoke::FindBuildPlacementNear(Session, EM, BarracksDefID,
                WorkerTrans->Position, 0, BuildPos))
        {
            return false;
        }

        State.WorkerID = WorkerID;
        SelectAndFocus(State, Client, EM, State.WorkerID);
        Hud.UpdateContext(EM, {State.WorkerID});
        if (!ExecuteHudCommandIndex(EM, {State.WorkerID}, Hud, 6))
            return false;
        Hud.UpdateContext(EM, {State.WorkerID});

        int BuildButtonIndex = -1;
        const auto* BuildButton = InteractionSmoke::FindHudButtonByTarget(
            Hud, UI::ActionKind::Build, BarracksDefID, &BuildButtonIndex);
        if (!BuildButton || !BuildButton->Enabled)
            return false;
        if (!ExecuteHudCommandIndex(EM, {State.WorkerID}, Hud, BuildButtonIndex))
            return false;

        Game::Client::GameLoop::ClickTargetContext EmptyTarget;
        if (!ExecutePendingTargetCommand(
                EM,
                {State.WorkerID},
                Hud.GetPendingTargetMode(),
                Hud.GetPendingBuildDefID(),
                EmptyTarget,
                BuildPos))
        {
            return false;
        }
        Hud.CancelPendingTarget();
        State.BarracksID = InteractionSmoke::FindBuildingByDefAndBuilder(
            EM, BarracksDefID, State.WorkerID);
        if (State.BarracksID < 0)
            return false;
        SelectAndFocus(State, Client, EM, State.BarracksID);
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo started Barracks construction entity=" +
                std::to_string(State.BarracksID) +
                " worker=" + std::to_string(State.WorkerID));
        return true;
    }

    inline bool StartMarineProduction(GameInteractionDemoState& State,
                                      GameSession& Session,
                                      ClientRuntime& Client,
                                      UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        const int MarineDefID = Game::Races::GetUnitDefID("Marine");
        if (State.BarracksID < 0 || MarineDefID < 0)
            return false;
        State.MarineStartCount = InteractionSmoke::CountOwnedUnitsByChk(EM, 0, 0x00);
        SelectAndFocus(State, Client, EM, State.BarracksID);
        Hud.UpdateContext(EM, {State.BarracksID});
        int MarineButtonIndex = -1;
        const auto* MarineButton = InteractionSmoke::FindHudButtonByTarget(
            Hud, UI::ActionKind::Train, MarineDefID, &MarineButtonIndex);
        if (!MarineButton || !MarineButton->Enabled)
            return false;
        if (!ExecuteHudCommandIndex(EM, {State.BarracksID}, Hud, MarineButtonIndex))
            return false;
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo queued Marine production barracks=" +
                std::to_string(State.BarracksID));
        return true;
    }

    inline bool EnsureResearchBuilding(GameInteractionDemoState& State,
                                       GameSession& Session)
    {
        if (State.EngineeringBayID >= 0 &&
            Session.Entities().IsAlive(State.EngineeringBayID))
        {
            return true;
        }

        auto& EM = Session.Entities();
        const int EngineeringBayDefID = Game::Races::GetUnitDefID("EngineeringBay");
        if (EngineeringBayDefID < 0)
            return false;

        Core::Math::Vec2 Position{128.0f, 128.0f};
        if (auto* DepotTrans =
                EM.GetComponent<Game::Components::TransformComponent>(State.DepotID))
        {
            Position = DepotTrans->Position + Core::Math::Vec2{160.0f, 96.0f};
        }
        State.EngineeringBayID =
            Game::Systems::BuildingSystem::DebugSpawnCompletedBuilding(
                EM, EngineeringBayDefID, Position, 0);
        return State.EngineeringBayID >= 0;
    }

    inline bool StartResearch(GameInteractionDemoState& State,
                              GameSession& Session,
                              ClientRuntime& Client,
                              UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        if (!EnsureResearchBuilding(State, Session))
            return false;

        SelectAndFocus(State, Client, EM, State.EngineeringBayID);
        Hud.UpdateContext(EM, {State.EngineeringBayID});
        int ResearchButtonIndex = -1;
        const auto* ResearchButton = InteractionSmoke::FindHudButtonByResearch(
            Hud,
            Game::Systems::TerranResearchId::InfantryWeapons,
            &ResearchButtonIndex);
        if (!ResearchButton || !ResearchButton->Enabled)
            return false;
        if (ResearchButton->Kind != UI::ActionKind::Upgrade)
            return false;

        State.ResearchStartLevel = Game::Systems::UpgradeSystem::GetResearchLevel(
            0, Game::Systems::ResearchId::Terran_InfantryWeapons);
        if (!ExecuteHudCommandIndex(EM, {State.EngineeringBayID}, Hud, ResearchButtonIndex))
            return false;
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo started Infantry Weapons upgrade building=" +
                std::to_string(State.EngineeringBayID));
        return true;
    }

    inline float DistanceSq(const Core::Math::Vec2& A, const Core::Math::Vec2& B)
    {
        const Core::Math::Vec2 Delta = A - B;
        return Delta.X * Delta.X + Delta.Y * Delta.Y;
    }

    inline int FindOwnedCommandCenter(Core::ECS::EntityManager& EM, int PlayerID)
    {
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent,
                                               Game::Components::BuildingComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (!Unit || !InteractionSmoke::IsOwnedBy(EM, EntityID, PlayerID) ||
                !InteractionSmoke::IsAliveUnit(EM, EntityID))
            {
                continue;
            }

            const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
            if (DefID < 0)
                continue;
            const auto Def = Game::Races::GetUnitDefByID(DefID);
            if (Def.Name == "CommandCenter")
                return EntityID;
        }
        return -1;
    }

    inline int FindNearestMineral(Core::ECS::EntityManager& EM, Core::Math::Vec2 Center)
    {
        int BestID = -1;
        float BestDistSq = std::numeric_limits<float>::max();
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::ResourceComponent>())
        {
            auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityID);
            if (!Trans || !Resource || Resource->IsDepleted || Resource->Amount <= 0 ||
                Resource->Type != Game::Components::ResourceType::Minerals)
            {
                continue;
            }

            const float DistSq = DistanceSq(Trans->Position, Center);
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestID = EntityID;
            }
        }
        return BestID;
    }

    inline int FindNearestGeyser(Core::ECS::EntityManager& EM, Core::Math::Vec2 Center)
    {
        int BestID = -1;
        float BestDistSq = std::numeric_limits<float>::max();
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::ResourceComponent>())
        {
            auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityID);
            if (!Trans || !Resource || Resource->IsDepleted || Resource->Amount <= 0 ||
                Resource->Type != Game::Components::ResourceType::VespeneGas)
            {
                continue;
            }

            if (auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID))
            {
                if (Building->BuildingDefID == Game::Races::GetUnitDefID("Refinery") ||
                    Building->BuildingDefID == Game::Races::GetUnitDefID("Extractor") ||
                    Building->BuildingDefID == Game::Races::GetUnitDefID("Assimilator"))
                {
                    continue;
                }
            }

            const float DistSq = DistanceSq(Trans->Position, Center);
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestID = EntityID;
            }
        }
        return BestID;
    }

    inline int FindAvailableScvNear(Core::ECS::EntityManager& EM,
                                    int PlayerID,
                                    Core::Math::Vec2 Center,
                                    int ExcludedID)
    {
        int BestID = -1;
        float BestDistSq = std::numeric_limits<float>::max();
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            if (EntityID == ExcludedID)
                continue;
            auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (!Trans || !Unit || Unit->ChkUnitID != 0x07 ||
                !InteractionSmoke::IsOwnedBy(EM, EntityID, PlayerID) ||
                !InteractionSmoke::IsAliveUnit(EM, EntityID))
            {
                continue;
            }

            bool BusyConstructing = false;
            for (int BuildingID : EM.GetEntitiesWith<Game::Components::BuildingComponent,
                                                     Game::Components::UnitComponent>())
            {
                auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(BuildingID);
                auto* BuildingUnit = EM.GetComponent<Game::Components::UnitComponent>(BuildingID);
                if (Building && BuildingUnit && Building->UnderConstruction &&
                    Building->HasBuilderUnit(EntityID) && BuildingUnit->HitPoints > 0)
                {
                    BusyConstructing = true;
                    break;
                }
            }
            if (BusyConstructing)
                continue;

            const float DistSq = DistanceSq(Trans->Position, Center);
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestID = EntityID;
            }
        }
        return BestID;
    }

    inline std::vector<int> FindNearbyMinerals(Core::ECS::EntityManager& EM,
                                               Core::Math::Vec2 Center,
                                               int MaxCount)
    {
        std::vector<std::pair<float, int>> Ranked;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::ResourceComponent>())
        {
            auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityID);
            if (!Trans || !Resource || Resource->IsDepleted || Resource->Amount <= 0 ||
                Resource->Type != Game::Components::ResourceType::Minerals)
            {
                continue;
            }
            Ranked.emplace_back(DistanceSq(Trans->Position, Center), EntityID);
        }
        std::sort(Ranked.begin(), Ranked.end(),
            [](const auto& A, const auto& B) { return A.first < B.first; });

        std::vector<int> Result;
        for (const auto& Entry : Ranked)
        {
            Result.push_back(Entry.second);
            if ((int)Result.size() >= MaxCount)
                break;
        }
        return Result;
    }

    inline std::vector<int> FindHarvestWorkersNear(Core::ECS::EntityManager& EM,
                                                   int PlayerID,
                                                   int ResourceID,
                                                   Core::Math::Vec2 Center,
                                                   int MaxCount)
    {
        std::vector<std::pair<float, int>> Ranked;
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (!Trans || !Unit || !InteractionSmoke::IsOwnedBy(EM, EntityID, PlayerID) ||
                !InteractionSmoke::IsAliveUnit(EM, EntityID) ||
                !Game::Systems::HarvestSystem::CanHarvest(EM, EntityID, ResourceID))
            {
                continue;
            }
            Ranked.emplace_back(DistanceSq(Trans->Position, Center), EntityID);
        }
        std::sort(Ranked.begin(), Ranked.end(),
            [](const auto& A, const auto& B) { return A.first < B.first; });

        std::vector<int> Result;
        for (const auto& Entry : Ranked)
        {
            Result.push_back(Entry.second);
            if ((int)Result.size() >= MaxCount)
                break;
        }
        return Result;
    }

    inline int FindWorkerNear(Core::ECS::EntityManager& EM,
                              int PlayerID,
                              int ResourceID,
                              Core::Math::Vec2 Center)
    {
        int BestID = -1;
        float BestDistSq = std::numeric_limits<float>::max();
        for (int EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent,
                                               Game::Components::PlayerComponent>())
        {
            auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            if (!Trans || !Unit || !InteractionSmoke::IsOwnedBy(EM, EntityID, PlayerID) ||
                !InteractionSmoke::IsAliveUnit(EM, EntityID) ||
                !Game::Systems::HarvestSystem::CanHarvest(EM, EntityID, ResourceID))
            {
                continue;
            }

            const float DistSq = DistanceSq(Trans->Position, Center);
            if (DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                BestID = EntityID;
            }
        }
        return BestID;
    }

    inline bool Begin(GameInteractionDemoState& State,
                      GameSession& Session,
                      ClientRuntime& Client,
                      UI::Hud& Hud)
    {
        auto& EM = Session.Entities();
        State.DepotID = FindOwnedCommandCenter(EM, 0);
        Core::Math::Vec2 StartCenter{0.0f, 0.0f};
        if (State.DepotID >= 0)
        {
            if (auto* DepotTrans =
                    EM.GetComponent<Game::Components::TransformComponent>(State.DepotID))
            {
                StartCenter = DepotTrans->Position;
                State.ResourceID = FindNearestMineral(EM, StartCenter);
            }
        }
        if (State.ResourceID < 0)
            State.ResourceID = InteractionSmoke::FindResource(EM);
        if (State.ResourceID < 0)
            return false;
        if (auto* ResourceTrans =
                EM.GetComponent<Game::Components::TransformComponent>(State.ResourceID))
        {
            if (State.DepotID < 0)
                StartCenter = ResourceTrans->Position;
        }

        State.WorkerID = FindWorkerNear(EM, 0, State.ResourceID, StartCenter);
        if (State.WorkerID < 0)
            State.WorkerID = InteractionSmoke::SpawnSmokeWorker(Session, 0, State.ResourceID);
        if (State.WorkerID < 0)
            return false;

        if (State.DepotID < 0)
            State.DepotID = InteractionSmoke::EnsureSmokeDepot(Session, 0, State.ResourceID);
        if (State.DepotID < 0)
            return false;

        InteractionSmoke::EnsurePlayerResources(0);
        Game::Systems::HarvestSystem::GetResources(0).VespeneGas = 0;
        if (!StartHarvest(State, Session, Client, Hud))
            return false;

        const auto Minerals = FindNearbyMinerals(EM, StartCenter, 8);
        const auto Workers = FindHarvestWorkersNear(EM, 0, State.ResourceID, StartCenter, 6);
        int AssignedExtra = 0;
        for (size_t I = 0; I < Workers.size(); ++I)
        {
            const int WorkerID = Workers[I];
            if (WorkerID == State.WorkerID)
                continue;
            const int MineralID = Minerals.empty()
                ? State.ResourceID
                : Minerals[(I + 1) % Minerals.size()];
            if (Game::Systems::HarvestSystem::IssueHarvest(EM, {WorkerID}, MineralID))
            {
                ++AssignedExtra;
                Core::Utils::Logger::Info(
                    "InteractionDemo",
                    "demo assigned extra harvester worker=" +
                        std::to_string(WorkerID) +
                        " resource=" + std::to_string(MineralID));
            }
        }

        State.Started = true;
        SetStage(State, GameInteractionDemoState::Stage::HarvestLoop);
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo started at 2x simulation speed depot=" +
                std::to_string(State.DepotID) +
                " worker=" + std::to_string(State.WorkerID) +
                " mineral=" + std::to_string(State.ResourceID) +
                " extraHarvesters=" + std::to_string(AssignedExtra));
        return true;
    }

    inline bool StartFullRoster(GameInteractionDemoState& State,
                                GameSession& Session,
                                ClientRuntime& Client,
                                UI::Hud& Hud)
    {
        InteractionSmoke::FullRosterSpawnResult Result;
        std::string Error;
        if (!InteractionSmoke::SpawnFullRoster(Session, Result, Error))
        {
            Core::Utils::Logger::Error("InteractionDemo", Error);
            return false;
        }

        State.FullRosterEntityIDs = std::move(Result.EntityIDs);
        State.FullRosterBuildingCount = Result.BuildingCount;
        State.FullRosterUnitCount = Result.UnitCount;
        if (!State.FullRosterEntityIDs.empty())
        {
            SelectAndFocus(
                State, Client, Session.Entities(), State.FullRosterEntityIDs.front());
            Hud.UpdateContext(Session.Entities(), {State.FullRosterEntityIDs.front()});
        }
        Core::Utils::Logger::Info(
            "InteractionDemo",
            "demo full roster ready buildings=" +
                std::to_string(State.FullRosterBuildingCount) +
                " units=" + std::to_string(State.FullRosterUnitCount));
        return true;
    }

    inline float Run(GameInteractionDemoState& State,
                     GameSession& Session,
                     ClientRuntime& Client,
                     UI::Hud& Hud,
                     float Dt)
    {
        if (IsAiShowcase())
        {
            if (!Session.IsEcsInitialized() || !Session.IsMapLoaded())
                return 1.0f;
            return RunAiShowcase(State, Session, Client, Hud, Dt);
        }
        if (!IsEnabled(State))
            return 1.0f;
        if (!Session.IsEcsInitialized() || !Session.IsMapLoaded())
            return 1.0f;
        if (State.CurrentStage == GameInteractionDemoState::Stage::Done ||
            State.CurrentStage == GameInteractionDemoState::Stage::Failed)
        {
            return 2.0f;
        }

        constexpr float kSimulationScale = 2.0f;
        if (!State.Started)
        {
            if (!Begin(State, Session, Client, Hud))
                Fail(State, "could not initialize visible interaction demo");
            return kSimulationScale;
        }

        auto& EM = Session.Entities();
        State.StageSeconds += Dt * kSimulationScale;
        KeepFocus(State, Client, EM);

        using StageT = GameInteractionDemoState::Stage;
        switch (State.CurrentStage)
        {
            case StageT::HarvestLoop:
            {
                auto* Harvest =
                    EM.GetComponent<Game::Components::HarvestComponent>(State.WorkerID);
                if (State.StageSeconds >= 12.0f ||
                    (Harvest && Harvest->CarryingAmount > 0))
                {
                    if (!StartProduction(State, Session, Client, Hud))
                        Fail(State, "could not queue SCV production");
                    else
                        SetStage(State, StageT::ProductionQueue);
                }
                break;
            }
            case StageT::ProductionQueue:
            {
                SelectAndFocus(State, Client, EM, State.DepotID);
                Hud.UpdateContext(EM, {State.DepotID});
                const int CurrentScvs =
                    InteractionSmoke::CountOwnedUnitsByChk(EM, 0, 0x07);
                if (CurrentScvs > State.TrainedUnitStartCount ||
                    State.StageSeconds >= 28.0f)
                {
                    const int NewScvID =
                        InteractionSmoke::FindNewestOwnedUnitByChk(EM, 0, 0x07);
                    if (NewScvID >= 0)
                        State.WorkerID = NewScvID;
                    if (!StartConstruction(State, Session, Client, Hud))
                        Fail(State, "could not start Supply Depot construction");
                    else
                        SetStage(State, StageT::Construction);
                }
                break;
            }
            case StageT::Construction:
            {
                SelectAndFocus(State, Client, EM, State.BuildSiteID);
                Hud.UpdateContext(EM, {State.BuildSiteID});
                auto* Building =
                    EM.GetComponent<Game::Components::BuildingComponent>(State.BuildSiteID);
                if ((Building && !Building->UnderConstruction) ||
                    State.StageSeconds >= 38.0f)
                {
                    if (!StartRefineryConstruction(State, Session, Client, Hud))
                        Fail(State, "could not start Refinery construction");
                    else
                        SetStage(State, StageT::GasConstruction);
                }
                break;
            }
            case StageT::GasConstruction:
            {
                SelectAndFocus(State, Client, EM, State.RefineryID);
                Hud.UpdateContext(EM, {State.RefineryID});
                auto* Building =
                    EM.GetComponent<Game::Components::BuildingComponent>(State.RefineryID);
                if ((Building && !Building->UnderConstruction) ||
                    State.StageSeconds >= 48.0f)
                {
                    if (!StartGasHarvest(State, Session, Client, Hud))
                        Fail(State, "could not start Refinery gas harvest");
                    else
                        SetStage(State, StageT::GasHarvest);
                }
                break;
            }
            case StageT::GasHarvest:
            {
                const auto& Resources = Game::Systems::HarvestSystem::GetResources(0);
                if (Resources.VespeneGas >= 100 || State.StageSeconds >= 90.0f)
                {
                    if (!StartBarracksConstruction(State, Session, Client, Hud))
                        Fail(State, "could not start Barracks construction");
                    else
                        SetStage(State, StageT::BarracksConstruction);
                }
                break;
            }
            case StageT::BarracksConstruction:
            {
                SelectAndFocus(State, Client, EM, State.BarracksID);
                Hud.UpdateContext(EM, {State.BarracksID});
                auto* Building =
                    EM.GetComponent<Game::Components::BuildingComponent>(State.BarracksID);
                if ((Building && !Building->UnderConstruction) ||
                    State.StageSeconds >= 58.0f)
                {
                    if (!StartMarineProduction(State, Session, Client, Hud))
                        Fail(State, "could not queue Marine production");
                    else
                        SetStage(State, StageT::MarineProduction);
                }
                break;
            }
            case StageT::MarineProduction:
            {
                SelectAndFocus(State, Client, EM, State.BarracksID);
                Hud.UpdateContext(EM, {State.BarracksID});
                const int MarineCount =
                    InteractionSmoke::CountOwnedUnitsByChk(EM, 0, 0x00);
                if (MarineCount > State.MarineStartCount ||
                    State.StageSeconds >= 32.0f)
                {
                    if (!StartResearch(State, Session, Client, Hud))
                        Fail(State, "could not start Infantry Weapons upgrade");
                    else
                        SetStage(State, StageT::Research);
                }
                break;
            }
            case StageT::Research:
            {
                SelectAndFocus(State, Client, EM, State.EngineeringBayID);
                Hud.UpdateContext(EM, {State.EngineeringBayID});
                const int Level = Game::Systems::UpgradeSystem::GetResearchLevel(
                    0, Game::Systems::ResearchId::Terran_InfantryWeapons);
                if (Level > State.ResearchStartLevel)
                {
                    Core::Utils::Logger::Info(
                        "InteractionDemo",
                        "demo completed Infantry Weapons level=" +
                            std::to_string(Level));
                    if (!StartFullRoster(State, Session, Client, Hud))
                        Fail(State, "could not spawn the full building/unit roster");
                    else
                        SetStage(State, StageT::FullRoster);
                }
                else if (State.StageSeconds >= 42.0f)
                {
                    Fail(State, "Infantry Weapons upgrade did not complete during demo");
                }
                break;
            }
            case StageT::FullRoster:
            {
                if (!State.FullRosterEntityIDs.empty())
                {
                    const size_t Index = std::min(
                        State.FullRosterEntityIDs.size() - 1,
                        (size_t)(State.StageSeconds / 2.0f));
                    SelectAndFocus(
                        State, Client, EM, State.FullRosterEntityIDs[Index]);
                    Hud.UpdateContext(EM, {State.FullRosterEntityIDs[Index]});
                }
                if (State.StageSeconds >= 16.0f)
                    SetStage(State, StageT::Observe);
                break;
            }
            case StageT::Observe:
            {
                if (State.StageSeconds >= 12.0f)
                {
                    Core::Utils::Logger::Info(
                        "InteractionDemo",
                        "demo completed visible economy loop");
                    SetStage(State, StageT::Done);
                }
                break;
            }
            default:
                break;
        }

        return kSimulationScale;
    }
}

inline float RunInteractionDemoIfRequested(GameInteractionDemoState& State,
                                           GameSession& Session,
                                           ClientRuntime& Client,
                                           UI::Hud& Hud,
                                           float Dt)
{
    return InteractionDemo::Run(State, Session, Client, Hud, Dt);
}

} // namespace Game::Client::GameLoop
