#include "Client/MapLoader.h"
#include "Core/Audio/AudioSystem.h"
#include "Core/ECS/EntityManager.h"
#include "Core/Math/Vec2.h"
#include "Game/Campaign/CampaignController.h"
#include "Game/Campaign/CampaignMissionCatalog.h"
#include "Game/Components.h"
#include "Game/DataEnums.h"
#include "Game/Map/Map.h"
#include "Game/Races/RaceDef.h"
#include "Game/Systems/CommandSystem.h"
#include "Game/Systems/CombatSystem.h"
#include "Game/Systems/BulletSystem.h"
#include "Game/Systems/DeathEvents.h"
#include "Game/Systems/AnimationSystem.h"
#include "Game/Systems/MovementSystem.h"
#include "Game/Trigger/SCRTriggerParser.h"
#include "Game/Trigger/TriggerVM.h"
#include "Render/Camera2D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Core::Audio
{
    int AudioSystem::PlaySound(int, float, float, int, int, SoundFlags)
    {
        return -1;
    }
}

namespace Game::Systems
{

bool BulletSystem::ResolveIScriptDamageFrame(int)
{
    return false;
}

bool CombatSystem::ResolvePendingAttackFrame(Core::ECS::EntityManager&,
                                             BulletSystem&,
                                             int)
{
    return false;
}

void CombatSystem::IssueAttack(Core::ECS::EntityManager&,
                               const std::vector<int>&,
                               int)
{
}

} // namespace Game::Systems

namespace
{
    void Require(bool Value, const std::string& Message)
    {
        if (!Value)
        {
            std::fprintf(stderr, "FAIL: %s\n", Message.c_str());
            std::exit(1);
        }
    }

    bool IsBuildingChkUnit(int UnitID)
    {
        if ((UnitID >= 106 && UnitID <= 127) || (UnitID >= 130 && UnitID <= 152))
            return true;
        if (UnitID >= 154 && UnitID <= 175)
            return true;
        if (UnitID >= 189 && UnitID <= 213)
            return true;
        return false;
    }

    bool IsResourceChkUnit(int UnitID)
    {
        return (UnitID >= 176 && UnitID <= 178) || UnitID == 188;
    }

    int SumCounts(const std::array<int, 256>& Counts)
    {
        int Total = 0;
        for (int Count : Counts)
            Total += Count;
        return Total;
    }

    bool IsFinite(float Value)
    {
        return std::isfinite(Value);
    }

    bool IsFiniteVec(Core::Math::Vec2 Value)
    {
        return IsFinite(Value.X) && IsFinite(Value.Y);
    }

    const char* BoolText(bool Value)
    {
        return Value ? "true" : "false";
    }

    std::string CsvEscape(const std::string& Value)
    {
        std::string Out;
        Out.reserve(Value.size() + 2);
        Out.push_back('"');
        for (char C : Value)
        {
            if (C == '"')
                Out.push_back('"');
            Out.push_back(C);
        }
        Out.push_back('"');
        return Out;
    }

    std::string CommandName(Game::Components::CommandType Command)
    {
        switch (Command)
        {
            case Game::Components::CommandType::Move: return "Move";
            case Game::Components::CommandType::AttackMove: return "AttackMove";
            case Game::Components::CommandType::Attack: return "Attack";
            case Game::Components::CommandType::Patrol: return "Patrol";
            case Game::Components::CommandType::HoldPosition: return "HoldPosition";
            case Game::Components::CommandType::Stop: return "Stop";
            case Game::Components::CommandType::Harvest: return "Harvest";
            case Game::Components::CommandType::Build: return "Build";
            case Game::Components::CommandType::Train: return "Train";
            case Game::Components::CommandType::Upgrade: return "Upgrade";
            case Game::Components::CommandType::UseAbility: return "UseAbility";
            case Game::Components::CommandType::Follow: return "Follow";
        }
        return "Unknown";
    }

    std::string HarvestName(Game::Components::HarvestState State)
    {
        switch (State)
        {
            case Game::Components::HarvestState::Idle: return "Idle";
            case Game::Components::HarvestState::MovingToResource: return "MovingToResource";
            case Game::Components::HarvestState::Mining: return "Mining";
            case Game::Components::HarvestState::MovingToDepot: return "MovingToDepot";
            case Game::Components::HarvestState::Depositing: return "Depositing";
        }
        return "Unknown";
    }

    const char* EntityKind(const Game::Components::BuildingComponent* Building,
                           const Game::Components::ResourceComponent* Resource)
    {
        if (Building && Building->IsBuilding)
            return "building";
        if (Resource && Resource->Type != Game::Components::ResourceType::None)
            return "resource";
        return "unit";
    }

    void PrintNonZeroCounts(const char* Label, const std::array<int, 256>& Counts)
    {
        const int Total = SumCounts(Counts);
        std::printf("%s_total=%d", Label, Total);
        for (size_t I = 0; I < Counts.size(); ++I)
            if (Counts[I] > 0)
                std::printf(" %zu:%d", I, Counts[I]);
        std::printf("\n");
    }

    void SeedCampaignUnits(Core::ECS::EntityManager& EM,
                           const Game::Client::MapLoader::Result& Result)
    {
        for (const auto& Placed : Result.ChkUnits)
        {
            const int EntityID = EM.CreateEntity();
            auto& Trans = EM.AddComponent<Game::Components::TransformComponent>(EntityID);
            Trans.Position = { (float)Placed.X, (float)Placed.Y };

            auto& Unit = EM.AddComponent<Game::Components::UnitComponent>(EntityID);
            Unit.ChkUnitID = (int)Placed.UnitID;
            Unit.MaxHitPoints = 100;
            Unit.HitPoints = std::max(1, (int)Placed.HpPercent);

            auto& Player = EM.AddComponent<Game::Components::PlayerComponent>(EntityID);
            Player.PlayerID = (int)Placed.Owner;

            auto& Render = EM.AddComponent<Game::Components::RenderComponent>(EntityID);
            Render.ImageID = Placed.UnitID;
            Render.TotalFrames = 1;

            const int DefID = Game::Races::GetUnitDefIDByChkUnitID((int)Placed.UnitID);
            if (DefID >= 0)
            {
                const auto Def = Game::Races::GetUnitDefByID(DefID);
                Unit.UnitRace = Def.RaceType;
                Unit.SizeType = Def.SizeType;
                Unit.MaxHitPoints = Def.HitPoints;
                Unit.HitPoints = std::max(1, Def.HitPoints * std::max(1, (int)Placed.HpPercent) / 100);
                Unit.MaxShieldPoints = Def.ShieldPoints;
                Unit.ShieldPoints = Def.ShieldPoints * std::max(0, (int)Placed.ShieldPercent) / 100;
                Unit.MaxEnergyPoints = Def.EnergyPoints;
                Unit.EnergyPoints = Def.EnergyPoints * std::max(0, (int)Placed.EnergyPercent) / 100;
                Unit.Armor = Def.Armor;
                Unit.Speed = Def.Speed;
                Unit.SightRange = Def.SightRange;
                if (Def.IsBuilding)
                {
                    auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
                    Building.IsBuilding = true;
                    Building.UnderConstruction = false;
                    Building.BuildingDefID = DefID;
                }
                else
                {
                    EM.AddComponent<Game::Components::CommandComponent>(EntityID);
                    auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
                    Path.IsAir = Game::Data::IsAirUnitType((int)Placed.UnitID);
                }
                if (Def.GroundDamage > 0 || Def.AirDamage > 0)
                {
                    auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
                    Game::Races::ApplyCombatDefinition(
                        Combat, Def, Game::Data::IsAirUnitType((int)Placed.UnitID));
                }
            }
            else if (IsBuildingChkUnit((int)Placed.UnitID))
            {
                auto& Building = EM.AddComponent<Game::Components::BuildingComponent>(EntityID);
                Building.IsBuilding = true;
                Building.UnderConstruction = false;
            }
            else if (!IsResourceChkUnit((int)Placed.UnitID))
            {
                EM.AddComponent<Game::Components::CommandComponent>(EntityID);
                auto& Path = EM.AddComponent<Game::Components::PathfindingComponent>(EntityID);
                Path.IsAir = Game::Data::IsAirUnitType((int)Placed.UnitID);
                auto& Combat = EM.AddComponent<Game::Components::CombatComponent>(EntityID);
                Combat.GroundDamage = 5;
                Combat.AttackRange = 4.0f;
                Combat.AttackCooldown = 1.0f;
                Combat.CanTargetGround = true;
                Combat.MyElevation = Game::Data::IsAirUnitType((int)Placed.UnitID) ?
                    Game::Components::Elevation::Air :
                    Game::Components::Elevation::Ground;
            }

            if (Placed.UnitID >= 176 && Placed.UnitID <= 178)
            {
                auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
                Res.Type = Game::Components::ResourceType::Minerals;
                Res.Amount = Placed.ResourceAmount ? (int)Placed.ResourceAmount : 1500;
                Res.MaxAmount = Res.Amount;
            }
            else if (Placed.UnitID == 188)
            {
                auto& Res = EM.AddComponent<Game::Components::ResourceComponent>(EntityID);
                Res.Type = Game::Components::ResourceType::VespeneGas;
                Res.Amount = Placed.ResourceAmount ? (int)Placed.ResourceAmount : 5000;
                Res.MaxAmount = Res.Amount;
            }
        }
    }

    Game::Map::GameMap BuildPathingMap(
        const Game::Client::MapLoader::Result& Result)
    {
        Game::Map::GameMap Map;
        Map.Generate(Result.Map.Width, Result.Map.Height);
        for (int Y = 0; Y < Result.Map.Height; ++Y)
        {
            for (int X = 0; X < Result.Map.Width; ++X)
            {
                const size_t Index = (size_t)Y * (size_t)Result.Map.Width +
                    (size_t)X;
                auto Tile = Map.GetTile(X, Y);
                Tile.Walkable = Index >= Result.Map.Walkable.size() ||
                    Result.Map.Walkable[Index] != 0;
                Tile.Buildable = Tile.Walkable;
                if (Index < Result.Map.MegaTileIds.size())
                    Tile.MegaTileID = Result.Map.MegaTileIds[Index];
                Map.SetTile(X, Y, Tile);
            }
        }
        return Map;
    }

    struct AuditTotals
    {
        int MissionSlots = 0;
        int TriggerTotal = 0;
        int UnitTotal = 0;
        int EntityRows = 0;
        int BuildingRows = 0;
        int ResourceRows = 0;
        int MovableRows = 0;
        int MovingIntentRows = 0;
        int OnScreenRows = 0;
        int Anomalies = 0;
    };

    void WriteAuditHeader(std::ofstream& Log)
    {
        Log << "phase,sample_time,set,race,mission,entity_id,kind,chk_unit_id,player,"
               "active,world_x,world_y,screen_x,screen_y,on_screen,"
               "hp,max_hp,shield,max_shield,energy,max_energy,"
               "render_visible,image_id,frame,total_frames,layer,flipped,"
               "building_def,under_construction,build_progress,is_lifted,"
               "command,target_entity,target_x,target_y,"
               "path_nodes,path_index,current_speed,target_speed,velocity_x,velocity_y,facing_angle,air,moving_intent,"
               "harvest_state,resource_amount,resource_max,subnodes,anomaly\n";
    }

    bool HasMovingIntent(const Game::Components::CommandComponent* Command,
                         const Game::Components::PathfindingComponent* Path,
                         const Game::Components::TransformComponent* Transform)
    {
        if (Path && (Path->PathIndex < (int)Path->Path.size() ||
                     Path->CurrentVelocity.LengthSq() > 0.01f))
        {
            return true;
        }
        if (!Command)
            return false;
        switch (Command->CurrentCommand)
        {
            case Game::Components::CommandType::Move:
            case Game::Components::CommandType::AttackMove:
            case Game::Components::CommandType::Patrol:
            {
                const float ArrivalTolerance = std::max(
                    Path ? Path->NodeTolerance : 0.0f, 8.0f);
                return Transform &&
                    Transform->Position.DistanceTo(Command->TargetPosition) >
                        ArrivalTolerance;
            }
            case Game::Components::CommandType::Attack:
            case Game::Components::CommandType::Harvest:
            case Game::Components::CommandType::Build:
            case Game::Components::CommandType::Follow:
                return true;
            default:
                return false;
        }
    }

    float ReadEnvFloat(const char* Name,
                       float DefaultValue,
                       float MinValue,
                       float MaxValue)
    {
        const char* Value = std::getenv(Name);
        if (!Value || Value[0] == '\0')
            return DefaultValue;
        char* End = nullptr;
        const float Parsed = std::strtof(Value, &End);
        if (End == Value || !IsFinite(Parsed))
            return DefaultValue;
        return std::clamp(Parsed, MinValue, MaxValue);
    }

    int ReadEnvInt(const char* Name,
                   int DefaultValue,
                   int MinValue,
                   int MaxValue)
    {
        const char* Value = std::getenv(Name);
        if (!Value || Value[0] == '\0')
            return DefaultValue;
        char* End = nullptr;
        const long Parsed = std::strtol(Value, &End, 10);
        if (End == Value)
            return DefaultValue;
        return std::clamp((int)Parsed, MinValue, MaxValue);
    }

    bool ExpectedAirState(const Game::Components::UnitComponent* Unit,
                          const Game::Components::BuildingComponent* Building)
    {
        if (Building && Building->IsLifted)
            return true;
        return Unit && Game::Data::IsAirUnitType(Unit->ChkUnitID);
    }

    struct LongEntityTrace
    {
        bool Initialized = false;
        Core::Math::Vec2 StartPosition;
        Core::Math::Vec2 LastPosition;
        Core::Math::Vec2 LastMoveDelta;
        float TotalDistance = 0.0f;
        float MaxStepDistance = 0.0f;
        int Samples = 0;
        int MovingSteps = 0;
        int DirectionFlips = 0;
        int NonFiniteSamples = 0;
        int OutOfMapSamples = 0;
        int LargeStepSamples = 0;
        int AirMismatchSamples = 0;
        int ElevationMismatchSamples = 0;
        int GroundMarkedAirSamples = 0;
        int CommandedStillSamples = 0;
        int CurrentCommandedStillRun = 0;
        int MaxCommandedStillRun = 0;
    };

    void TrackLongRunEntities(const Game::Client::MapLoader::Result& Result,
                              Core::ECS::EntityManager& EM,
                              float DeltaTime,
                              std::unordered_map<int, LongEntityTrace>& Traces)
    {
        const float WorldMaxX = (float)(Result.Map.Width * 32);
        const float WorldMaxY = (float)(Result.Map.Height * 32);
        for (auto EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent>())
        {
            const auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            const auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
            const auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityID);
            const auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
            const auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(EntityID);
            if (!Trans || !Unit)
                continue;
            if (Unit->HitPoints <= 0)
                continue;
            if (Player && (Player->PlayerID < 0 || Player->PlayerID >= 12))
                continue;

            auto& Trace = Traces[EntityID];
            if (!Trace.Initialized)
            {
                Trace.Initialized = true;
                Trace.StartPosition = Trans->Position;
                Trace.LastPosition = Trans->Position;
            }

            ++Trace.Samples;
            const Core::Math::Vec2 Delta = Trans->Position - Trace.LastPosition;
            const float StepDistance = Delta.Length();
            if (StepDistance > 0.001f)
            {
                Trace.TotalDistance += StepDistance;
                Trace.MaxStepDistance = std::max(Trace.MaxStepDistance, StepDistance);
            }
            if (StepDistance > 0.05f)
            {
                ++Trace.MovingSteps;
                if (Trace.LastMoveDelta.LengthSq() > 0.25f &&
                    Delta.LengthSq() > 0.25f &&
                    Trace.LastMoveDelta.Normalized().Dot(Delta.Normalized()) < -0.75f)
                {
                    ++Trace.DirectionFlips;
                }
                Trace.LastMoveDelta = Delta;
            }

            if (!IsFiniteVec(Trans->Position))
                ++Trace.NonFiniteSamples;
            if (Trans->Position.X < -256.0f || Trans->Position.Y < -256.0f ||
                Trans->Position.X > WorldMaxX + 256.0f ||
                Trans->Position.Y > WorldMaxY + 256.0f)
            {
                ++Trace.OutOfMapSamples;
            }

            const bool ExpectedAir = ExpectedAirState(Unit, Building);
            if (Path)
            {
                const bool HasMotionIntent = HasMovingIntent(Command, Path, Trans);
                if (Path->IsAir != ExpectedAir)
                    ++Trace.AirMismatchSamples;
                if (!ExpectedAir && Path->IsAir)
                    ++Trace.GroundMarkedAirSamples;

                const float ExpectedSpeed =
                    std::max({Unit->Speed, Path->CurrentSpeed, Path->TargetSpeed, 1.0f});
                const float MaxReasonableStep =
                    std::max(256.0f, ExpectedSpeed * std::max(DeltaTime, 0.001f) * 4.0f + 64.0f);
                if (HasMotionIntent && StepDistance > MaxReasonableStep)
                    ++Trace.LargeStepSamples;

                if (HasMotionIntent &&
                    (Path->CurrentSpeed > 10.0f || Path->TargetSpeed > 10.0f) &&
                    StepDistance < 0.05f)
                {
                    ++Trace.CommandedStillSamples;
                    ++Trace.CurrentCommandedStillRun;
                    Trace.MaxCommandedStillRun =
                        std::max(Trace.MaxCommandedStillRun,
                                 Trace.CurrentCommandedStillRun);
                }
                else
                {
                    Trace.CurrentCommandedStillRun = 0;
                }
            }
            if (Combat)
            {
                const bool CombatAir = Combat->MyElevation == Game::Components::Elevation::Air;
                if (CombatAir != ExpectedAir)
                    ++Trace.ElevationMismatchSamples;
            }

            Trace.LastPosition = Trans->Position;
        }
    }

    void AnalyzeLongRunTraces(const Game::Campaign::CampaignMissionRef& Ref,
                              Core::ECS::EntityManager& EM,
                              const std::unordered_map<int, LongEntityTrace>& Traces)
    {
        int ProblemCount = 0;
        for (const auto& Entry : Traces)
        {
            const int EntityID = Entry.first;
            const auto& Trace = Entry.second;
            const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            const auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            if (!Trace.Initialized || !Unit || !Trans)
                continue;

            std::vector<std::string> Issues;
            const float NetDistance = (Trans->Position - Trace.StartPosition).Length();
            if (Trace.NonFiniteSamples > 0)
                Issues.push_back("long_nonfinite_world");
            if (Trace.OutOfMapSamples > 0)
                Issues.push_back("long_world_outside_map_margin");
            if (Trace.LargeStepSamples > 0)
                Issues.push_back("long_large_step");
            if (Trace.GroundMarkedAirSamples > 0)
                Issues.push_back("long_ground_marked_air");
            if (Trace.AirMismatchSamples > 2)
                Issues.push_back("long_air_flag_mismatch");
            if (Trace.ElevationMismatchSamples > 2)
                Issues.push_back("long_elevation_mismatch");
            if (Trace.MaxCommandedStillRun > 120)
                Issues.push_back("long_commanded_still");
            if (Trace.TotalDistance > 96.0f &&
                NetDistance < 24.0f &&
                Trace.DirectionFlips >= 18)
            {
                Issues.push_back("long_jitter_suspect");
            }

            if (Issues.empty())
                continue;

            ++ProblemCount;
            if (ProblemCount <= 25)
            {
                std::fprintf(stderr,
                    "Long-run entity anomaly: set=%s race=%d mission=%d entity=%d unit=%d samples=%d total=%.2f net=%.2f max_step=%.2f flips=%d still_total=%d still_run=%d issues=",
                    Ref.Expansion ? "expansion" : "original",
                    Ref.Race,
                    Ref.Mission,
                    EntityID,
                    Unit->ChkUnitID,
                    Trace.Samples,
                    Trace.TotalDistance,
                    NetDistance,
                    Trace.MaxStepDistance,
                    Trace.DirectionFlips,
                    Trace.CommandedStillSamples,
                    Trace.MaxCommandedStillRun);
                for (size_t I = 0; I < Issues.size(); ++I)
                    std::fprintf(stderr, "%s%s", I ? "|" : "", Issues[I].c_str());
                std::fprintf(stderr, "\n");
            }
        }

        Require(ProblemCount == 0,
            "long-run entity motion audit found anomalies: " + Ref.Path);
    }

    void AuditMissionEntities(const Game::Campaign::CampaignMissionRef& Ref,
                              const Game::Client::MapLoader::Result& Result,
                              Core::ECS::EntityManager& EM,
                              const char* Phase,
                              float SampleTime,
                              std::ofstream& Log,
                              AuditTotals& Totals)
    {
        Render::Camera2D Camera;
        Camera.SetViewport(1280, 720);
        Camera.SetZoom(1.20f);
        float CameraX = (float)(Result.Map.Width * 16);
        float CameraY = (float)(Result.Map.Height * 16);
        for (const auto& Start : Result.ChkStartLocations)
        {
            if (Start.PlayerId == 0)
            {
                CameraX = (float)(Start.TileX * 32 + 16);
                CameraY = (float)(Start.TileY * 32 + 16);
                break;
            }
        }
        Camera.SetMapBounds(Result.Map.Width, Result.Map.Height);
        Camera.SetPosition(CameraX, CameraY);

        int MissionRows = 0;
        int MissionOnScreen = 0;
        int MissionAnomalies = 0;
        const float WorldMaxX = (float)(Result.Map.Width * 32);
        const float WorldMaxY = (float)(Result.Map.Height * 32);

        for (auto EntityID : EM.GetEntitiesWith<Game::Components::TransformComponent,
                                               Game::Components::UnitComponent>())
        {
            const auto* Trans = EM.GetComponent<Game::Components::TransformComponent>(EntityID);
            const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(EntityID);
            const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(EntityID);
            const auto* Render = EM.GetComponent<Game::Components::RenderComponent>(EntityID);
            const auto* Building = EM.GetComponent<Game::Components::BuildingComponent>(EntityID);
            const auto* Resource = EM.GetComponent<Game::Components::ResourceComponent>(EntityID);
            const auto* Command = EM.GetComponent<Game::Components::CommandComponent>(EntityID);
            const auto* Path = EM.GetComponent<Game::Components::PathfindingComponent>(EntityID);
            const auto* Harvest = EM.GetComponent<Game::Components::HarvestComponent>(EntityID);
            const auto* ImageTree = EM.GetComponent<Game::Components::ImageTreeComponent>(EntityID);
            const bool HasValidPlayer = Player && Player->PlayerID >= 0 && Player->PlayerID < 12;
            const bool Active = Unit->HitPoints > 0 && HasValidPlayer;
            const bool MovingIntent = HasMovingIntent(Command, Path, Trans);

            float ScreenX = 0.0f;
            float ScreenY = 0.0f;
            Camera.WorldToScreen(Trans->Position.X, Trans->Position.Y, ScreenX, ScreenY);
            const bool OnScreen =
                ScreenX >= -96.0f && ScreenX <= (float)Camera.GetViewportWidth() + 96.0f &&
                ScreenY >= -96.0f && ScreenY <= (float)Camera.GetViewportHeight() + 96.0f;

            std::vector<std::string> Issues;
            if (!IsFiniteVec(Trans->Position))
                Issues.push_back("nonfinite_world");
            if (!IsFinite(ScreenX) || !IsFinite(ScreenY))
                Issues.push_back("nonfinite_screen");
            if (Trans->Position.X < -256.0f || Trans->Position.Y < -256.0f ||
                Trans->Position.X > WorldMaxX + 256.0f ||
                Trans->Position.Y > WorldMaxY + 256.0f)
            {
                Issues.push_back("world_outside_map_margin");
            }
            if (!Render)
                Issues.push_back("missing_render");
            else
            {
                if (Render->TotalFrames <= 0)
                    Issues.push_back("invalid_total_frames");
                if (Render->FrameIndex < 0 || Render->FrameIndex >= std::max(1, Render->TotalFrames))
                    Issues.push_back("frame_out_of_range");
            }
            if (Unit->MaxHitPoints <= 0)
                Issues.push_back("invalid_max_hp");
            if (Unit->HitPoints < 0 || Unit->HitPoints > Unit->MaxHitPoints)
                Issues.push_back("hp_out_of_range");
            if (Unit->ShieldPoints < 0 || Unit->ShieldPoints > Unit->MaxShieldPoints)
                Issues.push_back("shield_out_of_range");
            if (Unit->EnergyPoints < 0 || Unit->EnergyPoints > Unit->MaxEnergyPoints)
                Issues.push_back("energy_out_of_range");
            if (Unit->HitPoints > 0 && !HasValidPlayer)
                Issues.push_back("active_unit_missing_valid_player");

            const int DefID = Game::Races::GetUnitDefIDByChkUnitID(Unit->ChkUnitID);
            const bool ExpectedAir = ExpectedAirState(Unit, Building);
            if (DefID >= 0)
            {
                const auto Def = Game::Races::GetUnitDefByID(DefID);
                if (Active && Def.IsBuilding && !Building)
                    Issues.push_back("def_building_missing_building_component");
                if (Active && !Def.IsBuilding && !Resource && !Path)
                    Issues.push_back("movable_def_missing_path_component");
            }
            if (Building && Building->BuildProgress < 0.0f)
                Issues.push_back("negative_build_progress");
            if (Path)
            {
                if (!IsFiniteVec(Path->CurrentVelocity))
                    Issues.push_back("nonfinite_velocity");
                if (!IsFinite(Path->CurrentSpeed) || Path->CurrentSpeed < 0.0f)
                    Issues.push_back("invalid_current_speed");
                if (!IsFinite(Path->TargetSpeed) || Path->TargetSpeed < 0.0f)
                    Issues.push_back("invalid_target_speed");
                if (!IsFinite(Path->FacingAngle))
                    Issues.push_back("nonfinite_facing_angle");
                if (Path->PathIndex < 0 || Path->PathIndex > (int)Path->Path.size())
                    Issues.push_back("path_index_out_of_range");
                if (Path->IsAir != ExpectedAir)
                    Issues.push_back("air_flag_mismatch");
                if (!ExpectedAir && Path->IsAir)
                    Issues.push_back("ground_unit_marked_air");
            }
            if (const auto* Combat = EM.GetComponent<Game::Components::CombatComponent>(EntityID))
            {
                const bool CombatAir = Combat->MyElevation == Game::Components::Elevation::Air;
                if (CombatAir != ExpectedAir)
                    Issues.push_back("combat_elevation_mismatch");
            }

            std::string IssueText;
            for (size_t I = 0; I < Issues.size(); ++I)
            {
                if (I > 0)
                    IssueText += "|";
                IssueText += Issues[I];
            }

            ++MissionRows;
            ++Totals.EntityRows;
            if (Building && Building->IsBuilding)
                ++Totals.BuildingRows;
            if (Resource && Resource->Type != Game::Components::ResourceType::None)
                ++Totals.ResourceRows;
            if (Path)
                ++Totals.MovableRows;
            if (MovingIntent)
                ++Totals.MovingIntentRows;
            if (OnScreen)
            {
                ++MissionOnScreen;
                ++Totals.OnScreenRows;
            }
            if (!Issues.empty())
            {
                ++MissionAnomalies;
                ++Totals.Anomalies;
            }

            const auto Target = Command ? Command->TargetPosition : Core::Math::Vec2{};
            const auto Velocity = Path ? Path->CurrentVelocity : Core::Math::Vec2{};
            Log << CsvEscape(Phase ? Phase : "") << ','
                << SampleTime << ','
                << (Ref.Expansion ? "expansion" : "original") << ','
                << Ref.Race << ','
                << Ref.Mission << ','
                << EntityID << ','
                << EntityKind(Building, Resource) << ','
                << Unit->ChkUnitID << ','
                << (Player ? Player->PlayerID : -1) << ','
                << BoolText(Active) << ','
                << Trans->Position.X << ','
                << Trans->Position.Y << ','
                << ScreenX << ','
                << ScreenY << ','
                << BoolText(OnScreen) << ','
                << Unit->HitPoints << ','
                << Unit->MaxHitPoints << ','
                << Unit->ShieldPoints << ','
                << Unit->MaxShieldPoints << ','
                << Unit->EnergyPoints << ','
                << Unit->MaxEnergyPoints << ','
                << BoolText(Render ? Render->Visible : false) << ','
                << (Render ? Render->ImageID : 0) << ','
                << (Render ? Render->FrameIndex : -1) << ','
                << (Render ? Render->TotalFrames : 0) << ','
                << (Render ? (int)Render->Layer : -1) << ','
                << BoolText(Render ? Render->Flipped : false) << ','
                << (Building ? Building->BuildingDefID : -1) << ','
                << BoolText(Building ? Building->UnderConstruction : false) << ','
                << (Building ? Building->BuildProgress : 0.0f) << ','
                << BoolText(Building ? Building->IsLifted : false) << ','
                << CsvEscape(Command ? CommandName(Command->CurrentCommand) : "None") << ','
                << (Command ? Command->TargetEntityID : -1) << ','
                << Target.X << ','
                << Target.Y << ','
                << (Path ? (int)Path->Path.size() : 0) << ','
                << (Path ? Path->PathIndex : -1) << ','
                << (Path ? Path->CurrentSpeed : 0.0f) << ','
                << (Path ? Path->TargetSpeed : 0.0f) << ','
                << Velocity.X << ','
                << Velocity.Y << ','
                << (Path ? Path->FacingAngle : 0.0f) << ','
                << BoolText(Path ? Path->IsAir : false) << ','
                << BoolText(MovingIntent) << ','
                << CsvEscape(Harvest ? HarvestName(Harvest->State) : "None") << ','
                << (Resource ? Resource->Amount : 0) << ','
                << (Resource ? Resource->MaxAmount : 0) << ','
                << (ImageTree ? (int)ImageTree->Nodes.size() : 0) << ','
                << CsvEscape(IssueText)
                << '\n';
        }

        Require(MissionRows > 0, "campaign audit captured entities: " + Ref.Path);
        std::printf("Campaign entity audit: phase=%s set=%s race=%d mission=%d rows=%d on_screen=%d anomalies=%d camera=%.1f,%.1f\n",
            Phase ? Phase : "",
            Ref.Expansion ? "expansion" : "original",
            Ref.Race,
            Ref.Mission,
            MissionRows,
            MissionOnScreen,
            MissionAnomalies,
            CameraX,
            CameraY);
    }

    void RunMission(bool Expansion, int Race, int Mission,
                    std::ofstream& AuditLog,
                    AuditTotals& Totals)
    {
        const auto Action = Game::Campaign::BuildCampaignMissionAction(Expansion, Race, Mission);
        const auto Ref = Game::Campaign::ResolveCampaignMission(Action);
        Require(Ref.Valid, "campaign action resolves: " + Action);
        Require(Ref.Expansion == Expansion && Ref.Race == Race && Ref.Mission == Mission,
            "campaign action round-trips: " + Action);
        Require(std::filesystem::exists(Ref.Path), "campaign CHK exists: " + Ref.Path);

        auto Result = Game::Client::MapLoader::Load(Ref.Path);
        Require(Result.Loaded, "campaign map loads: " + Ref.Path + " error=" + Result.ErrorMessage);
        Require(!Result.RawChkData.empty(), "campaign map exposes CHK bytes: " + Ref.Path);
        Require(!std::any_of(Result.ChkUnits.begin(), Result.ChkUnits.end(),
                    [](const Game::Map::PlacedUnit& Unit) { return Unit.UnitID == 214; }),
            "campaign spawn units exclude Special_Start_Location markers: " + Ref.Path);

        const float LongRunSeconds = ReadEnvFloat(
            "STARCRAFT_CAMPAIGN_LONG_AUDIT_SECONDS", 180.0f, 0.0f, 900.0f);
        const float SampleSeconds = ReadEnvFloat(
            "STARCRAFT_CAMPAIGN_LONG_AUDIT_SAMPLE_SECONDS", 5.0f, 0.5f, 60.0f);
        const float StepSeconds = ReadEnvFloat(
            "STARCRAFT_CAMPAIGN_LONG_AUDIT_STEP_SECONDS", 0.25f, 1.0f / 24.0f, 1.0f);

        Game::Trigger::SCRTriggerParser Parser;
        Game::Trigger::SCRTriggerData Data;
        Require(Parser.Parse(Result.RawChkData, Data),
            "campaign trigger parser succeeds: " + Ref.Path);
        Require(!Data.Triggers.empty(), "campaign has TRIG data: " + Ref.Path);

        Core::ECS::SystemManager SysMgr;
        auto& EM = SysMgr.GetEntityManager();
        SeedCampaignUnits(EM, Result);
        auto PathingMap = BuildPathingMap(Result);
        const auto* ActivePathingMap = LongRunSeconds > 0.0f ? &PathingMap : nullptr;
        Game::Systems::CommandSystem::SetMap(ActivePathingMap);

        AuditMissionEntities(Ref, Result, EM, "loaded", 0.0f, AuditLog, Totals);

        Game::Campaign::CampaignController Controller;
        Require(Controller.Init(EM, &Data), "campaign controller initializes: " + Ref.Path);
        const auto HumanPlayer = std::find_if(
            Data.Players.begin(), Data.Players.begin() + 8,
            [](const Game::Trigger::SCRPlayerInfo& Player) { return Player.Owner == 6; });
        Require(HumanPlayer != Data.Players.begin() + 8,
            "campaign declares an occupied human player: " + Ref.Path);
        Require(Controller.GetLocalPlayerID() ==
                    (int)std::distance(Data.Players.begin(), HumanPlayer),
            "campaign preserves the CHK human player slot: " + Ref.Path);
        for (int Tick = 0; Tick < 8; ++Tick)
            Controller.Update(EM, 1.0f);
        Require(Controller.GetState() == Game::Campaign::CampaignState::Playing,
                "campaign remains playable after initial trigger ticks: " + Ref.Path);

        auto* VM = Controller.GetTriggerVM();
        Require(VM != nullptr && VM->HasScript(), "campaign has runnable trigger VM: " + Ref.Path);
        const int UnsupportedConditions = SumCounts(VM->GetUnsupportedConditionCounts());
        const int UnsupportedActions = SumCounts(VM->GetUnsupportedActionCounts());
        if (UnsupportedConditions != 0 || UnsupportedActions != 0)
        {
            std::fprintf(stderr, "Unsupported trigger coverage in %s\n", Ref.Path.c_str());
            PrintNonZeroCounts("unsupported_conditions", VM->GetUnsupportedConditionCounts());
            PrintNonZeroCounts("unsupported_actions", VM->GetUnsupportedActionCounts());
            std::exit(1);
        }

        AuditMissionEntities(Ref, Result, EM, "after_trigger_ticks", 8.0f, AuditLog, Totals);

        Game::Systems::MovementSystem Movement(SysMgr, ActivePathingMap);
        Game::Systems::AnimationSystem Animation(SysMgr);
        for (int Step = 0; Step < 2; ++Step)
            Movement.Update(0.25f);
        AuditMissionEntities(Ref, Result, EM, "after_movement_step", 8.5f, AuditLog, Totals);

        for (int Step = 0; Step < 4; ++Step)
            Animation.Update(1.0f / 24.0f);
        AuditMissionEntities(Ref, Result, EM, "after_animation_step", 8.6667f, AuditLog, Totals);

        std::unordered_map<int, LongEntityTrace> LongTraces;
        float SimTime = 8.6667f;
        const float EndTime = SimTime + LongRunSeconds;
        float NextSampleTime = SimTime + SampleSeconds;
        TrackLongRunEntities(Result, EM, StepSeconds, LongTraces);
        while (SimTime < EndTime - 0.0001f)
        {
            const float Dt = std::min(StepSeconds, EndTime - SimTime);
            Controller.Update(EM, Dt);
            Movement.Update(Dt);
            Animation.Update(Dt);
            SimTime += Dt;
            TrackLongRunEntities(Result, EM, Dt, LongTraces);

            if (SimTime + 0.0001f >= NextSampleTime)
            {
                AuditMissionEntities(Ref, Result, EM, "long_run_sample",
                                     SimTime, AuditLog, Totals);
                NextSampleTime += SampleSeconds;
            }
        }
        if (LongRunSeconds > 0.0f)
        {
            AuditMissionEntities(Ref, Result, EM, "long_run_final",
                                 SimTime, AuditLog, Totals);
            AnalyzeLongRunTraces(Ref, EM, LongTraces);
        }

        ++Totals.MissionSlots;
        Totals.TriggerTotal += (int)Data.Triggers.size();
        Totals.UnitTotal += (int)Result.ChkUnits.size();
        std::printf("Campaign slot ok: set=%s race=%d mission=%d map=%s triggers=%zu units=%zu objectives=%zu messages=%zu ai=%d\n",
            Expansion ? "expansion" : "original",
            Race,
            Mission,
            Ref.Path.c_str(),
            Data.Triggers.size(),
            Result.ChkUnits.size(),
            Controller.GetObjectives().size(),
            Controller.GetMessages().size(),
            VM->GetActiveAIScriptCount());
        Game::Systems::CommandSystem::SetMap(nullptr);
    }
}

int main()
{
    std::filesystem::path AuditPath = "artifacts/campaign_entity_audit.csv";
    if (const char* EnvPath = std::getenv("STARCRAFT_CAMPAIGN_AUDIT_LOG"))
    {
        if (EnvPath[0] != '\0')
            AuditPath = EnvPath;
    }
    if (AuditPath.has_parent_path())
        std::filesystem::create_directories(AuditPath.parent_path());

    std::ofstream AuditLog(AuditPath, std::ios::binary);
    Require(AuditLog.good(), "open campaign entity audit log: " + AuditPath.string());
    WriteAuditHeader(AuditLog);

    AuditTotals Totals;
    const int FilterExpansion = ReadEnvInt(
        "STARCRAFT_CAMPAIGN_FILTER_EXPANSION", -1, -1, 1);
    const int FilterRace = ReadEnvInt(
        "STARCRAFT_CAMPAIGN_FILTER_RACE", -1, -1, 2);
    const int FilterMission = ReadEnvInt(
        "STARCRAFT_CAMPAIGN_FILTER_MISSION", -1, -1, 9);

    for (bool Expansion : { false, true })
    {
        if (FilterExpansion >= 0 && (Expansion ? 1 : 0) != FilterExpansion)
            continue;
        for (int Race = 0; Race < 3; ++Race)
        {
            if (FilterRace >= 0 && Race != FilterRace)
                continue;
            for (int Mission = 0; Mission < 10; ++Mission)
            {
                if (FilterMission >= 0 && Mission != FilterMission)
                    continue;
                RunMission(Expansion, Race, Mission, AuditLog, Totals);
            }
        }
    }

    AuditLog.flush();
    Require(AuditLog.good(), "write campaign entity audit log: " + AuditPath.string());

    const int ExpectedSlots =
        (FilterExpansion >= 0 ? 1 : 2) *
        (FilterRace >= 0 ? 1 : 3) *
        (FilterMission >= 0 ? 1 : 10);
    Require(Totals.MissionSlots == ExpectedSlots,
        "selected campaign catalog slots were exercised");
    Require(Totals.Anomalies == 0, "campaign entity audit has no state anomalies");
    std::printf("CampaignCatalogRuntimeTest passed: slots=%d triggers=%d units=%d audit_rows=%d buildings=%d resources=%d movable=%d moving_intent=%d on_screen=%d audit=%s\n",
        Totals.MissionSlots,
        Totals.TriggerTotal,
        Totals.UnitTotal,
        Totals.EntityRows,
        Totals.BuildingRows,
        Totals.ResourceRows,
        Totals.MovableRows,
        Totals.MovingIntentRows,
        Totals.OnScreenRows,
        AuditPath.string().c_str());
    return 0;
}
