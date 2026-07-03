#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

namespace Game::Replay
{

struct ReplayHeader
{
    char GameName[28] = {};
    char MapName[32] = {};
    uint8_t PlayerCount = 0;
    char PlayerNames[12][25] = {};
    uint16_t MapWidth = 0;
    uint16_t MapHeight = 0;
    uint32_t RandomSeed = 0;
    uint8_t GameType = 0;
    uint8_t ControllerType[12] = {};
    uint8_t RaceType[12] = {};
    uint8_t ColorType[12] = {};
    uint8_t ForceType[12] = {};
    uint16_t CampaignID = 0;
};

struct ReplayAction
{
    uint32_t Frame = 0;
    uint8_t PlayerID = 0;
    uint8_t ActionType = 0;
    uint16_t Param1 = 0;
    uint16_t Param2 = 0;
    uint16_t Param3 = 0;
    uint8_t Param4 = 0;
};

// ── ReplayReader (bit-level decompression) ──────────────────

class ReplayReader
{
public:
    bool LoadReplay(const std::vector<uint8_t>& FileData)
    {
        if (FileData.size() < 64) return false;
        mData = FileData;
        mReadPos = 0;

        uint32_t Magic = ReadU32();
        if (Magic != 0x53526573) return false;
        mReadPos = 4;

        uint32_t HeaderSize = ReadU32();
        uint32_t GameInfoSize = ReadU32();

        if (mData.size() < 12 + HeaderSize + GameInfoSize) return false;

        mReadPos = 12;
        ParseGameInfo(GameInfoSize);

        mCompressedData.assign(mData.begin() + mReadPos, mData.end());
        DecompressActions();
        return true;
    }

    const ReplayHeader& GetHeader() const { return mHeader; }
    const std::vector<ReplayAction>& GetActions() const { return mActions; }
    const std::vector<uint8_t>& GetMapData() const { return mMapData; }
    uint32_t GetFrameCount() const
    {
        if (mActions.empty()) return 0;
        return mActions.back().Frame;
    }

private:
    void ParseGameInfo(uint32_t Size)
    {
        size_t Start = mReadPos;
        if (mReadPos + Size > mData.size()) return;

        for (int i = 0; i < 27 && mReadPos < Start + Size; ++i)
            mHeader.GameName[i] = (char)ReadU8();
        mHeader.GameName[27] = '\0';

        for (int i = 0; i < 31 && mReadPos < Start + Size; ++i)
            mHeader.MapName[i] = (char)ReadU8();
        mHeader.MapName[31] = '\0';

        mHeader.PlayerCount = ReadU8();

        for (int p = 0; p < 12; ++p)
        {
            for (int i = 0; i < 24 && mReadPos < Start + Size; ++i)
                mHeader.PlayerNames[p][i] = (char)ReadU8();
            mHeader.PlayerNames[p][24] = '\0';
        }

        mHeader.MapWidth = ReadU16();
        mHeader.MapHeight = ReadU16();
        mHeader.RandomSeed = ReadU32();
        mHeader.GameType = ReadU8();

        for (int i = 0; i < 12; ++i) mHeader.ControllerType[i] = ReadU8();
        for (int i = 0; i < 12; ++i) mHeader.RaceType[i] = ReadU8();
        for (int i = 0; i < 12; ++i) mHeader.ColorType[i] = ReadU8();
        for (int i = 0; i < 12; ++i) mHeader.ForceType[i] = ReadU8();

        mHeader.CampaignID = ReadU16();

        uint32_t MapDataSize = ReadU32();
        if (MapDataSize > 0 && mReadPos + MapDataSize <= mData.size())
        {
            mMapData.insert(mMapData.end(), mData.begin() + mReadPos,
                            mData.begin() + mReadPos + MapDataSize);
            mReadPos += MapDataSize;
        }
    }

    void DecompressActions()
    {
        // Bit-level decompression matching Brood War replay format
        size_t Pos = 0;
        uint8_t Flags = 0;
        int BitsRemaining = 0;

        auto ReadBit = [&]() -> uint8_t {
            if (BitsRemaining == 0)
            {
                Flags = (Pos < mCompressedData.size()) ? mCompressedData[Pos++] : 0;
                BitsRemaining = 8;
            }
            uint8_t Bit = Flags & 1;
            Flags >>= 1;
            --BitsRemaining;
            return Bit;
        };

        auto ReadBits = [&](int N) -> uint32_t {
            uint32_t V = 0;
            for (int i = 0; i < N; ++i)
                V |= (uint32_t)ReadBit() << i;
            return V;
        };

        uint32_t Frame = 0;
        uint32_t ActionCount = ReadBits(16);

        for (uint32_t i = 0; i < ActionCount && Pos < mCompressedData.size(); ++i)
        {
            ReplayAction Action;
            uint32_t Delta = ReadBits(8);
            Frame += Delta > 0 ? Delta : ReadBits(16);

            Action.Frame = Frame;
            Action.PlayerID = (uint8_t)ReadBits(4);
            Action.ActionType = (uint8_t)ReadBits(4);

            switch (Action.ActionType)
            {
                case 0: // Select
                    Action.Param1 = (uint16_t)ReadBits(16);
                    Action.Param2 = (uint16_t)ReadBits(16);
                    break;
                case 1: // Right click
                    Action.Param1 = (uint16_t)ReadBits(16);
                    Action.Param2 = (uint16_t)ReadBits(16);
                    Action.Param3 = (uint16_t)ReadBits(16);
                    Action.Param4 = (uint8_t)ReadBits(4);
                    break;
                case 2: // Hotkey
                    Action.Param1 = (uint16_t)ReadBits(4);
                    Action.Param2 = (uint16_t)ReadBits(16);
                    break;
                case 3: // Train/Build
                    Action.Param1 = (uint16_t)ReadBits(8);
                    break;
                case 4: // Ability/Tech
                    Action.Param1 = (uint16_t)ReadBits(16);
                    Action.Param2 = (uint16_t)ReadBits(16);
                    break;
                default: break;
            }
            mActions.push_back(Action);
        }
    }

    uint8_t ReadU8() { return mReadPos < mData.size() ? mData[mReadPos++] : 0; }
    uint16_t ReadU16() { return (uint16_t)(ReadU8() | (ReadU8() << 8)); }
    uint32_t ReadU32() { return (uint32_t)(ReadU16() | (ReadU16() << 16)); }

    ReplayHeader mHeader;
    std::vector<ReplayAction> mActions;
    std::vector<uint8_t> mData;
    std::vector<uint8_t> mCompressedData;
    std::vector<uint8_t> mMapData;
    size_t mReadPos = 0;
};

// ── BitWriter helper for replay compression ─────────────────

class BitWriter
{
public:
    BitWriter(std::vector<uint8_t>& Output) : mOut(Output) {}

    void WriteBits(uint32_t Value, int NumBits)
    {
        for (int i = 0; i < NumBits; ++i)
        {
            mCurrentByte |= ((Value >> i) & 1) << mBitPos;
            ++mBitPos;
            if (mBitPos == 8)
            {
                mOut.push_back(mCurrentByte);
                mCurrentByte = 0;
                mBitPos = 0;
            }
        }
    }

    void WriteU16(uint16_t V) { WriteBits(V, 8); WriteBits(V >> 8, 8); }

    void Flush()
    {
        if (mBitPos > 0)
            mOut.push_back(mCurrentByte);
        mCurrentByte = 0;
        mBitPos = 0;
    }

private:
    std::vector<uint8_t>& mOut;
    uint8_t mCurrentByte = 0;
    int mBitPos = 0;
};

// ── ReplayWriter (bit-level compression) ────────────────────

class ReplayWriter
{
public:
    ReplayWriter() = default;

    void Init(const ReplayHeader& Header, const std::vector<uint8_t>& MapData)
    {
        mHeader = Header;
        mMapData = MapData;
        mActions.clear();
    }

    void RecordAction(uint32_t Frame, uint8_t PlayerID, uint8_t ActionType,
                      uint16_t Param1 = 0, uint16_t Param2 = 0,
                      uint16_t Param3 = 0, uint8_t Param4 = 0)
    {
        mActions.push_back({Frame, PlayerID, ActionType, Param1, Param2, Param3, Param4});
    }

    std::vector<uint8_t> SerializeReplay() const
    {
        std::vector<uint8_t> Result;

        // Magic "SeR\0x53"
        WriteU32(Result, 0x53526573);

        // Placeholder for header + gameinfo sizes (filled later)
        size_t HeaderSizePos = Result.size();
        WriteU32(Result, 0);
        size_t GameInfoSizePos = Result.size();
        WriteU32(Result, 0);

        // Write Game Info
        size_t GameInfoStart = Result.size();
        WriteGameInfo(Result);

        uint32_t GameInfoSize = (uint32_t)(Result.size() - GameInfoStart);

        // Bit-compress actions
        std::vector<uint8_t> Compressed = BitCompressActions();
        Result.insert(Result.end(), Compressed.begin(), Compressed.end());

        // Fix up sizes
        uint32_t* Ptr = (uint32_t*)&Result[GameInfoSizePos];
        *Ptr = GameInfoSize;

        return Result;
    }

private:
    void WriteGameInfo(std::vector<uint8_t>& Buf) const
    {
        for (int i = 0; i < 27; ++i)
            WriteU8(Buf, (uint8_t)(i < 27 ? mHeader.GameName[i] : 0));

        for (int i = 0; i < 31; ++i)
            WriteU8(Buf, (uint8_t)(i < 31 ? mHeader.MapName[i] : 0));

        WriteU8(Buf, mHeader.PlayerCount);

        for (int p = 0; p < 12; ++p)
            for (int i = 0; i < 24; ++i)
                WriteU8(Buf, mHeader.PlayerNames[p][i]);

        WriteU16(Buf, mHeader.MapWidth);
        WriteU16(Buf, mHeader.MapHeight);
        WriteU32(Buf, mHeader.RandomSeed);
        WriteU8(Buf, mHeader.GameType);

        for (int i = 0; i < 12; ++i) WriteU8(Buf, mHeader.ControllerType[i]);
        for (int i = 0; i < 12; ++i) WriteU8(Buf, mHeader.RaceType[i]);
        for (int i = 0; i < 12; ++i) WriteU8(Buf, mHeader.ColorType[i]);
        for (int i = 0; i < 12; ++i) WriteU8(Buf, mHeader.ForceType[i]);

        WriteU16(Buf, mHeader.CampaignID);

        uint32_t MapSize = (uint32_t)mMapData.size();
        WriteU32(Buf, MapSize);
        Buf.insert(Buf.end(), mMapData.begin(), mMapData.end());
    }

    std::vector<uint8_t> BitCompressActions() const
    {
        std::vector<uint8_t> Output;
        BitWriter BW(Output);

        // Action count (16 bits)
        BW.WriteU16((uint16_t)mActions.size());

        uint32_t LastFrame = 0;
        for (auto& A : mActions)
        {
            // Delta-encoded frame
            uint32_t Delta = A.Frame - LastFrame;
            LastFrame = A.Frame;

            if (Delta > 0 && Delta <= 255)
                BW.WriteBits(Delta, 8);
            else
                BW.WriteBits(0, 8), BW.WriteBits(Delta, 16);

            // PlayerID (4 bits) + ActionType (4 bits)
            BW.WriteBits(A.PlayerID & 0x0F, 4);
            BW.WriteBits(A.ActionType & 0x0F, 4);

            switch (A.ActionType)
            {
                case 0: // Select
                    BW.WriteBits(A.Param1, 16);
                    BW.WriteBits(A.Param2, 16);
                    break;
                case 1: // Right click
                    BW.WriteBits(A.Param1, 16);
                    BW.WriteBits(A.Param2, 16);
                    BW.WriteBits(A.Param3, 16);
                    BW.WriteBits(A.Param4, 4);
                    break;
                case 2: // Hotkey
                    BW.WriteBits(A.Param1, 4);
                    BW.WriteBits(A.Param2, 16);
                    break;
                case 3: // Train/Build
                    BW.WriteBits(A.Param1, 8);
                    break;
                case 4: // Ability/Tech
                    BW.WriteBits(A.Param1, 16);
                    BW.WriteBits(A.Param2, 16);
                    break;
                default: break;
            }
        }

        BW.Flush();
        return Output;
    }

    static void WriteU8(std::vector<uint8_t>& Buf, uint8_t V) { Buf.push_back(V); }
    static void WriteU16(std::vector<uint8_t>& Buf, uint16_t V)
    {
        WriteU8(Buf, (uint8_t)V);
        WriteU8(Buf, (uint8_t)(V >> 8));
    }
    static void WriteU32(std::vector<uint8_t>& Buf, uint32_t V)
    {
        WriteU16(Buf, (uint16_t)V);
        WriteU16(Buf, (uint16_t)(V >> 16));
    }

    ReplayHeader mHeader;
    std::vector<ReplayAction> mActions;
    std::vector<uint8_t> mMapData;
};

} // namespace Game::Replay
