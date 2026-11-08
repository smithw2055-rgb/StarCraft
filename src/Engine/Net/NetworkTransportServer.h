#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Engine/Net/DeterministicSessionServer.h"

namespace Engine::Net
{

enum class NetPacketType : uint8_t
{
    FrameCommand = 1,
    FrameHashVerify = 2,
    PeerHandshake = 3
};

#pragma pack(push, 1)
struct NetPacketHeader
{
    uint32_t Magic = 0x53434E54u; // "SCNT"
    NetPacketType Type = NetPacketType::FrameCommand;
    uint8_t SenderPlayerID = 0;
    uint16_t PayloadBytes = 0;
    uint32_t TargetTick = 0;
    uint32_t StateHashFNV1a = 0;
};
#pragma pack(pop)

struct NetEnvelope
{
    NetPacketHeader Header{};
    std::vector<uint8_t> Payload;
};

// ============================================================================
// INetworkTransport & LoopbackNetworkHub (Phase 48):
// Referenced from Godot MultiplayerPeer & Unity Netcode NetworkTransport:
// - Decouples deterministic lockstep/rollback packet exchange from OS sockets.
// - Provides an in-memory deterministic LoopbackNetworkHub for multi-peer
//   simulation testing and seamless integration with DeterministicSessionServer.
// ============================================================================
class INetworkTransport
{
public:
    using PacketReceiveFn = std::function<void(const NetEnvelope&)>;

    virtual ~INetworkTransport() = default;
    virtual uint8_t GetLocalPeerID() const noexcept = 0;
    virtual void SendToPeer(uint8_t TargetPeerID, const NetEnvelope& Packet) = 0;
    virtual void Broadcast(const NetEnvelope& Packet) = 0;
    virtual size_t PollIncoming(const PacketReceiveFn& OnReceive) = 0;
};

class LoopbackNetworkHub
{
public:
    class Endpoint final : public INetworkTransport
    {
    public:
        Endpoint(LoopbackNetworkHub& Hub, uint8_t PeerID)
            : mHub(Hub)
            , mPeerID(PeerID)
        {
        }

        uint8_t GetLocalPeerID() const noexcept override { return mPeerID; }

        void SendToPeer(uint8_t TargetPeerID, const NetEnvelope& Packet) override
        {
            mHub.RoutePacket(mPeerID, TargetPeerID, Packet);
        }

        void Broadcast(const NetEnvelope& Packet) override
        {
            mHub.BroadcastFrom(mPeerID, Packet);
        }

        size_t PollIncoming(const PacketReceiveFn& OnReceive) override
        {
            auto& Inbox = mHub.mInboxes[mPeerID];
            const size_t Count = Inbox.size();
            std::vector<NetEnvelope> Drained = std::move(Inbox);
            Inbox.clear();

            if (OnReceive)
            {
                for (const auto& Env : Drained)
                    OnReceive(Env);
            }
            return Count;
        }

    private:
        LoopbackNetworkHub& mHub;
        uint8_t mPeerID = 0;
    };

    std::unique_ptr<INetworkTransport> CreatePeerEndpoint(uint8_t PeerID)
    {
        mInboxes[PeerID];
        return std::make_unique<Endpoint>(*this, PeerID);
    }

    void RoutePacket(uint8_t FromPeerID, uint8_t ToPeerID, NetEnvelope Packet)
    {
        Packet.Header.SenderPlayerID = FromPeerID;
        mInboxes[ToPeerID].push_back(std::move(Packet));
        ++mTotalPacketsRouted;
    }

    void BroadcastFrom(uint8_t FromPeerID, const NetEnvelope& Packet)
    {
        for (auto& [PeerID, Inbox] : mInboxes)
        {
            if (PeerID == FromPeerID)
                continue;
            NetEnvelope Copy = Packet;
            Copy.Header.SenderPlayerID = FromPeerID;
            Inbox.push_back(std::move(Copy));
            ++mTotalPacketsRouted;
        }
    }

    // Helper: Serializes a trivially copyable CommandT into a FrameCommand NetEnvelope
    template <typename CommandT>
    static NetEnvelope MakeCommandPacket(uint8_t SenderPlayerID,
                                         uint32_t TargetTick,
                                         const CommandT& Cmd)
    {
        static_assert(std::is_trivially_copyable_v<CommandT>,
                      "Network command payload must be trivially copyable");
        NetEnvelope Env{};
        Env.Header.Type = NetPacketType::FrameCommand;
        Env.Header.SenderPlayerID = SenderPlayerID;
        Env.Header.TargetTick = TargetTick;
        Env.Header.PayloadBytes = static_cast<uint16_t>(sizeof(CommandT));
        Env.Payload.resize(sizeof(CommandT));
        std::memcpy(Env.Payload.data(), &Cmd, sizeof(CommandT));
        return Env;
    }

    static NetEnvelope MakeHashVerifyPacket(uint8_t SenderPlayerID,
                                            uint32_t Tick,
                                            uint32_t HashFNV1a)
    {
        NetEnvelope Env{};
        Env.Header.Type = NetPacketType::FrameHashVerify;
        Env.Header.SenderPlayerID = SenderPlayerID;
        Env.Header.TargetTick = Tick;
        Env.Header.StateHashFNV1a = HashFNV1a;
        return Env;
    }

    // Polls an endpoint and automatically applies incoming FrameCommand / FrameHashVerify
    // packets onto a DeterministicSessionServer<CommandT> (triggering rollback if late!).
    // Processes all FrameCommand packets in the batch before FrameHashVerify packets so
    // out-of-order late commands update tick hashes before remote hash verification runs.
    template <typename CommandT>
    static size_t PumpTransportIntoSession(
        INetworkTransport& Transport,
        DeterministicSessionServer<CommandT>& Session,
        Core::ECS::EntityManager& World,
        bool* OutHashVerifiedOk = nullptr)
    {
        if (OutHashVerifiedOk)
            *OutHashVerifiedOk = true;

        std::vector<NetEnvelope> Batch;
        const size_t Count = Transport.PollIncoming([&](const NetEnvelope& Env) {
            if (Env.Header.Magic == 0x53434E54u)
                Batch.push_back(Env);
        });

        for (const auto& Env : Batch)
        {
            if (Env.Header.Type == NetPacketType::FrameCommand &&
                Env.Payload.size() == sizeof(CommandT))
            {
                CommandT Cmd{};
                std::memcpy(&Cmd, Env.Payload.data(), sizeof(CommandT));
                if (Env.Header.TargetTick < Session.GetCurrentTick())
                {
                    Session.InjectLateCommandAndResimulate(
                        World, Env.Header.TargetTick, Env.Header.SenderPlayerID, Cmd);
                }
                else
                {
                    Session.ScheduleCommand(
                        Env.Header.TargetTick, Env.Header.SenderPlayerID, Cmd);
                }
            }
        }

        for (const auto& Env : Batch)
        {
            if (Env.Header.Type == NetPacketType::FrameHashVerify)
            {
                const bool Match = Session.VerifyRemoteTickHash(
                    Env.Header.TargetTick, Env.Header.StateHashFNV1a);
                if (!Match && OutHashVerifiedOk)
                    *OutHashVerifiedOk = false;
            }
        }

        return Count;
    }

    uint64_t GetTotalPacketsRouted() const noexcept { return mTotalPacketsRouted; }

private:
    std::unordered_map<uint8_t, std::vector<NetEnvelope>> mInboxes;
    uint64_t mTotalPacketsRouted = 0;
};

} // namespace Engine::Net
