#pragma once

#include "Client/ClientRuntime.h"
#include "Client/Selection.h"
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
#include <string>
#include <vector>

namespace Game::Client::GameLoop
{

struct GameInteractionSmokeState
{
    bool Attempted = false;
};

namespace InteractionSmoke
{
    inline bool IsEnabled()
    {
        const char* Value = std::getenv("STARCRAFT_INTERACTION_SMOKE");
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
                Building->BuilderUnitID == BuilderID)
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
        TickSystems(Session, 16, 0.1f);
        if (Resource->Amount >= StartAmount && Harvest->CarryingAmount <= 0)
        {
            Fail(Error, "harvest system did not mine from the resource");
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

    inline bool VerifyCampaignVictoryClosure(GameSession& Session,
                                             CampaignRuntime& Campaign,
                                             Core::ECS::EntityManager& EM,
                                             ClientRuntime& Client,
                                             GuiRuntime& Gui,
                                             UI::Hud& Hud,
                                             int WorkerID,
                                             std::string& Error)
    {
        const int RaynorID = FindNewestUnitByChk(EM, 19);
        if (RaynorID < 0)
        {
            Fail(Error, "loaded mission has no Raynor unit for victory closure");
            return false;
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

        auto& Controller = Campaign.Controller();
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
        if (Controller.GetState() != Game::Campaign::CampaignState::Playing)
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
                " mission=\"" + Controller.GetMissionName() + "\"");
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

        TickSystems(Session, 79, 0.1f);
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

        Core::Utils::Logger::Info(
            "InteractionSmoke",
            "Interaction smoke passed: worker=" + std::to_string(WorkerID) +
                " resource=" + std::to_string(ResourceID) +
                " depot=" + std::to_string(DepotID) +
                " trained=" + std::to_string(TrainedUnitID) +
                " building=" + std::to_string(BuiltBuildingID) +
                " attacker=" + std::to_string(AttackerID) +
                " target=" + std::to_string(TargetID));
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

} // namespace Game::Client::GameLoop
