#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Engine::Gameplay {

enum class ContainerKind2D : std::uint8_t {
    TransportVehicle = 0,
    BunkerGarrison   = 1,
    CarrierHangar    = 2
};

struct PassengerEntry2D {
    int passengerEntityID = -1;
    int passengerPlayerID = 0;
    int spaceCostSlots    = 1;
};

struct ContainerRecord2D {
    int containerEntityID                 = -1;
    int ownerPlayerID                     = 0;
    ContainerKind2D kind                  = ContainerKind2D::TransportVehicle;
    int maxCapacitySlots                  = 8;
    int usedCapacitySlots                 = 0;
    float garrisonBonusRangePx            = 0.0f;
    std::vector<PassengerEntry2D> entries = {};
};

struct ContainerDestructionOutcome2D {
    ContainerKind2D kind                   = ContainerKind2D::TransportVehicle;
    std::vector<int> killedPassengers      = {};
    std::vector<int> evacuatedPassengers   = {};
};

class GarrisonAndTransportServer2D {
public:
    GarrisonAndTransportServer2D() = default;
    ~GarrisonAndTransportServer2D() = default;

    void RegisterContainer(int containerEntityID,
                           int ownerPlayerID,
                           ContainerKind2D kind,
                           int maxCapacitySlots = 8,
                           float garrisonBonusRangePx = 0.0f) {
        if (containerEntityID < 0) {
            return;
        }

        int resolvedCapacity = maxCapacitySlots;
        if (resolvedCapacity <= 0) {
            switch (kind) {
                case ContainerKind2D::TransportVehicle: resolvedCapacity = 8; break;
                case ContainerKind2D::BunkerGarrison:   resolvedCapacity = 4; break;
                case ContainerKind2D::CarrierHangar:    resolvedCapacity = 8; break;
            }
        }

        float resolvedBonusRange = garrisonBonusRangePx;
        if (kind == ContainerKind2D::BunkerGarrison && resolvedBonusRange <= 0.0f) {
            resolvedBonusRange = 64.0f;
        }

        auto it = m_containers.find(containerEntityID);
        if (it != m_containers.end()) {
            it->second.ownerPlayerID        = ownerPlayerID;
            it->second.kind                 = kind;
            it->second.maxCapacitySlots     = std::max(it->second.usedCapacitySlots, resolvedCapacity);
            it->second.garrisonBonusRangePx = resolvedBonusRange;
            return;
        }

        ContainerRecord2D record{};
        record.containerEntityID    = containerEntityID;
        record.ownerPlayerID        = ownerPlayerID;
        record.kind                 = kind;
        record.maxCapacitySlots     = resolvedCapacity;
        record.usedCapacitySlots    = 0;
        record.garrisonBonusRangePx = resolvedBonusRange;
        m_containers.emplace(containerEntityID, std::move(record));
    }

    void UnregisterContainer(int containerEntityID) {
        auto it = m_containers.find(containerEntityID);
        if (it == m_containers.end()) {
            return;
        }
        for (const auto& entry : it->second.entries) {
            m_passengerToContainer.erase(entry.passengerEntityID);
        }
        m_containers.erase(it);
    }

    void Clear() {
        m_containers.clear();
        m_passengerToContainer.clear();
    }

    bool HasContainer(int containerEntityID) const {
        return m_containers.find(containerEntityID) != m_containers.end();
    }

    const ContainerRecord2D* FindContainer(int containerEntityID) const {
        auto it = m_containers.find(containerEntityID);
        return (it != m_containers.end()) ? &it->second : nullptr;
    }

    int GetMaxCapacity(int containerEntityID) const {
        auto it = m_containers.find(containerEntityID);
        return (it != m_containers.end()) ? it->second.maxCapacitySlots : 0;
    }

    int GetUsedCapacity(int containerEntityID) const {
        auto it = m_containers.find(containerEntityID);
        return (it != m_containers.end()) ? it->second.usedCapacitySlots : 0;
    }

    int GetRemainingCapacity(int containerEntityID) const {
        auto it = m_containers.find(containerEntityID);
        if (it == m_containers.end()) {
            return 0;
        }
        return std::max(0, it->second.maxCapacitySlots - it->second.usedCapacitySlots);
    }

    std::vector<int> GetPassengers(int containerEntityID) const {
        std::vector<int> result;
        auto it = m_containers.find(containerEntityID);
        if (it == m_containers.end()) {
            return result;
        }
        result.reserve(it->second.entries.size());
        for (const auto& entry : it->second.entries) {
            result.push_back(entry.passengerEntityID);
        }
        return result;
    }

    bool CanLoadPassenger(int containerEntityID,
                          int passengerEntityID,
                          int passengerPlayerID,
                          int spaceCostSlots = 1) const {
        if (containerEntityID < 0 || passengerEntityID < 0 || containerEntityID == passengerEntityID) {
            return false;
        }
        if (spaceCostSlots <= 0) {
            return false;
        }
        if (IsEntityLoaded(passengerEntityID) || IsEntityLoaded(containerEntityID)) {
            return false;
        }

        auto it = m_containers.find(containerEntityID);
        if (it == m_containers.end()) {
            return false;
        }

        const ContainerRecord2D& container = it->second;
        if (container.ownerPlayerID != passengerPlayerID) {
            return false;
        }

        const int remaining = container.maxCapacitySlots - container.usedCapacitySlots;
        return spaceCostSlots <= remaining;
    }

    bool LoadPassenger(int containerEntityID,
                       int passengerEntityID,
                       int passengerPlayerID,
                       int spaceCostSlots = 1) {
        const int clampedCost = std::max(1, spaceCostSlots);
        if (!CanLoadPassenger(containerEntityID, passengerEntityID, passengerPlayerID, clampedCost)) {
            return false;
        }

        auto& container = m_containers[containerEntityID];
        container.entries.push_back(PassengerEntry2D{
            passengerEntityID,
            passengerPlayerID,
            clampedCost
        });
        container.usedCapacitySlots += clampedCost;
        m_passengerToContainer[passengerEntityID] = containerEntityID;
        return true;
    }

    bool UnloadPassenger(int containerEntityID, int passengerEntityID) {
        auto it = m_containers.find(containerEntityID);
        if (it == m_containers.end()) {
            return false;
        }

        auto& entries = it->second.entries;
        for (auto entryIt = entries.begin(); entryIt != entries.end(); ++entryIt) {
            if (entryIt->passengerEntityID == passengerEntityID) {
                it->second.usedCapacitySlots = std::max(0, it->second.usedCapacitySlots - entryIt->spaceCostSlots);
                entries.erase(entryIt);
                m_passengerToContainer.erase(passengerEntityID);
                return true;
            }
        }
        return false;
    }

    std::vector<std::pair<int, Core::Math::Vec2>> UnloadAllPassengers(
        int containerEntityID,
        Core::Math::Vec2 dropCenter,
        float dropRadiusPx = 48.0f) {
        std::vector<std::pair<int, Core::Math::Vec2>> unloadedDrops;

        auto it = m_containers.find(containerEntityID);
        if (it == m_containers.end() || it->second.entries.empty()) {
            return unloadedDrops;
        }

        const std::size_t count = it->second.entries.size();
        unloadedDrops.reserve(count);

        constexpr float kTwoPi = 6.28318530717958647692f;
        const float safeRadius = std::max(0.0f, dropRadiusPx);

        for (std::size_t i = 0; i < count; ++i) {
            const int passengerID = it->second.entries[i].passengerEntityID;
            m_passengerToContainer.erase(passengerID);

            Core::Math::Vec2 dropPos = dropCenter;
            if (!(count == 1 && safeRadius == 0.0f)) {
                const float angle = (kTwoPi * static_cast<float>(i)) / static_cast<float>(count);
                dropPos.X += std::cos(angle) * safeRadius;
                dropPos.Y += std::sin(angle) * safeRadius;
            }
            unloadedDrops.emplace_back(passengerID, dropPos);
        }

        it->second.entries.clear();
        it->second.usedCapacitySlots = 0;
        return unloadedDrops;
    }

    bool IsEntityLoaded(int passengerEntityID) const {
        return m_passengerToContainer.find(passengerEntityID) != m_passengerToContainer.end();
    }

    int GetHostContainer(int passengerEntityID) const {
        auto it = m_passengerToContainer.find(passengerEntityID);
        return (it != m_passengerToContainer.end()) ? it->second : -1;
    }

    bool IsGarrisonedInBunker(int passengerEntityID) const {
        const int hostID = GetHostContainer(passengerEntityID);
        if (hostID < 0) {
            return false;
        }
        auto it = m_containers.find(hostID);
        return (it != m_containers.end()) && (it->second.kind == ContainerKind2D::BunkerGarrison);
    }

    float GetGarrisonBonusRangePx(int passengerEntityID) const {
        const int hostID = GetHostContainer(passengerEntityID);
        if (hostID < 0) {
            return 0.0f;
        }
        auto it = m_containers.find(hostID);
        if (it == m_containers.end() || it->second.kind != ContainerKind2D::BunkerGarrison) {
            return 0.0f;
        }
        return it->second.garrisonBonusRangePx;
    }

    ContainerDestructionOutcome2D HandleContainerDestroyed(int containerEntityID) {
        ContainerDestructionOutcome2D outcome{};

        auto it = m_containers.find(containerEntityID);
        if (it == m_containers.end()) {
            return outcome;
        }

        outcome.kind = it->second.kind;
        std::vector<int> passengerIDs;
        passengerIDs.reserve(it->second.entries.size());

        for (const auto& entry : it->second.entries) {
            passengerIDs.push_back(entry.passengerEntityID);
            m_passengerToContainer.erase(entry.passengerEntityID);
        }

        if (it->second.kind == ContainerKind2D::BunkerGarrison) {
            outcome.evacuatedPassengers = std::move(passengerIDs);
        } else {
            outcome.killedPassengers = std::move(passengerIDs);
        }

        m_containers.erase(it);
        return outcome;
    }

    std::size_t GetContainerCount() const {
        return m_containers.size();
    }

    std::size_t GetTotalLoadedPassengerCount() const {
        return m_passengerToContainer.size();
    }

private:
    std::unordered_map<int, ContainerRecord2D> m_containers{};
    std::unordered_map<int, int> m_passengerToContainer{};
};

} // namespace Engine::Gameplay
