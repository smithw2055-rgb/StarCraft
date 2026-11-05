#include "Render/BitmapFont.h"
#include "Render/Private/SokolGpuInterop.h"

#include "Core/Utils/Logger.h"

#include <cstring>
#include <algorithm>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef DrawText
#undef DrawText
#endif
#endif

namespace Render
{

// 5x7 ASCII font, char 32..126, packed as 5 bytes per char (columns, LSB=top).
// Generated from a standard 5x7 bitmap font table.
static const uint8_t kFont5x7[][5] = {
    // 32 ' '
    {0x00,0x00,0x00,0x00,0x00},
    // 33 '!'
    {0x00,0x00,0x5F,0x00,0x00},
    // 34 '"'
    {0x00,0x07,0x00,0x07,0x00},
    // 35 '#'
    {0x14,0x7F,0x14,0x7F,0x14},
    // 36 '$'
    {0x24,0x2A,0x7F,0x2A,0x12},
    // 37 '%'
    {0x23,0x13,0x08,0x64,0x62},
    // 38 '&'
    {0x36,0x49,0x55,0x22,0x50},
    // 39 '''
    {0x00,0x05,0x03,0x00,0x00},
    // 40 '('
    {0x00,0x1C,0x22,0x41,0x00},
    // 41 ')'
    {0x00,0x41,0x22,0x1C,0x00},
    // 42 '*'
    {0x08,0x2A,0x1C,0x2A,0x08},
    // 43 '+'
    {0x08,0x08,0x3E,0x08,0x08},
    // 44 ','
    {0x00,0x50,0x30,0x00,0x00},
    // 45 '-'
    {0x08,0x08,0x08,0x08,0x08},
    // 46 '.'
    {0x00,0x60,0x60,0x00,0x00},
    // 47 '/'
    {0x20,0x10,0x08,0x04,0x02},
    // 48 '0'
    {0x3E,0x51,0x49,0x45,0x3E},
    // 49 '1'
    {0x00,0x42,0x7F,0x40,0x00},
    // 50 '2'
    {0x42,0x61,0x51,0x49,0x46},
    // 51 '3'
    {0x21,0x41,0x45,0x4B,0x31},
    // 52 '4'
    {0x18,0x14,0x12,0x7F,0x10},
    // 53 '5'
    {0x27,0x45,0x45,0x45,0x39},
    // 54 '6'
    {0x3C,0x4A,0x49,0x49,0x30},
    // 55 '7'
    {0x01,0x71,0x09,0x05,0x03},
    // 56 '8'
    {0x36,0x49,0x49,0x49,0x36},
    // 57 '9'
    {0x06,0x49,0x49,0x29,0x1E},
    // 58 ':'
    {0x00,0x36,0x36,0x00,0x00},
    // 59 ';'
    {0x00,0x56,0x36,0x00,0x00},
    // 60 '<'
    {0x00,0x08,0x14,0x22,0x41},
    // 61 '='
    {0x14,0x14,0x14,0x14,0x14},
    // 62 '>'
    {0x41,0x22,0x14,0x08,0x00},
    // 63 '?'
    {0x02,0x01,0x51,0x09,0x06},
    // 64 '@'
    {0x32,0x49,0x79,0x41,0x3E},
    // 65 'A'
    {0x7E,0x11,0x11,0x11,0x7E},
    // 66 'B'
    {0x7F,0x49,0x49,0x49,0x36},
    // 67 'C'
    {0x3E,0x41,0x41,0x41,0x22},
    // 68 'D'
    {0x7F,0x41,0x41,0x22,0x1C},
    // 69 'E'
    {0x7F,0x49,0x49,0x49,0x41},
    // 70 'F'
    {0x7F,0x09,0x09,0x01,0x01},
    // 71 'G'
    {0x3E,0x41,0x41,0x51,0x32},
    // 72 'H'
    {0x7F,0x08,0x08,0x08,0x7F},
    // 73 'I'
    {0x00,0x41,0x7F,0x41,0x00},
    // 74 'J'
    {0x20,0x40,0x41,0x3F,0x01},
    // 75 'K'
    {0x7F,0x08,0x14,0x22,0x41},
    // 76 'L'
    {0x7F,0x40,0x40,0x40,0x40},
    // 77 'M'
    {0x7F,0x02,0x04,0x02,0x7F},
    // 78 'N'
    {0x7F,0x04,0x08,0x10,0x7F},
    // 79 'O'
    {0x3E,0x41,0x41,0x41,0x3E},
    // 80 'P'
    {0x7F,0x09,0x09,0x09,0x06},
    // 81 'Q'
    {0x3E,0x41,0x51,0x21,0x5E},
    // 82 'R'
    {0x7F,0x09,0x19,0x29,0x46},
    // 83 'S'
    {0x46,0x49,0x49,0x49,0x31},
    // 84 'T'
    {0x01,0x01,0x7F,0x01,0x01},
    // 85 'U'
    {0x3F,0x40,0x40,0x40,0x3F},
    // 86 'V'
    {0x1F,0x20,0x40,0x20,0x1F},
    // 87 'W'
    {0x7F,0x20,0x18,0x20,0x7F},
    // 88 'X'
    {0x63,0x14,0x08,0x14,0x63},
    // 89 'Y'
    {0x03,0x04,0x78,0x04,0x03},
    // 90 'Z'
    {0x61,0x51,0x49,0x45,0x43},
    // 91 '['
    {0x00,0x7F,0x41,0x41,0x00},
    // 92 '\'
    {0x02,0x04,0x08,0x10,0x20},
    // 93 ']'
    {0x00,0x41,0x41,0x7F,0x00},
    // 94 '^'
    {0x04,0x02,0x01,0x02,0x04},
    // 95 '_'
    {0x40,0x40,0x40,0x40,0x40},
    // 96 '`'
    {0x00,0x01,0x02,0x04,0x00},
    // 97 'a'
    {0x20,0x54,0x54,0x54,0x78},
    // 98 'b'
    {0x7F,0x48,0x44,0x44,0x38},
    // 99 'c'
    {0x38,0x44,0x44,0x44,0x20},
    // 100 'd'
    {0x38,0x44,0x44,0x48,0x7F},
    // 101 'e'
    {0x38,0x54,0x54,0x54,0x18},
    // 102 'f'
    {0x08,0x7E,0x09,0x01,0x02},
    // 103 'g'
    {0x08,0x14,0x54,0x54,0x3C},
    // 104 'h'
    {0x7F,0x08,0x04,0x04,0x78},
    // 105 'i'
    {0x00,0x44,0x7D,0x40,0x00},
    // 106 'j'
    {0x20,0x40,0x44,0x3D,0x00},
    // 107 'k'
    {0x00,0x7F,0x10,0x28,0x44},
    // 108 'l'
    {0x00,0x41,0x7F,0x40,0x00},
    // 109 'm'
    {0x7C,0x04,0x18,0x04,0x78},
    // 110 'n'
    {0x7C,0x08,0x04,0x04,0x78},
    // 111 'o'
    {0x38,0x44,0x44,0x44,0x38},
    // 112 'p'
    {0x7C,0x14,0x14,0x14,0x08},
    // 113 'q'
    {0x08,0x14,0x14,0x18,0x7C},
    // 114 'r'
    {0x7C,0x08,0x04,0x04,0x08},
    // 115 's'
    {0x48,0x54,0x54,0x54,0x20},
    // 116 't'
    {0x04,0x3F,0x44,0x40,0x20},
    // 117 'u'
    {0x3C,0x40,0x40,0x20,0x7C},
    // 118 'v'
    {0x1C,0x20,0x40,0x20,0x1C},
    // 119 'w'
    {0x3C,0x40,0x30,0x40,0x3C},
    // 120 'x'
    {0x44,0x28,0x10,0x28,0x44},
    // 121 'y'
    {0x0C,0x50,0x50,0x50,0x3C},
    // 122 'z'
    {0x44,0x64,0x54,0x4C,0x44},
    // 123 '{'
    {0x00,0x08,0x36,0x41,0x00},
    // 124 '|'
    {0x00,0x00,0x7F,0x00,0x00},
    // 125 '}'
    {0x00,0x41,0x36,0x08,0x00},
    // 126 '~'
    {0x08,0x04,0x08,0x10,0x08},
};

static constexpr int FirstChar = 32;

bool BitmapFont::Init()
{
    mPixels.assign(AtlasW * AtlasH * 4, 0);
    int NumChars = (int)(sizeof(kFont5x7) / sizeof(kFont5x7[0]));

    for (int C = 0; C < NumChars; C++)
    {
        int Idx = C;
        int Col = Idx % Cols;
        int Row = Idx / Cols;
        int OriginX = Col * CellW;
        int OriginY = Row * CellH;

        const uint8_t* Glyph = kFont5x7[C];
        for (int X = 0; X < 5; X++)
        {
            uint8_t Bits = Glyph[X];
            for (int Y = 0; Y < 7; Y++)
            {
                if (Bits & (1 << Y))
                {
                    int Px = OriginX + X;
                    int Py = OriginY + Y;
                    int Off = (Py * AtlasW + Px) * 4;
                    mPixels[Off + 0] = 255;
                    mPixels[Off + 1] = 255;
                    mPixels[Off + 2] = 255;
                    mPixels[Off + 3] = 255;
                }
            }
        }
    }

    return CreateGpuTexture();
}

bool BitmapFont::CreateGpuTexture()
{
    if (mTexture.View.id) { sg_destroy_view(Render::ToSokol(mTexture.View)); mTexture.View = {}; }
    if (mTexture.Image.id) { sg_destroy_image(Render::ToSokol(mTexture.Image)); mTexture.Image = {}; }

    sg_image_desc Desc = {};
    Desc.width = AtlasW;
    Desc.height = AtlasH;
    Desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    Desc.usage.dynamic_update = true;
    Desc.data.mip_levels[0] = { mPixels.data(), mPixels.size() };
    Desc.label = "bitmap-font-atlas";
    mTexture.Image = Render::FromSokol(sg_make_image(&Desc));
    if (mTexture.Image.id == 0)
    {
        Core::Utils::Logger::Error("BitmapFont", "sg_make_image failed");
        return false;
    }

    sg_view_desc VDesc = {};
    VDesc.texture.image = Render::ToSokol(mTexture.Image);
    VDesc.label = "bitmap-font-view";
    mTexture.View = Render::FromSokol(sg_make_view(&VDesc));
    if (mTexture.View.id == 0)
    {
        sg_destroy_image(Render::ToSokol(mTexture.Image));
        mTexture.Image = {};
        Core::Utils::Logger::Error("BitmapFont", "sg_make_view failed");
        return false;
    }

    mAtlasDirty = false;
    return true;
}

void BitmapFont::UploadPendingGlyphs()
{
    if (!mAtlasDirty || mTexture.Image.id == 0 || mPixels.empty())
        return;

    sg_image_data Data = {};
    Data.mip_levels[0] = { mPixels.data(), mPixels.size() };
    sg_update_image(Render::ToSokol(mTexture.Image), &Data);
    mAtlasDirty = false;
}

void BitmapFont::Shutdown()
{
    if (mTexture.View.id) { sg_destroy_view(Render::ToSokol(mTexture.View)); mTexture.View = {}; }
    if (mTexture.Image.id) { sg_destroy_image(Render::ToSokol(mTexture.Image)); mTexture.Image = {}; }
    mPixels.clear();
    mUnicodeSlots.clear();
    mNextSlot = 0;
    mAtlasDirty = false;
}

void BitmapFont::GetCharUV(char C, float UV[4]) const
{
    int Idx = 0;
    if (C >= FirstChar && C <= 126) Idx = C - FirstChar;
    if (Idx < 0) Idx = 0;
    if (Idx >= 95) Idx = 0;
    int Col = Idx % Cols;
    int Row = Idx / Cols;
    float U0 = (float)(Col * CellW) / (float)AtlasW;
    float V0 = (float)(Row * CellH) / (float)AtlasH;
    float U1 = (float)(Col * CellW + 5) / (float)AtlasW;  // 5 px wide
    float V1 = (float)(Row * CellH + 7) / (float)AtlasH;  // 7 px tall
    UV[0] = U0; UV[1] = V0; UV[2] = U1; UV[3] = V1;
}

#if defined(_WIN32)
void BitmapFont::RenderChineseGlyph(uint32_t Codepoint, int SlotX, int SlotY)
{
    static HDC sHdc = nullptr;
    static HBITMAP sBmp = nullptr;
    static uint32_t* sBmpBits = nullptr;
    static HFONT sFont = nullptr;

    if (!sHdc)
    {
        HDC screenDc = GetDC(nullptr);
        sHdc = CreateCompatibleDC(screenDc);
        ReleaseDC(nullptr, screenDc);

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 16;
        bmi.bmiHeader.biHeight = -16;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        sBmp = CreateDIBSection(sHdc, &bmi, DIB_RGB_COLORS, (void**)&sBmpBits, nullptr, 0);
        SelectObject(sHdc, sBmp);

        sFont = CreateFontW(15, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
        SelectObject(sHdc, sFont);
        SetBkColor(sHdc, RGB(0, 0, 0));
        SetTextColor(sHdc, RGB(255, 255, 255));
    }

    if (!sBmpBits) return;

    std::memset(sBmpBits, 0, 16 * 16 * sizeof(uint32_t));
    wchar_t wch[2] = { static_cast<wchar_t>(Codepoint), 0 };
    RECT rect = { 0, 0, 16, 16 };
    DrawTextW(sHdc, wch, 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            uint32_t pixel = sBmpBits[y * 16 + x];
            uint8_t alpha = static_cast<uint8_t>(pixel & 0xFF);
            int dstIdx = ((SlotY + y) * AtlasW + (SlotX + x)) * 4;
            mPixels[dstIdx + 0] = 255;
            mPixels[dstIdx + 1] = 255;
            mPixels[dstIdx + 2] = 255;
            mPixels[dstIdx + 3] = alpha;
        }
    }
}
#else
void BitmapFont::RenderChineseGlyph(uint32_t, int, int) {}
#endif

bool BitmapFont::GetGlyphUV(uint32_t Codepoint, float UV[4], bool& OutIsWide)
{
    if (Codepoint <= 126)
    {
        OutIsWide = false;
        GetCharUV(static_cast<char>(Codepoint), UV);
        return true;
    }

    OutIsWide = true;
    auto It = mUnicodeSlots.find(Codepoint);
    if (It == mUnicodeSlots.end())
    {
        if (mSlotToCodepoint.size() < (size_t)MaxUnicodeSlots)
            mSlotToCodepoint.resize(MaxUnicodeSlots, 0);

        int SlotIndex = mNextSlot % MaxUnicodeSlots;
        ++mNextSlot;

        uint32_t OldCodepoint = mSlotToCodepoint[(size_t)SlotIndex];
        if (OldCodepoint != 0 && OldCodepoint != Codepoint)
        {
            mUnicodeSlots.erase(OldCodepoint);
        }

        mUnicodeSlots[Codepoint] = SlotIndex;
        mSlotToCodepoint[(size_t)SlotIndex] = Codepoint;

        int SlotCol = SlotIndex % 32;
        int SlotRow = SlotIndex / 32;
        int SlotX = SlotCol * 16;
        int SlotY = 64 + SlotRow * 16;

        RenderChineseGlyph(Codepoint, SlotX, SlotY);
        mAtlasDirty = true;
        It = mUnicodeSlots.find(Codepoint);
    }

    int SlotIndex = It->second;
    int SlotCol = SlotIndex % 32;
    int SlotRow = SlotIndex / 32;
    int SlotX = SlotCol * 16;
    int SlotY = 64 + SlotRow * 16;

    UV[0] = (float)SlotX / (float)AtlasW;
    UV[1] = (float)SlotY / (float)AtlasH;
    UV[2] = (float)(SlotX + 16) / (float)AtlasW;
    UV[3] = (float)(SlotY + 16) / (float)AtlasH;
    return true;
}

uint32_t NextUtf8Codepoint(const std::string& S, size_t& Index)
{
    if (Index >= S.size()) return 0;
    uint8_t C0 = static_cast<uint8_t>(S[Index++]);
    if (C0 < 0x80) return C0;
    if ((C0 & 0xE0) == 0xC0)
    {
        if (Index >= S.size()) return '?';
        uint8_t C1 = static_cast<uint8_t>(S[Index++]);
        return ((C0 & 0x1F) << 6) | (C1 & 0x3F);
    }
    if ((C0 & 0xF0) == 0xE0)
    {
        if (Index + 1 >= S.size()) return '?';
        uint8_t C1 = static_cast<uint8_t>(S[Index++]);
        uint8_t C2 = static_cast<uint8_t>(S[Index++]);
        return ((C0 & 0x0F) << 12) | ((C1 & 0x3F) << 6) | (C2 & 0x3F);
    }
    if ((C0 & 0xF8) == 0xF0)
    {
        if (Index + 2 >= S.size()) return '?';
        uint8_t C1 = static_cast<uint8_t>(S[Index++]);
        uint8_t C2 = static_cast<uint8_t>(S[Index++]);
        uint8_t C3 = static_cast<uint8_t>(S[Index++]);
        return ((C0 & 0x07) << 18) | ((C1 & 0x3F) << 12) | ((C2 & 0x3F) << 6) | (C3 & 0x3F);
    }
    return '?';
}

int BitmapFont::MeasureText(const std::string& Text) const
{
    int Width = 0;
    size_t Index = 0;
    while (Index < Text.size())
    {
        if (Text[Index] == '\n') { ++Index; continue; }
        uint32_t Codepoint = NextUtf8Codepoint(Text, Index);
        if (Codepoint > 126)
            Width += CellH;
        else
            Width += CellW;
    }
    return Width;
}

} // namespace Render
