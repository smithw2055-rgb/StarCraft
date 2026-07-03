#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Render/GpuTextureHandle.h"

namespace Render
{

// 5x7 bitmap ASCII font. Atlas is 16x16 grid of 6x8 cells (96x128 px).
// 1-pixel pad on right + bottom of each cell.
class BitmapFont
{
public:
    bool Init();
    void Shutdown();

    // Returns atlas image (RGBA8). Cell index from `GlyphUV` retrieves the
    // sub-rect UV bounds for a given char (0..94).
    const GpuTextureHandle& GetTexture() const { return mTexture; }
    int GetCellW() const { return CellW; }
    int GetCellH() const { return CellH; }
    int GetAtlasW() const { return AtlasW; }
    int GetAtlasH() const { return AtlasH; }

    // Computes UV bounds for a given ASCII char (space for unsupported).
    // Returns uv[4] = {u0, v0, u1, v1}
    void GetCharUV(char C, float UV[4]) const;

    // Measures text width in pixels at font scale 1.0.
    int MeasureText(const std::string& Text) const;

    static constexpr int CellW = 6;
    static constexpr int CellH = 8;
    static constexpr int Cols = 16;
    static constexpr int Rows = 8;        // covers chars 0..127 â†?only 0..95 used
    static constexpr int AtlasW = CellW * Cols;  // 96
    static constexpr int AtlasH = CellH * Rows;  // 64
    static constexpr int FirstChar = 32;  // space

private:
    GpuTextureHandle mTexture;
};

} // namespace Render
