#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Resource/GpuImage.h"

namespace Game::Client
{

constexpr float TileSize = 32.0f;

enum class UnitSize : uint8_t
{
    Small,
    Medium,
    Large
};

enum class EntityKind : uint8_t
{
    Unit,
    Building,
    Resource,
    Effect
};

struct EntityImageNode
{
    int ImageId = 0;
    int ParentIndex = -1;
    int FrameIndex = 0;
    int FrameCount = 0;
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
    bool DrawAbove = true;
    bool Flipped = false;
    bool Visible = true;
    const std::vector<Resource::Graphics::GpuImage>* Frames = nullptr;
};

struct Entity
{
    int Id = 0;
    EntityKind Kind = EntityKind::Unit;
    float X = 0.0f;
    float Y = 0.0f;
    int ChkUnitId = 0;
    int ImageId = 0;
    int PlayerId = 0;
    int HitPoints = 0;
    int MaxHitPoints = 0;
    UnitSize Size = UnitSize::Medium;
    int ResourceAmount = 0;  // 0 = not a resource
    int FrameIndex = 0;
    int FrameCount = 0;
    bool Flipped = false;
    bool Visible = true;

    // Cached image frames (one per GRP frame; null if not yet loaded)
    const std::vector<Resource::Graphics::GpuImage>* Frames = nullptr;
    std::vector<EntityImageNode> ImageNodes;
};

struct StartLocation
{
    int TileX = 0;
    int TileY = 0;
    int PlayerId = 0;
};

struct GameMap
{
    int Width = 0;
    int Height = 0;
    std::vector<uint16_t> MegaTileIds;   // 16-bit megatile index per tile
    std::vector<uint8_t> Walkable;       // 1 byte per tile
    std::vector<StartLocation> StartLocations;
    std::string TilesetName;
};

class EntityManager
{
public:
    void Clear();
    int AddEntity(EntityKind Kind);
    Entity& GetEntity(int Id);
    const Entity& GetEntity(int Id) const;
    int GetCount() const { return static_cast<int>(mEntities.size()); }
    std::vector<Entity>& GetAll() { return mEntities; }
    const std::vector<Entity>& GetAll() const { return mEntities; }

    int SpawnStartUnits(const GameMap& Map, int PlayerId, int SpawnX, int SpawnY,
                        int CommandCenterChkId, int ScvChkId, int Frame0CommandCenter, int Frame0Scv);

    int SpawnCommandCenter(int PlayerId, int WorldX, int WorldY, int ChkId, int FrameIndex);
    int SpawnScv(int PlayerId, int WorldX, int WorldY, int ChkId, int FrameIndex);

private:
    std::vector<Entity> mEntities;
    int mNextId = 1;
};

} // namespace Game::Client
