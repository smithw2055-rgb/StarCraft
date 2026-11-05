#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Game/Networking/LockstepSync.h"
#include "Game/Systems/CommandSystem.h"
#include "Main/GameSession.h"

namespace Game::Client::GameLoop
{

inline bool HostSessionNetworkGame(GameSession& Session,
                                   const std::string& BindIP,
                                   int Port,
                                   int MaxClients)
{
    return Session.HostNetworkGame(BindIP, Port, MaxClients);
}

inline bool JoinSessionNetworkGame(GameSession& Session,
                                   const std::string& HostIP,
                                   int Port)
{
    return Session.JoinNetworkGame(HostIP, Port);
}

inline void StartSessionNetworkGame(GameSession& Session)
{
    Session.StartNetworkGameIfEnabled();
}

inline void RecordSessionLocalFrameAction(GameSession& Session,
                                          uint64_t FrameCount,
                                          uint8_t PlayerID,
                                          uint8_t ActionType,
                                          uint16_t P1,
                                          uint16_t P2,
                                          uint16_t P3,
                                          uint8_t P4)
{
    Session.RecordLocalFrameAction((uint32_t)FrameCount, PlayerID, ActionType, P1, P2, P3, P4);
}

#include "Game/Components.h"

inline float ReadNetworkActionFloat(const std::vector<uint8_t>& Data, size_t Offset)
{
    float Value = 0.0f;
    if (Offset + sizeof(Value) <= Data.size())
        std::memcpy(&Value, Data.data() + Offset, sizeof(Value));
    return Value;
}

inline void ExecuteNetworkAction(GameSession& Session, const Game::Networking::SyncAction& Action)
{
    if (Action.Data.empty())
        return;

    auto& EM = Session.Entities();
    const uint8_t CmdType = Action.Data[0];

    auto CollectControllableUnits = [&]() -> std::vector<int> {
        std::vector<int> Units;
        EM.ForEachEntityWith<Game::Components::PlayerComponent, Game::Components::UnitComponent>(
            [&](Core::ECS::EntityID Id) {
                const auto* Player = EM.GetComponent<Game::Components::PlayerComponent>(Id);
                const auto* Unit = EM.GetComponent<Game::Components::UnitComponent>(Id);
                if (Player && Player->PlayerID == Action.PlayerID && Unit && Unit->HitPoints > 0)
                {
                    if (!EM.GetComponent<Game::Components::BuildingComponent>(Id) &&
                        !EM.GetComponent<Game::Components::ResourceComponent>(Id) &&
                        EM.GetComponent<Game::Components::CommandComponent>(Id))
                    {
                        Units.push_back((int)Id);
                    }
                }
            });
        return Units;
    };

    switch (CmdType)
    {
    case 0:
    case 1:
    {
        float X = 0.0f;
        float Y = 0.0f;
        std::vector<int> Units;

        if (CmdType == 0 && Action.Data.size() >= 9)
        {
            X = ReadNetworkActionFloat(Action.Data, 1);
            Y = ReadNetworkActionFloat(Action.Data, 5);
            if (Action.Data.size() > 9)
            {
                for (size_t i = 9; i + sizeof(int) <= Action.Data.size(); i += sizeof(int))
                {
                    int Id = 0;
                    std::memcpy(&Id, Action.Data.data() + i, sizeof(Id));
                    if (EM.IsAlive(Id))
                        Units.push_back(Id);
                }
            }
        }
        else if (Action.Data.size() >= 5)
        {
            const uint16_t P1 = (uint16_t)Action.Data[1] | ((uint16_t)Action.Data[2] << 8);
            const uint16_t P2 = (uint16_t)Action.Data[3] | ((uint16_t)Action.Data[4] << 8);
            X = (float)P1;
            Y = (float)P2;
        }

        if (Units.empty())
            Units = CollectControllableUnits();

        if (!Units.empty())
            Game::Systems::CommandSystem::IssueMove(EM, Units, {X, Y});
        break;
    }
    default:
        break;
    }
}

} // namespace Game::Client::GameLoop
