#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

#include "Engine/ECS/GenericWorldSnapshot.h"

namespace Engine::ECS
{

// ============================================================================
// WorldSerializer (Phase 37):
// - Game-agnostic binary stream serializer for GenericWorldSnapshot and ECS Worlds.
// - Embeds a 16-byte header (Magic "SCWS", Version 1, Tick, CRC32 checksum).
// - Serializes entity identities and all binary-capable component pools without
//   any hardcoded dependency on Game/Components.h.
// ============================================================================
class WorldSerializer
{
public:
    static constexpr uint32_t kMagic = 0x53574353u; // 'S','C','W','S'
    static constexpr uint32_t kFormatVersion = 1u;

    struct BinaryHeader
    {
        uint32_t Magic = kMagic;
        uint32_t Version = kFormatVersion;
        uint32_t Tick = 0;
        uint32_t PayloadCRC32 = 0;
        uint32_t EntityCount = 0;
        uint32_t PoolCount = 0;
    };

    static uint32_t ComputeCRC32(const uint8_t* Data, size_t Length)
    {
        uint32_t Crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < Length; ++i)
        {
            Crc ^= Data[i];
            for (int Bit = 0; Bit < 8; ++Bit)
            {
                const uint32_t Mask = 0u - (Crc & 1u);
                Crc = (Crc >> 1) ^ (0xEDB88320u & Mask);
            }
        }
        return ~Crc;
    }

    static bool SerializeToBytes(const GenericWorldSnapshot& Snapshot,
                                 std::vector<uint8_t>& OutBytes)
    {
        OutBytes.clear();

        std::vector<uint8_t> Payload;
        Payload.reserve(Snapshot.ComputeTotalBinaryBytes() + 128);

        const auto& Entities = Snapshot.GetEntities();
        for (const auto& Ent : Entities)
        {
            Core::ECS::Detail::AppendPodBytes(Payload, Ent.Id);
            Core::ECS::Detail::AppendPodBytes(Payload, Ent.Generation);
        }

        uint32_t SerializedPools = 0;
        for (const auto& PoolEntry : Snapshot.GetPoolSnapshots())
        {
            if (!PoolEntry.IsBinary)
                continue;
            ++SerializedPools;
            Core::ECS::Detail::AppendPodBytes(Payload, PoolEntry.Slot);
            Core::ECS::Detail::AppendVectorPodBytes(Payload, PoolEntry.Entities);
            Core::ECS::Detail::AppendVectorPodBytes(Payload, PoolEntry.RawBytes);
        }

        BinaryHeader Header{};
        Header.Magic = kMagic;
        Header.Version = kFormatVersion;
        Header.Tick = Snapshot.GetTick();
        Header.PayloadCRC32 = ComputeCRC32(Payload.data(), Payload.size());
        Header.EntityCount = static_cast<uint32_t>(Entities.size());
        Header.PoolCount = SerializedPools;

        OutBytes.resize(sizeof(BinaryHeader) + Payload.size());
        std::memcpy(OutBytes.data(), &Header, sizeof(BinaryHeader));
        if (!Payload.empty())
        {
            std::memcpy(OutBytes.data() + sizeof(BinaryHeader), Payload.data(), Payload.size());
        }
        return true;
    }

    static bool DeserializeFromBytes(const std::vector<uint8_t>& Bytes,
                                     GenericWorldSnapshot& OutSnapshot)
    {
        OutSnapshot.Clear();
        if (Bytes.size() < sizeof(BinaryHeader))
            return false;

        BinaryHeader Header{};
        std::memcpy(&Header, Bytes.data(), sizeof(BinaryHeader));
        if (Header.Magic != kMagic || Header.Version != kFormatVersion)
            return false;

        const uint8_t* PayloadPtr = Bytes.data() + sizeof(BinaryHeader);
        const size_t PayloadLen = Bytes.size() - sizeof(BinaryHeader);
        if (ComputeCRC32(PayloadPtr, PayloadLen) != Header.PayloadCRC32)
            return false;

        std::vector<uint8_t> Payload(PayloadPtr, PayloadPtr + PayloadLen);
        size_t Cursor = 0;

        OutSnapshot.SetTick(Header.Tick);
        auto& Entities = OutSnapshot.GetMutableEntities();
        Entities.resize(Header.EntityCount);
        for (uint32_t i = 0; i < Header.EntityCount; ++i)
        {
            if (!Core::ECS::Detail::ReadPodBytes(Payload, Cursor, Entities[i].Id) ||
                !Core::ECS::Detail::ReadPodBytes(Payload, Cursor, Entities[i].Generation))
            {
                OutSnapshot.Clear();
                return false;
            }
        }

        auto& Pools = OutSnapshot.GetMutablePoolSnapshots();
        Pools.reserve(Header.PoolCount);
        for (uint32_t i = 0; i < Header.PoolCount; ++i)
        {
            GenericWorldSnapshot::PoolSnapshotEntry Entry;
            Entry.IsBinary = true;
            if (!Core::ECS::Detail::ReadPodBytes(Payload, Cursor, Entry.Slot) ||
                !Core::ECS::Detail::ReadVectorPodBytes(Payload, Cursor, Entry.Entities) ||
                !Core::ECS::Detail::ReadVectorPodBytes(Payload, Cursor, Entry.RawBytes))
            {
                OutSnapshot.Clear();
                return false;
            }
            Pools.push_back(std::move(Entry));
        }

        return Cursor == Payload.size();
    }

    static bool CaptureAndSerialize(const Core::ECS::EntityManager& EM,
                                    uint32_t Tick,
                                    std::vector<uint8_t>& OutBytes)
    {
        GenericWorldSnapshot Snap;
        Snap.Capture(EM, Tick);
        return SerializeToBytes(Snap, OutBytes);
    }

    static bool DeserializeAndRestore(Core::ECS::EntityManager& EM,
                                      const std::vector<uint8_t>& Bytes,
                                      uint32_t* OutTick = nullptr)
    {
        GenericWorldSnapshot Snap;
        if (!DeserializeFromBytes(Bytes, Snap))
            return false;
        Snap.Restore(EM);
        if (OutTick)
            *OutTick = Snap.GetTick();
        return true;
    }
};

} // namespace Engine::ECS
