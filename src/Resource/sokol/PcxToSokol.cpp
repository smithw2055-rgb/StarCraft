#include "Resource/sokol/PcxToSokol.h"

#include "Core/Utils/Logger.h"

#include <algorithm>
#include <cstring>
#include <cstdint>

namespace Resource::SokolBridge
{

#pragma pack(push, 1)
struct PcxHeader
{
    uint8_t  Manufacturer;
    uint8_t  Version;
    uint8_t  Encoding;
    uint8_t  BitsPerPixel;
    uint16_t XMin, YMin, XMax, YMax;
    uint16_t HDpi, VDpi;
    uint8_t  ColorMap[48];
    uint8_t  Reserved;
    uint8_t  ColorPlanes;
    uint16_t BytesPerLine;
    uint16_t PaletteType;
    uint16_t HScrSize, VScrSize;
    uint8_t  Padding[54];
};
#pragma pack(pop)

static bool IsRed(uint8_t R, uint8_t G, uint8_t B)
{
    return R >= 220 && G < 40 && B < 40;
}

std::optional<DecodedPcx> DecodePcxToRgba(const std::vector<uint8_t>& Data)
{
    if (Data.size() < sizeof(PcxHeader) + 256 * 3 + 1) return std::nullopt;

    const PcxHeader* Hdr = reinterpret_cast<const PcxHeader*>(Data.data());
    if (Hdr->Manufacturer != 10 || Hdr->Version < 5) return std::nullopt;

    int Width = Hdr->XMax - Hdr->XMin + 1;
    int Height = Hdr->YMax - Hdr->YMin + 1;
    int Bpl = Hdr->BytesPerLine;
    if (Bpl == 0 || Bpl < Width) Bpl = Width;

    if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096) return std::nullopt;

    std::vector<uint8_t> Pixels(Width * Height, 0);
    size_t SrcPos = sizeof(PcxHeader);
    int Planes = Hdr->ColorPlanes ? Hdr->ColorPlanes : 1;

    for (int Y = 0; Y < Height; Y++)
    {
        for (int P = 0; P < Planes; P++)
        {
            int X = 0;
            int RowOff = Y * Width;
            while (X < Bpl && SrcPos < Data.size())
            {
                uint8_t Byte = Data[SrcPos++];
                if ((Byte & 0xC0) == 0xC0)
                {
                    int Count = Byte & 0x3F;
                    uint8_t Value = (SrcPos < Data.size()) ? Data[SrcPos++] : 0;
                    for (int R = 0; R < Count && X < Bpl; R++, X++)
                    {
                        if (X < Width && RowOff + X < Width * Height)
                            Pixels[RowOff + X] = Value;
                    }
                }
                else if (X < Width && RowOff + X < Width * Height)
                {
                    Pixels[RowOff + X] = Byte;
                    X++;
                }
                else X++;
            }
        }
    }

    const uint8_t* PalStart = nullptr;
    for (size_t I = Data.size() - 256 * 3 - 1; I < Data.size(); I++)
    {
        if (Data[I] == 0x0C && I + 1 + 256 * 3 <= Data.size())
        {
            PalStart = &Data[I + 1];
            break;
        }
    }
    if (!PalStart) return std::nullopt;

    int RedIdx = -1;
    for (int I = 1; I < 256; I++)
    {
        if (IsRed(PalStart[I * 3 + 0], PalStart[I * 3 + 1], PalStart[I * 3 + 2]))
        { RedIdx = I; break; }
    }

    std::vector<bool> IsRedPixel(Width * Height, false);
    for (int I = 0; I < Width * Height; I++)
    {
        uint8_t Idx = Pixels[I];
        IsRedPixel[I] = (Idx == RedIdx) || (RedIdx == -1 && Idx != 0 &&
            IsRed(PalStart[Idx * 3 + 0], PalStart[Idx * 3 + 1], PalStart[Idx * 3 + 2]));
    }

    struct Rect { int X1 = 9999, Y1 = 9999, X2 = 0, Y2 = 0; bool Active = false; };
    std::vector<Rect> Rects;
    for (int Y = 0; Y < Height; Y++)
    {
        for (int X = 0; X < Width; X++)
        {
            if (IsRedPixel[Y * Width + X])
            {
                int RX1 = X, RY1 = Y, RX2 = X, RY2 = Y;
                while (RX2 + 1 < Width && IsRedPixel[Y * Width + RX2 + 1]) RX2++;
                for (int DY = RY1; DY <= RY2 + 8 && DY < Height; DY++)
                {
                    int RowRed = 0;
                    for (int DX = RX1; DX <= RX2; DX++)
                        if (DY < Height && IsRedPixel[DY * Width + DX]) RowRed++;
                    if (RowRed > (RX2 - RX1 + 1) * 3 / 4) { RY2 = DY; }
                    else break;
                }
                int RW = RX2 - RX1 + 1, RH = RY2 - RY1 + 1;
                if (RW >= 10 && RH >= 10)
                {
                    bool Merged = false;
                    for (auto& R : Rects)
                    {
                        int Ox = std::max(RX1, R.X1), Oy = std::max(RY1, R.Y1);
                        int Ox2 = std::min(RX2, R.X2), Oy2 = std::min(RY2, R.Y2);
                        if (Ox <= Ox2 && Oy <= Oy2 && (RW * RH < 2000 || R.Active))
                        {
                            R.X1 = std::min(R.X1, RX1); R.Y1 = std::min(R.Y1, RY1);
                            R.X2 = std::max(R.X2, RX2); R.Y2 = std::max(R.Y2, RY2);
                            R.Active = true;
                            Merged = true;
                            break;
                        }
                    }
                    if (!Merged)
                    {
                        Rect NR;
                        NR.X1 = RX1; NR.Y1 = RY1; NR.X2 = RX2; NR.Y2 = RY2;
                        NR.Active = true;
                        Rects.push_back(NR);
                    }
                }
                X = RX2;
            }
        }
    }

    std::sort(Rects.begin(), Rects.end(), [](const Rect& A, const Rect& B) {
        return A.Y1 < B.Y1 || (A.Y1 == B.Y1 && A.X1 < B.X1);
    });

    DecodedPcx Out;
    Out.Width = Width;
    Out.Height = Height;

    for (auto& R : Rects)
    {
        int RW = R.X2 - R.X1 + 1, RH = R.Y2 - R.Y1 + 1;
        if (RW < 10 || RH < 10) continue;
        PcxRegion PR;
        PR.X = R.X1; PR.Y = R.Y1; PR.W = RW; PR.H = RH;
        bool InBottom = (R.Y1 >= Height - 160);
        bool IsRight = (R.X1 >= Width * 2 / 3);
        if (InBottom)
        {
            if (RW < 60 && RH < 60)
                Out.Regions.CmdButtons.push_back(PR);
            else if (IsRight)
                Out.Regions.CmdButtons.push_back(PR);
            else if (RW > 80 && RH > 80)
                Out.Regions.Portrait.push_back(PR);
            else if (RW > 80)
                Out.Regions.Info.push_back(PR);
            else
                Out.Regions.Minimap.push_back(PR);
        }
        else
        {
            if (RW > 100)
                Out.Regions.Resources.push_back(PR);
        }
    }

    Out.Rgba.resize(static_cast<size_t>(Width) * static_cast<size_t>(Height) * 4);
    for (int I = 0; I < Width * Height; I++)
    {
        uint8_t Idx = Pixels[I];
        bool Transparent = IsRedPixel[I] || Idx == 0;
        Out.Rgba[I * 4 + 0] = PalStart[Idx * 3 + 0];
        Out.Rgba[I * 4 + 1] = PalStart[Idx * 3 + 1];
        Out.Rgba[I * 4 + 2] = PalStart[Idx * 3 + 2];
        Out.Rgba[I * 4 + 3] = Transparent ? 0 : 255;
    }

    return Out;
}

} // namespace Resource::SokolBridge
