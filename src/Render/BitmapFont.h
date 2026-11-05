#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "Render/GpuTextureHandle.h"

namespace Render
{

// Bitmap font supporting ASCII 5x7 and dynamic Chinese/Unicode glyphs.
// Atlas is 512x512.
class BitmapFont
{
public:
    bool Init();
    void Shutdown();

    const GpuTextureHandle& GetTexture() const { return mTexture; }
    int GetCellW() const { return CellW; }
    int GetCellH() const { return CellH; }
    int GetAtlasW() const { return AtlasW; }
    int GetAtlasH() const { return AtlasH; }

    void GetCharUV(char C, float UV[4]) const;
    bool GetGlyphUV(uint32_t Codepoint, float UV[4], bool& OutIsWide);
    void UploadPendingGlyphs();
    int MeasureText(const std::string& Text) const;

    static constexpr int CellW = 6;
    static constexpr int CellH = 8;
    static constexpr int Cols = 16;
    static constexpr int Rows = 8;
    static constexpr int AtlasW = 512;
    static constexpr int AtlasH = 512;
    static constexpr int MaxUnicodeSlots = 32 * 28; // 896

private:
    void RenderChineseGlyph(uint32_t Codepoint, int SlotX, int SlotY);
    bool CreateGpuTexture();

    GpuTextureHandle mTexture;
    std::vector<uint8_t> mPixels;
    std::unordered_map<uint32_t, int> mUnicodeSlots;
    std::vector<uint32_t> mSlotToCodepoint;
    int mNextSlot = 0;
    bool mAtlasDirty = false;
};

uint32_t NextUtf8Codepoint(const std::string& S, size_t& Index);

} // namespace Render
