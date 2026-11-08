#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Engine::Gameplay {

enum class PlacementCheckResult2D : std::uint8_t {
    Valid = 0,
    OutOfBounds,
    UnbuildableTerrain,
    OccupiedByBuilding,
    MissingPowerField,
    MissingCreepField,
    RequiresGeyser,
    TooCloseToResource
};

struct PlacementTile2D {
    bool TerrainWalkable = true;
    bool Walkable = true;
    bool Buildable = true;
    bool HighGround = false;
    bool HasResourceGeyser = false;
    bool HasMineralNode = false;
    bool OccupiedByBuilding = false;
    bool HasCreep = false;
    std::uint8_t PowerMaskByPlayer = 0;
    int OccupyingEntityID = -1;
};

struct FieldProvider2D {
    int EntityID = -1;
    int OwnerPlayerID = 0;
    int TileX = 0;
    int TileY = 0;
    int RadiusTiles = 6;
    bool IsPowerField = false;
    bool IsCreepField = false;
};

struct BuildingPlacementRule2D {
    int FootprintW = 3;
    int FootprintH = 3;
    bool RequiresPower = false;
    bool RequiresCreep = false;
    bool ForbidsCreep = true;
    bool RequiresVespeneGeyser = false;
    int MinResourceDepotDistTiles = 0;
};

struct AddonAttachmentRecord2D {
    int MainBuildingEntityID = -1;
    int AddonEntityID = -1;
    int OffsetX = 4;
    int OffsetY = 1;
};

struct PlacedBuildingRecord2D {
    int EntityID = -1;
    int TileX = 0;
    int TileY = 0;
    int Width = 1;
    int Height = 1;
};

class BuildingPlacementServer2D {
public:
    static constexpr int kMaxSupportedPlayers = 8;

    BuildingPlacementServer2D() = default;
    ~BuildingPlacementServer2D() = default;

    void InitializeGrid(int widthTiles, int heightTiles, float tileSizePx = 32.0f) {
        m_widthTiles = std::max(1, widthTiles);
        m_heightTiles = std::max(1, heightTiles);
        m_tileSizePx = (tileSizePx > 1.0f) ? tileSizePx : 32.0f;

        m_tiles.assign(static_cast<std::size_t>(m_widthTiles * m_heightTiles), PlacementTile2D{});
        m_placedBuildings.clear();
        m_addonAttachments.clear();
        m_poweredTileCountByPlayer.fill(0);
        m_creepTileCount = 0;
    }

    int GetWidthTiles() const { return m_widthTiles; }
    int GetHeightTiles() const { return m_heightTiles; }
    float GetTileSizePx() const { return m_tileSizePx; }

    bool IsInBounds(int tileX, int tileY) const {
        return tileX >= 0 && tileX < m_widthTiles && tileY >= 0 && tileY < m_heightTiles;
    }

    Core::Math::Vec2 TileToWorldTopLeft(int tileX, int tileY) const {
        return Core::Math::Vec2{
            static_cast<float>(tileX) * m_tileSizePx,
            static_cast<float>(tileY) * m_tileSizePx
        };
    }

    Core::Math::Vec2 TileFootprintCenterToWorld(int tileX, int tileY, int footprintW, int footprintH) const {
        return Core::Math::Vec2{
            (static_cast<float>(tileX) + static_cast<float>(footprintW) * 0.5f) * m_tileSizePx,
            (static_cast<float>(tileY) + static_cast<float>(footprintH) * 0.5f) * m_tileSizePx
        };
    }

    void WorldToTile(const Core::Math::Vec2& worldPos, int& outTileX, int& outTileY) const {
        outTileX = static_cast<int>(std::floor(worldPos.X / m_tileSizePx));
        outTileY = static_cast<int>(std::floor(worldPos.Y / m_tileSizePx));
    }

    PlacementTile2D* GetTileMutable(int tileX, int tileY) {
        if (!IsInBounds(tileX, tileY)) {
            return nullptr;
        }
        return &m_tiles[ToIndex(tileX, tileY)];
    }

    const PlacementTile2D* GetTile(int tileX, int tileY) const {
        if (!IsInBounds(tileX, tileY)) {
            return nullptr;
        }
        return &m_tiles[ToIndex(tileX, tileY)];
    }

    void SetTileTerrainFlags(int tileX, int tileY, bool walkable, bool buildable, bool highGround = false) {
        if (PlacementTile2D* tile = GetTileMutable(tileX, tileY)) {
            tile->TerrainWalkable = walkable;
            tile->Walkable = walkable && !tile->OccupiedByBuilding;
            tile->Buildable = buildable;
            tile->HighGround = highGround;
        }
    }

    void SetTileResourceFlags(int tileX, int tileY, bool hasGeyser, bool hasMineral = false) {
        if (PlacementTile2D* tile = GetTileMutable(tileX, tileY)) {
            tile->HasResourceGeyser = hasGeyser;
            tile->HasMineralNode = hasMineral;
        }
    }

    void RebuildPowerAndCreepFields(const std::vector<FieldProvider2D>& providers) {
        m_poweredTileCountByPlayer.fill(0);
        m_creepTileCount = 0;

        for (PlacementTile2D& tile : m_tiles) {
            tile.PowerMaskByPlayer = 0;
            tile.HasCreep = false;
        }

        for (const FieldProvider2D& provider : providers) {
            const int r = (provider.RadiusTiles > 0) ? provider.RadiusTiles : 6;
            const int rSq = r * r;
            const std::uint8_t playerBit = GetPlayerBitMask(provider.OwnerPlayerID);

            const int minX = std::max(0, provider.TileX - r);
            const int maxX = std::min(m_widthTiles - 1, provider.TileX + r);
            const int minY = std::max(0, provider.TileY - r);
            const int maxY = std::min(m_heightTiles - 1, provider.TileY + r);

            for (int y = minY; y <= maxY; ++y) {
                const int dy = y - provider.TileY;
                for (int x = minX; x <= maxX; ++x) {
                    const int dx = x - provider.TileX;
                    if (dx * dx + dy * dy <= rSq) {
                        PlacementTile2D& tile = m_tiles[ToIndex(x, y)];
                        if (provider.IsPowerField && playerBit != 0) {
                            tile.PowerMaskByPlayer |= playerBit;
                        }
                        if (provider.IsCreepField) {
                            tile.HasCreep = true;
                        }
                    }
                }
            }
        }

        for (const PlacementTile2D& tile : m_tiles) {
            if (tile.HasCreep) {
                ++m_creepTileCount;
            }
            if (tile.PowerMaskByPlayer != 0) {
                for (int p = 0; p < kMaxSupportedPlayers; ++p) {
                    if ((tile.PowerMaskByPlayer & static_cast<std::uint8_t>(1u << p)) != 0) {
                        ++m_poweredTileCountByPlayer[static_cast<std::size_t>(p)];
                    }
                }
            }
        }
    }

    bool IsTilePowered(int tileX, int tileY, int playerID) const {
        const PlacementTile2D* tile = GetTile(tileX, tileY);
        if (!tile) {
            return false;
        }
        const std::uint8_t mask = GetPlayerBitMask(playerID);
        return (tile->PowerMaskByPlayer & mask) != 0;
    }

    bool HasCreepAt(int tileX, int tileY) const {
        const PlacementTile2D* tile = GetTile(tileX, tileY);
        return tile ? tile->HasCreep : false;
    }

    PlacementCheckResult2D CanPlaceBuilding(
        int tileX,
        int tileY,
        int playerID,
        const BuildingPlacementRule2D& rule) const {

        const int w = std::max(1, rule.FootprintW);
        const int h = std::max(1, rule.FootprintH);

        if (!IsInBounds(tileX, tileY) || !IsInBounds(tileX + w - 1, tileY + h - 1)) {
            return PlacementCheckResult2D::OutOfBounds;
        }

        for (int y = tileY; y < tileY + h; ++y) {
            for (int x = tileX; x < tileX + w; ++x) {
                const PlacementTile2D& tile = m_tiles[ToIndex(x, y)];

                if (tile.OccupiedByBuilding) {
                    return PlacementCheckResult2D::OccupiedByBuilding;
                }

                if (rule.RequiresVespeneGeyser) {
                    if (!tile.Buildable || tile.HasMineralNode) {
                        return PlacementCheckResult2D::UnbuildableTerrain;
                    }
                    if (!tile.HasResourceGeyser) {
                        return PlacementCheckResult2D::RequiresGeyser;
                    }
                } else {
                    if (!tile.Buildable || tile.HasResourceGeyser || tile.HasMineralNode) {
                        return PlacementCheckResult2D::UnbuildableTerrain;
                    }
                }

                if (rule.RequiresCreep && !tile.HasCreep) {
                    return PlacementCheckResult2D::MissingCreepField;
                }

                if (!rule.RequiresCreep && rule.ForbidsCreep && tile.HasCreep) {
                    return PlacementCheckResult2D::UnbuildableTerrain;
                }

                if (rule.RequiresPower && !IsTilePowered(x, y, playerID)) {
                    return PlacementCheckResult2D::MissingPowerField;
                }
            }
        }

        if (rule.MinResourceDepotDistTiles > 0) {
            const int margin = rule.MinResourceDepotDistTiles;
            const int minSearchX = std::max(0, tileX - margin);
            const int maxSearchX = std::min(m_widthTiles - 1, tileX + w - 1 + margin);
            const int minSearchY = std::max(0, tileY - margin);
            const int maxSearchY = std::min(m_heightTiles - 1, tileY + h - 1 + margin);

            for (int y = minSearchY; y <= maxSearchY; ++y) {
                for (int x = minSearchX; x <= maxSearchX; ++x) {
                    const PlacementTile2D& nearTile = m_tiles[ToIndex(x, y)];
                    if (nearTile.HasResourceGeyser || nearTile.HasMineralNode) {
                        return PlacementCheckResult2D::TooCloseToResource;
                    }
                }
            }
        }

        return PlacementCheckResult2D::Valid;
    }

    void OccupyBuildingFootprint(int entityID, int tileX, int tileY, int w, int h, bool occupied) {
        const int clampedW = std::max(1, w);
        const int clampedH = std::max(1, h);

        for (int y = tileY; y < tileY + clampedH; ++y) {
            for (int x = tileX; x < tileX + clampedW; ++x) {
                if (PlacementTile2D* tile = GetTileMutable(x, y)) {
                    if (occupied) {
                        tile->OccupiedByBuilding = true;
                        tile->Walkable = false;
                        tile->OccupyingEntityID = entityID;
                    } else if (entityID < 0 || tile->OccupyingEntityID < 0 || tile->OccupyingEntityID == entityID) {
                        tile->OccupiedByBuilding = false;
                        tile->Walkable = tile->TerrainWalkable;
                        tile->OccupyingEntityID = -1;
                    }
                }
            }
        }

        if (entityID >= 0) {
            if (occupied) {
                m_placedBuildings[entityID] = PlacedBuildingRecord2D{entityID, tileX, tileY, clampedW, clampedH};
            } else {
                m_placedBuildings.erase(entityID);
                m_addonAttachments.erase(entityID);
            }
        }
    }

    bool AttachAddon(int mainBuildingEntityID, int addonEntityID, int offsetX = 4, int offsetY = 1) {
        if (mainBuildingEntityID < 0 || addonEntityID < 0) {
            return false;
        }
        AddonAttachmentRecord2D record{};
        record.MainBuildingEntityID = mainBuildingEntityID;
        record.AddonEntityID = addonEntityID;
        record.OffsetX = offsetX;
        record.OffsetY = offsetY;
        m_addonAttachments[mainBuildingEntityID] = record;
        return true;
    }

    bool DetachAddon(int mainBuildingEntityID) {
        return m_addonAttachments.erase(mainBuildingEntityID) > 0;
    }

    int GetAttachedAddon(int mainBuildingEntityID) const {
        const auto it = m_addonAttachments.find(mainBuildingEntityID);
        if (it == m_addonAttachments.end()) {
            return -1;
        }
        return it->second.AddonEntityID;
    }

    const AddonAttachmentRecord2D* GetAddonAttachmentRecord(int mainBuildingEntityID) const {
        const auto it = m_addonAttachments.find(mainBuildingEntityID);
        return (it != m_addonAttachments.end()) ? &it->second : nullptr;
    }

    int GetPoweredTileCount(int playerID) const {
        if (playerID < 0 || playerID >= kMaxSupportedPlayers) {
            return 0;
        }
        return m_poweredTileCountByPlayer[static_cast<std::size_t>(playerID)];
    }

    int GetCreepTileCount() const {
        return m_creepTileCount;
    }

    int GetPlacedBuildingCount() const {
        return static_cast<int>(m_placedBuildings.size());
    }

private:
    std::size_t ToIndex(int tileX, int tileY) const {
        return static_cast<std::size_t>(tileY * m_widthTiles + tileX);
    }

    static std::uint8_t GetPlayerBitMask(int playerID) {
        if (playerID < 0 || playerID >= kMaxSupportedPlayers) {
            return 0;
        }
        return static_cast<std::uint8_t>(1u << playerID);
    }

    int m_widthTiles = 0;
    int m_heightTiles = 0;
    float m_tileSizePx = 32.0f;

    std::vector<PlacementTile2D> m_tiles;
    std::unordered_map<int, PlacedBuildingRecord2D> m_placedBuildings;
    std::unordered_map<int, AddonAttachmentRecord2D> m_addonAttachments;

    std::array<int, kMaxSupportedPlayers> m_poweredTileCountByPlayer{};
    int m_creepTileCount = 0;
};

} // namespace Engine::Gameplay
