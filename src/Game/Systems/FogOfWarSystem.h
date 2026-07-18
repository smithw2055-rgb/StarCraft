#pragma once

#include <vector>
#include <cstdint>

#include "Core/ECS/SystemManager.h"
#include "Core/Math/Vec2.h"

namespace Game::Map { class GameMap; }

namespace Game::Systems
{

enum class FogState : uint8_t
{
    Unexplored = 0,
    Explored,
    Visible
};

struct LastSeenSnapshot
{
    int UnitEntityID = -1;
    int ChkUnitID = 0;
    Core::Math::Vec2 Position;
    int HitPoints = 0;
    int MaxHitPoints = 0;
    int PlayerID = 0;
    float TimeStamp = 0.0f;
    bool Valid = false;
};

class FogOfWarSystem : public Core::ECS::ISystem
{
public:
    FogOfWarSystem(Core::ECS::SystemManager& SysMgr, const Game::Map::GameMap* Map);
    void Update(float DeltaTime) override;
    std::string_view GetName() const override { return "FogOfWarSystem"; }

    FogState GetFog(int TileX, int TileY, int PlayerID = 0) const;
    bool IsVisible(int TileX, int TileY, int PlayerID = 0) const;
    bool IsExplored(int TileX, int TileY, int PlayerID = 0) const;

    // Simulation consumers such as AI must not inherit the local observer's
    // presentation-only reveal-all state.
    FogState GetGameplayFog(int TileX, int TileY, int PlayerID = 0) const;
    bool IsGameplayVisible(int TileX, int TileY, int PlayerID = 0) const;

    // Detector visibility
    bool IsDetected(int TileX, int TileY, int PlayerID = 0) const;
    bool IsGameplayDetected(int TileX, int TileY, int PlayerID = 0) const;

    // Last-seen snapshots for ghost rendering
    const std::vector<LastSeenSnapshot>& GetLastSeenSnapshots() const { return mLastSeen; }
    void SetAlliance(int PlayerA, int PlayerB, bool Allied);
    bool AreAllied(int PlayerA, int PlayerB) const;
    void SetLocalPlayerID(int PlayerID) { mLocalPlayerID = PlayerID; }
    void SetObserverRevealAll(bool Enabled) { mObserverRevealAll = Enabled; }

    int GetWidth() const { return mMapWidth; }
    int GetHeight() const { return mMapHeight; }

private:
    void RevealAround(int CenterX, int CenterY, int SightRange, uint8_t* Grid, uint8_t* DetectorGrid);
    void ResetVisibility();
    void UpdateLastSeen(Core::ECS::EntityManager& EM);

    Core::ECS::SystemManager& mSystemMgr;
    const Game::Map::GameMap* mMap = nullptr;
    int mMapWidth = 0;
    int mMapHeight = 0;

    // Per-player fog grids: 0=Unexplored, 1=Explored, 2=Visible
    std::vector<uint8_t> mFogGrid;

    // Per-player detector coverage grid
    std::vector<uint8_t> mDetectorGrid;

    // Alliance matrix: [playerA * maxPlayers + playerB] = true if allied
    std::vector<bool> mAlliance;

    // Last-seen snapshots for ghost rendering
    std::vector<LastSeenSnapshot> mLastSeen;

    int mMaxPlayers = 8;
    int mLocalPlayerID = 0;
    bool mObserverRevealAll = false;
};

} // namespace Game::Systems
