#pragma once

// Cross-platform TCP transport for lockstep networking.
// Uses POSIX sockets on Linux/WSL, WinSock2 on Windows.
// No external dependencies (no ASIO required).

#include <cstdint>
#include <vector>
#include <queue>
#include <mutex>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <cstring>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  using SocketType = SOCKET;
  constexpr SocketType kInvalidSocket = INVALID_SOCKET;
  #define CLOSE_SOCKET closesocket
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  using SocketType = int;
  constexpr SocketType kInvalidSocket = -1;
  #define CLOSE_SOCKET close
#endif

namespace Game::Networking
{

// Length-prefixed binary message (uint16_t LE size prefix + payload)
struct TransportMessage
{
    uint32_t ClientID = 0;  // owning client
    std::vector<uint8_t> Payload;
};

using MessageHandler = std::function<void(const TransportMessage&)>;
using ConnectionHandler = std::function<void(uint32_t ClientID, bool Connected)>;

class SyncTransport
{
public:
    SyncTransport() = default;
    ~SyncTransport() { Stop(); }

    bool Init()
    {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
        return true;
    }

    void Stop()
    {
        mRunning = false;
        if (mListenThread.joinable()) mListenThread.join();

        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& [id, fd] : mClients)
            CLOSE_SOCKET(fd);
        mClients.clear();
        mClientIDs.clear();

        if (mListenSocket != kInvalidSocket)
        {
            CLOSE_SOCKET(mListenSocket);
            mListenSocket = kInvalidSocket;
        }

#ifdef _WIN32
        WSACleanup();
#endif
    }

    // ---- Server (host) ----

    bool StartHost(const std::string& BindIP, int Port, int MaxClients = 7)
    {
        mMaxClients = MaxClients;
        mListenSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (mListenSocket == kInvalidSocket) return false;

        int Opt = 1;
#ifdef _WIN32
        setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&Opt, sizeof(Opt));
#else
        setsockopt(mListenSocket, SOL_SOCKET, SO_REUSEADDR, &Opt, sizeof(Opt));
        // Set non-blocking
        int Flags = fcntl(mListenSocket, F_GETFL, 0);
        fcntl(mListenSocket, F_SETFL, Flags | O_NONBLOCK);
#endif

        sockaddr_in Addr{};
        Addr.sin_family = AF_INET;
        Addr.sin_port = htons((uint16_t)Port);
        if (BindIP.empty() || BindIP == "0.0.0.0")
            Addr.sin_addr.s_addr = INADDR_ANY;
        else
            inet_pton(AF_INET, BindIP.c_str(), &Addr.sin_addr);

        if (bind(mListenSocket, (sockaddr*)&Addr, sizeof(Addr)) != 0)
        {
            CLOSE_SOCKET(mListenSocket);
            mListenSocket = kInvalidSocket;
            return false;
        }

        if (listen(mListenSocket, MaxClients + 1) != 0)
        {
            CLOSE_SOCKET(mListenSocket);
            mListenSocket = kInvalidSocket;
            return false;
        }

        mRunning = true;
        mListenThread = std::thread(&SyncTransport::ListenLoop, this);
        return true;
    }

    // ---- Client (join) ----

    bool Connect(const std::string& HostIP, int Port)
    {
        SocketType FD = socket(AF_INET, SOCK_STREAM, 0);
        if (FD == kInvalidSocket) return false;

        sockaddr_in Addr{};
        Addr.sin_family = AF_INET;
        Addr.sin_port = htons((uint16_t)Port);
        inet_pton(AF_INET, HostIP.c_str(), &Addr.sin_addr);

        if (connect(FD, (sockaddr*)&Addr, sizeof(Addr)) != 0)
        {
            CLOSE_SOCKET(FD);
            return false;
        }

        // Non-blocking for async read
#ifdef _WIN32
        u_long Mode = 1;
        ioctlsocket(FD, FIONBIO, &Mode);
#else
        int Flags = fcntl(FD, F_GETFL, 0);
        fcntl(FD, F_SETFL, Flags | O_NONBLOCK);
#endif

        uint32_t CID = mNextClientID++;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mClients[CID] = FD;
            mClientIDs[FD] = CID;
        }

        if (mOnConnect) mOnConnect(CID, true);
        return true;
    }

    // ---- Message I/O ----

    void SendToClient(uint32_t ClientID, const std::vector<uint8_t>& Payload)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto It = mClients.find(ClientID);
        if (It == mClients.end()) return;

        // Length prefix (uint16_t LE)
        uint16_t Len = (uint16_t)Payload.size();
        std::vector<uint8_t> Framed(2 + Payload.size());
        Framed[0] = (uint8_t)Len;
        Framed[1] = (uint8_t)(Len >> 8);
        std::memcpy(Framed.data() + 2, Payload.data(), Payload.size());

        send(It->second, (const char*)Framed.data(), (int)Framed.size(), 0);
    }

    void Broadcast(const std::vector<uint8_t>& Payload)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& [id, fd] : mClients)
        {
            uint16_t Len = (uint16_t)Payload.size();
            std::vector<uint8_t> Framed(2 + Payload.size());
            Framed[0] = (uint8_t)Len;
            Framed[1] = (uint8_t)(Len >> 8);
            std::memcpy(Framed.data() + 2, Payload.data(), Payload.size());
            send(fd, (const char*)Framed.data(), (int)Framed.size(), 0);
        }
    }

    void PollMessages()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        for (auto& [id, fd] : mClients)
        {
            uint8_t Buf[65536];
            while (true)
            {
                int N = recv(fd, (char*)Buf, sizeof(Buf), 0);
                if (N <= 0) break;

                mRecvBuffer[id].insert(mRecvBuffer[id].end(), Buf, Buf + N);

                // Extract length-prefixed messages
                while (mRecvBuffer[id].size() >= 2)
                {
                    uint16_t Len = (uint16_t)mRecvBuffer[id][0] | ((uint16_t)mRecvBuffer[id][1] << 8);
                    if (mRecvBuffer[id].size() < (size_t)(2 + Len)) break;

                    TransportMessage Msg;
                    Msg.ClientID = id;
                    Msg.Payload.assign(mRecvBuffer[id].begin() + 2,
                                       mRecvBuffer[id].begin() + 2 + Len);
                    mRecvBuffer[id].erase(mRecvBuffer[id].begin(),
                                          mRecvBuffer[id].begin() + 2 + Len);

                    if (mOnMessage) mOnMessage(Msg);
                }
            }
        }
    }

    // ---- Client management ----

    void DisconnectClient(uint32_t ClientID)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto It = mClients.find(ClientID);
        if (It != mClients.end())
        {
            CLOSE_SOCKET(It->second);
            mClientIDs.erase(It->second);
            mRecvBuffer.erase(ClientID);
            mClients.erase(It);
            if (mOnConnect) mOnConnect(ClientID, false);
        }
    }

    size_t GetClientCount() const { return mClients.size(); }
    uint32_t GetNextClientID() { return mNextClientID++; }

    // ---- Callbacks ----

    void SetOnMessage(MessageHandler H) { mOnMessage = std::move(H); }
    void SetOnConnect(ConnectionHandler H) { mOnConnect = std::move(H); }

private:
    void ListenLoop()
    {
        while (mRunning)
        {
            sockaddr_in ClientAddr{};
            socklen_t AddrLen = sizeof(ClientAddr);
            SocketType NewFD = accept(mListenSocket, (sockaddr*)&ClientAddr, &AddrLen);

            if (NewFD != kInvalidSocket)
            {
#ifdef _WIN32
                u_long Mode = 1;
                ioctlsocket(NewFD, FIONBIO, &Mode);
#else
                int Flags = fcntl(NewFD, F_GETFL, 0);
                fcntl(NewFD, F_SETFL, Flags | O_NONBLOCK);
#endif
                uint32_t CID = mNextClientID++;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    mClients[CID] = NewFD;
                    mClientIDs[NewFD] = CID;
                }
                if (mOnConnect) mOnConnect(CID, true);
            }
            else
            {
                // No pending connections; sleep briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }

    int mMaxClients = 7;
    SocketType mListenSocket = kInvalidSocket;
    std::atomic<bool> mRunning{false};
    std::thread mListenThread;

    std::mutex mMutex;
    std::unordered_map<uint32_t, SocketType> mClients;
    std::unordered_map<SocketType, uint32_t> mClientIDs;
    std::unordered_map<uint32_t, std::vector<uint8_t>> mRecvBuffer;
    uint32_t mNextClientID = 1;

    MessageHandler mOnMessage;
    ConnectionHandler mOnConnect;
};

} // namespace Game::Networking
