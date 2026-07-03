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
    switch (CmdType)
    {
    case 0:
        if (Action.Data.size() >= 9)
        {
            const float X = ReadNetworkActionFloat(Action.Data, 1);
            const float Y = ReadNetworkActionFloat(Action.Data, 5);
            Game::Systems::CommandSystem::IssueMove(EM, {}, {X, Y});
        }
        break;
    default:
        break;
    }
}

} // namespace Game::Client::GameLoop
