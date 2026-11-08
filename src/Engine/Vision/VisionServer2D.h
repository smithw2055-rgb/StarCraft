#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Core/Math/Vec2.h"

namespace Engine::Vision
{

// ============================================================================
// VisionServer2D (Phase 73):
// Referenced from Godot 4 PointLight2D / Visibility Occluder & Unity RTS
// Line-of-Sight / Fog-of-War Grid Subsystem:
// - General-purpose multi-player 2D tile visibility (Unexplored, Explored,
//   Visible), shared alliance vision masks, high-ground cliff elevation
//   occlusion, detector revelation grid, and GPU FoW LUT export.
// ============================================================================
enum class TileVisibilityState : uint8_t
{
    Unexplored = 0,
    Explored = 1,
    Visible = 2
};

struct VisionRevealer2D
{
    int32_t OwnerPlayerID = 0;
    int32_t TileX = 0;
    int32_t TileY = 0;
    int32_t SightRadiusTiles = 7;
    uint8_t ElevationLevel = 0;
    bool IgnoresElevation = false; // True for air units / scanner sweep
    bool IsDetector = false;
    int32_t DetectorRadiusTiles = 0;
};

struct VisionTelemetry
{
    uint64_t FramesEvaluated = 0;
    uint64_t RevealersProcessed = 0;
    uint64_t DetectorStampsApplied = 0;
    uint32_t LastVisibleTileCountPlayer0 = 0;
};

class VisionServer2D
{
public:
    static constexpr int32_t kMaxPlayers = 16;

    VisionServer2D()
    {
        ResetAlliances();
    }

    void InitializeGrid(int32_t WidthTiles, int32_t HeightTiles, int32_t MaxPlayers = 8, int32_t TileSizePx = 32)
    {
        mWidth = std::max(1, WidthTiles);
        mHeight = std::max(1, HeightTiles);
        mActivePlayers = std::clamp(MaxPlayers, 1, kMaxPlayers);
        mTileSizePx = std::max(1, TileSizePx);

        const size_t TotalCells = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
        mElevationGrid.assign(TotalCells, 0u);
        mFogGrid.assign(static_cast<size_t>(mActivePlayers) * TotalCells,
                        static_cast<uint8_t>(TileVisibilityState::Unexplored));
        mDetectorGrid.assign(static_cast<size_t>(mActivePlayers) * TotalCells, 0u);
        ResetAlliances();
        ++mRevision;
    }

    void ResetAlliances() noexcept
    {
        for (int32_t P = 0; P < kMaxPlayers; ++P)
        {
            mAllianceMask[static_cast<size_t>(P)] = (1u << static_cast<uint32_t>(P));
        }
    }

    void SetSharedVisionAlliance(int32_t PlayerA, int32_t PlayerB, bool Allied) noexcept
    {
        if (PlayerA < 0 || PlayerA >= kMaxPlayers || PlayerB < 0 || PlayerB >= kMaxPlayers)
            return;
        const uint16_t BitB = static_cast<uint16_t>(1u << static_cast<uint32_t>(PlayerB));
        if (Allied)
            mAllianceMask[static_cast<size_t>(PlayerA)] |= BitB;
        else if (PlayerA != PlayerB)
            mAllianceMask[static_cast<size_t>(PlayerA)] &= static_cast<uint16_t>(~BitB);
        ++mRevision;
    }

    bool AreAllied(int32_t PlayerA, int32_t PlayerB) const noexcept
    {
        if (PlayerA < 0 || PlayerA >= kMaxPlayers || PlayerB < 0 || PlayerB >= kMaxPlayers)
            return false;
        return (mAllianceMask[static_cast<size_t>(PlayerA)] & (1u << static_cast<uint32_t>(PlayerB))) != 0u;
    }

    void SetCellElevation(int32_t TileX, int32_t TileY, uint8_t ElevationLevel) noexcept
    {
        if (!InBounds(TileX, TileY))
            return;
        mElevationGrid[CellIndex(TileX, TileY)] = ElevationLevel;
    }

    uint8_t GetCellElevation(int32_t TileX, int32_t TileY) const noexcept
    {
        if (!InBounds(TileX, TileY))
            return 0u;
        return mElevationGrid[CellIndex(TileX, TileY)];
    }

    // Demotes current Visible (2) cells to Explored (1) before stamping current-frame revealers
    void BeginVisionFrame() noexcept
    {
        for (uint8_t& State : mFogGrid)
        {
            if (State == static_cast<uint8_t>(TileVisibilityState::Visible))
                State = static_cast<uint8_t>(TileVisibilityState::Explored);
        }
        std::fill(mDetectorGrid.begin(), mDetectorGrid.end(), static_cast<uint8_t>(0));
    }

    void StampRevealer(const VisionRevealer2D& Revealer) noexcept
    {
        if (mWidth <= 0 || mHeight <= 0)
            return;
        if (Revealer.OwnerPlayerID < 0 || Revealer.OwnerPlayerID >= mActivePlayers)
            return;

        ++mTelemetry.RevealersProcessed;
        const int32_t R = std::max(0, Revealer.SightRadiusTiles);
        const int32_t R2 = R * R;
        const uint8_t ObserverElev = Revealer.IgnoresElevation
            ? 255u
            : std::max(Revealer.ElevationLevel, GetCellElevation(Revealer.TileX, Revealer.TileY));

        const uint16_t AlliedMask = mAllianceMask[static_cast<size_t>(Revealer.OwnerPlayerID)];

        const int32_t MinY = std::max(0, Revealer.TileY - R);
        const int32_t MaxY = std::min(mHeight - 1, Revealer.TileY + R);
        const int32_t MinX = std::max(0, Revealer.TileX - R);
        const int32_t MaxX = std::min(mWidth - 1, Revealer.TileX + R);

        for (int32_t Y = MinY; Y <= MaxY; ++Y)
        {
            const int32_t DY = Y - Revealer.TileY;
            for (int32_t X = MinX; X <= MaxX; ++X)
            {
                const int32_t DX = X - Revealer.TileX;
                if (DX * DX + DY * DY > R2)
                    continue;

                // High-ground occlusion: low-ground ground units cannot reveal higher elevation cliffs
                const size_t CellIdx = CellIndex(X, Y);
                if (!Revealer.IgnoresElevation && mElevationGrid[CellIdx] > ObserverElev)
                    continue;

                for (int32_t P = 0; P < mActivePlayers; ++P)
                {
                    if ((AlliedMask & (1u << static_cast<uint32_t>(P))) != 0u)
                    {
                        mFogGrid[PlayerOffset(P) + CellIdx] =
                            static_cast<uint8_t>(TileVisibilityState::Visible);
                    }
                }
            }
        }

        if (Revealer.IsDetector)
        {
            ++mTelemetry.DetectorStampsApplied;
            const int32_t DetR = Revealer.DetectorRadiusTiles > 0 ? Revealer.DetectorRadiusTiles : R;
            const int32_t DetR2 = DetR * DetR;
            const int32_t DMinY = std::max(0, Revealer.TileY - DetR);
            const int32_t DMaxY = std::min(mHeight - 1, Revealer.TileY + DetR);
            const int32_t DMinX = std::max(0, Revealer.TileX - DetR);
            const int32_t DMaxX = std::min(mWidth - 1, Revealer.TileX + DetR);

            for (int32_t Y = DMinY; Y <= DMaxY; ++Y)
            {
                const int32_t DY = Y - Revealer.TileY;
                for (int32_t X = DMinX; X <= DMaxX; ++X)
                {
                    const int32_t DX = X - Revealer.TileX;
                    if (DX * DX + DY * DY > DetR2)
                        continue;
                    const size_t CellIdx = CellIndex(X, Y);
                    for (int32_t P = 0; P < mActivePlayers; ++P)
                    {
                        if ((AlliedMask & (1u << static_cast<uint32_t>(P))) != 0u)
                            mDetectorGrid[PlayerOffset(P) + CellIdx] = 1u;
                    }
                }
            }
        }
    }

    void EvaluateFrame(const std::vector<VisionRevealer2D>& Revealers) noexcept
    {
        BeginVisionFrame();
        for (const auto& Rev : Revealers)
            StampRevealer(Rev);

        uint32_t CountP0 = 0;
        const size_t TotalCells = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
        if (mActivePlayers > 0 && mFogGrid.size() >= TotalCells)
        {
            for (size_t i = 0; i < TotalCells; ++i)
            {
                if (mFogGrid[i] == static_cast<uint8_t>(TileVisibilityState::Visible))
                    ++CountP0;
            }
        }
        mTelemetry.LastVisibleTileCountPlayer0 = CountP0;
        ++mTelemetry.FramesEvaluated;
        ++mRevision;
    }

    TileVisibilityState GetTileVisibility(int32_t TileX, int32_t TileY, int32_t PlayerID = 0) const noexcept
    {
        if (mObserverRevealAll)
            return TileVisibilityState::Visible;
        return GetGameplayTileVisibility(TileX, TileY, PlayerID);
    }

    TileVisibilityState GetGameplayTileVisibility(int32_t TileX, int32_t TileY, int32_t PlayerID = 0) const noexcept
    {
        if (!InBounds(TileX, TileY) || PlayerID < 0 || PlayerID >= mActivePlayers)
            return TileVisibilityState::Unexplored;
        return static_cast<TileVisibilityState>(mFogGrid[PlayerOffset(PlayerID) + CellIndex(TileX, TileY)]);
    }

    bool IsTileVisible(int32_t TileX, int32_t TileY, int32_t PlayerID = 0) const noexcept
    {
        return GetTileVisibility(TileX, TileY, PlayerID) == TileVisibilityState::Visible;
    }

    bool IsTileExplored(int32_t TileX, int32_t TileY, int32_t PlayerID = 0) const noexcept
    {
        return GetTileVisibility(TileX, TileY, PlayerID) != TileVisibilityState::Unexplored;
    }

    bool IsTileDetected(int32_t TileX, int32_t TileY, int32_t PlayerID = 0) const noexcept
    {
        if (mObserverRevealAll)
            return true;
        return IsGameplayTileDetected(TileX, TileY, PlayerID);
    }

    bool IsGameplayTileDetected(int32_t TileX, int32_t TileY, int32_t PlayerID = 0) const noexcept
    {
        if (!InBounds(TileX, TileY) || PlayerID < 0 || PlayerID >= mActivePlayers)
            return false;
        return mDetectorGrid[PlayerOffset(PlayerID) + CellIndex(TileX, TileY)] != 0u;
    }

    bool IsWorldPosDetected(const Core::Math::Vec2& WorldPos, int32_t PlayerID = 0) const noexcept
    {
        const int32_t TX = static_cast<int32_t>(WorldPos.X) / mTileSizePx;
        const int32_t TY = static_cast<int32_t>(WorldPos.Y) / mTileSizePx;
        return IsGameplayTileDetected(TX, TY, PlayerID);
    }

    // Direct bulk import from Game::Systems::FogOfWarSystem grids
    void ImportPlayerGrids(int32_t PlayerID,
                           const uint8_t* FogBytes,
                           const uint8_t* DetectorBytes,
                           size_t CellCount) noexcept
    {
        if (PlayerID < 0 || PlayerID >= mActivePlayers)
            return;
        const size_t TotalCells = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
        const size_t CopyCount = std::min(CellCount, TotalCells);
        const size_t Off = PlayerOffset(PlayerID);
        if (FogBytes && Off + CopyCount <= mFogGrid.size())
            std::copy_n(FogBytes, CopyCount, mFogGrid.data() + Off);
        if (DetectorBytes && Off + CopyCount <= mDetectorGrid.size())
            std::copy_n(DetectorBytes, CopyCount, mDetectorGrid.data() + Off);
        ++mRevision;
    }

    // Export 8-bit GPU FoW alpha lookup texture (0 = black unexplored, 128 = explored dim, 255 = full visible)
    std::vector<uint8_t> ExportGpuFogAlphaLut(int32_t PlayerID = 0) const
    {
        const size_t TotalCells = static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
        std::vector<uint8_t> Out(TotalCells, 0u);
        if (PlayerID < 0 || PlayerID >= mActivePlayers || mFogGrid.size() < PlayerOffset(PlayerID) + TotalCells)
            return Out;
        const size_t Off = PlayerOffset(PlayerID);
        for (size_t i = 0; i < TotalCells; ++i)
        {
            const uint8_t V = mObserverRevealAll ? 2u : mFogGrid[Off + i];
            Out[i] = (V == 2u) ? 255u : ((V == 1u) ? 128u : 0u);
        }
        return Out;
    }

    void SetObserverRevealAll(bool RevealAll) noexcept
    {
        mObserverRevealAll = RevealAll;
        ++mRevision;
    }

    bool GetObserverRevealAll() const noexcept { return mObserverRevealAll; }
    int32_t GetWidth() const noexcept { return mWidth; }
    int32_t GetHeight() const noexcept { return mHeight; }
    int32_t GetActivePlayers() const noexcept { return mActivePlayers; }
    uint32_t GetRevision() const noexcept { return mRevision; }
    const VisionTelemetry& GetTelemetry() const noexcept { return mTelemetry; }

private:
    bool InBounds(int32_t X, int32_t Y) const noexcept
    {
        return X >= 0 && X < mWidth && Y >= 0 && Y < mHeight;
    }

    size_t CellIndex(int32_t X, int32_t Y) const noexcept
    {
        return static_cast<size_t>(Y) * static_cast<size_t>(mWidth) + static_cast<size_t>(X);
    }

    size_t PlayerOffset(int32_t PlayerID) const noexcept
    {
        return static_cast<size_t>(PlayerID) * static_cast<size_t>(mWidth) * static_cast<size_t>(mHeight);
    }

    int32_t mWidth = 0;
    int32_t mHeight = 0;
    int32_t mActivePlayers = 8;
    int32_t mTileSizePx = 32;
    bool mObserverRevealAll = false;
    uint32_t mRevision = 1;

    std::array<uint16_t, kMaxPlayers> mAllianceMask{};
    std::vector<uint8_t> mElevationGrid;
    std::vector<uint8_t> mFogGrid;
    std::vector<uint8_t> mDetectorGrid;
    VisionTelemetry mTelemetry{};
};

} // namespace Engine::Vision
