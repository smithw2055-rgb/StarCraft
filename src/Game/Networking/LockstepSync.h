#pragma once

#include <vector>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <random>
#include <thread>
#include <array>

namespace Game::Networking
{

// ── 256-bit Unique ID ──────────────────────────────────────

struct UID
{
    std::array<uint32_t, 8> Data{};

    static UID Generate()
    {
        UID uid;
        auto Now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        std::hash<std::thread::id> Hasher;
        uint64_t ThreadHash = Hasher(std::this_thread::get_id());
        std::mt19937_64 Rng((uint64_t)Now ^ ThreadHash);
        for (int i = 0; i < 8; ++i) uid.Data[i] = (uint32_t)Rng();
        return uid;
    }

    bool operator==(const UID& O) const { return Data == O.Data; }
    bool operator!=(const UID& O) const { return Data != O.Data; }
    bool operator<(const UID& O) const
    {
        for (int i = 7; i >= 0; --i)
            if (Data[i] != O.Data[i]) return Data[i] < O.Data[i];
        return false;
    }

    void Serialize(std::vector<uint8_t>& Buf) const
    {
        for (auto V : Data)
        {
            Buf.push_back((uint8_t)V);
            Buf.push_back((uint8_t)(V >> 8));
            Buf.push_back((uint8_t)(V >> 16));
            Buf.push_back((uint8_t)(V >> 24));
        }
    }

    static UID Deserialize(const uint8_t* Ptr)
    {
        UID uid;
        for (int i = 0; i < 8; ++i)
            uid.Data[i] = (uint32_t)Ptr[i*4] | ((uint32_t)Ptr[i*4+1] << 8) |
                          ((uint32_t)Ptr[i*4+2] << 16) | ((uint32_t)Ptr[i*4+3] << 24);
        return uid;
    }
};

} // namespace Game::Networking

namespace std
{
template<> struct hash<Game::Networking::UID>
{
    size_t operator()(const Game::Networking::UID& U) const noexcept
    {
        size_t H = 0;
        for (auto V : U.Data) H ^= (size_t)V + 0x9e3779b9 + (H << 6) + (H >> 2);
        return H;
    }
};
}

namespace Game::Networking
{

// ── Variable-length action format ─────────────────────────

struct SyncAction
{
    uint32_t Frame = 0;
    uint8_t PlayerID = 0;
    std::vector<uint8_t> Data;

    void Serialize(std::vector<uint8_t>& Buf) const
    {
        Buf.push_back((uint8_t)Frame);
        Buf.push_back((uint8_t)(Frame >> 8));
        Buf.push_back((uint8_t)(Frame >> 16));
        Buf.push_back((uint8_t)(Frame >> 24));
        Buf.push_back(PlayerID);
        uint16_t Len = (uint16_t)Data.size();
        Buf.push_back((uint8_t)Len);
        Buf.push_back((uint8_t)(Len >> 8));
        Buf.insert(Buf.end(), Data.begin(), Data.end());
    }

    static SyncAction Deserialize(const uint8_t* Ptr, size_t Size)
    {
        SyncAction A;
        if (Size < 7) return A;
        A.Frame = (uint32_t)Ptr[0] | ((uint32_t)Ptr[1] << 8) |
                  ((uint32_t)Ptr[2] << 16) | ((uint32_t)Ptr[3] << 24);
        A.PlayerID = Ptr[4];
        uint16_t Len = (uint16_t)Ptr[5] | ((uint16_t)Ptr[6] << 8);
        if (Size >= 7 + Len)
            A.Data.assign(Ptr + 7, Ptr + 7 + Len);
        return A;
    }
};

// ── Message types ──────────────────────────────────────────

enum class SyncMsgType : uint8_t
{
    Invalid = 0,
    Greeting = 1, GreetingAck = 2, ClientUID = 3,
    GameInfo = 4, OccupySlot = 5, SetRace = 6,
    StartGame = 7, GameStarted = 8, LeaveGame = 9,
    ScheduledAction = 10, SyncHash = 11,
    CreateUnit = 12, KillUnit = 13, RemoveUnit = 14,
    PlayerLeft = 15, KeepAlive = 16,
};

// ── Client state ───────────────────────────────────────────

struct ClientState
{
    UID Uid;
    uint32_t ClientID = 0;
    uint8_t Slot = 0xFF;
    uint8_t Race = 0;
    char Name[32] = {};
    bool Connected = false;
    bool GreetingDone = false;
    bool UidExchanged = false;
    bool AllowSend = false;
    uint32_t LastSyncMs = 0;
    uint32_t ReportedFrame = 0;
};

// ── LockstepSync ────────────────────────────────────────────

class LockstepSync
{
public:
    using ActionCallback = std::function<void(const SyncAction&)>;
    using StateHashFn = std::function<uint32_t()>;

    LockstepSync() = default;

    void Init(uint8_t TotalSlots, uint8_t LatencyFrames = 2)
    {
        mUid = UID::Generate();
        mTotalSlots = TotalSlots;
        mLatencyFrames = LatencyFrames;
        mCurrentFrame = 0;
        mGameStarted = false;
        mGameStartingCountdown = 0;
        mGreetingValue = (uint8_t)(std::chrono::steady_clock::now().time_since_epoch().count() & 0xFF);

        mClients.clear();
        mUidIndex.clear();
        mSlotOccupants.assign(TotalSlots, 0xFFFFFFFFu);
        mSlotRaces.assign(TotalSlots, 0xFF);
        mScheduledActions.clear();
        mSyncHashes.fill({0, 0});
        mHashIndex = 0;

        // Self
        ClientState Me;
        Me.Uid = mUid;
        Me.ClientID = 0;
        Me.Slot = 0;
        Me.Connected = true;
        Me.GreetingDone = true;
        Me.UidExchanged = true;
        Me.AllowSend = true;
        mClients[0] = Me;
        mUidIndex[mUid] = 0;
        RebuildClientOrder();
    }

    // ── Connection ─────────────────────────────────────────

    void OnClientConnected(uint32_t ClientID)
    {
        ClientState C;
        C.ClientID = ClientID;
        C.Connected = true;
        C.LastSyncMs = NowMs();
        mClients[ClientID] = C;

        std::vector<uint8_t> Msg;
        Msg.push_back((uint8_t)SyncMsgType::Greeting);
        Msg.push_back(mGreetingValue);
        WriteU32(Msg, mCurrentFrame);
        PushMsg(ClientID, Msg);
    }

    void OnClientDisconnected(uint32_t ClientID)
    {
        auto It = mClients.find(ClientID);
        if (It == mClients.end()) return;
        mUidIndex.erase(It->second.Uid);
        mClients.erase(It);
        RebuildClientOrder();
    }

    bool ProcessMessage(uint32_t FromID, const uint8_t* Data, size_t Size)
    {
        if (Size < 1) return false;
        SyncMsgType Type = (SyncMsgType)Data[0];
        auto& C = mClients[FromID];
        C.LastSyncMs = NowMs();

        switch (Type)
        {
            case SyncMsgType::Greeting:
                if (Size >= 2)
                {
                    std::vector<uint8_t> Ack;
                    Ack.push_back((uint8_t)SyncMsgType::GreetingAck);
                    Ack.push_back(Data[1]);
                    PushMsg(FromID, Ack);
                    C.GreetingDone = true;
                }
                break;

            case SyncMsgType::GreetingAck:
                if (Size >= 2 && Data[1] == mGreetingValue)
                {
                    C.GreetingDone = true;
                    std::vector<uint8_t> UidMsg;
                    UidMsg.push_back((uint8_t)SyncMsgType::ClientUID);
                    mUid.Serialize(UidMsg);
                    for (char Ch : mPlayerName) {
                        UidMsg.push_back((uint8_t)Ch);
                        if (Ch == '\0') break;
                    }
                    if (mPlayerName.empty()) UidMsg.push_back(0);
                    PushMsg(FromID, UidMsg);
                }
                break;

            case SyncMsgType::ClientUID:
                if (Size >= 33)
                {
                    UID RemoteUid = UID::Deserialize(Data + 1);
                    if (mUidIndex.count(RemoteUid) && mUidIndex[RemoteUid] != FromID)
                    {
                        PushMsg(FromID, {(uint8_t)SyncMsgType::LeaveGame});
                        return false;
                    }
                    C.Uid = RemoteUid;
                    C.UidExchanged = true;
                    mUidIndex[RemoteUid] = FromID;
                    int NI = 0;
                    for (size_t Pos = 33; Pos < Size && NI < 31; ++Pos)
                    {
                        C.Name[NI] = (char)Data[Pos];
                        if (C.Name[NI] == '\0') break;
                        ++NI;
                    }
                    C.Name[NI] = '\0';
                    C.AllowSend = true;
                    RebuildClientOrder();
                    BroadcastGameInfo();
                }
                break;

            case SyncMsgType::GameInfo:
                DeserializeGameInfo(Data + 1, Size - 1);
                break;

            case SyncMsgType::OccupySlot:
                if (Size >= 2) {
                    uint8_t Slot = Data[1];
                    if (Slot < mTotalSlots && mSlotOccupants[Slot] == 0xFFFFFFFFu) {
                        mSlotOccupants[Slot] = FromID;
                        C.Slot = Slot;
                        BroadcastGameInfo();
                    }
                }
                break;

            case SyncMsgType::SetRace:
                if (Size >= 2) {
                    C.Race = Data[1];
                    if (C.Slot < mTotalSlots) mSlotRaces[C.Slot] = Data[1];
                    BroadcastGameInfo();
                }
                break;

            case SyncMsgType::StartGame:
                if (FromID == 0) {
                    mGameStateSeed = Size >= 5 ? ReadU32(Data + 1) : NowMs();
                    mGameStartingCountdown = 10;
                }
                break;

            case SyncMsgType::GameStarted:
                if (!mGameStarted && mGameStartingCountdown > 0) {
                    mGameStarted = true;
                    mCurrentFrame = 0;
                    mGameStartingCountdown = 0;
                }
                break;

            case SyncMsgType::ScheduledAction:
                if (Size >= 7) ReceiveRemoteAction(FromID, Data + 1, Size - 1);
                break;

            case SyncMsgType::SyncHash:
                if (Size >= 6) mRemoteHashes[Data[1]][FromID] = ReadU32(Data + 2);
                break;

            case SyncMsgType::LeaveGame:
            case SyncMsgType::PlayerLeft:
                OnClientDisconnected(FromID);
                break;

            default: break;
        }
        return true;
    }

    std::vector<uint8_t> GetNextOutgoingMsg(uint32_t ClientID)
    {
        auto It = mPendingMessages.find(ClientID);
        if (It == mPendingMessages.end() || It->second.empty()) return {};

        std::vector<uint8_t> Msg = std::move(It->second.front());
        It->second.erase(It->second.begin());
        return Msg;
    }

    bool HasPendingMessages(uint32_t ClientID) const
    {
        auto It = mPendingMessages.find(ClientID);
        return It != mPendingMessages.end() && !It->second.empty();
    }

    // ── Game lifecycle ─────────────────────────────────────

    bool AllClientsReady() const
    {
        if (mClients.size() < (size_t)mTotalSlots) return false;
        for (auto& [id, c] : mClients)
            if (!c.GreetingDone || !c.UidExchanged) return false;
        return true;
    }

    bool AllClientsInSync() const
    {
        for (auto& [id, c] : mClients)
        {
            if (!c.Connected) continue;
            if ((int8_t)(mCurrentFrame - c.ReportedFrame) >= (int8_t)mLatencyFrames)
                return false;
        }
        return true;
    }

    void StartGame()
    {
        mGameStateSeed = GenerateGameSeed();
        mGameStartingCountdown = 10;
        Broadcast({(uint8_t)SyncMsgType::StartGame,
                   (uint8_t)mGameStateSeed, (uint8_t)(mGameStateSeed >> 8),
                   (uint8_t)(mGameStateSeed >> 16), (uint8_t)(mGameStateSeed >> 24)});
    }

    bool IsGameStarted() const { return mGameStarted; }
    uint8_t GetCountdown() const { return mGameStartingCountdown; }
    uint32_t GetGameSeed() const { return mGameStateSeed; }
    uint32_t GetCurrentFrame() const { return mCurrentFrame; }
    uint8_t GetLatencyFrames() const { return mLatencyFrames; }
    uint8_t GetTotalSlots() const { return mTotalSlots; }

    // ── Action scheduling ──────────────────────────────────

    void ScheduleAction(uint8_t PlayerID, const std::vector<uint8_t>& ActionData)
    {
        uint32_t TargetFrame = mCurrentFrame + mLatencyFrames;
        SyncAction A;
        A.Frame = TargetFrame;
        A.PlayerID = PlayerID;
        A.Data = ActionData;
        mScheduledActions.push_back(A);

        std::vector<uint8_t> Msg;
        Msg.push_back((uint8_t)SyncMsgType::ScheduledAction);
        A.Serialize(Msg);
        BroadcastToOthers(Msg);
    }

    // ── Frame advancement ──────────────────────────────────

    std::vector<SyncAction> AdvanceFrame(ActionCallback OnAction = nullptr)
    {
        std::vector<SyncAction> Commands;

        auto It = mScheduledActions.begin();
        while (It != mScheduledActions.end())
        {
            if (It->Frame == mCurrentFrame)
            {
                Commands.push_back(*It);
                if (OnAction) OnAction(*It);
                It = mScheduledActions.erase(It);
            }
            else ++It;
        }

        auto RemoteIt = mRemoteActions.find(mCurrentFrame);
        if (RemoteIt != mRemoteActions.end())
        {
            for (auto& A : RemoteIt->second)
            {
                Commands.push_back(A);
                if (OnAction) OnAction(A);
            }
            mRemoteActions.erase(RemoteIt);
        }

        if (mCurrentFrame > 0 && mCurrentFrame % 32 == 0)
        {
            uint32_t Hash = ComputeStateHash();
            mSyncHashes[mHashIndex] = {mCurrentFrame, Hash};
            mHashIndex = (mHashIndex + 1) % 4;

            std::vector<uint8_t> HashMsg;
            HashMsg.push_back((uint8_t)SyncMsgType::SyncHash);
            HashMsg.push_back(mHashIndex);
            WriteU32(HashMsg, Hash);
            BroadcastToOthers(HashMsg);
        }

        if (mGameStartingCountdown > 0)
        {
            --mGameStartingCountdown;
            if (mGameStartingCountdown == 0)
            {
                mGameStarted = true;
                mCurrentFrame = 0;
                Broadcast({(uint8_t)SyncMsgType::GameStarted});
            }
        }

        ++mCurrentFrame;
        return Commands;
    }

    void ReportClientFrame(uint32_t ClientID, uint32_t Frame)
    {
        auto It = mClients.find(ClientID);
        if (It != mClients.end()) It->second.ReportedFrame = Frame;
    }

    // ── State hash ─────────────────────────────────────────

    void SetStateHashCallback(StateHashFn CB) { mStateHashCallback = std::move(CB); }

    uint32_t ComputeStateHash() const
    {
        if (mStateHashCallback) return mStateHashCallback();
        uint32_t H = 2166136261u;
        H = (H ^ (mCurrentFrame & 0xFF)) * 16777619u;
        H = (H ^ ((mCurrentFrame >> 8) & 0xFF)) * 16777619u;
        H = (H ^ ((mCurrentFrame >> 16) & 0xFF)) * 16777619u;
        H = (H ^ ((mCurrentFrame >> 24) & 0xFF)) * 16777619u;
        return H;
    }

    bool VerifyHash(uint32_t FromClient, uint8_t Index, uint32_t RemoteHash) const
    {
        auto It = mRemoteHashes.find(Index);
        if (It == mRemoteHashes.end()) return true;
        auto CI = It->second.find(FromClient);
        if (CI == It->second.end()) return true;
        return CI->second == mSyncHashes[Index].Hash;
    }

    // ── Trigger unit sync ──────────────────────────────────

    void SyncCreateUnit(uint32_t UnitTypeID, int32_t X, int32_t Y, uint8_t Owner)
    {
        std::vector<uint8_t> Msg;
        Msg.push_back((uint8_t)SyncMsgType::CreateUnit);
        WriteU32(Msg, UnitTypeID);
        WriteI32(Msg, X); WriteI32(Msg, Y);
        Msg.push_back(Owner);
        BroadcastToOthers(Msg);
    }

    void SynckillUnit(uint32_t UnitID)
    {
        std::vector<uint8_t> Msg;
        Msg.push_back((uint8_t)SyncMsgType::KillUnit);
        WriteU32(Msg, UnitID);
        BroadcastToOthers(Msg);
    }

    void SyncRemoveUnit(uint32_t UnitID)
    {
        std::vector<uint8_t> Msg;
        Msg.push_back((uint8_t)SyncMsgType::RemoveUnit);
        WriteU32(Msg, UnitID);
        BroadcastToOthers(Msg);
    }

    // ── Lobby / slot ───────────────────────────────────────

    void OccupySlot(uint8_t Slot)
    {
        if (Slot >= mTotalSlots || mSlotOccupants[Slot] != 0xFFFFFFFFu) return;
        mSlotOccupants[Slot] = 0;
        mClients[0].Slot = Slot;
        Broadcast({(uint8_t)SyncMsgType::OccupySlot, Slot});
    }

    void SetRace(uint8_t Race)
    {
        mClients[0].Race = Race;
        if (mClients[0].Slot < mTotalSlots) mSlotRaces[mClients[0].Slot] = Race;
        Broadcast({(uint8_t)SyncMsgType::SetRace, Race});
    }

    uint8_t GetSlotRace(uint8_t Slot) const { return Slot < mTotalSlots ? mSlotRaces[Slot] : 0xFF; }
    uint32_t GetSlotClient(uint8_t Slot) const { return Slot < mTotalSlots ? mSlotOccupants[Slot] : 0xFFFFFFFFu; }

    // ── Client info ────────────────────────────────────────

    size_t GetClientCount() const { return mClients.size(); }
    const ClientState* GetClient(uint32_t ID) const
    {
        auto It = mClients.find(ID);
        return It != mClients.end() ? &It->second : nullptr;
    }
    const std::unordered_map<uint32_t, ClientState>& GetClients() const { return mClients; }
    const std::vector<uint32_t>& GetClientOrder() const { return mClientOrder; }
    const UID& GetMyUID() const { return mUid; }

    // ── Timeout ────────────────────────────────────────────

    void CheckTimeouts(uint32_t TimeoutMs = 60000)
    {
        uint32_t Now = NowMs();
        std::vector<uint32_t> ToRemove;
        for (auto& [id, c] : mClients)
            if (id != 0 && Now - c.LastSyncMs > TimeoutMs)
                ToRemove.push_back(id);
        for (auto id : ToRemove) OnClientDisconnected(id);
    }

    // ── Player left action encoding ────────────────────────

    static std::vector<uint8_t> EncodePlayerLeftAction(uint8_t PlayerID, bool Defeated)
    {
        return {87, PlayerID, (uint8_t)(Defeated ? 6 : 0)};
    }

private:
    void RebuildClientOrder()
    {
        mClientOrder.clear();
        for (auto& [id, c] : mClients) mClientOrder.push_back(id);
        std::sort(mClientOrder.begin(), mClientOrder.end(),
            [this](uint32_t A, uint32_t B) { return mClients[A].Uid < mClients[B].Uid; });
    }

    uint32_t GenerateGameSeed() const
    {
        uint32_t Seed = 0;
        for (auto& [id, c] : mClients)
            Seed ^= c.Uid.Data[0] ^ c.Uid.Data[4];
        return Seed;
    }

    void PushMsg(uint32_t ClientID, std::vector<uint8_t> Msg)
    {
        mPendingMessages[ClientID].push_back(std::move(Msg));
    }

    void Broadcast(const std::vector<uint8_t>& Msg)
    {
        for (auto& [id, c] : mClients)
            if (id != 0) mPendingMessages[id].push_back(Msg);
    }

    void BroadcastToOthers(const std::vector<uint8_t>& Msg)
    {
        for (auto& [id, c] : mClients)
            if (id != 0 && c.AllowSend) mPendingMessages[id].push_back(Msg);
    }

    void BroadcastGameInfo()
    {
        std::vector<uint8_t> Msg;
        Msg.push_back((uint8_t)SyncMsgType::GameInfo);
        WriteU32(Msg, (uint32_t)mClients.size());
        for (auto& [id, c] : mClients)
        {
            c.Uid.Serialize(Msg);
            Msg.push_back(c.Slot);
            Msg.push_back(c.Race);
            for (int i = 0; i < 32; ++i)
            {
                Msg.push_back((uint8_t)c.Name[i]);
                if (c.Name[i] == '\0') break;
            }
            Msg.push_back(0);
        }
        for (int i = 0; i < mTotalSlots; ++i) WriteU32(Msg, mSlotOccupants[i]);
        for (int i = 0; i < mTotalSlots; ++i) Msg.push_back(mSlotRaces[i]);
        BroadcastToOthers(Msg);
    }

    void DeserializeGameInfo(const uint8_t* Data, size_t Size)
    {
        size_t P = 0;
        if (P + 4 > Size) return;
        uint32_t NumClients = (uint32_t)Data[P] | ((uint32_t)Data[P+1] << 8) |
                              ((uint32_t)Data[P+2] << 16) | ((uint32_t)Data[P+3] << 24);
        P += 4;

        for (uint32_t i = 0; i < NumClients; ++i)
        {
            if (P + 32 > Size) return;
            UID ClientUid = UID::Deserialize(Data + P); P += 32;
            if (P + 2 > Size) return;
            uint8_t Slot = Data[P++]; uint8_t Race = Data[P++];

            char NameBuf[32] = {};
            int NI = 0;
            while (NI < 31 && P < Size)
            {
                NameBuf[NI] = (char)Data[P++];
                if (NameBuf[NI] == '\0') break;
                ++NI;
            }
            NameBuf[NI] = '\0';
            if (P < Size && Data[P] == 0) ++P;

            if (!mUidIndex.count(ClientUid))
            {
                uint32_t NewID = (uint32_t)(mClients.size() + 100);
                ClientState C;
                C.ClientID = NewID;
                C.Uid = ClientUid;
                C.Slot = Slot;
                C.Race = Race;
                std::memcpy(C.Name, NameBuf, 32);
                C.Connected = true;
                C.GreetingDone = true;
                C.UidExchanged = true;
                C.AllowSend = true;
                mClients[NewID] = C;
                mUidIndex[ClientUid] = NewID;
            }
        }

        for (int i = 0; i < mTotalSlots && P + 4 <= Size; ++i)
        {
            mSlotOccupants[i] = (uint32_t)Data[P] | ((uint32_t)Data[P+1] << 8) |
                                ((uint32_t)Data[P+2] << 16) | ((uint32_t)Data[P+3] << 24);
            P += 4;
        }
        for (int i = 0; i < mTotalSlots && P < Size; ++i)
            mSlotRaces[i] = Data[P++];

        RebuildClientOrder();
    }

    void ReceiveRemoteAction(uint32_t FromID, const uint8_t* Data, size_t Size)
    {
        SyncAction A = SyncAction::Deserialize(Data, Size);
        auto It = mClients.find(FromID);
        if (It != mClients.end()) A.PlayerID = It->second.Slot;
        mRemoteActions[A.Frame].push_back(A);
    }

    static uint32_t NowMs()
    {
        auto Now = std::chrono::steady_clock::now();
        return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
            Now.time_since_epoch()).count();
    }

    static void WriteU32(std::vector<uint8_t>& Buf, uint32_t V)
    {
        Buf.push_back((uint8_t)V);
        Buf.push_back((uint8_t)(V >> 8));
        Buf.push_back((uint8_t)(V >> 16));
        Buf.push_back((uint8_t)(V >> 24));
    }

    static void WriteI32(std::vector<uint8_t>& Buf, int32_t V)
    {
        uint32_t U = (uint32_t)V;
        Buf.push_back((uint8_t)U);
        Buf.push_back((uint8_t)(U >> 8));
        Buf.push_back((uint8_t)(U >> 16));
        Buf.push_back((uint8_t)(U >> 24));
    }

    static uint32_t ReadU32(const uint8_t* P)
    {
        return (uint32_t)P[0] | ((uint32_t)P[1] << 8) |
               ((uint32_t)P[2] << 16) | ((uint32_t)P[3] << 24);
    }

    // ── State ─────────────────────────────────────────────

    UID mUid;
    uint8_t mTotalSlots = 2;
    uint8_t mLatencyFrames = 2;
    uint32_t mCurrentFrame = 0;
    bool mGameStarted = false;
    uint8_t mGameStartingCountdown = 0;
    uint32_t mGameStateSeed = 0;
    uint8_t mGreetingValue = 0;
    std::string mPlayerName;

    std::unordered_map<uint32_t, ClientState> mClients;
    std::unordered_map<UID, uint32_t> mUidIndex;
    std::vector<uint32_t> mClientOrder;

    std::vector<uint32_t> mSlotOccupants;
    std::vector<uint8_t> mSlotRaces;

    std::vector<SyncAction> mScheduledActions;
    std::unordered_map<uint32_t, std::vector<SyncAction>> mRemoteActions;
    std::unordered_map<uint32_t, std::vector<std::vector<uint8_t>>> mPendingMessages;

    struct HashEntry { uint32_t Frame; uint32_t Hash; };
    std::array<HashEntry, 4> mSyncHashes{};
    uint8_t mHashIndex = 0;
    std::unordered_map<uint8_t, std::unordered_map<uint32_t, uint32_t>> mRemoteHashes;

    StateHashFn mStateHashCallback;
};

} // namespace Game::Networking
